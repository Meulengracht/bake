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

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include <chef/containerv/layers.h>

#include "oci-spec.h"
#include "oci-bundle.h"

#include "private.h"

// Some Windows SDKs don't expose these HCS_* HRESULTs unless additional headers are used.
// They are only used for improved error messages, so provide reasonable fallbacks.
#ifndef HCS_E_OPERATION_NOT_SUPPORTED
#define HCS_E_OPERATION_NOT_SUPPORTED HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
#endif

#ifndef HCS_E_SERVICE_NOT_AVAILABLE
#define HCS_E_SERVICE_NOT_AVAILABLE HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)
#endif

// Global HCS API structure
struct hcs_api g_hcs = { 0 };

static FARPROC __get_proc_any(const char* name)
{
    if (name == NULL) {
        return NULL;
    }

    FARPROC p = NULL;
    if (g_hcs.hComputeCore != NULL) {
        p = GetProcAddress(g_hcs.hComputeCore, name);
        if (p != NULL) {
            return p;
        }
    }

    if (g_hcs.hVmCompute != NULL) {
        p = GetProcAddress(g_hcs.hVmCompute, name);
        if (p != NULL) {
            return p;
        }
    }
    return NULL;
}

static void __hcs_localfree_wstr(PWSTR s)
{
    if (s != NULL) {
        LocalFree(s);
    }
}

static int __appendf(char** buf, size_t* cap, size_t* len, const char* fmt, ...)
{
    if (buf == NULL || cap == NULL || len == NULL || fmt == NULL) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);

    for (;;) {
        if (*buf == NULL) {
            *cap = 4096;
            *len = 0;
            *buf = calloc(*cap, 1);
            if (*buf == NULL) {
                va_end(args);
                return -1;
            }
        }

        va_list args2;
        va_copy(args2, args);
        int n = vsnprintf(*buf + *len, *cap - *len, fmt, args2);
        va_end(args2);

        if (n < 0) {
            va_end(args);
            return -1;
        }

        if (*len + (size_t)n < *cap) {
            *len += (size_t)n;
            va_end(args);
            return 0;
        }

        // Need more space
        size_t new_cap = *cap * 2;
        while (*len + (size_t)n >= new_cap) {
            new_cap *= 2;
        }
        char* tmp = realloc(*buf, new_cap);
        if (tmp == NULL) {
            va_end(args);
            return -1;
        }
        *buf = tmp;
        memset(*buf + *cap, 0, new_cap - *cap);
        *cap = new_cap;
    }
}

static char* __json_escape_utf8(const char* s)
{
    if (s == NULL) {
        return _strdup("");
    }

    size_t in_len = strlen(s);
    // Worst-case expand ~6x for \u00XX
    size_t cap = (in_len * 6) + 1;
    char* out = calloc(cap, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < in_len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c == '\b') {
            out[j++] = '\\'; out[j++] = 'b';
        } else if (c == '\f') {
            out[j++] = '\\'; out[j++] = 'f';
        } else if (c == '\n') {
            out[j++] = '\\'; out[j++] = 'n';
        } else if (c == '\r') {
            out[j++] = '\\'; out[j++] = 'r';
        } else if (c == '\t') {
            out[j++] = '\\'; out[j++] = 't';
        } else if (c < 0x20) {
            // control chars as \u00XX
            int n = snprintf(out + j, cap - j, "\\u%04x", (unsigned int)c);
            if (n < 0) {
                free(out);
                return NULL;
            }
            j += (size_t)n;
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
    return out;
}

static wchar_t* __utf8_to_wide_alloc(const char* s)
{
    if (s == NULL) {
        return NULL;
    }

    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }

    wchar_t* out = calloc((size_t)needed, sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, needed) == 0) {
        free(out);
        return NULL;
    }
    return out;
}

static int __looks_like_guid(const char* s)
{
    if (s == NULL) {
        return 0;
    }
    // Expected: 8-4-4-4-12 (36 chars)
    if (strlen(s) != 36) {
        return 0;
    }
    const int dash_pos[] = {8, 13, 18, 23};
    for (int i = 0; i < 4; i++) {
        if (s[dash_pos[i]] != '-') {
            return 0;
        }
    }
    for (int i = 0; i < 36; i++) {
        if (s[i] == '-') {
            continue;
        }
        char c = s[i];
        int is_hex = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if (!is_hex) {
            return 0;
        }
    }
    return 1;
}

static char* __normalize_container_path_win_alloc(const char* p)
{
    if (p == NULL) {
        return NULL;
    }
    if (p[0] == '\\' || (strlen(p) >= 2 && p[1] == ':')) {
        return _strdup(p);
    }
    if (p[0] == '/') {
        // Map POSIX-ish paths to C:\<path> for convenience.
        size_t n = strlen(p);
        // Worst case: same length + "C:" prefix
        char* out = calloc(n + 3, 1);
        if (out == NULL) {
            return NULL;
        }
        out[0] = 'C';
        out[1] = ':';
        size_t j = 2;
        for (size_t i = 0; i < n; i++) {
            char c = p[i];
            if (c == '/') {
                out[j++] = '\\';
            } else {
                out[j++] = c;
            }
        }
        out[j] = 0;
        return out;
    }
    // Relative-ish: treat as C:\<p>
    size_t n = strlen(p);
    char* out = calloc(n + 4, 1);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out, n + 4, "C:\\%s", p);
    for (char* q = out; *q; ++q) {
        if (*q == '/') {
            *q = '\\';
        }
    }
    return out;
}

struct __mapped_dir_build_ctx {
    char**  json;
    size_t* cap;
    size_t* len;
    int     emitted;
    int     linux_container;
    const char* linux_container_prefix;
};

static int __starts_with_path_prefix(const char* s, const char* prefix)
{
    if (s == NULL || prefix == NULL) {
        return 0;
    }
    size_t ps = strlen(prefix);
    if (ps == 0) {
        return 1;
    }
    if (strncmp(s, prefix, ps) != 0) {
        return 0;
    }
    // Exact match or prefix followed by '/'
    return (s[ps] == '\0' || s[ps] == '/');
}

