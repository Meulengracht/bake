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
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

/**
 * @brief Layer context structure (Windows stub)
 */
struct containerv_layer_context {
    char* composed_rootfs;
    struct containerv_layer* layers;
    int                      layer_count;
    // TODO: Windows HCI layer handles
};

static void __spawn_output_handler(const char* line, enum platform_spawn_output_type type)
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_DEBUG("containerv[layers]", line);
    } else {
        VLOG_ERROR("containerv[layers]", line);
    }
}

static char* __create_windows_layers_rootfs_dir(const char* container_id)
{
    char tempPath[MAX_PATH];
    DWORD written = GetTempPathA(MAX_PATH, tempPath);
    if (written == 0 || written >= MAX_PATH) {
        return NULL;
    }

    // %TEMP%\chef-layers\<id>\rootfs
    char root[MAX_PATH];
    int rc = snprintf(root, sizeof(root), "%s\\chef-layers\\%s\\rootfs", tempPath,
                      container_id ? container_id : "unknown");
    if (rc < 0 || (size_t)rc >= sizeof(root)) {
        return NULL;
    }

    // Best-effort create all directories.
    char base[MAX_PATH];
    rc = snprintf(base, sizeof(base), "%s\\chef-layers", tempPath);
    if (rc < 0 || (size_t)rc >= sizeof(base)) {
        return NULL;
    }
    CreateDirectoryA(base, NULL);

    char idDir[MAX_PATH];
    rc = snprintf(idDir, sizeof(idDir), "%s\\%s", base, container_id ? container_id : "unknown");
    if (rc < 0 || (size_t)rc >= sizeof(idDir)) {
        return NULL;
    }
    CreateDirectoryA(idDir, NULL);

    CreateDirectoryA(root, NULL);
    return _strdup(root);
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

int containerv_layers_compose(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
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

    context->layers = calloc((size_t)layer_count, sizeof(struct containerv_layer));
    // If no VAFS layers are present, we can use BASE_ROOTFS directly.
    // If VAFS layers are present (with or without BASE_ROOTFS), we materialize into a directory.
    if (vafs_count == 0 && base_rootfs_count == 1) {
        context->composed_rootfs = base_rootfs ? _strdup(base_rootfs) : NULL;
    } else {
        char* outDir = __create_windows_layers_rootfs_dir(container_id);
        if (outDir == NULL) {
            VLOG_ERROR("containerv", "containerv_layers_compose: failed to create layers directory\n");
            containerv_layers_destroy(context);
            errno = ENOMEM;
            return -1;
        }

        // If we have a BASE_ROOTFS, copy it into the materialized directory first.
        if (base_rootfs_count == 1) {
            if (base_rootfs == NULL || base_rootfs[0] == '\0') {
                VLOG_ERROR("containerv", "containerv_layers_compose: BASE_ROOTFS layer missing source path\n");
                free(outDir);
                containerv_layers_destroy(context);
                errno = EINVAL;
                return -1;
            }
            if (__copy_tree_recursive(base_rootfs, outDir) != 0) {
                VLOG_ERROR("containerv", "containerv_layers_compose: failed to materialize BASE_ROOTFS into %s\n", outDir);
                free(outDir);
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
                free(outDir);
                containerv_layers_destroy(context);
                errno = EINVAL;
                return -1;
            }

            char args[4096];
            int rc = snprintf(args, sizeof(args), "--no-progress --out \"%s\" \"%s\"", outDir, layers[i].source);
            if (rc < 0 || (size_t)rc >= sizeof(args)) {
                free(outDir);
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
                free(outDir);
                containerv_layers_destroy(context);
                errno = EIO;
                return -1;
            }
        }

        context->composed_rootfs = outDir;
    }
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
                free(outDir);
                containerv_layers_destroy(context);
                errno = EIO;
                return -1;
            }
        }

        context->composed_rootfs = outDir;
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
    free(context->composed_rootfs);
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
