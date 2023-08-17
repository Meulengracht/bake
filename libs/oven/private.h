/**
 * Copyright 2022, Philip Meulengracht
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

#ifndef __OVEN_PRIVATE_H__
#define __OVEN_PRIVATE_H__

struct list;

struct scratch_options {
    const char*  name;
    const char*  install_path;
    const char*  project_path;
    struct list* ingredients; // list<oven_ingredient>
    struct list* imports; // list<packaging_import>
};

struct scratch {
    char* host_build_path;
    char* host_install_path;
    char* host_checkpoint_path;

    char* project_root;
    char* build_root;
    char* install_root;
};

/**
 * @brief 
 * 
 * @param[In] commands 
 * @param[In] resolves
 * @return int 
 */
extern int oven_resolve_commands(struct list* commands, struct list* resolves);

/**
 * @brief 
 * 
 * @param resolves 
 */
extern void oven_resolve_destroy(struct list* resolves);


/**
 * @brief
 */
extern int scratch_setup(struct scratch_options* options, struct scratch* scratch);

/**
 * @brief
 */
extern int scratch_enter(struct scratch* scratch);

/**
 * @brief 
 */
extern int scratch_leave(struct scratch* scratch);

#endif //!__OVEN_PRIVATE_H__
