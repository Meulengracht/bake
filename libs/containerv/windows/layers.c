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
    (void)container_id;

    if (context_out == NULL || layers == NULL || layer_count <= 0) {
        errno = EINVAL;
        return -1;
    }

    int saw_overlay = 0;
    int saw_vafs = 0;
    int base_rootfs_count = 0;

    for (int i = 0; i < layer_count; ++i) {
        switch (layers[i].type) {
            case CONTAINERV_LAYER_BASE_ROOTFS:
                base_rootfs_count++;
                break;
            case CONTAINERV_LAYER_VAFS_PACKAGE:
                saw_vafs = 1;
                break;
            case CONTAINERV_LAYER_OVERLAY:
                saw_overlay = 1;
                break;
            case CONTAINERV_LAYER_HOST_DIRECTORY:
            default:
                break;
        }
    }

    if (base_rootfs_count != 1) {
        VLOG_ERROR("containerv", "containerv_layers_compose: expected exactly one BASE_ROOTFS layer, got %d\n", base_rootfs_count);
        errno = EINVAL;
        return -1;
    }

    if (saw_vafs) {
        VLOG_ERROR("containerv", "containerv_layers_compose: VAFS package layers are not supported on Windows\n");
        errno = ENOTSUP;
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
    if (context->layers == NULL) {
        free(context);
        errno = ENOMEM;
        return -1;
    }

    context->layer_count = layer_count;

    for (int i = 0; i < layer_count; ++i) {
        context->layers[i].type = layers[i].type;
        context->layers[i].readonly = layers[i].readonly;
        context->layers[i].source = layers[i].source ? _strdup(layers[i].source) : NULL;
        context->layers[i].target = layers[i].target ? _strdup(layers[i].target) : NULL;

        if ((layers[i].source && context->layers[i].source == NULL) ||
            (layers[i].target && context->layers[i].target == NULL)) {
            __free_layer_copy(context->layers, context->layer_count);
            free(context);
            errno = ENOMEM;
            return -1;
        }
    }

    // Minimal Windows implementation: pick BASE_ROOTFS as the composed rootfs.
    // OVERLAY is treated as a no-op; VAFS_PACKAGE is rejected above.
    for (int i = 0; i < layer_count; ++i) {
        if (layers[i].type == CONTAINERV_LAYER_BASE_ROOTFS && layers[i].source) {
            context->composed_rootfs = _strdup(layers[i].source);
            break;
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
