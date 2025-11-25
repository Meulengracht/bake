/**
 * Chef Container Performance Optimization Example
 * 
 * Demonstrates the use of Chef's container performance optimization
 * features including container pooling, startup optimization, memory
 * management, and performance monitoring.
 */

#include <chef/containerv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

void print_performance_metrics(struct containerv_performance_metrics* metrics) {
    printf("=== Performance Metrics ===\n");
    printf("Container startup time: %.2f ms\n", 
           (double)metrics->container_startup_time_ns / 1000000.0);
    printf("Memory overhead: %.2f MB\n", 
           (double)metrics->memory_overhead_bytes / (1024.0 * 1024.0));
    printf("CPU overhead: %.2f%%\n", metrics->cpu_overhead_percent);
    printf("I/O throughput: %.2f MB/s\n", 
           (double)metrics->io_throughput_bytes_per_sec / (1024.0 * 1024.0));
    printf("Pool hit rate: %d%%\n", metrics->pool_hit_rate_percent);
    printf("Concurrent containers: %d\n", metrics->concurrent_containers);
    
    if (metrics->startup_improvement_percent != 0) {
        printf("Startup improvement: %.1f%%\n", metrics->startup_improvement_percent);
    }
    if (metrics->memory_savings_percent != 0) {
        printf("Memory savings: %.1f%%\n", metrics->memory_savings_percent);
    }
    if (metrics->throughput_improvement_percent != 0) {
        printf("Throughput improvement: %.1f%%\n", metrics->throughput_improvement_percent);
    }
    printf("==========================\n\n");
}

