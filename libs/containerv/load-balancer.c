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
#include <threads.h>
#include <limits.h>

#ifdef _WIN32
#include <windows.h>
#include <winsock2.h>
#else
#include <unistd.h>
#include <arpa/inet.h>
#endif

// Load balancer internal structure
struct containerv_load_balancer {
    char service_name[256];
    enum containerv_lb_algorithm algorithm;
    
    struct containerv_service_endpoint* endpoints;
    int endpoint_count;
    int endpoint_capacity;
    
    // Algorithm-specific state
    int round_robin_index;
    time_t* endpoint_last_used;
    int* endpoint_connection_count;
    
    mtx_t lock;
    time_t created_at;
    time_t last_updated;
};

// Hash function for IP-based load balancing
static unsigned int hash_ip(const char* ip) {
    unsigned int hash = 5381;
    if (ip) {
        while (*ip) {
            hash = ((hash << 5) + hash) + *ip++;
        }
    }
    return hash;
}

// Weighted selection helper
static int select_weighted_endpoint(struct containerv_load_balancer* lb) {
    int total_weight = 0;
    
    // Calculate total weight of healthy endpoints
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (lb->endpoints[i].healthy) {
            total_weight += lb->endpoints[i].weight;
        }
    }
    
    if (total_weight == 0) {
        return -1; // No healthy endpoints
    }
    
    // Random selection based on weight
    int random_weight = rand() % total_weight;
    int current_weight = 0;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (lb->endpoints[i].healthy) {
            current_weight += lb->endpoints[i].weight;
            if (random_weight < current_weight) {
                return i;
            }
        }
    }
    
    return -1; // Should not reach here
}

/**
 * @brief Create a load balancer for a service
 */
int containerv_create_load_balancer(const char* service_name,
                                   enum containerv_lb_algorithm algorithm,
                                   struct containerv_load_balancer** lb) {
    if (!service_name || !lb) {
        return -1;
    }
    
    struct containerv_load_balancer* new_lb = malloc(sizeof(struct containerv_load_balancer));
    if (!new_lb) {
        return -1;
    }
    
    memset(new_lb, 0, sizeof(*new_lb));
    
    strncpy(new_lb->service_name, service_name, sizeof(new_lb->service_name) - 1);
    new_lb->algorithm = algorithm;
    new_lb->endpoint_capacity = 16;
    new_lb->round_robin_index = 0;
    new_lb->created_at = time(NULL);
    
    // Allocate endpoint storage
    new_lb->endpoints = calloc(new_lb->endpoint_capacity, sizeof(struct containerv_service_endpoint));
    if (!new_lb->endpoints) {
        free(new_lb);
        return -1;
    }
    
    // Algorithm-specific initialization
    switch (algorithm) {
        case CV_LB_LEAST_CONNECTIONS:
            new_lb->endpoint_connection_count = calloc(new_lb->endpoint_capacity, sizeof(int));
            if (!new_lb->endpoint_connection_count) {
                free(new_lb->endpoints);
                free(new_lb);
                return -1;
            }
            break;
            
        case CV_LB_WEIGHTED_ROUND_ROBIN:
            new_lb->endpoint_last_used = calloc(new_lb->endpoint_capacity, sizeof(time_t));
            if (!new_lb->endpoint_last_used) {
                free(new_lb->endpoints);
                free(new_lb);
                return -1;
            }
            break;
            
        default:
            break;
    }
    
    if (mtx_init(&new_lb->lock, mtx_plain) != thrd_success) {
        free(new_lb->endpoint_connection_count);
        free(new_lb->endpoint_last_used);
        free(new_lb->endpoints);
        free(new_lb);
        return -1;
    }
    
    // Load current endpoints from service discovery
    struct containerv_service_endpoint discovered_endpoints[64];
    int discovered_count = containerv_discover_service_endpoints(service_name,
                                                                discovered_endpoints, 64);
    
    if (discovered_count > 0) {
        for (int i = 0; i < discovered_count && i < new_lb->endpoint_capacity; i++) {
            new_lb->endpoints[i] = discovered_endpoints[i];
            // Duplicate string fields
            new_lb->endpoints[i].service_name = strdup(discovered_endpoints[i].service_name);
            new_lb->endpoints[i].instance_id = strdup(discovered_endpoints[i].instance_id);
            new_lb->endpoints[i].ip_address = strdup(discovered_endpoints[i].ip_address);
        }
        new_lb->endpoint_count = discovered_count > new_lb->endpoint_capacity ? 
                                new_lb->endpoint_capacity : discovered_count;
    }
    
    *lb = new_lb;
    return 0;
}

