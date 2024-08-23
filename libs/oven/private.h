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

#ifndef __OVEN_PRIVATE_H__
#define __OVEN_PRIVATE_H__

#include <liboven.h>
#include "backends/include/backend.h"

struct oven_recipe_context {
    const char* name;
    const char* toolchain;
    const char* source_root;
    const char* build_root;
};

struct oven_variables {
    const char* target_platform;
    const char* target_arch;
};

struct oven_context {
    const char* const*         process_environment;
    struct oven_paths          paths;
    struct oven_variables      variables;
    struct oven_recipe_context recipe;
};

struct oven_backend {
    const char* name;
    int       (*generate)(struct oven_backend_data* data, union chef_backend_options* options);
    int       (*build)(struct oven_backend_data* data, union chef_backend_options* options);
    int       (*clean)(struct oven_backend_data* data);
};

extern struct oven_context* __oven_instance();

#endif //!__OVEN_PRIVATE_H__
