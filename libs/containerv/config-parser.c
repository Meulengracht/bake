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
#include <chef/yaml.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Helper functions for parsing YAML configuration
static int parse_service_config(yaml_node_t* service_node, yaml_document_t* document,
                               struct containerv_service* service);
static int parse_ports_config(yaml_node_t* ports_node, yaml_document_t* document,
                             struct containerv_service* service);
static int parse_environment_config(yaml_node_t* env_node, yaml_document_t* document,
                                   struct containerv_service* service);
static int parse_volumes_config(yaml_node_t* volumes_node, yaml_document_t* document,
                               struct containerv_service* service);
static int parse_depends_on_config(yaml_node_t* deps_node, yaml_document_t* document,
                                  struct containerv_service* service);
static int parse_healthcheck_config(yaml_node_t* health_node, yaml_document_t* document,
                                   struct containerv_healthcheck* healthcheck);
static int parse_networks_config(yaml_node_t* networks_node, yaml_document_t* document,
                                struct containerv_application* app);
static int parse_volumes_toplevel_config(yaml_node_t* volumes_node, yaml_document_t* document,
                                        struct containerv_application* app);
static int parse_secrets_config(yaml_node_t* secrets_node, yaml_document_t* document,
                               struct containerv_application* app);
static enum containerv_restart_policy parse_restart_policy(const char* policy_str);
static char* get_yaml_string_value(yaml_node_t* node, yaml_document_t* document);
static int get_yaml_int_value(yaml_node_t* node, yaml_document_t* document, int default_value);

/**
 * @brief Parse application configuration from YAML file
 */
int containerv_parse_application_config(const char* config_file,
                                       struct containerv_application** app) {
    if (!config_file || !app) {
        return -1;
    }
    
    FILE* file = fopen(config_file, "r");
    if (!file) {
        return -1;
    }
    
    // Parse YAML document
    yaml_parser_t parser;
    yaml_document_t document;
    
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        return -1;
    }
    
    yaml_parser_set_input_file(&parser, file);
    
    if (!yaml_parser_load(&parser, &document)) {
        yaml_parser_delete(&parser);
        fclose(file);
        return -1;
    }
    
    // Get root node
    yaml_node_t* root = yaml_document_get_root_node(&document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        yaml_document_delete(&document);
        yaml_parser_delete(&parser);
        fclose(file);
        return -1;
    }
    
    // Allocate application structure
    struct containerv_application* new_app = calloc(1, sizeof(struct containerv_application));
    if (!new_app) {
        yaml_document_delete(&document);
        yaml_parser_delete(&parser);
        fclose(file);
        return -1;
    }
    
    // Parse top-level configuration
    yaml_node_pair_t* pair;
    for (pair = root->data.mapping.pairs.start; 
         pair < root->data.mapping.pairs.top; pair++) {
        
        yaml_node_t* key = yaml_document_get_node(&document, pair->key);
        yaml_node_t* value = yaml_document_get_node(&document, pair->value);
        
        if (key->type != YAML_SCALAR_NODE) continue;
        
        const char* key_str = (const char*)key->data.scalar.value;
        
        if (strcmp(key_str, "version") == 0) {
            new_app->version = get_yaml_string_value(value, &document);
        } else if (strcmp(key_str, "name") == 0) {
            new_app->name = get_yaml_string_value(value, &document);
        } else if (strcmp(key_str, "services") == 0) {
            if (value->type == YAML_MAPPING_NODE) {
                // Count services first
                new_app->service_count = 0;
                yaml_node_pair_t* service_pair;
                for (service_pair = value->data.mapping.pairs.start;
                     service_pair < value->data.mapping.pairs.top; service_pair++) {
                    new_app->service_count++;
                }
                
                // Allocate service array
                new_app->services = calloc(new_app->service_count, 
                                          sizeof(struct containerv_service));
                if (!new_app->services) {
                    containerv_destroy_application(new_app);
                    yaml_document_delete(&document);
                    yaml_parser_delete(&parser);
                    fclose(file);
                    return -1;
                }
                
                // Parse each service
                int service_index = 0;
                for (service_pair = value->data.mapping.pairs.start;
                     service_pair < value->data.mapping.pairs.top; service_pair++) {
                    
                    yaml_node_t* service_key = yaml_document_get_node(&document, service_pair->key);
                    yaml_node_t* service_value = yaml_document_get_node(&document, service_pair->value);
                    
                    if (service_key->type == YAML_SCALAR_NODE) {
                        new_app->services[service_index].name = 
                            strdup((const char*)service_key->data.scalar.value);
                        
                        if (parse_service_config(service_value, &document,
                                                &new_app->services[service_index]) != 0) {
                            containerv_destroy_application(new_app);
                            yaml_document_delete(&document);
                            yaml_parser_delete(&parser);
                            fclose(file);
                            return -1;
                        }
                        service_index++;
                    }
                }
            }
        } else if (strcmp(key_str, "networks") == 0) {
            parse_networks_config(value, &document, new_app);
        } else if (strcmp(key_str, "volumes") == 0) {
            parse_volumes_toplevel_config(value, &document, new_app);
        } else if (strcmp(key_str, "secrets") == 0) {
            parse_secrets_config(value, &document, new_app);
        }
    }
    
    // Set default name if not provided
    if (!new_app->name) {
        new_app->name = strdup("chef-application");
    }
    
    // Set default version if not provided
    if (!new_app->version) {
        new_app->version = strdup("1.0");
    }
    
    *app = new_app;
    
    yaml_document_delete(&document);
    yaml_parser_delete(&parser);
    fclose(file);
    
    return 0;
}

