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
#include "pubkey/pubkey.h"

static struct __chefclient_login_context {
    enum chef_login_flow_type flow;
} g_loginContext = { 0 };

int chefclient_login(struct chefclient_login_params* params)
{
    // store the flow we are using
    g_loginContext.flow = params->flow;

    if (params->flow == CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE) {
        return oauth_login(OAUTH_FLOW_DEVICECODE);
    } else if (params->flow == CHEF_LOGIN_FLOW_TYPE_PUBLIC_KEY) {
        return pubkey_login(params->public_key, params->private_key);
    }

    errno = ENOTSUP;
    return -1;
}

void chefclient_logout(void)
{
    switch (g_loginContext.flow) {
        case CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE:
            oauth_logout();
            break;
        case CHEF_LOGIN_FLOW_TYPE_PUBLIC_KEY:
            pubkey_logout();
            break;
        default:
            VLOG_WARNING("chef-client", "chefclient_logout: unsupported login flow type %d\n", g_loginContext.flow);
            break;
    }

    // reset the login context
    memset(&g_loginContext, 0, sizeof(g_loginContext));
}

void chefclient_set_authentication(void** headerlist)
{
    switch (g_loginContext.flow) {
        case CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE:
            oauth_set_authentication(headerlist);
            break;
        case CHEF_LOGIN_FLOW_TYPE_PUBLIC_KEY:
            pubkey_set_authentication(headerlist);
            break;
        default:
            VLOG_WARNING("chef-client", "chefclient_set_authentication: unsupported login flow type %d\n", g_loginContext.flow);
            break;
    }
}
