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
union oven_backend_options;

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
    const char** process_environment;

    /**
     * @brief The working directory from which the backend is executed
     * This is not the current active directory. The current working directory
     * will be BAKE_BUILD_DIR.
     */
    const char* root_directory;

    /**
     * @brief The is the path to the project source directory, this is the path
     * where the backend is supposed to load/execute files from.
     * <root_directory>/<source_offset>
     */
    const char* project_directory;

    /**
     * @brief The path to the project build directory, this is the path where
     * the oven backend is supposed to store the generated files.
     */
    const char* build_directory;

    /**
     * @brief The path to the project output directory, this is the path where
     * the backend is supposed to store the files that should be installed.
     */
    const char* install_directory;

    /**
     * @brief The path where the fridge keeps it's ingredients. This is the prep area
     * path and not the storage path. The prep area will usually contain bin/, lib/ and
     * include/
     */
    const char* fridge_directory;

    /**
     * @brief Argument string for the current recipe step. The string is a
     * whitespace seperated string with arguments.
     */
    const char* arguments;

    /**
     * @brief list of key-value pairs for the current recipe step.
     */
    struct list* environment;
};

//****************************************************************************//
// Configure backend entries                                                  //
//****************************************************************************//
extern int configure_main(struct oven_backend_data* data, union oven_backend_options* options);
extern int cmake_main(struct oven_backend_data* data, union oven_backend_options* options);
extern int meson_main(struct oven_backend_data* data, union oven_backend_options* options);

//****************************************************************************//
// Build backend entries                                                      //
//****************************************************************************//
extern int make_main(struct oven_backend_data* data, union oven_backend_options* options);

#endif //!__LIBOVEN_BACKEND_H__
