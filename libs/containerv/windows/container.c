/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation ? , either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <windows.h>
#include <objbase.h>
#include <shlwapi.h>
#include <chef/platform.h>
#include <chef/containerv.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include <jansson.h>
#include <ctype.h>

#include "json-util.h"

#include "private.h"

#include "standard-mounts.h"
#include "oci-bundle.h"

#define MIN_REMAINING_PATH_LENGTH 20  // Minimum space needed for "containerv-XXXXXX" + null

// Ensure the parent directory exists for a host path.
static int __ensure_parent_dir_hostpath(const char* hostPath)
{
    char  tempPath[MAX_PATH];
    char* lastSlash;
    char* lastFSlash;
    char* sep;

    if (hostPath == NULL) {
        return -1;
    }

    memset(tempPath, 0, sizeof(tempPath));
    strncpy_s(tempPath, sizeof(tempPath), hostPath, _TRUNCATE);

    lastSlash = strrchr(tempPath, '\\');
    lastFSlash = strrchr(tempPath, '/');
    sep = lastSlash;
    if (lastFSlash != NULL && (sep == NULL || lastFSlash > sep)) {
        sep = lastFSlash;
    }
    if (sep == NULL) {
        return 0;
    }
    *sep = 0;
    if (tempPath[0] == 0) {
        return 0;
    }
    return platform_mkdir(tempPath);
}

// Create a unique runtime directory under the temp path.
static char* __container_create_runtime_dir(void)
{
    char   tempPath[MAX_PATH];
    char*  directory;
    DWORD  result;
    size_t remaining;
    
    // Get temp path
    result = GetTempPathA(MAX_PATH, tempPath);
    if (result == 0 || result > MAX_PATH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to get temp path\n");
        return NULL;
    }
    
    // Create a unique subdirectory for the container
    // strcat_s second parameter is the total buffer size, not remaining space
    remaining = MAX_PATH - strlen(tempPath);
    if (remaining < MIN_REMAINING_PATH_LENGTH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: temp path too long\n");
        return NULL;
    }
    strcat_s(tempPath, MAX_PATH, "containerv-XXXXXX");
    
    // Use _mktemp_s to create unique name
    if (_mktemp_s(tempPath, strlen(tempPath) + 1) != 0) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create unique name\n");
        return NULL;
    }
    
    // Create the directory
    if (!CreateDirectoryA(tempPath, NULL)) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create directory: %s\n", tempPath);
        return NULL;
    }
    
    directory = _strdup(tempPath);
    return directory;
}

