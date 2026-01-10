/**
 * Example: Using Per-Container Policy Specification with CVD
 * 
 * This example demonstrates how to specify security policies when creating
 * containers through the CVD daemon API.
 */

#include <chef/cvd.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/**
 * Example 1: Creating a container with minimal policy (default)
 */
void example_minimal_container(gracht_client_t* client)
{
    printf("=== Example 1: Container with Minimal Policy ===\n");
    printf("Using default minimal policy from CVD configuration.\n\n");
    
    struct chef_create_parameters params;
    struct chef_layer_descriptor layer;
    struct gracht_message_context context;
    char container_id[64];
    enum chef_status status;
    
    // Initialize parameters
    chef_create_parameters_init(&params);
    params.id = platform_strdup("example-minimal");
    
    // Add a base rootfs layer
    chef_create_parameters_layers_add(&params, 1);
    struct chef_layer_descriptor* base_layer = chef_create_parameters_layers_get(&params, 0);
    base_layer->type = CHEF_LAYER_TYPE_BASE_ROOTFS;
    base_layer->source = platform_strdup("/var/chef/rootfs/ubuntu-base");
    base_layer->options = 0;
    
    // Leave policy.profiles empty to use default from configuration
    params.policy.profiles = NULL;
    
    printf("Creating container with:\n");
    printf("  - ID: %s\n", params.id);
    printf("  - Policy: (using CVD configuration default)\n\n");
    
    // Create the container
    int result = chef_cvd_create(client, &context, &params);
    chef_create_parameters_destroy(&params);
    
    if (result == 0) {
        gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
        chef_cvd_create_result(client, &context, &container_id[0], sizeof(container_id) - 1, &status);
        
        if (status == CHEF_STATUS_SUCCESS) {
            printf("✓ Container created successfully: %s\n", container_id);
        } else {
            printf("✗ Container creation failed with status: %d\n", status);
        }
    } else {
        printf("✗ Failed to send create request\n");
    }
    printf("\n");
}

/**
 * Example 2: Creating a container with build policy
 */
void example_build_container(gracht_client_t* client)
{
    printf("=== Example 2: Container with Build Policy ===\n");
    printf("Explicitly requesting build policy for compilation workload.\n\n");
    
    struct chef_create_parameters params;
    struct gracht_message_context context;
    char container_id[64];
    enum chef_status status;
    
    // Initialize parameters
    chef_create_parameters_init(&params);
    params.id = platform_strdup("example-build");
    
    // Add base rootfs layer
    chef_create_parameters_layers_add(&params, 1);
    struct chef_layer_descriptor* base_layer = chef_create_parameters_layers_get(&params, 0);
    base_layer->type = CHEF_LAYER_TYPE_BASE_ROOTFS;
    base_layer->source = platform_strdup("/var/chef/rootfs/ubuntu-base");
    base_layer->options = 0;
    
    // Specify build policy
    params.policy.profiles = platform_strdup("build");
    
    printf("Creating container with:\n");
    printf("  - ID: %s\n", params.id);
    printf("  - Policy: build\n");
    printf("  - Capabilities: fork, exec, file creation, etc.\n\n");
    
    // Create the container
    int result = chef_cvd_create(client, &context, &params);
    chef_create_parameters_destroy(&params);
    
    if (result == 0) {
        gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
        chef_cvd_create_result(client, &context, &container_id[0], sizeof(container_id) - 1, &status);
        
        if (status == CHEF_STATUS_SUCCESS) {
            printf("✓ Container created successfully: %s\n", container_id);
            printf("  Ready for compilation and build tasks\n");
        } else {
            printf("✗ Container creation failed with status: %d\n", status);
        }
    } else {
        printf("✗ Failed to send create request\n");
    }
    printf("\n");
}

/**
 * Example 3: Creating a container with network policy
 */
void example_network_container(gracht_client_t* client)
{
    printf("=== Example 3: Container with Network Policy ===\n");
    printf("Explicitly requesting network policy for web service.\n\n");
    
    struct chef_create_parameters params;
    struct gracht_message_context context;
    char container_id[64];
    enum chef_status status;
    
    // Initialize parameters
    chef_create_parameters_init(&params);
    params.id = platform_strdup("example-network");
    
    // Add base rootfs layer
    chef_create_parameters_layers_add(&params, 1);
    struct chef_layer_descriptor* base_layer = chef_create_parameters_layers_get(&params, 0);
    base_layer->type = CHEF_LAYER_TYPE_BASE_ROOTFS;
    base_layer->source = platform_strdup("/var/chef/rootfs/ubuntu-base");
    base_layer->options = 0;
    
    // Specify network policy
    params.policy.profiles = platform_strdup("network");
    
    printf("Creating container with:\n");
    printf("  - ID: %s\n", params.id);
    printf("  - Policy: network\n");
    printf("  - Capabilities: socket, bind, connect, etc.\n\n");
    
    // Create the container
    int result = chef_cvd_create(client, &context, &params);
    chef_create_parameters_destroy(&params);
    
    if (result == 0) {
        gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
        chef_cvd_create_result(client, &context, &container_id[0], sizeof(container_id) - 1, &status);
        
        if (status == CHEF_STATUS_SUCCESS) {
            printf("✓ Container created successfully: %s\n", container_id);
            printf("  Ready for network operations\n");
        } else {
            printf("✗ Container creation failed with status: %d\n", status);
        }
    } else {
        printf("✗ Failed to send create request\n");
    }
    printf("\n");
}

