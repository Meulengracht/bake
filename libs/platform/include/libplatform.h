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

#ifndef __LIBPLATFORM_H__
#define __LIBPLATFORM_H__

#include <stddef.h>

extern char** strsplit(const char* text, char sep);
extern void   strsplit_free(char** strings);

/**
 * @brief Creates the provided directory path, if the directory already exists
 * nothing happens.
 * 
 * @param[In] path The path to create 
 * @return int 0 on success, -1 on error
 */
extern int platform_mkdir(const char* path);

extern int platform_rmdir(const char* path);

/**
 * @brief Check whether the path exists and is a directory
 * 
 * @param[In] path The path to check
 * @return int 0 if the path exists and is a directory, -1 otherwise
 */
extern int platform_isdir(const char* path);
extern int platform_getenv(const char* name, char* buffer, size_t length);
extern int platform_setenv(const char* name, const char* value);
extern int platform_unsetenv(const char* name);
extern int platform_getcwd(char* buffer, size_t length);
extern int platform_chdir(const char* path);

/**
 * @brief 
 * 
 * @param milliseconds 
 * @return int 
 */
extern int platform_sleep(unsigned int milliseconds);

/**
 * @brief Spawns a new process, and waits for the process to complete. 
 * 
 * @param[In] path      The path to the executable 
 * @param[In] arguments The arguments to pass to the executable
 * @param[In] envp      The environment variables to pass to the executable
 * @param[In] cwd       The working directory to pass to the executable
 * @return int 0 on success, -1 on error
 */
extern int platform_spawn(const char* path, const char* arguments, const char* const* envp, const char* cwd);

#endif //!__LIBPLATFORM_H__