// Allocate and initialize a new container object.
static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;
    char                         stagingPath[MAX_PATH];
    size_t                       idLen;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    container->runtime_dir = __container_create_runtime_dir();
    if (container->runtime_dir == NULL) {
        free(container);
        return NULL;
    }
    
    // Create staging directory for file transfers
    sprintf_s(stagingPath, sizeof(stagingPath), "%s\\staging", container->runtime_dir);
    if (platform_mkdir(stagingPath) != 0) {
        VLOG_WARNING("containerv", "failed to create staging directory %s\n", stagingPath);
    }
    
    // Generate container ID
    platform_guid_new_string(container->id);

    // Convert container ID to wide string for HCS
    idLen = strlen(container->id);
    container->vm_id = calloc(idLen + 1, sizeof(wchar_t));
    if (container->vm_id == NULL) {
        free(container->runtime_dir);
        free(container);
        return NULL;
    }
    
    if (MultiByteToWideChar(CP_UTF8, 0, container->id, -1, container->vm_id, (int)idLen + 1) == 0) {
        free(container->vm_id);
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    // Use container ID as hostname
    container->hostname = _strdup(container->id);
    if (container->hostname == NULL) {
        free(container->vm_id);
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    container->hcs_system = NULL;
    container->lcow_console_pipe = INVALID_HANDLE_VALUE;
    container->lcow_console_thread = NULL;
    container->lcow_gcs_listener = (uintptr_t)INVALID_SOCKET;
    container->lcow_gcs_socket = (uintptr_t)INVALID_SOCKET;
    container->lcow_gcs_next_id = 1;
    container->vm_started = 0;
    container->layers = NULL;
    list_init(&container->processes);
    container->policy = NULL;

    container->guest_is_windows = 1;

    container->hns_endpoint_id = NULL;
    container->hns_mac_address = NULL;
    container->hns_endpoint_predeclared = 0;

    return container;
}

// Return non-zero if HCS should run in LCOW mode.
static int __is_hcs_lcow_mode(const struct containerv_options* options)
{
    if (options == NULL) {
        return 0;
    }
    return (options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX);
}

// Ensure LCOW rootfs mountpoint directories exist under the host path.
static void __ensure_lcow_rootfs_mountpoint_dirs(const char* rootfsHostPath)
{
    char        stagingDir[MAX_PATH];
    char        resolverSource[MAX_PATH];
    char        resolverSeed[MAX_PATH];
    const char* s;
    char        rel[MAX_PATH];
    size_t      j;
    char        full[MAX_PATH];

    if (rootfsHostPath == NULL || rootfsHostPath[0] == '\0') {
        return;
    }

    snprintf(stagingDir, sizeof(stagingDir), "%s\\chef\\staging", rootfsHostPath);
    snprintf(resolverSource, sizeof(resolverSource), "%s\\etc\\resolv.conf", rootfsHostPath);
    snprintf(resolverSeed, sizeof(resolverSeed), "%s\\chef\\resolv.conf", rootfsHostPath);

    // Best-effort: these are only mountpoints for bind mounts.
    (void)platform_mkdir(stagingDir);
    if (!CopyFileA(resolverSource, resolverSeed, FALSE)) {
        VLOG_WARNING("containerv", "LCOW: failed to seed resolver config: %lu\n", GetLastError());
    }

    // Standard Linux mountpoints (stored as Linux-style absolute paths).
    for (const char* const* mp = containerv_standard_linux_mountpoints(); mp != NULL && *mp != NULL; ++mp) {
        s = *mp;
        if (s == NULL || s[0] == '\0') {
            continue;
        }

        // Convert "/dev/pts" -> "dev\\pts" and join under rootfs_host_path.
        j = 0;
        while (*s == '/') {
            s++;
        }
        for (; *s && j + 1 < sizeof(rel); ++s) {
            rel[j++] = (*s == '/') ? '\\' : *s;
        }
        rel[j] = '\0';
        if (rel[0] == '\0') {
            continue;
        }

        snprintf(full, sizeof(full), "%s\\%s", rootfsHostPath, rel);
        (void)platform_mkdir(full);
    }
}

struct __lcow_bind_dir_ctx {
    const struct containerv_oci_bundle_paths* paths;
};

// Prepare LCOW bind mount target directories for OCI bundle paths.
static int __lcow_prepare_bind_dir_cb(
    const char* host_path,
    const char* container_path,
    int         readonly,
    void*       user_context)
{
    struct __lcow_bind_dir_ctx* ctx;

    ctx = (struct __lcow_bind_dir_ctx*)user_context;
    (void)host_path;
    (void)readonly;

    if (ctx == NULL || ctx->paths == NULL) {
        return -1;
    }
    if (container_path == NULL || container_path[0] == '\0') {
        return 0;
    }

    if (containerv_oci_bundle_prepare_rootfs_dir(ctx->paths, container_path, 0755) != 0) {
        VLOG_WARNING("containerv", "LCOW: failed to prepare bind mount target %s\n", container_path);
        return -1;
    }

    return 0;
}

// Escape single quotes for safe inclusion in single-quoted shell strings.
static char* __escape_sh_single_quotes_alloc(const char* src)
{
    size_t len;
    size_t extra;
    char*  out;
    size_t j;
    size_t i;

    if (src == NULL) {
        return _strdup("");
    }

    len = strlen(src);
    extra = 0;
    for (i = 0; i < len; i++) {
        if (src[i] == '\'') {
            extra += 3; // ' -> '\'' (4 chars instead of 1)
        }
    }

    out = calloc(len + extra + 1, 1);
    if (out == NULL) {
        return NULL;
    }

    j = 0;
    for (i = 0; i < len; i++) {
        if (src[i] == '\'') {
            out[j++] = '\'';
            out[j++] = '\\';
            out[j++] = '\'';
            out[j++] = '\'';
        } else {
            out[j++] = src[i];
        }
    }
    out[j] = '\0';
    return out;
}

// Read the WCOW container folder's layerchain.json and expand it into the full parent chain.
static int __wcow_resolve_parent_chain(const char* rootFs, char*** parentsOut, int* countOut)
{
    char** chain = NULL;
    int    chainCount = 0;

    if (__windows_layerchain_read(rootFs, &chain, &chainCount) != 0 || chainCount == 0) {
        VLOG_ERROR("containerv", "WCOW: missing or empty layerchain.json under %s\n", rootFs);
        __windows_strv_free(chain, chainCount);
        return -1;
    }

    if (__windows_layerchain_expand((const char* const*)chain, chainCount, parentsOut, countOut) != 0) {
        VLOG_ERROR("containerv", "WCOW: parent layer chain validation/expansion failed for %s\n", rootFs);
        __windows_strv_free(chain, chainCount);
        return -1;
    }

    // Persist the expanded chain so later runs (and other tools) see the full list.
    if (*countOut != chainCount &&
        __windows_layerchain_write(rootFs, (const char* const*)*parentsOut, *countOut) != 0) {
        VLOG_WARNING("containerv", "WCOW: failed to rewrite layerchain.json with expanded chain under %s\n", rootFs);
    }

    __windows_strv_free(chain, chainCount);
    return 0;
}

// Derive a UtilityVM path from options or parent layers.
static char* __derive_utilityvm_path(
    const struct containerv_options* options,
    const char* const*               parentLayers,
    int                              parentLayerCount)
{
    char        candidate[MAX_PATH];
    int         rc;

    // Caller requested Hyper-V isolation.
    if (options != NULL && options->windows_container.utilityvm_path != NULL && options->windows_container.utilityvm_path[0] != '\0') {
        return _strdup(options->windows_container.utilityvm_path);
    }

    // Best-effort: scan from base-most to top-most and pick the first existing "UtilityVM".
    // In practice it's usually on the base OS layer, but this is more robust when chains are incomplete.
    if (parentLayers == NULL || parentLayerCount <= 0) {
        return NULL;
    }

    for (int i = parentLayerCount - 1; i >= 0; i--) {
        const char* base = parentLayers[i];
        if (base == NULL || base[0] == '\0') {
            continue;
        }

        rc = snprintf(candidate, sizeof(candidate), "%s\\UtilityVM", base);
        if (rc < 0 || (size_t)rc >= sizeof(candidate)) {
            continue;
        }

        if (containerv_disk_path_is_directory(candidate)) {
            return _strdup(candidate);
        }
    }

    return NULL;
}

// Validate a UtilityVM path and provide a reason on failure.
static int __validate_utilityvm_path(const char* path, char* reason, size_t reasonCap)
{
    char  vhdx[MAX_PATH];
    char  filesDir[MAX_PATH];
    int   rv;
    int   rf;
    int   vhdxExists;
    int   filesExists;

    if (reason && reasonCap > 0) {
        reason[0] = '\0';
    }

    if (path == NULL || path[0] == '\0') {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM path is empty");
        }
        return 0;
    }

    if (!containerv_disk_path_is_directory(path)) {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM path is not a directory");
        }
        return 0;
    }

    rv = snprintf(vhdx, sizeof(vhdx), "%s\\UtilityVM.vhdx", path);
    rf = snprintf(filesDir, sizeof(filesDir), "%s\\Files", path);
    if (rv < 0 || (size_t)rv >= sizeof(vhdx) || rf < 0 || (size_t)rf >= sizeof(filesDir)) {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM path is too long");
        }
        return 0;
    }

    vhdxExists = PathFileExistsA(vhdx) ? 1 : 0;
    filesExists = containerv_disk_path_is_directory(filesDir);
    if (!vhdxExists && !filesExists) {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM missing UtilityVM.vhdx and Files directory");
        }
        return 0;
    }

    return 1;
}

