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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 * 
 */

#include <chef/containerv/layers.h>
#include <chef/containerv.h>
#include <chef/platform.h>
#include <windows.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>
#include <jansson.h>

#include "private.h"

/**
 * @brief Layer context structure (Windows stub)
 */
struct containerv_layer_context {
    char* composed_rootfs;
    char* materialized_container_dir;
    int   composed_rootfs_is_materialized;
    struct containerv_layer* layers;
    int                      layer_count;
    // TODO: Windows HCI layer handles
    char* windowsfilter_dir;
};

typedef HRESULT (WINAPI *WclayerImportLayer_t)(
    PCWSTR layerPath,
    PCWSTR sourcePath,
    PCWSTR* parentLayerPaths,
    DWORD parentLayerPathsLength);

struct wclayer_api {
    HMODULE              module;
    WclayerImportLayer_t ImportLayer;
};

static struct wclayer_api g_wclayer = {0};

static int __wclayer_initialize(void)
{
    if (g_wclayer.module != NULL) {
        return (g_wclayer.ImportLayer != NULL) ? 0 : -1;
    }

    g_wclayer.module = LoadLibraryA("wclayer.dll");
    if (g_wclayer.module == NULL) {
        VLOG_ERROR("containerv[layers]", "failed to load wclayer.dll (Windows containers not available)\n");
        return -1;
    }

    g_wclayer.ImportLayer = (WclayerImportLayer_t)GetProcAddress(g_wclayer.module, "ImportLayer");
    if (g_wclayer.ImportLayer == NULL) {
        VLOG_ERROR("containerv[layers]", "wclayer ImportLayer not available\n");
        return -1;
    }

    return 0;
}

static int __utf8_to_wide_buf(const char* s, wchar_t* out, size_t cap)
{
    if (s == NULL || out == NULL || cap == 0) {
        return -1;
    }

    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, out, (int)cap);
    if (n <= 0) {
        return -1;
    }
    return 0;
}

