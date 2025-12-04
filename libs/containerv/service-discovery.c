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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Service discovery registry implementation
struct service_registry_entry {
    char service_name[256];
    struct containerv_service_endpoint* endpoints;
    int endpoint_count;
    int endpoint_capacity;
    time_t last_updated;
};

struct service_discovery_state {
    bool initialized;
    pthread_mutex_t registry_lock;
    struct service_registry_entry* services;
    int service_count;
    int service_capacity;
    
    // DNS-like resolution cache
    struct dns_cache_entry* dns_cache;
    int dns_cache_count;
    int dns_cache_capacity;
    time_t dns_cache_ttl;  // Cache TTL in seconds
};

struct dns_cache_entry {
    char service_name[256];
    char ip_address[16];
    int port;
    time_t expires_at;
};

static struct service_discovery_state g_discovery = {0};

// Forward declarations
static int find_service_entry(const char* service_name);
static int create_service_entry(const char* service_name);
static int find_endpoint_in_service(struct service_registry_entry* entry, const char* instance_id);
static void cleanup_expired_endpoints(void);
static int update_dns_cache(const char* service_name, const char* ip_address, int port);
static int lookup_dns_cache(const char* service_name, char* ip_address, int* port);

/**
 * @brief Initialize service discovery system
 */
int containerv_service_discovery_init(void) {
    if (g_discovery.initialized) {
        return 0;
    }
    
    if (pthread_mutex_init(&g_discovery.registry_lock, NULL) != 0) {
        return -1;
    }
    
    g_discovery.service_capacity = 32;
    g_discovery.services = calloc(g_discovery.service_capacity, 
                                 sizeof(struct service_registry_entry));
    if (!g_discovery.services) {
        pthread_mutex_destroy(&g_discovery.registry_lock);
        return -1;
    }
    
    g_discovery.dns_cache_capacity = 128;
    g_discovery.dns_cache = calloc(g_discovery.dns_cache_capacity,
                                  sizeof(struct dns_cache_entry));
    if (!g_discovery.dns_cache) {
        free(g_discovery.services);
        pthread_mutex_destroy(&g_discovery.registry_lock);
        return -1;
    }
    
    g_discovery.dns_cache_ttl = 30; // 30 seconds TTL
    g_discovery.initialized = true;
    
    return 0;
}

/**
 * @brief Cleanup service discovery system
 */