// Format a UtilityVM candidate path from a base path.
static char* __format_utilityvm_candidate(const char* base)
{
    char candidate[MAX_PATH];
    int  rc;

    if (base == NULL || base[0] == '\0') {
        return NULL;
    }

    rc = snprintf(candidate, sizeof(candidate), "%s\\UtilityVM", base);
    if (rc < 0 || (size_t)rc >= sizeof(candidate)) {
        return NULL;
    }

    return _strdup(candidate);
}

// Release resources associated with a container instance.
// Release the handle owned by a tracked process entry and free the entry.
static void __container_process_free(struct containerv_container_process* proc)
{
    if (proc->handle != NULL) {
        if (proc->is_lcow_gcs) {
            free(proc->handle);
        } else if (g_hcs.HcsCloseProcess != NULL) {
            g_hcs.HcsCloseProcess((HCS_PROCESS)proc->handle);
        } else {
            CloseHandle(proc->handle);
        }
    }
    free(proc);
}

static struct containerv_container_process* __container_process_find(
    struct containerv_container* container,
    HANDLE                       handle)
{
    struct list_item* i;

    for (i = container->processes.head; i != NULL; i = i->next) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->handle == handle) {
            return proc;
        }
    }
    return NULL;
}

static void __container_delete(struct containerv_container* container)
{
    struct list_item* i;
    
    if (!container) {
        return;
    }

    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;
        __container_process_free(proc);
    }

    if (container->hcs_system != NULL) {
        __hcs_destroy_compute_system(container);
    }

    // Closing the job object triggers JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
    if (container->job_object != NULL) {
        CloseHandle(container->job_object);
        container->job_object = NULL;
    }

    if (container->lcow_console_thread != NULL) {
        if (WaitForSingleObject(container->lcow_console_thread, 2000) == WAIT_TIMEOUT &&
            container->lcow_console_pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(container->lcow_console_pipe);
            container->lcow_console_pipe = INVALID_HANDLE_VALUE;
            WaitForSingleObject(container->lcow_console_thread, 2000);
        }
        CloseHandle(container->lcow_console_thread);
        container->lcow_console_thread = NULL;
    }

    if (container->lcow_console_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(container->lcow_console_pipe);
        container->lcow_console_pipe = INVALID_HANDLE_VALUE;
    }
    
    free(container->vm_id);
    free(container->hostname);
    free(container->runtime_dir);
    free(container->rootfs);

    if (container->policy != NULL) {
        containerv_policy_delete(container->policy);
        container->policy = NULL;
    }
    free(container);
}

int containerv_create(
    const char*                   containerId,
    struct containerv_options*    options,
    struct containerv_container** containerOut)
{
    struct containerv_container* container;
    const char*                  rootFs;
    HRESULT                      hr;
    BOOL                         rootfsExists;
    char**                       parentLayers;
    int                          parentLayerCount;
    int                          hv;
    char*                        utilityVm;
    const char*                  baseLayer;
    char*                        candidate;
    char                         reasonBuf[256];
    const char*                  imagePath;
    struct containerv_oci_bundle_paths bundlePaths;
    const char*                  lcowRootfsHost;
    
    VLOG_DEBUG("containerv", "containerv_create(containerId=%s)\n", containerId);
    
    if (containerId == NULL || containerOut == NULL) {
        return -1;
    }

    if (options == NULL) {
        VLOG_ERROR("containerv", "containerv_create: options are required on Windows\n");
        errno = EINVAL;
        return -1;
    }
    
    container = __container_new();
    if (container == NULL) {
        VLOG_ERROR("containerv", "containerv_create: failed to allocate container\n");
        return -1;
    }
    
    rootFs = containerv_layers_get_rootfs(options->layers);
    container->rootfs = _strdup(rootFs);
    if (container->rootfs == NULL) {
        __container_delete(container);
        return -1;
    }

    // Track whether the guest rootfs is expected to be Windows or Linux.
    // For HCS container mode, this is controlled explicitly by options->windows_container_type.
    container->guest_is_windows = __is_hcs_lcow_mode(options) ? 0 : 1;

    // Rootfs preparation for HCS container mode expects BASE_ROOTFS to point at a
    // pre-prepared windowsfilter container folder (WCOW) or an OCI rootfs (LCOW).
    rootfsExists = PathFileExistsA(rootFs);
    if (!rootfsExists) {
        if (__is_hcs_lcow_mode(options)) {
            VLOG_ERROR("containerv", "containerv_create: LCOW requires an existing rootfs path at %s\n", rootFs);
            __container_delete(container);
            errno = ENOENT;
            return -1;
        } else {
            VLOG_ERROR("containerv", "containerv_create: HCS container mode requires an existing windowsfilter container folder at %s\n", rootFs);
            __container_delete(container);
            return -1;
        }
    }
    if (__is_hcs_lcow_mode(options) && rootfsExists) {
        __ensure_lcow_rootfs_mountpoint_dirs(rootFs);
    }
    
