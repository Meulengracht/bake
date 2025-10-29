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
#include <chef/store.h>
#include <chef/platform.h>
#include <vlog.h>

static int __resolve_package(const char* publisher, const char* package, const char* platform, const char* arch, const char* channel, struct chef_version* version, const char* path, int* revisionDownloaded)
{
    struct chef_download_params downloadParams;
    int                         status;
    VLOG_DEBUG("served", "__resolve_package()\n");

    // initialize download params
    downloadParams.publisher = publisher;
    downloadParams.package   = package;
    downloadParams.platform  = platform;
    downloadParams.arch      = arch;
    downloadParams.channel   = channel;
    downloadParams.revision  = 0;

    VLOG_TRACE("served", "downloading package %s/%s\n", publisher, package),
    status = chefclient_pack_download(&downloadParams, path);
    if (status == 0) {
        *revisionDownloaded = downloadParams.revision;
    }
    return status;
}

int served_resolver_initialize(void)
{
    int status;
    VLOG_DEBUG("cookd", "served_resolver_initialize()\n");

    status = chefclient_initialize();
    if (status != 0) {
        VLOG_ERROR("cookd", "failed to initialize chef client\n");
        return -1;
    }

    status = store_initialize(&(struct store_parameters) {
        .platform = CHEF_PLATFORM_STR,
        .architecture = CHEF_ARCHITECTURE_STR,
        .backend = {
            .resolve_package = __resolve_package
        }
    });
    if (status) {
        VLOG_ERROR("cookd", "failed to initialize store\n");
        chefclient_cleanup();
    }
    return status;
}

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
    char* path;
    int   status;
    VLOG_DEBUG("resolver", "served_resolver_download_package(name=%s)", name);

    status = store_ensure_package(&(struct store_package) {
        .name = name,
        .platform = CHEF_PLATFORM_STR,
        .arch = CHEF_ARCHITECTURE_STR,
        .channel = options->channel,
    });
    if (status) {
        return status;
    }

    status = store_package_path(&(struct store_package) {
        .name = name,
        .platform = CHEF_PLATFORM_STR,
        .arch = CHEF_ARCHITECTURE_STR,
        .channel = options->channel,
    }, &path);

    free(path);
    return status;
}
