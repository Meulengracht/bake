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

#include <chef/storage/download.h>
#include <chef/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

int remote_download(const char* url, const char* path)
{
    int status;

    status = chefclient_initialize();
    if (status != 0) {
        fprintf(stderr, "remote_download: failed to initialize chef client\n");
        return -1;
    }

    status = chef_client_gen_download(url, path);
    if (status) {
        fprintf(stderr, "remote_download: failed to download %s\n", url);
        chefclient_cleanup();
        return status;
    }

    chefclient_cleanup();
    return 0;
}