    // Initialize COM for HyperV operations
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        VLOG_ERROR("containerv", "containerv_create: failed to initialize COM: 0x%lx\n", hr);
        __container_delete(container);
        return -1;
    }
    
    {
        if (!__is_hcs_lcow_mode(options)) {
            if (!__windows_layerchain_exists(rootFs)) {
                VLOG_ERROR(
                    "containerv",
                    "containerv_create: HCS container mode requires a windowsfilter folder with layerchain.json at %s (VAFS/overlay materialization is not supported)\n",
                    rootFs);
                __container_delete(container);
                return -1;
            }

            // True Windows containers (WCOW): rootfs must be a windowsfilter container folder.
            parentLayers = NULL;
            parentLayerCount = 0;
            if (__wcow_resolve_parent_chain(rootFs, &parentLayers, &parentLayerCount) != 0) {
                __container_delete(container);
                return -1;
            }

            hv = (options && options->windows_container.isolation == WINDOWS_CONTAINER_ISOLATION_HYPERV);
            utilityVm = NULL;
            if (hv) {
                utilityVm = __derive_utilityvm_path(options, (const char* const*)parentLayers, parentLayerCount);
                if (utilityVm == NULL) {
                    baseLayer = (parentLayers != NULL && parentLayerCount > 0) ? parentLayers[parentLayerCount - 1] : NULL;
                    candidate = __format_utilityvm_candidate(baseLayer);
                    if (candidate != NULL) {
                        VLOG_ERROR(
                            "containerv",
                            "containerv_create: Hyper-V isolation requires UtilityVM path (set via containerv_options_set_windows_container_utilityvm_path or ensure base layer has UtilityVM at %s)\n",
                            candidate);
                    } else {
                        VLOG_ERROR("containerv", "containerv_create: Hyper-V isolation requires UtilityVM path (set via containerv_options_set_windows_container_utilityvm_path or ensure base layer has UtilityVM)\n");
                    }
                    free(candidate);
                    __windows_strv_free(parentLayers, parentLayerCount);
                    __container_delete(container);
                    errno = ENOENT;
                    return -1;
                }

                if (!__validate_utilityvm_path(utilityVm, reasonBuf, sizeof(reasonBuf))) {
                    VLOG_ERROR(
                        "containerv",
                        "containerv_create: UtilityVM validation failed for %s (%s)\n",
                        utilityVm,
                        reasonBuf[0] ? reasonBuf : "invalid UtilityVM path");
                    free(utilityVm);
                    __windows_strv_free(parentLayers, parentLayerCount);
                    __container_delete(container);
                    errno = ENOENT;
                    return -1;
                }
            }

            // Create WCOW container compute system.
            if (__hcs_create_container_system(container, options, rootFs, (const char* const*)parentLayers, parentLayerCount, utilityVm, 0) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to create HCS container compute system\n");
                free(utilityVm);
                __windows_strv_free(parentLayers, parentLayerCount);
                __container_delete(container);
                return -1;
            }

            free(utilityVm);
            __windows_strv_free(parentLayers, parentLayerCount);
        } else {
            // LCOW container compute system (bring-up scaffolding): uses ContainerType=Linux and HvRuntime.
            // NOTE: OCI spec + rootfs plumbing is added in a subsequent step.
            imagePath = (options && options->windows_lcow.image_path) ? options->windows_lcow.image_path : NULL;
            if (imagePath == NULL || imagePath[0] == '\0') {
                VLOG_ERROR("containerv", "containerv_create: LCOW requires HvRuntime.ImagePath (set via containerv_options_set_windows_lcow_hvruntime)\n");
                __container_delete(container);
                errno = ENOENT;
                return -1;
            }

            // If a host rootfs was provided, prepare a per-container OCI bundle under runtime_dir.
            // This gives us a stable rootfs directory for mapping into the UVM.
            memset(&bundlePaths, 0, sizeof(bundlePaths));

            lcowRootfsHost = NULL;
            if (rootfsExists) {
                if (containerv_oci_bundle_get_paths(container->runtime_dir, &bundlePaths) != 0) {
                    VLOG_ERROR("containerv", "containerv_create: failed to compute OCI bundle paths\n");
                    __container_delete(container);
                    return -1;
                }
                if (platform_mkdir(bundlePaths.bundle_dir) != 0) {
                    VLOG_ERROR("containerv", "containerv_create: failed to prepare OCI bundle directory\n");
                    containerv_oci_bundle_paths_delete(&bundlePaths);
                    __container_delete(container);
                    return -1;
                }
                free(bundlePaths.rootfs_dir);
                bundlePaths.rootfs_dir = platform_strdup(rootFs);
                if (bundlePaths.rootfs_dir == NULL) {
                    containerv_oci_bundle_paths_delete(&bundlePaths);
                    __container_delete(container);
                    errno = ENOMEM;
                    return -1;
                }
                (void)containerv_oci_bundle_prepare_rootfs_mountpoints(&bundlePaths);
                (void)containerv_oci_bundle_prepare_rootfs_standard_files(
                    &bundlePaths,
                    container->hostname,
                    (options && options->network.dns) ? options->network.dns : NULL);
                (void)containerv_oci_bundle_prepare_rootfs_dir(&bundlePaths, "/chef", 0755);
                (void)containerv_oci_bundle_prepare_rootfs_dir(&bundlePaths, "/chef/staging", 0755);

                if (options != NULL && options->layers != NULL) {
                    struct __lcow_bind_dir_ctx bctx = {.paths = &bundlePaths};
                    (void)containerv_layers_iterate(
                        options->layers,
                        CONTAINERV_LAYER_HOST_DIRECTORY,
                        __lcow_prepare_bind_dir_cb,
                        &bctx);
                }
                lcowRootfsHost = bundlePaths.rootfs_dir;
            }

            if (options != NULL && (options->capabilities & CV_CAP_NETWORK) &&
                __windows_prepare_hcs_container_network(container, options) != 0) {
                VLOG_WARNING("containerv", "containerv_create: failed to prepare LCOW network endpoint\n");
            }

            if (__hcs_create_container_system(container, options, lcowRootfsHost, NULL, 0, imagePath, 1) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to create LCOW HCS container compute system\n");
                containerv_oci_bundle_paths_delete(&bundlePaths);
                __container_delete(container);
                return -1;
            }

            if (lcowRootfsHost == NULL || lcowRootfsHost[0] == '\0') {
                VLOG_WARNING("containerv", "containerv_create: LCOW compute system started without a mapped rootfs; OCI process spec will be limited\n");
            } else {
                VLOG_DEBUG("containerv", "containerv_create: LCOW rootfs mapped at %s\n", lcowRootfsHost);
            }

            containerv_oci_bundle_paths_delete(&bundlePaths);
        }
    }

    // Take ownership of the policy (options may be deleted after create)
    if (options && options->policy) {
        container->policy = options->policy;
        options->policy = NULL;
    }

    if (options && options->layers) {
        container->layers = options->layers;
    }
    
    // Setup resource limits and/or security restrictions using Job Objects
    {
        // TODO: HCS processes are never assigned to this job; limits must move into the HCS document.
        int wantJob = 1;
        if (options && (options->capabilities & CV_CAP_CGROUPS)) {
            if (options->limits.memory_max || options->limits.cpu_percent || options->limits.process_count) {
                wantJob = 1;
            }
        }
        if (container->policy) {
            enum containerv_security_level level = containerv_policy_get_security_level(container->policy);
            if (level >= CV_SECURITY_RESTRICTED) {
                wantJob = 1;
            }
        }

        if (wantJob) {
            if (options) {
                container->resource_limits = options->limits;
            }
            container->job_object = __windows_create_job_object(
                container,
                (options && (options->limits.memory_max || options->limits.cpu_percent || options->limits.process_count))
                    ? &options->limits
                    : NULL);

            if (!container->job_object) {
                VLOG_WARNING("containerv", "containerv_create: failed to create job object\n");
            } else {
                VLOG_DEBUG("containerv", "containerv_create: created job object\n");
                if (container->policy) {
                    if (windows_apply_job_security(container->job_object, container->policy) != 0) {
                        VLOG_WARNING("containerv", "containerv_create: failed to apply job security\n");
                    }
                }
            }
        }
    }
    
    // Setup volumes and mounts for container
    if (options && (options->capabilities & CV_CAP_FILESYSTEM)) {
        if (__windows_setup_volumes(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: volume setup encountered issues\n");
            // Continue anyway, basic filesystem might still work
        }
    }
    
    // Configure host-side networking after compute system is created
    if (options && (options->capabilities & CV_CAP_NETWORK)) {
        if (container->hcs_system != NULL) {
            // True container compute system (WCOW/LCOW): attach HNS endpoint on the host.
            if (__windows_configure_hcs_container_network(container, options) != 0) {
                VLOG_WARNING("containerv", "containerv_create: HCS container network setup encountered issues\n");
            }
        }
    }

    VLOG_DEBUG("containerv", "containerv_create: created HCS container %s\n", container->id);
    
    *containerOut = container;
    return 0;
}