static char* __join_linux_prefix_alloc(const char* prefix, const char* container_path)
{
    char* norm = __normalize_container_path_linux_alloc(container_path);
    if (norm == NULL) {
        return NULL;
    }

    if (prefix == NULL || prefix[0] == '\0') {
        return norm;
    }

    // Normalize prefix: ensure leading '/', no trailing '/'.
    const char* pfx = prefix;
    char* pfx_norm = NULL;
    if (pfx[0] != '/') {
        size_t n = strlen(pfx);
        pfx_norm = calloc(n + 2, 1);
        if (pfx_norm == NULL) {
            free(norm);
            return NULL;
        }
        pfx_norm[0] = '/';
        memcpy(pfx_norm + 1, pfx, n + 1);
        pfx = pfx_norm;
    }

    size_t pfx_len = strlen(pfx);
    while (pfx_len > 1 && pfx[pfx_len - 1] == '/') {
        pfx_len--;
    }

    if (__starts_with_path_prefix(norm, pfx)) {
        free(pfx_norm);
        return norm;
    }

    // Join pfx + norm (drop leading '/' from norm).
    const char* rel = norm;
    while (*rel == '/') {
        rel++;
    }

    size_t rel_len = strlen(rel);
    size_t out_len = pfx_len + 1 + rel_len + 1;
    char* out = calloc(out_len, 1);
    if (out == NULL) {
        free(pfx_norm);
        free(norm);
        return NULL;
    }

    memcpy(out, pfx, pfx_len);
    out[pfx_len] = '/';
    memcpy(out + pfx_len + 1, rel, rel_len);
    out[pfx_len + 1 + rel_len] = '\0';

    free(pfx_norm);
    free(norm);
    return out;
}

static char* __normalize_container_path_linux_alloc(const char* p)
{
    if (p == NULL || p[0] == '\0') {
        return _strdup("/");
    }

    // Replace backslashes with forward slashes and ensure leading '/'.
    const char* s = p;
    size_t n = strlen(s);
    int need_leading = (s[0] != '/');
    char* out = calloc(n + (need_leading ? 2 : 1), 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    if (need_leading) {
        out[j++] = '/';
    }
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        out[j++] = (c == '\\') ? '/' : c;
    }
    out[j] = '\0';
    return out;
}

static int __append_mapped_dir_entry(
    struct __mapped_dir_build_ctx* ctx,
    const char* host_path,
    const char* container_path,
    int readonly)
{
    if (ctx == NULL || ctx->json == NULL || ctx->cap == NULL || ctx->len == NULL) {
        return -1;
    }

    char* esc_host = __json_escape_utf8(host_path);
    char* norm_container = NULL;
    if (ctx->linux_container) {
        norm_container = __join_linux_prefix_alloc(ctx->linux_container_prefix, container_path);
    } else {
        norm_container = __normalize_container_path_win_alloc(container_path);
    }
    if (esc_host == NULL || norm_container == NULL) {
        free(esc_host);
        free(norm_container);
        return -1;
    }
    char* esc_container = __json_escape_utf8(norm_container);
    free(norm_container);
    if (esc_container == NULL) {
        free(esc_host);
        return -1;
    }

    int rc = __appendf(
        ctx->json,
        ctx->cap,
        ctx->len,
        "%s{\"HostPath\":\"%s\",\"ContainerPath\":\"%s\",\"ReadOnly\":%s%s}",
        ctx->emitted == 0 ? "" : ",",
        esc_host,
        esc_container,
        readonly ? "true" : "false",
        ctx->linux_container ? ",\"LinuxMetadata\":true" : "");
    free(esc_host);
    free(esc_container);
    if (rc != 0) {
        return -1;
    }
    ctx->emitted++;
    return 0;
}

static int __mapped_dir_cb(const char* host_path, const char* container_path, int readonly, void* user_context)
{
    struct __mapped_dir_build_ctx* ctx = (struct __mapped_dir_build_ctx*)user_context;
    return __append_mapped_dir_entry(ctx, host_path, container_path, readonly);
}

static void __derive_layer_id_from_path(const char* path, char out36[37])
{
    // Prefer basename if it looks like a GUID (common for windowsfilter layer folders).
    const char* base = path;
    if (path != NULL) {
        const char* s1 = strrchr(path, '\\');
        const char* s2 = strrchr(path, '/');
        base = s1;
        if (s2 != NULL && (base == NULL || s2 > base)) {
            base = s2;
        }
        if (base != NULL) {
            base++;
        } else {
            base = path;
        }
    }

    if (__looks_like_guid(base)) {
        strncpy(out36, base, 36);
        out36[36] = 0;
        return;
    }

    // Fallback: stable pseudo-GUID derived from FNV1a of the path.
    uint64_t h1 = 1469598103934665603ull;
    uint64_t h2 = 1099511628211ull;
    if (path != NULL) {
        for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
            h1 ^= (uint64_t)(*p);
            h1 *= 1099511628211ull;
        }
        for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
            h2 ^= (uint64_t)(*p);
            h2 *= 1469598103934665603ull;
        }
    }

    unsigned int a = (unsigned int)(h1 & 0xffffffffu);
    unsigned short b = (unsigned short)((h1 >> 32) & 0xffffu);
    unsigned short c = (unsigned short)((h1 >> 48) & 0xffffu);
    unsigned short d = (unsigned short)(h2 & 0xffffu);
    unsigned long long e = (unsigned long long)((h2 >> 16) & 0xffffffffffffull);
    (void)snprintf(out36, 37, "%08x-%04x-%04x-%04x-%012llx", a, b, c, d, e);
    out36[36] = 0;
}

