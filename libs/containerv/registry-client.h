/**
 * Copyright 2024, Philip Meulengracht
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

#ifndef __REGISTRY_CLIENT_H__
#define __REGISTRY_CLIENT_H__

#include <chef/containerv.h>
#include <stdbool.h>
#include <stdint.h>

// Forward declarations
struct registry_client;

/**
 * @brief Create a new registry client
 * @param registry Registry hostname (NULL for Docker Hub)
 * @param username Username for authentication (NULL for anonymous)
 * @param password Password or token for authentication (NULL for anonymous)
 * @return Registry client instance or NULL on failure
 */
extern struct registry_client* registry_client_create(
    const char* registry,
    const char* username,
    const char* password
);

/**
 * @brief Destroy a registry client and free resources
 * @param client Registry client to destroy
 */
extern void registry_client_destroy(struct registry_client* client);

/**
 * @brief Download image manifest from registry
 * @param client Registry client instance
 * @param image_ref Image reference to get manifest for
 * @param manifest_json Output pointer to manifest JSON string (caller must free)
 * @return 0 on success, -1 on failure
 */
extern int registry_get_manifest(
    struct registry_client* client,
    const struct containerv_image_ref* image_ref,
    char** manifest_json
);

/**
 * @brief Download a blob (layer or config) from registry
 * @param client Registry client instance  
 * @param image_ref Image reference (for repository context)
 * @param digest Blob digest (sha256:...)
 * @param output_path Local file path to save blob
 * @param progress_callback Optional progress callback function
 * @param callback_data User data for progress callback
 * @return 0 on success, -1 on failure
 */
extern int registry_download_blob(
    struct registry_client* client,
    const struct containerv_image_ref* image_ref,
    const char* digest,
    const char* output_path,
    void (*progress_callback)(const char* status, int percent, void* data),
    void* callback_data
);

/**
 * @brief Check if an image exists in the registry
 * @param client Registry client instance
 * @param image_ref Image reference to check
 * @return 1 if exists, 0 if not found, -1 on error
 */
extern int registry_image_exists(
    struct registry_client* client,
    const struct containerv_image_ref* image_ref
);

/**
 * @brief Upload a blob to registry
 * @param client Registry client instance
 * @param image_ref Image reference (for repository context) 
 * @param blob_path Local file path of blob to upload
 * @param digest Expected digest of the blob
 * @return 0 on success, -1 on failure
 */
extern int registry_upload_blob(
    struct registry_client* client,
    const struct containerv_image_ref* image_ref,
    const char* blob_path,
    const char* digest
);

/**
 * @brief Upload image manifest to registry
 * @param client Registry client instance
 * @param image_ref Image reference to upload manifest for
 * @param manifest_json Manifest JSON content
 * @return 0 on success, -1 on failure
 */
extern int registry_put_manifest(
    struct registry_client* client,
    const struct containerv_image_ref* image_ref,
    const char* manifest_json
);

#endif // __REGISTRY_CLIENT_H__