// Wait for a tracked guest process to exit and retrieve its exit code.
static int __container_process_wait(
    struct containerv_container*         container,
    struct containerv_container_process* proc,
    unsigned long*                       exitCodeOut)
{
    if (proc->is_lcow_gcs) {
        return __hcs_wait_lcow_gcs_process(
            container,
            (const struct containerv_lcow_gcs_process*)proc->handle,
            exitCodeOut);
    }

    // HCS_PROCESS handles are waitable.
    if (WaitForSingleObject(proc->handle, INFINITE) != WAIT_OBJECT_0) {
        VLOG_ERROR("containerv", "__container_process_wait: WaitForSingleObject failed: %lu\n", GetLastError());
        return -1;
    }
    return __hcs_get_process_exit_code((HCS_PROCESS)proc->handle, exitCodeOut);
}

int __containerv_spawn(
    struct containerv_container*       container,
    struct __containerv_spawn_options* options,
    HANDLE*                            handleOut)
{
    HCS_PROCESS_INFORMATION              hcsProcessInfo;
    struct containerv_container_process* proc;
    HCS_PROCESS                          hcsProcess;
    
    if (!container || !options || !options->path || container->hcs_system == NULL) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_spawn(path=%s)\n", options->path);

    hcsProcess = NULL;
    memset(&hcsProcessInfo, 0, sizeof(hcsProcessInfo));
    if (__hcs_create_process(container, options, &hcsProcess, &hcsProcessInfo) != 0) {
        VLOG_ERROR("containerv", "__containerv_spawn: HCS create process failed\n");
        return -1;
    }

    proc = calloc(1, sizeof(struct containerv_container_process));
    if (proc == NULL) {
        VLOG_ERROR("containerv", "__containerv_spawn: out of memory\n");
        if (g_hcs.HcsCloseProcess != NULL && hcsProcess != NULL) {
            g_hcs.HcsCloseProcess(hcsProcess);
        }
        return -1;
    }

    proc->handle = (HANDLE)hcsProcess;
    proc->pid = hcsProcessInfo.ProcessId;
    proc->is_lcow_gcs = (!container->guest_is_windows && hcsProcess != NULL &&
        ((struct containerv_lcow_gcs_process*)hcsProcess)->magic == CONTAINERV_LCOW_GCS_PROCESS_MAGIC);
    list_add(&container->processes, &proc->list_header);

    if (options->flags & CV_SPAWN_WAIT) {
        unsigned long exitCode = 0;
        int           waitStatus = __container_process_wait(container, proc, &exitCode);

        if (waitStatus != 0 || exitCode != 0) {
            VLOG_ERROR("containerv[hcs]", "guest process %s failed (wait=%d, exit=%lu)\n",
                options->path,
                waitStatus,
                exitCode);
            errno = ECHILD;
            return -1;
        }
    }

    if (handleOut) {
        *handleOut = proc->handle;
    }

    VLOG_DEBUG("containerv", "__containerv_spawn: spawned process via HCS (pid=%lu)\n", (unsigned long)hcsProcessInfo.ProcessId);
    return 0;
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct __containerv_spawn_options spawnOpts = {0};
    HANDLE                           handle;
    int                              status;
    char*                            argsCopy = NULL;
    char**                           argvList = NULL;
    
    if (!container || !path) {
        return -1;
    }
    
    // Validate and copy path
    size_t pathLen = strlen(path);
    if (pathLen == 0 || pathLen >= MAX_PATH) {
        VLOG_ERROR("containerv", "containerv_spawn: invalid path length\n");
        return -1;
    }
    
    spawnOpts.path = path;
    if (options) {
        spawnOpts.flags = options->flags;

        // Parse arguments string into argv.
        // Matches Linux semantics where `arguments` is a whitespace-delimited string supporting quotes.
        if (options->arguments && options->arguments[0] != '\0') {
            argsCopy = _strdup(options->arguments);
            if (argsCopy == NULL) {
                return -1;
            }
        }

        argvList = strargv(argsCopy, path, NULL);
        if (argvList == NULL) {
            free(argsCopy);
            return -1;
        }
        spawnOpts.argv = (const char* const*)argvList;

        // Environment is a NULL-terminated array of KEY=VALUE strings.
        spawnOpts.envv = options->environment;
    }

    status = __containerv_spawn(container, &spawnOpts, &handle);
    if (status == 0 && pidOut) {
        *pidOut = handle;
    }

    strargv_free(argvList);
    free(argsCopy);
    return status;
}

