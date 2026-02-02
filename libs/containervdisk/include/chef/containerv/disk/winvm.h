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
 * Windows VM disk preparation for containerv.
 *
 * This is a Windows-host-only helper. On non-Windows hosts the functions are
 * no-ops and return success.
 */

#ifndef __CONTAINERV_DISK_WINVM_H__
#define __CONTAINERV_DISK_WINVM_H__

// avoid including containerv headers here
struct containerv_container;
struct containerv_layer;

// Prototype for now, but we want to avoid including chef_cvd_service.h here.
// TODO: Create a new structure that mirrors the needed fields.
struct chef_create_parameters;

struct containerv_disk_winvm_prepare_result {
    // If non-NULL, a temporary directory created by the preparer that should
    // be deleted after layer composition has copied its contents.
    char* staging_rootfs;

    // Non-zero if VAFS packages were applied into the VHD chain and the
    // post-boot guest provisioning step should be skipped.
    int applied_packages;
};

/**
 * @brief Prepare a Windows VM boot disk chain (base/cache + app layer + writable).
 *
 * If this function decides the request is not a Windows-VM-disk scenario,
 * it returns 0 and leaves layers unchanged.
 *
 * On success, it may replace the layer array with a filtered/adjusted version
 * and return a staging rootfs directory containing a `container.vhdx`.
 */
extern int containerv_disk_winvm_prepare_layers(
    const char*                         container_id,
    struct containerv_layer**           layers_inout,
    int*                                layer_count_inout,
    struct containerv_disk_winvm_prepare_result* result_out
);

extern int containerv_disk_winvm_provision(
    struct containerv_container*         container, 
    const struct chef_create_parameters* params
);

extern void containerv_disk_winvm_prepare_result_destroy(
    struct containerv_disk_winvm_prepare_result* result
);

#endif // !__CONTAINERV_DISK_WINVM_H__