static int __file_exists(const char* path)
{
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && !(attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static int __dir_exists(const char* path)
{
    DWORD attrs = GetFileAttributesA(path);
    return (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY));
}

static int __windowsfilter_has_layerchain(const char* layer_dir)
{
    if (layer_dir == NULL || layer_dir[0] == '\0') {
        return 0;
    }

    char chain_path[MAX_PATH];
    int rc = snprintf(chain_path, sizeof(chain_path), "%s\\layerchain.json", layer_dir);
    if (rc < 0 || (size_t)rc >= sizeof(chain_path)) {
        return 0;
    }

    return __file_exists(chain_path);
}

static int __write_empty_layerchain(const char* layer_dir)
{
    if (layer_dir == NULL || layer_dir[0] == '\0') {
        return -1;
    }

    char chain_path[MAX_PATH];
    int rc = snprintf(chain_path, sizeof(chain_path), "%s\\layerchain.json", layer_dir);
    if (rc < 0 || (size_t)rc >= sizeof(chain_path)) {
        return -1;
    }

    FILE* f = NULL;
    if (fopen_s(&f, chain_path, "wb") != 0 || f == NULL) {
        return -1;
    }
    fwrite("[]", 1, 2, f);
    fclose(f);
    return 0;
}

static int __write_layerchain(const char* layer_dir, const char* const* parents, int parent_count)
{
    if (layer_dir == NULL || layer_dir[0] == '\0') {
        return -1;
    }

    char chain_path[MAX_PATH];
    int rc = snprintf(chain_path, sizeof(chain_path), "%s\\layerchain.json", layer_dir);
    if (rc < 0 || (size_t)rc >= sizeof(chain_path)) {
        return -1;
    }

    json_t* arr = json_array();
    if (arr == NULL) {
        return -1;
    }

    for (int i = 0; i < parent_count; i++) {
        if (parents[i] == NULL || parents[i][0] == '\0') {
            continue;
        }
        if (json_array_append_new(arr, json_string(parents[i])) != 0) {
            json_decref(arr);
            return -1;
        }
    }

    int dump_rc = json_dump_file(arr, chain_path, JSON_COMPACT);
    json_decref(arr);
    return dump_rc == 0 ? 0 : -1;
}

static int __copy_parent_layers(
    const char* const* parents_in,
    int parent_count_in,
    char*** parents_out,
    int* parent_count_out)
{
    if (parents_out == NULL || parent_count_out == NULL) {
        return -1;
    }

    *parents_out = NULL;
    *parent_count_out = 0;

    if (parents_in == NULL || parent_count_in <= 0) {
        return 0;
    }

    char** out = calloc((size_t)parent_count_in, sizeof(char*));
    if (out == NULL) {
        return -1;
    }

    int idx = 0;
    for (int i = 0; i < parent_count_in; i++) {
        if (parents_in[i] == NULL || parents_in[i][0] == '\0') {
            continue;
        }
        out[idx++] = _strdup(parents_in[i]);
        if (out[idx - 1] == NULL) {
            for (int j = 0; j < idx - 1; j++) {
                free(out[j]);
            }
            free(out);
            return -1;
        }
    }

    if (idx == 0) {
        free(out);
        return 0;
    }

    *parents_out = out;
    *parent_count_out = idx;
    return 0;
}

static void __free_parent_layers(char** parents, int parent_count)
{
    if (parents == NULL) {
        return;
    }
    for (int i = 0; i < parent_count; i++) {
        free(parents[i]);
    }
    free(parents);
}

static int __windowsfilter_import_from_dir(
    const char* layer_dir,
    const char* source_dir,
    const char* const* parent_layers,
    int parent_layer_count)
{
    if (layer_dir == NULL || source_dir == NULL || layer_dir[0] == '\0' || source_dir[0] == '\0') {
        return -1;
    }

    if (__wclayer_initialize() != 0) {
        return -1;
    }

    if (!__dir_exists(source_dir)) {
        VLOG_ERROR("containerv[layers]", "source rootfs directory does not exist: %s\n", source_dir);
        return -1;
    }

    CreateDirectoryA(layer_dir, NULL);

    wchar_t layer_w[MAX_PATH];
    wchar_t source_w[MAX_PATH];
    if (__utf8_to_wide_buf(layer_dir, layer_w, sizeof(layer_w) / sizeof(layer_w[0])) != 0 ||
        __utf8_to_wide_buf(source_dir, source_w, sizeof(source_w) / sizeof(source_w[0])) != 0) {
        VLOG_ERROR("containerv[layers]", "failed to convert layer paths to wide strings\n");
        return -1;
    }

    wchar_t** parent_w = NULL;
    if (parent_layer_count > 0) {
        parent_w = calloc((size_t)parent_layer_count, sizeof(wchar_t*));
        if (parent_w == NULL) {
            return -1;
        }
        for (int i = 0; i < parent_layer_count; i++) {
            if (parent_layers[i] == NULL || parent_layers[i][0] == '\0') {
                parent_w[i] = NULL;
                continue;
            }
            parent_w[i] = calloc(MAX_PATH, sizeof(wchar_t));
            if (parent_w[i] == NULL ||
                __utf8_to_wide_buf(parent_layers[i], parent_w[i], MAX_PATH) != 0) {
                for (int j = 0; j <= i; j++) {
                    free(parent_w[j]);
                }
                free(parent_w);
                return -1;
            }
        }
    }

    HRESULT hr = g_wclayer.ImportLayer(layer_w, source_w, (PCWSTR*)parent_w, (DWORD)parent_layer_count);
    if (parent_w != NULL) {
        for (int i = 0; i < parent_layer_count; i++) {
            free(parent_w[i]);
        }
        free(parent_w);
    }
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[layers]", "wclayer ImportLayer failed: 0x%lx\n", hr);
        return -1;
    }

    if (__write_layerchain(layer_dir, parent_layers, parent_layer_count) != 0) {
        VLOG_ERROR("containerv[layers]", "failed to write layerchain.json to %s\n", layer_dir);
        return -1;
    }

    return 0;
}

