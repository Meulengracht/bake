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
#include <unistd.h>
#include <time.h>

// Orchestration event callback
void orchestration_event_handler(enum containerv_orchestration_event event,
                                const char* service_name,
                                const char* message,
                                void* user_data) {
    const char* app_name = (const char*)user_data;
    time_t now = time(NULL);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", localtime(&now));
    
    const char* event_name;
    switch (event) {
        case CV_ORCH_SERVICE_STARTING:      event_name = "SERVICE_STARTING"; break;
        case CV_ORCH_SERVICE_STARTED:       event_name = "SERVICE_STARTED"; break;
        case CV_ORCH_SERVICE_STOPPING:      event_name = "SERVICE_STOPPING"; break;
        case CV_ORCH_SERVICE_STOPPED:       event_name = "SERVICE_STOPPED"; break;
        case CV_ORCH_SERVICE_FAILED:        event_name = "SERVICE_FAILED"; break;
        case CV_ORCH_SERVICE_UNHEALTHY:     event_name = "SERVICE_UNHEALTHY"; break;
        case CV_ORCH_SERVICE_HEALTHY:       event_name = "SERVICE_HEALTHY"; break;
        case CV_ORCH_APPLICATION_DEPLOYED:  event_name = "APPLICATION_DEPLOYED"; break;
        case CV_ORCH_APPLICATION_STOPPED:   event_name = "APPLICATION_STOPPED"; break;
        case CV_ORCH_SCALING_STARTED:       event_name = "SCALING_STARTED"; break;
        case CV_ORCH_SCALING_COMPLETED:     event_name = "SCALING_COMPLETED"; break;
        case CV_ORCH_DEPENDENCY_TIMEOUT:    event_name = "DEPENDENCY_TIMEOUT"; break;
        default:                            event_name = "UNKNOWN"; break;
    }
    
    printf("[%s] [%s] %s: %s - %s\n", 
           timestamp, app_name, event_name, service_name, message);
}