int __containerv_kill(struct containerv_container* container, HANDLE handle)
{
    struct containerv_container_process* proc;

    if (!container || handle == NULL) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_kill(handle=%p)\n", handle);

    proc = __container_process_find(container, handle);
    if (proc == NULL) {
        VLOG_ERROR("containerv", "__containerv_kill: unknown process handle %p\n", handle);
        return -1;
    }

    // GCS-backed LCOW processes have no host handle to terminate; they die with the UVM.
    if (!proc->is_lcow_gcs && !TerminateProcess(handle, 1)) {
        VLOG_ERROR("containerv", "__containerv_kill: TerminateProcess failed: %lu\n", GetLastError());
        return -1;
    }

    list_remove(&container->processes, &proc->list_header);
    __container_process_free(proc);
    return 0;
}

int containerv_kill(struct containerv_container* container, process_handle_t pid)
{
    return __containerv_kill(container, pid);
}

int containerv_wait(struct containerv_container* container, process_handle_t pid, int* exit_code_out)
{
    struct containerv_container_process* proc;
    unsigned long                        exitCode = 0;

    if (container == NULL || pid == NULL) {
        return -1;
    }

    proc = __container_process_find(container, (HANDLE)pid);
    if (proc == NULL) {
        VLOG_ERROR("containerv", "containerv_wait: unknown process handle %p\n", pid);
        return -1;
    }

    if (__container_process_wait(container, proc, &exitCode) != 0) {
        VLOG_ERROR("containerv", "containerv_wait: failed to wait for process\n");
        return -1;
    }

    if (exit_code_out != NULL) {
        *exit_code_out = (int)exitCode;
    }

    list_remove(&container->processes, &proc->list_header);
    __container_process_free(proc);
    return 0;
}

// Format the host and guest paths of a file in the per-container staging directory.
static void __container_staging_paths(
    struct containerv_container* container,
    const char*                  name,
    char*                        hostPath,
    char*                        guestPath,
    size_t                       length)
{
    snprintf(hostPath, length, "%s\\staging\\%s", container->runtime_dir, name);
    if (container->guest_is_windows) {
        snprintf(guestPath, length, "C:\\chef\\staging\\%s", name);
    } else {
        snprintf(guestPath, length, "/chef/staging/%s", name);
    }
}

// Copy a file between two paths inside the guest using the guest's shell.
static int __container_guest_copy(struct containerv_container* container, const char* source, const char* destination)
{
    struct containerv_spawn_options spawnOpts = {0};
    process_handle_t                processHandle;
    char                            cmd[2048];
    const char*                     shell;
    int                             exitCode = 0;

    if (container->guest_is_windows) {
        snprintf(cmd, sizeof(cmd), "/c copy /Y \"%s\" \"%s\"", source, destination);
        shell = "cmd.exe";
    } else {
        char* srcEsc = __escape_sh_single_quotes_alloc(source);
        char* dstEsc = __escape_sh_single_quotes_alloc(destination);
        if (srcEsc == NULL || dstEsc == NULL) {
            free(srcEsc);
            free(dstEsc);
            return -1;
        }
        snprintf(cmd, sizeof(cmd), "-c \"cp -f -- '%s' '%s'\"", srcEsc, dstEsc);
        free(srcEsc);
        free(dstEsc);
        shell = "/bin/sh";
    }

    spawnOpts.arguments = cmd;
    if (containerv_spawn(container, shell, &spawnOpts, &processHandle) != 0) {
        return -1;
    }
    if (containerv_wait(container, processHandle, &exitCode) != 0 || exitCode != 0) {
        VLOG_ERROR("containerv", "__container_guest_copy: %s -> %s failed (exit=%d)\n", source, destination, exitCode);
        return -1;
    }
    return 0;
}

int containerv_upload(
    struct containerv_container* container,
    const char* const*           hostPaths,
    const char* const*           containerPaths,
    int                          count
)
{
    char stageHost[MAX_PATH];
    char stageGuest[MAX_PATH];
    char tmpName[64];

    VLOG_DEBUG("containerv", "containerv_upload(count=%d)\n", count);
    
    if (!container || !hostPaths || !containerPaths || count <= 0) {
        return -1;
    }

    // Files travel through the staging directory, which is mapped into every guest.
    for (int i = 0; i < count; i++) {
        VLOG_DEBUG("containerv", "uploading: %s -> %s\n", hostPaths[i], containerPaths[i]);

        snprintf(tmpName, sizeof(tmpName), "upload-%d.tmp", i);
        __container_staging_paths(container, tmpName, stageHost, stageGuest, sizeof(stageHost));

        if (!CopyFileA(hostPaths[i], stageHost, FALSE)) {
            VLOG_ERROR("containerv", "containerv_upload: failed to stage %s: %lu\n", hostPaths[i], GetLastError());
            return -1;
        }

        if (__container_guest_copy(container, stageGuest, containerPaths[i]) != 0) {
            VLOG_ERROR("containerv", "containerv_upload: in-container copy failed for %s\n", containerPaths[i]);
            return -1;
        }
    }
    
    return 0;
}

