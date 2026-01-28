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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "../private.h"
#include <vlog.h>

// Define Windows-specific debootstrap options to match Linux interface
struct containerv_rootfs_debootstrap {
    enum windows_rootfs_type    rootfs_type;      // Windows rootfs selection
    const char*                 version;          // OS version (e.g., "22.04", "ltsc2022")
    const char*                 mirror_url;       // Custom image URL (for CUSTOM type)
    int                         enable_updates;   // Enable OS updates during setup
};

int containerv_rootfs_setup_debootstrap(const char* path)
{
    // Default Windows rootfs setup - equivalent to Linux debootstrap
    struct containerv_options_rootfs rootfs_opts = {
        .type = WINDOWS_ROOTFS_WSL_UBUNTU,  // Default to WSL Ubuntu for compatibility
        .version = "22.04",                 // Ubuntu 22.04 LTS
        .enable_updates = 1,                // Enable updates
        .custom_image_url = NULL
    };

    VLOG_DEBUG("containerv", "containerv_rootfs_setup_debootstrap(path=%s) - Windows\n", path);
    VLOG_DEBUG("containerv", "using WSL Ubuntu 22.04 as Linux debootstrap equivalent\n");

    return __windows_setup_rootfs(path, &rootfs_opts);
}

int containerv_rootfs_debootstrap(
    const char*                            path,
    struct containerv_rootfs_debootstrap*  options
)
{
    struct containerv_options_rootfs rootfs_opts;

    if (!path) {
        VLOG_ERROR("containerv", "containerv_rootfs_debootstrap: path cannot be NULL\n");
        return -1;
    }

    VLOG_DEBUG("containerv", "containerv_rootfs_debootstrap(path=%s) - Windows\n", path);

    // Convert debootstrap options to Windows rootfs options
    if (options) {
        rootfs_opts.type = options->rootfs_type;
        rootfs_opts.version = options->version;
        rootfs_opts.custom_image_url = options->mirror_url;
        rootfs_opts.enable_updates = options->enable_updates;
    } else {
        // Use defaults
        rootfs_opts.type = WINDOWS_ROOTFS_WSL_UBUNTU;
        rootfs_opts.version = "22.04";
        rootfs_opts.custom_image_url = NULL;
        rootfs_opts.enable_updates = 1;
    }

    return __windows_setup_rootfs(path, &rootfs_opts);
}
