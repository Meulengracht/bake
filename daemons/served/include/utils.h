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

#ifndef __SERVED_UTILS_H__
#define __SERVED_UTILS_H__

typedef struct gracht_server gracht_server_t;
struct chef_config_address;

/**
 * 
 * @return
 */
extern gracht_server_t* served_gracht_server(void);

/**
 * @brief Verifies a publisher against the database of proofs. 
 */
extern int utils_verify_publisher(const char* publisher);

/**
 * @brief Verifies the package and it's publisher against the database of proofs.
 */
extern int utils_verify_package(const char* publisher, const char* package, int revision);

/**
 * @brief Splits a package name in the format of <publisher>/<package> into their subparts
 * The array must be freed with strsplit_free from chef/platform.h
 */
extern char** utils_split_package_name(const char* name);

/**
 * @brief Formats the given system path according to the base-directory set for the current
 * served instance. Sometimes it's useful to override, for instance for testing or when running
 * as a snap service.
 */
extern char* served_paths_path(const char* path);

// The following functions return paths already adjusted by served_paths_path
extern void  utils_path_set_root(const char* root);
extern char* utils_path_pack(const char* publisher, const char* package);
extern char* utils_path_mount(const char* publisher, const char* package);
extern char* utils_path_data(const char* publisher, const char* package, int revision);
extern char* utils_path_command_wrapper(const char* name);


/**
 * CVD Client utility functions
 */

struct container_options {
    const char* id;
    const char* rootfs;
    const char* package;
};

extern int  container_client_initialize(struct chef_config_address* config);
extern void container_client_shutdown(void);

extern int container_client_create_container(struct container_options* options);
extern int container_client_spawn(
    const char*        id,
    const char* const* environment,
    const char*        command,
    unsigned int*      pidOut);
extern int container_client_kill(const char*  id, unsigned int pid);
extern int container_client_destroy_container(const char* id);

#endif //!__SERVED_UTILS_H__
