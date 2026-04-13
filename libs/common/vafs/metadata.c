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

#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <chef/utils_vafs.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vafs/vafs.h>
#include <vlog.h>

static struct VaFsGuid g_headerGuid       = CHEF_PACKAGE_HEADER_GUID;
static struct VaFsGuid g_versionGuid      = CHEF_PACKAGE_VERSION_GUID;
static struct VaFsGuid g_iconGuid         = CHEF_PACKAGE_ICON_GUID;
static struct VaFsGuid g_commandsGuid     = CHEF_PACKAGE_APPS_GUID;
static struct VaFsGuid g_optionsGuid      = CHEF_PACKAGE_INGREDIENT_OPTS_GUID;
static struct VaFsGuid g_networkGuid      = CHEF_PACKAGE_NETWORK_GUID;
static struct VaFsGuid g_capabilitiesGuid = CHEF_PACKAGE_CAPABILITIES_GUID;

struct __manifest_capability_serializer {
    const char* name;
    size_t (*serialize)(const struct chef_package_manifest_capability* capability, char* buffer);
    size_t (*size)(const struct chef_package_manifest_capability* capability);
};

static size_t __safe_strlen(const char* string)
{
    if (string == NULL) {
        return 0;
    }
    return strlen(string);
}

static void* __duplicate_buffer(const void* data, size_t size)
{
    void* duplicate;

    if (data == NULL || size == 0) {
        return NULL;
    }

    duplicate = malloc(size);
    if (duplicate == NULL) {
        return NULL;
    }

    memcpy(duplicate, data, size);
    return duplicate;
}

static int __duplicate_string_array(
    struct chef_package_string_array* destination,
    const struct chef_package_string_array* source)
{
    size_t i;

    destination->values = NULL;
    destination->count = 0;
    if (source == NULL || source->count == 0) {
        return 0;
    }

    destination->values = calloc(source->count, sizeof(char*));
    if (destination->values == NULL) {
        errno = ENOMEM;
        return -1;
    }

    destination->count = source->count;
    for (i = 0; i < source->count; i++) {
        ((char**)destination->values)[i] = platform_strdup(source->values[i]);
        if (((char**)destination->values)[i] == NULL) {
            return -1;
        }
    }
    return 0;
}

static void __free_string_array(struct chef_package_string_array* array)
{
    size_t i;

    if (array == NULL || array->values == NULL) {
        return;
    }

    for (i = 0; i < array->count; i++) {
        free((void*)array->values[i]);
    }
    free((void*)array->values);
    array->values = NULL;
    array->count = 0;
}

static void __free_manifest_contents(struct chef_package_manifest* manifest)
{
    size_t i;

    if (manifest == NULL) {
        return;
    }

    free((void*)manifest->name);
    free((void*)manifest->platform);
    free((void*)manifest->architecture);
    free((void*)manifest->base);
    free((void*)manifest->summary);
    free((void*)manifest->description);
    free((void*)manifest->license);
    free((void*)manifest->eula);
    free((void*)manifest->maintainer);
    free((void*)manifest->maintainer_email);
    free((void*)manifest->homepage);
    free((void*)manifest->version.tag);
    free((void*)manifest->version.created);
    free((void*)manifest->icon.data);
    free((void*)manifest->application.network_gateway);
    free((void*)manifest->application.network_dns);

    __free_string_array(&manifest->ingredient.bin_dirs);
    __free_string_array(&manifest->ingredient.inc_dirs);
    __free_string_array(&manifest->ingredient.lib_dirs);
    __free_string_array(&manifest->ingredient.compiler_flags);
    __free_string_array(&manifest->ingredient.linker_flags);

    for (i = 0; i < manifest->commands_count; i++) {
        free((void*)manifest->commands[i].name);
        free((void*)manifest->commands[i].description);
        free((void*)manifest->commands[i].arguments);
        free((void*)manifest->commands[i].path);
        free((void*)manifest->commands[i].icon.data);
    }
    free(manifest->commands);

    for (i = 0; i < manifest->capabilities_count; i++) {
        free((void*)manifest->capabilities[i].name);
        __free_string_array(&manifest->capabilities[i].allow_list);
    }
    free(manifest->capabilities);
}

