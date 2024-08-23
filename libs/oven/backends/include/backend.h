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

#ifndef __LIBOVEN_BACKEND_H__
#define __LIBOVEN_BACKEND_H__

// prototype some types from liboven.h
struct oven_generate_options;
struct oven_build_options;
union chef_backend_options;

struct oven_backend_data_paths {
    /**
     * @brief The working directory from which the backend is executed
     * This is not the current active directory. The current working directory
     * will be BAKE_BUILD_DIR.
     */
    const char* root;

    /**
     * @brief The is the path to the project source directory, this is the path
     * where the backend is supposed to load/execute files from.
     * <root_directory>/<source_offset>
     */
    const char* project;

    /**
     * @brief The path to the project build directory, this is the path where
     * the oven backend is supposed to store the generated files.
     */
    const char* build;

    /**
     * @brief The path to the project output directory, this is the path where
     * the backend is supposed to store the files that should be installed.
     */
    const char* install;

    /**
     * @brief The path to the build ingredients root directory. This is where
     * ingredients used specifically for building are stored. This is usually
     * only relevant for cross-compilation.
     */
    const char* build_ingredients;
};

struct oven_backend_data_platform {
    const char* host_platform;
    const char* host_architecture;
    const char* target_platform;
    const char* target_architecture;
};

struct oven_backend_data {
    /**
     * @brief The name of the current project. Will usually be the file-name without .yaml
     */
    const char* project_name;

    /**
     * @brief The current compilation/build profile. Usually 'Release'
     */
    const char* profile_name;

    /**
     * @brief The environmental values that the current process has
     */
    const char* const* process_environment;

    /**
     * @brief Argument string for the current recipe step. The string is a
     * whitespace seperated string with arguments.
     */
    const char* arguments;

    /**
     * @brief list of key-value pairs for the current recipe step.
     */
    struct list* environment;

    /**
     * @brief The platform information.
     */
    struct oven_backend_data_platform platform;

    /**
     * @brief The paths relevant to the project.
     */
    struct oven_backend_data_paths paths;
};

//****************************************************************************//
// Configure backend entries                                                  //
//****************************************************************************//
extern int configure_main(struct oven_backend_data* data, union chef_backend_options* options);
extern int cmake_main(struct oven_backend_data* data, union chef_backend_options* options);
extern int meson_config_main(struct oven_backend_data* data, union chef_backend_options* options);

//****************************************************************************//
// Build backend entries                                                      //
//****************************************************************************//
extern int make_main(struct oven_backend_data* data, union chef_backend_options* options);
extern int meson_build_main(struct oven_backend_data* data, union chef_backend_options* options);
extern int ninja_main(struct oven_backend_data* data, union chef_backend_options* options);

#endif //!__LIBOVEN_BACKEND_H__
