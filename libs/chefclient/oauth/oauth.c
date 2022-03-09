/**
 * Copyright 2022, Philip Meulengracht
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

#include <curl/curl.h>
#include <errno.h>
#include "oauth.h"
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

extern int oauth_deviceflow_start(struct token_context* tokenContexth);
extern json_t* g_chefSettings;

static struct token_context g_tokenContext = { 0 };
static char                 g_token[4096]  = { 0 };

static int __load_oauth_settings(void)
{
    // do we have oauth settings stored?
    if (g_chefSettings != NULL) {
        json_t* oauth = json_object_get(g_chefSettings, "oauth");
        if (oauth != NULL) {
            json_t* token = json_object_get(oauth, "access-token");
            
            if (json_string_value(token) != NULL && strlen(json_string_value(token)) > 0) {
                g_tokenContext.access_token = strdup(json_string_value(token));
            }

            return 0;
        }
    }
    return -1;
}

static void __save_oauth_settings(void)
{
    if (g_chefSettings != NULL) {
        json_t* oauth;
        json_t* accessToken;

        oauth = json_object();
        if (oauth == NULL) {
            return;
        }

        accessToken = json_string(g_tokenContext.access_token);

        // build oauth object
        json_object_set_new(oauth, "access-token", accessToken);

        // store the oauth object
        json_object_set_new(g_chefSettings, "oauth", oauth);
    }
}

int oauth_login(enum oauth_flow_type flowType)
{
    int status = __load_oauth_settings();

    if (g_tokenContext.access_token == NULL && flowType == OAUTH_FLOW_DEVICECODE) {
        status = oauth_deviceflow_start(&g_tokenContext);
        if (status == 0) {
            __save_oauth_settings();
        }
    }

    if (!status) {
        // build the auth bearer token for http header
        snprintf(g_token, sizeof(g_token), "Authorization: Bearer %s", g_tokenContext.access_token);
    }

    return status;
}

void oauth_logout(void)
{
    memset(&g_tokenContext, 0, sizeof(struct token_context));
}

void oauth_set_authentication(void** headerlist)
{
    struct curl_slist* headers = curl_slist_append(*headerlist, &g_token[0]);
    *headerlist = headers;
}
