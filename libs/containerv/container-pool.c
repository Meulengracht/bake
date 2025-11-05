/**
 * Container Pool Implementation
 * 
 * Provides container pooling for fast startup times by pre-allocating
 * and reusing container instances.
 */

#include <chef/containerv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <threads.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>


// Pool entry states
enum pool_entry_state {
    POOL_ENTRY_AVAILABLE,   // Ready for use
    POOL_ENTRY_IN_USE,      // Currently allocated
    POOL_ENTRY_WARMING,     // Being prepared
    POOL_ENTRY_INVALID      // Needs to be replaced
};

// Container pool entry
struct pool_entry {
    struct containerv_container* container;
    enum pool_entry_state        state;
    struct containerv_image_ref  image_ref;
    time_t                       created_at;
    time_t                       last_used;
    uint32_t                     use_count;
    bool                         needs_reset;
    struct pool_entry*           next;
};

// Container pool structure
struct containerv_pool {
    struct containerv_pool_config config;
    
    // Pool entries
    struct pool_entry* entries;
    struct pool_entry* free_list;
    uint32_t           entry_count;
    uint32_t           available_count;
    uint32_t           in_use_count;
    
    // Statistics
    uint64_t allocations_total;
    uint64_t pool_hits;
    uint64_t pool_misses;
    uint64_t evictions;
    
    // Synchronization
    mtx_t              pool_mutex;
    cnd_t              pool_cond;
    bool               shutdown;
    
    // Background maintenance thread
    thrd_t             maintenance_thread;
    bool               maintenance_running;
};

// Forward declarations
static int pool_maintenance_thread(void* arg);
static int pool_create_entry(struct containerv_pool* pool, const struct containerv_image_ref* image_ref);
static struct pool_entry* pool_find_available_entry(struct containerv_pool* pool, const struct containerv_image_ref* image_ref);
static int pool_reset_container(struct pool_entry* entry);
static void pool_cleanup_entry(struct pool_entry* entry);
static bool pool_images_match(const struct containerv_image_ref* ref1, const struct containerv_image_ref* ref2);

int containerv_create_container_pool(struct containerv_performance_engine* engine, 
                                   const struct containerv_pool_config* config) {
    if (!engine || !config) {
        errno = EINVAL;
        return -1;
    }
    
    // Allocate pool structure
    struct containerv_pool* pool = calloc(1, sizeof(struct containerv_pool));
    if (!pool) {
        return -1;
    }
    
    // Copy configuration with defaults
    pool->config = *config;
    if (pool->config.min_size == 0) pool->config.min_size = 2;
    if (pool->config.max_size == 0) pool->config.max_size = 20;
    if (pool->config.warm_count == 0) pool->config.warm_count = pool->config.min_size;
    if (pool->config.idle_timeout_seconds == 0) pool->config.idle_timeout_seconds = 300; // 5 minutes
    
    // Initialize synchronization
    if (mtx_init(&pool->pool_mutex, mtx_plain) != thrd_success) {
        free(pool);
        return -1;
    }
    
    if (cnd_init(&pool->pool_cond) != thrd_success) {
        mtx_destroy(&pool->pool_mutex);
        free(pool);
        return -1;
    }
    
    // Pre-allocate minimum pool entries
    if (pool->config.policy == CV_POOL_PREALLOC || pool->config.policy == CV_POOL_HYBRID) {
        for (int i = 0; i < pool->config.prewarmed_image_count && i < pool->config.min_size; i++) {
            struct containerv_image_ref image_ref = {0};
            // Parse image string into ref structure
            const char* image_str = pool->config.prewarmed_images[i];
            char* image_copy = strdup(image_str);
            if (!image_copy) continue;
            
            // Simple parsing for "registry/namespace/repository:tag"
            char* tag_sep = strrchr(image_copy, ':');
            if (tag_sep) {
                *tag_sep = '\0';
                image_ref.tag = strdup(tag_sep + 1);
            }
            
            char* slash1 = strchr(image_copy, '/');
            if (slash1) {
                *slash1 = '\0';
                image_ref.registry = strdup(image_copy);
                
                char* slash2 = strchr(slash1 + 1, '/');
                if (slash2) {
                    *slash2 = '\0';
                    image_ref.namespace = strdup(slash1 + 1);
                    image_ref.repository = strdup(slash2 + 1);
                } else {
                    image_ref.repository = strdup(slash1 + 1);
                }
            } else {
                image_ref.repository = strdup(image_copy);
            }
            
            pool_create_entry(pool, &image_ref);
            
            free(image_copy);
            if (image_ref.registry) free(image_ref.registry);
            if (image_ref.namespace) free(image_ref.namespace);
            if (image_ref.repository) free(image_ref.repository);
            if (image_ref.tag) free(image_ref.tag);
        }
    }
    
    // Start maintenance thread
    pool->maintenance_running = true;
    if (thrd_create(&pool->maintenance_thread, pool_maintenance_thread, pool) != thrd_success) {
        pool->maintenance_running = false;
        // Continue without maintenance thread (non-fatal)
    }
    
    engine->container_pool = pool;
    return 0;
}