/**
 * @brief Destroy a load balancer
 */
void containerv_destroy_load_balancer(struct containerv_load_balancer* lb) {
    if (!lb) {
        return;
    }
    
    mtx_lock(&lb->lock);
    
    // Free endpoints
    for (int i = 0; i < lb->endpoint_count; i++) {
        free(lb->endpoints[i].service_name);
        free(lb->endpoints[i].instance_id);
        free(lb->endpoints[i].ip_address);
    }
    free(lb->endpoints);
    
    // Free algorithm-specific data
    free(lb->endpoint_connection_count);
    free(lb->endpoint_last_used);
    
    mtx_unlock(&lb->lock);
    mtx_destroy(&lb->lock);
    
    free(lb);
}

/**
 * @brief Get next endpoint from load balancer
 */
int containerv_lb_get_endpoint(struct containerv_load_balancer* lb,
                              const char* client_info,
                              struct containerv_service_endpoint* endpoint) {
    if (!lb || !endpoint) {
        return -1;
    }
    
    mtx_lock(&lb->lock);
    
    // Refresh endpoints from service discovery
    struct containerv_service_endpoint fresh_endpoints[64];
    int fresh_count = containerv_discover_service_endpoints(lb->service_name,
                                                           fresh_endpoints, 64);
    
    if (fresh_count > 0) {
        // Update endpoint list
        for (int i = 0; i < lb->endpoint_count; i++) {
            free(lb->endpoints[i].service_name);
            free(lb->endpoints[i].instance_id);
            free(lb->endpoints[i].ip_address);
        }
        
        if (fresh_count > lb->endpoint_capacity) {
            lb->endpoint_capacity = fresh_count * 2;
            lb->endpoints = realloc(lb->endpoints, 
                                   lb->endpoint_capacity * sizeof(struct containerv_service_endpoint));
            
            // Reallocate algorithm-specific arrays
            if (lb->endpoint_connection_count) {
                lb->endpoint_connection_count = realloc(lb->endpoint_connection_count,
                                                       lb->endpoint_capacity * sizeof(int));
            }
            if (lb->endpoint_last_used) {
                lb->endpoint_last_used = realloc(lb->endpoint_last_used,
                                                lb->endpoint_capacity * sizeof(time_t));
            }
        }
        
        for (int i = 0; i < fresh_count; i++) {
            lb->endpoints[i] = fresh_endpoints[i];
            lb->endpoints[i].service_name = strdup(fresh_endpoints[i].service_name);
            lb->endpoints[i].instance_id = strdup(fresh_endpoints[i].instance_id);
            lb->endpoints[i].ip_address = strdup(fresh_endpoints[i].ip_address);
        }
        lb->endpoint_count = fresh_count;
        lb->last_updated = time(NULL);
    }
    
    // Count healthy endpoints
    int healthy_count = 0;
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (lb->endpoints[i].healthy) {
            healthy_count++;
        }
    }
    
    if (healthy_count == 0) {
        mtx_unlock(&lb->lock);
        return -1; // No healthy endpoints
    }
    
    int selected_index = -1;
    
    // Apply load balancing algorithm
    switch (lb->algorithm) {
        case CV_LB_ROUND_ROBIN: {
            // Simple round-robin among healthy endpoints
            int attempts = 0;
            do {
                lb->round_robin_index = (lb->round_robin_index + 1) % lb->endpoint_count;
                if (lb->endpoints[lb->round_robin_index].healthy) {
                    selected_index = lb->round_robin_index;
                    break;
                }
                attempts++;
            } while (attempts < lb->endpoint_count);
            break;
        }
        
        case CV_LB_LEAST_CONNECTIONS: {
            int min_connections = INT_MAX;
            
            for (int i = 0; i < lb->endpoint_count; i++) {
                if (lb->endpoints[i].healthy && 
                    lb->endpoint_connection_count[i] < min_connections) {
                    min_connections = lb->endpoint_connection_count[i];
                    selected_index = i;
                }
            }
            
            if (selected_index != -1) {
                lb->endpoint_connection_count[selected_index]++;
            }
            break;
        }
        
        case CV_LB_WEIGHTED_ROUND_ROBIN: {
            selected_index = select_weighted_endpoint(lb);
            if (selected_index != -1) {
                lb->endpoint_last_used[selected_index] = time(NULL);
            }
            break;
        }
        
        case CV_LB_IP_HASH: {
            if (client_info) {
                unsigned int hash = hash_ip(client_info);
                int start_index = hash % lb->endpoint_count;
                
                // Find healthy endpoint starting from hash position
                for (int i = 0; i < lb->endpoint_count; i++) {
                    int check_index = (start_index + i) % lb->endpoint_count;
                    if (lb->endpoints[check_index].healthy) {
                        selected_index = check_index;
                        break;
                    }
                }
            } else {
                // Fallback to round-robin if no client info
                selected_index = 0;
                for (int i = 0; i < lb->endpoint_count; i++) {
                    if (lb->endpoints[i].healthy) {
                        selected_index = i;
                        break;
                    }
                }
            }
            break;
        }
        
        case CV_LB_RANDOM: {
            // Random selection among healthy endpoints
            int healthy_indices[64];
            int healthy_index_count = 0;
            
            for (int i = 0; i < lb->endpoint_count && healthy_index_count < 64; i++) {
                if (lb->endpoints[i].healthy) {
                    healthy_indices[healthy_index_count++] = i;
                }
            }
            
            if (healthy_index_count > 0) {
                selected_index = healthy_indices[rand() % healthy_index_count];
            }
            break;
        }
    }
    
    if (selected_index != -1) {
        *endpoint = lb->endpoints[selected_index];
        // Duplicate strings for safety
        endpoint->service_name = strdup(lb->endpoints[selected_index].service_name);
        endpoint->instance_id = strdup(lb->endpoints[selected_index].instance_id);
        endpoint->ip_address = strdup(lb->endpoints[selected_index].ip_address);
    }
    
    mtx_unlock(&lb->lock);
    return selected_index != -1 ? 0 : -1;
}

