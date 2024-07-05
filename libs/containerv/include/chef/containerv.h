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

#ifndef __CONTAINERV_H__
#define __CONTAINERV_H__

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
typedef HANDLE process_handle_t;
#elif defined(__linux__) || defined(__unix__)
#include <sys/types.h>
typedef pid_t process_handle_t;
#endif

struct containerv_container;

enum containerv_mount_flags {
    CV_MOUNT_BIND = 0x1,
    CV_MOUNT_RECURSIVE = 0x2,
    CV_MOUNT_READONLY = 0x4
};

struct containerv_mount {
    char*                       source;
    char*                       destination;
    char*                       fstype;
    enum containerv_mount_flags flags;
};

enum containerv_capabilities {
    CV_CAP_NETWORK = 0x1,
    CV_CAP_PROCESS_CONTROL = 0x2,
    CV_CAP_IPC = 0x4,
    CV_CAP_FILESYSTEM = 0x8,
    CV_CAP_CGROUPS = 0x10
};

extern int containerv_create(
    const char*                   rootFs,
    enum containerv_capabilities  capabilities,
    struct containerv_mount*      mounts,
    int                           mountsCount,
    struct containerv_container** containerOut
);

enum container_spawn_flags {
    CV_SPAWN_WAIT = 0x1,
};

struct containerv_spawn_options {
    const char*                arguments;
    const char* const*         environment;
    enum container_spawn_flags flags;
};

extern int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut
);

extern int container_script(struct containerv_container* container, const char* script);

extern int container_kill(struct containerv_container* container, process_handle_t pid);

extern int container_destroy(struct containerv_container* container);

#endif //!__CONTAINERV_H__