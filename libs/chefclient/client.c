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
#include <chef/client.h>
#include <errno.h>
#include "oauth/oauth.h"
#include "private.h"
#include <chef/platform.h>
#include <jansson.h>
#include <stdlib.h>
#include <string.h>
#include <wolfssl/ssl.h>

struct chefclient {
    char*   settings_path;
    json_t* settings;
};

static const char* g_mollenosTenantId   = "d8acf75d-9820-4522-a25b-ad672acc5fdd";
static const char* g_chefClientId       = "17985824-571b-4bdf-b291-c25b2ff14837";
static int         g_curlTrace          = 0;

struct chefclient g_chefclient = { 0 };

json_t* chefclient_settings(void)
{
    return g_chefclient.settings;
}

static int __load_settings(struct chefclient* client, const char* path)
{
    char         buff[PATH_MAX];
    json_error_t error;

    snprintf(&buff[0], sizeof(buff), "%s" CHEF_PATH_SEPARATOR_S "client.json", path);;

    client->settings_path = platform_strdup(&buff[0]);
    client->settings = json_load_file(&buff[0], 0, &error);
    if (client->settings == NULL) {
        // handle if file not found
        if (json_error_code(&error) == json_error_cannot_open_file) {
            client->settings = json_object();
        } else {
            fprintf(stderr, "__load_settings: failed to read %s: %i\n", &buff[0], json_error_code(&error));
            return -1;
        }
    }
    return 0;
}

static int __save_settings(struct chefclient* client)
{
    return json_dump_file(client->settings, client->settings_path, JSON_INDENT(2));
}

int chefclient_initialize(void)
{
    char buff[PATH_MAX];
    int  status;

    if (g_chefclient.settings != NULL) {
        return -1;
    }

    status = platform_getuserdir(&buff[0], PATH_MAX);
    if (status) {
        return status;
    }

    // required on windows
    curl_global_init(CURL_GLOBAL_ALL);

    status = __load_settings(&g_chefclient, &buff[0]);
    if (status != 0) {
        fprintf(stderr, "chefclient_initialize: failed to load settings\n");
        return -1;
    }
    return 0;
}

void chefclient_cleanup(void)
{
    // save settings
    if (__save_settings(&g_chefclient) != 0) {
        fprintf(stderr, "chefclient_cleanup: failed to save settings\n");
    }

    // required on windows
    curl_global_cleanup();
}

const char* chef_tenant_id(void)
{
    return g_mollenosTenantId;
}

const char* chef_client_id(void)
{
    return g_chefClientId;
}

int chef_trace_requests(void)
{
    return g_curlTrace;
}

void chef_set_curl_common_headers(void** headerlist, int authorization)
{
    if (authorization) {
        if (headerlist == NULL) {
            fprintf(stderr, "chef_set_curl_common: auth requested but headerlist is NULL\n");
            return;
        }
        oauth_set_authentication(headerlist);
    }
}
