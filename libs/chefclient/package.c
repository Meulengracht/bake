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

#include <chef/package.h>
#include <chef/utils_vafs.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static struct VaFsGuid g_headerGuid  = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_versionGuid = CHEF_PACKAGE_VERSION_GUID;


static int __load_package_header(struct chef_vafs_feature_package_header* header, struct chef_package* package)
{
    char* data = (char*)header + sizeof(struct chef_vafs_feature_package_header);

    package->type = header->type;

    if (header->package_length) {
        package->package = strndup(data, header->package_length);
        data += header->package_length;
    }

    if (header->description_length) {
        package->description = strndup(data, header->description_length);
        data += header->description_length;
    }

    if (header->homepage_length) {
        package->homepage = strndup(data, header->homepage_length);
        data += header->homepage_length;
    }

    if (header->license_length) {
        package->license = strndup(data, header->license_length);
        data += header->license_length;
    }

    if (header->maintainer_length) {
        package->maintainer = strndup(data, header->maintainer_length);
        data += header->maintainer_length;
    }

    if (header->maintainer_email_length) {
        package->maintainer_email = strndup(data, header->maintainer_email_length);
        data += header->maintainer_email_length;
    }
    return 0;
}

static int __load_package_version(struct chef_vafs_feature_package_version* header, struct chef_version* version)
{
    char* data = (char*)header + sizeof(struct chef_vafs_feature_package_version);

    version->major = header->major;
    version->minor = header->minor;
    version->revision = header->revision;

    if (header->tag_length) {
        version->tag = strndup(data, header->tag_length);
        data += header->tag_length;
    }
    return 0;
}

int chef_package_load(const char* path, struct chef_package** packageOut, struct chef_version** versionOut)
{
    struct VaFsFeatureHeader* chefHeader;
    struct VaFs*              vafs;
    int                       status;

    if (path == NULL || packageOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_open_file(path, &vafs);
    if (status != 0) {
        return status;
    }

    // locate the chef package header
    status = vafs_feature_query(vafs, &g_headerGuid, &chefHeader);
    if (status != 0) {
        vafs_close(vafs);
        return status;
    }

    // allocate the package structure
    *packageOut = (struct chef_package*)malloc(sizeof(struct chef_package));
    if (*packageOut == NULL) {
        vafs_close(vafs);
        return -1;
    }
    memset(*packageOut, 0, sizeof(struct chef_package));

    // load the package header
    status = __load_package_header((struct chef_vafs_feature_package_header*)chefHeader, *packageOut);
    if (status != 0) {
        free(*packageOut);
        vafs_close(vafs);
        return status;
    }

    // locate the package version header
    status = vafs_feature_query(vafs, &g_versionGuid, &chefHeader);
    if (status != 0) {
        free(*packageOut);
        vafs_close(vafs);
        return status;
    }

    // allocate the version structure
    *versionOut = (struct chef_version*)malloc(sizeof(struct chef_version));
    if (*versionOut == NULL) {
        free(*packageOut);
        vafs_close(vafs);
        return -1;
    }
    memset(*versionOut, 0, sizeof(struct chef_version));

    // load the package version header
    status = __load_package_version((struct chef_vafs_feature_package_version*)chefHeader, *versionOut);
    if (status != 0) {
        free(*packageOut);
        free(*versionOut);
        vafs_close(vafs);
        return status;
    }

    // close the vafs handle
    vafs_close(vafs);
    return 0;
}

static void __free_version(struct chef_version* version)
{
    free((void*)version->tag);
}

static void __free_channel(struct chef_channel* channel)
{
    free((void*)channel->name);
    __free_version(&channel->current_version);
}

static void __free_architecture(struct chef_architecture* architecture)
{
    free((void*)architecture->name);
    if (architecture->channels != NULL) {
        for (size_t i = 0; i < architecture->channels_count; i++) {
            __free_channel(&architecture->channels[i]);
        }
        free(architecture->channels);
    }
}

static void __free_platform(struct chef_platform* platform)
{
    free((void*)platform->name);
    if (platform->architectures != NULL) {
        for (size_t i = 0; i < platform->architectures_count; i++) {
            __free_architecture(&platform->architectures[i]);
        }
        free(platform->architectures);
    }
}

void chef_package_free(struct chef_package* package)
{
    if (package == NULL) {
        return;
    }

    free((void*)package->publisher);
    free((void*)package->package);
    free((void*)package->description);
    free((void*)package->homepage);
    free((void*)package->license);
    free((void*)package->maintainer);
    free((void*)package->maintainer_email);

    if (package->platforms != NULL) {
        for (size_t i = 0; i < package->platforms_count; i++) {
            __free_platform(&package->platforms[i]);
        }
        free(package->platforms);
    }
    free(package);
}

void chef_version_free(struct chef_version* version)
{
    if (version == NULL) {
        return;
    }

    __free_version(version);
    free(version);
}
