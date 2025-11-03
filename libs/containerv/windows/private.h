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

#ifndef __WINDOWS_PRIVATE_H__
#define __WINDOWS_PRIVATE_H__

#include <windows.h>
#include <chef/containerv.h>
#include <chef/list.h>

#define __CONTAINER_ID_LENGTH 8

struct containerv_options {
    enum containerv_capabilities capabilities;
};

struct containerv_container_process {
    struct list_item list_header;
    HANDLE           handle;
    DWORD            pid;
};

struct containerv_container {
    // Container identification
    char id[__CONTAINER_ID_LENGTH + 1];
    
    // HCS (Host Compute Service) handle
    HANDLE hcs_handle;
    
    // Container processes
    struct list processes;
    
    // Container configuration
    char* rootfs;
    char* hostname;
    
    // Synchronization
    HANDLE init_event;
    CRITICAL_SECTION lock;
};

/**
 * @brief Initialize HCS (Host Compute Service) subsystem
 * @return 0 on success, negative on error
 */
extern int containerv_hcs_initialize(void);

/**
 * @brief Cleanup HCS subsystem
 */
extern void containerv_hcs_cleanup(void);

/**
 * @brief Create a new HCS container
 * @param rootfs Path to the container root filesystem
 * @param options Container options
 * @param containerOut Output container handle
 * @return 0 on success, negative on error
 */
extern int containerv_hcs_create(
    const char* rootfs,
    struct containerv_options* options,
    struct containerv_container** containerOut
);

/**
 * @brief Start a container
 * @param container The container to start
 * @return 0 on success, negative on error
 */
extern int containerv_hcs_start(struct containerv_container* container);

/**
 * @brief Stop a container
 * @param container The container to stop
 * @return 0 on success, negative on error
 */
extern int containerv_hcs_stop(struct containerv_container* container);

/**
 * @brief Destroy a container and cleanup resources
 * @param container The container to destroy
 */
extern void containerv_hcs_destroy(struct containerv_container* container);

/**
 * @brief Spawn a process in the container
 * @param container The container to spawn in
 * @param path Process path
 * @param options Spawn options
 * @param handleOut Output process handle
 * @return 0 on success, negative on error
 */
extern int containerv_hcs_spawn(
    struct containerv_container* container,
    const char* path,
    struct containerv_spawn_options* options,
    HANDLE* handleOut
);

/**
 * @brief Kill a process in the container
 * @param container The container
 * @param handle Process handle to kill
 * @return 0 on success, negative on error
 */
extern int containerv_hcs_kill(
    struct containerv_container* container,
    HANDLE handle
);

#endif //!__WINDOWS_PRIVATE_H__
