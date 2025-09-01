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
#include <string.h>
#include <vlog.h>

static struct __chefclient_login_context {
    enum chef_login_flow_type flow;
    char                      api_key_bearer[2048];
    int                       key_valid;
} g_loginContext = { 0 };

int chefclient_login(struct chefclient_login_params* params)
{
    // store the flow we are using
    g_loginContext.flow = params->flow;

    // if api-key was provided, use that instead
    if (params->api_key != NULL) {
        snprintf(
            &g_loginContext.api_key_bearer[0],
            sizeof(g_loginContext.api_key_bearer),
            "Authorization: Bearer %s",
            params->api_key
        );

        // don't do anything else
        g_loginContext.key_valid = 1;
        return 0;
    }

    if (params->flow == CHEF_LOGIN_FLOW_TYPE_OAUTH2_DEVICECODE) {
        return oauth_login(OAUTH_FLOW_DEVICECODE);
    } else if (params->flow == CHEF_LOGIN_FLOW_TYPE_PUBLIC_KEY) {
        return pubkey_login(params->email, params->public_key, params->private_key);
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
    if (g_loginContext.key_valid) {
        struct curl_slist* headers = *headerlist;
        headers = curl_slist_append(headers, &g_loginContext.api_key_bearer[0]);
        *headerlist = headers;
        return;
    }

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