int containerv_get_pooled_container(struct containerv_performance_engine* engine,
                                  const struct containerv_image_ref* image_ref,
                                  struct containerv_options* options,
                                  struct containerv_container** container) {
    if (!engine || !engine->container_pool || !image_ref || !container) {
        errno = EINVAL;
        return -1;
    }
    
    struct containerv_pool* pool = engine->container_pool;
    struct pool_entry* entry = NULL;
    
    mtx_lock(&pool->pool_mutex);
    
    // Update statistics
    pool->allocations_total++;
    
    // Try to find available container in pool
    entry = pool_find_available_entry(pool, image_ref);
    if (entry) {
        // Pool hit - reuse existing container
        entry->state = POOL_ENTRY_IN_USE;
        entry->last_used = time(NULL);
        entry->use_count++;
        pool->available_count--;
        pool->in_use_count++;
        pool->pool_hits++;
        
        *container = entry->container;
        mtx_unlock(&pool->pool_mutex);
        
        // Reset container to clean state if needed
        if (entry->needs_reset) {
            if (pool_reset_container(entry) != 0) {
                // Reset failed, mark entry as invalid
                mtx_lock(&pool->pool_mutex);
                entry->state = POOL_ENTRY_INVALID;
                pool->in_use_count--;
                mtx_unlock(&pool->pool_mutex);
                return -1;
            }
            entry->needs_reset = false;
        }
        
        return 0;
    }
    
    // Pool miss - need to create new container
    pool->pool_misses++;
    mtx_unlock(&pool->pool_mutex);
    
    // Create container normally
    // Note: In a real implementation, this would extract the rootfs from the image
    // For now, we'll create a basic container and let the caller handle image setup
    char rootfs_path[256];
    snprintf(rootfs_path, sizeof(rootfs_path), "/tmp/container_%ld", time(NULL));
    
    int result = containerv_create(rootfs_path, options, container);
    if (result != 0) {
        return result;
    }
    
    // If pool has space and policy allows, add to pool for future use
    mtx_lock(&pool->pool_mutex);
    if (pool->entry_count < pool->config.max_size && 
        (pool->config.policy == CV_POOL_ON_DEMAND || pool->config.policy == CV_POOL_HYBRID)) {
        
        // Create new pool entry for this container
        struct pool_entry* new_entry = calloc(1, sizeof(struct pool_entry));
        if (new_entry) {
            new_entry->container = *container;
            new_entry->state = POOL_ENTRY_IN_USE;
            new_entry->created_at = time(NULL);
            new_entry->last_used = time(NULL);
            new_entry->use_count = 1;
            
            // Copy image reference
            if (image_ref->registry) new_entry->image_ref.registry = strdup(image_ref->registry);
            if (image_ref->namespace) new_entry->image_ref.namespace = strdup(image_ref->namespace);
            if (image_ref->repository) new_entry->image_ref.repository = strdup(image_ref->repository);
            if (image_ref->tag) new_entry->image_ref.tag = strdup(image_ref->tag);
            if (image_ref->digest) new_entry->image_ref.digest = strdup(image_ref->digest);
            
            // Add to pool
            new_entry->next = pool->entries;
            pool->entries = new_entry;
            pool->entry_count++;
            pool->in_use_count++;
        }
    }
    mtx_unlock(&pool->pool_mutex);
    
    return 0;
}

int containerv_return_to_pool(struct containerv_performance_engine* engine,
                            struct containerv_container* container) {
    if (!engine || !engine->container_pool || !container) {
        errno = EINVAL;
        return -1;
    }
    
    struct containerv_pool* pool = engine->container_pool;
    struct pool_entry* entry = NULL;
    
    mtx_lock(&pool->pool_mutex);
    
    // Find the pool entry for this container
    for (entry = pool->entries; entry; entry = entry->next) {
        if (entry->container == container && entry->state == POOL_ENTRY_IN_USE) {
            break;
        }
    }
    
    if (!entry) {
        mtx_unlock(&pool->pool_mutex);
        // Container not from pool, destroy normally
        return containerv_destroy(container);
    }
    
    // Return container to pool
    entry->state = POOL_ENTRY_AVAILABLE;
    entry->needs_reset = true; // Mark for cleanup on next use
    pool->available_count++;
    pool->in_use_count--;
    
    // Signal waiting threads
    cnd_signal(&pool->pool_cond);
    
    mtx_unlock(&pool->pool_mutex);
    return 0;
}

