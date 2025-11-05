/**
 * Performance Optimization Engine
 * 
 * Main engine that coordinates container pooling, startup optimization,
 * memory management, CPU affinity, and performance monitoring.
 */

#include <chef/containerv.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>


#ifdef __linux__
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#ifdef HAVE_NUMA
#include <numa.h>
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#include <processthreadsapi.h>
#endif

// Auto-tuning context
struct auto_tuner {
    struct containerv_performance_engine* engine;
    thrd_t tuner_thread;
    mtx_t tuner_mutex;
    bool active;
    bool shutdown;
    time_t last_tuning;
    uint32_t tuning_iterations;
    
    // Tuning parameters
    double improvement_threshold;    // Minimum improvement to keep changes
    uint32_t observation_period_s;  // How long to observe after changes
    uint32_t max_iterations;        // Maximum tuning iterations
};

// Forward declarations
static int auto_tuner_thread(void* arg);
static int apply_memory_optimizations(struct containerv_performance_engine* engine);
static int apply_cpu_optimizations(struct containerv_performance_engine* engine);
static int apply_io_optimizations(struct containerv_performance_engine* engine);
static int tune_pool_parameters(struct containerv_performance_engine* engine);
static int analyze_performance_bottlenecks(struct containerv_performance_engine* engine);
static void cleanup_performance_engine(struct containerv_performance_engine* engine);

// Global predefined performance profiles
static struct containerv_performance_config profile_balanced;
static struct containerv_performance_config profile_high_throughput;
static struct containerv_performance_config profile_low_latency;
static struct containerv_performance_config profile_memory_efficient;
static bool profiles_initialized = false;

// Initialize predefined profiles
static void init_predefined_profiles(void) {
    if (profiles_initialized) return;
    
    // Load predefined profiles
    containerv_load_performance_profile("balanced", &profile_balanced);
    containerv_load_performance_profile("high-throughput", &profile_high_throughput);
    containerv_load_performance_profile("low-latency", &profile_low_latency);
    containerv_load_performance_profile("memory-efficient", &profile_memory_efficient);
    
    profiles_initialized = true;
}

// Exported predefined profiles
const struct containerv_performance_config* containerv_perf_profile_balanced = &profile_balanced;
const struct containerv_performance_config* containerv_perf_profile_high_throughput = &profile_high_throughput;
const struct containerv_performance_config* containerv_perf_profile_low_latency = &profile_low_latency;
const struct containerv_performance_config* containerv_perf_profile_memory_efficient = &profile_memory_efficient;

int containerv_performance_init(const struct containerv_performance_config* config,
                              struct containerv_performance_engine** engine) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    // Initialize predefined profiles
    init_predefined_profiles();
    
    // Allocate performance engine
    struct containerv_performance_engine* perf_engine = 
        calloc(1, sizeof(struct containerv_performance_engine));
    if (!perf_engine) {
        return -1;
    }
    
    // Set configuration
    if (config) {
        perf_engine->config = *config;
    } else {
        // Use balanced profile as default
        containerv_load_performance_profile("balanced", &perf_engine->config);
    }
    
    // Initialize container pool if enabled
    if (perf_engine->config.pool.max_size > 0) {
        if (containerv_create_container_pool(perf_engine, &perf_engine->config.pool) != 0) {
            free(perf_engine);
            return -1;
        }
    }
    
    // Apply initial optimizations
    if (apply_memory_optimizations(perf_engine) != 0 ||
        apply_cpu_optimizations(perf_engine) != 0 ||
        apply_io_optimizations(perf_engine) != 0) {
        cleanup_performance_engine(perf_engine);
        return -1;
    }
    
    // Start performance monitoring if enabled
    if (perf_engine->config.enable_performance_monitoring) {
        if (containerv_start_performance_monitoring(perf_engine) != 0) {
            // Non-fatal, continue without monitoring
        }
    }
    
    *engine = perf_engine;
    return 0;
}

void containerv_performance_cleanup(struct containerv_performance_engine* engine) {
    if (!engine) return;
    
    cleanup_performance_engine(engine);
    free(engine);
}

