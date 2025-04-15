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

#include <chef/containerv.h>
#include <chef/platform.h>
#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

struct __container {
    char* id;
};

static struct {
    struct list containers;
} g_server = { 0 };

static int __resolve_rootfs(const struct chef_create_parameters* params, char** path)
{
    switch (params->type) {
        case CHEF_ROOTFS_TYPE_DEBOOTSTRAP:
        case CHEF_ROOTFS_TYPE_OSBASE:
        case CHEF_ROOTFS_TYPE_IMAGE:
    }
}

static int __resolve_mounts()
{

}

enum chef_status cvd_create(const struct chef_create_parameters* params, char* const* id)
{
    char* rootfs;
    int   status;

    // resolve the type of roots
    status = __resolve_rootfs(params, &rootfs);
    if (status) {
        return ;
    }

    // setup mounts
    status = __resolve_mounts();
    if (status) {
        return ;
    }

    // setup other config

    // create the container

    return CHEF_STATUS_SUCCESS;
}