static wchar_t* __hcs_create_container_config_schema1(
    struct containerv_container* container,
    struct containerv_options* options,
    const char* layer_folder_path,
    const char* const* parent_layers,
    int parent_layer_count,
    const char* utilityvm_path,
    int linux_container)
{
    if (container == NULL || parent_layer_count < 0) {
        return NULL;
    }

    // Build UTF-8 JSON and convert to wide.
    char* json = NULL;
    size_t cap = 0;
    size_t len = 0;

    char* esc_name = __json_escape_utf8(container->id);
    char* esc_owner = __json_escape_utf8("chef");
    char* esc_layer_folder = (layer_folder_path != NULL) ? __json_escape_utf8(layer_folder_path) : _strdup("");
    char* esc_hostname = __json_escape_utf8(container->hostname ? container->hostname : container->id);
    if (esc_name == NULL || esc_owner == NULL || esc_layer_folder == NULL || esc_hostname == NULL) {
        free(esc_name);
        free(esc_owner);
        free(esc_layer_folder);
        free(esc_hostname);
        free(json);
        return NULL;
    }

    int hv = 0;
    if (linux_container) {
        hv = 1; // LCOW always uses a utility VM.
    } else if (options != NULL && options->windows_container.isolation == WINDOWS_CONTAINER_ISOLATION_HYPERV) {
        hv = 1;
    }

    if (__appendf(&json, &cap, &len, "{") != 0) goto fail;
    if (__appendf(&json, &cap, &len, "\"SystemType\":\"Container\"") != 0) goto fail;
    if (__appendf(&json, &cap, &len, ",\"Name\":\"%s\"", esc_name) != 0) goto fail;
    if (__appendf(&json, &cap, &len, ",\"Owner\":\"%s\"", esc_owner) != 0) goto fail;
    if (__appendf(&json, &cap, &len, ",\"HostName\":\"%s\"", esc_hostname) != 0) goto fail;

    // Windows containers: HCS expects a writable layer folder plus parent layers.
    // LCOW: LayerFolderPath/Layers are not required for UVM bring-up.
    if (!linux_container) {
        if (layer_folder_path == NULL || layer_folder_path[0] == '\0' || parent_layers == NULL) {
            goto fail;
        }
        if (__appendf(&json, &cap, &len, ",\"LayerFolderPath\":\"%s\"", esc_layer_folder) != 0) goto fail;
    }

    // Mapped directories: always mount a staging folder for file transfers, plus any HOST_DIRECTORY layers.
    if (__appendf(&json, &cap, &len, ",\"MappedDirectories\":[") != 0) goto fail;
    {
        // LCOW: when we map a host rootfs folder, we treat that mapping as the OCI root and
        // rebase *all other mounts* under it so they survive pivot_root.
        const int lcow_has_rootfs = (linux_container && layer_folder_path != NULL && layer_folder_path[0] != '\0');

        // Map rootfs itself first without prefixing.
        if (lcow_has_rootfs) {
            struct __mapped_dir_build_ctx rootctx = {
                .json = &json,
                .cap = &cap,
                .len = &len,
                .emitted = 0,
                .linux_container = linux_container,
                .linux_container_prefix = NULL,
            };
            if (__append_mapped_dir_entry(&rootctx, layer_folder_path, "/chef/rootfs", 0) != 0) goto fail;
        }

        struct __mapped_dir_build_ctx mctx = {
            .json = &json,
            .cap = &cap,
            .len = &len,
            .emitted = lcow_has_rootfs ? 1 : 0,
            .linux_container = linux_container,
            .linux_container_prefix = lcow_has_rootfs ? "/chef/rootfs" : NULL,
        };

        // Built-in staging mount
        char stage_host[MAX_PATH];
        snprintf(stage_host, sizeof(stage_host), "%s\\staging", container->runtime_dir);
        if (__append_mapped_dir_entry(&mctx, stage_host, linux_container ? "/chef/staging" : "C:\\chef\\staging", 0) != 0) goto fail;

        // Host-directory layers
        if (options != NULL && options->layers != NULL) {
            if (containerv_layers_iterate(options->layers, CONTAINERV_LAYER_HOST_DIRECTORY, __mapped_dir_cb, &mctx) != 0) {
                // If iteration fails, be conservative.
                goto fail;
            }
        }
    }
    if (__appendf(&json, &cap, &len, "]") != 0) goto fail;

    if (linux_container) {
        if (__appendf(&json, &cap, &len, ",\"ContainerType\":\"Linux\"") != 0) goto fail;
    }

    if (!linux_container) {
        if (__appendf(&json, &cap, &len, ",\"Layers\":[") != 0) goto fail;
        int emitted = 0;
        for (int i = 0; i < parent_layer_count; i++) {
            if (parent_layers[i] == NULL || parent_layers[i][0] == '\0') {
                continue;
            }

            char idbuf[37];
            __derive_layer_id_from_path(parent_layers[i], idbuf);

            char* esc_layer_path = __json_escape_utf8(parent_layers[i]);
            if (esc_layer_path == NULL) {
                goto fail;
            }

            if (__appendf(&json, &cap, &len, "%s{\"ID\":\"%s\",\"Path\":\"%s\"}", emitted == 0 ? "" : ",", idbuf, esc_layer_path) != 0) {
                free(esc_layer_path);
                goto fail;
            }
            free(esc_layer_path);
            emitted++;
        }
        if (__appendf(&json, &cap, &len, "]") != 0) goto fail;
    } else {
        // LCOW: no windowsfilter parent layers required for UVM bring-up.
        if (__appendf(&json, &cap, &len, ",\"Layers\":[ ]") != 0) goto fail;
    }

    // Hyper-V isolation (schema1)
    if (hv) {
        if (__appendf(&json, &cap, &len, ",\"HvPartition\":true") != 0) goto fail;
        if (utilityvm_path != NULL && utilityvm_path[0] != '\0') {
            char* esc_uvm = __json_escape_utf8(utilityvm_path);
            if (esc_uvm == NULL) {
                goto fail;
            }

            if (!linux_container) {
                if (__appendf(&json, &cap, &len, ",\"HvRuntime\":{\"ImagePath\":\"%s\"}", esc_uvm) != 0) {
                    free(esc_uvm);
                    goto fail;
                }
                free(esc_uvm);
            } else {
                // LCOW: include optional kernel/initrd/boot params under HvRuntime.
                const char* kf = (options && options->windows_lcow.kernel_file) ? options->windows_lcow.kernel_file : NULL;
                const char* ir = (options && options->windows_lcow.initrd_file) ? options->windows_lcow.initrd_file : NULL;
                const char* bp = (options && options->windows_lcow.boot_parameters) ? options->windows_lcow.boot_parameters : NULL;

                char* esc_kf = (kf && kf[0]) ? __json_escape_utf8(kf) : NULL;
                char* esc_ir = (ir && ir[0]) ? __json_escape_utf8(ir) : NULL;
                char* esc_bp = (bp && bp[0]) ? __json_escape_utf8(bp) : NULL;

                if ((kf && kf[0] && esc_kf == NULL) || (ir && ir[0] && esc_ir == NULL) || (bp && bp[0] && esc_bp == NULL)) {
                    free(esc_uvm);
                    free(esc_kf);
                    free(esc_ir);
                    free(esc_bp);
                    goto fail;
                }

                if (__appendf(&json, &cap, &len, ",\"HvRuntime\":{\"ImagePath\":\"%s\"", esc_uvm) != 0) {
                    free(esc_uvm);
                    free(esc_kf);
                    free(esc_ir);
                    free(esc_bp);
                    goto fail;
                }

                if (esc_kf != NULL) {
                    if (__appendf(&json, &cap, &len, ",\"LinuxKernelFile\":\"%s\"", esc_kf) != 0) {
                        free(esc_uvm);
                        free(esc_kf);
                        free(esc_ir);
                        free(esc_bp);
                        goto fail;
                    }
                }

                if (esc_ir != NULL) {
                    if (__appendf(&json, &cap, &len, ",\"LinuxInitrdFile\":\"%s\"", esc_ir) != 0) {
                        free(esc_uvm);
                        free(esc_kf);
                        free(esc_ir);
                        free(esc_bp);
                        goto fail;
                    }
                }

                if (esc_bp != NULL) {
                    if (__appendf(&json, &cap, &len, ",\"LinuxBootParameters\":\"%s\"", esc_bp) != 0) {
                        free(esc_uvm);
                        free(esc_kf);
                        free(esc_ir);
                        free(esc_bp);
                        goto fail;
                    }
                }

                if (__appendf(&json, &cap, &len, "}") != 0) {
                    free(esc_uvm);
                    free(esc_kf);
                    free(esc_ir);
                    free(esc_bp);
                    goto fail;
                }

                free(esc_uvm);
                free(esc_kf);
                free(esc_ir);
                free(esc_bp);
            }
        }
    } else {
        if (__appendf(&json, &cap, &len, ",\"HvPartition\":false") != 0) goto fail;
    }

    // Termination behavior
    if (__appendf(&json, &cap, &len, ",\"TerminateOnLastHandleClosed\":true") != 0) goto fail;

    if (__appendf(&json, &cap, &len, "}") != 0) goto fail;

    {
        wchar_t* w = __utf8_to_wide_alloc(json);
        free(esc_name);
        free(esc_owner);
        free(esc_layer_folder);
        free(esc_hostname);
        free(json);
        return w;
    }

fail:
    free(esc_name);
    free(esc_owner);
    free(esc_layer_folder);
    free(esc_hostname);
    free(json);
    return NULL;
}

