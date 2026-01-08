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

#include <chef/rootfs/debootstrap.h>
#include <chef/platform.h>
#include <poll.h>
#include <sys/wait.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

static void __print(const char* line, int error) {
    if (error) {
        VLOG_ERROR("debootstrap", line);
    } else {
        VLOG_TRACE("debootstrap", line);
    }
}

static void __report(char* line, int error)
{
    const char* s = line;
    char*       p = line;
    char        tmp[2048];

    while (*p) {
        if (*p == '\n') {
            // include the \n
            size_t count = (size_t)(p - s) + 1;
            strncpy(&tmp[0], s, count);

            // zero terminate the string and report
            tmp[count] = '\0';
            __print(&tmp[0], error);

            // update new start
            s = ++p;
        } else {
            p++;
        }
    }
    
    // only do a final report if the line didn't end with a newline
    if (s != p) {
        __print(s, error);
    }
}

// 0 => stdout
// 1 => stderr
static void __wait_and_read_stds(struct pollfd* fds)
{
    char line[2048];

    for (;;) {
        int status = poll(fds, 2, -1);
        if (status <= 0) {
            return;
        }
        if (fds[0].revents & POLLIN) {
            status = read(fds[0].fd, &line[0], sizeof(line));
            line[status] = 0;
            __report(&line[0], 0);
        } else if (fds[1].revents & POLLIN) {
            status = read(fds[1].fd, &line[0], sizeof(line));
            line[status] = 0;
            __report(&line[0], 1);
        } else {
            break;
        }
    }
}

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
    int   outp[2] = { 0 };
    int   errp[2] = { 0 };
    pid_t child, wt;
    VLOG_DEBUG("containerv", "container_rootfs_setup_debootstrap(path=%s)\n", path);

    status = platform_spawn("debootstrap", "--version", NULL, &(struct platform_spawn_options) {
        .output_handler = __debootstrap_output_handler
    });
    if (status) {
        VLOG_ERROR("containerv", "container_rootfs_setup_debootstrap: \"debootstrap\" package must be installed\n");
        return status;
    }

    // let's redirect and poll for output
    if (pipe(outp) || pipe(errp)) {
        if (outp[0] > 0) {
            close(outp[0]);
            close(outp[1]);
        }
        VLOG_ERROR("containerv", "container_rootfs_setup_debootstrap: failed to create descriptors\n");
        return -1;
    }
    
    snprintf(&scratchPad[0], sizeof(scratchPad), "--variant=minbase stable %s http://deb.debian.org/debian/", path);
    VLOG_DEBUG("containerv", "executing 'debootstrap %s'\n", &scratchPad[0]);

    child = fork();
    if (child == 0) {
        // close pipes we don't need on the child side
        close(outp[0]);
        close(errp[0]);

        // switch output, from this point forward we do not use VLOG_*
        dup2(outp[1], STDOUT_FILENO);
        close(outp[1]);
        dup2(errp[1], STDERR_FILENO);
        close(errp[1]);

        // debootstrap must run under the root user, so lets make sure we've switched
        // to root as the real user.
        if (setuid(geteuid()) || setgid(getegid())) {
            fprintf(stdout, "container_rootfs_setup_debootstrap: failed to switch to root\n");
            _Exit(-1);
        }

        status = platform_spawn("debootstrap", &scratchPad[0], NULL, &(struct platform_spawn_options) {});
        if (status) {
            fprintf(stdout, "container_rootfs_setup_debootstrap: \"debootstrap\" failed: %i\n", status);
            fprintf(stdout, "see %s/debootstrap/debootstrap.log for details\n", path);
        }
        _Exit(-status);
    } else {
        struct pollfd fds[2] = { 
            { 
                .fd = outp[0],
                .events = POLLIN
            },
            {
                .fd = errp[0],
                .events = POLLIN
            }
        };

        // close child-side of pipes
        close(outp[1]);
        close(errp[1]); 

        __wait_and_read_stds(&fds[0]);

        // close host-side of pipes
        close(outp[0]);
        close(errp[0]); 
        
        wt = wait(&status);
    }
    return status;
}