static void __spawn_output_handler(const char* line, enum platform_spawn_output_type type)
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_DEBUG("containerv[layers]", line);
    } else {
        VLOG_ERROR("containerv[layers]", line);
    }
}

static int __create_windows_layers_dirs(const char* container_id, char** container_dir_out, char** rootfs_dir_out)
{
    if (container_dir_out == NULL || rootfs_dir_out == NULL) {
        errno = EINVAL;
        return -1;
    }

    *container_dir_out = NULL;
    *rootfs_dir_out = NULL;

    char tempPath[MAX_PATH];
    DWORD written = GetTempPathA(MAX_PATH, tempPath);
    if (written == 0 || written >= MAX_PATH) {
        errno = EIO;
        return -1;
    }

    // %TEMP%\chef-layers\<id>\rootfs
    char root[MAX_PATH];
    int rc = snprintf(root, sizeof(root), "%s\\chef-layers\\%s\\rootfs", tempPath,
                      container_id ? container_id : "unknown");
    if (rc < 0 || (size_t)rc >= sizeof(root)) {
        errno = EINVAL;
        return -1;
    }

    // Best-effort create all directories.
    char base[MAX_PATH];
    rc = snprintf(base, sizeof(base), "%s\\chef-layers", tempPath);
    if (rc < 0 || (size_t)rc >= sizeof(base)) {
        errno = EINVAL;
        return -1;
    }
    CreateDirectoryA(base, NULL);

    char idDir[MAX_PATH];
    rc = snprintf(idDir, sizeof(idDir), "%s\\%s", base, container_id ? container_id : "unknown");
    if (rc < 0 || (size_t)rc >= sizeof(idDir)) {
        errno = EINVAL;
        return -1;
    }
    CreateDirectoryA(idDir, NULL);

    CreateDirectoryA(root, NULL);

    *container_dir_out = _strdup(idDir);
    *rootfs_dir_out = _strdup(root);
    if (*container_dir_out == NULL || *rootfs_dir_out == NULL) {
        free(*container_dir_out);
        free(*rootfs_dir_out);
        *container_dir_out = NULL;
        *rootfs_dir_out = NULL;
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

static int __remove_tree_recursive(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        // Already gone.
        return 0;
    }

    if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
        if (!DeleteFileA(path)) {
            errno = EIO;
            return -1;
        }
        return 0;
    }

    char search[MAX_PATH];
    int rc = snprintf(search, sizeof(search), "%s\\*", path);
    if (rc < 0 || (size_t)rc >= sizeof(search)) {
        errno = EINVAL;
        return -1;
    }

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search, &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            const char* name = ffd.cFileName;
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
                continue;
            }

            char child[MAX_PATH];
            rc = snprintf(child, sizeof(child), "%s\\%s", path, name);
            if (rc < 0 || (size_t)rc >= sizeof(child)) {
                FindClose(hFind);
                errno = EINVAL;
                return -1;
            }

            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (__remove_tree_recursive(child) != 0) {
                    FindClose(hFind);
                    return -1;
                }
            } else {
                SetFileAttributesA(child, FILE_ATTRIBUTE_NORMAL);
                if (!DeleteFileA(child)) {
                    FindClose(hFind);
                    errno = EIO;
                    return -1;
                }
            }
        } while (FindNextFileA(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    SetFileAttributesA(path, FILE_ATTRIBUTE_NORMAL);
    if (!RemoveDirectoryA(path)) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static int __copy_tree_recursive(const char* srcDir, const char* dstDir)
{
    if (srcDir == NULL || dstDir == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Ensure destination exists
    CreateDirectoryA(dstDir, NULL);

    char search[MAX_PATH];
    int rc = snprintf(search, sizeof(search), "%s\\*", srcDir);
    if (rc < 0 || (size_t)rc >= sizeof(search)) {
        errno = EINVAL;
        return -1;
    }

    WIN32_FIND_DATAA ffd;
    HANDLE hFind = FindFirstFileA(search, &ffd);
    if (hFind == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return 0; // empty dir
        }
        errno = EIO;
        return -1;
    }

    do {
        const char* name = ffd.cFileName;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) {
            continue;
        }

        char srcPath[MAX_PATH];
        char dstPath[MAX_PATH];
        rc = snprintf(srcPath, sizeof(srcPath), "%s\\%s", srcDir, name);
        if (rc < 0 || (size_t)rc >= sizeof(srcPath)) {
            FindClose(hFind);
            errno = EINVAL;
            return -1;
        }
        rc = snprintf(dstPath, sizeof(dstPath), "%s\\%s", dstDir, name);
        if (rc < 0 || (size_t)rc >= sizeof(dstPath)) {
            FindClose(hFind);
            errno = EINVAL;
            return -1;
        }

        if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (__copy_tree_recursive(srcPath, dstPath) != 0) {
                FindClose(hFind);
                return -1;
            }
            continue;
        }

        // Best-effort: copy file. If it already exists, overwrite.
        if (!CopyFileA(srcPath, dstPath, FALSE)) {
            FindClose(hFind);
            errno = EIO;
            return -1;
        }
    } while (FindNextFileA(hFind, &ffd) != 0);

    FindClose(hFind);
    return 0;
}

