#include "standard-mounts.h"

#include <stddef.h>

static const char* const g_linux_mountpoints[] = {
    "/proc",
    "/sys",
    "/dev",
    "/dev/pts",
    "/dev/shm",
    NULL,
};

const char* const* containerv_standard_linux_mountpoints(void)
{
    return g_linux_mountpoints;
}
