/**
 * Example: BPF Policy Metrics Monitoring
 * 
 * This example demonstrates how to use the BPF manager metrics API
 * to monitor policy enforcement statistics.
 */

#include <chef/containerv/bpf_manager.h>
#include <stdio.h>
#include <stdlib.h>

void print_global_metrics(void)
{
    struct containerv_bpf_metrics metrics;
    
    printf("=== Global BPF Policy Metrics ===\n");
    
    if (containerv_bpf_manager_get_metrics(&metrics) != 0) {
        printf("Failed to retrieve BPF metrics\n");
        return;
    }
    
    printf("BPF LSM Status: %s\n", metrics.available ? "Available" : "Not Available");
    
    if (!metrics.available) {
        printf("Note: BPF LSM is not available. Metrics will be zero.\n");
        return;
    }
    
    printf("\nPolicy Map Status:\n");
    printf("  Active Containers: %d\n", metrics.total_containers);
    printf("  Total Policy Entries: %d\n", metrics.total_policy_entries);
    printf("  Map Capacity: %d\n", metrics.max_map_capacity);
    printf("  Utilization: %.1f%%\n", 
           (float)metrics.total_policy_entries / metrics.max_map_capacity * 100.0);
    
    printf("\nOperation Statistics:\n");
    printf("  Populate Operations: %llu\n", metrics.total_populate_ops);
    printf("  Cleanup Operations: %llu\n", metrics.total_cleanup_ops);
    
    if (metrics.failed_populate_ops > 0 || metrics.failed_cleanup_ops > 0) {
        printf("\n⚠️  Failures Detected:\n");
        printf("  Failed Populates: %llu\n", metrics.failed_populate_ops);
        printf("  Failed Cleanups: %llu\n", metrics.failed_cleanup_ops);
    }
    
    printf("\n");
}

void print_container_metrics(const char* container_id)
{
    struct containerv_bpf_container_metrics metrics;
    
    printf("=== Container Metrics: %s ===\n", container_id);
    
    if (containerv_bpf_manager_get_container_metrics(container_id, &metrics) != 0) {
        printf("Failed to retrieve metrics for container '%s'\n", container_id);
        printf("Container may not exist or have no policy configured.\n\n");
        return;
    }
    
    printf("Container ID: %s\n", metrics.container_id);
    printf("Cgroup ID: %llu\n", metrics.cgroup_id);
    printf("Policy Entries: %d\n", metrics.policy_entry_count);
    
    printf("\nPerformance:\n");
    printf("  Populate Time: %llu μs (%.3f ms)\n", 
           metrics.populate_time_us, metrics.populate_time_us / 1000.0);
    
    if (metrics.cleanup_time_us > 0) {
        printf("  Cleanup Time: %llu μs (%.3f ms)\n",
               metrics.cleanup_time_us, metrics.cleanup_time_us / 1000.0);
    } else {
        printf("  Cleanup Time: Not yet cleaned up\n");
    }
    
    printf("\n");
}

int main(int argc, char** argv)
{
    printf("BPF Policy Metrics Example\n");
    printf("==========================\n\n");
    
    // Check if BPF manager is initialized
    if (!containerv_bpf_manager_is_available()) {
        printf("Note: BPF manager is not initialized or BPF LSM is not available.\n");
        printf("This is normal if:\n");
        printf("  - The cvd daemon is not running\n");
        printf("  - BPF LSM is not enabled in the kernel\n");
        printf("  - This program is run without BPF manager initialization\n\n");
    }
    
    // Display global metrics
    print_global_metrics();
    
    // If a container ID was provided, show its metrics
    if (argc > 1) {
        print_container_metrics(argv[1]);
    } else {
        printf("Usage: %s [container_id]\n", argv[0]);
        printf("Provide a container ID to see per-container metrics.\n\n");
    }
    
    // Example: Monitoring capacity
    struct containerv_bpf_metrics metrics;
    if (containerv_bpf_manager_get_metrics(&metrics) == 0 && metrics.available) {
        float utilization = (float)metrics.total_policy_entries / metrics.max_map_capacity;
        
        printf("=== Capacity Analysis ===\n");
        if (utilization > 0.9) {
            printf("⚠️  WARNING: Policy map is %.1f%% full!\n", utilization * 100);
            printf("Consider:\n");
            printf("  - Reducing number of active containers\n");
            printf("  - Simplifying container policies\n");
            printf("  - Increasing MAX_TRACKED_ENTRIES in bpf-manager.c\n");
        } else if (utilization > 0.7) {
            printf("⚡ Policy map is %.1f%% full\n", utilization * 100);
            printf("Monitor capacity regularly.\n");
        } else {
            printf("✓ Policy map has %.1f%% capacity available\n", (1.0 - utilization) * 100);
        }
        printf("\n");
    }
    
    return 0;
}