static void __free_layer_copy(struct containerv_layer* layers, int layer_count)
{
    if (layers == NULL) {
        return;
    }

    for (int i = 0; i < layer_count; ++i) {
        free(layers[i].source);
        free(layers[i].target);
    }
    free(layers);
}

int containerv_layers_compose_ex(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
    const struct containerv_layers_compose_options* compose_options,
    struct containerv_layer_context** context_out)
{
    if (context_out == NULL || layers == NULL || layer_count <= 0) {
        errno = EINVAL;
        return -1;
    }

    int saw_overlay = 0;
    int base_rootfs_count = 0;
    int vafs_count = 0;
    const char* base_rootfs = NULL;

    for (int i = 0; i < layer_count; ++i) {
        switch (layers[i].type) {
            case CONTAINERV_LAYER_BASE_ROOTFS:
                base_rootfs_count++;
                if (base_rootfs == NULL) {
                    base_rootfs = layers[i].source;
                }
                break;
            case CONTAINERV_LAYER_VAFS_PACKAGE:
                vafs_count++;
                break;
            case CONTAINERV_LAYER_OVERLAY:
                saw_overlay = 1;
                break;
            case CONTAINERV_LAYER_HOST_DIRECTORY:
            default:
                break;
        }
    }

    // Windows backend supports:
    // - Exactly one BASE_ROOTFS, plus optional VAFS_PACKAGE layers applied on top by materialization, OR
    // - One or more VAFS_PACKAGE layers materialized into a directory (no BASE_ROOTFS).
    // OVERLAY layers are ignored (no overlayfs).
    if (base_rootfs_count > 1) {
        VLOG_ERROR("containerv", "containerv_layers_compose: multiple BASE_ROOTFS layers are not supported on Windows\n");
        errno = ENOTSUP;
        return -1;
    }
    if (base_rootfs_count == 0 && vafs_count == 0) {
        VLOG_ERROR("containerv", "containerv_layers_compose: missing rootfs layer (BASE_ROOTFS or VAFS_PACKAGE)\n");
        errno = EINVAL;
        return -1;
    }

    if (saw_overlay) {
        VLOG_WARNING("containerv", "containerv_layers_compose: OVERLAY layers are ignored on Windows (no overlayfs)\n");
    }

    struct containerv_layer_context* context = calloc(1, sizeof(*context));
    if (context == NULL) {
        errno = ENOMEM;
        return -1;
    }

    context->layer_count = layer_count;
    context->layers = calloc((size_t)layer_count, sizeof(struct containerv_layer));
    if (context->layers == NULL) {
        containerv_layers_destroy(context);
        errno = ENOMEM;
        return -1;
    }

