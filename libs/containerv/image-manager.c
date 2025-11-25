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

#include <chef/containerv.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#define PATH_SEPARATOR "\\"
#define DEFAULT_CACHE_BASE "C:\\ProgramData\\chef\\images"
#else
#include <sys/statvfs.h>
#define PATH_SEPARATOR "/"
#define DEFAULT_CACHE_BASE "/var/lib/chef/images"
#endif

// Global image system state
struct image_system_state {
    char*  cache_dir;
    char*  blobs_dir;
    char*  repos_dir;
    char*  cache_layers_dir;
    char*  temp_dir;
    bool   initialized;
};

static struct image_system_state g_image_system = {0};

// Internal image management structures
struct image_manifest {
    int     schema_version;
    char*   media_type;
    struct containerv_layer config;
    struct containerv_layer* layers;
    int     layer_count;
    char*   os;
    char*   architecture;
};

struct image_config {
    char*   author;
    char*   comment;
    char**  env;
    int     env_count;
    char**  cmd;
    int     cmd_count;
    char**  entrypoint;
    int     entrypoint_count;
    char*   working_dir;
    char*   user;
    time_t  created;
};

// Utility functions
static char* path_join(const char* base, const char* path) {
    size_t base_len = strlen(base);
    size_t path_len = strlen(path);
    size_t total_len = base_len + path_len + 2; // +1 for separator, +1 for null
    
    char* result = malloc(total_len);
    if (!result) return NULL;
    
    snprintf(result, total_len, "%s%s%s", base, PATH_SEPARATOR, path);
    return result;
}

static int ensure_directory(const char* path) {
#ifdef _WIN32
    return CreateDirectoryA(path, NULL) || GetLastError() == ERROR_ALREADY_EXISTS ? 0 : -1;
#else
    return mkdir(path, 0755) == 0 || errno == EEXIST ? 0 : -1;
#endif
}

static int ensure_directory_recursive(const char* path) {
    char* temp = strdup(path);
    if (!temp) return -1;
    
    for (char* p = temp + 1; *p; p++) {
        if (*p == '/' || *p == '\\') {
            *p = '\0';
            if (ensure_directory(temp) != 0 && errno != EEXIST) {
                free(temp);
                return -1;
            }
            *p = PATH_SEPARATOR[0];
        }
    }
    
    int result = ensure_directory(temp);
    free(temp);
    return result;
}

static char* get_default_cache_dir(void) {
#ifdef _WIN32
    return strdup(DEFAULT_CACHE_BASE);
#else
    // Check if running as root or user
    if (getuid() == 0) {
        return strdup(DEFAULT_CACHE_BASE);
    } else {
        const char* home = getenv("HOME");
        if (!home) home = "/tmp";
        
        char* user_cache = malloc(strlen(home) + 64);
        if (!user_cache) return NULL;
        
        snprintf(user_cache, strlen(home) + 64, "%s/.local/share/chef/images", home);
        return user_cache;
    }
#endif
}

static uint64_t get_available_disk_space(const char* path) {
#ifdef _WIN32
    ULARGE_INTEGER free_bytes;
    if (GetDiskFreeSpaceExA(path, &free_bytes, NULL, NULL)) {
        return free_bytes.QuadPart;
    }
    return 0;
#else
    struct statvfs stat;
    if (statvfs(path, &stat) == 0) {
        return (uint64_t)stat.f_bavail * stat.f_frsize;
    }
    return 0;
#endif
}

// Image reference helper functions
static char* image_ref_to_string(const struct containerv_image_ref* ref) {
    if (!ref || !ref->repository) return NULL;
    
    // Calculate required buffer size
    size_t size = strlen(ref->repository) + 1;
    if (ref->registry) size += strlen(ref->registry) + 1;
    if (ref->namespace) size += strlen(ref->namespace) + 1;
    if (ref->digest) {
        size += strlen(ref->digest) + 1; // @digest
    } else if (ref->tag) {
        size += strlen(ref->tag) + 1; // :tag
    } else {
        size += 8; // :latest
    }
    
    char* result = malloc(size);
    if (!result) return NULL;
    
    // Build the string
    result[0] = '\0';
    
    if (ref->registry) {
        strcat(result, ref->registry);
        strcat(result, "/");
    }
    
    if (ref->namespace) {
        strcat(result, ref->namespace);
        strcat(result, "/");
    }
    
    strcat(result, ref->repository);
    
    if (ref->digest) {
        strcat(result, "@");
        strcat(result, ref->digest);
    } else if (ref->tag) {
        strcat(result, ":");
        strcat(result, ref->tag);
    } else {
        strcat(result, ":latest");
    }
    
    return result;
}

