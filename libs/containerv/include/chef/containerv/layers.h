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

#ifndef __CONTAINERV_LAYERS_H__
#define __CONTAINERV_LAYERS_H__

#include <stdint.h>

/**
 * @brief Layer types for container composition
 */
enum containerv_layer_type {
    /** Base rootfs layer (debootstrap, image, or path) */
    CONTAINERV_LAYER_BASE_ROOTFS,
    /** VaFS package layer (.pack file) */
    CONTAINERV_LAYER_VAFS_PACKAGE,
    /** Host directory bind mount */
    CONTAINERV_LAYER_HOST_DIRECTORY,
    /** Writable overlay layer */
    CONTAINERV_LAYER_OVERLAY
};

/**
 * @brief Layer descriptor for container composition
 */
struct containerv_layer {
    enum containerv_layer_type type;
    
    /** Source path - interpretation depends on type:
     *  - BASE_ROOTFS: path to rootfs directory
     *  - VAFS_PACKAGE: path to .pack file
     *  - HOST_DIRECTORY: host path to bind
     *  - OVERLAY: working directory path (or NULL for auto)
     */
    char* source;
    
    /** Target mount point (for HOST_DIRECTORY type) */
    char* target;
    
    /** Base image name (for BASE_ROOTFS type with image) */
    char* base_image;
    
    /** Read-only flag */
    int readonly;
};

/**
 * @brief Opaque context for layer composition
 */
struct containerv_layer_context;

/**
 * @brief Compose multiple layers into a unified rootfs
 * 
 * This function will:
 * - Mount VaFS packages using FUSE
 * - Set up overlayfs with multiple layers
 * - Handle base rootfs setup
 * - Prepare the final composed rootfs
 * 
 * @param layers Array of layer descriptors
 * @param layer_count Number of layers
 * @param container_id Container ID for unique paths
 * @param context_out Output context containing layer composition state
 * @return 0 on success, -1 on failure
 */
extern int containerv_layers_compose(
    struct containerv_layer*          layers,
    int                               layer_count,
    const char*                       container_id,
    struct containerv_layer_context** context_out
);

/**
 * @brief Mount the composed layers into an existing namespace
 */
extern int containerv_layers_mount_in_namespace(struct containerv_layer_context* context);

/**
 * @brief Get the composed rootfs path from layer context
 * 
 * @param context Layer context
 * @return Path to composed rootfs, or NULL on error
 */
extern const char* containerv_layers_get_rootfs(
    struct containerv_layer_context* context
);

/**
 * @brief Clean up and destroy layer context
 * 
 * Unmounts all layers and frees resources
 * 
 * @param context Layer context to destroy
 */
extern void containerv_layers_destroy(
    struct containerv_layer_context* context
);

#endif //!__CONTAINERV_LAYERS_H__
