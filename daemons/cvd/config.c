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

#include "private.h"

struct config_address {
    const char*    type;
    const char*    address;
    unsigned short port;
};

struct config_security {
    const char*               default_policy;  // "minimal", "build", "network", or NULL for default
    struct config_custom_path* custom_paths;
    size_t                    custom_paths_count;
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

static int __parse_config_security(struct config_security* security, json_t* root)
{
    json_t* member;
    json_t* custom_paths_array;
    VLOG_DEBUG("config", "__parse_config_security()\n");

    // Parse default_policy
    member = json_object_get(root, "default_policy");
    if (member != NULL && json_is_string(member)) {
        security->default_policy = platform_strdup(json_string_value(member));
    }

    // Parse custom_paths array
    custom_paths_array = json_object_get(root, "custom_paths");
    if (custom_paths_array != NULL && json_is_array(custom_paths_array)) {
        size_t count = json_array_size(custom_paths_array);
        if (count > 0) {
            security->custom_paths = calloc(count, sizeof(struct config_custom_path));
            if (security->custom_paths == NULL) {
                return -1;
            }
            security->custom_paths_count = count;

            for (size_t i = 0; i < count; i++) {
                json_t* path_obj = json_array_get(custom_paths_array, i);
                if (!json_is_object(path_obj)) {
                    continue;
                }

                json_t* path_member = json_object_get(path_obj, "path");
                json_t* access_member = json_object_get(path_obj, "access");
                
                if (path_member != NULL && json_is_string(path_member)) {
                    security->custom_paths[i].path = platform_strdup(json_string_value(path_member));
                }
                
                if (access_member != NULL && json_is_string(access_member)) {
                    const char* access_str = json_string_value(access_member);
                    int access = 0;
                    if (strstr(access_str, "read")) {
                        access |= 0x1;  // CV_FS_READ
                    }
                    if (strstr(access_str, "write")) {
                        access |= 0x2;  // CV_FS_WRITE
                    }
                    if (strstr(access_str, "execute")) {
                        access |= 0x4;  // CV_FS_EXEC
                    }
                    security->custom_paths[i].access = access;
                }
            }
        }
    }
    return 0;
}

static json_t* __serialize_config_security(struct config_security* security)
{
    json_t* root;
    VLOG_DEBUG("config", "__serialize_config_security()\n");
    
    root = json_object();
    if (!root) {
        return NULL;
    }

    if (security->default_policy != NULL) {
        json_object_set_new(root, "default_policy", json_string(security->default_policy));
    } else {
        json_object_set_new(root, "default_policy", json_string("minimal"));
    }

    if (security->custom_paths_count > 0 && security->custom_paths != NULL) {
        json_t* paths_array = json_array();
        if (!paths_array) {
            json_decref(root);
            return NULL;
        }

        for (size_t i = 0; i < security->custom_paths_count; i++) {
            json_t* path_obj = json_object();
            if (!path_obj) {
                continue;
            }

            if (security->custom_paths[i].path != NULL) {
                json_object_set_new(path_obj, "path", json_string(security->custom_paths[i].path));
            }

            // Build access string
            char access_str[64] = {0};
            int access = security->custom_paths[i].access;
            int first = 1;
            if (access & 0x1) {
                strcat(access_str, "read");
                first = 0;
            }
            if (access & 0x2) {
                if (!first) strcat(access_str, ",");
                strcat(access_str, "write");
                first = 0;
            }
            if (access & 0x4) {
                if (!first) strcat(access_str, ",");
                strcat(access_str, "execute");
            }
            json_object_set_new(path_obj, "access", json_string(access_str));

            json_array_append_new(paths_array, path_obj);
        }

        json_object_set_new(root, "custom_paths", paths_array);
    } else {
        json_object_set_new(root, "custom_paths", json_array());
    }

    return root;
}

struct config {
    struct config_address  api_address;
    struct config_security security;
};

static struct config g_config = { 0 };


static json_t* __serialize_config(struct config* config)
{
    json_t* root;
    json_t* api_address;
    json_t* security;
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

    json_object_set_new(root, "api-address", api_address);

    security = __serialize_config_security(&config->security);
    if (security != NULL) {
        json_object_set_new(root, "security", security);
    }

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

    // Parse security section (optional)
    member = json_object_get(root, "security");
    if (member != NULL) {
        status = __parse_config_security(&config->security, member);
        if (status) {
            return status;
        }
    }

    return 0;
}

static int __initialize_config(struct config* config)
{
#ifdef CHEF_ON_LINUX
    config->api_address.type = platform_strdup("local");
    config->api_address.address = platform_strdup("@/chef/cvd/api");
#elif CHEF_ON_WINDOWS
    config->api_address.type = platform_strdup("inet4");
    config->api_address.address = platform_strdup("127.0.0.1");
    config->api_address.port = 51003;
#endif
    // Initialize security with default values
    config->security.default_policy = platform_strdup("minimal");
    config->security.custom_paths = NULL;
    config->security.custom_paths_count = 0;
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
int cvd_config_load(const char* confdir)
{
    char buff[PATH_MAX] = { 0 };
    int  status;
    VLOG_DEBUG("config", "cvd_config_load(confdir=%s)\n", confdir);

    snprintf(&buff[0], sizeof(buff), "%s" CHEF_PATH_SEPARATOR_S "cvd.json", confdir);
    status = __load_config(&g_config, &buff[0]);
    if (status) {
        VLOG_ERROR("config", "failed to load or initialize configuration\n");
        return status;
    }
    return 0;
}

void cvd_config_api_address(struct cvd_config_address* address)
{
    address->type = g_config.api_address.type;
    address->address = g_config.api_address.address;
    address->port = g_config.api_address.port;
}

const char* cvd_config_security_default_policy(void)
{
    return g_config.security.default_policy;
}

void cvd_config_security_custom_paths(struct config_custom_path** paths, size_t* count)
{
    if (paths != NULL) {
        *paths = g_config.security.custom_paths;
    }
    if (count != NULL) {
        *count = g_config.security.custom_paths_count;
    }
}