/**
 * @brief Update endpoint health status in load balancer
 */
int containerv_lb_update_health(struct containerv_load_balancer* lb,
                               const char* instance_id, bool healthy) {
    if (!lb || !instance_id) {
        return -1;
    }
    
    mtx_lock(&lb->lock);
    
    int found_index = -1;
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].instance_id, instance_id) == 0) {
            lb->endpoints[i].healthy = healthy;
            lb->endpoints[i].last_health_check = time(NULL);
            found_index = i;
            
            // Reset connection count if endpoint becomes unhealthy
            if (!healthy && lb->endpoint_connection_count) {
                lb->endpoint_connection_count[i] = 0;
            }
            break;
        }
    }
    
    mtx_unlock(&lb->lock);
    return found_index != -1 ? 0 : -1;
}

/**
 * @brief Manually add endpoint to load balancer
 */
int containerv_lb_add_endpoint(struct containerv_load_balancer* lb,
                              const struct containerv_service_endpoint* endpoint) {
    if (!lb || !endpoint) {
        return -1;
    }
    
    mtx_lock(&lb->lock);
    
    // Check if endpoint already exists
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].instance_id, endpoint->instance_id) == 0) {
            // Update existing endpoint
            free(lb->endpoints[i].service_name);
            free(lb->endpoints[i].instance_id);
            free(lb->endpoints[i].ip_address);
            
            lb->endpoints[i] = *endpoint;
            lb->endpoints[i].service_name = strdup(endpoint->service_name);
            lb->endpoints[i].instance_id = strdup(endpoint->instance_id);
            lb->endpoints[i].ip_address = strdup(endpoint->ip_address);
            
            mtx_unlock(&lb->lock);
            return 0;
        }
    }
    
    // Add new endpoint
    if (lb->endpoint_count >= lb->endpoint_capacity) {
        lb->endpoint_capacity *= 2;
        lb->endpoints = realloc(lb->endpoints,
                               lb->endpoint_capacity * sizeof(struct containerv_service_endpoint));
        
        // Reallocate algorithm-specific arrays
        if (lb->endpoint_connection_count) {
            lb->endpoint_connection_count = realloc(lb->endpoint_connection_count,
                                                   lb->endpoint_capacity * sizeof(int));
            memset(&lb->endpoint_connection_count[lb->endpoint_count], 0,
                   (lb->endpoint_capacity - lb->endpoint_count) * sizeof(int));
        }
        if (lb->endpoint_last_used) {
            lb->endpoint_last_used = realloc(lb->endpoint_last_used,
                                            lb->endpoint_capacity * sizeof(time_t));
            memset(&lb->endpoint_last_used[lb->endpoint_count], 0,
                   (lb->endpoint_capacity - lb->endpoint_count) * sizeof(time_t));
        }
    }
    
    lb->endpoints[lb->endpoint_count] = *endpoint;
    lb->endpoints[lb->endpoint_count].service_name = strdup(endpoint->service_name);
    lb->endpoints[lb->endpoint_count].instance_id = strdup(endpoint->instance_id);
    lb->endpoints[lb->endpoint_count].ip_address = strdup(endpoint->ip_address);
    
    lb->endpoint_count++;
    
    mtx_unlock(&lb->lock);
    return 0;
}

