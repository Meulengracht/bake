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

#ifndef __CHEF_DIRS_H__
#define __CHEF_DIRS_H__

#include <stdio.h>

/**
 * @file dirs.h
 * @brief Resolves and creates Chef runtime directories.
 *
 * This module provides a small set of functions for locating Chef directories
 * (config, store, cache, kitchens/rootfs workspaces) depending on where Chef is
 * running.
 *
 * Call chef_dirs_initialize() once early in startup before using any of the
 * getter functions.
 *
 * Thread-safety: this module uses global state and is not currently designed
 * for concurrent initialization.
 */

/**
 * @brief Directory layout scope.
 *
 * Scopes typically correspond to:
 * - CLI tools running as a user (BAKE)
 * - CLI tools running in a container environment (BAKECTL)
 * - Daemons/services (DAEMON)
 */
enum chef_dir_scope {
    /**
     * @brief User-facing CLI scope.
     *
     * Typical conventions:
     * - Linux: under the user's home directory.
     * - Windows: data under LocalAppData and config under RoamingAppData.
     */
    CHEF_DIR_SCOPE_BAKE,

    /**
     * @brief Controller running inside the build/container environment.
     *
     * Paths are chosen to be container-friendly and self-contained.
     */
    CHEF_DIR_SCOPE_BAKECTL,

    /**
     * @brief Daemon/service scope.
     *
     * Uses machine-global locations suitable for long-running services.
     */
    CHEF_DIR_SCOPE_DAEMON,
};

/**
 * @brief Initialize directory paths for the given scope.
 *
 * Implementations typically create the relevant directories as part of
 * initialization.
 *
 * @param scope The desired directory scope.
 * @return 0 on success, -1 on error.
 */
extern int chef_dirs_initialize(enum chef_dir_scope scope);

/**
 * @brief Returns the root directory for Chef data in the active scope.
 *
 * @return Pointer owned by this module, or NULL if not initialized.
 */
extern const char* chef_dirs_root(void);

/**
 * @brief Returns the store directory.
 *
 * The store is used for persisted artifacts and assets.
 *
 * @return Pointer owned by this module, or NULL if unavailable in the active scope.
 */
extern const char* chef_dirs_store(void);

/**
 * @brief Returns the cache directory.
 *
 * The cache is used for disposable, reproducible, or derived data.
 *
 * @return Pointer owned by this module, or NULL if unavailable in the active scope.
 */
extern const char* chef_dirs_cache(void);

/**
 * @brief Creates (if needed) and returns the full path to a new rootfs/kitchen workspace.
 *
 * This is typically used when you need a directory to exist for a given UUID.
 *
 * @param uuid A unique identifier for the workspace.
 * @return Newly allocated path string on success (caller must free), NULL on error.
 */
extern char* chef_dirs_rootfs_new(const char* uuid);

/**
 * @brief Returns the kitchen root path or the rootfs path for a UUID.
 *
 * This function never returns a caller-owned allocation.
 *
 * - If uuid is NULL (or empty), returns the kitchen root directory.
 * - If uuid is non-NULL, returns a module-owned path for that UUID.
 *
 * The returned pointer is valid until the next call to chef_dirs_rootfs() (with a
 * non-NULL uuid) and until re-initialization.
 *
 * Prefer chef_dirs_rootfs_alloc() if you need a stable, caller-owned string.
 * Prefer chef_dirs_rootfs_new() if you need the directory to exist.
 *
 * @param uuid Optional UUID for a specific workspace.
 * @return Path string or NULL on error.
 */
extern const char* chef_dirs_rootfs(const char* uuid);

/**
 * @brief Returns a newly allocated kitchen/rootfs path.
 *
 * Unlike chef_dirs_rootfs(), the returned string is always caller-owned and must
 * be freed.
 *
 * @param uuid Optional UUID for a specific workspace.
 * @return Newly allocated path string on success (caller must free), NULL on error.
 */
extern char* chef_dirs_rootfs_alloc(const char* uuid);

/**
 * @brief Returns the configuration directory.
 *
 * This directory contains configuration files such as bake.json.
 *
 * @return Pointer owned by this module, or NULL if unavailable in the active scope.
 */
extern const char* chef_dirs_config(void);

/**
 * @brief Opens a new temporary file and returns a writable FILE*.
 *
 * The file name is derived from the provided base name and extension and is
 * made unique by the implementation.
 *
 * @param name  Base name (e.g. "cookd" or "bake-build").
 * @param ext   File extension (with or without leading dot).
 * @param rpath If non-NULL, receives a newly allocated full path to the temp file.
 *              The caller must free it.
 * @return An opened FILE* on success, NULL on error.
 */
extern FILE* chef_dirs_open_temp_file(const char* name, const char* ext, char** rpath);

#endif //!__CHEF_DIRS_H__
