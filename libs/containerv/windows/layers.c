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

// Initialize wclayer.dll bindings.
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

static int __windowsfilter_import_from_dir(
    const char* layer_dir,
    const char* source_dir,
    const char* const* parent_layers,
    int parent_layer_count)
{
    wchar_t*  layerW = NULL;
    wchar_t*  sourceW = NULL;
    wchar_t** parentsW = NULL;
    char**    expanded = NULL;
    int       expandedCount = 0;
    int       status = -1;
    HRESULT   hr;

    if (layer_dir == NULL || source_dir == NULL || layer_dir[0] == '\0' || source_dir[0] == '\0') {
        return -1;
    }

    if (__wclayer_initialize() != 0) {
        return -1;
    }

    if (!containerv_disk_path_is_directory(source_dir)) {
        VLOG_ERROR("containerv[layers]", "source rootfs directory does not exist: %s\n", source_dir);
        return -1;
    }

    (void)platform_mkdir(layer_dir);

    if (__windows_layerchain_expand(parent_layers, parent_layer_count, &expanded, &expandedCount) != 0) {
        VLOG_ERROR(
            "containerv[layers]",
            "WCOW windowsfilter import requires a valid parent layer chain (set via containerv_options_set_windows_wcow_parent_layers)\n");
        return -1;
    }

    layerW = __windows_utf8_to_wide_alloc(layer_dir);
    sourceW = __windows_utf8_to_wide_alloc(source_dir);
    parentsW = calloc((size_t)expandedCount, sizeof(wchar_t*));
    if (layerW == NULL || sourceW == NULL || parentsW == NULL) {
        VLOG_ERROR("containerv[layers]", "failed to convert layer paths to wide strings\n");
        goto cleanup;
    }
    for (int i = 0; i < expandedCount; i++) {
        parentsW[i] = __windows_utf8_to_wide_alloc(expanded[i]);
        if (parentsW[i] == NULL) {
            goto cleanup;
        }
    }

    hr = g_wclayer.ImportLayer(layerW, sourceW, (PCWSTR*)parentsW, (DWORD)expandedCount);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[layers]", "wclayer ImportLayer failed: 0x%lx\n", hr);
        goto cleanup;
    }

    status = __windows_layerchain_write(layer_dir, (const char* const*)expanded, expandedCount);

cleanup:
    if (parentsW != NULL) {
        for (int i = 0; i < expandedCount; i++) {
            free(parentsW[i]);
        }
        free(parentsW);
    }
    free(layerW);
    free(sourceW);
    __windows_strv_free(expanded, expandedCount);
    return status;
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
    char idDir[MAX_PATH];
    char root[MAX_PATH];
    int rc = snprintf(idDir, sizeof(idDir), "%schef-layers\\%s", tempPath, container_id ? container_id : "unknown");
    if (rc < 0 || (size_t)rc >= sizeof(idDir)) {
        errno = EINVAL;
        return -1;
    }
    rc = snprintf(root, sizeof(root), "%s\\rootfs", idDir);
    if (rc < 0 || (size_t)rc >= sizeof(root)) {
        errno = EINVAL;
        return -1;
    }

    if (platform_mkdir(root) != 0) {
        VLOG_ERROR("containerv[layers]", "failed to create layers directory %s\n", root);
        return -1;
    }

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
    const int is_lcow = (compose_options != NULL && compose_options->windows_is_lcow != 0) ? 1 : 0;

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

    // If HCS mode already has a usable rootfs path, reuse it directly when no package layers need applying.
    if (vafs_count == 0 && base_rootfs_count == 1 && base_rootfs != NULL &&
        (is_lcow || __windows_layerchain_exists(base_rootfs))) {
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

        if (vafs_count > 0) {
            source_rootfs = outDir;

            // If we have a BASE_ROOTFS, copy it into the materialized directory first.
            if (base_rootfs_count == 1) {
                if (base_rootfs == NULL || base_rootfs[0] == '\0') {
                    VLOG_ERROR("containerv", "containerv_layers_compose: BASE_ROOTFS layer missing source path\n");
                    containerv_layers_destroy(context);
                    errno = EINVAL;
                    return -1;
                }
                if (platform_copydir(base_rootfs, outDir) != 0) {
                    VLOG_ERROR("containerv", "containerv_layers_compose: failed to materialize BASE_ROOTFS into %s (%s: %lu)\n",
                        outDir, platform_copydir_lasterror_operation(), platform_copydir_lasterror());
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
                    containerv_layers_destroy(context);
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
            VLOG_ERROR("containerv", "containerv_layers_compose: missing rootfs content for %s\n", is_lcow ? "LCOW materialization" : "windowsfilter import");
            containerv_layers_destroy(context);
            errno = EINVAL;
            return -1;
        }

        if (!is_lcow) {
            char wcow_dir[MAX_PATH];
            int rc = snprintf(wcow_dir, sizeof(wcow_dir), "%s\\windowsfilter", context->materialized_container_dir);
            if (rc < 0 || (size_t)rc >= sizeof(wcow_dir)) {
                containerv_layers_destroy(context);
                errno = EINVAL;
                return -1;
            }

            if (__windowsfilter_import_from_dir(wcow_dir, source_rootfs, parents, parent_count) != 0) {
                VLOG_ERROR("containerv", "containerv_layers_compose: failed to import windowsfilter layer from %s\n", source_rootfs);
                containerv_layers_destroy(context);
                return -1;
            }

            context->windowsfilter_dir = _strdup(wcow_dir);
            free(context->composed_rootfs);
            context->composed_rootfs = _strdup(wcow_dir);
        }
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
        compose_options.windows_is_lcow = options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX;
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
    
    if (context->composed_rootfs_is_materialized && context->materialized_container_dir != NULL &&
        containerv_disk_path_is_directory(context->materialized_container_dir)) {
        if (platform_rmdir(context->materialized_container_dir) != 0) {
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
