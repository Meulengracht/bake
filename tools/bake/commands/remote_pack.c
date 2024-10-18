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

#include <chef/platform.h>
#include <stdio.h>
#include <vlog.h>

static void __output_handler(const char* line, enum platform_spawn_output_type type) 
{
    if (type == PLATFORM_SPAWN_OUTPUT_TYPE_STDOUT) {
        VLOG_DEBUG("pack", line);
    } else {
        VLOG_ERROR("pack", line);
    }
}

int remote_pack(const char* path, const char* const* envp)
{
    int  status;
    char args[PATH_MAX];

    // prepare arguments
    snprintf(&args[0], sizeof(args), "--git-ignore %s", path);

    // create a .vafs archive over the path
    status = platform_spawn(
        "mkvafs",
        &args[0],
        envp,
        &(struct platform_spawn_options) {
            .output_handler = __output_handler
        }
    );
}