// Example: Create a multi-service web application programmatically
int example_create_web_application(void) {
    printf("=== Creating Multi-Service Web Application ===\n");
    
    // Create application structure
    struct containerv_application* app = calloc(1, sizeof(struct containerv_application));
    app->name = strdup("web-app");
    app->version = strdup("1.0");
    
    // Create three services: database, API, and web frontend
    app->service_count = 3;
    app->services = calloc(3, sizeof(struct containerv_service));
    
    // 1. Database service (PostgreSQL)
    struct containerv_service* db_service = &app->services[0];
    db_service->name = strdup("database");
    db_service->image = strdup("postgres:13");
    db_service->replicas = 1;
    db_service->restart = CV_RESTART_ALWAYS;
    
    // Database environment
    db_service->environment = calloc(4, sizeof(char*));
    db_service->environment[0] = strdup("POSTGRES_DB=webapp");
    db_service->environment[1] = strdup("POSTGRES_USER=webuser");
    db_service->environment[2] = strdup("POSTGRES_PASSWORD=secret123");
    db_service->environment[3] = NULL;
    
    // Database health check
    db_service->healthcheck = malloc(sizeof(struct containerv_healthcheck));
    db_service->healthcheck->test_command = calloc(4, sizeof(char*));
    db_service->healthcheck->test_command[0] = strdup("CMD-SHELL");
    db_service->healthcheck->test_command[1] = strdup("pg_isready");
    db_service->healthcheck->test_command[2] = strdup("-U webuser -d webapp");
    db_service->healthcheck->test_command[3] = NULL;
    db_service->healthcheck->interval_seconds = 30;
    db_service->healthcheck->timeout_seconds = 5;
    db_service->healthcheck->retries = 5;
    db_service->healthcheck->start_period_seconds = 10;
    
    // 2. API service
    struct containerv_service* api_service = &app->services[1];
    api_service->name = strdup("api");
    api_service->image = strdup("webapp-api:latest");
    api_service->replicas = 2;
    api_service->restart = CV_RESTART_ON_FAILURE;
    
    // API dependencies
    api_service->depends_on = calloc(1, sizeof(struct containerv_service_dependency));
    api_service->dependency_count = 1;
    api_service->depends_on[0].service_name = strdup("database");
    api_service->depends_on[0].required = true;
    api_service->depends_on[0].timeout_seconds = 60;
    
    // API environment
    api_service->environment = calloc(2, sizeof(char*));
    api_service->environment[0] = strdup("DATABASE_URL=postgresql://webuser:secret123@database:5432/webapp");
    api_service->environment[1] = NULL;
    
    // API ports
    api_service->ports = calloc(1, sizeof(struct containerv_port_mapping));
    api_service->port_count = 1;
    api_service->ports[0].host_port = 0; // Dynamic port
    api_service->ports[0].container_port = 8080;
    api_service->ports[0].protocol = strdup("tcp");
    
    // API health check
    api_service->healthcheck = malloc(sizeof(struct containerv_healthcheck));
    api_service->healthcheck->test_command = calloc(4, sizeof(char*));
    api_service->healthcheck->test_command[0] = strdup("CMD");
    api_service->healthcheck->test_command[1] = strdup("curl");
    api_service->healthcheck->test_command[2] = strdup("-f http://localhost:8080/health");
    api_service->healthcheck->test_command[3] = NULL;
    api_service->healthcheck->interval_seconds = 15;
    api_service->healthcheck->timeout_seconds = 5;
    api_service->healthcheck->retries = 3;
    api_service->healthcheck->start_period_seconds = 30;
    
    // 3. Web frontend service
    struct containerv_service* web_service = &app->services[2];
    web_service->name = strdup("web");
    web_service->image = strdup("nginx:alpine");
    web_service->replicas = 2;
    web_service->restart = CV_RESTART_ALWAYS;
    
    // Web dependencies
    web_service->depends_on = calloc(1, sizeof(struct containerv_service_dependency));
    web_service->dependency_count = 1;
    web_service->depends_on[0].service_name = strdup("api");
    web_service->depends_on[0].required = true;
    web_service->depends_on[0].timeout_seconds = 30;
    
    // Web ports
    web_service->ports = calloc(1, sizeof(struct containerv_port_mapping));
    web_service->port_count = 1;
    web_service->ports[0].host_port = 8080;
    web_service->ports[0].container_port = 80;
    web_service->ports[0].protocol = strdup("tcp");
    
    // Web health check
    web_service->healthcheck = malloc(sizeof(struct containerv_healthcheck));
    web_service->healthcheck->test_command = calloc(4, sizeof(char*));
    web_service->healthcheck->test_command[0] = strdup("CMD");
    web_service->healthcheck->test_command[1] = strdup("curl");
    web_service->healthcheck->test_command[2] = strdup("-f http://localhost/health");
    web_service->healthcheck->test_command[3] = NULL;
    web_service->healthcheck->interval_seconds = 30;
    web_service->healthcheck->timeout_seconds = 10;
    web_service->healthcheck->retries = 3;
    
    // Initialize orchestration system
    if (containerv_orchestration_init() != 0) {
        printf("Error: Failed to initialize orchestration system\n");
        containerv_destroy_application(app);
        return -1;
    }
    
    // Initialize service discovery
    if (containerv_service_discovery_init() != 0) {
        printf("Error: Failed to initialize service discovery\n");
        containerv_destroy_application(app);
        return -1;
    }
    
    // Deploy the application
    printf("Deploying web application...\n");
    if (containerv_deploy_application(app) != 0) {
        printf("Error: Failed to deploy application\n");
        containerv_destroy_application(app);
        return -1;
    }
    
    // Start health monitoring
    printf("Starting health monitoring...\n");
    if (containerv_start_health_monitoring(app, orchestration_event_handler, app->name) != 0) {
        printf("Warning: Failed to start health monitoring\n");
    }
    
    printf("Application deployed successfully!\n");
    printf("Services:\n");
    printf("  - Database: 1 replica (postgres:13)\n");
    printf("  - API: 2 replicas (webapp-api:latest)\n");
    printf("  - Web: 2 replicas (nginx:alpine) on port 8080\n");
    printf("\nApplication is running. Press Enter to continue with scaling demo...\n");
    getchar();
    
    return 0;
}