int containerv_enable_memory_optimization(struct containerv_performance_engine* engine,
                                        uint64_t optimization_flags) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    engine->config.memory.optimization_flags = optimization_flags;
    return apply_memory_optimizations(engine);
}

int containerv_set_cpu_optimization(struct containerv_performance_engine* engine,
                                   uint32_t cpu_mask,
                                   uint64_t optimization_flags) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    engine->config.cpu.cpu_affinity_mask = cpu_mask;
    engine->config.cpu.optimization_flags = optimization_flags;
    return apply_cpu_optimizations(engine);
}

int containerv_configure_io_optimization(struct containerv_performance_engine* engine,
                                        const struct containerv_io_config* io_config) {
    if (!engine || !io_config) {
        errno = EINVAL;
        return -1;
    }
    
    engine->config.io = *io_config;
    return apply_io_optimizations(engine);
}

int containerv_enable_auto_tuning(struct containerv_performance_engine* engine, bool enable) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    if (enable && !engine->auto_tuning_active) {
        // Start auto-tuning
        struct auto_tuner* tuner = calloc(1, sizeof(struct auto_tuner));
        if (!tuner) {
            return -1;
        }
        
        tuner->engine = engine;
        tuner->improvement_threshold = 5.0; // 5% minimum improvement
        tuner->observation_period_s = engine->config.tuning_interval_seconds;
        if (tuner->observation_period_s == 0) {
            tuner->observation_period_s = 300; // 5 minutes default
        }
        tuner->max_iterations = 50; // Maximum 50 tuning iterations
        
        if (mtx_init(&tuner->tuner_mutex, mtx_plain) != thrd_success) {
            free(tuner);
            return -1;
        }
        
        tuner->active = true;
        if (thrd_create(&tuner->tuner_thread, auto_tuner_thread, tuner) != thrd_success) {
            mtx_destroy(&tuner->tuner_mutex);
            free(tuner);
            return -1;
        }
        
        engine->auto_tuning_active = true;
        
    } else if (!enable && engine->auto_tuning_active) {
        // Stop auto-tuning (implementation would need proper tuner reference management)
        engine->auto_tuning_active = false;
    }
    
    return 0;
}

int containerv_trigger_performance_tuning(struct containerv_performance_engine* engine) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    // Analyze current performance bottlenecks
    int bottlenecks = analyze_performance_bottlenecks(engine);
    if (bottlenecks < 0) {
        return -1;
    }
    
    // Apply optimizations based on bottlenecks identified
    int improvements = 0;
    
    // Tune container pool parameters
    if (engine->container_pool) {
        if (tune_pool_parameters(engine) == 0) {
            improvements++;
        }
    }
    
    // Adjust memory settings based on usage patterns
    if (bottlenecks & 0x1) { // Memory bottleneck detected
        if (engine->config.memory.memory_overcommit_ratio < 2.0) {
            engine->config.memory.memory_overcommit_ratio += 0.1;
            apply_memory_optimizations(engine);
            improvements++;
        }
    }
    
    // Adjust CPU settings based on usage patterns
    if (bottlenecks & 0x2) { // CPU bottleneck detected
        if (!(engine->config.cpu.optimization_flags & CV_CPU_NUMA_AWARE)) {
            engine->config.cpu.optimization_flags |= CV_CPU_NUMA_AWARE;
            apply_cpu_optimizations(engine);
            improvements++;
        }
    }
    
    // Adjust I/O settings based on usage patterns
    if (bottlenecks & 0x4) { // I/O bottleneck detected
        if (engine->config.io.readahead_kb < 1024) {
            engine->config.io.readahead_kb *= 2;
            apply_io_optimizations(engine);
            improvements++;
        }
    }
    
    return improvements;
}