// Internal helper functions

static struct pool_entry* pool_find_available_entry(struct containerv_pool* pool,
                                                  const struct containerv_image_ref* image_ref) {
    struct pool_entry* entry;
    
    for (entry = pool->entries; entry; entry = entry->next) {
        if (entry->state == POOL_ENTRY_AVAILABLE && 
            pool_images_match(&entry->image_ref, image_ref)) {
            return entry;
        }
    }
    
    return NULL;
}

static int pool_create_entry(struct containerv_pool* pool, const struct containerv_image_ref* image_ref) {
    if (!pool || !image_ref) {
        return -1;
    }
    
    // Create container options for pool entry
    struct containerv_options* options = containerv_options_new();
    if (!options) {
        return -1;
    }
    
    // Set basic capabilities for pooled container
    containerv_options_set_caps(options, CV_CAP_NETWORK | CV_CAP_FILESYSTEM);
    
    // Create container
    // Note: In a real implementation, this would set up the container from the image
    char rootfs_path[256];
    snprintf(rootfs_path, sizeof(rootfs_path), "/tmp/pool_container_%ld_%d", 
             time(NULL), rand());
    
    struct containerv_container* container;
    int result = containerv_create(rootfs_path, options, &container);
    containerv_options_delete(options);
    
    if (result != 0) {
        return -1;
    }
    
    // Create pool entry
    struct pool_entry* entry = calloc(1, sizeof(struct pool_entry));
    if (!entry) {
        containerv_destroy(container);
        return -1;
    }
    
    entry->container = container;
    entry->state = POOL_ENTRY_AVAILABLE;
    entry->created_at = time(NULL);
    entry->last_used = time(NULL);
    
    // Copy image reference
    if (image_ref->registry) entry->image_ref.registry = strdup(image_ref->registry);
    if (image_ref->namespace) entry->image_ref.namespace = strdup(image_ref->namespace);
    if (image_ref->repository) entry->image_ref.repository = strdup(image_ref->repository);
    if (image_ref->tag) entry->image_ref.tag = strdup(image_ref->tag);
    if (image_ref->digest) entry->image_ref.digest = strdup(image_ref->digest);
    
    // Add to pool
    mtx_lock(&pool->pool_mutex);
    entry->next = pool->entries;
    pool->entries = entry;
    pool->entry_count++;
    pool->available_count++;
    mtx_unlock(&pool->pool_mutex);
    
    return 0;
}

static int pool_reset_container(struct pool_entry* entry) {
    if (!entry || !entry->container) {
        return -1;
    }
    
    // Reset container to clean state
    // This would typically involve:
    // 1. Stopping any running processes
    // 2. Cleaning up temporary files
    // 3. Resetting environment variables
    // 4. Clearing network connections
    
    // For now, we'll just mark it as reset
    // In a real implementation, this would do actual cleanup
    
    return 0;
}

static void pool_cleanup_entry(struct pool_entry* entry) {
    if (!entry) return;
    
    if (entry->container) {
        containerv_destroy(entry->container);
    }
    
    if (entry->image_ref.registry) free(entry->image_ref.registry);
    if (entry->image_ref.namespace) free(entry->image_ref.namespace);
    if (entry->image_ref.repository) free(entry->image_ref.repository);
    if (entry->image_ref.tag) free(entry->image_ref.tag);
    if (entry->image_ref.digest) free(entry->image_ref.digest);
    
    free(entry);
}

static bool pool_images_match(const struct containerv_image_ref* ref1,
                            const struct containerv_image_ref* ref2) {
    if (!ref1 || !ref2) return false;
    
    // Compare repository (required)
    if (!ref1->repository || !ref2->repository ||
        strcmp(ref1->repository, ref2->repository) != 0) {
        return false;
    }
    
    // Compare registry (optional)
    const char* reg1 = ref1->registry ? ref1->registry : "docker.io";
    const char* reg2 = ref2->registry ? ref2->registry : "docker.io";
    if (strcmp(reg1, reg2) != 0) {
        return false;
    }
    
    // Compare namespace (optional)
    const char* ns1 = ref1->namespace ? ref1->namespace : "library";
    const char* ns2 = ref2->namespace ? ref2->namespace : "library";
    if (strcmp(ns1, ns2) != 0) {
        return false;
    }
    
    // Compare tag/digest
    if (ref1->digest && ref2->digest) {
        return strcmp(ref1->digest, ref2->digest) == 0;
    } else {
        const char* tag1 = ref1->tag ? ref1->tag : "latest";
        const char* tag2 = ref2->tag ? ref2->tag : "latest";
        return strcmp(tag1, tag2) == 0;
    }
}