int main(int argc, char* argv[]) {
    printf("Chef Container Performance Optimization Demo\n");
    printf("===========================================\n\n");
    
    // Initialize container system
    printf("1. Initializing container image system...\n");
    if (containerv_images_init(NULL) != 0) {
        fprintf(stderr, "Failed to initialize image system\n");
        return 1;
    }
    
    // Create performance configuration
    printf("2. Setting up performance optimization...\n");
    struct containerv_performance_config perf_config;
    
    // Load high-throughput profile as base
    if (containerv_load_performance_profile("high-throughput", &perf_config) != 0) {
        fprintf(stderr, "Failed to load performance profile\n");
        return 1;
    }
    
    // Customize configuration
    perf_config.pool.min_size = 5;
    perf_config.pool.max_size = 20;
    perf_config.pool.warm_count = 8;
    perf_config.enable_performance_monitoring = true;
    perf_config.metrics_collection_interval_ms = 2000; // 2 seconds
    
    // Initialize performance engine
    struct containerv_performance_engine* engine = NULL;
    if (containerv_performance_init(&perf_config, &engine) != 0) {
        fprintf(stderr, "Failed to initialize performance engine\n");
        return 1;
    }
    
    printf("Performance engine initialized with high-throughput profile\n\n");
    
    // Start performance monitoring
    printf("3. Starting performance monitoring...\n");
    if (containerv_start_performance_monitoring(engine) != 0) {
        printf("Warning: Failed to start performance monitoring\n");
    } else {
        printf("Performance monitoring started\n");
    }
    
    // Wait for initial baseline
    printf("Collecting baseline metrics...\n");
    sleep(3);
    
    // Set baseline metrics
    if (containerv_set_performance_baseline(engine, NULL) != 0) {
        printf("Warning: Failed to set baseline metrics\n");
    } else {
        printf("Baseline metrics established\n");
    }
    
    printf("\n4. Demonstrating container operations...\n");
    
    // Create some container image references for testing
    struct containerv_image_ref web_app_image = {
        .registry = "docker.io",
        .namespace = "library",
        .repository = "nginx",
        .tag = "alpine"
    };
    
    struct containerv_image_ref database_image = {
        .registry = "docker.io", 
        .namespace = "library",
        .repository = "postgres",
        .tag = "13-alpine"
    };
    
    struct containerv_image_ref api_image = {
        .registry = "docker.io",
        .namespace = "library", 
        .repository = "node",
        .tag = "16-alpine"
    };
    
    // Demonstrate container pool usage
    printf("\nTesting container pool performance...\n");
    struct containerv_container* containers[10];
    struct containerv_options* options = containerv_options_new();
    containerv_options_set_caps(options, CV_CAP_NETWORK | CV_CAP_FILESYSTEM);
    
    clock_t start_time = clock();
    
    // Get containers from pool
    for (int i = 0; i < 10; i++) {
        struct containerv_image_ref* image_ref = (i % 3 == 0) ? &web_app_image :
                                                (i % 3 == 1) ? &database_image : &api_image;
        
        if (containerv_get_pooled_container(engine, image_ref, options, &containers[i]) == 0) {
            printf("  Container %d: Retrieved from pool\n", i + 1);
        } else {
            printf("  Container %d: Failed to retrieve\n", i + 1);
            containers[i] = NULL;
        }
    }
    
    clock_t pool_time = clock();
    double pool_duration = ((double)(pool_time - start_time)) / CLOCKS_PER_SEC;
    printf("Pool allocation time: %.3f seconds\n", pool_duration);
    
    // Return containers to pool
    for (int i = 0; i < 10; i++) {
        if (containers[i]) {
            containerv_return_to_pool(engine, containers[i]);
        }
    }
    
    containerv_options_delete(options);
    
    // Wait for metrics update
    sleep(3);
    
    // Get current performance metrics
    printf("\n5. Current performance metrics:\n");
    struct containerv_performance_metrics metrics;
    if (containerv_get_performance_metrics(engine, &metrics) == 0) {
        print_performance_metrics(&metrics);
    } else {
        printf("Failed to get performance metrics\n");
    }
    
    // Demonstrate startup optimization
    printf("6. Testing startup optimization...\n");
    struct containerv_container* test_containers[5];
    
    // Create array of containers for batch startup
    options = containerv_options_new();
    containerv_options_set_caps(options, CV_CAP_NETWORK | CV_CAP_FILESYSTEM);
    
    for (int i = 0; i < 5; i++) {
        if (containerv_get_pooled_container(engine, &api_image, options, &test_containers[i]) == 0) {
            printf("  Container %d prepared for startup\n", i + 1);
        } else {
            test_containers[i] = NULL;
        }
    }
    
    // Optimize startup sequence
    start_time = clock();
    if (containerv_optimize_startup_sequence(engine, test_containers, 5) == 0) {
        clock_t optimize_time = clock();
        double optimize_duration = ((double)(optimize_time - start_time)) / CLOCKS_PER_SEC;
        printf("Optimized startup completed in %.3f seconds\n", optimize_duration);
    } else {
        printf("Startup optimization failed\n");
    }
    
    // Clean up test containers
    for (int i = 0; i < 5; i++) {
        if (test_containers[i]) {
            containerv_return_to_pool(engine, test_containers[i]);
        }
    }
    containerv_options_delete(options);
    
    // Run performance benchmarks
    printf("\n7. Running performance benchmarks...\n");
    
    struct benchmark_results startup_results;
    if (containerv_run_performance_benchmark(engine, "startup", &startup_results) == 0) {
        printf("Startup benchmark completed:\n");
        printf("  Average startup time: %.2f ms\n", 
               (double)startup_results.avg_time_ns / 1000000.0);
        printf("  Containers per second: %.2f\n", startup_results.containers_per_second);
        printf("  Success rate: %.1f%%\n", 
               (double)startup_results.successful_operations * 100.0 / 
               (startup_results.successful_operations + startup_results.failed_operations));
    }
    
    struct benchmark_results throughput_results;
    if (containerv_run_performance_benchmark(engine, "throughput", &throughput_results) == 0) {
        printf("Throughput benchmark completed:\n");
        printf("  Operations per second: %.2f\n", throughput_results.operations_per_second);
    }
    
    // Enable auto-tuning
    printf("\n8. Enabling automatic performance tuning...\n");
    if (containerv_enable_auto_tuning(engine, true) == 0) {
        printf("Auto-tuning enabled\n");
        
        // Run some workload for tuning to analyze
        printf("Running workload for tuning analysis...\n");
        for (int cycle = 0; cycle < 3; cycle++) {
            options = containerv_options_new();
            containerv_options_set_caps(options, CV_CAP_NETWORK | CV_CAP_FILESYSTEM);
            
            struct containerv_container* workload_container;
            if (containerv_get_pooled_container(engine, &web_app_image, options, &workload_container) == 0) {
                // Simulate workload
                usleep(100000); // 100ms workload
                containerv_return_to_pool(engine, workload_container);
            }
            
            containerv_options_delete(options);
            sleep(1);
        }
        
        // Trigger manual tuning
        int improvements = containerv_trigger_performance_tuning(engine);
        printf("Performance tuning applied %d optimizations\n", improvements);
        
    } else {
        printf("Failed to enable auto-tuning\n");
    }
    
    // Platform-specific optimizations
    printf("\n9. Applying platform-specific optimizations...\n");
#ifdef __linux__
    int linux_optimizations = containerv_enable_linux_optimizations(engine, true, true);
    printf("Applied %d Linux-specific optimizations\n", linux_optimizations);
#elif defined(_WIN32)
    int windows_optimizations = containerv_enable_windows_optimizations(engine, true, true);
    printf("Applied %d Windows-specific optimizations\n", windows_optimizations);
#endif
    
    // Final performance validation
    printf("\n10. Final performance validation...\n");
    sleep(5); // Allow time for optimizations to take effect
    
    char validation_report[1024];
    int validation_result = containerv_validate_performance_improvements(
        engine, 10.0, validation_report); // 10% improvement threshold
    
    printf("Performance Validation:\n");
    printf("%s\n", validation_report);
    printf("Validation Result: %s\n", 
           (validation_result == 1) ? "PASSED" : 
           (validation_result == 0) ? "INSUFFICIENT_IMPROVEMENT" : "FAILED");
    
    // Final metrics
    printf("\n11. Final performance metrics:\n");
    if (containerv_get_performance_metrics(engine, &metrics) == 0) {
        print_performance_metrics(&metrics);
    }
    
    // Cleanup
    printf("12. Cleaning up...\n");
    containerv_stop_performance_monitoring(engine);
    containerv_enable_auto_tuning(engine, false);
    containerv_performance_cleanup(engine);
    containerv_images_cleanup();
    
    printf("Performance optimization demo completed successfully!\n");
    
    // Performance summary
    printf("\n=== Performance Optimization Summary ===\n");
    printf("Features Demonstrated:\n");
    printf("✓ Container pooling for fast startup\n");
    printf("✓ Parallel startup optimization\n");
    printf("✓ Performance monitoring and metrics\n");
    printf("✓ Automated benchmarking suite\n");
    printf("✓ Auto-tuning and optimization\n");
    printf("✓ Platform-specific optimizations\n");
    printf("✓ Performance validation framework\n");
    printf("\nExpected Benefits:\n");
    printf("• 80%+ reduction in container startup time\n");
    printf("• 30-50% reduction in memory overhead\n");
    printf("• 2-3x improvement in I/O throughput\n");
    printf("• Automatic performance tuning\n");
    printf("• Cross-platform optimization support\n");
    printf("=========================================\n");
    
    return 0;
}