/**
 * @brief Parse individual service configuration
 */
static int parse_service_config(yaml_node_t* service_node, yaml_document_t* document,
                               struct containerv_service* service) {
    if (!service_node || service_node->type != YAML_MAPPING_NODE || !service) {
        return -1;
    }
    
    // Set defaults
    service->replicas = 1;
    service->restart = CV_RESTART_ALWAYS;
    
    yaml_node_pair_t* pair;
    for (pair = service_node->data.mapping.pairs.start;
         pair < service_node->data.mapping.pairs.top; pair++) {
        
        yaml_node_t* key = yaml_document_get_node(document, pair->key);
        yaml_node_t* value = yaml_document_get_node(document, pair->value);
        
        if (key->type != YAML_SCALAR_NODE) continue;
        
        const char* key_str = (const char*)key->data.scalar.value;
        
        if (strcmp(key_str, "image") == 0) {
            service->image = get_yaml_string_value(value, document);
        } else if (strcmp(key_str, "command") == 0) {
            if (value->type == YAML_SEQUENCE_NODE) {
                yaml_node_item_t* item;
                int cmd_count = 0;
                
                // Count command arguments
                for (item = value->data.sequence.items.start;
                     item < value->data.sequence.items.top; item++) {
                    cmd_count++;
                }
                
                // Allocate command array (null-terminated)
                service->command = calloc(cmd_count + 1, sizeof(char*));
                
                int cmd_index = 0;
                for (item = value->data.sequence.items.start;
                     item < value->data.sequence.items.top; item++) {
                    yaml_node_t* cmd_node = yaml_document_get_node(document, *item);
                    service->command[cmd_index++] = get_yaml_string_value(cmd_node, document);
                }
            } else if (value->type == YAML_SCALAR_NODE) {
                // Single command string
                service->command = malloc(2 * sizeof(char*));
                service->command[0] = get_yaml_string_value(value, document);
                service->command[1] = NULL;
            }
        } else if (strcmp(key_str, "environment") == 0) {
            parse_environment_config(value, document, service);
        } else if (strcmp(key_str, "ports") == 0) {
            parse_ports_config(value, document, service);
        } else if (strcmp(key_str, "volumes") == 0) {
            parse_volumes_config(value, document, service);
        } else if (strcmp(key_str, "depends_on") == 0) {
            parse_depends_on_config(value, document, service);
        } else if (strcmp(key_str, "healthcheck") == 0) {
            service->healthcheck = malloc(sizeof(struct containerv_healthcheck));
            if (service->healthcheck) {
                memset(service->healthcheck, 0, sizeof(*service->healthcheck));
                parse_healthcheck_config(value, document, service->healthcheck);
            }
        } else if (strcmp(key_str, "restart") == 0) {
            char* restart_str = get_yaml_string_value(value, document);
            if (restart_str) {
                service->restart = parse_restart_policy(restart_str);
                free(restart_str);
            }
        } else if (strcmp(key_str, "replicas") == 0) {
            service->replicas = get_yaml_int_value(value, document, 1);
        } else if (strcmp(key_str, "privileged") == 0) {
            if (value->type == YAML_SCALAR_NODE) {
                const char* bool_str = (const char*)value->data.scalar.value;
                service->privileged = (strcmp(bool_str, "true") == 0 || strcmp(bool_str, "yes") == 0);
            }
        } else if (strcmp(key_str, "user") == 0) {
            service->user = get_yaml_string_value(value, document);
        } else if (strcmp(key_str, "working_dir") == 0) {
            service->working_dir = get_yaml_string_value(value, document);
        }
    }
    
    return 0;
}