int containerv_download(
    struct containerv_container* container,
    const char* const*           containerPaths,
    const char* const*           hostPaths,
    int                          count
)
{
    char stageHost[MAX_PATH];
    char stageGuest[MAX_PATH];
    char tmpName[64];

    VLOG_DEBUG("containerv", "containerv_download(count=%d)\n", count);
    
    if (!container || !hostPaths || !containerPaths || count <= 0) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        VLOG_DEBUG("containerv", "downloading: %s -> %s\n", containerPaths[i], hostPaths[i]);

        (void)__ensure_parent_dir_hostpath(hostPaths[i]);

        snprintf(tmpName, sizeof(tmpName), "download-%d.tmp", i);
        __container_staging_paths(container, tmpName, stageHost, stageGuest, sizeof(stageHost));

        if (__container_guest_copy(container, containerPaths[i], stageGuest) != 0) {
            VLOG_ERROR("containerv", "containerv_download: in-container stage copy failed for %s\n", containerPaths[i]);
            return -1;
        }

        if (!CopyFileA(stageHost, hostPaths[i], FALSE)) {
            VLOG_ERROR("containerv", "containerv_download: failed to copy staged file to host: %lu\n", GetLastError());
            return -1;
        }
    }
    
    return 0;
}

int containerv_guest_is_windows(struct containerv_container* container)
{
    if (container == NULL) {
        return 0;
    }
    if (container->hcs_system == NULL) {
        return 0;
    }
    return (container->guest_is_windows != 0);
}

void __containerv_destroy(struct containerv_container* container)
{
    if (!container) {
        return;
    }
    
    VLOG_DEBUG("containerv", "__containerv_destroy(id=%s)\n", container->id);
    
    // Terminate all running processes
    struct list_item* i;
    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;

        if (proc->handle != NULL && !proc->is_lcow_gcs) {
            TerminateProcess(proc->handle, 0);
        }
        __container_process_free(proc);
    }
    list_init(&container->processes);
    
    // Clean up job object for resource limits
    if (container->job_object) {
        __windows_cleanup_job_object(container->job_object);
        container->job_object = NULL;
    }
    
    // Clean up network configuration
    __windows_cleanup_network(container, NULL);
    
    // Shut down and delete the HCS compute system
    if (container->hcs_system) {
        __hcs_destroy_compute_system(container);
    }
    
    // Keep the runtime share available when collecting opt-in LCOW process logs.
    if (container->runtime_dir && getenv("CHEF_LCOW_CAPTURE_STDIO") == NULL) {
        if (platform_rmdir(container->runtime_dir) != 0) {
            VLOG_WARNING("containerv", "__containerv_destroy: failed to remove runtime dir: %s\n", strerror(errno));
        }
    }
}

int containerv_destroy(struct containerv_container* container)
{
    if (!container) {
        return -1;
    }
    
    __containerv_destroy(container);
    __container_delete(container);
    
    return 0;
}

static size_t __calculate_cmdline_length(const char* const* argv)
{
    size_t cmdlineLen = 0;

    if (argv == NULL) {
        return 0;
    }

    for (int i = 1; argv[i] != NULL; i++) {
        const char* arg = argv[i];
        int         needsQuotes = 0;
        
        // Check if argument needs quoting
        for (const char* p = arg; *p; ++p) {
            if (*p == ' ' || *p == '\t' || *p == '"') {
                needsQuotes = 1;
                break;
            }
        }
        
        cmdlineLen += 1; // space before argument
        if (needsQuotes) {
            cmdlineLen += 2; // opening and closing quotes
            // Account for escaped quotes
            for (const char* p = arg; *p; ++p) {
                cmdlineLen += (*p == '"') ? 2 : 1; // \" for quotes, 1 for others
            }
        } else {
            cmdlineLen += strlen(arg);
        }
    }
    return cmdlineLen;
}

static json_t* __build_process_oci(const char* commandPath, struct containerv_join_options* options)
{
    char*   cmdline = NULL;
    size_t  cmdlineLen;
    json_t* root = NULL;
    json_t* argsArray = NULL;

    // Build process configuration JSON
    root = json_object();
    if (root == NULL) {
        VLOG_ERROR("containerv", "__build_process_oci: failed to create JSON object\n");
        return NULL;
    }
    
    // Build command line string from argv with proper Windows quoting
    // Calculate required size first
    cmdlineLen = strlen(commandPath); // path without null terminator initially
    cmdlineLen += __calculate_cmdline_length(options->argv);
    
    cmdline = (char*)calloc(cmdlineLen + 1, 1); // +1 for null terminator
    if (cmdline == NULL) {
        VLOG_ERROR("containerv", "__build_process_oci: failed to allocate command line\n");
        goto cleanup;
    }
    
    // Build the command line
    strcpy(cmdline, commandPath);
    if (options->argv != NULL) {
        for (int i = 1; options->argv[i] != NULL; i++) {
            const char* arg = options->argv[i];
            strcat(cmdline, " ");
            
            // Check if argument needs quoting
            int needsQuotes = 0;
            for (const char* p = arg; *p; ++p) {
                if (*p == ' ' || *p == '\t' || *p == '"') {
                    needsQuotes = 1;
                    break;
                }
            }
            
            if (!needsQuotes) {
                strcat(cmdline, arg);
            } else {
                strcat(cmdline, "\"");
                // Add argument with escaped quotes
                for (const char* p = arg; *p; ++p) {
                    if (*p == '"') {
                        strcat(cmdline, "\\\"");
                    } else {
                        size_t len = strlen(cmdline);
                        cmdline[len] = *p;
                        cmdline[len + 1] = '\0';
                    }
                }
                strcat(cmdline, "\"");
            }
        }
    }
    
    json_object_set_new(root, "CommandLine", json_string(cmdline));

    // Set working directory
    if (options->cwd != NULL && strlen(options->cwd) > 0) {
        json_object_set_new(root, "WorkingDirectory", json_string(options->cwd));
    }

    // Add environment variables
    if (options->envp != NULL) {
        json_t* envObj = json_object();
        if (envObj == NULL) {
            VLOG_ERROR("containerv", "__build_process_oci: failed to create environment JSON object\n");
            goto cleanup;
        }

        for (int i = 0; options->envp[i] != NULL; i++) {
            // Parse "KEY=VALUE" format
            const char* eq = strchr(options->envp[i], '=');
            if (eq != NULL) {
                size_t keyLen = eq - options->envp[i];
                char* key = (char*)malloc(keyLen + 1);
                if (key != NULL) {
                    memcpy(key, options->envp[i], keyLen);
                    key[keyLen] = '\0';
                    json_object_set_new(envObj, key, json_string(eq + 1));
                    free(key);
                }
            }
        }
        
        json_object_set_new(root, "Environment", envObj);
    }
    
    free(cmdline);
    return root;

cleanup:
    free(cmdline);
    json_decref(root);
    return NULL;
}

