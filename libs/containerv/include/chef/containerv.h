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

struct containerv_options;
struct containerv_container;
struct containerv_user;

enum containerv_capabilities {
    CV_CAP_NETWORK = 0x1,
    CV_CAP_PROCESS_CONTROL = 0x2,
    CV_CAP_IPC = 0x4,
    CV_CAP_FILESYSTEM = 0x8,
    CV_CAP_CGROUPS = 0x10,
    CV_CAP_USERS = 0x20
};

extern struct containerv_options* containerv_options_new(void);
extern void containerv_options_delete(struct containerv_options* options);
extern void containerv_options_set_caps(struct containerv_options* options, enum containerv_capabilities caps);

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
// TODO
#elif defined(__linux__) || defined(__unix__)
enum containerv_mount_flags {
    CV_MOUNT_BIND = 0x1,
    CV_MOUNT_RECURSIVE = 0x2,
    CV_MOUNT_READONLY = 0x4,
    CV_MOUNT_CREATE = 0x100
};

struct containerv_mount {
    char*                       what;
    char*                       where;
    char*                       fstype;
    enum containerv_mount_flags flags;
};

extern void containerv_options_set_mounts(struct containerv_options* options, struct containerv_mount* mounts, int count);
extern void containerv_options_set_users(struct containerv_options* options, uid_t hostUidStart, uid_t childUidStart, int count);
extern void containerv_options_set_groups(struct containerv_options* options, gid_t hostGidStart, gid_t childGidStart, int count);
#endif

/**
 * @brief Creates a new container.
 * @param rootFs The absolute path of where the chroot root is.
 * @param capabilities
 */
extern int containerv_create(
    const char*                   rootFs,
    struct containerv_options*    options,
    struct containerv_container** containerOut
);

enum container_spawn_flags {
    CV_SPAWN_WAIT = 0x1
};

struct containerv_spawn_options {
    const char*                arguments;
    const char* const*         environment;
    struct containerv_user*    as_user;
    enum container_spawn_flags flags;
};

extern int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut
);

extern int containerv_kill(struct containerv_container* container, process_handle_t pid);

extern int containerv_upload(struct containerv_container* container, const char* const* hostPaths, const char* const* containerPaths, int count);

extern int containerv_download(struct containerv_container* container, const char* const* containerPaths, const char* const* hostPaths, int count);

extern int containerv_destroy(struct containerv_container* container);

extern int containerv_join(const char* containerId);

/**
 * @brief Returns the container ID of the given container.
 * @param container The container to get the ID from.
 * @return A read-only string containing the container ID.
 */
extern const char* containerv_id(struct containerv_container* container);

#endif //!__CONTAINERV_H__