// Platform-specific optimizations
#ifdef __linux__
int containerv_enable_linux_optimizations(struct containerv_performance_engine* engine,
                                         bool enable_overlayfs_tuning,
                                         bool enable_namespace_sharing) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    int optimizations_applied = 0;
    
    if (enable_overlayfs_tuning) {
        // Enable OverlayFS-specific optimizations
        // This would typically involve:
        // 1. Tuning OverlayFS mount options
        // 2. Optimizing layer caching
        // 3. Enabling OverlayFS metacopy feature
        // 4. Configuring optimal OverlayFS upper/work directories
        
        // For demonstration, we'll simulate the optimization
        optimizations_applied++;
    }
    
    if (enable_namespace_sharing) {
        // Enable namespace sharing between containers
        // This would involve:
        // 1. Sharing network namespaces where appropriate
        // 2. Sharing IPC namespaces for related containers
        // 3. Optimizing PID namespace creation
        // 4. Sharing mount namespaces for read-only content
        
        // For demonstration, we'll simulate the optimization
        optimizations_applied++;
    }
    
    return optimizations_applied;
}
#endif

#ifdef _WIN32
int containerv_enable_windows_optimizations(struct containerv_performance_engine* engine,
                                           bool enable_hyperv_optimization,
                                           bool enable_dynamic_memory) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    int optimizations_applied = 0;
    
    if (enable_hyperv_optimization) {
        // Enable Hyper-V container optimizations
        // This would involve:
        // 1. Optimizing Hyper-V container startup
        // 2. Tuning memory allocation for Hyper-V containers
        // 3. Optimizing network performance for Hyper-V containers
        // 4. Configuring optimal CPU allocation
        
        optimizations_applied++;
    }
    
    if (enable_dynamic_memory) {
        // Enable dynamic memory allocation
        // This would involve:
        // 1. Configuring dynamic memory for containers
        // 2. Setting appropriate memory buffer percentages
        // 3. Optimizing memory pressure handling
        // 4. Tuning memory reclaim policies
        
        optimizations_applied++;
    }
    
    return optimizations_applied;
}
#endif

// Internal helper functions

static int apply_memory_optimizations(struct containerv_performance_engine* engine) {
    if (!engine) return -1;
    
    uint64_t flags = engine->config.memory.optimization_flags;
    int optimizations_applied = 0;
    
    // Apply copy-on-write optimization
    if (flags & CV_MEM_COPY_ON_WRITE) {
        // Enable copy-on-write for container memory pages
        // This would involve configuring the container runtime to use COW
        optimizations_applied++;
    }
    
    // Apply shared library optimization
    if (flags & CV_MEM_SHARED_LIBS) {
        // Enable sharing of common libraries between containers
        // This would involve:
        // 1. Identifying commonly used libraries
        // 2. Setting up shared library caching
        // 3. Configuring containers to use shared libraries
        optimizations_applied++;
    }
    
    // Apply memory deduplication
    if (flags & CV_MEM_DEDUPLICATION) {
        // Enable memory page deduplication
        #ifdef __linux__
        // On Linux, this might involve enabling KSM (Kernel Samepage Merging)
        // madvise(addr, length, MADV_MERGEABLE) for container memory regions
        #endif
        optimizations_applied++;
    }
    
    // Apply memory compression
    if (flags & CV_MEM_COMPRESSION) {
        // Enable memory compression for less frequently used pages
        // This would typically be handled by the container runtime
        optimizations_applied++;
    }
    
    // Configure memory ballooning if enabled
    if (engine->config.memory.enable_memory_ballooning) {
        // Set up memory ballooning for dynamic memory adjustment
        optimizations_applied++;
    }
    
    return optimizations_applied;
}