static int pool_maintenance_thread(void* arg) {
    struct containerv_pool* pool = (struct containerv_pool*)arg;
    time_t now, last_maintenance = time(NULL);
    
    while (pool->maintenance_running) {
        sleep(60); // Check every minute
        
        if (!pool->maintenance_running) break;
        
        now = time(NULL);
        mtx_lock(&pool->pool_mutex);
        
        // Clean up expired entries
        struct pool_entry* entry = pool->entries;
        struct pool_entry* prev = NULL;
        
        while (entry) {
            bool should_remove = false;
            
            // Check if entry has expired
            if (entry->state == POOL_ENTRY_AVAILABLE &&
                (now - entry->last_used) > pool->config.idle_timeout_seconds) {
                should_remove = true;
                pool->evictions++;
            }
            
            // Check if entry is invalid
            if (entry->state == POOL_ENTRY_INVALID) {
                should_remove = true;
            }
            
            if (should_remove && pool->entry_count > pool->config.min_size) {
                struct pool_entry* to_remove = entry;
                entry = entry->next;
                
                if (prev) {
                    prev->next = entry;
                } else {
                    pool->entries = entry;
                }
                
                if (to_remove->state == POOL_ENTRY_AVAILABLE) {
                    pool->available_count--;
                }
                pool->entry_count--;
                
                mtx_unlock(&pool->pool_mutex);
                pool_cleanup_entry(to_remove);
                mtx_lock(&pool->pool_mutex);
            } else {
                prev = entry;
                entry = entry->next;
            }
        }
        
        // Ensure minimum pool size with prewarmed containers
        if (pool->config.enable_prewarming && 
            pool->available_count < pool->config.warm_count) {
            
            int containers_to_create = pool->config.warm_count - pool->available_count;
            if (pool->entry_count + containers_to_create <= pool->config.max_size) {
                
                mtx_unlock(&pool->pool_mutex);
                
                // Create prewarmed containers
                for (int i = 0; i < containers_to_create && i < pool->config.prewarmed_image_count; i++) {
                    struct containerv_image_ref image_ref = {0};
                    // Parse prewarmed image (simplified)
                    if (pool->config.prewarmed_images[i]) {
                        image_ref.repository = strdup(pool->config.prewarmed_images[i]);
                        pool_create_entry(pool, &image_ref);
                        if (image_ref.repository) free(image_ref.repository);
                    }
                }
                
                mtx_lock(&pool->pool_mutex);
            }
        }
        
        mtx_unlock(&pool->pool_mutex);
        last_maintenance = now;
    }
    
    return 0;
}

void containerv_pool_cleanup(struct containerv_pool* pool) {
    if (!pool) return;
    
    // Stop maintenance thread
    pool->maintenance_running = false;
    if (pool->maintenance_thread) {
        thrd_join(pool->maintenance_thread, NULL);
    }
    
    // Cleanup all entries
    mtx_lock(&pool->pool_mutex);
    struct pool_entry* entry = pool->entries;
    while (entry) {
        struct pool_entry* next = entry->next;
        pool_cleanup_entry(entry);
        entry = next;
    }
    mtx_unlock(&pool->pool_mutex);
    
    // Cleanup synchronization
    mtx_destroy(&pool->pool_mutex);
    cnd_destroy(&pool->pool_cond);
    
    free(pool);
}

// Pool statistics functions
int containerv_pool_get_stats(struct containerv_pool* pool,
                            uint32_t* total_entries,
                            uint32_t* available_entries,
                            uint32_t* in_use_entries,
                            uint64_t* total_allocations,
                            uint64_t* pool_hits,
                            uint64_t* pool_misses) {
    if (!pool) {
        errno = EINVAL;
        return -1;
    }
    
    mtx_lock(&pool->pool_mutex);
    
    if (total_entries) *total_entries = pool->entry_count;
    if (available_entries) *available_entries = pool->available_count;
    if (in_use_entries) *in_use_entries = pool->in_use_count;
    if (total_allocations) *total_allocations = pool->allocations_total;
    if (pool_hits) *pool_hits = pool->pool_hits;
    if (pool_misses) *pool_misses = pool->pool_misses;
    
    mtx_unlock(&pool->pool_mutex);
    return 0;
}