// Example: Service scaling demonstration
int example_service_scaling(struct containerv_application* app) {
    printf("\n=== Service Scaling Demo ===\n");
    
    // Scale up API service
    printf("Scaling API service from 2 to 4 replicas...\n");
    if (containerv_scale_service(app, "api", 4) == 0) {
        printf("API service scaled to 4 replicas successfully\n");
    } else {
        printf("Error: Failed to scale API service\n");
    }
    
    sleep(2);
    
    // Scale down web service
    printf("Scaling web service from 2 to 1 replica...\n");
    if (containerv_scale_service(app, "web", 1) == 0) {
        printf("Web service scaled to 1 replica successfully\n");
    } else {
        printf("Error: Failed to scale web service\n");
    }
    
    sleep(1);
    
    // Get application status
    printf("\nCurrent application status:\n");
    struct containerv_service_instance instances[16];
    int instance_count = containerv_get_application_status(app, instances, 16);
    
    if (instance_count > 0) {
        for (int i = 0; i < instance_count; i++) {
            const char* state_str;
            switch (instances[i].state) {
                case CV_INSTANCE_RUNNING: state_str = "RUNNING"; break;
                case CV_INSTANCE_STARTING: state_str = "STARTING"; break;
                case CV_INSTANCE_STOPPED: state_str = "STOPPED"; break;
                case CV_INSTANCE_FAILED: state_str = "FAILED"; break;
                default: state_str = "UNKNOWN"; break;
            }
            
            const char* health_str;
            switch (instances[i].health) {
                case CV_HEALTH_HEALTHY: health_str = "HEALTHY"; break;
                case CV_HEALTH_UNHEALTHY: health_str = "UNHEALTHY"; break;
                case CV_HEALTH_STARTING: health_str = "STARTING"; break;
                case CV_HEALTH_UNKNOWN: health_str = "UNKNOWN"; break;
                default: health_str = "NONE"; break;
            }
            
            printf("  %s[%s]: %s, %s, IP: %s\n",
                   instances[i].service_name, instances[i].id,
                   state_str, health_str, instances[i].ip_address);
        }
    }
    
    printf("\nPress Enter to continue with service discovery demo...\n");
    getchar();
    
    return 0;
}

// Example: Service discovery demonstration
int example_service_discovery(void) {
    printf("\n=== Service Discovery Demo ===\n");
    
    // Discover API service endpoints
    printf("Discovering API service endpoints...\n");
    struct containerv_service_endpoint endpoints[10];
    int endpoint_count = containerv_discover_service_endpoints("api", endpoints, 10);
    
    if (endpoint_count > 0) {
        printf("Found %d API service endpoints:\n", endpoint_count);
        for (int i = 0; i < endpoint_count; i++) {
            printf("  Instance %s: %s:%d (healthy: %s)\n",
                   endpoints[i].instance_id,
                   endpoints[i].ip_address,
                   endpoints[i].port,
                   endpoints[i].healthy ? "yes" : "no");
            
            // Cleanup endpoint strings
            free(endpoints[i].service_name);
            free(endpoints[i].instance_id);
            free(endpoints[i].ip_address);
        }
    } else {
        printf("No API service endpoints found\n");
    }
    
    // Resolve service address
    char ip_address[16];
    int port;
    if (containerv_resolve_service_address("api", ip_address, &port) == 0) {
        printf("\nResolved 'api' service to: %s:%d\n", ip_address, port);
    } else {
        printf("\nFailed to resolve 'api' service address\n");
    }
    
    printf("\nPress Enter to continue with load balancing demo...\n");
    getchar();
    
    return 0;
}

