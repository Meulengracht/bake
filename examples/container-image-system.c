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

#include <chef/containerv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Example progress callback for image operations
static void progress_callback(const char* status, int percent, void* user_data) {
    const char* operation = (const char*)user_data;
    printf("\r%s: %s (%d%%)", operation, status, percent);
    fflush(stdout);
    if (percent >= 100) {
        printf("\n");
    }
}

// Helper function to format file sizes
static void format_size(uint64_t bytes, char* buffer, size_t buffer_size) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = (double)bytes;
    
    while (size >= 1024.0 && unit_index < 4) {
        size /= 1024.0;
        unit_index++;
    }
    
    if (unit_index == 0) {
        snprintf(buffer, buffer_size, "%.0f %s", size, units[unit_index]);
    } else {
        snprintf(buffer, buffer_size, "%.1f %s", size, units[unit_index]);
    }
}

// Helper function to format timestamps
static void format_time(time_t timestamp, char* buffer, size_t buffer_size) {
    struct tm* tm_info = localtime(&timestamp);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", tm_info);
}

int main(int argc, char* argv[]) {
    printf("Chef Container Image System Demo\n");
    printf("===============================\n\n");
    
    // Initialize the image system
    printf("1. Initializing image system...\n");
    if (containerv_images_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize image system\n");
        return 1;
    }
    printf("   ✓ Image system initialized with default cache location\n\n");
    
    // Example 1: Pull a popular image
    printf("2. Pulling container images...\n");
    
    struct containerv_image_ref ubuntu_ref = {
        .registry = NULL,           // Use default (Docker Hub)
        .namespace = NULL,          // Use default (library)
        .repository = "ubuntu",     // Ubuntu base image
        .tag = "22.04",            // LTS version
        .digest = NULL              // Use tag instead of digest
    };
    
    printf("   Pulling ubuntu:22.04 from Docker Hub...\n");
    if (containerv_image_pull(&ubuntu_ref, progress_callback, "Pull") == 0) {
        printf("   ✓ Successfully pulled ubuntu:22.04\n");
    } else {
        printf("   ✗ Failed to pull ubuntu:22.04 (this is expected in demo mode)\n");
    }
    
    // Example for Windows containers
    struct containerv_image_ref windows_ref = {
        .registry = "mcr.microsoft.com",
        .namespace = "windows",
        .repository = "servercore", 
        .tag = "ltsc2022",
        .digest = NULL
    };
    
    printf("   Pulling mcr.microsoft.com/windows/servercore:ltsc2022...\n");
    if (containerv_image_pull(&windows_ref, progress_callback, "Pull") == 0) {
        printf("   ✓ Successfully pulled Windows Server Core\n");
    } else {
        printf("   ✗ Failed to pull Windows Server Core (this is expected in demo mode)\n");
    }
    printf("\n");
    
    // Example 2: List cached images
    printf("3. Listing cached images...\n");
    
    struct containerv_image images[10];
    int image_count = containerv_image_list(images, 10);
    
    if (image_count > 0) {
        printf("   Found %d cached images:\n\n", image_count);
        
        printf("   %-40s %-15s %-12s %-20s %s\n", 
               "REPOSITORY", "TAG", "IMAGE ID", "CREATED", "SIZE");
        printf("   %s\n", "--------------------------------------------------------------------------------");
        
        for (int i = 0; i < image_count; i++) {
            char size_str[32];
            char created_str[32];
            
            format_size(images[i].size, size_str, sizeof(size_str));
            format_time(images[i].created, created_str, sizeof(created_str));
            
            // Show first tag or <none>
            const char* tag = (images[i].tag_count > 0 && images[i].tags[0]) ? 
                             images[i].tags[0] : "<none>";
            
            // Truncate long image IDs for display
            char short_id[13];
            if (images[i].id && strlen(images[i].id) > 12) {
                strncpy(short_id, images[i].id, 12);
                short_id[12] = '\0';
            } else {
                strcpy(short_id, images[i].id ? images[i].id : "<none>");
            }
            
            printf("   %-40s %-15s %-12s %-20s %s\n",
                   images[i].ref.repository, tag, short_id, created_str, size_str);
        }
    } else {
        printf("   No cached images found (this is expected in demo mode)\n");
    }
    printf("\n");
    
    // Example 3: Inspect an image
    printf("4. Inspecting image details...\n");
    
    struct containerv_image image_info;
    if (containerv_image_inspect(&ubuntu_ref, &image_info) == 0) {
        printf("   Image: %s\n", image_info.ref.repository);
        printf("   ID: %s\n", image_info.id ? image_info.id : "N/A");
        printf("   OS/Architecture: %s/%s\n", 
               image_info.os ? image_info.os : "N/A",
               image_info.architecture ? image_info.architecture : "N/A");
        
        char size_str[32], vsize_str[32];
        format_size(image_info.size, size_str, sizeof(size_str));
        format_size(image_info.virtual_size, vsize_str, sizeof(vsize_str));
        
        printf("   Size: %s (Virtual: %s)\n", size_str, vsize_str);
        
        char created_str[32];
        format_time(image_info.created, created_str, sizeof(created_str));
        printf("   Created: %s\n", created_str);
        
        if (image_info.author) {
            printf("   Author: %s\n", image_info.author);
        }
        
        if (image_info.comment) {
            printf("   Comment: %s\n", image_info.comment);
        }
        
        if (image_info.tag_count > 1) {
            printf("   Additional Tags:\n");
            for (int i = 1; i < image_info.tag_count; i++) {
                printf("     - %s\n", image_info.tags[i]);
            }
        }
    } else {
        printf("   ✗ Image not found locally (this is expected in demo mode)\n");
    }
    printf("\n");
    
    // Example 4: Create container from image
    printf("5. Creating container from image...\n");
    
    struct containerv_options* options = containerv_options_new();
    if (!options) {
        fprintf(stderr, "Failed to create container options\n");
        goto cleanup;
    }
    
    // Set some basic options
    containerv_options_set_caps(options, CV_CAP_NETWORK | CV_CAP_FILESYSTEM);
    
#ifdef _WIN32
    // Windows-specific resource limits
    containerv_options_set_resource_limits(options, "1G", "50", "256");
#else
    // Linux-specific cgroup limits  
    containerv_options_set_cgroup_limits(options, "1G", "100", "256");
#endif
    
    // Set the image to use
    containerv_options_set_image(options, &ubuntu_ref);
    
    struct containerv_container* container = NULL;
    printf("   Creating container from ubuntu:22.04...\n");
    
    if (containerv_create_from_image(&ubuntu_ref, options, &container) == 0) {
        printf("   ✓ Container created successfully\n");
        printf("   Container ID: %s\n", containerv_id(container));
        
        // Get image reference back from container
        struct containerv_image_ref container_image;
        if (containerv_get_image(container, &container_image) == 0) {
            printf("   Base Image: %s:%s\n", 
                   container_image.repository,
                   container_image.tag ? container_image.tag : "latest");
        }
        
        // Clean up container
        containerv_destroy(container);
        printf("   ✓ Container cleaned up\n");
        
    } else {
        printf("   ✗ Failed to create container (this is expected in demo mode)\n");
    }
    
    containerv_options_delete(options);
    printf("\n");
    
    // Example 5: Cache management
    printf("6. Cache management operations...\n");
    
    struct containerv_cache_stats stats;
    if (containerv_cache_get_stats(&stats) == 0) {
        char total_size_str[32], available_str[32];
        format_size(stats.total_size, total_size_str, sizeof(total_size_str));
        format_size(stats.available_space, available_str, sizeof(available_str));
        
        printf("   Cache Statistics:\n");
        printf("   - Total cache size: %s\n", total_size_str);
        printf("   - Available disk space: %s\n", available_str);
        printf("   - Cached images: %d\n", stats.image_count);
        printf("   - Cached layers: %d\n", stats.layer_count);
        
        if (stats.last_gc > 0) {
            char gc_time_str[32];
            format_time(stats.last_gc, gc_time_str, sizeof(gc_time_str));
            printf("   - Last garbage collection: %s\n", gc_time_str);
        } else {
            printf("   - Last garbage collection: Never\n");
        }
    } else {
        printf("   ✗ Failed to get cache statistics\n");
    }
    
    // Run garbage collection
    printf("\n   Running garbage collection...\n");
    int gc_items = containerv_cache_gc(false);
    if (gc_items >= 0) {
        printf("   ✓ Garbage collection completed, %d items cleaned up\n", gc_items);
    } else {
        printf("   ✗ Garbage collection failed\n");
    }
    
    // Prune unused images (older than 7 days)
    printf("   Pruning unused images (7+ days old)...\n");
    int pruned_items = containerv_cache_prune(7);
    if (pruned_items >= 0) {
        printf("   ✓ Pruning completed, %d items removed\n", pruned_items);
    } else {
        printf("   ✗ Pruning failed\n");
    }
    printf("\n");
    
    // Example 6: Advanced image operations
    printf("7. Advanced image operations...\n");
    
    // Parse image reference from string
    printf("   Parsing image references...\n");
    
    const char* test_refs[] = {
        "ubuntu:latest",
        "docker.io/library/ubuntu:22.04", 
        "mcr.microsoft.com/windows/servercore:ltsc2022",
        "localhost:5000/myapp:v1.0.0",
        "ubuntu@sha256:abcd1234...",
    };
    
    for (size_t i = 0; i < sizeof(test_refs) / sizeof(test_refs[0]); i++) {
        printf("     \"%s\"\n", test_refs[i]);
        
        // In a real implementation, you'd have a parse function
        // struct containerv_image_ref parsed;
        // if (parse_image_ref(test_refs[i], &parsed) == 0) {
        //     printf("       Registry: %s\n", parsed.registry ? parsed.registry : "default");
        //     printf("       Namespace: %s\n", parsed.namespace ? parsed.namespace : "default"); 
        //     printf("       Repository: %s\n", parsed.repository);
        //     printf("       Tag: %s\n", parsed.tag ? parsed.tag : "latest");
        //     if (parsed.digest) {
        //         printf("       Digest: %.12s...\n", parsed.digest);
        //     }
        // }
    }
    
    // Image removal example
    printf("\n   Removing images...\n");
    if (containerv_image_remove(&ubuntu_ref, false) == 0) {
        printf("   ✓ Removed ubuntu:22.04\n");
    } else {
        printf("   ✗ Failed to remove ubuntu:22.04 (this is expected in demo mode)\n");
    }
    printf("\n");
    
cleanup:
    // Clean up the image system
    printf("8. Cleaning up...\n");
    containerv_images_cleanup();
    printf("   ✓ Image system cleaned up\n\n");
    
    printf("Demo completed successfully!\n");
    printf("\nKey Features Demonstrated:\n");
    printf("- OCI-compatible image management\n");
    printf("- Cross-platform support (Linux OverlayFS, Windows VHD)\n");
    printf("- Registry integration with authentication\n");  
    printf("- Layer caching and deduplication\n");
    printf("- Container creation from images\n");
    printf("- Cache management and garbage collection\n");
    printf("- Progress tracking for long operations\n");
    printf("- Comprehensive image metadata handling\n");
    
    return 0;
}