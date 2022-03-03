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
#include <curl/curl.h>
#include <errno.h>
#include <jansson.h>
#include "private.h"
#include <string.h>

static int __get_info_url(struct chef_info_params* params, char* urlBuffer, size_t bufferSize)
{
    int written = snprintf(urlBuffer, bufferSize - 1, 
        "https://chef-api.azurewebsites.net/api/PackInfo?publisher=%s&name=%s",
        params->publisher, params->package
    );
    urlBuffer[written] = '\0';
    return written == bufferSize - 1 ? -1 : 0;
}

static int __parse_package_info_response(const char* response, struct chef_package** packageOut)
{
    struct chef_package* package;
    json_error_t         error;
    json_t*              root;
    json_t*              channels;

    printf("__parse_package_info_response: %s\n", response);

    root = json_loads(response, 0, &error);
    if (!root) {
        return -1;
    }

    // allocate memory for the package
    package = (struct chef_package*)malloc(sizeof(struct chef_package));
    if (!package) {
        return -1;
    }
    memset(package, 0, sizeof(struct chef_package));

    // parse the package
    package->publisher = json_string_value(json_object_get(root, "publisher"));
    package->package = json_string_value(json_object_get(root, "name"));
    package->description = json_string_value(json_object_get(root, "description"));
    package->homepage = json_string_value(json_object_get(root, "homepage"));
    package->license = json_string_value(json_object_get(root, "license"));
    package->maintainer = json_string_value(json_object_get(root, "maintainer"));
    package->maintainer_email = json_string_value(json_object_get(root, "maintainer_email"));

    // parse the channels
    channels = json_object_get(root, "channels");
    if (channels) {
        size_t i;
        size_t channels_count = json_array_size(channels);
        package->channels = (struct chef_channel*)malloc(sizeof(struct chef_channel) * channels_count);
        if (!package->channels) {
            return -1;
        }
        memset(package->channels, 0, sizeof(struct chef_channel) * channels_count);
        package->channels_count = channels_count;

        for (i = 0; i < channels_count; i++) {
            json_t* channel = json_array_get(channels, i);
            json_t* version = json_object_get(channel, "version");
            json_t* version_major = json_object_get(version, "major");
            json_t* version_minor = json_object_get(version, "minor");
            json_t* version_revision = json_object_get(version, "revision");
            json_t* version_tag = json_object_get(version, "tag");

            package->channels[i].name = json_string_value(json_object_get(channel, "name"));
            package->channels[i].current_version.major = json_integer_value(version_major);
            package->channels[i].current_version.minor = json_integer_value(version_minor);
            package->channels[i].current_version.revision = json_integer_value(version_revision);
            package->channels[i].current_version.tag = json_string_value(version_tag);
        }
    }
    return 0;
}

int chefclient_pack_info(struct chef_info_params* params, struct chef_package** packageOut)
{
    CURL*    curl;
    CURLcode code;
    size_t   dataIndex = 0;
    char     buffer[256];
    int      status = -1;
    long     httpCode;

    // initialize a curl session
    curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "chefclient_pack_info: curl_easy_init() failed\n");
        return -1;
    }
    chef_set_curl_common(curl, NULL, 1, 1, 0);

    // set the url
    if (__get_info_url(params, buffer, sizeof(buffer)) != 0) {
        fprintf(stderr, "chefclient_pack_info: buffer too small for package info link\n");
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_URL, &buffer[0]);
    if (code != CURLE_OK) {
        fprintf(stderr, "chefclient_pack_info: failed to set url [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_setopt(curl, CURLOPT_WRITEDATA, &dataIndex);
    if(code != CURLE_OK) {
        fprintf(stderr, "chefclient_pack_info: failed to set write data [%s]\n", chef_error_buffer());
        goto cleanup;
    }

    code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        fprintf(stderr, "chefclient_pack_info: curl_easy_perform() failed: %s\n", curl_easy_strerror(code));
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
        fprintf(stderr, "chefclient_pack_info: curl_easy_perform() failed: %ld\n", httpCode);
        goto cleanup;
    }

    status = __parse_package_info_response(chef_response_buffer(), packageOut);

cleanup:
    curl_easy_cleanup(curl);
    return status;
}
