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

#include <ctype.h>
#include <chef/platform.h>
#include <errno.h>
#include <jansson.h>
#include <server.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>

struct config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

static int __parse_config_address(struct config_address* address, json_t* root)
{
    json_t* member;
    VLOG_DEBUG("config", "__parse_config_address()\n");

    member = json_object_get(root, "type");
    if (member == NULL) {
        return -1;
    }
    address->type = platform_strdup(json_string_value(member));

    member = json_object_get(root, "address");
    if (member == NULL) {
        return -1;
    }
    address->address = platform_strdup(json_string_value(member));

    member = json_object_get(root, "port");
    if (member != NULL) {
        address->port = (unsigned short)(json_integer_value(member) & 0xFFFF);
    }
    return 0;
}

static json_t* __serialize_config_address(struct config_address* address)
{
    json_t* root;
    VLOG_DEBUG("config", "__serialize_config_address(type=%s)\n", address->type);
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    json_object_set_new(root, "type", json_string(address->type));
    json_object_set_new(root, "address", json_string(address->address));
    json_object_set_new(root, "port", json_integer((long long)address->port));
    return root;
}

struct config {
    struct config_address api_address;
    struct config_address cook_address;
};

static struct config g_config = { 0 };


static json_t* __serialize_config(struct config* config)
{
    json_t* root;
    json_t* api_address;
    json_t* cook_address;
    VLOG_DEBUG("config", "__serialize_config()\n");
    
    root = json_object();
    if (!root) {
        VLOG_ERROR("config", "__serialize_config: failed to allocate memory for root object\n");
        return NULL;
    }
    
    api_address = __serialize_config_address(&config->api_address);
    if (api_address == NULL) {
        json_decref(root);
        return NULL;
    }

    cook_address = __serialize_config_address(&config->cook_address);
    if (cook_address == NULL) {
        json_decref(api_address);
        json_decref(root);
        return NULL;
    }
    
    json_object_set_new(root, "api-address", api_address);
    json_object_set_new(root, "cook-address", cook_address);
    return root;
}

static int __save_config(struct config* config, const char* path)
{
    json_t* root;
    VLOG_DEBUG("config", "__save_config(path=%s)\n", path);

    root = __serialize_config(config);
    if (root == NULL) {
        VLOG_ERROR("config", "__save_config: failed to serialize configuration\n");
        return -1;
    }
    
    if (json_dump_file(root, path, JSON_INDENT(2)) < 0) {
        VLOG_ERROR("config", "__save_config: failed to write configuration to file\n");
        return -1;
    }
    return 0;
}

static int __parse_config(struct config* config, json_t* root)
{
    json_t* member;
    int     status;

    member = json_object_get(root, "api-address");
    if (member == NULL) {
        return 0;
    }

    status = __parse_config_address(&config->api_address, member);
    if (status) {
        return status;
    }

    member = json_object_get(root, "cook-address");
    if (member == NULL) {
        return 0;
    }

    status = __parse_config_address(&config->cook_address, member);
    if (status) {
        return status;
    }
    return 0;
}

static int __initialize_config(struct config* config)
{
#ifdef CHEF_ON_LINUX
    config->api_address.type = platform_strdup("local");
    config->api_address.address = platform_strdup("/run/chef/waiterd/api");
    
    config->cook_address.type = platform_strdup("local");
    config->cook_address.address = platform_strdup("/run/chef/waiterd/cook");
#elif CHEF_ON_WINDOWS
    config->api_address.type = platform_strdup("inet4");
    config->api_address.address = platform_strdup("127.0.0.1");
    config->api_address.port = 51001;
    
    config->cook_address.type = platform_strdup("inet4");
    config->cook_address.address = platform_strdup("127.0.0.1");
    config->cook_address.port = 51002;
#endif
    return 0;
}

static int __load_config(struct config* config, const char* path)
{
    json_error_t error;
    json_t*      root;
    VLOG_DEBUG("config", "__load_config(path=%s)\n", path);

    root = json_load_file(path, 0, &error);
    if (root == NULL) {
        if (json_error_code(&error) == json_error_cannot_open_file) {
            // assume no config, write the default one
            if (__initialize_config(config)) {
                return -1;
            }
            return __save_config(config, path);
        }
        return -1;
    }
    return __parse_config(config, root);
}

// API
int waiterd_config_load(const char* confdir)
{
    char buff[PATH_MAX] = { 0 };
    int  status;
    VLOG_DEBUG("config", "waiterd_config_load(confdir=%s)\n", confdir);

    snprintf(&buff[0], sizeof(buff), "%s" CHEF_PATH_SEPARATOR_S "waiterd.json", confdir);
    status = __load_config(&g_config, &buff[0]);
    if (status) {
        VLOG_ERROR("config", "failed to load or initialize configuration\n");
        return status;
    }
    return 0;
}

extern void waiterd_config_api_address(struct waiterd_config_address* address)
{
    address->type = g_config.api_address.type;
    address->address = g_config.api_address.address;
    address->port = g_config.api_address.port;
}

extern void waiterd_config_cook_address(struct waiterd_config_address* address)
{
    address->type = g_config.cook_address.type;
    address->address = g_config.cook_address.address;
    address->port = g_config.cook_address.port;
}
