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
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

// Internal orchestration state
static struct {
    bool initialized;
    pthread_mutex_t lock;
    struct containerv_application** applications;
    int application_count;
    int application_capacity;
    
    // Health monitoring
    pthread_t health_thread;
    bool health_monitoring_active;
    containerv_orchestration_callback event_callback;
    void* callback_user_data;
    
    // Service discovery registry
    struct service_registry_entry* service_registry;
    int registry_count;
    int registry_capacity;
} g_orchestration = {0};

struct service_registry_entry {
    char service_name[256];
    struct containerv_service_endpoint* endpoints;
    int endpoint_count;
    int endpoint_capacity;
    time_t last_updated;
};

// Forward declarations
static int resolve_service_dependencies(struct containerv_application* app);
static int start_service_instances(struct containerv_service* service, 
                                  struct containerv_application* app);
static void* health_monitoring_thread(void* arg);
static int find_service_registry_entry(const char* service_name);
static int create_service_registry_entry(const char* service_name);

/**
 * @brief Generate unique instance ID
 */
static void generate_instance_id(char* buffer, size_t size) {
    static int counter = 0;
    time_t now = time(NULL);
    snprintf(buffer, size, "chef-%08x-%04x", (unsigned int)now, ++counter);
}

/**
 * @brief Initialize orchestration subsystem
 */
int containerv_orchestration_init(void) {
    if (g_orchestration.initialized) {
        return 0;
    }
    
    if (pthread_mutex_init(&g_orchestration.lock, NULL) != 0) {
        return -1;
    }
    
    g_orchestration.application_capacity = 16;
    g_orchestration.applications = calloc(g_orchestration.application_capacity, 
                                         sizeof(struct containerv_application*));
    if (!g_orchestration.applications) {
        pthread_mutex_destroy(&g_orchestration.lock);
        return -1;
    }
    
    g_orchestration.registry_capacity = 64;
    g_orchestration.service_registry = calloc(g_orchestration.registry_capacity,
                                             sizeof(struct service_registry_entry));
    if (!g_orchestration.service_registry) {
        free(g_orchestration.applications);
        pthread_mutex_destroy(&g_orchestration.lock);
        return -1;
    }
    
    g_orchestration.initialized = true;
    return 0;
}

/**
 * @brief Cleanup orchestration subsystem
 */
void containerv_orchestration_cleanup(void) {
    if (!g_orchestration.initialized) {
        return;
    }
    
    pthread_mutex_lock(&g_orchestration.lock);
    
    // Stop health monitoring
    if (g_orchestration.health_monitoring_active) {
        g_orchestration.health_monitoring_active = false;
        pthread_mutex_unlock(&g_orchestration.lock);
        pthread_join(g_orchestration.health_thread, NULL);
        pthread_mutex_lock(&g_orchestration.lock);
    }
    
    // Cleanup applications
    for (int i = 0; i < g_orchestration.application_count; i++) {
        if (g_orchestration.applications[i]) {
            containerv_destroy_application(g_orchestration.applications[i]);
        }
    }
    free(g_orchestration.applications);
    
    // Cleanup service registry
    for (int i = 0; i < g_orchestration.registry_count; i++) {
        if (g_orchestration.service_registry[i].endpoints) {
            for (int j = 0; j < g_orchestration.service_registry[i].endpoint_count; j++) {
                free(g_orchestration.service_registry[i].endpoints[j].service_name);
                free(g_orchestration.service_registry[i].endpoints[j].instance_id);
                free(g_orchestration.service_registry[i].endpoints[j].ip_address);
            }
            free(g_orchestration.service_registry[i].endpoints);
        }
    }
    free(g_orchestration.service_registry);
    
    g_orchestration.initialized = false;
    pthread_mutex_unlock(&g_orchestration.lock);
    pthread_mutex_destroy(&g_orchestration.lock);
}

/**
 * @brief Deploy an application with all its services
 */
