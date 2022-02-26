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

#include <errno.h>
#include "oauth.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>

extern int oauth_deviceflow_start(struct token_context* tokenContexth);

int oauth_login(enum oauth_flow_type flowType)
{
    struct token_context* tokenContext;
    int                   status = -1;

    tokenContext = calloc(1, sizeof(struct token_context));
    if (!tokenContext) {
        fprintf(stderr, "oauth_login: failed to allocate token context\n");
        return -1;
    }

    if (flowType == OAUTH_FLOW_DEVICECODE) {
        status = oauth_deviceflow_start(tokenContext);
    }
    
    free(tokenContext);
    return status;
}
