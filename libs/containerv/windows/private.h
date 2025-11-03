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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#ifndef __CONTAINERV_WINDOWS_PRIVATE_H__
#define __CONTAINERV_WINDOWS_PRIVATE_H__

#include <windows.h>
#include <chef/containerv.h>
#include <chef/list.h>

#define __CONTAINER_SOCKET_RUNTIME_BASE "\\\\.\\pipe\\containerv"
#define __CONTAINER_ID_LENGTH 8

struct containerv_options {
    enum containerv_capabilities capabilities;
    struct containerv_mount*     mounts;
    int                          mounts_count;
};

struct containerv_container_process {
    struct list_item list_header;
    HANDLE           handle;
    DWORD            pid;
};

struct containerv_container {
    // HyperV VM handle and configuration
    HANDLE       vm_handle;
    char*        rootfs;
    char*        hostname;
    
    // Process management
    struct list  processes;
    
    // Container identification
    char         id[__CONTAINER_ID_LENGTH + 1];
    char*        runtime_dir;
    
    // Communication pipes
    HANDLE       host_pipe;
    HANDLE       child_pipe;
};

/**
 * @brief Generate a unique container ID
 */
extern void containerv_generate_id(char* buffer, size_t length);

/**
 * @brief Create runtime directory for container
 */
extern char* containerv_create_runtime_dir(void);

/**
 * @brief Internal spawn implementation
 */
struct __containerv_spawn_options {
    const char*                path;
    const char* const*         argv;
    const char* const*         envv;
    enum container_spawn_flags flags;
};

extern int __containerv_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, HANDLE* handleOut);
extern int __containerv_kill(struct containerv_container* container, HANDLE handle);
extern void __containerv_destroy(struct containerv_container* container);

#endif //!__CONTAINERV_WINDOWS_PRIVATE_H__