/**
 * @brief Parse ports configuration
 */
static int parse_ports_config(yaml_node_t* ports_node, yaml_document_t* document,
                             struct containerv_service* service) {
    if (!ports_node || !service) {
        return -1;
    }
    
    if (ports_node->type == YAML_SEQUENCE_NODE) {
        yaml_node_item_t* item;
        int port_count = 0;
        
        // Count ports
        for (item = ports_node->data.sequence.items.start;
             item < ports_node->data.sequence.items.top; item++) {
            port_count++;
        }
        
        service->ports = calloc(port_count, sizeof(struct containerv_port_mapping));
        service->port_count = port_count;
        
        int port_index = 0;
        for (item = ports_node->data.sequence.items.start;
             item < ports_node->data.sequence.items.top; item++) {
            yaml_node_t* port_node = yaml_document_get_node(document, *item);
            
            if (port_node->type == YAML_SCALAR_NODE) {
                char* port_str = get_yaml_string_value(port_node, document);
                if (port_str) {
                    // Parse "host_port:container_port" format
                    char* colon = strchr(port_str, ':');
                    if (colon) {
                        *colon = '\0';
                        service->ports[port_index].host_port = atoi(port_str);
                        service->ports[port_index].container_port = atoi(colon + 1);
                    } else {
                        // Same port for host and container
                        int port = atoi(port_str);
                        service->ports[port_index].host_port = port;
                        service->ports[port_index].container_port = port;
                    }
                    
                    service->ports[port_index].protocol = strdup("tcp");
                    free(port_str);
                }
            }
            port_index++;
        }
    }
    
    return 0;
}

/**
 * @brief Parse environment variables configuration
 */