    for (int i = 0; i < layer_count; ++i) {
        context->layers[i] = layers[i];
        context->layers[i].source = layers[i].source ? _strdup(layers[i].source) : NULL;
        context->layers[i].target = layers[i].target ? _strdup(layers[i].target) : NULL;
        if ((layers[i].source && context->layers[i].source == NULL) ||
            (layers[i].target && context->layers[i].target == NULL)) {
            containerv_layers_destroy(context);
            errno = ENOMEM;
            return -1;
        }
    }
    
    const char* const* parents = NULL;
    int parent_count = 0;
    if (compose_options != NULL) {
        parents = compose_options->windows_wcow_parent_layers;
        parent_count = compose_options->windows_wcow_parent_layer_count;
    }

    // If HCS mode is preferred and the base rootfs is already a windowsfilter layer, use it directly
    // (only valid when no VAFS layers need applying).
    if (vafs_count == 0 && base_rootfs_count == 1 && base_rootfs != NULL &&
        __windowsfilter_has_layerchain(base_rootfs)) {
        context->composed_rootfs = _strdup(base_rootfs);
    } else {
        char* outDir = NULL;
        char* containerDir = NULL;

        if (__create_windows_layers_dirs(container_id, &containerDir, &outDir) != 0) {
            VLOG_ERROR("containerv", "containerv_layers_compose: failed to create layers directory\n");
            containerv_layers_destroy(context);
            return -1;
        }

        context->materialized_container_dir = containerDir;
        context->composed_rootfs_is_materialized = 1;
        context->composed_rootfs = outDir;

        const char* source_rootfs = NULL;
        int owned_rootfs_dir = 0;

        if (vafs_count > 0) {
            source_rootfs = outDir;
            owned_rootfs_dir = 1;

            // If we have a BASE_ROOTFS, copy it into the materialized directory first.
            if (base_rootfs_count == 1) {
                if (base_rootfs == NULL || base_rootfs[0] == '\0') {
                    VLOG_ERROR("containerv", "containerv_layers_compose: BASE_ROOTFS layer missing source path\n");
                    containerv_layers_destroy(context);
                    errno = EINVAL;
                    return -1;
                }
                if (__copy_tree_recursive(base_rootfs, outDir) != 0) {
                    VLOG_ERROR("containerv", "containerv_layers_compose: failed to materialize BASE_ROOTFS into %s\n", outDir);
                    containerv_layers_destroy(context);
                    return -1;
                }
            }

            // Apply VAFS layers in order on top.
            for (int i = 0; i < layer_count; ++i) {
                if (layers[i].type != CONTAINERV_LAYER_VAFS_PACKAGE) {
                    continue;
                }

                if (layers[i].source == NULL || layers[i].source[0] == '\0') {
                    VLOG_ERROR("containerv", "containerv_layers_compose: VAFS layer missing source path\n");
                    containerv_layers_destroy(context);
                    errno = EINVAL;
                    return -1;
                }

                char args[4096];
                int rc = snprintf(args, sizeof(args), "--no-progress --out \"%s\" \"%s\"", outDir, layers[i].source);
                if (rc < 0 || (size_t)rc >= sizeof(args)) {
                    containerv_layers_destroy(context);
                    errno = EINVAL;
                    return -1;
                }

                int status = platform_spawn(
                    "unmkvafs",
                    args,
                    NULL,
                    &(struct platform_spawn_options) {
                        .output_handler = __spawn_output_handler,
                    }
                );

                if (status != 0) {
                    VLOG_ERROR("containerv", "containerv_layers_compose: unmkvafs failed (%d) for %s\n", status, layers[i].source);
                    errno = EIO;
                    return -1;
                }
            }
        } else if (base_rootfs_count == 1) {
            if (base_rootfs == NULL || base_rootfs[0] == '\0') {
                VLOG_ERROR("containerv", "containerv_layers_compose: BASE_ROOTFS layer missing source path\n");
                containerv_layers_destroy(context);
                errno = EINVAL;
                return -1;
            }
            source_rootfs = base_rootfs;
        }
        
        if (source_rootfs == NULL) {
            VLOG_ERROR("containerv", "containerv_layers_compose: missing rootfs content for windowsfilter import\n");
            containerv_layers_destroy(context);
            errno = EINVAL;
            return -1;
        }

        char wcow_dir[MAX_PATH];
        int rc = snprintf(wcow_dir, sizeof(wcow_dir), "%s\\windowsfilter", context->materialized_container_dir);
        if (rc < 0 || (size_t)rc >= sizeof(wcow_dir)) {
            containerv_layers_destroy(context);
            errno = EINVAL;
            return -1;
        }

        char** parent_layers = NULL;
        int parent_layer_count = 0;
        if (__copy_parent_layers(parents, parent_count, &parent_layers, &parent_layer_count) != 0) {
            containerv_layers_destroy(context);
            errno = EINVAL;
            return -1;
        }

        if (__windowsfilter_import_from_dir(wcow_dir, source_rootfs, (const char* const*)parent_layers, parent_layer_count) != 0) {
            __free_parent_layers(parent_layers, parent_layer_count);
            VLOG_ERROR("containerv", "containerv_layers_compose: failed to import windowsfilter layer from %s\n", source_rootfs);
            containerv_layers_destroy(context);
            return -1;
        }

        __free_parent_layers(parent_layers, parent_layer_count);

        context->windowsfilter_dir = _strdup(wcow_dir);
        free(context->composed_rootfs);
        context->composed_rootfs = _strdup(wcow_dir);
    }

