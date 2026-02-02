#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Internal helper for preparing an OCI bundle directory on the host.
// Not part of the public API.

struct containerv_oci_bundle_paths {
    char* bundle_dir;   // e.g. <runtime_dir>/oci-bundle
    char* rootfs_dir;   // e.g. <runtime_dir>/oci-bundle/rootfs
    char* config_path;  // e.g. <runtime_dir>/oci-bundle/config.json
};

// Populates paths (allocates strings). Does not create anything on disk.
int containerv_oci_bundle_get_paths(
    const char* runtime_dir,
    struct containerv_oci_bundle_paths* out);

// Ensures bundle dir exists and prepares rootfs/ as either a symlink to source_rootfs
// or as a copied directory tree. If source_rootfs is NULL, just creates an empty rootfs dir.
int containerv_oci_bundle_prepare_rootfs(
    const struct containerv_oci_bundle_paths* paths,
    const char* source_rootfs);

// Writes config.json into the bundle directory.
int containerv_oci_bundle_write_config(
    const struct containerv_oci_bundle_paths* paths,
    const char* oci_config_json);

// Creates standard mountpoint target directories inside rootfs.
// This is best-effort and intended to reduce runtime failures.
int containerv_oci_bundle_prepare_rootfs_mountpoints(
    const struct containerv_oci_bundle_paths* paths);

void containerv_oci_bundle_paths_destroy(struct containerv_oci_bundle_paths* paths);

#ifdef __cplusplus
}
#endif
