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

#include <chef/client.h>
#include <errno.h>
#include "private.h"
#include "oauth/oauth.h"

int chefclient_login(enum chef_login_flow_type flowType)
{
    if (flowType == CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE) {
        return oauth_login(OAUTH_FLOW_DEVICECODE);
    }
    
    errno = ENOTSUP;
    return -1;
}

void chefclient_logout(void)
{
    oauth_logout();
}