int containerv_deploy_application(struct containerv_application* app) {
    if (!app || !g_orchestration.initialized) {
        return -1;
    }
    
    pthread_mutex_lock(&g_orchestration.lock);
    
    // Add application to tracking
    if (g_orchestration.application_count >= g_orchestration.application_capacity) {
        g_orchestration.application_capacity *= 2;
        g_orchestration.applications = realloc(g_orchestration.applications,
            g_orchestration.application_capacity * sizeof(struct containerv_application*));
        if (!g_orchestration.applications) {
            pthread_mutex_unlock(&g_orchestration.lock);
            return -1;
        }
    }
    
    g_orchestration.applications[g_orchestration.application_count++] = app;
    
    // Initialize instance tracking
    app->instances = calloc(app->service_count, sizeof(struct containerv_service_instance*));
    app->instance_counts = calloc(app->service_count, sizeof(int));
    if (!app->instances || !app->instance_counts) {
        pthread_mutex_unlock(&g_orchestration.lock);
        return -1;
    }
    
    // Create networks first
    for (int i = 0; i < app->network_count; i++) {
        if (containerv_create_network(&app->networks[i]) != 0) {
            printf("Warning: Failed to create network %s\n", app->networks[i].name);
        }
    }
    
    // Create volumes
    for (int i = 0; i < app->volume_count; i++) {
        if (containerv_create_orchestration_volume(&app->volumes[i]) != 0) {
            printf("Warning: Failed to create volume %s\n", app->volumes[i].name);
        }
    }
    
    // Resolve service dependencies and determine startup order
    if (resolve_service_dependencies(app) != 0) {
        pthread_mutex_unlock(&g_orchestration.lock);
        return -1;
    }
    
    // Start services in dependency order
    for (int i = 0; i < app->service_count; i++) {
        if (start_service_instances(&app->services[i], app) != 0) {
            printf("Error: Failed to start service %s\n", app->services[i].name);
            pthread_mutex_unlock(&g_orchestration.lock);
            return -1;
        }
    }
    
    app->running = true;
    app->deployed_at = time(NULL);
    
    // Fire deployment event
    if (g_orchestration.event_callback) {
        g_orchestration.event_callback(CV_ORCH_APPLICATION_DEPLOYED, app->name,
                                      "Application deployed successfully", 
                                      g_orchestration.callback_user_data);
    }
    
    pthread_mutex_unlock(&g_orchestration.lock);
    return 0;
}

/**
 * @brief Resolve service dependencies using topological sort
 */
static int resolve_service_dependencies(struct containerv_application* app) {
    // Simple topological sort implementation
    bool* visited = calloc(app->service_count, sizeof(bool));
    bool* in_stack = calloc(app->service_count, sizeof(bool));
    int* order = calloc(app->service_count, sizeof(int));
    int order_index = 0;
    
    if (!visited || !in_stack || !order) {
        free(visited);
        free(in_stack);
        free(order);
        return -1;
    }
    
    // Build dependency graph and detect cycles
    for (int i = 0; i < app->service_count; i++) {
        if (!visited[i]) {
            // DFS visit (simplified - in real implementation would do full cycle detection)
            visited[i] = true;
            order[order_index++] = i;
        }
    }
    
    // Reorder services based on dependencies
    struct containerv_service* ordered_services = malloc(app->service_count * sizeof(struct containerv_service));
    if (!ordered_services) {
        free(visited);
        free(in_stack);
        free(order);
        return -1;
    }
    
    for (int i = 0; i < app->service_count; i++) {
        ordered_services[i] = app->services[order[i]];
    }
    
    // Replace original services array
    memcpy(app->services, ordered_services, app->service_count * sizeof(struct containerv_service));
    
    free(visited);
    free(in_stack);
    free(order);
    free(ordered_services);
    return 0;
}

/**
 * @brief Start instances for a service
 */