int __hcs_create_container_system(
    struct containerv_container* container,
    struct containerv_options* options,
    const char* layer_folder_path,
    const char* const* parent_layers,
    int parent_layer_count,
    const char* utilityvm_path,
    int linux_container)
{
    HCS_OPERATION operation = NULL;
    wchar_t* config = NULL;
    HRESULT hr;
    int status = -1;

    if (!container || !container->vm_id) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating HCS container compute system for %s\n", container->id);

    if (__hcs_initialize() != 0) {
        return -1;
    }

    config = __hcs_create_container_config_schema1(
        container,
        options,
        layer_folder_path,
        parent_layers,
        parent_layer_count,
        utilityvm_path,
        linux_container);
    if (config == NULL) {
        VLOG_ERROR("containerv[hcs]", "failed to create container configuration\n");
        return -1;
    }

    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
        goto cleanup;
    }

    hr = g_hcs.HcsCreateComputeSystem(
        container->vm_id,
        config,
        operation,
        NULL,
        &container->hcs_system);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create container compute system: 0x%lx\n", hr);
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        __hcs_localfree_wstr(resultDoc);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "container create wait failed: 0x%lx\n", hr);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
    }

    g_hcs.HcsCloseOperation(operation);
    operation = NULL;
    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation for start: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    hr = g_hcs.HcsStartComputeSystem(container->hcs_system, operation, NULL);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to start container compute system: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        __hcs_localfree_wstr(resultDoc);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "container start wait failed: 0x%lx\n", hr);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
    }

    container->vm_started = 1;
    status = 0;

cleanup:
    if (config) {
        free(config);
    }
    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }
    return status;
}

// HCS operation callback (stub for now)
static void CALLBACK __hcs_operation_callback(HCS_OPERATION operation, void* context)
{
    // For now, we use synchronous operations
    // In the future, this could handle async completion
    VLOG_DEBUG("containerv[hcs]", "__hcs_operation_callback called\n");
}

