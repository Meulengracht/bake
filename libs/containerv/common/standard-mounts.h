#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Internal shared list of standard Linux mountpoints used by OCI specs and rootfs prep.
// Paths are Linux-style absolute paths (e.g. "/proc"). The list is NULL-terminated.
const char* const* containerv_standard_linux_mountpoints(void);

#ifdef __cplusplus
}
#endif
