/**
 * Copyright 2023, Philip Meulengracht
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

#ifndef __LIBINGREDIENT_H__
#define __LIBINGREDIENT_H__

#include <chef/package.h>

// prototypes imported from vafs;
struct VaFs;
struct VaFsDirectoryHandle;

struct ingredient_options {
    char** bin_dirs;
    char** inc_dirs;
    char** lib_dirs;
    char** compiler_flags;
    char** linker_flags;
};

struct ingredient {
    struct VaFs*                vafs;
    struct VaFsDirectoryHandle* root_handle;
    struct chef_package*        package;
    struct chef_version*        version;
    struct ingredient_options*  options;
    int                         file_count;
    int                         directory_count;
    int                         symlink_count;
};

#define INGREDIENT_PROGRESS_START     0
#define INGREDIENT_PROGRESS_FILE      1
#define INGREDIENT_PROGRESS_DIRECTORY 2
#define INGREDIENT_PROGRESS_SYMLINK   3
typedef void(*ingredient_progress_cb)(const char* name, int type, void* context);

/**
 * @brief Opens up an ingredient for reading. Returns a handle that can be used for
 * manually doing operations or be used for further operations.
 */
extern int ingredient_open(const char* path, struct ingredient** ingredientOut);

/**
 * @brief Closes a previously opened ingredient handle
 */
extern void ingredient_close(struct ingredient* ingredient);

/**
 * @brief Helper to extract an ingredient to a specific path. Optionally a progress callback
 * can be provided.
 */
extern int ingredient_unpack(struct ingredient* ingredient, const char* path, ingredient_progress_cb progressCB, void* context);

#endif //!__LIBINGREDIENT_H__