int __hcs_initialize(void)
{
    if (g_hcs.hVmCompute != NULL) {
        return 0; // Already initialized
    }

    VLOG_DEBUG("containerv[hcs]", "initializing HCS API\n");

    // Load vmcompute.dll
    g_hcs.hVmCompute = LoadLibraryA("vmcompute.dll");
    if (g_hcs.hVmCompute == NULL) {
        DWORD error = GetLastError();
        VLOG_ERROR("containerv[hcs]", "failed to load vmcompute.dll: %lu\n", error);
        
        // Check if HyperV is available
        if (error == ERROR_MOD_NOT_FOUND) {
            VLOG_ERROR("containerv[hcs]", "HyperV/HCS not available on this system\n");
        }
        return -1;
    }

    // Load HCS function pointers
    g_hcs.HcsCreateComputeSystem = (HcsCreateComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCreateComputeSystem");
    g_hcs.HcsStartComputeSystem = (HcsStartComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsStartComputeSystem");
    g_hcs.HcsShutdownComputeSystem = (HcsShutdownComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsShutdownComputeSystem");
    g_hcs.HcsTerminateComputeSystem = (HcsTerminateComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsTerminateComputeSystem");
    g_hcs.HcsCreateProcess = (HcsCreateProcess_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCreateProcess");
    g_hcs.HcsCreateOperation = (HcsCreateOperation_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCreateOperation");
    g_hcs.HcsCloseOperation = (HcsCloseOperation_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCloseOperation");
    g_hcs.HcsCloseComputeSystem = (HcsCloseComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCloseComputeSystem");
    g_hcs.HcsCloseProcess = (HcsCloseProcess_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCloseProcess");

    // Load ComputeCore.dll for synchronous wait helpers (Win10 1809+).
    // Some installations may still export these from vmcompute.dll, so we resolve from either.
    g_hcs.hComputeCore = LoadLibraryA("computecore.dll");
    g_hcs.HcsWaitForOperationResult = (HcsWaitForOperationResult_t)__get_proc_any("HcsWaitForOperationResult");
    g_hcs.HcsWaitForOperationResultAndProcessInfo = (HcsWaitForOperationResultAndProcessInfo_t)__get_proc_any("HcsWaitForOperationResultAndProcessInfo");

    // Check that we got all required functions
    if (!g_hcs.HcsCreateComputeSystem || !g_hcs.HcsStartComputeSystem ||
        !g_hcs.HcsShutdownComputeSystem || !g_hcs.HcsTerminateComputeSystem ||
        !g_hcs.HcsCreateProcess || !g_hcs.HcsCreateOperation ||
        !g_hcs.HcsCloseOperation || !g_hcs.HcsCloseComputeSystem ||
        !g_hcs.HcsCloseProcess) {
        
        VLOG_ERROR("containerv[hcs]", "failed to load required HCS functions\n");
        if (g_hcs.hComputeCore != NULL) {
            FreeLibrary(g_hcs.hComputeCore);
        }
        FreeLibrary(g_hcs.hVmCompute);
        memset(&g_hcs, 0, sizeof(g_hcs));
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "HCS API initialized successfully\n");
    return 0;
}

void __hcs_cleanup(void)
{
    if (g_hcs.hVmCompute != NULL) {
        VLOG_DEBUG("containerv[hcs]", "cleaning up HCS API\n");
        if (g_hcs.hComputeCore != NULL) {
            FreeLibrary(g_hcs.hComputeCore);
        }
        FreeLibrary(g_hcs.hVmCompute);
        memset(&g_hcs, 0, sizeof(g_hcs));
    }
}

wchar_t* __hcs_create_vm_config(
    struct containerv_container* container,
    struct containerv_options* options)
{
    wchar_t* config;
    size_t config_size = 4096;  // Start with 4KB, expand if needed
    int written;

    if (!container) {
        return NULL;
    }

    config = calloc(config_size, sizeof(wchar_t));
    if (!config) {
        return NULL;
    }

    // Create HCS configuration JSON with configurable options
    // Use options from containerv_options for VM resources and networking
    unsigned int memory_mb = options ? options->vm.memory_mb : 1024;
    unsigned int cpu_count = options ? options->vm.cpu_count : 2;
    const char* switch_name = (options && options->network.enable) ? options->network.switch_name : "";
    
    written = swprintf(config, config_size,
        L"{"
        L"\"SchemaVersion\":{\"Major\":2,\"Minor\":1},"
        L"\"VirtualMachine\":{"
            L"\"StopOnReset\":true,"
            L"\"Chipset\":{\"Uefi\":{\"BootThis\":true}},"
            L"\"ComputeTopology\":{"
                L"\"Memory\":{\"SizeInMB\":%u},"
                L"\"Processor\":{\"Count\":%u}"
            L"},"
            L"\"Devices\":{"
                L"\"Scsi\":{"
                    L"\"scsi0\":{"
                        L"\"Attachments\":{"
                            L"\"0\":{"
                                L"\"Type\":\"VirtualDisk\","
                                L"\"Path\":\"%hs\\\\container.vhdx\","
                                L"\"ReadOnly\":false"
                            L"}"
                        L"}"
                    L"}"
                L"}"
                // Network configuration added conditionally
                L"%s"  // Network adapter JSON inserted here
            L"}"
        L"},"
        L"\"ShouldTerminateOnLastHandleClosed\":true"
        L"}",
        memory_mb,
        cpu_count,
        container->runtime_dir,
        (options && options->network.enable) ? 
            L",\"NetworkAdapters\":{"
                L"\"net0\":{"
                    L"\"EndpointId\":\"\","
                    L"\"MacAddress\":\"\""
                L"}"
            L"}" : L""
    );

    if (written < 0 || written >= (int)config_size) {
        VLOG_ERROR("containerv[hcs]", "HCS config too large for buffer\n");
        free(config);
        return NULL;
    }

    VLOG_DEBUG("containerv[hcs]", "created HCS VM config for container %s\n", container->id);
    return config;
}

int __hcs_create_vm(
    struct containerv_container* container,
    struct containerv_options* options)
{
    HCS_OPERATION operation = NULL;
    wchar_t* config = NULL;
    HRESULT hr;
    int status = -1;

    if (!container || !container->vm_id) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating HyperV VM for container %s\n", container->id);

    // Ensure HCS API is initialized
    if (__hcs_initialize() != 0) {
        return -1;
    }

    // Generate VM configuration
    config = __hcs_create_vm_config(container, options);
    if (!config) {
        VLOG_ERROR("containerv[hcs]", "failed to create VM configuration\n");
        return -1;
    }

    // Create operation handle
    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
        goto cleanup;
    }

    // Create the compute system (VM)
    hr = g_hcs.HcsCreateComputeSystem(
        container->vm_id,
        config,
        operation,
        NULL,  // Security descriptor
        &container->hcs_system
    );

    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create compute system: 0x%lx\n", hr);
        
        // Provide more specific error information
        if (hr == HCS_E_SERVICE_NOT_AVAILABLE) {
            VLOG_ERROR("containerv[hcs]", "HCS service not available - ensure HyperV is enabled\n");
        } else if (hr == HCS_E_OPERATION_NOT_SUPPORTED) {
            VLOG_ERROR("containerv[hcs]", "HCS operation not supported on this Windows version\n");
        }
        
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        __hcs_localfree_wstr(resultDoc);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "compute system create wait failed: 0x%lx\n", hr);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
    } else {
        VLOG_WARNING("containerv[hcs]", "HcsWaitForOperationResult not available; VM start may be unreliable\n");
    }

    g_hcs.HcsCloseOperation(operation);
    operation = NULL;

    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation for start: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    // Start the VM
    hr = g_hcs.HcsStartComputeSystem(container->hcs_system, operation, NULL);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to start compute system: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        __hcs_localfree_wstr(resultDoc);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "compute system start wait failed: 0x%lx\n", hr);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
    }

    container->vm_started = 1;
    status = 0;

    VLOG_DEBUG("containerv[hcs]", "successfully created and started VM for container %s\n", 
               container->id);

cleanup:
    if (config) {
        free(config);
    }
    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }

    return status;
}

int __hcs_destroy_vm(struct containerv_container* container)
{
    HCS_OPERATION operation = NULL;
    HRESULT hr;
    int status = 0;

    if (!container || !container->hcs_system) {
        return 0;  // Nothing to destroy
    }

    VLOG_DEBUG("containerv[hcs]", "destroying HyperV VM for container %s\n", container->id);

    if (container->vm_started) {
        // Try graceful shutdown first
        hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
        if (FAILED(hr)) {
            VLOG_WARNING("containerv[hcs]", "failed to create operation for shutdown: 0x%lx\n", hr);
            operation = NULL;
        }

        hr = g_hcs.HcsShutdownComputeSystem(container->hcs_system, operation, NULL);
        if (SUCCEEDED(hr) && g_hcs.HcsWaitForOperationResult != NULL && operation != NULL) {
            PWSTR resultDoc = NULL;
            HRESULT whr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
            __hcs_localfree_wstr(resultDoc);
            if (FAILED(whr)) {
                hr = whr;
            }
        }

        if (operation != NULL) {
            g_hcs.HcsCloseOperation(operation);
            operation = NULL;
        }

        if (FAILED(hr)) {
            VLOG_WARNING("containerv[hcs]", "graceful shutdown failed, forcing termination: 0x%lx\n", hr);

            hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
            if (FAILED(hr)) {
                VLOG_WARNING("containerv[hcs]", "failed to create operation for terminate: 0x%lx\n", hr);
                operation = NULL;
            }

            hr = g_hcs.HcsTerminateComputeSystem(container->hcs_system, operation, NULL);
            if (SUCCEEDED(hr) && g_hcs.HcsWaitForOperationResult != NULL && operation != NULL) {
                PWSTR resultDoc = NULL;
                HRESULT whr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
                __hcs_localfree_wstr(resultDoc);
                if (FAILED(whr)) {
                    hr = whr;
                }
            }

            if (FAILED(hr)) {
                VLOG_ERROR("containerv[hcs]", "failed to terminate compute system: 0x%lx\n", hr);
                status = -1;
            }

            if (operation != NULL) {
                g_hcs.HcsCloseOperation(operation);
                operation = NULL;
            }
        }

        container->vm_started = 0;
    }

    // Close the compute system handle
    hr = g_hcs.HcsCloseComputeSystem(container->hcs_system);
    if (FAILED(hr)) {
        VLOG_WARNING("containerv[hcs]", "failed to close compute system handle: 0x%lx\n", hr);
        status = -1;
    }

    container->hcs_system = NULL;

    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }

    VLOG_DEBUG("containerv[hcs]", "destroyed VM for container %s\n", container->id);
    return status;
}

int __hcs_create_process(
    struct containerv_container* container,
    struct __containerv_spawn_options* options,
    HCS_PROCESS* processOut,
    HCS_PROCESS_INFORMATION* processInfoOut)
{
    HCS_OPERATION operation = NULL;
    wchar_t* process_config = NULL;
    HCS_PROCESS process = NULL;
    HRESULT hr;
    int status = -1;
    char* json_utf8 = NULL;
    size_t json_cap = 0;
    size_t json_len = 0;

    char* args_json_utf8 = NULL;
    size_t args_cap = 0;
    size_t args_len = 0;

    // LCOW: optional OCI spec JSON (raw) used by Linux GCS.
    char* oci_spec_utf8 = NULL;
    size_t oci_cap = 0;
    size_t oci_len = 0;

    if (!container || !container->hcs_system || !options || !options->path) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating process in compute system: %s\n", options->path);

    const int guest_is_windows = (container->guest_is_windows != 0);

    const struct containerv_policy* policy = container->policy;
    enum containerv_security_level security_level = CV_SECURITY_DEFAULT;
    int win_use_app_container = 0;
    const char* win_integrity_level = NULL;
    const char* const* win_capability_sids = NULL;
    int win_capability_sid_count = 0;
    if (policy != NULL) {
        security_level = containerv_policy_get_security_level(policy);
        (void)containerv_policy_get_windows_isolation(
            policy,
            &win_use_app_container,
            &win_integrity_level,
            &win_capability_sids,
            &win_capability_sid_count);
    }

    // Build command representation.
    // - Windows GCS uses CommandLine (string).
    // - Linux GCS supports CommandArgs (array) and treats it as an alternative to CommandLine.
    char* esc_cmd = NULL;
    if (guest_is_windows) {
        // Build command line: path + optional argv (quoted conservatively)
        char* cmd_utf8 = NULL;
        size_t cmd_cap = 0;
        size_t cmd_len = 0;
        if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "%s", options->path) != 0) {
            goto cleanup;
        }

        if (options->argv != NULL) {
            for (int i = 1; options->argv[i] != NULL; ++i) {
                const char* arg = options->argv[i];
                if (arg == NULL) {
                    continue;
                }

                int needs_quotes = 0;
                for (const char* p = arg; *p; ++p) {
                    if (*p == ' ' || *p == '\t' || *p == '"') {
                        needs_quotes = 1;
                        break;
                    }
                }

                if (!needs_quotes) {
                    if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, " %s", arg) != 0) {
                        free(cmd_utf8);
                        goto cleanup;
                    }
                    continue;
                }

                // Quote and escape quotes.
                if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, " \"") != 0) {
                    free(cmd_utf8);
                    goto cleanup;
                }
                for (const char* p = arg; *p; ++p) {
                    if (*p == '"') {
                        if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "\\\"") != 0) {
                            free(cmd_utf8);
                            goto cleanup;
                        }
                    } else {
                        if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "%c", *p) != 0) {
                            free(cmd_utf8);
                            goto cleanup;
                        }
                    }
                }
                if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "\"") != 0) {
                    free(cmd_utf8);
                    goto cleanup;
                }
            }
        }

        esc_cmd = __json_escape_utf8(cmd_utf8);
        free(cmd_utf8);
        if (esc_cmd == NULL) {
            goto cleanup;
        }
    } else {
        const char* const* argv = options->argv;
        const char* default_argv_buf[2] = { 0 };
        if (argv == NULL) {
            default_argv_buf[0] = options->path;
            default_argv_buf[1] = NULL;
            argv = default_argv_buf;
        }

        if (__appendf(&args_json_utf8, &args_cap, &args_len, "[") != 0) {
            goto cleanup;
        }

        int first_arg = 1;
        for (int i = 0; argv[i] != NULL; ++i) {
            char* esc = __json_escape_utf8(argv[i]);
            if (esc == NULL) {
                goto cleanup;
            }
            if (__appendf(&args_json_utf8, &args_cap, &args_len, "%s\"%s\"", first_arg ? "" : ",", esc) != 0) {
                free(esc);
                goto cleanup;
            }
            free(esc);
            first_arg = 0;
        }

        if (__appendf(&args_json_utf8, &args_cap, &args_len, "]") != 0) {
            goto cleanup;
        }
    }

    // If we're running a Linux container compute system (LCOW), prefer OCISpecification.
    // Rootfs is expected to be mapped into the container at /chef/rootfs.
    if (!guest_is_windows && container->hcs_is_vm == 0) {
        struct containerv_oci_linux_spec_params p = {
            .args_json = (args_json_utf8 != NULL) ? args_json_utf8 : "[]",
            .envv = (const char* const*)options->envv,
            .root_path = "/chef/rootfs",
            .cwd = "/",
            .hostname = "chef",
        };

        if (containerv_oci_build_linux_spec_json(&p, &oci_spec_utf8) != 0) {
            goto cleanup;
        }

        // Best-effort: also write config.json into the per-container OCI bundle for inspection.
        {
            struct containerv_oci_bundle_paths bundle;
            memset(&bundle, 0, sizeof(bundle));
            if (containerv_oci_bundle_get_paths(container->runtime_dir, &bundle) == 0) {
                (void)containerv_oci_bundle_write_config(&bundle, oci_spec_utf8);
                containerv_oci_bundle_paths_destroy(&bundle);
            }
        }
    }

    // Build environment object. Always include a default PATH if not provided.
    int has_path = 0;
    int has_policy_level = 0;
    int has_policy_appc = 0;
    int has_policy_integrity = 0;
    int has_policy_caps = 0;
    if (options->envv != NULL) {
        for (int i = 0; options->envv[i] != NULL; ++i) {
            if (_strnicmp(options->envv[i], "PATH=", 5) == 0) {
                has_path = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_SECURITY_LEVEL=", 30) == 0) {
                has_policy_level = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_USE_APPCONTAINER=", 38) == 0) {
                has_policy_appc = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_INTEGRITY_LEVEL=", 36) == 0) {
                has_policy_integrity = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_CAPABILITY_SIDS=", 39) == 0) {
                has_policy_caps = 1;
                continue;
            }
        }
    }

    char* env_utf8 = NULL;
    size_t env_cap = 0;
    size_t env_len = 0;
    if (__appendf(&env_utf8, &env_cap, &env_len, "{") != 0) {
        free(esc_cmd);
        goto cleanup;
    }

    int first = 1;
    if (!has_path) {
        char* esc_key = __json_escape_utf8("PATH");
        const char* default_path = guest_is_windows
            ? "C:\\Windows\\System32;C:\\Windows"
            : "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
        char* esc_val = __json_escape_utf8(default_path);
        if (esc_key == NULL || esc_val == NULL) {
            free(esc_key);
            free(esc_val);
            free(env_utf8);
            free(esc_cmd);
            goto cleanup;
        }
        if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
            free(esc_key);
            free(esc_val);
            free(env_utf8);
            free(esc_cmd);
            goto cleanup;
        }
        free(esc_key);
        free(esc_val);
        first = 0;
    }

    if (options->envv != NULL) {
        for (int i = 0; options->envv[i] != NULL; ++i) {
            const char* kv = options->envv[i];
            const char* eq = strchr(kv, '=');
            if (eq == NULL || eq == kv) {
                continue;
            }
            size_t key_len = (size_t)(eq - kv);
            char* key = calloc(key_len + 1, 1);
            if (key == NULL) {
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            memcpy(key, kv, key_len);
            key[key_len] = '\0';
            const char* val = eq + 1;

            char* esc_key = __json_escape_utf8(key);
            char* esc_val = __json_escape_utf8(val);
            free(key);
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }

            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }
    }

    // Inject policy metadata for in-VM agents/workloads (best-effort).
    if (policy != NULL) {
        if (!has_policy_level) {
            const char* lvl = "default";
            if (security_level >= CV_SECURITY_STRICT) {
                lvl = "strict";
            } else if (security_level >= CV_SECURITY_RESTRICTED) {
                lvl = "restricted";
            }
            char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_SECURITY_LEVEL");
            char* esc_val = __json_escape_utf8(lvl);
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }

        // Windows-specific policy metadata only applies to Windows guests.
        if (guest_is_windows && !has_policy_appc) {
            char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_WIN_USE_APPCONTAINER");
            char* esc_val = __json_escape_utf8(win_use_app_container ? "1" : "0");
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }

        if (guest_is_windows && !has_policy_integrity && win_integrity_level != NULL && win_integrity_level[0] != '\0') {
            char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_WIN_INTEGRITY_LEVEL");
            char* esc_val = __json_escape_utf8(win_integrity_level);
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }

        if (guest_is_windows && !has_policy_caps && win_capability_sids != NULL && win_capability_sid_count > 0) {
            // Comma-separated list.
            char* caps = NULL;
            size_t caps_cap = 0;
            size_t caps_len = 0;
            for (int i = 0; i < win_capability_sid_count; i++) {
                if (win_capability_sids[i] == NULL) {
                    continue;
                }
                if (__appendf(&caps, &caps_cap, &caps_len, "%s%s", (caps_len == 0) ? "" : ",", win_capability_sids[i]) != 0) {
                    free(caps);
                    free(env_utf8);
                    free(esc_cmd);
                    goto cleanup;
                }
            }
            if (caps != NULL && caps[0] != '\0') {
                char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_WIN_CAPABILITY_SIDS");
                char* esc_val = __json_escape_utf8(caps);
                free(caps);
                if (esc_key == NULL || esc_val == NULL) {
                    free(esc_key);
                    free(esc_val);
                    free(env_utf8);
                    free(esc_cmd);
                    goto cleanup;
                }
                if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                    free(esc_key);
                    free(esc_val);
                    free(env_utf8);
                    free(esc_cmd);
                    goto cleanup;
                }
                free(esc_key);
                free(esc_val);
                first = 0;
            } else {
                free(caps);
            }
        }
    }

    if (__appendf(&env_utf8, &env_cap, &env_len, "}") != 0) {
        free(env_utf8);
        free(esc_cmd);
        goto cleanup;
    }

    int try_security_fields = 0;
    if (guest_is_windows && policy != NULL) {
        if (security_level >= CV_SECURITY_RESTRICTED || win_use_app_container || (win_integrity_level != NULL && win_integrity_level[0] != '\0')) {
            try_security_fields = 1;
        }
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        int include_security = (attempt == 0) && try_security_fields;

        // (Re)create operation handle
        if (operation != NULL) {
            g_hcs.HcsCloseOperation(operation);
            operation = NULL;
        }
        hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
            goto cleanup;
        }

        free(json_utf8);
        json_utf8 = NULL;
        json_cap = 0;
        json_len = 0;

        // Build process configuration JSON (UTF-8) then convert to wide.
        const char* working_dir = guest_is_windows ? "C:\\\\" : "/";
        const char* emulate_console = guest_is_windows ? "true" : "false";
        const char* create_in_uvm = (!guest_is_windows && container->hcs_is_vm == 0 && oci_spec_utf8 == NULL) ? "true" : "false";
        if (guest_is_windows) {
            if (__appendf(
                    &json_utf8,
                    &json_cap,
                    &json_len,
                    "{"
                    "\"CommandLine\":\"%s\"," 
                    "\"WorkingDirectory\":\"%s\"," 
                    "%s"
                    "\"Environment\":%s,"
                    "\"EmulateConsole\":%s,"
                "\"CreateStdInPipe\":%s,"
                "\"CreateStdOutPipe\":%s,"
                "\"CreateStdErrPipe\":%s"
                    "}",
                    esc_cmd,
                    working_dir,
                    (guest_is_windows && include_security) ? "\"User\":\"ContainerUser\"," : "",
                env_utf8,
                emulate_console,
                options->create_stdio_pipes ? "true" : "false",
                options->create_stdio_pipes ? "true" : "false",
                options->create_stdio_pipes ? "true" : "false") != 0) {
                goto cleanup;
            }
        } else {
            // Linux guest: prefer CommandArgs, and include OCISpecification for LCOW when available.
            if (__appendf(
                    &json_utf8,
                    &json_cap,
                    &json_len,
                    "{"
                    "\"CommandArgs\":%s,"
                    "\"WorkingDirectory\":\"%s\"," 
                    "\"CreateInUtilityVm\":%s,"
                    "\"Environment\":%s,"
                    "\"EmulateConsole\":%s,"
                "\"CreateStdInPipe\":%s,"
                "\"CreateStdOutPipe\":%s,"
                "\"CreateStdErrPipe\":%s"
                    "}",
                    (args_json_utf8 != NULL) ? args_json_utf8 : "[]",
                    working_dir,
                create_in_uvm,
                env_utf8,
                emulate_console,
                options->create_stdio_pipes ? "true" : "false",
                options->create_stdio_pipes ? "true" : "false",
                options->create_stdio_pipes ? "true" : "false") != 0) {
                goto cleanup;
            }

            // If we emitted the OCISpecification key, rebuild with the raw JSON value.
            if (oci_spec_utf8 != NULL) {
                free(json_utf8);
                json_utf8 = NULL;
                json_cap = 0;
                json_len = 0;

                if (__appendf(
                        &json_utf8,
                        &json_cap,
                        &json_len,
                        "{"
                        "\"CommandArgs\":%s,"
                        "\"WorkingDirectory\":\"%s\"," 
                        "\"CreateInUtilityVm\":false,"
                        "\"OCISpecification\":%s,"
                        "\"Environment\":%s,"
                        "\"EmulateConsole\":%s,"
                        "\"CreateStdInPipe\":%s,"
                        "\"CreateStdOutPipe\":%s,"
                        "\"CreateStdErrPipe\":%s"
                        "}",
                        (args_json_utf8 != NULL) ? args_json_utf8 : "[]",
                        working_dir,
                        oci_spec_utf8,
                        env_utf8,
                        emulate_console,
                        options->create_stdio_pipes ? "true" : "false",
                        options->create_stdio_pipes ? "true" : "false",
                        options->create_stdio_pipes ? "true" : "false") != 0) {
                    goto cleanup;
                }
            }
        }

        if (process_config) {
            free(process_config);
            process_config = NULL;
        }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, json_utf8, -1, NULL, 0);
        if (wlen <= 0) {
            VLOG_ERROR("containerv[hcs]", "failed to size wide process config\n");
            goto cleanup;
        }

        process_config = calloc((size_t)wlen, sizeof(wchar_t));
        if (!process_config) {
            goto cleanup;
        }

        if (MultiByteToWideChar(CP_UTF8, 0, json_utf8, -1, process_config, wlen) == 0) {
            VLOG_ERROR("containerv[hcs]", "failed to convert process config to wide string\n");
            goto cleanup;
        }

        // Create the process in the VM
        hr = g_hcs.HcsCreateProcess(
            container->hcs_system,
            process_config,
            operation,
            NULL,  // Security descriptor
            &process
        );

        if (SUCCEEDED(hr)) {
            // Wait for completion and optionally fetch stdio handles.
            PWSTR resultDoc = NULL;
            HCS_PROCESS_INFORMATION pi;
            memset(&pi, 0, sizeof(pi));

            HRESULT whr = S_OK;
            if (g_hcs.HcsWaitForOperationResultAndProcessInfo != NULL) {
                HCS_PROCESS_INFORMATION* piPtr = NULL;
                if (options->create_stdio_pipes) {
                    piPtr = &pi;
                }
                whr = g_hcs.HcsWaitForOperationResultAndProcessInfo(operation, INFINITE, piPtr, &resultDoc);
            } else if (g_hcs.HcsWaitForOperationResult != NULL) {
                whr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
            } else {
                VLOG_WARNING("containerv[hcs]", "no operation wait API available; process creation may be unreliable\n");
            }

            __hcs_localfree_wstr(resultDoc);

            if (FAILED(whr)) {
                if (g_hcs.HcsCloseProcess) {
                    g_hcs.HcsCloseProcess(process);
                }
                process = NULL;

                if (include_security) {
                    VLOG_WARNING("containerv[hcs]", "process create wait rejected security fields (hr=0x%lx); retrying without them\n", whr);
                    continue;
                }

                VLOG_ERROR("containerv[hcs]", "failed to wait for process creation: 0x%lx\n", whr);
                break;
            }

            if (processOut) {
                *processOut = process;
            }

            if (options->create_stdio_pipes) {
                if (processInfoOut != NULL) {
                    *processInfoOut = pi;
                } else {
                    if (pi.StdInput != NULL) {
                        CloseHandle(pi.StdInput);
                    }
                    if (pi.StdOutput != NULL) {
                        CloseHandle(pi.StdOutput);
                    }
                    if (pi.StdError != NULL) {
                        CloseHandle(pi.StdError);
                    }
                }
            }

            status = 0;
            VLOG_DEBUG("containerv[hcs]", "successfully created process in VM\n");
            break;
        }

        if (include_security) {
            VLOG_WARNING("containerv[hcs]", "process create rejected security fields (hr=0x%lx); retrying without them\n", hr);
            continue;
        }

        VLOG_ERROR("containerv[hcs]", "failed to create process in VM: 0x%lx\n", hr);
        break;
    }

    free(env_utf8);
    env_utf8 = NULL;
    free(esc_cmd);
    esc_cmd = NULL;

