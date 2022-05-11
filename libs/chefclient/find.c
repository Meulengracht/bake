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

#include <chef/client.h>
#include <chef/api/package.h>
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include "private.h"
#include <string.h>

static const char* __get_json_string_safe(json_t* object, const char* key)
{
    json_t* value = json_object_get(object, key);
    if (value != NULL && json_string_value(value) != NULL) {
        return strdup(json_string_value(value));
    }
    return strdup("<not set>");
}

static int __get_find_url(struct chef_find_params* params, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/pack/find?search=%s",
        params->query
    );
    return written < (bufferSize - 1) ? 0 : -1;
}

static int __parse_channels(json_t* channels, struct chef_architecture* architecture)
{
    size_t i;
    size_t channelsCount = json_array_size(channels);
    
    architecture->channels_count = channelsCount;
    architecture->channels = (struct chef_channel*)malloc(sizeof(struct chef_channel) * channelsCount);
    if (architecture->channels == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (i = 0; i < channelsCount; i++) {
        json_t* channel = json_array_get(channels, i);
        json_t* version = json_object_get(channel, "current-version");
        json_t* version_major = json_object_get(version, "major");
        json_t* version_minor = json_object_get(version, "minor");
        json_t* version_patch = json_object_get(version, "patch");
        json_t* version_revision = json_object_get(version, "revision");
        json_t* size_revision = json_object_get(version, "size");

        // transfer members of architecture
        architecture->channels[i].name = __get_json_string_safe(channel, "name");
        architecture->channels[i].current_version.major = json_integer_value(version_major);
        architecture->channels[i].current_version.minor = json_integer_value(version_minor);
        architecture->channels[i].current_version.patch = json_integer_value(version_patch);
        architecture->channels[i].current_version.revision = json_integer_value(version_revision);
        architecture->channels[i].current_version.tag = __get_json_string_safe(version, "additional");
        architecture->channels[i].current_version.size = json_integer_value(size_revision);
        architecture->channels[i].current_version.created = __get_json_string_safe(version, "created");
    }
    return 0;
}

static int __parse_architectures(json_t* architectures, struct chef_platform* platform)
{
    size_t i;
    size_t architecturesCount = json_array_size(architectures);
    
    platform->architectures_count = architecturesCount;
    platform->architectures = (struct chef_architecture*)malloc(sizeof(struct chef_architecture) * architecturesCount);
    if (platform->architectures == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (i = 0; i < architecturesCount; i++)
    {
        json_t* architecture = json_array_get(architectures, i);
        json_t* channels;

        // transfer members of architecture
        platform->architectures[i].name = __get_json_string_safe(architecture, "name");

        // parse channels
        channels = json_object_get(architecture, "channels");
        if (channels != NULL) {
            if (__parse_channels(channels, &platform->architectures[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int __parse_platforms(json_t* platforms, struct chef_package* package)
{
    size_t i;
    size_t platformsCount = json_array_size(platforms);
    
    package->platforms_count = platformsCount;
    package->platforms = (struct chef_platform*)malloc(sizeof(struct chef_platform) * platformsCount);
    if (package->platforms == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (i = 0; i < platformsCount; i++)
    {
        json_t* platform = json_array_get(platforms, i);
        json_t* architectures;

        // transfer members of platform
        package->platforms[i].name = __get_json_string_safe(platform, "name");

        // parse architecures
        architectures = json_object_get(platform, "architectures");
        if (architectures != NULL) {
            if (__parse_architectures(architectures, &package->platforms[i]) != 0) {
                return -1;
            }
        }
    }
    return 0;
}

static int __parse_package(json_t* root, struct chef_package** packageOut)
{
    struct chef_package* package;
    json_t*              platforms;

    // allocate memory for the package
    package = (struct chef_package*)malloc(sizeof(struct chef_package));
    if (!package) {
        return -1;
    }
    memset(package, 0, sizeof(struct chef_package));

    // parse the required members
    package->publisher = __get_json_string_safe(root, "publisher");
    package->package = __get_json_string_safe(root, "name");
    package->summary = __get_json_string_safe(root, "summary");
    package->description = __get_json_string_safe(root, "description");
    package->homepage = __get_json_string_safe(root, "homepage");
    package->license = __get_json_string_safe(root, "license");
    package->eula = __get_json_string_safe(root, "eula");
    package->maintainer = __get_json_string_safe(root, "maintainer");
    package->maintainer_email = __get_json_string_safe(root, "maintainer_email");

    // parse the platforms
    platforms = json_object_get(root, "platforms");
    if (platforms != NULL) {
        if (__parse_platforms(platforms, package) != 0) {
            chef_package_free(package);
            return -1;
        }
    }

    *packageOut = package;

    json_decref(root);
    return 0;
}

static int __parse_package_find_response(const char* response, struct chef_package*** packagesOut, int* countOut)
{
    json_error_t          error;
    json_t*               root;
    size_t                i;
    struct chef_package** packages;
    size_t                packageCount;

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    // root is a list of packages
    packageCount = json_array_size(root);
    if (!packageCount) {
        json_decref(root);
        *packagesOut = NULL;
        *countOut = 0;
        return 0;
    }

    packages = (struct chef_package**)calloc(packageCount, sizeof(struct chef_package*));
    if (packages == NULL) {
        return -1;
    }

    for (i = 0; i < packageCount; i++) {
        json_t* package = json_array_get(root, i);
        if (__parse_package(package, &packages[i]) != 0) {
            return -1;
        }
    }

    json_decref(root);
    *packagesOut = packages;
    *countOut = (int)packageCount;
    return 0;
}

int chefclient_pack_find(struct chef_find_params* params, struct chef_package*** packagesOut, int* countOut)
{
    struct chef_request* request;
    CURLcode             code;
    char                 buffer[256];
    int                  status = -1;
    long                 httpCode;

    request = chef_request_new(1, params->privileged);
    if (!request) {
        fprintf(stderr, "chefclient_pack_find: failed to create request\n");
        return -1;
    }

    // set the url
    if (__get_find_url(params, buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "chefclient_pack_find: buffer too small for package info link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(request->curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "chefclient_pack_find: failed to set url [%s]\n", request->error);
        goto cleanup;
    }
    
    code = curl_easy_perform(request->curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "chefclient_pack_find: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        status = -1;
        
        if (httpCode == 404) {
            fprintf(stderr, "chefclient_pack_find: package not found\n");
            errno = ENOENT;
        }
        else {
            fprintf(stderr, "chefclient_pack_find: http error %ld [%s]\n", httpCode, request->response);
            errno = EIO;
        }
        goto cleanup;
    }

    status = __parse_package_find_response(request->response, packagesOut, countOut);

cleanup:
    chef_request_delete(request);
    return status;
}