int containerv_join(
    const char*                     containerId,
    const char*                     commandPath,
    struct containerv_join_options* options)
{
    wchar_t*      containerIdW = NULL;
    wchar_t*      processConfigW = NULL;
    HCS_SYSTEM    hcsSystem = NULL;
    HCS_OPERATION operation = NULL;
    HCS_PROCESS   process = NULL;
    HRESULT       hr;
    int           exitCode = -1;
    json_t*       root = NULL;
    char*         jsonUtf8 = NULL;
    PWSTR         resultDoc = NULL;
    
    VLOG_DEBUG("containerv", "containerv_join(id=%s, path=%s)\n", containerId, commandPath);
    
    if (containerId == NULL || commandPath == NULL) {
        VLOG_ERROR("containerv", "containerv_join: invalid arguments\n");
        return -1;
    }
    
    // Initialize HCS API
    if (__hcs_initialize() != 0) {
        VLOG_ERROR("containerv", "containerv_join: failed to initialize HCS API\n");
        return -1;
    }
    
    // Convert container ID to wide string
    containerIdW = __windows_utf8_to_wide_alloc(containerId);
    if (containerIdW == NULL) {
        VLOG_ERROR("containerv", "containerv_join: failed to convert container ID\n");
        goto cleanup;
    }
    
    // Open the compute system
    hr = g_hcs.HcsOpenComputeSystem(containerIdW, GENERIC_ALL, &hcsSystem);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv", "containerv_join: failed to open compute system: 0x%lx\n", hr);
        goto cleanup;
    }
    
    // Build process configuration JSON
    root = __build_process_oci(commandPath, options);
    if (root == NULL) {
        VLOG_ERROR("containerv", "containerv_join: failed to build process config JSON\n");
        goto cleanup;
    }

    // Convert JSON to string
    if (containerv_json_dumps_compact(root, &jsonUtf8)) {
        VLOG_ERROR("containerv", "containerv_join: failed to serialize JSON\n");
        goto cleanup;
    }
    
    VLOG_DEBUG("containerv", "Process config JSON: %s\n", jsonUtf8);
    
    // Convert to wide string
    processConfigW = __windows_utf8_to_wide_alloc(jsonUtf8);
    if (processConfigW == NULL) {
        VLOG_ERROR("containerv", "containerv_join: failed to convert process config\n");
        goto cleanup;
    }
    
    // Create operation
    operation = g_hcs.HcsCreateOperation(NULL, NULL);
    if (operation == NULL) {
        VLOG_ERROR("containerv", "containerv_join: failed to create operation\n");
        goto cleanup;
    }
    
    // Create process in container
    hr = g_hcs.HcsCreateProcess(hcsSystem, processConfigW, operation, NULL, &process);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv", "containerv_join: failed to create process: 0x%lx\n", hr);
        goto cleanup;
    }
    
    // Wait for process creation to complete
    if (g_hcs.HcsWaitForOperationResult != NULL) {
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        if (resultDoc != NULL) {
            LocalFree(resultDoc);
            resultDoc = NULL;
        }
        if (FAILED(hr)) {
            VLOG_ERROR("containerv", "containerv_join: process creation failed: 0x%lx\n", hr);
            goto cleanup;
        }
    }
    
    // Wait for process to complete
    if (__hcs_wait_process(process, INFINITE) != 0) {
        VLOG_ERROR("containerv", "containerv_join: failed to wait for process\n");
        goto cleanup;
    }
    
    // Get exit code
    unsigned long exitCodeUl = 0;
    if (__hcs_get_process_exit_code(process, &exitCodeUl) == 0) {
        exitCode = (int)exitCodeUl;
        VLOG_DEBUG("containerv", "containerv_join: process exited with code %d\n", exitCode);
    } else {
        VLOG_ERROR("containerv", "containerv_join: failed to get exit code\n");
        exitCode = -1; // Error - could not determine exit code
    }
    
cleanup:
    if (process != NULL) {
        g_hcs.HcsCloseProcess(process);
    }
    if (operation != NULL) {
        g_hcs.HcsCloseOperation(operation);
    }
    if (hcsSystem != NULL) {
        g_hcs.HcsCloseComputeSystem(hcsSystem);
    }
    free(containerIdW);
    free(processConfigW);
    free(jsonUtf8);
    json_decref(root);
    return exitCode;
}

const char* containerv_id(struct containerv_container* container)
{
    if (!container) {
        return NULL;
    }
    return container->id;
}
