#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Internal helper for generating OCI runtime-spec JSON used by LCOW (OCI-in-UVM).
// Not part of the public API.

struct containerv_oci_linux_spec_params {
    // JSON array string of args, e.g. ["/bin/sh","-lc","echo hi"].
    const char* args_json;

    // Array of KEY=VALUE strings (NULL-terminated). Optional.
    const char* const* envv;

    // OCI root.path (e.g. "/chef/rootfs"). Required.
    const char* root_path;

    // process.cwd (e.g. "/"). Optional; defaults to "/".
    const char* cwd;

    // hostname (optional).
    const char* hostname;
};

int containerv_oci_build_linux_spec_json(
    const struct containerv_oci_linux_spec_params* params,
    char** out_json_utf8);

#ifdef __cplusplus
}
#endif