cleanup:
    free(json_utf8);
    free(env_utf8);
    free(esc_cmd);
    free(args_json_utf8);
    free(oci_spec_utf8);
    if (process_config) {
        free(process_config);
    }
    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }

    return status;
}

/**
 * @brief Wait for HCS process completion (similar to Linux waitpid)
 */
int __hcs_wait_process(HCS_PROCESS process, unsigned int timeout_ms)
{
    if (!process) {
        return -1;
    }
    
    DWORD wait_ms = (DWORD)timeout_ms;
    VLOG_DEBUG("containerv[hcs]", "waiting for process completion (timeout: %lu ms)\n", (unsigned long)wait_ms);

    DWORD wr = WaitForSingleObject((HANDLE)process, wait_ms);
    if (wr == WAIT_OBJECT_0) {
        VLOG_DEBUG("containerv[hcs]", "process wait completed\n");
        return 0;
    }
    if (wr == WAIT_TIMEOUT) {
        errno = ETIMEDOUT;
        return -1;
    }

    VLOG_ERROR("containerv[hcs]", "WaitForSingleObject failed: %lu\n", GetLastError());
    errno = EIO;
    return -1;
}

/**
 * @brief Get HCS process exit code
 */
int __hcs_get_process_exit_code(HCS_PROCESS process, unsigned long* exit_code)
{
    if (!process || !exit_code) {
        return -1;
    }

    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)process, &code)) {
        VLOG_ERROR("containerv[hcs]", "GetExitCodeProcess failed: %lu\n", GetLastError());
        errno = EIO;
        return -1;
    }

    *exit_code = (unsigned long)code;
    return 0;
}