    if (context->composed_rootfs == NULL) {
        VLOG_ERROR("containerv", "containerv_layers_compose: missing BASE_ROOTFS layer\n");
        containerv_layers_destroy(context);
        errno = EINVAL;
        return -1;
    }

    *context_out = context;
    return 0;
}

int containerv_layers_compose(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
    struct containerv_layer_context** context_out)
{
    return containerv_layers_compose_ex(layers, layer_count, container_id, NULL, context_out);
}

int containerv_layers_compose_with_options(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
    const struct containerv_options*  options,
    struct containerv_layer_context** context_out)
{
    struct containerv_layers_compose_options compose_options = {0};
    if (options != NULL) {
        compose_options.windows_wcow_parent_layers = options->windows_wcow_parent_layers;
        compose_options.windows_wcow_parent_layer_count = options->windows_wcow_parent_layer_count;
    }

    return containerv_layers_compose_ex(layers, layer_count, container_id, &compose_options, context_out);
}

int containerv_layers_mount_in_namespace(struct containerv_layer_context* context)
{
    // Windows has no mount namespaces in this implementation.
    (void)context;
    return 0;
}

const char* containerv_layers_get_rootfs(struct containerv_layer_context* context)
{
    if (context == NULL) {
        return NULL;
    }
    return context->composed_rootfs;
}

void containerv_layers_destroy(struct containerv_layer_context* context)
{
    if (context == NULL) {
        return;
    }
    
    // TODO: Clean up Windows HCI layer resources

    if (context->composed_rootfs_is_materialized && context->materialized_container_dir != NULL) {
        if (__remove_tree_recursive(context->materialized_container_dir) != 0) {
            VLOG_WARNING(
                "containerv",
                "containerv_layers_destroy: failed to remove materialized layers dir %s (errno=%d)\n",
                context->materialized_container_dir,
                errno
            );
        }
    }

    free(context->materialized_container_dir);
    free(context->composed_rootfs);
    free(context->windowsfilter_dir);
    __free_layer_copy(context->layers, context->layer_count);
    free(context);
}

int containerv_layers_iterate(
    struct containerv_layer_context* context,
    enum containerv_layer_type       layerType,
    containerv_layers_iterate_cb     cb,
    void*                            userContext)
{
    if (context == NULL || cb == NULL) {
        errno = EINVAL;
        return -1;
    }

    for (int i = 0; i < context->layer_count; ++i) {
        struct containerv_layer* layer = &context->layers[i];

        if (layer->type != layerType) {
            continue;
        }

        if (layer->source == NULL || layer->target == NULL) {
            continue;
        }

        int rc = cb(layer->source, layer->target, layer->readonly, userContext);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}
