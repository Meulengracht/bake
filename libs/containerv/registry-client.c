/**
 * Copyright 2024, Philip Meulengracht
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

#define _GNU_SOURCE

#include "registry-client.h"
#include <chef/containerv.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <curl/curl.h>
#include <jansson.h>

// Registry client configuration
#define USER_AGENT "chef-containerv/1.0"
#define MAX_REDIRECTS 10
#define TIMEOUT_SECONDS 30

// Docker Hub defaults
#define DEFAULT_REGISTRY "registry-1.docker.io"
#define DEFAULT_NAMESPACE "library"
#define AUTH_URL "https://auth.docker.io/token"
#define REGISTRY_URL_PATTERN "https://%s/v2"

// HTTP response buffer
struct http_response {
    char*  data;
    size_t size;
    size_t capacity;
};

// Registry client state
struct registry_client {
    CURL*  curl;
    char*  registry_url;
    char*  auth_token;
    char*  username;
    char*  password;
    time_t token_expires;
};

// Progress tracking for downloads
struct download_progress {
    void (*callback)(const char* status, int percent, void* data);
    void* user_data;
    const char* layer_digest;
    uint64_t total_bytes;
    uint64_t downloaded_bytes;
    time_t start_time;
};

// Utility functions
static size_t write_response_callback(void* contents, size_t size, size_t nmemb, struct http_response* response) {
    size_t total_size = size * nmemb;
    
    // Ensure we have enough capacity
    if (response->size + total_size >= response->capacity) {
        size_t new_capacity = response->capacity * 2;
        if (new_capacity < response->size + total_size + 1) {
            new_capacity = response->size + total_size + 1024;
        }
        
        char* new_data = realloc(response->data, new_capacity);
        if (!new_data) {
            return 0; // Out of memory
        }
        
        response->data = new_data;
        response->capacity = new_capacity;
    }
    
    // Copy the data
    memcpy(response->data + response->size, contents, total_size);
    response->size += total_size;
    response->data[response->size] = '\0'; // Null terminate
    
    return total_size;
}

static int progress_callback(void* clientp, curl_off_t dltotal, curl_off_t dlnow, 
                           curl_off_t ultotal, curl_off_t ulnow) {
    struct download_progress* progress = (struct download_progress*)clientp;
    
    if (progress->callback && dltotal > 0) {
        int percent = (int)((dlnow * 100) / dltotal);
        
        char status[256];
        snprintf(status, sizeof(status), "Downloading %s: %lld/%lld bytes", 
                progress->layer_digest ? progress->layer_digest : "data",
                (long long)dlnow, (long long)dltotal);
        
        progress->callback(status, percent, progress->user_data);
    }
    
    return 0; // Continue
}

static char* build_registry_url(const char* registry) {
    if (!registry) registry = DEFAULT_REGISTRY;
    
    size_t url_len = strlen(REGISTRY_URL_PATTERN) + strlen(registry) + 1;
    char* url = malloc(url_len);
    if (!url) return NULL;
    
    snprintf(url, url_len, REGISTRY_URL_PATTERN, registry);
    return url;
}

static char* normalize_image_ref(const struct containerv_image_ref* ref, 
                               char** registry_out, char** namespace_out) {
    if (!ref || !ref->repository) return NULL;
    
    // Set defaults
    const char* registry = ref->registry ? ref->registry : DEFAULT_REGISTRY;
    const char* namespace = ref->namespace ? ref->namespace : DEFAULT_NAMESPACE;
    const char* tag = ref->tag ? ref->tag : "latest";
    
    // For Docker Hub, use index.docker.io for API calls but registry-1.docker.io for pulls
    if (strcmp(registry, "docker.io") == 0 || strcmp(registry, "index.docker.io") == 0) {
        registry = DEFAULT_REGISTRY;
    }
    
    if (registry_out) {
        *registry_out = strdup(registry);
    }
    if (namespace_out) {
        *namespace_out = strdup(namespace);
    }
    
    // Build repository name (namespace/repository for Docker Hub, just repository for others)
    char* repo_name;
    if (strcmp(registry, DEFAULT_REGISTRY) == 0 && namespace && strcmp(namespace, DEFAULT_NAMESPACE) != 0) {
        size_t len = strlen(namespace) + strlen(ref->repository) + 2;
        repo_name = malloc(len);
        if (repo_name) {
            snprintf(repo_name, len, "%s/%s", namespace, ref->repository);
        }
    } else {
        repo_name = strdup(ref->repository);
    }
    
    return repo_name;
}

// Registry client implementation
struct registry_client* registry_client_create(const char* registry, const char* username, const char* password) {
#ifndef HAVE_IMAGE_DEPENDENCIES
    // Return NULL if dependencies are not available
    return NULL;
#else
    struct registry_client* client = calloc(1, sizeof(struct registry_client));
    if (!client) return NULL;
    
    // Initialize curl
    client->curl = curl_easy_init();
    if (!client->curl) {
        free(client);
        return NULL;
    }
    
    // Set up basic curl options
    curl_easy_setopt(client->curl, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(client->curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(client->curl, CURLOPT_MAXREDIRS, MAX_REDIRECTS);
    curl_easy_setopt(client->curl, CURLOPT_TIMEOUT, TIMEOUT_SECONDS);
    curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(client->curl, CURLOPT_SSL_VERIFYHOST, 2L);
    
    // Store registry URL
    client->registry_url = build_registry_url(registry);
    if (!client->registry_url) {
        registry_client_destroy(client);
        return NULL;
    }
    
    // Store credentials if provided
    if (username) client->username = strdup(username);
    if (password) client->password = strdup(password);
    
    return client;
#endif
}

void registry_client_destroy(struct registry_client* client) {
    if (!client) return;
    
    if (client->curl) {
        curl_easy_cleanup(client->curl);
    }
    
    free(client->registry_url);
    free(client->auth_token);
    free(client->username);
    free(client->password);
    free(client);
}

static int authenticate_registry(struct registry_client* client, const char* scope) {
    // For Docker Hub, get authentication token
    if (strstr(client->registry_url, "registry-1.docker.io")) {
        struct http_response response = {0};
        response.capacity = 4096;
        response.data = malloc(response.capacity);
        if (!response.data) return -1;
        
        // Build auth URL
        char auth_url[1024];
        snprintf(auth_url, sizeof(auth_url), 
                "%s?service=registry.docker.io&scope=%s", AUTH_URL, scope);
        
        curl_easy_setopt(client->curl, CURLOPT_URL, auth_url);
        curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
        curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &response);
        
        // Set credentials if available
        if (client->username && client->password) {
            curl_easy_setopt(client->curl, CURLOPT_USERNAME, client->username);
            curl_easy_setopt(client->curl, CURLOPT_PASSWORD, client->password);
        }
        
        CURLcode res = curl_easy_perform(client->curl);
        
        if (res != CURLE_OK || !response.data) {
            free(response.data);
            return -1;
        }
        
        // Parse JSON response
        json_t* json = json_loads(response.data, 0, NULL);
        free(response.data);
        
        if (!json) return -1;

        json_t* token = json_object_get(json, "token");
        json_t* expires_in = json_object_get(json, "expires_in");
        
        if (token && json_is_string(token)) {
            free(client->auth_token);
            client->auth_token = strdup(json_string_value(token));
            
            // Set expiration time
            if (expires_in && json_is_number(expires_in)) {
                client->token_expires = time(NULL) + json_integer_value(expires_in) - 60; // 1 minute buffer
            } else {
                client->token_expires = time(NULL) + 3600; // 1 hour default
            }
        }
        
        json_decref(json);

        return client->auth_token ? 0 : -1;
    }
    
    return 0; // No authentication needed for other registries (for now)
}

static int set_auth_headers(struct registry_client* client) {
    if (client->auth_token && time(NULL) < client->token_expires) {
        char auth_header[1024];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", client->auth_token);
        
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, auth_header);
        headers = curl_slist_append(headers, "Accept: application/vnd.docker.distribution.manifest.v2+json");
        headers = curl_slist_append(headers, "Accept: application/vnd.oci.image.manifest.v1+json");
        
        curl_easy_setopt(client->curl, CURLOPT_HTTPHEADER, headers);
        return 0;
    }
    
    return -1;
}

int registry_get_manifest(struct registry_client* client, 
                         const struct containerv_image_ref* image_ref,
                         char** manifest_json) {
    if (!client || !image_ref || !manifest_json) return -1;
    
    char* registry = NULL;
    char* namespace = NULL;
    char* repo_name = normalize_image_ref(image_ref, &registry, &namespace);
    if (!repo_name) return -1;
    
    // Build scope for authentication
    char scope[512];
    snprintf(scope, sizeof(scope), "repository:%s:pull", repo_name);
    
    // Authenticate if needed
    if (authenticate_registry(client, scope) != 0) {
        free(repo_name);
        free(registry);
        free(namespace);
        return -1;
    }
    
    // Build manifest URL
    const char* reference = image_ref->digest ? image_ref->digest : 
                          (image_ref->tag ? image_ref->tag : "latest");
    
    char manifest_url[1024];
    snprintf(manifest_url, sizeof(manifest_url), 
            "%s/%s/manifests/%s", client->registry_url, repo_name, reference);
    
    // Set up request
    struct http_response response = {0};
    response.capacity = 8192;
    response.data = malloc(response.capacity);
    if (!response.data) {
        free(repo_name);
        free(registry);
        free(namespace);
        return -1;
    }
    
    curl_easy_setopt(client->curl, CURLOPT_URL, manifest_url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, write_response_callback);
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, &response);
    
    // Set authentication headers
    set_auth_headers(client);
    
    CURLcode res = curl_easy_perform(client->curl);
    long response_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    free(repo_name);
    free(registry);
    free(namespace);
    
    if (res != CURLE_OK || response_code != 200) {
        free(response.data);
        return -1;
    }
    
    *manifest_json = response.data; // Transfer ownership
    return 0;
}

int registry_download_blob(struct registry_client* client,
                          const struct containerv_image_ref* image_ref,
                          const char* digest,
                          const char* output_path,
                          void (*progress_callback)(const char* status, int percent, void* data),
                          void* callback_data) {
    if (!client || !image_ref || !digest || !output_path) return -1;
    
    char* registry = NULL;
    char* namespace = NULL;
    char* repo_name = normalize_image_ref(image_ref, &registry, &namespace);
    if (!repo_name) return -1;
    
    // Build blob URL
    char blob_url[1024];
    snprintf(blob_url, sizeof(blob_url), 
            "%s/%s/blobs/%s", client->registry_url, repo_name, digest);
    
    // Open output file
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        free(repo_name);
        free(registry);
        free(namespace);
        return -1;
    }
    
    // Set up progress tracking
    struct download_progress progress = {
        .callback = progress_callback,
        .user_data = callback_data,
        .layer_digest = digest,
        .total_bytes = 0,
        .downloaded_bytes = 0,
        .start_time = time(NULL)
    };
    
    curl_easy_setopt(client->curl, CURLOPT_URL, blob_url);
    curl_easy_setopt(client->curl, CURLOPT_WRITEFUNCTION, NULL); // Use default (fwrite)
    curl_easy_setopt(client->curl, CURLOPT_WRITEDATA, output_file);
    curl_easy_setopt(client->curl, CURLOPT_PROGRESSFUNCTION, progress_callback);
    curl_easy_setopt(client->curl, CURLOPT_PROGRESSDATA, &progress);
    curl_easy_setopt(client->curl, CURLOPT_NOPROGRESS, 0L);
    
    // Set authentication headers
    set_auth_headers(client);
    
    CURLcode res = curl_easy_perform(client->curl);
    long response_code = 0;
    curl_easy_getinfo(client->curl, CURLINFO_RESPONSE_CODE, &response_code);
    
    fclose(output_file);
    free(repo_name);
    free(registry);
    free(namespace);
    
    if (res != CURLE_OK || response_code != 200) {
        unlink(output_path); // Remove partial file
        return -1;
    }
    
    return 0;
}