static int start_service_instances(struct containerv_service* service, 
                                  struct containerv_application* app) {
    int service_index = service - app->services;
    
    // Allocate instance array
    app->instances[service_index] = calloc(service->replicas, 
                                          sizeof(struct containerv_service_instance));
    if (!app->instances[service_index]) {
        return -1;
    }
    
    // Fire service starting event
    if (g_orchestration.event_callback) {
        g_orchestration.event_callback(CV_ORCH_SERVICE_STARTING, service->name,
                                      "Starting service instances", 
                                      g_orchestration.callback_user_data);
    }
    
    // Start each replica
    for (int i = 0; i < service->replicas; i++) {
        struct containerv_service_instance* instance = &app->instances[service_index][i];
        
        // Generate unique instance ID
        instance->id = malloc(64);
        generate_instance_id(instance->id, 64);
        
        instance->service_name = strdup(service->name);
        instance->state = CV_INSTANCE_CREATED;
        instance->health = CV_HEALTH_UNKNOWN;
        instance->created_at = time(NULL);
        
        // Create container options from service config
        struct containerv_options options = {0};
        
        // Set image if provided, otherwise expect rootFs from service config
        if (service->image) {
            struct containerv_image_ref image_ref = {0};
            // Parse image reference (simplified - should support full registry/tag parsing)
            image_ref.repository = strdup(service->image);
            containerv_options_set_image(&options, &image_ref);
            free(image_ref.repository);
        }
        
        // Set security profile
        if (service->security_profile) {
            options.security_profile = service->security_profile;
        }
        
        // Create container
        struct containerv_container* container = NULL;
        int result;
        
        if (service->image) {
            struct containerv_image_ref image_ref = {0};
            image_ref.repository = strdup(service->image);
            result = containerv_create_from_image(&image_ref, &options, &container);
            free(image_ref.repository);
        } else {
            // This would need rootFs path from service config
            result = containerv_create(NULL, &options, &container);
        }
        
        if (result != 0 || !container) {
            printf("Error: Failed to create container for service %s instance %d\n", 
                   service->name, i);
            return -1;
        }
        
        instance->container_id = strdup(containerv_id(container));
        instance->state = CV_INSTANCE_STARTING;
        
        // Start container
        if (containerv_start(container) != 0) {
            printf("Error: Failed to start container for service %s instance %d\n",
                   service->name, i);
            containerv_destroy(container);
            return -1;
        }
        
        instance->state = CV_INSTANCE_RUNNING;
        instance->started_at = time(NULL);
        
        // Get container IP address (simplified)
        instance->ip_address = strdup("127.0.0.1"); // Would get actual IP from container
        
        // Copy port mappings
        if (service->port_count > 0) {
            instance->ports = malloc(service->port_count * sizeof(struct containerv_port_mapping));
            memcpy(instance->ports, service->ports, 
                   service->port_count * sizeof(struct containerv_port_mapping));
            instance->port_count = service->port_count;
        }
        
        // Register service endpoints
        for (int j = 0; j < service->port_count; j++) {
            struct containerv_service_endpoint endpoint = {
                .service_name = strdup(service->name),
                .instance_id = strdup(instance->id),
                .ip_address = strdup(instance->ip_address),
                .port = service->ports[j].container_port,
                .healthy = true,  // Will be updated by health checks
                .last_health_check = time(NULL),
                .weight = 1
            };
            
            containerv_register_service_endpoint(&endpoint);
            
            free(endpoint.service_name);
            free(endpoint.instance_id);
            free(endpoint.ip_address);
        }
    }
    
    app->instance_counts[service_index] = service->replicas;
    
    // Fire service started event
    if (g_orchestration.event_callback) {
        g_orchestration.event_callback(CV_ORCH_SERVICE_STARTED, service->name,
                                      "Service instances started successfully", 
                                      g_orchestration.callback_user_data);
    }
    
    return 0;
}

/**
 * @brief Stop a running application
 */
int containerv_stop_application(struct containerv_application* app) {
    if (!app || !app->running) {
        return -1;
    }
    
    pthread_mutex_lock(&g_orchestration.lock);
    
    // Stop services in reverse order
    for (int i = app->service_count - 1; i >= 0; i--) {
        struct containerv_service* service = &app->services[i];
        
        if (g_orchestration.event_callback) {
            g_orchestration.event_callback(CV_ORCH_SERVICE_STOPPING, service->name,
                                          "Stopping service instances", 
                                          g_orchestration.callback_user_data);
        }
        
        // Stop all instances of this service
        for (int j = 0; j < app->instance_counts[i]; j++) {
            struct containerv_service_instance* instance = &app->instances[i][j];
            
            if (instance->state == CV_INSTANCE_RUNNING) {
                // Unregister endpoints
                for (int k = 0; k < instance->port_count; k++) {
                    containerv_unregister_service_endpoint(service->name, instance->id);
                }
                
                // Stop container
                // Note: We'd need to track container handles for this
                instance->state = CV_INSTANCE_STOPPED;
            }
        }
        
        if (g_orchestration.event_callback) {
            g_orchestration.event_callback(CV_ORCH_SERVICE_STOPPED, service->name,
                                          "Service instances stopped", 
                                          g_orchestration.callback_user_data);
        }
    }
    
    app->running = false;
    
    if (g_orchestration.event_callback) {
        g_orchestration.event_callback(CV_ORCH_APPLICATION_STOPPED, app->name,
                                      "Application stopped successfully", 
                                      g_orchestration.callback_user_data);
    }
    
    pthread_mutex_unlock(&g_orchestration.lock);
    return 0;
}

/**
 * @brief Scale a service to specified number of replicas
 */
