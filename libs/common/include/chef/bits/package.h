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

#ifndef __CHEF_PACKAGE_BITS_H__
#define __CHEF_PACKAGE_BITS_H__

#include <stddef.h>

#define CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX 64
#define CHEF_PACKAGE_NAME_LENGTH_MAX           128
#define CHEF_PACKAGE_ID_LENGTH_MAX             CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX + CHEF_PACKAGE_NAME_LENGTH_MAX

// prototypes imported from vafs;
struct VaFs;

enum chef_package_type {
    CHEF_PACKAGE_TYPE_UNKNOWN,
    CHEF_PACKAGE_TYPE_BOOTLOADER,
    CHEF_PACKAGE_TYPE_TOOLCHAIN,
    CHEF_PACKAGE_TYPE_OSBASE,
    CHEF_PACKAGE_TYPE_CONTENT,
    CHEF_PACKAGE_TYPE_INGREDIENT,
    CHEF_PACKAGE_TYPE_APPLICATION
};

struct chef_version {
    int         major;
    int         minor;
    int         patch;
    int         revision;
    const char* tag;
    long long   size;
    const char* created;
};

struct chef_revision {
    const char*         channel;
    const char*         platform;
    const char*         architecture;
    struct chef_version current_version;
};

enum chef_command_type {
    CHEF_COMMAND_TYPE_UNKNOWN,
    CHEF_COMMAND_TYPE_EXECUTABLE,
    CHEF_COMMAND_TYPE_DAEMON
};

struct chef_command {
    enum chef_command_type type;
    const char*            name;
    const char*            description;
    const char*            arguments;
    const char*            path;
    const void*            icon_buffer;
};

struct chef_package_application_config {
    // Optional package-provided runtime network defaults.
    // These are advisory and may be overridden by runtime policy.
    const char* network_gateway;
    const char* network_dns;
};

struct chef_package {
    const char* platform;
    const char* arch;
    const char* publisher;
    const char* package;
    const char* base;
    const char* summary;
    const char* description;
    const char* homepage;
    const char* license;
    const char* eula;
    const char* maintainer;
    const char* maintainer_email;

    enum chef_package_type type;

    struct chef_revision*  revisions;
    size_t                 revisions_count;
};

#endif //!__CHEF_PACKAGE_BITS_H__
