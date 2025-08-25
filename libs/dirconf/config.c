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
#include <stdlib.h>
#include <vlog.h>

static int __parse_config_address(struct chef_config_address* address, json_t* root)
{
    json_t* member;
    VLOG_DEBUG("config", "__parse_config_address()\n");

    member = json_object_get(root, "type");
    if (member != NULL) {
        address->type = platform_strdup(json_string_value(member));
    }

    member = json_object_get(root, "address");
    if (member != NULL) {
        address->address = platform_strdup(json_string_value(member));
    }

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

    // if both type and address is NULL then don't serialize
    if (address->type == NULL && address->address == NULL) {
        return NULL;
    }

    root = json_object();
    if (root == NULL) {
        VLOG_ERROR("config", "__serialize_config_address: failed to allocate memory for root object\n");
        return NULL;
    }

    if (address->type != NULL) {
        json_object_set_new(root, "type", json_string(address->type));
    }
    if (address->address != NULL) {
        json_object_set_new(root, "address", json_string(address->address));
    }
    json_object_set_new(root, "port", json_integer((long long)address->port));
    return root;
}

struct chef_config {
    char*   path;
    json_t* root_object;

    struct chef_config_address cvd;
    struct chef_config_address remote;
};

static struct chef_config* __chef_config_new(const char* path)
{
    struct chef_config* config;

    config = calloc(1, sizeof(struct chef_config));
    if (config == NULL) {
        return NULL;
    }
    config->path = platform_strdup(path);
    return config;
}

static void __chef_config_delete(struct chef_config* config)
{
    if (config == NULL) {
        return;
    }
    free(config);
}

static void __serialize_config(struct chef_config* config)
{
    json_t* remoteAddress;
    VLOG_DEBUG("config", "__serialize_config()\n");
    
    remoteAddress = __serialize_config_address(&config->remote);
    if (remoteAddress != NULL) {
        json_object_set_new(config->root_object, "remote-address", remoteAddress);
    }
    
    remoteAddress = __serialize_config_address(&config->cvd);
    if (remoteAddress != NULL) {
        json_object_set_new(config->root_object, "cvd-address", remoteAddress);
    }
}

static int __parse_config(struct chef_config* config)
{
    json_t* member;
    int     status;
    VLOG_DEBUG("config", "__parse_config(config=%s)\n", config->path);

    member = json_object_get(config->root_object, "remote-address");
    if (member != NULL) {
        status = __parse_config_address(&config->remote, member);
        if (status) {
            VLOG_ERROR("config", "chef_config_load: failed to parse 'remote-address'\n");
            return status;
        }
    }

    member = json_object_get(config->root_object, "cvd-address");
    if (member != NULL) {
        status = __parse_config_address(&config->cvd, member);
        if (status) {
            VLOG_ERROR("config", "chef_config_load: failed to parse 'cvd-address'\n");
            return status;
        }
    }

    return 0;
}

static int __initialize_config(struct chef_config* config)
{
    // No default values for remote address, it needs
    // to go through the wizard.
#ifdef CHEF_ON_LINUX
    config->cvd.type = platform_strdup("local");
    config->cvd.address = platform_strdup("@/chef/cvd/api");
#elif CHEF_ON_WINDOWS
    config->api.type = platform_strdup("inet4");
    config->api.address = platform_strdup("127.0.0.1");
    config->api.port = 51003;
#endif
    config->root_object = json_object();
    return 0;
}

struct chef_config* chef_config_load(const char* confdir)
{
    struct chef_config* config;
    json_error_t        error;
    char                path[PATH_MAX] = { 0 };
    VLOG_DEBUG("config", "chef_config_load(confdir=%s)\n", confdir);

    snprintf(&path[0], sizeof(path), "%s" CHEF_PATH_SEPARATOR_S "bake.json", confdir);

    config = __chef_config_new(&path[0]);
    if (config == NULL) {
        VLOG_ERROR("config", "chef_config_load: failed to allocate memory for configuration data\n");
        return NULL;
    }