/**
 * @brief Remove endpoint from load balancer
 */
int containerv_lb_remove_endpoint(struct containerv_load_balancer* lb, const char* instance_id) {
    if (!lb || !instance_id) {
        return -1;
    }
    
    mtx_lock(&lb->lock);
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].instance_id, instance_id) == 0) {
            // Free endpoint strings
            free(lb->endpoints[i].service_name);
            free(lb->endpoints[i].instance_id);
            free(lb->endpoints[i].ip_address);
            
            // Move last endpoint to this position
            if (i < lb->endpoint_count - 1) {
                lb->endpoints[i] = lb->endpoints[lb->endpoint_count - 1];
                
                if (lb->endpoint_connection_count) {
                    lb->endpoint_connection_count[i] = lb->endpoint_connection_count[lb->endpoint_count - 1];
                }
                if (lb->endpoint_last_used) {
                    lb->endpoint_last_used[i] = lb->endpoint_last_used[lb->endpoint_count - 1];
                }
            }
            
            lb->endpoint_count--;
            
            // Adjust round-robin index if necessary
            if (lb->round_robin_index >= lb->endpoint_count && lb->endpoint_count > 0) {
                lb->round_robin_index = 0;
            }
            
            mtx_unlock(&lb->lock);
            return 0;
        }
    }
    
    mtx_unlock(&lb->lock);
    return -1; // Endpoint not found
}

/**
 * @brief Notify load balancer that connection to endpoint ended
 */
int containerv_lb_connection_ended(struct containerv_load_balancer* lb, const char* instance_id) {
    if (!lb || !instance_id || !lb->endpoint_connection_count) {
        return -1;
    }
    
    mtx_lock(&lb->lock);
    
    for (int i = 0; i < lb->endpoint_count; i++) {
        if (strcmp(lb->endpoints[i].instance_id, instance_id) == 0) {
            if (lb->endpoint_connection_count[i] > 0) {
                lb->endpoint_connection_count[i]--;
            }
            mtx_unlock(&lb->lock);
            return 0;
        }
    }
    
    mtx_unlock(&lb->lock);
    return -1;
}

/**
 * @brief Get load balancer statistics
 */
int containerv_lb_get_stats(struct containerv_load_balancer* lb,
                           int* total_endpoints, int* healthy_endpoints,
                           int* total_requests) {
    if (!lb) {
        return -1;
    }
    
    mtx_lock(&lb->lock);
    
    if (total_endpoints) *total_endpoints = lb->endpoint_count;
    
    if (healthy_endpoints) {
        int healthy_count = 0;
        for (int i = 0; i < lb->endpoint_count; i++) {
            if (lb->endpoints[i].healthy) {
                healthy_count++;
            }
        }
        *healthy_endpoints = healthy_count;
    }
    
    if (total_requests && lb->endpoint_connection_count) {
        int total_connections = 0;
        for (int i = 0; i < lb->endpoint_count; i++) {
            total_connections += lb->endpoint_connection_count[i];
        }
        *total_requests = total_connections;
    }
    
    mtx_unlock(&lb->lock);
    return 0;
}