// Example: Load balancing demonstration
int example_load_balancing(void) {
    printf("\n=== Load Balancing Demo ===\n");
    
    // Create load balancer for API service
    struct containerv_load_balancer* lb;
    if (containerv_create_load_balancer("api", CV_LB_ROUND_ROBIN, &lb) == 0) {
        printf("Created round-robin load balancer for API service\n");
        
        // Simulate multiple requests
        printf("Simulating 5 load-balanced requests:\n");
        for (int i = 0; i < 5; i++) {
            struct containerv_service_endpoint endpoint;
            if (containerv_lb_get_endpoint(lb, NULL, &endpoint) == 0) {
                printf("  Request %d -> %s:%d (instance: %s)\n",
                       i + 1, endpoint.ip_address, endpoint.port, endpoint.instance_id);
                
                // Cleanup
                free(endpoint.service_name);
                free(endpoint.instance_id);
                free(endpoint.ip_address);
            } else {
                printf("  Request %d -> No healthy endpoints available\n", i + 1);
            }
        }
        
        // Get load balancer stats
        int total_endpoints, healthy_endpoints, total_requests;
        if (containerv_lb_get_stats(lb, &total_endpoints, &healthy_endpoints, &total_requests) == 0) {
            printf("\nLoad Balancer Stats:\n");
            printf("  Total endpoints: %d\n", total_endpoints);
            printf("  Healthy endpoints: %d\n", healthy_endpoints);
            printf("  Total requests handled: %d\n", total_requests);
        }
        
        containerv_destroy_load_balancer(lb);
    } else {
        printf("Error: Failed to create load balancer\n");
    }
    
    printf("\nPress Enter to continue with health monitoring demo...\n");
    getchar();
    
    return 0;
}

// Example: Health monitoring demonstration
int example_health_monitoring(void) {
    printf("\n=== Health Monitoring Demo ===\n");
    
    // Check health status of all services
    const char* services[] = {"database", "api", "web"};
    
    for (int i = 0; i < 3; i++) {
        enum containerv_health_status health;
        if (containerv_get_service_health(services[i], &health) == 0) {
            const char* health_str;
            switch (health) {
                case CV_HEALTH_HEALTHY: health_str = "HEALTHY"; break;
                case CV_HEALTH_UNHEALTHY: health_str = "UNHEALTHY"; break;
                case CV_HEALTH_STARTING: health_str = "STARTING"; break;
                case CV_HEALTH_UNKNOWN: health_str = "UNKNOWN"; break;
                default: health_str = "NONE"; break;
            }
            printf("Service %s: %s\n", services[i], health_str);
        } else {
            printf("Service %s: No health check configured\n", services[i]);
        }
    }
    
    // Trigger manual health check
    printf("\nTriggering manual health check for API service...\n");
    if (containerv_trigger_health_check("api", NULL) == 0) {
        printf("Manual health check triggered for all API instances\n");
    } else {
        printf("Failed to trigger health check\n");
    }
    
    printf("\nPress Enter to stop the application...\n");
    getchar();
    
    return 0;
}

int main(int argc, char* argv[]) {
    printf("Chef Container Orchestration Demo\n");
    printf("=================================\n\n");
    
    if (argc > 1 && strcmp(argv[1], "--config") == 0 && argc > 2) {
        // Parse from YAML configuration file
        printf("Loading application from YAML config: %s\n", argv[2]);
        
        struct containerv_application* app;
        if (containerv_parse_application_config(argv[2], &app) == 0) {
            printf("Successfully loaded application: %s (version %s)\n", 
                   app->name, app->version);
            printf("Services: %d\n", app->service_count);
            for (int i = 0; i < app->service_count; i++) {
                printf("  - %s: %s (%d replicas)\n",
                       app->services[i].name,
                       app->services[i].image ? app->services[i].image : "no image",
                       app->services[i].replicas);
            }
            
            containerv_destroy_application(app);
        } else {
            printf("Error: Failed to parse YAML configuration\n");
            return 1;
        }
    } else {
        // Run interactive demo
        struct containerv_application* app = NULL;
        
        // Create and deploy application
        if (example_create_web_application() != 0) {
            return 1;
        }
        
        // Get the deployed application (in a real implementation, we'd track this properly)
        // For demo purposes, we'll create a simple reference
        app = calloc(1, sizeof(struct containerv_application));
        app->name = strdup("web-app");
        app->service_count = 3;
        
        // Run demos
        example_service_scaling(app);
        example_service_discovery();
        example_load_balancing();
        example_health_monitoring();
        
        // Stop application
        printf("\n=== Stopping Application ===\n");
        if (containerv_stop_application(app) == 0) {
            printf("Application stopped successfully\n");
        } else {
            printf("Error stopping application\n");
        }
        
        // Cleanup
        containerv_stop_health_monitoring(app);
        containerv_destroy_application(app);
        containerv_service_discovery_cleanup();
        containerv_orchestration_cleanup();
        
        free(app);
    }
    
    printf("\nOrchestration demo completed!\n");
    return 0;
}