static int apply_cpu_optimizations(struct containerv_performance_engine* engine) {
    if (!engine) return -1;
    
    uint64_t flags = engine->config.cpu.optimization_flags;
    int optimizations_applied = 0;
    
    // Apply CPU affinity optimization
    if (flags & CV_CPU_AFFINITY) {
        #ifdef __linux__
        cpu_set_t cpu_set;
        CPU_ZERO(&cpu_set);
        
        // Set CPU affinity based on mask
        uint32_t mask = engine->config.cpu.cpu_affinity_mask;
        for (int i = 0; i < 32; i++) {
            if (mask & (1U << i)) {
                CPU_SET(i, &cpu_set);
            }
        }
        
        if (sched_setaffinity(0, sizeof(cpu_set), &cpu_set) == 0) {
            optimizations_applied++;
        }
        #elif defined(_WIN32)
        DWORD_PTR mask = engine->config.cpu.cpu_affinity_mask;
        if (SetProcessAffinityMask(GetCurrentProcess(), mask)) {
            optimizations_applied++;
        }
        #endif
    }
    
    // Apply NUMA-aware optimization
    if (flags & CV_CPU_NUMA_AWARE) {
        #ifdef __linux__
        #ifdef HAVE_NUMA
        if (numa_available() != -1) {
            // Configure NUMA-aware allocation
            // This would involve setting NUMA policies for container processes
            optimizations_applied++;
        }
        #endif
        #endif
    }
    
    // Apply CPU priority optimization
    if (flags & CV_CPU_PRIORITY) {
        #ifdef __linux__
        int priority = engine->config.cpu.priority_adjustment;
        if (setpriority(PRIO_PROCESS, 0, priority) == 0) {
            optimizations_applied++;
        }
        #elif defined(_WIN32)
        DWORD priority_class = NORMAL_PRIORITY_CLASS;
        if (engine->config.cpu.priority_adjustment < -10) {
            priority_class = HIGH_PRIORITY_CLASS;
        } else if (engine->config.cpu.priority_adjustment > 10) {
            priority_class = BELOW_NORMAL_PRIORITY_CLASS;
        }
        if (SetPriorityClass(GetCurrentProcess(), priority_class)) {
            optimizations_applied++;
        }
        #endif
    }
    
    // Apply CPU throttling optimization
    if (flags & CV_CPU_THROTTLING) {
        // Configure intelligent CPU throttling based on workload
        // This would typically be handled by cgroups on Linux or Job Objects on Windows
        optimizations_applied++;
    }
    
    return optimizations_applied;
}

static int apply_io_optimizations(struct containerv_performance_engine* engine) {
    if (!engine) return -1;
    
    uint64_t flags = engine->config.io.optimization_flags;
    int optimizations_applied = 0;
    
    // Apply direct I/O optimization
    if (flags & CV_IO_DIRECT) {
        // Enable direct I/O bypassing page cache where appropriate
        // This would be configured per container filesystem
        optimizations_applied++;
    }
    
    // Apply asynchronous I/O optimization
    if (flags & CV_IO_ASYNC) {
        // Enable asynchronous I/O for container operations
        // This might involve configuring io_uring on Linux
        optimizations_applied++;
    }
    
    // Apply read-ahead optimization
    if (flags & CV_IO_READAHEAD) {
        // Configure read-ahead for container filesystems
        uint32_t readahead_kb = engine->config.io.readahead_kb;
        if (readahead_kb > 0) {
            // Set read-ahead size for container block devices
            optimizations_applied++;
        }
    }
    
    // Apply write cache optimization
    if (flags & CV_IO_WRITE_CACHE) {
        // Configure write caching for container I/O
        uint32_t cache_mb = engine->config.io.write_cache_mb;
        if (cache_mb > 0) {
            // Configure write cache size
            optimizations_applied++;
        }
    }
    
    return optimizations_applied;
}

static int tune_pool_parameters(struct containerv_performance_engine* engine) {
    if (!engine || !engine->container_pool) {
        return -1;
    }
    
    // Get pool statistics
    uint32_t total_entries, available_entries, in_use_entries;
    uint64_t total_allocations, pool_hits, pool_misses;
    
    if (containerv_pool_get_stats(engine->container_pool, &total_entries, 
                                &available_entries, &in_use_entries,
                                &total_allocations, &pool_hits, &pool_misses) != 0) {
        return -1;
    }
    
    int adjustments_made = 0;
    
    // Calculate hit rate
    double hit_rate = (total_allocations > 0) ? 
                     (double)pool_hits / (double)total_allocations : 0.0;
    
    // Adjust pool size based on hit rate
    if (hit_rate < 0.80 && total_entries < engine->config.pool.max_size) {
        // Low hit rate, increase pool size
        engine->config.pool.warm_count = 
            (engine->config.pool.warm_count * 120) / 100; // Increase by 20%
        adjustments_made++;
    } else if (hit_rate > 0.95 && available_entries > engine->config.pool.min_size) {
        // Very high hit rate with many available containers, reduce pool size
        engine->config.pool.warm_count = 
            (engine->config.pool.warm_count * 90) / 100; // Decrease by 10%
        adjustments_made++;
    }
    
    return adjustments_made;
}