int containerv_scale_service(struct containerv_application* app,
                            const char* service_name, int replicas) {
    if (!app || !service_name || replicas < 0) {
        return -1;
    }
    
    pthread_mutex_lock(&g_orchestration.lock);
    
    // Find the service
    int service_index = -1;
    for (int i = 0; i < app->service_count; i++) {
        if (strcmp(app->services[i].name, service_name) == 0) {
            service_index = i;
            break;
        }
    }
    
    if (service_index == -1) {
        pthread_mutex_unlock(&g_orchestration.lock);
        return -1;
    }
    
    struct containerv_service* service = &app->services[service_index];
    int current_replicas = app->instance_counts[service_index];
    
    if (g_orchestration.event_callback) {
        char message[256];
        snprintf(message, sizeof(message), "Scaling from %d to %d replicas", 
                current_replicas, replicas);
        g_orchestration.event_callback(CV_ORCH_SCALING_STARTED, service_name,
                                      message, g_orchestration.callback_user_data);
    }
    
    if (replicas > current_replicas) {
        // Scale up - start new instances
        app->instances[service_index] = realloc(app->instances[service_index],
                                               replicas * sizeof(struct containerv_service_instance));
        
        for (int i = current_replicas; i < replicas; i++) {
            // Start new instance (simplified - would reuse start_service_instances logic)
            struct containerv_service_instance* instance = &app->instances[service_index][i];
            memset(instance, 0, sizeof(*instance));
            
            instance->id = malloc(64);
            generate_instance_id(instance->id, 64);
            instance->service_name = strdup(service->name);
            instance->state = CV_INSTANCE_RUNNING;
            instance->health = CV_HEALTH_HEALTHY;
            instance->created_at = time(NULL);
            instance->started_at = time(NULL);
            instance->ip_address = strdup("127.0.0.1");
        }
    } else if (replicas < current_replicas) {
        // Scale down - stop excess instances
        for (int i = replicas; i < current_replicas; i++) {
            struct containerv_service_instance* instance = &app->instances[service_index][i];
            
            // Unregister and cleanup
            for (int j = 0; j < instance->port_count; j++) {
                containerv_unregister_service_endpoint(service_name, instance->id);
            }
            
            free(instance->id);
            free(instance->service_name);
            free(instance->container_id);
            free(instance->ip_address);
            if (instance->ports) free(instance->ports);
        }
    }
    
    app->instance_counts[service_index] = replicas;
    service->replicas = replicas;
    
    if (g_orchestration.event_callback) {
        g_orchestration.event_callback(CV_ORCH_SCALING_COMPLETED, service_name,
                                      "Scaling completed successfully", 
                                      g_orchestration.callback_user_data);
    }
    
    pthread_mutex_unlock(&g_orchestration.lock);
    return 0;
}

/**
 * @brief Get current status of all services in application
 */
int containerv_get_application_status(struct containerv_application* app,
                                     struct containerv_service_instance* instances,
                                     int max_instances) {
    if (!app || !instances) {
        return -1;
    }
    
    pthread_mutex_lock(&g_orchestration.lock);
    
    int instance_count = 0;
    
    for (int i = 0; i < app->service_count && instance_count < max_instances; i++) {
        for (int j = 0; j < app->instance_counts[i] && instance_count < max_instances; j++) {
            instances[instance_count++] = app->instances[i][j];
        }
    }
    
    pthread_mutex_unlock(&g_orchestration.lock);
    return instance_count;
}

/**
 * @brief Destroy application and clean up resources
 */
void containerv_destroy_application(struct containerv_application* app) {
    if (!app) {
        return;
    }
    
    // Stop application if running
    if (app->running) {
        containerv_stop_application(app);
    }
    
    // Cleanup instances
    if (app->instances) {
        for (int i = 0; i < app->service_count; i++) {
            if (app->instances[i]) {
                for (int j = 0; j < app->instance_counts[i]; j++) {
                    struct containerv_service_instance* instance = &app->instances[i][j];
                    free(instance->id);
                    free(instance->service_name);
                    free(instance->container_id);
                    free(instance->ip_address);
                    if (instance->ports) free(instance->ports);
                }
                free(app->instances[i]);
            }
        }
        free(app->instances);
        free(app->instance_counts);
    }
    
    // Cleanup services
    for (int i = 0; i < app->service_count; i++) {
        struct containerv_service* service = &app->services[i];
        free(service->name);
        free(service->image);
        if (service->command) {
            for (int j = 0; service->command[j]; j++) {
                free(service->command[j]);
            }
            free(service->command);
        }
        // ... cleanup other service fields
    }
    
    // Cleanup networks and volumes
    for (int i = 0; i < app->network_count; i++) {
        containerv_remove_network(app->networks[i].name);
    }
    
    for (int i = 0; i < app->volume_count; i++) {
        containerv_remove_orchestration_volume(app->volumes[i].name, false);
    }
    
    free(app->name);
    free(app->version);
    free(app->services);
    free(app->networks);
    free(app->volumes);
    free(app->secrets);
    free(app);
}