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

#include <windows.h>
#include <chef/containerv.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "private.h"

static char* __generate_container_id(void)
{
    static const char charset[] = "0123456789abcdef";
    char* id = malloc(__CONTAINER_ID_LENGTH + 1);
    if (id == NULL) {
        return NULL;
    }

    // Use Windows crypto API for random generation
    for (int i = 0; i < __CONTAINER_ID_LENGTH; i++) {
        id[i] = charset[rand() % (sizeof(charset) - 1)];
    }
    id[__CONTAINER_ID_LENGTH] = '\0';
    return id;
}

static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;
    char* id;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    id = __generate_container_id();
    if (id == NULL) {
        free(container);
        return NULL;
    }

    memcpy(&container->id[0], id, __CONTAINER_ID_LENGTH + 1);
    free(id);

    list_construct(&container->processes);
    InitializeCriticalSection(&container->lock);
    
    container->init_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (container->init_event == NULL) {
        DeleteCriticalSection(&container->lock);
        free(container);
        return NULL;
    }

    return container;
}

static void __container_delete(struct containerv_container* container)
{
    if (container == NULL) {
        return;
    }

    if (container->rootfs) {
        free(container->rootfs);
    }
    if (container->hostname) {
        free(container->hostname);
    }
    if (container->init_event) {
        CloseHandle(container->init_event);
    }
    DeleteCriticalSection(&container->lock);
    free(container);
}

int containerv_create(
    const char*                   rootfs,
    struct containerv_options*    options,
    struct containerv_container** containerOut)
{
    struct containerv_container* container;
    int status;

    if (rootfs == NULL || containerOut == NULL) {
        VLOG_ERROR("containerv", "containerv_create: invalid parameters\n");
        return -1;
    }

    VLOG_DEBUG("containerv", "containerv_create(rootfs=%s)\n", rootfs);

    container = __container_new();
    if (container == NULL) {
        VLOG_ERROR("containerv", "containerv_create: failed to allocate container\n");
        return -1;
    }

    container->rootfs = platform_strdup(rootfs);
    if (container->rootfs == NULL) {
        VLOG_ERROR("containerv", "containerv_create: failed to duplicate rootfs path\n");
        __container_delete(container);
        return -1;
    }

    // Create the HCS container
    status = containerv_hcs_create(rootfs, options, &container);
    if (status) {
        VLOG_ERROR("containerv", "containerv_create: failed to create HCS container\n");
        __container_delete(container);
        return status;
    }

    *containerOut = container;
    VLOG_DEBUG("containerv", "containerv_create: container created with id %s\n", container->id);
    return 0;
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                handleOut)
{
    HANDLE process_handle;
    int status;

    if (container == NULL || path == NULL) {
        VLOG_ERROR("containerv", "containerv_spawn: invalid parameters\n");
        return -1;
    }

    VLOG_DEBUG("containerv", "containerv_spawn(path=%s)\n", path);

    status = containerv_hcs_spawn(container, path, options, &process_handle);
    if (status) {
        VLOG_ERROR("containerv", "containerv_spawn: failed to spawn process\n");
        return status;
    }

    if (handleOut) {
        *handleOut = process_handle;
    }

    // If wait flag is set, wait for process to complete
    if (options && (options->flags & CV_SPAWN_WAIT)) {
        WaitForSingleObject(process_handle, INFINITE);
    }

    return 0;
}

int containerv_kill(struct containerv_container* container, process_handle_t handle)
{
    if (container == NULL) {
        VLOG_ERROR("containerv", "containerv_kill: invalid container\n");
        return -1;
    }

    VLOG_DEBUG("containerv", "containerv_kill()\n");

    return containerv_hcs_kill(container, handle);
}

int containerv_upload(
    struct containerv_container* container,
    const char* const*           hostPaths,
    const char* const*           containerPaths,
    int                          count)
{
    // TODO: Implement file upload using Windows APIs
    VLOG_WARNING("containerv", "containerv_upload: not yet implemented on Windows\n");
    return -1;
}

int containerv_download(
    struct containerv_container* container,
    const char* const*           containerPaths,
    const char* const*           hostPaths,
    int                          count)
{
    // TODO: Implement file download using Windows APIs
    VLOG_WARNING("containerv", "containerv_download: not yet implemented on Windows\n");
    return -1;
}

int containerv_destroy(struct containerv_container* container)
{
    if (container == NULL) {
        VLOG_ERROR("containerv", "containerv_destroy: invalid container\n");
        return -1;
    }

    VLOG_DEBUG("containerv", "containerv_destroy(id=%s)\n", container->id);

    containerv_hcs_destroy(container);
    __container_delete(container);
    return 0;
}

int containerv_join(const char* containerId)
{
    // TODO: Implement container join functionality
    VLOG_WARNING("containerv", "containerv_join: not yet implemented on Windows\n");
    return -1;
}

const char* containerv_id(struct containerv_container* container)
{
    if (container == NULL) {
        return NULL;
    }
    return &container->id[0];
}
