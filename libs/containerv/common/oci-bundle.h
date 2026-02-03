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

#ifndef __CONTAINERV_OCI_BUNDLE_H__
#define __CONTAINERV_OCI_BUNDLE_H__

#include <stdint.h>

// Internal helper for preparing an OCI bundle directory on the host.
// Not part of the public API.

struct containerv_oci_bundle_paths {
    char* bundle_dir;   // e.g. <runtime_dir>/oci-bundle
    char* rootfs_dir;   // e.g. <runtime_dir>/oci-bundle/rootfs
    char* config_path;  // e.g. <runtime_dir>/oci-bundle/config.json
};

// Populates paths (allocates strings). Does not create anything on disk.
extern int containerv_oci_bundle_get_paths(
    const char* runtime_dir,
    struct containerv_oci_bundle_paths* out
);

// Ensures bundle dir exists and prepares rootfs/ as either a symlink to source_rootfs
// or as a copied directory tree. If source_rootfs is NULL, just creates an empty rootfs dir.
extern int containerv_oci_bundle_prepare_rootfs(
    const struct containerv_oci_bundle_paths* paths,
    const char* source_rootfs
);

// Writes config.json into the bundle directory.
extern int containerv_oci_bundle_write_config(
    const struct containerv_oci_bundle_paths* paths,
    const char* oci_config_json
);

// Creates standard mountpoint target directories inside rootfs.
// This is best-effort and intended to reduce runtime failures.
extern int containerv_oci_bundle_prepare_rootfs_mountpoints(
    const struct containerv_oci_bundle_paths* paths
);

// Ensures a directory exists inside rootfs for a Linux container path (best-effort).
// Useful for LCOW bind mount targets and staging paths.
extern int containerv_oci_bundle_prepare_rootfs_dir(
    const struct containerv_oci_bundle_paths* paths,
    const char* linux_path,
    uint32_t permissions
);

// Creates /etc/hosts, /etc/hostname, /etc/resolv.conf inside rootfs (best-effort).
extern int containerv_oci_bundle_prepare_rootfs_standard_files(
    const struct containerv_oci_bundle_paths* paths,
    const char* hostname,
    const char* dns_servers
);

extern void containerv_oci_bundle_paths_delete(
    struct containerv_oci_bundle_paths* paths
);

#endif // !__CONTAINERV_OCI_BUNDLE_H__
