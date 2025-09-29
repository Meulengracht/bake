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

#ifndef __LIBCHEF_API_PACKAGE_H__
#define __LIBCHEF_API_PACKAGE_H__

#include <stddef.h>
#include <chef/package.h>

struct chef_info_params {
    const char* publisher;
    const char* package;
};

struct chef_find_params {
    const char* query;
    int         privileged;
};

struct chef_publish_params {
    const char*          publisher;
    const char*          package;
    const char*          platform;
    const char*          architecture;
    const char*          channel;
    struct chef_version* version;
};

struct chef_download_params {
    const char*          publisher;
    const char*          package;
    const char*          platform;
    const char*          arch;
    const char*          channel;
    struct chef_version* version;

    // this will be updated to the revision downloaded,
    // which means from a callers perspective this is read-only
    int                  revision;
};

struct chef_find_result {
    const char*            publisher;
    const char*            package;
    const char*            summary;
    enum chef_package_type type;
    const char*            maintainer;
    const char*            maintainer_email;
};

/**
 * @brief 
 * 
 * @param[In]  params 
 * @param[In]  path 
 * @return int 
 */
extern int chefclient_pack_download(struct chef_download_params* params, const char* path);

/**
 * @brief 
 * 
 * @param params 
 * @return int 
 */
extern int chefclient_pack_info(struct chef_info_params* params, struct chef_package** packageOut);

/**
 * @brief 
 * 
 * @param params 
 * @return int 
 */
extern int  chefclient_pack_find(struct chef_find_params* params, struct chef_find_result*** results, int* count);
extern void chefclient_pack_find_free(struct chef_find_result** results, int count);

/**
 * @brief 
 * 
 * @param params 
 * @param path 
 * @return int 
 */
extern int chefclient_pack_publish(struct chef_publish_params* params, const char* path);

#endif //!__LIBCHEF_API_PACKAGE_H__