static int parse_image_ref(const char* ref_str, struct containerv_image_ref* ref) {
    if (!ref_str || !ref) return -1;
    
    memset(ref, 0, sizeof(*ref));
    
    char* temp = strdup(ref_str);
    if (!temp) return -1;
    
    char* remaining = temp;
    char* registry_part = NULL;
    char* digest_part = NULL;
    char* tag_part = NULL;
    
    // Check for digest (@sha256:...)
    char* at_pos = strrchr(remaining, '@');
    if (at_pos) {
        *at_pos = '\0';
        digest_part = at_pos + 1;
    }
    
    // Check for tag (:tag) if no digest
    if (!digest_part) {
        char* colon_pos = strrchr(remaining, ':');
        if (colon_pos) {
            // Make sure this isn't a registry port (has slash after colon)
            char* slash_pos = strchr(colon_pos, '/');
            if (!slash_pos) {
                *colon_pos = '\0';
                tag_part = colon_pos + 1;
            }
        }
    }
    
    // Count slashes to determine registry/namespace/repository structure
    int slash_count = 0;
    for (char* p = remaining; *p; p++) {
        if (*p == '/') slash_count++;
    }
    
    if (slash_count >= 2) {
        // registry/namespace/repository
        char* first_slash = strchr(remaining, '/');
        *first_slash = '\0';
        registry_part = remaining;
        remaining = first_slash + 1;
        
        char* second_slash = strchr(remaining, '/');
        *second_slash = '\0';
        ref->namespace = strdup(remaining);
        ref->repository = strdup(second_slash + 1);
    } else if (slash_count == 1) {
        // Check if first part looks like registry (contains . or :)
        char* slash_pos = strchr(remaining, '/');
        *slash_pos = '\0';
        
        if (strchr(remaining, '.') || strchr(remaining, ':')) {
            // registry/repository
            registry_part = remaining;
            ref->repository = strdup(slash_pos + 1);
        } else {
            // namespace/repository (default registry)
            ref->namespace = strdup(remaining);
            ref->repository = strdup(slash_pos + 1);
        }
    } else {
        // Just repository name
        ref->repository = strdup(remaining);
    }
    
    // Set optional parts
    if (registry_part) {
        ref->registry = strdup(registry_part);
    }
    if (tag_part) {
        ref->tag = strdup(tag_part);
    }
    if (digest_part) {
        ref->digest = strdup(digest_part);
    }
    
    free(temp);
    return 0;
}

static void free_image_ref(struct containerv_image_ref* ref) {
    if (!ref) return;
    
    free(ref->registry);
    free(ref->namespace);
    free(ref->repository);
    free(ref->tag);
    free(ref->digest);
    memset(ref, 0, sizeof(*ref));
}

// Image system initialization and cleanup
int containerv_images_init(const char* cache_dir) {
    if (g_image_system.initialized) {
        return 0; // Already initialized
    }
    
    // Set cache directory
    if (cache_dir) {
        g_image_system.cache_dir = strdup(cache_dir);
    } else {
        g_image_system.cache_dir = get_default_cache_dir();
    }
    
    if (!g_image_system.cache_dir) {
        return -1;
    }
    
    // Create subdirectories
    g_image_system.blobs_dir = path_join(g_image_system.cache_dir, "blobs");
    g_image_system.repos_dir = path_join(g_image_system.cache_dir, "repositories");
    g_image_system.cache_layers_dir = path_join(g_image_system.cache_dir, "cache");
    g_image_system.temp_dir = path_join(g_image_system.cache_dir, "tmp");
    
    if (!g_image_system.blobs_dir || !g_image_system.repos_dir || 
        !g_image_system.cache_layers_dir || !g_image_system.temp_dir) {
        containerv_images_cleanup();
        return -1;
    }
    
    // Ensure all directories exist
    if (ensure_directory_recursive(g_image_system.cache_dir) != 0 ||
        ensure_directory_recursive(g_image_system.blobs_dir) != 0 ||
        ensure_directory_recursive(g_image_system.repos_dir) != 0 ||
        ensure_directory_recursive(g_image_system.cache_layers_dir) != 0 ||
        ensure_directory_recursive(g_image_system.temp_dir) != 0) {
        containerv_images_cleanup();
        return -1;
    }
    
    // Create blobs/sha256 subdirectory
    char* sha256_dir = path_join(g_image_system.blobs_dir, "sha256");
    if (!sha256_dir || ensure_directory_recursive(sha256_dir) != 0) {
        free(sha256_dir);
        containerv_images_cleanup();
        return -1;
    }
    free(sha256_dir);
    
    g_image_system.initialized = true;
    return 0;
}

