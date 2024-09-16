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

#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static char g_templateGuid[] = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char* g_hexValues = "0123456789ABCDEF-";

static struct waiterd_server g_server = { 0 };

static struct waiterd_cook* __waiterd_cook_new(gracht_conn_t client)
{
    struct waiterd_cook* cook;

    cook = calloc(1, sizeof(struct waiterd_cook));
    if (cook == NULL) {
        return NULL;
    }

    cook->client = client;
    return cook;
}

static void __waiterd_cook_delete(struct waiterd_cook* cook) 
{
    // nothing to do here for now
    free(cook);
}

struct waiterd_cook* __find_cook_by_client(gracht_conn_t client)
{
    struct list_item* i;

    list_foreach(&g_server.cooks, i) {
        struct waiterd_cook* cook = (struct waiterd_cook*)i;
        if (cook->client == client) {
            return cook;
        }
    }

    return NULL;
}


void waiterd_server_cook_connect(gracht_conn_t client)
{
    struct waiterd_cook* cook = __waiterd_cook_new(client);
    if (cook == NULL) {
        // needs to be logged fatally
        return;
    }

    list_add(&g_server.cooks, &cook->list_header);
}

void waiterd_server_cook_disconnect(gracht_conn_t client)
{
    struct waiterd_cook* cook = __find_cook_by_client(client);
    struct list_item*    i;

    // invalid cook?
    if (cook == NULL) {
        // log this
        return;
    }
    
    // remove cook immediately
    list_remove(&g_server.cooks, &cook->list_header);

    // abort any request in flight for waiters
    list_foreach(&g_server.requests, i) {
        struct waiterd_request* request = (struct waiterd_request*)i;
        if (request->cook == client) {
            // abort
        }
    }

    // cleanup cook
    __waiterd_cook_delete(cook);
}

void waiterd_server_cook_ready(gracht_conn_t client, enum waiterd_architecture arch)
{
    struct waiterd_cook* cook = __find_cook_by_client(client);

    if (cook == NULL) {
        // invalid cook, log this 
        return;
    }

    cook->architecture = arch;
    cook->ready = 1;
}

struct waiterd_cook* waiterd_server_cook_find(enum waiterd_architecture arch)
{
    struct list_item* i;

    list_foreach(&g_server.cooks, i) {
        struct waiterd_cook* cook = (struct waiterd_cook*)i;
        if (cook->architecture == arch) {
            return cook;
        }
    }

    return NULL;
}

static void __guid_new(char guidBuffer[40])
{
    // yes we are well aware that this provides _very_ poor randomness
    // but we do not require a cryptographically secure guid for this purpose
    srand(clock());
    for (int t = 0; t < (sizeof(g_templateGuid) - 1); t++) {
        int  r = rand() % 16;
        char c = ' ';

        switch (g_templateGuid[t]) {
            case 'x' : { c = g_hexValues[r]; } break;
            case 'y' : { c = g_hexValues[(r & 0x03) | 0x08]; } break;
            case '-' : { c = '-'; } break;
            case '4' : { c = '4'; } break;
        }
        guidBuffer[t] = c;
    }
    guidBuffer[sizeof(g_templateGuid) - 1] = 0;
}

struct waiterd_request* waiterd_server_request_new(
    struct waiterd_cook*   cook,
    struct gracht_message* message)
{
    struct waiterd_request* request;

    request = calloc(1, sizeof(struct waiterd_request));
    if (request == NULL) {
        return NULL;
    }
    
    request->cook = cook->client;
    request->source = malloc(GRACHT_MESSAGE_DEFERRABLE_SIZE(message));
    if (request->source == NULL) {
        free(request);
        return NULL;
    }
    gracht_server_defer_message(message, request->source);
    __guid_new(request->guid);
    return request;
}

struct waiterd_request* waiterd_server_request_find(const char* id)
{
    struct list_item* i;
    
    list_foreach(&g_server.requests, i) {
        struct waiterd_request* request = (struct waiterd_request*)i;
        if (!strcmp(request->guid, id)) {
            return request;
        }
    }
    
    return NULL;
}
