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

#ifndef __CVD_VAFS_MOUNT_MANAGER_H__
#define __CVD_VAFS_MOUNT_MANAGER_H__

#include <chef/containerv/layers.h>
#include <chef/platform.h>

/**
 * @brief Opaque set of VaFS mount references held by one container.
 *
 * The activation keeps every package mount acquired while preparing a
 * container alive until the container has stopped. Callers must release it
 * exactly once with cvd_vafs_layer_instance_release().
 */
struct cvd_vafs_layer_instance;

#ifdef CHEF_ON_LINUX

/**
 * @brief Initialize the process-wide VaFS mount manager.
 *
 * Initialization is idempotent. The manager must be initialized before
 * preparing layers; prepare_layers() also initializes it defensively.
 *
 * @return 0 on success, -1 if the manager lock cannot be initialized.
 */
extern int cvd_vafs_mount_manager_initialize(void);

/**
 * @brief Stop and release all mounts owned by the manager.
 *
 * Normal container teardown should release activations first. This function
 * is a final daemon-shutdown safety net and may force-release mounts that
 * still have active references.
 */
extern void cvd_vafs_mount_manager_shutdown(void);

/**
 * @brief Acquire the VaFS mounts required by a layer array.
 *
 * Each CONTAINERV_LAYER_VAFS_PACKAGE layer is mounted in the daemon's mount
 * namespace and rewritten in place as a read-only BASE_ROOTFS layer whose
 * source points to that mount. Package paths are shared process-wide and are
 * reference counted, so containers using the same package reuse one FUSE
 * server and mount.
 *
 * The mounts must be acquired before containerv_create() forks. The child
 * then inherits the daemon's mounts when it unshares its mount namespace.
 * On failure, all references acquired by this call are rolled back and no
 * activation is returned.
 *
 * @param layers Layer descriptors to prepare; VaFS entries are modified.
 * @param layerCount Number of entries in layers.
 * @param instanceOut Receives the activation owned by the caller.
 * @return 0 on success, -1 on invalid input or mount failure.
 */
extern int cvd_vafs_mount_manager_prepare_layers(
    struct containerv_layer*         layers,
    int                              layerCount,
    struct cvd_vafs_layer_instance** instanceOut);

/**
 * @brief Release all package mount references in an activation.
 *
 * The final reference to a package stops its FUSE worker, unmounts the
 * package, closes the VaFS reader, and frees the manager entry.
 */
extern void cvd_vafs_layer_instance_release(
    struct cvd_vafs_layer_instance* instance);

#else

// Only Linux activates VaFS packages through cvd; other backends consume the
// package files directly, so the manager collapses to no-ops.
static inline int cvd_vafs_mount_manager_initialize(void) { return 0; }
static inline void cvd_vafs_mount_manager_shutdown(void) { }
static inline void cvd_vafs_layer_instance_release(struct cvd_vafs_layer_instance* instance) { (void)activation; }

static inline int cvd_vafs_mount_manager_prepare_layers(
    struct containerv_layer*         layers,
    int                              layerCount,
    struct cvd_vafs_layer_instance** instanceOut)
{
    (void)layers;
    (void)layerCount;
    *instanceOut = NULL;
    return 0;
}

#endif

#endif //!__CVD_VAFS_MOUNT_MANAGER_H__