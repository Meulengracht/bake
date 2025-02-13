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

#include <chef/storage/bashupload.h>
#include <chef/client.h>
#include <stdio.h>
#include <stdlib.h>
#include <vlog.h>

int remote_upload(const char* path, char** downloadUrl)
{
    int status;

    status = chef_client_bu_upload(path, downloadUrl);
    if (status) {
        fprintf(stderr, "remote_upload: failed to upload %s\n", path);
        chefclient_cleanup();
        return status;
    }
    return 0;
}