static int __copy_manifest_string(
    char** destination,
    const char* source,
    size_t length)
{
    *destination = NULL;
    if (length == 0) {
        return 0;
    }

    *destination = platform_strndup(source, length);
    if (*destination == NULL) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

static int __serialize_csv_string_array(
    const struct chef_package_string_array* array,
    char** bufferOut,
    size_t* lengthOut)
{
    size_t totalLength = 0;
    size_t i;
    char*  buffer;
    char*  itr;

    *bufferOut = NULL;
    *lengthOut = 0;
    if (array == NULL || array->count == 0) {
        return 0;
    }

    for (i = 0; i < array->count; i++) {
        totalLength += __safe_strlen(array->values[i]);
        if (i + 1 < array->count) {
            totalLength++;
        }
    }

    buffer = calloc(totalLength + 1, 1);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    itr = buffer;
    for (i = 0; i < array->count; i++) {
        size_t length = __safe_strlen(array->values[i]);

        if (length > 0) {
            memcpy(itr, array->values[i], length);
            itr += length;
        }
        if (i + 1 < array->count) {
            *itr++ = ',';
        }
    }

    *bufferOut = buffer;
    *lengthOut = totalLength;
    return 0;
}

static int __parse_csv_string_array(
    const char* source,
    size_t length,
    struct chef_package_string_array* arrayOut)
{
    const char* itr;
    const char* tokenStart;
    size_t      count = 1;
    size_t      index = 0;

    arrayOut->values = NULL;
    arrayOut->count = 0;
    if (source == NULL || length == 0) {
        return 0;
    }

    for (itr = source; itr < source + length; itr++) {
        if (*itr == ',') {
            count++;
        }
    }

    arrayOut->values = calloc(count, sizeof(char*));
    if (arrayOut->values == NULL) {
        errno = ENOMEM;
        return -1;
    }
    arrayOut->count = count;

    tokenStart = source;
    for (itr = source; itr <= source + length; itr++) {
        if (itr == source + length || *itr == ',') {
            size_t tokenLength = (size_t)(itr - tokenStart);

            ((char**)arrayOut->values)[index] = platform_strndup(tokenStart, tokenLength);
            if (((char**)arrayOut->values)[index] == NULL) {
                errno = ENOMEM;
                return -1;
            }
            index++;
            tokenStart = itr + 1;
        }
    }
    return 0;
}

static size_t __capability_allow_list_size(const struct chef_package_manifest_capability* capability)
{
    size_t size = 0;
    size_t i;

    for (i = 0; i < capability->allow_list.count; i++) {
        size += sizeof(uint32_t) + __safe_strlen(capability->allow_list.values[i]);
    }
    return size;
}

static size_t __capability_allow_list_serialize(
    const struct chef_package_manifest_capability* capability,
    char*                                          buffer)
{
    size_t size = 0;
    size_t i;

    for (i = 0; i < capability->allow_list.count; i++) {
        uint32_t length = (uint32_t)__safe_strlen(capability->allow_list.values[i]);

        memcpy(buffer, &length, sizeof(uint32_t));
        buffer += sizeof(uint32_t);
        memcpy(buffer, capability->allow_list.values[i], length);
        buffer += length;
        size += sizeof(uint32_t) + length;
    }
    return size;
}

static struct __manifest_capability_serializer g_capabilitySerializers[] = {
    { "network-client", __capability_allow_list_serialize, __capability_allow_list_size },
    { NULL, NULL, NULL }
};

static const struct __manifest_capability_serializer* __find_capability_serializer(const char* name)
{
    size_t i;

    for (i = 0; g_capabilitySerializers[i].name != NULL; i++) {
        if (strcmp(g_capabilitySerializers[i].name, name) == 0) {
            return &g_capabilitySerializers[i];
        }
    }
    return NULL;
}

static int __write_header_metadata(struct VaFs* vafs, const struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_header* packageHeader;
    size_t                                   featureSize;
    char*                                    dataPointer;
    int                                      status;

    featureSize = sizeof(struct chef_vafs_feature_package_header);
    featureSize += __safe_strlen(manifest->platform);
    featureSize += __safe_strlen(manifest->architecture);
    featureSize += __safe_strlen(manifest->name);
    featureSize += __safe_strlen(manifest->base);
    featureSize += __safe_strlen(manifest->summary);
    featureSize += __safe_strlen(manifest->description);
    featureSize += __safe_strlen(manifest->license);
    featureSize += __safe_strlen(manifest->eula);
    featureSize += __safe_strlen(manifest->homepage);
    featureSize += __safe_strlen(manifest->maintainer);
    featureSize += __safe_strlen(manifest->maintainer_email);

    packageHeader = malloc(featureSize);
    if (packageHeader == NULL) {
        VLOG_ERROR("bake", "failed to allocate package header\n");
        return -1;
    }

    memcpy(&packageHeader->header.Guid, &g_headerGuid, sizeof(struct VaFsGuid));
    packageHeader->header.Length = (uint32_t)featureSize;
    packageHeader->version = CHEF_PACKAGE_VERSION;
    packageHeader->type = manifest->type;
    packageHeader->platform_length = (uint32_t)__safe_strlen(manifest->platform);
    packageHeader->arch_length = (uint32_t)__safe_strlen(manifest->architecture);
    packageHeader->package_length = (uint32_t)__safe_strlen(manifest->name);
    packageHeader->base_length = (uint32_t)__safe_strlen(manifest->base);
    packageHeader->summary_length = (uint32_t)__safe_strlen(manifest->summary);
    packageHeader->description_length = (uint32_t)__safe_strlen(manifest->description);
    packageHeader->homepage_length = (uint32_t)__safe_strlen(manifest->homepage);
    packageHeader->license_length = (uint32_t)__safe_strlen(manifest->license);
    packageHeader->eula_length = (uint32_t)__safe_strlen(manifest->eula);
    packageHeader->maintainer_length = (uint32_t)__safe_strlen(manifest->maintainer);
    packageHeader->maintainer_email_length = (uint32_t)__safe_strlen(manifest->maintainer_email);

    dataPointer = (char*)packageHeader + sizeof(struct chef_vafs_feature_package_header);

#define WRITE_MANIFEST_STRING(__FIELD, __SOURCE) \
    if (packageHeader->__FIELD ## _length > 0) { \
        memcpy(dataPointer, manifest->__SOURCE, packageHeader->__FIELD ## _length); \
        dataPointer += packageHeader->__FIELD ## _length; \
    }

    WRITE_MANIFEST_STRING(platform, platform)
    WRITE_MANIFEST_STRING(arch, architecture)
    WRITE_MANIFEST_STRING(package, name)
    WRITE_MANIFEST_STRING(base, base)
    WRITE_MANIFEST_STRING(summary, summary)
    WRITE_MANIFEST_STRING(description, description)
    WRITE_MANIFEST_STRING(homepage, homepage)
    WRITE_MANIFEST_STRING(license, license)
    WRITE_MANIFEST_STRING(eula, eula)
    WRITE_MANIFEST_STRING(maintainer, maintainer)
    WRITE_MANIFEST_STRING(maintainer_email, maintainer_email)

#undef WRITE_MANIFEST_STRING

    status = vafs_feature_add(vafs, &packageHeader->header);
    free(packageHeader);
    if (status != 0) {
        VLOG_ERROR("bake", "failed to write package header\n");
        return -1;
    }
    return 0;
}

static int __write_version_metadata(struct VaFs* vafs, const struct chef_version* version)
{
    struct chef_vafs_feature_package_version* packageVersion;
    size_t                                    featureSize;
    int                                       status;

    featureSize = sizeof(struct chef_vafs_feature_package_version) + __safe_strlen(version->tag);
    packageVersion = malloc(featureSize);
    if (packageVersion == NULL) {
        VLOG_ERROR("bake", "failed to allocate package version\n");
        return -1;
    }

    memcpy(&packageVersion->header.Guid, &g_versionGuid, sizeof(struct VaFsGuid));
    packageVersion->header.Length = (uint32_t)featureSize;
    packageVersion->major = version->major;
    packageVersion->minor = version->minor;
    packageVersion->patch = version->patch;
    packageVersion->revision = version->revision;
    packageVersion->tag_length = (uint32_t)__safe_strlen(version->tag);

    if (packageVersion->tag_length > 0) {
        memcpy(
            (char*)packageVersion + sizeof(struct chef_vafs_feature_package_version),
            version->tag,
            packageVersion->tag_length
        );
    }

    status = vafs_feature_add(vafs, &packageVersion->header);
    free(packageVersion);
    return status;
}

static int __write_icon_metadata(struct VaFs* vafs, const struct chef_package_blob* icon)
{
    struct chef_vafs_feature_package_icon* packageIcon;
    size_t                                 featureSize;
    int                                    status;

    if (icon == NULL || icon->data == NULL || icon->size == 0) {
        return 0;
    }

    featureSize = sizeof(struct chef_vafs_feature_package_icon) + icon->size;
    packageIcon = malloc(featureSize);
    if (packageIcon == NULL) {
        VLOG_ERROR("bake", "failed to allocate package icon\n");
        return -1;
    }

    memcpy(&packageIcon->header.Guid, &g_iconGuid, sizeof(struct VaFsGuid));
    packageIcon->header.Length = (uint32_t)featureSize;
    memcpy((char*)packageIcon + sizeof(struct chef_vafs_feature_package_icon), icon->data, icon->size);

    status = vafs_feature_add(vafs, &packageIcon->header);
    free(packageIcon);
    return status;
}

static size_t __command_size(const struct chef_package_manifest_command* command)
{
    return sizeof(struct chef_vafs_package_app)
        + __safe_strlen(command->name)
        + __safe_strlen(command->description)
        + __safe_strlen(command->arguments)
        + __safe_strlen(command->path)
        + command->icon.size;
}

static size_t __serialize_command(const struct chef_package_manifest_command* command, char* buffer)
{
    struct chef_vafs_package_app* app = (struct chef_vafs_package_app*)buffer;

    app->name_length = (uint32_t)__safe_strlen(command->name);
    app->description_length = (uint32_t)__safe_strlen(command->description);
    app->arguments_length = (uint32_t)__safe_strlen(command->arguments);
    app->type = (int)command->type;
    app->path_length = (uint32_t)__safe_strlen(command->path);
    app->icon_length = (uint32_t)command->icon.size;

    buffer += sizeof(struct chef_vafs_package_app);
    if (app->name_length > 0) {
        memcpy(buffer, command->name, app->name_length);
        buffer += app->name_length;
    }
    if (app->description_length > 0) {
        memcpy(buffer, command->description, app->description_length);
        buffer += app->description_length;
    }
    if (app->arguments_length > 0) {
        memcpy(buffer, command->arguments, app->arguments_length);
        buffer += app->arguments_length;
    }
    if (app->path_length > 0) {
        memcpy(buffer, command->path, app->path_length);
        buffer += app->path_length;
    }
    if (app->icon_length > 0) {
        memcpy(buffer, command->icon.data, app->icon_length);
    }

    return sizeof(struct chef_vafs_package_app)
        + app->name_length
        + app->description_length
        + app->arguments_length
        + app->path_length
        + app->icon_length;
}

static int __write_commands_metadata(struct VaFs* vafs, const struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_apps* packageApps;
    size_t                                 totalSize = sizeof(struct chef_vafs_feature_package_apps);
    size_t                                 i;
    char*                                  buffer;
    int                                    status;

    if (manifest->commands_count == 0) {
        return 0;
    }

    for (i = 0; i < manifest->commands_count; i++) {
        totalSize += __command_size(&manifest->commands[i]);
    }

    buffer = malloc(totalSize);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    packageApps = (struct chef_vafs_feature_package_apps*)buffer;
    memcpy(&packageApps->header.Guid, &g_commandsGuid, sizeof(struct VaFsGuid));
    packageApps->header.Length = (uint32_t)totalSize;
    packageApps->apps_count = (int)manifest->commands_count;

    buffer += sizeof(struct chef_vafs_feature_package_apps);
    for (i = 0; i < manifest->commands_count; i++) {
        buffer += __serialize_command(&manifest->commands[i], buffer);
    }

    status = vafs_feature_add(vafs, &packageApps->header);
    free(packageApps);
    return status;
}

static int __write_ingredient_options_metadata(struct VaFs* vafs, const struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_ingredient_opts* optionsFeature;
    char*                                     bins = NULL;
    char*                                     incs = NULL;
    char*                                     libs = NULL;
    char*                                     compilerFlags = NULL;
    char*                                     linkerFlags = NULL;
    size_t                                    binsLength = 0;
    size_t                                    incsLength = 0;
    size_t                                    libsLength = 0;
    size_t                                    compilerFlagsLength = 0;
    size_t                                    linkerFlagsLength = 0;
    size_t                                    totalSize;
    char*                                     data;
    int                                       status;

    if (manifest->type != CHEF_PACKAGE_TYPE_INGREDIENT) {
        return 0;
    }

    if (__serialize_csv_string_array(&manifest->ingredient.bin_dirs, &bins, &binsLength)
     || __serialize_csv_string_array(&manifest->ingredient.inc_dirs, &incs, &incsLength)
     || __serialize_csv_string_array(&manifest->ingredient.lib_dirs, &libs, &libsLength)
     || __serialize_csv_string_array(&manifest->ingredient.compiler_flags, &compilerFlags, &compilerFlagsLength)
     || __serialize_csv_string_array(&manifest->ingredient.linker_flags, &linkerFlags, &linkerFlagsLength)) {
        free(bins);
        free(incs);
        free(libs);
        free(compilerFlags);
        free(linkerFlags);
        return -1;
    }

    totalSize = sizeof(struct chef_vafs_feature_ingredient_opts)
        + binsLength + incsLength + libsLength + compilerFlagsLength + linkerFlagsLength;

    optionsFeature = malloc(totalSize);
    if (optionsFeature == NULL) {
        free(bins);
        free(incs);
        free(libs);
        free(compilerFlags);
        free(linkerFlags);
        errno = ENOMEM;
        return -1;
    }

    memcpy(&optionsFeature->header.Guid, &g_optionsGuid, sizeof(struct VaFsGuid));
    optionsFeature->header.Length = (uint32_t)totalSize;
    optionsFeature->bin_dirs_length = (uint32_t)binsLength;
    optionsFeature->inc_dirs_length = (uint32_t)incsLength;
    optionsFeature->lib_dirs_length = (uint32_t)libsLength;
    optionsFeature->compiler_flags_length = (uint32_t)compilerFlagsLength;
    optionsFeature->linker_flags_length = (uint32_t)linkerFlagsLength;

    data = (char*)optionsFeature + sizeof(struct chef_vafs_feature_ingredient_opts);

#define WRITE_OPTIONAL_BUFFER(__BUFFER, __LENGTH) \
    if ((__LENGTH) > 0) { \
        memcpy(data, (__BUFFER), (__LENGTH)); \
        data += (__LENGTH); \
    }

    WRITE_OPTIONAL_BUFFER(bins, binsLength)
    WRITE_OPTIONAL_BUFFER(incs, incsLength)
    WRITE_OPTIONAL_BUFFER(libs, libsLength)
    WRITE_OPTIONAL_BUFFER(compilerFlags, compilerFlagsLength)
    WRITE_OPTIONAL_BUFFER(linkerFlags, linkerFlagsLength)

#undef WRITE_OPTIONAL_BUFFER

    status = vafs_feature_add(vafs, &optionsFeature->header);
    free(optionsFeature);
    free(bins);
    free(incs);
    free(libs);
    free(compilerFlags);
    free(linkerFlags);
    return status;
}

static int __write_network_metadata(struct VaFs* vafs, const struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_network* network;
    size_t                                    gatewayLength;
    size_t                                    dnsLength;
    size_t                                    totalSize;
    char*                                     data;
    int                                       status;

    if (manifest->type != CHEF_PACKAGE_TYPE_APPLICATION) {
        return 0;
    }

    gatewayLength = __safe_strlen(manifest->application.network_gateway);
    dnsLength = __safe_strlen(manifest->application.network_dns);
    if (gatewayLength == 0 && dnsLength == 0) {
        return 0;
    }

    totalSize = sizeof(struct chef_vafs_feature_package_network) + gatewayLength + dnsLength;
    network = malloc(totalSize);
    if (network == NULL) {
        errno = ENOMEM;
        return -1;
    }

    memcpy(&network->header.Guid, &g_networkGuid, sizeof(struct VaFsGuid));
    network->header.Length = (uint32_t)totalSize;
    network->gateway_length = (uint32_t)gatewayLength;
    network->dns_length = (uint32_t)dnsLength;

    data = (char*)network + sizeof(struct chef_vafs_feature_package_network);
    if (gatewayLength > 0) {
        memcpy(data, manifest->application.network_gateway, gatewayLength);
        data += gatewayLength;
    }
    if (dnsLength > 0) {
        memcpy(data, manifest->application.network_dns, dnsLength);
    }

    status = vafs_feature_add(vafs, &network->header);
    free(network);
    return status;
}

static size_t __capability_size(const struct chef_package_manifest_capability* capability)
{
    const struct __manifest_capability_serializer* serializer;

    serializer = __find_capability_serializer(capability->name);
    if (serializer != NULL) {
        return sizeof(struct chef_vafs_capability_header)
            + __safe_strlen(capability->name)
            + serializer->size(capability);
    }

    return sizeof(struct chef_vafs_capability_header) + __safe_strlen(capability->name);
}

static size_t __serialize_capability(const struct chef_package_manifest_capability* capability, char* buffer)
{
    const struct __manifest_capability_serializer* serializer;
    struct chef_vafs_capability_header*            header;
    size_t                                         nameLength;

    header = (struct chef_vafs_capability_header*)buffer;
    buffer += sizeof(struct chef_vafs_capability_header);

    nameLength = __safe_strlen(capability->name);
    serializer = __find_capability_serializer(capability->name);

    header->name_length = (uint8_t)nameLength;
    header->config_length = 0;

    if (nameLength > 0) {
        memcpy(buffer, capability->name, nameLength);
        buffer += nameLength;
    }

    if (serializer == NULL) {
        return sizeof(struct chef_vafs_capability_header) + nameLength;
    }

    header->config_length = (uint16_t)serializer->size(capability);
    return sizeof(struct chef_vafs_capability_header)
        + nameLength
        + serializer->serialize(capability, buffer);
}

static int __write_capabilities_metadata(struct VaFs* vafs, const struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_capabilities* feature;
    size_t                                         totalSize;
    size_t                                         i;
    char*                                          buffer;
    int                                            status;

    if (manifest->capabilities_count == 0) {
        return 0;
    }

    totalSize = sizeof(struct chef_vafs_feature_package_capabilities);
    for (i = 0; i < manifest->capabilities_count; i++) {
        totalSize += __capability_size(&manifest->capabilities[i]);
    }

    buffer = malloc(totalSize);
    if (buffer == NULL) {
        errno = ENOMEM;
        return -1;
    }

    feature = (struct chef_vafs_feature_package_capabilities*)buffer;
    memcpy(&feature->header.Guid, &g_capabilitiesGuid, sizeof(struct VaFsGuid));
    feature->header.Length = (uint32_t)totalSize;
    feature->capabilities_count = (uint32_t)manifest->capabilities_count;

    buffer += sizeof(struct chef_vafs_feature_package_capabilities);
    for (i = 0; i < manifest->capabilities_count; i++) {
        buffer += __serialize_capability(&manifest->capabilities[i], buffer);
    }

    status = vafs_feature_add(vafs, &feature->header);
    free(feature);
    return status;
}

static int __load_package_header(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_header* header;
    char*                                    data;
    int                                      status;

    status = vafs_feature_query(vafs, &g_headerGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    manifest->type = header->type;
    data = (char*)header + sizeof(struct chef_vafs_feature_package_header);

#define READ_MANIFEST_STRING(__FIELD, __TARGET) \
    if (__copy_manifest_string((char**)&manifest->__TARGET, data, header->__FIELD ## _length) != 0) { \
        return -1; \
    } \
    data += header->__FIELD ## _length;

    READ_MANIFEST_STRING(platform, platform)
    READ_MANIFEST_STRING(arch, architecture)
    READ_MANIFEST_STRING(package, name)
    READ_MANIFEST_STRING(base, base)
    READ_MANIFEST_STRING(summary, summary)
    READ_MANIFEST_STRING(description, description)
    READ_MANIFEST_STRING(homepage, homepage)
    READ_MANIFEST_STRING(license, license)
    READ_MANIFEST_STRING(eula, eula)
    READ_MANIFEST_STRING(maintainer, maintainer)
    READ_MANIFEST_STRING(maintainer_email, maintainer_email)

#undef READ_MANIFEST_STRING
    return 0;
}

static int __load_package_version(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_version* header;
    int                                       status;

    status = vafs_feature_query(vafs, &g_versionGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return status;
    }

    manifest->version.major = header->major;
    manifest->version.minor = header->minor;
    manifest->version.patch = header->patch;
    manifest->version.revision = header->revision;
    if (header->tag_length > 0) {
        manifest->version.tag = platform_strndup(
            (char*)header + sizeof(struct chef_vafs_feature_package_version),
            header->tag_length
        );
        if (manifest->version.tag == NULL) {
            errno = ENOMEM;
            return -1;
        }
    }
    return 0;
}

static int __load_icon_metadata(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_icon* header;
    size_t                                 iconSize;
    int                                    status;

    status = vafs_feature_query(vafs, &g_iconGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return 0;
    }

    iconSize = header->header.Length - sizeof(struct chef_vafs_feature_package_icon);
    manifest->icon.data = __duplicate_buffer(
        (char*)header + sizeof(struct chef_vafs_feature_package_icon),
        iconSize
    );
    if (iconSize > 0 && manifest->icon.data == NULL) {
        errno = ENOMEM;
        return -1;
    }
    manifest->icon.size = iconSize;
    return 0;
}

static int __load_commands_metadata(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_apps* header;
    char*                                  data;
    int                                    status;
    int                                    i;

    status = vafs_feature_query(vafs, &g_commandsGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return 0;
    }

    if (header->apps_count <= 0) {
        return 0;
    }

    manifest->commands = calloc((size_t)header->apps_count, sizeof(struct chef_package_manifest_command));
    if (manifest->commands == NULL) {
        errno = ENOMEM;
        return -1;
    }
    manifest->commands_count = (size_t)header->apps_count;

    data = (char*)header + sizeof(struct chef_vafs_feature_package_apps);
    for (i = 0; i < header->apps_count; i++) {
        struct chef_vafs_package_app* app = (struct chef_vafs_package_app*)data;

        data += sizeof(struct chef_vafs_package_app);
        manifest->commands[i].type = (enum chef_command_type)app->type;
        if (__copy_manifest_string((char**)&manifest->commands[i].name, data, app->name_length) != 0) {
            return -1;
        }
        data += app->name_length;
        if (__copy_manifest_string((char**)&manifest->commands[i].description, data, app->description_length) != 0) {
            return -1;
        }
        data += app->description_length;
        if (__copy_manifest_string((char**)&manifest->commands[i].arguments, data, app->arguments_length) != 0) {
            return -1;
        }
        data += app->arguments_length;
        if (__copy_manifest_string((char**)&manifest->commands[i].path, data, app->path_length) != 0) {
            return -1;
        }
        data += app->path_length;

        if (app->icon_length > 0) {
            manifest->commands[i].icon.data = __duplicate_buffer(data, app->icon_length);
            if (manifest->commands[i].icon.data == NULL) {
                errno = ENOMEM;
                return -1;
            }
            manifest->commands[i].icon.size = app->icon_length;
        }
        data += app->icon_length;
    }
    return 0;
}

static int __load_ingredient_options_metadata(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_ingredient_opts* header;
    char*                                     data;
    int                                       status;

    status = vafs_feature_query(vafs, &g_optionsGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return 0;
    }

    data = (char*)header + sizeof(struct chef_vafs_feature_ingredient_opts);
    if (__parse_csv_string_array(data, header->bin_dirs_length, &manifest->ingredient.bin_dirs) != 0) {
        return -1;
    }
    data += header->bin_dirs_length;
    if (__parse_csv_string_array(data, header->inc_dirs_length, &manifest->ingredient.inc_dirs) != 0) {
        return -1;
    }
    data += header->inc_dirs_length;
    if (__parse_csv_string_array(data, header->lib_dirs_length, &manifest->ingredient.lib_dirs) != 0) {
        return -1;
    }
    data += header->lib_dirs_length;
    if (__parse_csv_string_array(data, header->compiler_flags_length, &manifest->ingredient.compiler_flags) != 0) {
        return -1;
    }
    data += header->compiler_flags_length;
    if (__parse_csv_string_array(data, header->linker_flags_length, &manifest->ingredient.linker_flags) != 0) {
        return -1;
    }
    return 0;
}

static int __load_network_metadata(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_network* header;
    char*                                     data;
    int                                       status;

    status = vafs_feature_query(vafs, &g_networkGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return 0;
    }

    data = (char*)header + sizeof(struct chef_vafs_feature_package_network);
    if (__copy_manifest_string((char**)&manifest->application.network_gateway, data, header->gateway_length) != 0) {
        return -1;
    }
    data += header->gateway_length;
    if (__copy_manifest_string((char**)&manifest->application.network_dns, data, header->dns_length) != 0) {
        return -1;
    }
    return 0;
}

static int __parse_capability_allow_list(
    const char*                                data,
    size_t                                     length,
    struct chef_package_manifest_capability* capability)
{
    const char* itr = data;
    size_t      count = 0;

    while ((size_t)(itr - data) < length) {
        uint32_t entryLength;

        memcpy(&entryLength, itr, sizeof(uint32_t));
        itr += sizeof(uint32_t) + entryLength;
        count++;
    }

    capability->allow_list.values = calloc(count, sizeof(char*));
    if (capability->allow_list.values == NULL) {
        errno = ENOMEM;
        return -1;
    }
    capability->allow_list.count = count;
    capability->type = CHEF_PACKAGE_MANIFEST_CAPABILITY_ALLOW_LIST;

    itr = data;
    count = 0;
    while ((size_t)(itr - data) < length) {
        uint32_t entryLength;

        memcpy(&entryLength, itr, sizeof(uint32_t));
        itr += sizeof(uint32_t);
        ((char**)capability->allow_list.values)[count] = platform_strndup(itr, entryLength);
        if (((char**)capability->allow_list.values)[count] == NULL) {
            errno = ENOMEM;
            return -1;
        }
        itr += entryLength;
        count++;
    }
    return 0;
}

static int __load_capabilities_metadata(struct VaFs* vafs, struct chef_package_manifest* manifest)
{
    struct chef_vafs_feature_package_capabilities* header;
    char*                                          data;
    uint32_t                                       i;
    int                                            status;

    status = vafs_feature_query(vafs, &g_capabilitiesGuid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        return 0;
    }

    if (header->capabilities_count == 0) {
        return 0;
    }

    manifest->capabilities = calloc(header->capabilities_count, sizeof(struct chef_package_manifest_capability));
    if (manifest->capabilities == NULL) {
        errno = ENOMEM;
        return -1;
    }
    manifest->capabilities_count = header->capabilities_count;

    data = (char*)header + sizeof(struct chef_vafs_feature_package_capabilities);
    for (i = 0; i < header->capabilities_count; i++) {
        struct chef_vafs_capability_header* entry = (struct chef_vafs_capability_header*)data;
        const char*                         nameData;
        const char*                         configData;

        data += sizeof(struct chef_vafs_capability_header);
        nameData = data;
        data += entry->name_length;
        configData = data;
        data += entry->config_length;

        manifest->capabilities[i].name = platform_strndup(nameData, entry->name_length);
        if (manifest->capabilities[i].name == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (strcmp(manifest->capabilities[i].name, "network-client") == 0 && entry->config_length > 0) {
            if (__parse_capability_allow_list(configData, entry->config_length, &manifest->capabilities[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

int chef_package_manifest_parse_version(const char* string, struct chef_version* versionOut)
{
    char* pointer;
    char* pointerEnd;

    if (string == NULL || versionOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(versionOut, 0, sizeof(struct chef_version));
    pointer = (char*)string;
    pointerEnd = strchr(pointer, '.');
    if (pointerEnd == NULL) {
        versionOut->revision = (int)strtol(pointer, &pointerEnd, 10);
        if (versionOut->revision == 0 || (pointerEnd != NULL && *pointerEnd != '\0')) {
            errno = EINVAL;
            return -1;
        }
        return 0;
    }

    versionOut->major = (int)strtol(pointer, &pointerEnd, 10);
    if (pointerEnd == NULL || *pointerEnd != '.') {
        errno = EINVAL;
        return -1;
    }

    pointer = pointerEnd + 1;
    versionOut->minor = (int)strtol(pointer, &pointerEnd, 10);
    if (pointerEnd == NULL || *pointerEnd != '.') {
        errno = EINVAL;
        return -1;
    }

    pointer = pointerEnd + 1;
    versionOut->patch = (int)strtol(pointer, &pointerEnd, 10);
    if (pointerEnd == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (*pointerEnd == '+') {
        versionOut->tag = platform_strdup(pointerEnd);
        if (versionOut->tag == NULL) {
            errno = ENOMEM;
            return -1;
        }
    } else if (*pointerEnd != '\0') {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int chef_package_manifest_load_vafs(struct VaFs* vafs, struct chef_package_manifest** manifestOut)
{
    struct chef_package_manifest* manifest;
    int                           status;

    if (vafs == NULL || manifestOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    manifest = calloc(1, sizeof(struct chef_package_manifest));
    if (manifest == NULL) {
        errno = ENOMEM;
        return -1;
    }

    status = __load_package_header(vafs, manifest);
    if (status == 0) {
        status = __load_package_version(vafs, manifest);
    }
    if (status == 0) {
        status = __load_icon_metadata(vafs, manifest);
    }
    if (status == 0) {
        status = __load_commands_metadata(vafs, manifest);
    }
    if (status == 0) {
        status = __load_ingredient_options_metadata(vafs, manifest);
    }
    if (status == 0) {
        status = __load_network_metadata(vafs, manifest);
    }
    if (status == 0) {
        status = __load_capabilities_metadata(vafs, manifest);
    }
    if (status != 0) {
        __free_manifest_contents(manifest);
        free(manifest);
        return status;
    }

    *manifestOut = manifest;
    return 0;
}

int chef_package_manifest_load(const char* path, struct chef_package_manifest** manifestOut)
{
    struct VaFs* vafs;
    int          status;

    if (path == NULL || manifestOut == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = vafs_open_file(path, &vafs);
    if (status != 0) {
        return status;
    }

    status = chef_package_manifest_load_vafs(vafs, manifestOut);
    vafs_close(vafs);
    return status;
}

int chef_package_manifest_write(struct VaFs* vafs, const struct chef_package_manifest* manifest)
{
    int status;

    if (vafs == NULL || manifest == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = __write_header_metadata(vafs, manifest);
    if (status != 0) {
        return -1;
    }
    status = __write_version_metadata(vafs, &manifest->version);
    if (status != 0) {
        return -1;
    }
    status = __write_icon_metadata(vafs, &manifest->icon);
    if (status != 0) {
        return -1;
    }
    status = __write_ingredient_options_metadata(vafs, manifest);
    if (status != 0) {
        return -1;
    }
    status = __write_network_metadata(vafs, manifest);
    if (status != 0) {
        return -1;
    }
    status = __write_capabilities_metadata(vafs, manifest);
    if (status != 0) {
        return -1;
    }
    status = __write_commands_metadata(vafs, manifest);
    if (status != 0) {
        return -1;
    }
    return 0;
}

void chef_package_manifest_free(struct chef_package_manifest* manifest)
{
    if (manifest == NULL) {
        return;
    }

    __free_manifest_contents(manifest);
    free(manifest);
}