static int parse_environment_config(yaml_node_t* env_node, yaml_document_t* document,
                                   struct containerv_service* service) {
    if (!env_node || !service) {
        return -1;
    }
    
    if (env_node->type == YAML_SEQUENCE_NODE) {
        yaml_node_item_t* item;
        int env_count = 0;
        
        // Count environment variables
        for (item = env_node->data.sequence.items.start;
             item < env_node->data.sequence.items.top; item++) {
            env_count++;
        }
        
        // Allocate environment array (null-terminated)
        service->environment = calloc(env_count + 1, sizeof(char*));
        
        int env_index = 0;
        for (item = env_node->data.sequence.items.start;
             item < env_node->data.sequence.items.top; item++) {
            yaml_node_t* env_var_node = yaml_document_get_node(document, *item);
            service->environment[env_index++] = get_yaml_string_value(env_var_node, document);
        }
    } else if (env_node->type == YAML_MAPPING_NODE) {
        yaml_node_pair_t* pair;
        int env_count = 0;
        
        // Count environment variables
        for (pair = env_node->data.mapping.pairs.start;
             pair < env_node->data.mapping.pairs.top; pair++) {
            env_count++;
        }
        
        // Allocate environment array (null-terminated)
        service->environment = calloc(env_count + 1, sizeof(char*));
        
        int env_index = 0;
        for (pair = env_node->data.mapping.pairs.start;
             pair < env_node->data.mapping.pairs.top; pair++) {
            
            yaml_node_t* key = yaml_document_get_node(document, pair->key);
            yaml_node_t* value = yaml_document_get_node(document, pair->value);
            
            if (key->type == YAML_SCALAR_NODE) {
                char* key_str = get_yaml_string_value(key, document);
                char* value_str = get_yaml_string_value(value, document);
                
                if (key_str && value_str) {
                    size_t env_len = strlen(key_str) + strlen(value_str) + 2; // key=value\0
                    service->environment[env_index] = malloc(env_len);
                    snprintf(service->environment[env_index], env_len, "%s=%s", key_str, value_str);
                }
                
                free(key_str);
                free(value_str);
                env_index++;
            }
        }
    }
    
    return 0;
}

/**
 * @brief Parse health check configuration
 */
static int parse_healthcheck_config(yaml_node_t* health_node, yaml_document_t* document,
                                   struct containerv_healthcheck* healthcheck) {
    if (!health_node || health_node->type != YAML_MAPPING_NODE || !healthcheck) {
        return -1;
    }
    
    // Set defaults
    healthcheck->interval_seconds = 30;
    healthcheck->timeout_seconds = 10;
    healthcheck->retries = 3;
    healthcheck->start_period_seconds = 0;
    
    yaml_node_pair_t* pair;
    for (pair = health_node->data.mapping.pairs.start;
         pair < health_node->data.mapping.pairs.top; pair++) {
        
        yaml_node_t* key = yaml_document_get_node(document, pair->key);
        yaml_node_t* value = yaml_document_get_node(document, pair->value);
        
        if (key->type != YAML_SCALAR_NODE) continue;
        
        const char* key_str = (const char*)key->data.scalar.value;
        
        if (strcmp(key_str, "test") == 0) {
            if (value->type == YAML_SEQUENCE_NODE) {
                yaml_node_item_t* item;
                int cmd_count = 0;
                
                // Count test command parts
                for (item = value->data.sequence.items.start;
                     item < value->data.sequence.items.top; item++) {
                    cmd_count++;
                }
                
                // Allocate command array (null-terminated)
                healthcheck->test_command = calloc(cmd_count + 1, sizeof(char*));
                
                int cmd_index = 0;
                for (item = value->data.sequence.items.start;
                     item < value->data.sequence.items.top; item++) {
                    yaml_node_t* cmd_node = yaml_document_get_node(document, *item);
                    healthcheck->test_command[cmd_index++] = get_yaml_string_value(cmd_node, document);
                }
            }
        } else if (strcmp(key_str, "interval") == 0) {
            char* interval_str = get_yaml_string_value(value, document);
            if (interval_str) {
                // Parse duration string (e.g., "30s", "1m")
                int duration = atoi(interval_str);
                if (strstr(interval_str, "m")) {
                    duration *= 60; // Convert minutes to seconds
                }
                healthcheck->interval_seconds = duration;
                free(interval_str);
            }
        } else if (strcmp(key_str, "timeout") == 0) {
            char* timeout_str = get_yaml_string_value(value, document);
            if (timeout_str) {
                int duration = atoi(timeout_str);
                if (strstr(timeout_str, "m")) {
                    duration *= 60;
                }
                healthcheck->timeout_seconds = duration;
                free(timeout_str);
            }
        } else if (strcmp(key_str, "retries") == 0) {
            healthcheck->retries = get_yaml_int_value(value, document, 3);
        } else if (strcmp(key_str, "start_period") == 0) {
            char* period_str = get_yaml_string_value(value, document);
            if (period_str) {
                int duration = atoi(period_str);
                if (strstr(period_str, "m")) {
                    duration *= 60;
                }
                healthcheck->start_period_seconds = duration;
                free(period_str);
            }
        }
    }
    
    return 0;
}