void containerv_images_cleanup(void) {
    if (g_image_system.cache_dir) {
        free(g_image_system.cache_dir);
        g_image_system.cache_dir = NULL;
    }
    if (g_image_system.blobs_dir) {
        free(g_image_system.blobs_dir);
        g_image_system.blobs_dir = NULL;
    }
    if (g_image_system.repos_dir) {
        free(g_image_system.repos_dir);
        g_image_system.repos_dir = NULL;
    }
    if (g_image_system.cache_layers_dir) {
        free(g_image_system.cache_layers_dir);
        g_image_system.cache_layers_dir = NULL;
    }
    if (g_image_system.temp_dir) {
        free(g_image_system.temp_dir);
        g_image_system.temp_dir = NULL;
    }
    
    g_image_system.initialized = false;
}

// Placeholder implementations for image operations
int containerv_image_pull(
    const struct containerv_image_ref* image_ref,
    void (*progress_callback)(const char* status, int percent, void* data),
    void* callback_data
) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    if (!image_ref || !image_ref->repository) {
        return -1;
    }
    
#ifdef HAVE_IMAGE_DEPENDENCIES
    // TODO: Implement actual registry communication with curl/cjson
    // For now, create a placeholder implementation
    
    if (progress_callback) {
        progress_callback("Pulling image manifest", 10, callback_data);
    }
    
    // Simulate some work
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif
    
    if (progress_callback) {
        progress_callback("Downloading layers", 50, callback_data);
    }
    
#ifdef _WIN32
    Sleep(200);
#else
    usleep(200000);
#endif
    
    if (progress_callback) {
        progress_callback("Extracting layers", 90, callback_data);
    }
    
#ifdef _WIN32
    Sleep(100);
#else
    usleep(100000);
#endif
    
    if (progress_callback) {
        progress_callback("Pull complete", 100, callback_data);
    }
    
    return 0; // Success for now
#else
    // Dependencies not available - return error
    if (progress_callback) {
        progress_callback("Image management dependencies not available", 0, callback_data);
    }
    return -1;
#endif
}

int containerv_image_list(
    struct containerv_image* images,
    int max_images
) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    if (!images || max_images <= 0) {
        return -1;
    }
    
    // TODO: Implement actual image enumeration
    // For now, return empty list
    
    return 0; // No images found
}

int containerv_image_inspect(
    const struct containerv_image_ref* image_ref,
    struct containerv_image* image
) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    if (!image_ref || !image_ref->repository || !image) {
        return -1;
    }
    
    // TODO: Implement actual image inspection
    // For now, return not found
    
    return -1; // Image not found
}

int containerv_image_remove(
    const struct containerv_image_ref* image_ref,
    bool force
) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    if (!image_ref || !image_ref->repository) {
        return -1;
    }
    
    // TODO: Implement actual image removal
    // For now, return success
    
    return 0;
}

int containerv_create_from_image(
    const struct containerv_image_ref* image_ref,
    struct containerv_options*         options,
    struct containerv_container**      container_out
) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    if (!image_ref || !image_ref->repository || !options || !container_out) {
        return -1;
    }
    
    // TODO: Implement image mounting and container creation
    // For now, this would need to:
    // 1. Check if image exists locally, pull if needed
    // 2. Extract/mount image layers
    // 3. Create container with mounted rootfs
    // 4. Store image reference in container metadata
    
    return -1; // Not implemented yet
}

void containerv_options_set_image(
    struct containerv_options*         options,
    const struct containerv_image_ref* image_ref
) {
    if (!options || !image_ref) return;
    
    // TODO: Store image reference in options structure
    // This would require extending the containerv_options structure
    // to include an image_ref field
}

int containerv_get_image(
    struct containerv_container*       container,
    struct containerv_image_ref*       image_ref
) {
    if (!container || !image_ref) {
        return -1;
    }
    
    // TODO: Retrieve image reference from container metadata
    // For now, return not found
    
    return -1;
}

// Cache management functions
int containerv_cache_get_stats(struct containerv_cache_stats* stats) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    if (!stats) return -1;
    
    memset(stats, 0, sizeof(*stats));
    
    // Get available disk space
    stats->available_space = get_available_disk_space(g_image_system.cache_dir);
    
    // TODO: Calculate actual cache statistics
    // - Walk through blobs directory to get total size and count
    // - Count repository entries for image count
    // - Get last GC time from metadata file
    
    stats->total_size = 0;
    stats->image_count = 0;
    stats->layer_count = 0;
    stats->last_gc = 0;
    
    return 0;
}

int containerv_cache_gc(bool force) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    // TODO: Implement garbage collection
    // - Remove unused layers (not referenced by any image)
    // - Remove old temporary files
    // - Update last GC timestamp
    
    return 0; // No items cleaned up yet
}

int containerv_cache_prune(int max_age_days) {
    if (!g_image_system.initialized) {
        if (containerv_images_init(NULL) != 0) return -1;
    }
    
    // TODO: Implement cache pruning
    // - Remove images not used by any container
    // - Remove layers older than max_age_days
    // - Update cache statistics
    
    return 0; // No items pruned yet
}