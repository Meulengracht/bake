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

#include <chef/package.h>
#include <chef/platform.h>
#include <chef/utils_vafs.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>

static struct VaFsGuid g_headerGuid   = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_versionGuid  = CHEF_PACKAGE_VERSION_GUID;
static struct VaFsGuid g_commandsGuid = CHEF_PACKAGE_APPS_GUID;
static struct VaFsGuid g_networkGuid  = CHEF_PACKAGE_NETWORK_GUID;
static struct VaFsGuid g_capabilitiesGuid = CHEF_PACKAGE_CAPABILITIES_GUID;

static int __load_package_header(struct VaFs* vafs, struct chef_package** packageOut)
{
    struct chef_vafs_feature_package_header* header;
    struct chef_package*                     package;
    char*                                    data;
    int                                      status;

    status = vafs_feature_query(vafs, &g_headerGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    package = (struct chef_package*)malloc(sizeof(struct chef_package));
    if (package == NULL) {
        return -1;
    }
    memset(package, 0, sizeof(struct chef_package));

    data = (char*)header + sizeof(struct chef_vafs_feature_package_header);

    package->type = header->type;

#define READ_IF_PRESENT(__MEM) if (header->__MEM ## _length > 0) { \
        package->__MEM = platform_strndup(data, header->__MEM ## _length); \
        data += header->__MEM ## _length; \
    }

    READ_IF_PRESENT(platform)
    READ_IF_PRESENT(arch)
    READ_IF_PRESENT(package)
    READ_IF_PRESENT(base)
    READ_IF_PRESENT(summary)
    READ_IF_PRESENT(description)
    READ_IF_PRESENT(homepage)
    READ_IF_PRESENT(license)
    READ_IF_PRESENT(eula)
    READ_IF_PRESENT(maintainer)
    READ_IF_PRESENT(maintainer_email)

#undef READ_IF_PRESENT

    *packageOut = package;
    return 0;
}

static int __load_package_version(struct VaFs* vafs, struct chef_version** versionOut)
{
    struct chef_vafs_feature_package_version* header;
    struct chef_version*                      version;
    char*                                     data;
    int                                       status;

    status = vafs_feature_query(vafs, &g_versionGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    version = (struct chef_version*)malloc(sizeof(struct chef_version));
    if (version == NULL) {
        return -1;
    }
    memset(version, 0, sizeof(struct chef_version));

    data = (char*)header + sizeof(struct chef_vafs_feature_package_version);

    version->major = header->major;
    version->minor = header->minor;
    version->patch = header->patch;
    version->revision = header->revision;

    if (header->tag_length) {
        version->tag = platform_strndup(data, header->tag_length);
    }

    *versionOut = version;
    return 0;
}

static void __fill_command(char** dataPointer, struct chef_command* command)
{
    struct chef_vafs_package_app* entry = (struct chef_vafs_package_app*)*dataPointer;

    command->type = (enum chef_command_type)entry->type;

    // move datapointer up to the rest of the data
    *dataPointer += sizeof(struct chef_vafs_package_app);

#define READ_IF_PRESENT(__MEM) if (entry->__MEM ## _length > 0) { \
        command->__MEM = platform_strndup(*dataPointer, entry->__MEM ## _length); \
        *dataPointer += entry->__MEM ## _length; \
    }

    READ_IF_PRESENT(name)
    READ_IF_PRESENT(description)
    READ_IF_PRESENT(arguments)
    READ_IF_PRESENT(path)

    // TOOD skip icon for now, we haven't completed support for this
    // on linux yet
    *dataPointer += entry->icon_length;
#undef READ_IF_PRESENT
}

static int __load_package_commands(struct VaFs* vafs, struct chef_command** commandsOut, int* commandCountOut)
{
    struct chef_vafs_feature_package_apps* header;
    struct chef_command*                   commands;
    char*                                  data;
    int                                    status;

    status = vafs_feature_query(vafs, &g_commandsGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    if (header->apps_count == 0) {
        return -1;
    }

    commands = (struct chef_command*)calloc(header->apps_count, sizeof(struct chef_command));
    if (commands == NULL) {
        return -1;
    }

    data = (char*)header + sizeof(struct chef_vafs_feature_package_apps);
    for (int i = 0; i < header->apps_count; i++) {
        __fill_command(&data, &commands[i]);
    }

    *commandsOut     = commands;
    *commandCountOut = header->apps_count;
    return 0;
}

static int __load_package_network(struct VaFs* vafs, struct chef_package_application_config* appConfig)
{
    struct chef_vafs_feature_package_network* header;
    int                                       status;
    char*                                     data;

    // optional feature
    status = vafs_feature_query(vafs, &g_networkGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return 0;
    }

    data = (char*)header + sizeof(struct chef_vafs_feature_package_network);
    if (header->gateway_length > 0) {
        appConfig->network_gateway = platform_strndup(data, header->gateway_length);
        data += header->gateway_length;
    }

    if (header->dns_length > 0) {
        appConfig->network_dns = platform_strndup(data, header->dns_length);
        data += header->dns_length;
    }
    return 0;
}

static int __load_package_capabilities(
    struct VaFs*                      vafs,
    struct chef_package_capability**  capabilitiesOut,
    int*                              capabilitiesCountOut)
{
    struct chef_vafs_feature_package_capabilities* header;
    struct chef_package_capability*                caps;
    char*                                          data;
    int                                            status;

    status = vafs_feature_query(vafs, &g_capabilitiesGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    if (header->capabilities_count == 0) {
        return -1;
    }

    caps = calloc(header->capabilities_count, sizeof(struct chef_package_capability));
    if (caps == NULL) {
        return -1;
    }

    data = (char*)header + sizeof(struct chef_vafs_feature_package_capabilities);
    for (uint32_t i = 0; i < header->capabilities_count; i++) {
        uint32_t nameLen, configCount;

        memcpy(&nameLen, data, sizeof(uint32_t));     data += sizeof(uint32_t);
        memcpy(&configCount, data, sizeof(uint32_t)); data += sizeof(uint32_t);

        if (nameLen > 0) {
            caps[i].name = platform_strndup(data, nameLen);
            data += nameLen;
        }

        caps[i].config_count = (int)configCount;
        if (configCount > 0) {
            caps[i].config = calloc(configCount, sizeof(struct chef_package_capability_config));
            if (caps[i].config == NULL) {
                chef_package_capabilities_free(caps, (int)i);
                return -1;
            }

            for (uint32_t j = 0; j < configCount; j++) {
                uint32_t keyLen, valLen, valsCnt;

                memcpy(&keyLen, data, sizeof(uint32_t));   data += sizeof(uint32_t);
                memcpy(&valLen, data, sizeof(uint32_t));   data += sizeof(uint32_t);
                memcpy(&valsCnt, data, sizeof(uint32_t));  data += sizeof(uint32_t);

                if (keyLen > 0) {
                    caps[i].config[j].key = platform_strndup(data, keyLen);
                    data += keyLen;
                }
                if (valLen > 0) {
                    caps[i].config[j].value = platform_strndup(data, valLen);
                    data += valLen;
                }
                caps[i].config[j].values_count = (int)valsCnt;
                if (valsCnt > 0) {
                    caps[i].config[j].values = calloc(valsCnt, sizeof(const char*));
                    if (caps[i].config[j].values == NULL) {
                        chef_package_capabilities_free(caps, (int)i + 1);
                        return -1;
                    }
                    for (uint32_t k = 0; k < valsCnt; k++) {
                        uint32_t itemLen;
                        memcpy(&itemLen, data, sizeof(uint32_t)); data += sizeof(uint32_t);
                        if (itemLen > 0) {
                            caps[i].config[j].values[k] = platform_strndup(data, itemLen);
                            data += itemLen;
                        }
                    }
                }
            }
        }
    }

    *capabilitiesOut = caps;
    *capabilitiesCountOut = (int)header->capabilities_count;
    return 0;
}

int chef_package_load_vafs(
    struct VaFs*                             vafs,
    struct chef_package**                    packageOut,
    struct chef_version**                    versionOut,
    struct chef_command**                    commandsOut,
    int*                                     commandCountOut,
    struct chef_package_application_config** appConfigOut,
    struct chef_package_capability**         capabilitiesOut,
    int*                                     capabilitiesCountOut)
{
    int status = 0;

    if (packageOut != NULL) {
        status = __load_package_header(vafs, packageOut);
        if (status != 0) {
            vafs_close(vafs);
            return status;
        }
    }

    if (versionOut != NULL) {
        status = __load_package_version(vafs, versionOut);
        if (status != 0) {
            // This is a required header, so something is definitely off
            // lets cleanup
            if (packageOut != NULL) {
                chef_package_free(*packageOut);
            }
            vafs_close(vafs);
            return status;
        }
    }

    if (commandsOut != NULL && commandCountOut) {
        // This header is optional, which means we won't ever fail on it. If
        // the loader/locate returns error, we zero the out values
        status = __load_package_commands(vafs, commandsOut, commandCountOut);
        if (status != 0) {
            *commandsOut     = NULL;
            *commandCountOut = 0;
        }
    }

    if (appConfigOut != NULL) {
        // This header is optional, which means we won't ever fail on it. If
        // the loader/locate returns error, we zero the out values
        status = __load_package_network(vafs, *appConfigOut);
        if (status != 0) {
            *appConfigOut = NULL;
        }
    }

    if (capabilitiesOut != NULL && capabilitiesCountOut != NULL) {
        // This header is optional
        status = __load_package_capabilities(vafs, capabilitiesOut, capabilitiesCountOut);
        if (status != 0) {
            *capabilitiesOut = NULL;
            *capabilitiesCountOut = 0;
        }
    }

    return status;
}

int chef_package_load(
        const char*           path,
        struct chef_package** packageOut,
        struct chef_version** versionOut,
        struct chef_command** commandsOut,
        int*                  commandCountOut)
{
    struct VaFs* vafs;
    int          status;

    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_open_file(path, &vafs);
    if (status != 0) {
        return status;
    }

    status = chef_package_load_vafs(
        vafs,
        packageOut,
        versionOut,
        commandsOut,
        commandCountOut,
        NULL,
        NULL,
        NULL
    );
    vafs_close(vafs);
    return status;
}

static void __free_version(struct chef_version* version)
{
    free((void*)version->created);
    free((void*)version->tag);
}

static void __free_revision(struct chef_revision* revision)
{
    free((void*)revision->channel);
    free((void*)revision->platform);
    free((void*)revision->architecture);
    __free_version(&revision->current_version);
}

void chef_package_proof_free(struct chef_package_proof* proof)
{
    if (proof == NULL) {
        return;
    }

    free((void*)proof->identity);
    free((void*)proof->hash_algorithm);
    free((void*)proof->hash);
    free((void*)proof->public_key);
    free((void*)proof->signature);
    free(proof);
}

void chef_package_free(struct chef_package* package)
{
    if (package == NULL) {
        return;
    }

    free((void*)package->publisher);
    free((void*)package->package);
    free((void*)package->description);
    free((void*)package->homepage);
    free((void*)package->license);
    free((void*)package->maintainer);
    free((void*)package->maintainer_email);

    if (package->revisions != NULL) {
        for (size_t i = 0; i < package->revisions_count; i++) {
            __free_revision(&package->revisions[i]);
        }
        free(package->revisions);
    }
    free(package);
}

void chef_version_free(struct chef_version* version)
{
    if (version == NULL) {
        return;
    }

    __free_version(version);
    free(version);
}

void chef_commands_free(struct chef_command* commands, int count)
{
    if (commands == NULL || count == 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((void*)commands[i].name);
        free((void*)commands[i].path);
        free((void*)commands[i].arguments);
        free((void*)commands[i].description);
        free((void*)commands[i].icon_buffer);
    }
    free(commands);
}

void chef_package_application_config_free(struct chef_package_application_config* appConfig)
{
    if (appConfig == NULL) {
        return;
    }

    free((void*)appConfig->network_gateway);
    free((void*)appConfig->network_dns);
}

void chef_package_capabilities_free(struct chef_package_capability* capabilities, int count)
{
    if (capabilities == NULL || count == 0) {
        return;
    }

    for (int i = 0; i < count; i++) {
        free((void*)capabilities[i].name);
        if (capabilities[i].config != NULL) {
            for (int j = 0; j < capabilities[i].config_count; j++) {
                free((void*)capabilities[i].config[j].key);
                free((void*)capabilities[i].config[j].value);
                if (capabilities[i].config[j].values != NULL) {
                    for (int k = 0; k < capabilities[i].config[j].values_count; k++) {
                        free((void*)capabilities[i].config[j].values[k]);
                    }
                    free(capabilities[i].config[j].values);
                }
            }
            free(capabilities[i].config);
        }
    }
    free(capabilities);
}

int chef_version_from_string(const char* string, struct chef_version* version)
{
    // parse a version string of format "1.2.3(+tag)"
    // where tag is optional
    char* pointer    = (char*)string;
    char* pointerEnd = strchr(pointer, '.');

    // if '.' was not found, then the revision is provided, so we use that
    if (pointerEnd == NULL) {
        version->major    = 0;
        version->minor    = 0;
        version->patch    = 0;
        version->revision = (int)strtol(pointer, &pointerEnd, 10);
        if (version->revision == 0) {
            errno = EINVAL;
            return -1;
        }

        version->tag = NULL;
        return 0;
    }
    
    // extract first part
    version->major = (int)strtol(pointer, &pointerEnd, 10);
    
    // extract second part
    pointer    = pointerEnd + 1;
    pointerEnd = strchr(pointer, '.');
    if (pointerEnd == NULL) {
        errno = EINVAL;
        return -1;
    }
    version->minor = strtol(pointer, &pointerEnd, 10);
    
    pointer    = pointerEnd + 1;
    pointerEnd = NULL;
    
    // extract the 3rd part, patch
    version->patch = strtol(pointer, &pointerEnd, 10);
    version->tag   = NULL;
    return 0;
}
