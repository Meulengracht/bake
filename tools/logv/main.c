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
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <vlog.h>

int main(int argc, char** argv, char** envp)
{
    // initialize the logging system
    vlog_initialize(VLOG_LEVEL_DEBUG);

    vlog_start(stdout, "initializing", "footer", 4);

    vlog_content_set_index(0);
    vlog_content_set_prefix("prepare");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WAITING);

    vlog_content_set_index(1);
    vlog_content_set_prefix("source");
    vlog_content_set_status(VLOG_CONTENT_STATUS_WORKING);

    vlog_content_set_index(2);
    vlog_content_set_prefix("build");
    vlog_content_set_status(VLOG_CONTENT_STATUS_DONE);

    vlog_content_set_index(3);
    vlog_content_set_prefix("pack");
    vlog_content_set_status(VLOG_CONTENT_STATUS_FAILED);

    vlog_refresh(stdout);

    vlog_content_set_index(1);
    VLOG_TRACE("test", "testing output by writing this string");

    for (;;) {
        platform_sleep(1000);
    }

    vlog_cleanup();
}