/**
 * Example 4: Policy composition - combining multiple features
 */
void example_composed_container(gracht_client_t* client)
{
    printf("=== Example 4: Container with Composed Policies ===\n");
    printf("Combining build and network features for a CI/CD pipeline.\n\n");
    
    struct chef_create_parameters params;
    struct gracht_message_context context;
    char container_id[64];
    enum chef_status status;
    
    // Initialize parameters
    chef_create_parameters_init(&params);
    params.id = platform_strdup("example-ci-pipeline");
    
    // Add base rootfs layer
    chef_create_parameters_layers_add(&params, 1);
    struct chef_layer_descriptor* base_layer = chef_create_parameters_layers_get(&params, 0);
    base_layer->type = CHEF_LAYER_TYPE_BASE_ROOTFS;
    base_layer->source = platform_strdup("/var/chef/rootfs/ubuntu-base");
    base_layer->options = 0;
    
    // Specify multiple policy features (composable)
    params.policy.profiles = platform_strdup("build,network");
    
    printf("Creating container with:\n");
    printf("  - ID: %s\n", params.id);
    printf("  - Policy: build,network (composed)\n");
    printf("  - Features: Minimal (base) + build paths + network paths\n\n");
    
    // Create the container
    int result = chef_cvd_create(client, &context, &params);
    chef_create_parameters_destroy(&params);
    
    if (result == 0) {
        gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
        chef_cvd_create_result(client, &context, &container_id[0], sizeof(container_id) - 1, &status);
        
        if (status == CHEF_STATUS_SUCCESS) {
            printf("✓ Container created successfully: %s\n", container_id);
            printf("  Ready for builds that require network access\n");
        } else {
            printf("✗ Container creation failed with status: %d\n", status);
        }
    } else {
        printf("✗ Failed to send create request\n");
    }
    printf("\n");
}

/**
 * Example 5: Policy override behavior
 */
void example_policy_override_explanation(void)
{
    printf("=== Example 5: Policy Composition Behavior ===\n\n");
    
    printf("CVD Security Policy Composition:\n\n");
    
    printf("Policies are COMPOSABLE BUILDING BLOCKS:\n");
    printf("  • All containers start with MINIMAL base policy\n");
    printf("  • Additional features extend the base policy\n");
    printf("  • Features can be combined: \"build,network\"\n\n");
    
    printf("Resolution Order:\n");
    printf("1. Start with minimal base (always)\n");
    printf("2. Add per-container policy features (if specified)\n");
    printf("3. Add global default policy features (if no per-container policy)\n");
    printf("4. Add custom_paths from configuration (always)\n\n");
    
    printf("Examples:\n");
    printf("  params.policy.profiles = NULL      → Use global default\n");
    printf("  params.policy.profiles = \"\"        → Minimal only\n");
    printf("  params.policy.profiles = \"build\"   → Minimal + build\n");
    printf("  params.policy.profiles = \"build,network\" → Minimal + build + network\n\n");
    
    printf("Example cvd.json configuration:\n");
    printf("{\n");
    printf("  \"security\": {\n");
    printf("    \"default_policy\": \"build\",\n");
    printf("    \"custom_paths\": [\n");
    printf("      {\n");
    printf("        \"path\": \"/workspace\",\n");
    printf("        \"access\": \"read,write,execute\"\n");
    printf("      }\n");
    printf("    ]\n");
    printf("  }\n");
    printf("}\n\n");
    printf("With this config:\n");
    printf("  • No policy specified → Minimal + build + /workspace\n");
    printf("  • \"network\" specified → Minimal + network + /workspace\n");
    printf("  • \"build,network\" → Minimal + build + network + /workspace\n\n");
}

int main(int argc, char** argv)
{
    printf("CVD Composable Policy Examples\n");
    printf("===============================\n\n");
    
    printf("NOTE: These examples show the API usage.\n");
    printf("To actually run them, you need:\n");
    printf("1. CVD daemon running\n");
    printf("2. Valid gracht_client_t connection\n");
    printf("3. Proper rootfs at the specified paths\n\n");
    
    // Show policy composition explanation
    example_policy_override_explanation();
    
    // The following would work with a real CVD connection:
    // gracht_client_t* client = ...;
    // example_minimal_container(client);
    // example_build_container(client);
    // example_network_container(client);
    // example_composed_container(client);
    
    printf("=== Summary ===\n");
    printf("Policies are composable building blocks that extend a minimal base.\n");
    printf("This allows flexible, fine-grained control over container permissions\n");
    printf("without changing the CVD configuration file.\n\n");
    
    printf("Available policy features:\n");
    printf("  - minimal:  Base policy (always included)\n");
    printf("  - build:    Adds build tool paths\n");
    printf("  - network:  Adds network configuration paths\n\n");
    
    printf("Composition examples:\n");
    printf("  - \"build\"           → Minimal + build\n");
    printf("  - \"network\"         → Minimal + network\n");
    printf("  - \"build,network\"   → Minimal + build + network\n\n");
    
    printf("For more information, see:\n");
    printf("  - docs/CVD_POLICY_CONFIGURATION.md\n");
    printf("  - docs/CONTAINER_SECURITY_POLICIES.md\n");
    
    return 0;
}