void containerv_service_discovery_cleanup(void) {
    if (!g_discovery.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    // Cleanup service registry
    for (int i = 0; i < g_discovery.service_count; i++) {
        struct service_registry_entry* entry = &g_discovery.services[i];
        
        if (entry->endpoints) {
            for (int j = 0; j < entry->endpoint_count; j++) {
                free(entry->endpoints[j].service_name);
                free(entry->endpoints[j].instance_id);
                free(entry->endpoints[j].ip_address);
            }
            free(entry->endpoints);
        }
    }
    free(g_discovery.services);
    
    // Cleanup DNS cache
    free(g_discovery.dns_cache);
    
    g_discovery.initialized = false;
    pthread_mutex_unlock(&g_discovery.registry_lock);
    pthread_mutex_destroy(&g_discovery.registry_lock);
}

/**
 * @brief Find service entry by name
 */
static int find_service_entry(const char* service_name) {
    for (int i = 0; i < g_discovery.service_count; i++) {
        if (strcmp(g_discovery.services[i].service_name, service_name) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Create new service entry
 */
static int create_service_entry(const char* service_name) {
    // Expand capacity if needed
    if (g_discovery.service_count >= g_discovery.service_capacity) {
        g_discovery.service_capacity *= 2;
        g_discovery.services = realloc(g_discovery.services,
            g_discovery.service_capacity * sizeof(struct service_registry_entry));
        if (!g_discovery.services) {
            return -1;
        }
    }
    
    struct service_registry_entry* entry = &g_discovery.services[g_discovery.service_count];
    memset(entry, 0, sizeof(*entry));
    
    strncpy(entry->service_name, service_name, sizeof(entry->service_name) - 1);
    entry->endpoint_capacity = 8;
    entry->endpoints = calloc(entry->endpoint_capacity, 
                             sizeof(struct containerv_service_endpoint));
    if (!entry->endpoints) {
        return -1;
    }
    
    entry->last_updated = time(NULL);
    return g_discovery.service_count++;
}

/**
 * @brief Find endpoint in service by instance ID
 */
static int find_endpoint_in_service(struct service_registry_entry* entry, const char* instance_id) {
    for (int i = 0; i < entry->endpoint_count; i++) {
        if (strcmp(entry->endpoints[i].instance_id, instance_id) == 0) {
            return i;
        }
    }
    return -1;
}

/**
 * @brief Register a service endpoint
 */
int containerv_register_service_endpoint(const struct containerv_service_endpoint* endpoint) {
    if (!endpoint || !endpoint->service_name || !endpoint->instance_id || 
        !g_discovery.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    // Find or create service entry
    int service_index = find_service_entry(endpoint->service_name);
    if (service_index == -1) {
        service_index = create_service_entry(endpoint->service_name);
        if (service_index == -1) {
            pthread_mutex_unlock(&g_discovery.registry_lock);
            return -1;
        }
    }
    
    struct service_registry_entry* entry = &g_discovery.services[service_index];
    
    // Check if endpoint already exists (update case)
    int endpoint_index = find_endpoint_in_service(entry, endpoint->instance_id);
    
    if (endpoint_index == -1) {
        // New endpoint - expand capacity if needed
        if (entry->endpoint_count >= entry->endpoint_capacity) {
            entry->endpoint_capacity *= 2;
            entry->endpoints = realloc(entry->endpoints,
                entry->endpoint_capacity * sizeof(struct containerv_service_endpoint));
            if (!entry->endpoints) {
                pthread_mutex_unlock(&g_discovery.registry_lock);
                return -1;
            }
        }
        endpoint_index = entry->endpoint_count++;
    } else {
        // Update existing endpoint - free old strings
        free(entry->endpoints[endpoint_index].service_name);
        free(entry->endpoints[endpoint_index].instance_id);
        free(entry->endpoints[endpoint_index].ip_address);
    }
    
    // Copy endpoint data
    struct containerv_service_endpoint* stored_endpoint = &entry->endpoints[endpoint_index];
    stored_endpoint->service_name = strdup(endpoint->service_name);
    stored_endpoint->instance_id = strdup(endpoint->instance_id);
    stored_endpoint->ip_address = strdup(endpoint->ip_address);
    stored_endpoint->port = endpoint->port;
    stored_endpoint->healthy = endpoint->healthy;
    stored_endpoint->last_health_check = endpoint->last_health_check;
    stored_endpoint->weight = endpoint->weight > 0 ? endpoint->weight : 1;
    
    entry->last_updated = time(NULL);
    
    // Update DNS cache if endpoint is healthy
    if (endpoint->healthy) {
        update_dns_cache(endpoint->service_name, endpoint->ip_address, endpoint->port);
    }
    
    pthread_mutex_unlock(&g_discovery.registry_lock);
    return 0;
}

/**
 * @brief Unregister a service endpoint
 */
int containerv_unregister_service_endpoint(const char* service_name, const char* instance_id) {
    if (!service_name || !instance_id || !g_discovery.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    int service_index = find_service_entry(service_name);
    if (service_index == -1) {
        pthread_mutex_unlock(&g_discovery.registry_lock);
        return -1;
    }
    
    struct service_registry_entry* entry = &g_discovery.services[service_index];
    int endpoint_index = find_endpoint_in_service(entry, instance_id);
    
    if (endpoint_index == -1) {
        pthread_mutex_unlock(&g_discovery.registry_lock);
        return -1;
    }
    
    // Free endpoint data
    free(entry->endpoints[endpoint_index].service_name);
    free(entry->endpoints[endpoint_index].instance_id);
    free(entry->endpoints[endpoint_index].ip_address);
    
    // Move last endpoint to this position
    if (endpoint_index < entry->endpoint_count - 1) {
        entry->endpoints[endpoint_index] = entry->endpoints[entry->endpoint_count - 1];
    }
    entry->endpoint_count--;
    
    entry->last_updated = time(NULL);
    
    pthread_mutex_unlock(&g_discovery.registry_lock);
    return 0;
}

/**
 * @brief Discover all endpoints for a service
 */
int containerv_discover_service_endpoints(const char* service_name,
                                         struct containerv_service_endpoint* endpoints,
                                         int max_endpoints) {
    if (!service_name || !endpoints || max_endpoints <= 0 || !g_discovery.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    // Clean up expired endpoints first
    cleanup_expired_endpoints();
    
    int service_index = find_service_entry(service_name);
    if (service_index == -1) {
        pthread_mutex_unlock(&g_discovery.registry_lock);
        return 0; // No endpoints found
    }
    
    struct service_registry_entry* entry = &g_discovery.services[service_index];
    int endpoint_count = 0;
    
    for (int i = 0; i < entry->endpoint_count && endpoint_count < max_endpoints; i++) {
        // Only return healthy endpoints
        if (entry->endpoints[i].healthy) {
            endpoints[endpoint_count] = entry->endpoints[i];
            
            // Duplicate strings for safety
            endpoints[endpoint_count].service_name = strdup(entry->endpoints[i].service_name);
            endpoints[endpoint_count].instance_id = strdup(entry->endpoints[i].instance_id);
            endpoints[endpoint_count].ip_address = strdup(entry->endpoints[i].ip_address);
            
            endpoint_count++;
        }
    }
    
    pthread_mutex_unlock(&g_discovery.registry_lock);
    return endpoint_count;
}

/**
 * @brief Resolve service name to IP address and port (load balanced)
 */
int containerv_resolve_service_address(const char* service_name, char* ip_address, int* port) {
    if (!service_name || !ip_address || !port || !g_discovery.initialized) {
        return -1;
    }
    
    // First check DNS cache
    if (lookup_dns_cache(service_name, ip_address, port) == 0) {
        return 0;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    int service_index = find_service_entry(service_name);
    if (service_index == -1) {
        pthread_mutex_unlock(&g_discovery.registry_lock);
        return -1;
    }
    
    struct service_registry_entry* entry = &g_discovery.services[service_index];
    
    // Find healthy endpoints
    struct containerv_service_endpoint* healthy_endpoints[64];
    int healthy_count = 0;
    
    for (int i = 0; i < entry->endpoint_count && healthy_count < 64; i++) {
        if (entry->endpoints[i].healthy) {
            healthy_endpoints[healthy_count++] = &entry->endpoints[i];
        }
    }
    
    if (healthy_count == 0) {
        pthread_mutex_unlock(&g_discovery.registry_lock);
        return -1;
    }
    
    // Simple round-robin load balancing
    static int round_robin_counter = 0;
    int selected_index = (round_robin_counter++) % healthy_count;
    
    struct containerv_service_endpoint* selected = healthy_endpoints[selected_index];
    
    strcpy(ip_address, selected->ip_address);
    *port = selected->port;
    
    // Update DNS cache
    update_dns_cache(service_name, selected->ip_address, selected->port);
    
    pthread_mutex_unlock(&g_discovery.registry_lock);
    return 0;
}

/**
 * @brief Update DNS cache entry
 */
static int update_dns_cache(const char* service_name, const char* ip_address, int port) {
    // Find existing cache entry
    int cache_index = -1;
    for (int i = 0; i < g_discovery.dns_cache_count; i++) {
        if (strcmp(g_discovery.dns_cache[i].service_name, service_name) == 0) {
            cache_index = i;
            break;
        }
    }
    
    // Create new entry if not found
    if (cache_index == -1) {
        if (g_discovery.dns_cache_count >= g_discovery.dns_cache_capacity) {
            return -1; // Cache full
        }
        cache_index = g_discovery.dns_cache_count++;
    }
    
    struct dns_cache_entry* entry = &g_discovery.dns_cache[cache_index];
    strncpy(entry->service_name, service_name, sizeof(entry->service_name) - 1);
    strncpy(entry->ip_address, ip_address, sizeof(entry->ip_address) - 1);
    entry->port = port;
    entry->expires_at = time(NULL) + g_discovery.dns_cache_ttl;
    
    return 0;
}

/**
 * @brief Lookup DNS cache entry
 */
static int lookup_dns_cache(const char* service_name, char* ip_address, int* port) {
    time_t now = time(NULL);
    
    for (int i = 0; i < g_discovery.dns_cache_count; i++) {
        struct dns_cache_entry* entry = &g_discovery.dns_cache[i];
        
        if (strcmp(entry->service_name, service_name) == 0) {
            if (entry->expires_at > now) {
                strcpy(ip_address, entry->ip_address);
                *port = entry->port;
                return 0; // Cache hit
            } else {
                // Remove expired entry
                if (i < g_discovery.dns_cache_count - 1) {
                    g_discovery.dns_cache[i] = g_discovery.dns_cache[g_discovery.dns_cache_count - 1];
                }
                g_discovery.dns_cache_count--;
                i--; // Check this index again
            }
        }
    }
    
    return -1; // Cache miss
}

/**
 * @brief Cleanup expired endpoints
 */
static void cleanup_expired_endpoints(void) {
    time_t now = time(NULL);
    const time_t expiry_threshold = 300; // 5 minutes without health check
    
    for (int i = 0; i < g_discovery.service_count; i++) {
        struct service_registry_entry* entry = &g_discovery.services[i];
        
        for (int j = 0; j < entry->endpoint_count; j++) {
            struct containerv_service_endpoint* endpoint = &entry->endpoints[j];
            
            // Mark as unhealthy if no recent health check
            if (endpoint->healthy && 
                (now - endpoint->last_health_check) > expiry_threshold) {
                endpoint->healthy = false;
            }
        }
    }
}

/**
 * @brief Get service registry statistics (for debugging/monitoring)
 */
int containerv_get_service_registry_stats(int* service_count, int* endpoint_count, 
                                         int* cache_entries) {
    if (!g_discovery.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    if (service_count) *service_count = g_discovery.service_count;
    if (cache_entries) *cache_entries = g_discovery.dns_cache_count;
    
    if (endpoint_count) {
        int total_endpoints = 0;
        for (int i = 0; i < g_discovery.service_count; i++) {
            total_endpoints += g_discovery.services[i].endpoint_count;
        }
        *endpoint_count = total_endpoints;
    }
    
    pthread_mutex_unlock(&g_discovery.registry_lock);
    return 0;
}

/**
 * @brief Update endpoint health status
 */
int containerv_update_endpoint_health(const char* service_name, const char* instance_id, 
                                     bool healthy) {
    if (!service_name || !instance_id || !g_discovery.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_discovery.registry_lock);
    
    int service_index = find_service_entry(service_name);
    if (service_index == -1) {
        pthread_mutex_unlock(&g_discovery.registry_lock);
        return -1;
    }
    
    struct service_registry_entry* entry = &g_discovery.services[service_index];
    int endpoint_index = find_endpoint_in_service(entry, instance_id);
    
    if (endpoint_index != -1) {
        entry->endpoints[endpoint_index].healthy = healthy;
        entry->endpoints[endpoint_index].last_health_check = time(NULL);
    }
    
    pthread_mutex_unlock(&g_discovery.registry_lock);
    return endpoint_index != -1 ? 0 : -1;
}