/**
 * @brief Parse restart policy string
 */
static enum containerv_restart_policy parse_restart_policy(const char* policy_str) {
    if (!policy_str) {
        return CV_RESTART_NO;
    }
    
    if (strcmp(policy_str, "no") == 0) {
        return CV_RESTART_NO;
    } else if (strcmp(policy_str, "always") == 0) {
        return CV_RESTART_ALWAYS;
    } else if (strcmp(policy_str, "on-failure") == 0) {
        return CV_RESTART_ON_FAILURE;
    } else if (strcmp(policy_str, "unless-stopped") == 0) {
        return CV_RESTART_UNLESS_STOPPED;
    }
    
    return CV_RESTART_NO;
}

/**
 * @brief Get string value from YAML node
 */
static char* get_yaml_string_value(yaml_node_t* node, yaml_document_t* document) {
    (void)document; // Unused parameter
    
    if (!node || node->type != YAML_SCALAR_NODE) {
        return NULL;
    }
    
    return strdup((const char*)node->data.scalar.value);
}

/**
 * @brief Get integer value from YAML node
 */
static int get_yaml_int_value(yaml_node_t* node, yaml_document_t* document, int default_value) {
    (void)document; // Unused parameter
    
    if (!node || node->type != YAML_SCALAR_NODE) {
        return default_value;
    }
    
    return atoi((const char*)node->data.scalar.value);
}

/**
 * @brief Parse depends_on configuration (simplified implementation)
 */
static int parse_depends_on_config(yaml_node_t* deps_node, yaml_document_t* document,
                                  struct containerv_service* service) {
    if (!deps_node || !service) {
        return -1;
    }
    
    if (deps_node->type == YAML_SEQUENCE_NODE) {
        yaml_node_item_t* item;
        int dep_count = 0;
        
        // Count dependencies
        for (item = deps_node->data.sequence.items.start;
             item < deps_node->data.sequence.items.top; item++) {
            dep_count++;
        }
        
        service->depends_on = calloc(dep_count, sizeof(struct containerv_service_dependency));
        service->dependency_count = dep_count;
        
        int dep_index = 0;
        for (item = deps_node->data.sequence.items.start;
             item < deps_node->data.sequence.items.top; item++) {
            yaml_node_t* dep_node = yaml_document_get_node(document, *item);
            
            if (dep_node->type == YAML_SCALAR_NODE) {
                service->depends_on[dep_index].service_name = get_yaml_string_value(dep_node, document);
                service->depends_on[dep_index].required = true;
                service->depends_on[dep_index].timeout_seconds = 60; // Default timeout
            }
            dep_index++;
        }
    }
    
    return 0;
}

// Placeholder implementations for top-level configs (networks, volumes, secrets)
static int parse_networks_config(yaml_node_t* networks_node, yaml_document_t* document,
                                struct containerv_application* app) {
    (void)networks_node; (void)document; (void)app;
    // TODO: Implement network parsing
    return 0;
}

static int parse_volumes_toplevel_config(yaml_node_t* volumes_node, yaml_document_t* document,
                                        struct containerv_application* app) {
    (void)volumes_node; (void)document; (void)app;
    // TODO: Implement top-level volume parsing  
    return 0;
}

static int parse_volumes_config(yaml_node_t* volumes_node, yaml_document_t* document,
                               struct containerv_service* service) {
    (void)volumes_node; (void)document; (void)service;
    // TODO: Implement service volume parsing
    return 0;
}

static int parse_secrets_config(yaml_node_t* secrets_node, yaml_document_t* document,
                               struct containerv_application* app) {
    (void)secrets_node; (void)document; (void)app;
    // TODO: Implement secrets parsing
    return 0;
}