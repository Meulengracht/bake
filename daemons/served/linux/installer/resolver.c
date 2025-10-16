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

#include <application.h>
#include <chef/client.h>
#include <chef/api/package.h>
#include <chef/platform.h>
#include <vlog.h>

static int __parse_package_identifier(const char* id, char** publisherOut, char** nameOut)
{
    char** names;
    int    count = 0;

    // split the publisher/package
    names = strsplit(id, '/');
    if (names == NULL) {
        VLOG_ERROR("resolver", "unknown package name or path: %s\n", id);
        return -1;
    }
    
    while (names[count] != NULL) {
        count++;
    }

    if (count != 2) {
        VLOG_ERROR("resolver", "unknown package name or path: %s\n", id);
        return -1;
    }

    *publisherOut = platform_strdup(names[0]);
    *nameOut      = platform_strdup(names[1]);
    strsplit_free(names);
    return 0;
}

struct served_resolver_download_options {
    const char* channel;
    int         revision;
};

struct served_resolver_download {
    struct chef_package* package;
    char*                path;
};

int served_resolver_download_package(const char* name, struct served_resolver_download_options* options)
{
    char* publisher;
    char* package;
    char* path;
    int   status;
    VLOG_DEBUG("resolver", "served_resolver_download_package(name=%s)", name);

    status = __parse_package_identifier(name, &publisher, &package);
    if (status) {
        VLOG_ERROR("resolver", "served_resolver_download_package: failed to resolve publisher and package from name id\n");
        return status;
    }

    status = chefclient_pack_download(&(struct chef_download_params) {
        .publisher = publisher,
        .package = package,
        .platform = CHEF_PLATFORM_STR,
        .arch = CHEF_ARCHITECTURE_STR,
        .channel = options->channel,
        .revision = options->revision
    }, path);


    free(path);
    free(publisher);
    free(package);
    return status;
}
