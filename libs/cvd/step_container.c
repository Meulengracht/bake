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

#include <chef/cvd.h>
#include <chef/list.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <vlog.h>

int bake_build_setup(struct __bake_build_context* bctx)
{
    unsigned int pid;
    char         buffer[1024];
    int          status;
    VLOG_DEBUG("bake", "bake_build_setup()\n");

    if (bctx->cvd_client == NULL) {
        errno = ENOTSUP;
        return -1;
    }

    status = bake_client_create_container(bctx);
    if (status) {
        VLOG_ERROR("bake", "bake_build_setup: failed to create build container: %u\n", status);
        return status;
    }

    snprintf(&buffer[0], sizeof(buffer),
        "%s init --recipe %s",
        bctx->bakectl_path, bctx->recipe_path
    );

    status = bake_client_spawn(
        bctx,
        &buffer[0],
        CHEF_SPAWN_OPTIONS_WAIT,
        &pid
    );
    if (status) {
        VLOG_ERROR("bake", "failed to setup project inside the container\n");
    }
    return status;
}
