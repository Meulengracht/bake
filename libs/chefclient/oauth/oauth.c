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
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

extern int oauth_deviceflow_start(struct token_context* tokenContexth);

static struct token_context g_tokenContext = { 0 };
static char                 g_token[1024]  = { 0 };

int oauth_login(enum oauth_flow_type flowType)
{
    int status = -1;

    if (flowType == OAUTH_FLOW_DEVICECODE) {
        status = oauth_deviceflow_start(&g_tokenContext);
    }

    if (!status) {
        sprintf(&g_token[0], "access_token: %s", g_tokenContext.access_token);
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