    config->root_object = json_load_file(path, 0, &error);
    if (config->root_object == NULL) {
        if (json_error_code(&error) == json_error_cannot_open_file) {
            // assume no config, write the default one
            if (__initialize_config(config)) {
                __chef_config_delete(config);
                return NULL;
            }
            return config;
        }
        VLOG_ERROR("config", "chef_config_load: failed to load %s\n", &path[0]);
        __chef_config_delete(config);
        return NULL;
    }

    if (__parse_config(config)) {
        VLOG_ERROR("config", "chef_config_load: failed to parse %s\n", &path[0]);
        __chef_config_delete(config);
        return NULL;
    }
    return config;
}

int chef_config_save(struct chef_config* config)
{
    VLOG_DEBUG("config", "chef_config_save(path=%s)\n", config->path);

    __serialize_config(config);

    if (json_dump_file(config->root_object, config->path, JSON_INDENT(2)) < 0) {
        VLOG_ERROR("config", "chef_config_save: failed to write configuration to file\n");
        return -1;
    }
    return 0;
}

void chef_config_cvd_address(struct chef_config* config, struct chef_config_address* address)
{
    VLOG_DEBUG("config", "chef_config_cvd_address()\n");
    address->type = config->cvd.type;
    address->address = config->cvd.address;
    address->port = config->cvd.port;
}

void chef_config_remote_address(struct chef_config* config, struct chef_config_address* address)
{
    VLOG_DEBUG("config", "chef_config_remote_address()\n");
    address->type = config->remote.type;
    address->address = config->remote.address;
    address->port = config->remote.port;
}

void* chef_config_section(const struct chef_config* config, const char* section)
{
    json_t* sectionObject;
    VLOG_DEBUG("config", "chef_config_section(section=%s)\n", section);
    
    if (section == NULL) {
        return (void*)config->root_object;
    }

    sectionObject = json_object_get(config->root_object, section);
    if (sectionObject == NULL) {
        sectionObject = json_object();

        // store the section in the root object
        json_object_set_new(config->root_object, section, sectionObject);
    }
    return sectionObject;
}

const char* chef_config_get_string(const struct chef_config* config, void* section, const char* key)
{
    json_t* sectionObject = section;
    json_t* keyObject;
    VLOG_DEBUG("config", "chef_config_get_string(section=%s, key=%s)\n", section, key);

    if (key == NULL) {
        return NULL;
    }

    if (section == NULL) {
        sectionObject = config->root_object;
    }

    keyObject = json_object_get(sectionObject, key);
    if (keyObject == NULL) {
        return NULL;
    }
    return json_string_value(keyObject);
}

int chef_config_set_string(struct chef_config* config, void* section, const char* key, const char* value)
{
    json_t* sectionObject = section;
    VLOG_DEBUG("config", "chef_config_set_string(section=%s, key=%s, value=%s)\n", section, key, value);

    if (key == NULL) {
        VLOG_ERROR("config", "chef_config_set_string: key is NULL\n");
        return -1;
    }

    if (section == NULL) {
        sectionObject = config->root_object;
    }

    if (value != NULL) {
        json_object_set_new(sectionObject, key, json_string(value));
    } else {
        json_object_del(sectionObject, key);
    }
    return 0;
}

static int __replace_string(char** original, const char* value)
{
    if (*original != NULL) {
        free(*original);
    }

    if (value != NULL) {
        *original = platform_strdup(value);
        return *original != NULL ? 0 : -1;
    }

    *original = NULL;
    return 0;
}

void chef_config_set_cvd_address(struct chef_config* config, struct chef_config_address* address)
{
    VLOG_DEBUG("config", "chef_config_set_cvd_address(address=%s)\n", address->address);
    if (__replace_string((char**)&config->cvd.type, address->type) ||
        __replace_string((char**)&config->cvd.address, address->address)) {
        VLOG_ERROR("config", "chef_config_set_cvd_address: failed to update address\n");
        return;
    }
    config->cvd.port = address->port;
}

void chef_config_set_remote_address(struct chef_config* config, struct chef_config_address* address)
{
    VLOG_DEBUG("config", "chef_config_set_remote_address(address=%s)\n", address->address);
    if (__replace_string((char**)&config->remote.type, address->type) ||
        __replace_string((char**)&config->remote.address, address->address)) {
        VLOG_ERROR("config", "chef_config_set_remote_address: failed to update address\n");
        return;
    }
    config->remote.port = address->port;
}
