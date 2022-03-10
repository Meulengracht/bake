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

#ifndef __LIBCHEF_PACKAGE_H__
#define __LIBCHEF_PACKAGE_H__

#include <stddef.h>

struct chef_version {
    int         major;
    int         minor;
    int         revision;
    const char* tag;
};

struct chef_channel {
    const char*         name;
    struct chef_version current_version;
};

struct chef_architecture {
    const char*          name;
    struct chef_channel* channels;
    size_t               channels_count;
};

struct chef_platform {
    const char*               name;
    struct chef_architecture* architectures;
    size_t                    architectures_count;
};

struct chef_package {
    const char* publisher;
    const char* package;
    const char* description;
    const char* homepage;
    const char* license;
    const char* maintainer;
    const char* maintainer_email;

    struct chef_platform* platforms;
    size_t                platforms_count;
};

/**
 * @brief 
 * 
 * @param path 
 * @param packageOut 
 * @param versionOut 
 * @return int 
 */
extern int chef_package_load(const char* path, struct chef_package** packageOut, struct chef_version** versionOut);

/**
 * @brief Cleans up any resources allocated by the package.
 * 
 * @param[In] package A pointer to the package that will be freed. 
 */
extern void chef_package_free(struct chef_package* package);

/**
 * @brief Cleans up any resources allocated by the version structure. This is only neccessary
 * to call if the verison was allocated seperately (by chef_package_load).
 * 
 * @param[In] version A pointer to the version that will be freed. 
 */
extern void chef_version_free(struct chef_version* version);

#endif //!__LIBCHEF_PACKAGE_H__
