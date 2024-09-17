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

#include <chef/config.h>
#include <chef/platform.h>
#include <jansson.h>


static int __parse_config_address(struct chef_config_address* address, json_t* root)
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

static json_t* __serialize_config_address(struct chef_config_address* address)
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

struct chef_config {
    struct chef_config_address remote;
};


static json_t* __serialize_config(struct chef_config* config)
{
    json_t* root;
    json_t* remote_address;
    VLOG_DEBUG("config", "__serialize_config()\n");
    
    root = json_object();
    if (!root) {
        VLOG_ERROR("config", "__serialize_config: failed to allocate memory for root object\n");
        return NULL;
    }
    
    remote_address = __serialize_config_address(&config->remote);
    if (remote_address == NULL) {
        json_decref(root);
        return NULL;
    }
    
    json_object_set_new(root, "remote-address", remote_address);
    return root;
}

static int __parse_config(struct chef_config* config, json_t* root)
{
    json_t* member;
    int     status;

    member = json_object_get(root, "remote-address");
    if (member == NULL) {
        return 0;
    }

    status = __parse_config_address(&config->remote, member);
    if (status) {
        return status;
    }
    return 0;
}


struct chef_config* chef_config_load(void)
{
    json_error_t error;
    json_t*      root;
    char         buff[PATH_MAX] = { 0 };

    snprintf(&buff[0], sizeof(buff), "%s" CHEF_PATH_SEPARATOR_S "bake.json", confdir);

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

int chef_config_save(struct chef_config* config)
{

}

void chef_config_remote_address(struct chef_config* config, struct chef_config_address* address)
{

}

void chef_config_set_remote_address(struct chef_config* config, struct chef_config_address* address)
{

}
