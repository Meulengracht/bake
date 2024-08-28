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

#include <chef/rootfs/debootstrap.h>
#include <chef/platform.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vlog.h>

static void __debootstrap_output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_TRACE("containerv", line);
    } else {
        VLOG_ERROR("containerv", line);
    }
}

int container_rootfs_setup_debootstrap(const char* path)
{
    char  scratchPad[PATH_MAX];
    int   status;
    pid_t child, wt;
    VLOG_DEBUG("containerv", "container_rootfs_setup_debootstrap(path=%s)\n", path);

    status = platform_spawn("debootstrap", "--version", NULL, &(struct platform_spawn_options) {
        .output_handler = __debootstrap_output_handler
    });
    if (status) {
        VLOG_ERROR("containerv", "container_rootfs_setup_debootstrap: \"debootstrap\" package must be installed\n");
        return status;
    }
    
    snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase stable %s http://deb.debian.org/debian/", path);
    VLOG_DEBUG("containerv", "executing 'debootstrap %s'\n", &scratchPad[0]);

    child = fork();
    if (child == 0) {
        // debootstrap must run under the root user, so lets make sure we've switched
        // to root as the real user.
        if (setuid(geteuid()) || setgid(getegid())) {
            VLOG_ERROR("containerv", "container_rootfs_setup_debootstrap: failed to switch to root\n");
            _Exit(-1);
        }

        status = platform_spawn("debootstrap", &scratchPad[0], NULL, &(struct platform_spawn_options) {
            .output_handler = __debootstrap_output_handler
        });
        if (status) {
            VLOG_ERROR("containerv", "container_rootfs_setup_debootstrap: \"debootstrap\" failed: %i\n", status);
            VLOG_ERROR("containerv", "see %s/debootstrap/debootstrap.log for details\n", path);
            _Exit(-1);
        }
        _Exit(0);
    } else {
        wt = wait(&status);
    }
    return status;
}