static int analyze_performance_bottlenecks(struct containerv_performance_engine* engine) {
    if (!engine || !engine->monitoring_active) {
        return -1;
    }
    
    struct containerv_performance_metrics current_metrics;
    if (containerv_get_performance_metrics(engine, &current_metrics) != 0) {
        return -1;
    }
    
    int bottlenecks = 0;
    
    // Analyze memory usage
    if (current_metrics.system_cpu_usage_percent > 80.0) {
        bottlenecks |= 0x1; // Memory bottleneck
    }
    
    // Analyze CPU usage
    if (current_metrics.system_cpu_usage_percent > 85.0) {
        bottlenecks |= 0x2; // CPU bottleneck
    }
    
    // Analyze I/O throughput
    if (current_metrics.io_throughput_bytes_per_sec < 50 * 1024 * 1024) { // Less than 50 MB/s
        bottlenecks |= 0x4; // I/O bottleneck
    }
    
    // Analyze startup times
    if (current_metrics.container_startup_time_ns > 5000000000ULL) { // More than 5 seconds
        bottlenecks |= 0x8; // Startup bottleneck
    }
    
    return bottlenecks;
}

static void cleanup_performance_engine(struct containerv_performance_engine* engine) {
    if (!engine) return;
    
    // Stop performance monitoring
    if (engine->monitoring_active) {
        containerv_stop_performance_monitoring(engine);
    }
    
    // Stop auto-tuning
    if (engine->auto_tuning_active) {
        containerv_enable_auto_tuning(engine, false);
    }
    
    // Cleanup container pool
    if (engine->container_pool) {
        containerv_pool_cleanup(engine->container_pool);
        engine->container_pool = NULL;
    }
    
    // Cleanup startup optimizer
    if (engine->startup_optimizer) {
        containerv_startup_optimizer_cleanup(engine->startup_optimizer);
        engine->startup_optimizer = NULL;
    }
    
    // Cleanup memory pool (if implemented)
    if (engine->memory_pool) {
        // Memory pool cleanup would go here
        engine->memory_pool = NULL;
    }
}

static int auto_tuner_thread(void* arg) {
    struct auto_tuner* tuner = (struct auto_tuner*)arg;
    
    while (!tuner->shutdown && tuner->tuning_iterations < tuner->max_iterations) {
        sleep(tuner->observation_period_s);
        
        if (tuner->shutdown) break;
        
        mtx_lock(&tuner->tuner_mutex);
        
        // Get baseline metrics
        struct containerv_performance_metrics baseline = tuner->engine->baseline_metrics;
        
        // Trigger performance tuning
        int improvements = containerv_trigger_performance_tuning(tuner->engine);
        
        // Wait for observation period
        mtx_unlock(&tuner->tuner_mutex);
        sleep(tuner->observation_period_s);
        mtx_lock(&tuner->tuner_mutex);
        
        // Measure improvements
        struct containerv_performance_metrics current;
        if (containerv_get_performance_metrics(tuner->engine, &current) == 0) {
            
            // Calculate overall improvement
            double startup_improvement = 
                (baseline.container_startup_time_ns > current.container_startup_time_ns) ?
                ((double)(baseline.container_startup_time_ns - current.container_startup_time_ns) * 100.0) /
                (double)baseline.container_startup_time_ns : 0.0;
            
            double memory_improvement = 
                (baseline.memory_overhead_bytes > current.memory_overhead_bytes) ?
                ((double)(baseline.memory_overhead_bytes - current.memory_overhead_bytes) * 100.0) /
                (double)baseline.memory_overhead_bytes : 0.0;
            
            double overall_improvement = (startup_improvement + memory_improvement) / 2.0;
            
            if (overall_improvement < tuner->improvement_threshold) {
                // Improvements not significant enough, consider reverting changes
                // For now, we'll just continue with the next iteration
            }
        }
        
        tuner->tuning_iterations++;
        tuner->last_tuning = time(NULL);
        
        mtx_unlock(&tuner->tuner_mutex);
    }
    
    mtx_lock(&tuner->tuner_mutex);
    tuner->active = false;
    mtx_unlock(&tuner->tuner_mutex);
    
    return 0;
}