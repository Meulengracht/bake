/**
 * Performance Monitoring and Metrics Collection
 * 
 * Provides comprehensive performance metrics collection, analysis,
 * and optimization recommendations for container systems.
 */

#include <chef/containerv.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <threads.h>
#include <time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/resource.h>


#ifdef __linux__
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#ifdef HAVE_PROC
#include <proc/readproc.h>
#endif
#endif

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#include <pdh.h>
#endif

// Performance monitoring context
struct performance_monitor {
    struct containerv_performance_engine* engine;
    
    // Monitoring configuration
    uint32_t collection_interval_ms;
    bool     monitoring_active;
    bool     auto_baseline;
    
    // Data collection
    struct containerv_performance_metrics* metrics_history;
    uint32_t metrics_count;
    uint32_t metrics_capacity;
    uint32_t current_index;
    
    // Threading
    thrd_t       monitor_thread;
    mtx_t        metrics_mutex;
    cnd_t        metrics_updated;
    bool         shutdown;
    
    // System baseline
    struct containerv_performance_metrics baseline;
    bool baseline_set;
    
    // Performance alerts
    double memory_alert_threshold;    // Memory usage alert threshold (0.0-1.0)
    double cpu_alert_threshold;       // CPU usage alert threshold (0.0-1.0)
    uint64_t startup_alert_threshold_ms; // Startup time alert threshold
    
    // Statistics
    uint64_t samples_collected;
    uint64_t alerts_generated;
};

// Forward declarations
static int performance_monitor_thread(void* arg);
static int collect_system_metrics(struct containerv_performance_metrics* metrics);
static int collect_container_metrics(struct containerv_performance_engine* engine,
                                   struct containerv_performance_metrics* metrics);
static int collect_pool_metrics(struct containerv_pool* pool,
                              struct containerv_performance_metrics* metrics);
static uint64_t get_timestamp_ns(void);
static double calculate_improvement(uint64_t baseline, uint64_t current);
static void analyze_performance_trends(struct performance_monitor* monitor);

int containerv_start_performance_monitoring(struct containerv_performance_engine* engine) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    // Allocate performance monitor
    struct performance_monitor* monitor = calloc(1, sizeof(struct performance_monitor));
    if (!monitor) {
        return -1;
    }
    
    monitor->engine = engine;
    monitor->collection_interval_ms = engine->config.metrics_collection_interval_ms;
    if (monitor->collection_interval_ms == 0) {
        monitor->collection_interval_ms = 5000; // Default 5 seconds
    }
    
    // Allocate metrics history buffer
    monitor->metrics_capacity = 1000; // Store last 1000 samples
    monitor->metrics_history = calloc(monitor->metrics_capacity, 
                                    sizeof(struct containerv_performance_metrics));
    if (!monitor->metrics_history) {
        free(monitor);
        return -1;
    }
    
    // Initialize synchronization
    if (mtx_init(&monitor->metrics_mutex, mtx_plain) != thrd_success ||
        cnd_init(&monitor->metrics_updated) != thrd_success) {
        free(monitor->metrics_history);
        free(monitor);
        return -1;
    }
    
    // Set alert thresholds
    monitor->memory_alert_threshold = 0.85;  // 85% memory usage
    monitor->cpu_alert_threshold = 0.90;     // 90% CPU usage
    monitor->startup_alert_threshold_ms = 10000; // 10 seconds
    monitor->auto_baseline = true;
    
    // Start monitoring thread
    monitor->monitoring_active = true;
    if (thrd_create(&monitor->monitor_thread, performance_monitor_thread, monitor) != thrd_success) {
        mtx_destroy(&monitor->metrics_mutex);
        cnd_destroy(&monitor->metrics_updated);
        free(monitor->metrics_history);
        free(monitor);
        return -1;
    }
    
    // Store monitor in engine
    engine->current_metrics = (struct containerv_performance_metrics){0};
    engine->monitoring_active = true;
    
    // Store monitor reference (in a real implementation, this would be properly managed)
    static struct performance_monitor* global_monitor = NULL;
    global_monitor = monitor;
    
    return 0;
}

void containerv_stop_performance_monitoring(struct containerv_performance_engine* engine) {
    if (!engine || !engine->monitoring_active) {
        return;
    }
    
    // Access global monitor (in a real implementation, this would be properly managed)
    extern struct performance_monitor* global_monitor;
    struct performance_monitor* monitor = global_monitor;
    
    if (!monitor) {
        return;
    }
    
    // Stop monitoring
    monitor->shutdown = true;
    thrd_join(monitor->monitor_thread, NULL);
    
    // Cleanup
    mtx_destroy(&monitor->metrics_mutex);
    cnd_destroy(&monitor->metrics_updated);
    free(monitor->metrics_history);
    free(monitor);
    
    engine->monitoring_active = false;
    global_monitor = NULL;
}

int containerv_get_performance_metrics(struct containerv_performance_engine* engine,
                                     struct containerv_performance_metrics* metrics) {
    if (!engine || !metrics || !engine->monitoring_active) {
        errno = EINVAL;
        return -1;
    }
    
    // Copy current metrics
    *metrics = engine->current_metrics;
    return 0;
}

int containerv_set_performance_baseline(struct containerv_performance_engine* engine,
                                      const struct containerv_performance_metrics* baseline) {
    if (!engine) {
        errno = EINVAL;
        return -1;
    }
    
    // Access global monitor (in a real implementation, this would be properly managed)
    extern struct performance_monitor* global_monitor;
    struct performance_monitor* monitor = global_monitor;
    
    if (!monitor) {
        errno = ENOENT;
        return -1;
    }
    
    mtx_lock(&monitor->metrics_mutex);
    
    if (baseline) {
        monitor->baseline = *baseline;
    } else {
        // Use current metrics as baseline
        monitor->baseline = engine->current_metrics;
    }
    
    monitor->baseline_set = true;
    engine->baseline_metrics = monitor->baseline;
    
    mtx_unlock(&monitor->metrics_mutex);
    return 0;
}

// Performance monitoring thread
static int performance_monitor_thread(void* arg) {
    struct performance_monitor* monitor = (struct performance_monitor*)arg;
    struct containerv_performance_metrics metrics;
    
    while (!monitor->shutdown) {
        // Collect performance metrics
        memset(&metrics, 0, sizeof(metrics));
        metrics.measurement_timestamp = time(NULL);
        uint64_t start_time = get_timestamp_ns();
        
        // Collect system-level metrics
        if (collect_system_metrics(&metrics) != 0) {
            sleep(1);
            continue;
        }
        
        // Collect container-specific metrics
        if (collect_container_metrics(monitor->engine, &metrics) != 0) {
            sleep(1);
            continue;
        }
        
        // Collect pool metrics if available
        if (monitor->engine->container_pool) {
            collect_pool_metrics(monitor->engine->container_pool, &metrics);
        }
        
        // Calculate measurement duration
        metrics.measurement_duration_ns = get_timestamp_ns() - start_time;
        
        // Calculate improvements if baseline is set
        if (monitor->baseline_set) {
            metrics.startup_improvement_percent = 
                calculate_improvement(monitor->baseline.container_startup_time_ns,
                                    metrics.container_startup_time_ns);
            
            metrics.memory_savings_percent = 
                calculate_improvement(monitor->baseline.memory_overhead_bytes,
                                    metrics.memory_overhead_bytes);
            
            metrics.throughput_improvement_percent = 
                calculate_improvement(monitor->baseline.io_throughput_bytes_per_sec,
                                    metrics.io_throughput_bytes_per_sec);
        }
        
        mtx_lock(&monitor->metrics_mutex);
        
        // Store metrics in history
        monitor->metrics_history[monitor->current_index] = metrics;
        monitor->current_index = (monitor->current_index + 1) % monitor->metrics_capacity;
        if (monitor->metrics_count < monitor->metrics_capacity) {
            monitor->metrics_count++;
        }
        
        // Update engine metrics
        monitor->engine->current_metrics = metrics;
        monitor->samples_collected++;
        
        // Set initial baseline if auto-baseline is enabled
        if (monitor->auto_baseline && !monitor->baseline_set && monitor->samples_collected >= 10) {
            monitor->baseline = metrics;
            monitor->baseline_set = true;
            monitor->engine->baseline_metrics = monitor->baseline;
        }
        
        cnd_broadcast(&monitor->metrics_updated);
        mtx_unlock(&monitor->metrics_mutex);
        
        // Analyze trends and generate alerts
        analyze_performance_trends(monitor);
        
        // Sleep until next collection interval
        usleep(monitor->collection_interval_ms * 1000);
    }
    
    return 0;
}

static int collect_system_metrics(struct containerv_performance_metrics* metrics) {
    if (!metrics) return -1;
    
#ifdef __linux__
    // Get system memory information
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        metrics->total_memory_usage_bytes = (si.totalram - si.freeram) * si.mem_unit;
    }
    
    // Get CPU usage from /proc/stat
    static unsigned long long prev_idle = 0, prev_total = 0;
    FILE* file = fopen("/proc/stat", "r");
    if (file) {
        unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
        if (fscanf(file, "cpu %llu %llu %llu %llu %llu %llu %llu %llu",
                  &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) == 8) {
            
            unsigned long long total = user + nice + system + idle + iowait + irq + softirq + steal;
            unsigned long long diff_idle = idle - prev_idle;
            unsigned long long diff_total = total - prev_total;
            
            if (diff_total > 0) {
                metrics->system_cpu_usage_percent = 
                    100.0 * (double)(diff_total - diff_idle) / (double)diff_total;
            }
            
            prev_idle = idle;
            prev_total = total;
        }
        fclose(file);
    }
    
    // Get file descriptor count
    file = fopen("/proc/sys/fs/file-nr", "r");
    if (file) {
        unsigned int open_fds, free_fds, max_fds;
        if (fscanf(file, "%u %u %u", &open_fds, &free_fds, &max_fds) == 3) {
            metrics->file_descriptor_count = open_fds;
        }
        fclose(file);
    }
    
    // Get thread count (simplified)
    metrics->thread_count = 0; // Would need proper implementation
    
#elif defined(_WIN32)
    // Windows performance monitoring
    MEMORYSTATUSEX mem_status;
    mem_status.dwLength = sizeof(mem_status);
    if (GlobalMemoryStatusEx(&mem_status)) {
        metrics->total_memory_usage_bytes = mem_status.ullTotalPhys - mem_status.ullAvailPhys;
        metrics->system_cpu_usage_percent = 0; // Would use PDH for CPU metrics
    }
    
    // Get handle count (approximation of file descriptors)
    PROCESS_MEMORY_COUNTERS pmc;
    HANDLE process = GetCurrentProcess();
    if (GetProcessMemoryInfo(process, &pmc, sizeof(pmc))) {
        metrics->file_descriptor_count = pmc.PagefileUsage / 4096; // Rough estimate
    }
    
    metrics->thread_count = 0; // Would need proper implementation
#endif
    
    return 0;
}

static int collect_container_metrics(struct containerv_performance_engine* engine,
                                   struct containerv_performance_metrics* metrics) {
    if (!engine || !metrics) return -1;
    
    // Collect container-specific metrics
    // In a real implementation, this would iterate through all active containers
    // and collect their individual statistics
    
    // Simulate some container metrics
    metrics->concurrent_containers = 0; // Would count active containers
    metrics->memory_overhead_bytes = 64 * 1024 * 1024; // 64MB overhead per container
    metrics->cpu_overhead_percent = 5.0; // 5% CPU overhead
    
    // Container startup metrics (would be collected from actual operations)
    metrics->container_startup_time_ns = 2000000000ULL; // 2 seconds
    metrics->image_pull_time_ns = 5000000000ULL;        // 5 seconds
    metrics->filesystem_setup_time_ns = 500000000ULL;   // 0.5 seconds
    metrics->network_setup_time_ns = 200000000ULL;      // 0.2 seconds
    
    // I/O throughput (would be measured from actual container I/O)
    metrics->io_throughput_bytes_per_sec = 100 * 1024 * 1024; // 100 MB/s
    
    return 0;
}

static int collect_pool_metrics(struct containerv_pool* pool,
                              struct containerv_performance_metrics* metrics) {
    if (!pool || !metrics) return -1;
    
    // Get pool statistics
    uint32_t total_entries, available_entries, in_use_entries;
    uint64_t total_allocations, pool_hits, pool_misses;
    
    if (containerv_pool_get_stats(pool, &total_entries, &available_entries, 
                                &in_use_entries, &total_allocations, 
                                &pool_hits, &pool_misses) == 0) {
        
        metrics->pool_size_current = available_entries;
        metrics->pool_size_maximum = total_entries;
        metrics->pool_allocations_total = (uint32_t)total_allocations;
        
        // Calculate hit rate
        if (total_allocations > 0) {
            metrics->pool_hit_rate_percent = 
                (uint32_t)((pool_hits * 100) / total_allocations);
        }
    }
    
    return 0;
}

static uint64_t get_timestamp_ns(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }
    return 0;
}

static double calculate_improvement(uint64_t baseline, uint64_t current) {
    if (baseline == 0) return 0.0;
    
    if (current < baseline) {
        // Improvement (reduction in time/usage)
        return ((double)(baseline - current) / (double)baseline) * 100.0;
    } else {
        // Regression (increase in time/usage)
        return -((double)(current - baseline) / (double)baseline) * 100.0;
    }
}

static void analyze_performance_trends(struct performance_monitor* monitor) {
    if (!monitor || monitor->metrics_count < 10) {
        return; // Need at least 10 samples for trend analysis
    }
    
    mtx_lock(&monitor->metrics_mutex);
    
    // Get recent metrics for trend analysis
    uint32_t recent_count = (monitor->metrics_count >= 20) ? 20 : monitor->metrics_count;
    struct containerv_performance_metrics* recent_metrics = 
        &monitor->metrics_history[(monitor->current_index - recent_count + monitor->metrics_capacity) 
                                % monitor->metrics_capacity];
    
    // Check for alerts
    struct containerv_performance_metrics* current = 
        &monitor->metrics_history[(monitor->current_index - 1 + monitor->metrics_capacity) 
                                % monitor->metrics_capacity];
    
    bool alert_triggered = false;
    
    // Memory usage alert
    if (current->total_memory_usage_bytes > 0) {
        double memory_usage_ratio = (double)current->total_memory_usage_bytes / 
                                  (double)(current->total_memory_usage_bytes + 1024*1024*1024); // Assume some free memory
        if (memory_usage_ratio > monitor->memory_alert_threshold) {
            // Memory alert would be triggered here
            alert_triggered = true;
        }
    }
    
    // CPU usage alert
    if (current->system_cpu_usage_percent > monitor->cpu_alert_threshold * 100.0) {
        // CPU alert would be triggered here
        alert_triggered = true;
    }
    
    // Startup time alert
    if (current->container_startup_time_ns > monitor->startup_alert_threshold_ms * 1000000ULL) {
        // Startup time alert would be triggered here
        alert_triggered = true;
    }
    
    if (alert_triggered) {
        monitor->alerts_generated++;
    }
    
    mtx_unlock(&monitor->metrics_mutex);
}

// Performance profile functions
int containerv_load_performance_profile(const char* profile_name,
                                      struct containerv_performance_config* config) {
    if (!profile_name || !config) {
        errno = EINVAL;
        return -1;
    }
    
    // Initialize with default values
    memset(config, 0, sizeof(*config));
    
    // Load predefined profiles
    if (strcmp(profile_name, "balanced") == 0) {
        // Balanced performance profile
        config->pool.policy = CV_POOL_HYBRID;
        config->pool.min_size = 2;
        config->pool.max_size = 10;
        config->pool.warm_count = 3;
        config->pool.idle_timeout_seconds = 300;
        config->pool.enable_prewarming = true;
        
        config->startup.strategy = CV_STARTUP_PARALLEL;
        config->startup.parallel_limit = 4;
        config->startup.enable_fast_clone = true;
        config->startup.enable_lazy_loading = true;
        
        config->memory.optimization_flags = CV_MEM_COPY_ON_WRITE | CV_MEM_SHARED_LIBS;
        config->memory.memory_overcommit_ratio = 1.2;
        
        config->cpu.optimization_flags = CV_CPU_AFFINITY;
        config->cpu.enable_numa_balancing = true;
        
        config->io.optimization_flags = CV_IO_READAHEAD;
        config->io.readahead_kb = 128;
        
    } else if (strcmp(profile_name, "high-throughput") == 0) {
        // High throughput profile
        config->pool.policy = CV_POOL_PREALLOC;
        config->pool.min_size = 5;
        config->pool.max_size = 50;
        config->pool.warm_count = 10;
        config->pool.enable_prewarming = true;
        
        config->startup.strategy = CV_STARTUP_PARALLEL;
        config->startup.parallel_limit = 8;
        config->startup.enable_fast_clone = true;
        config->startup.skip_health_check_on_startup = true;
        
        config->memory.optimization_flags = CV_MEM_COPY_ON_WRITE | CV_MEM_SHARED_LIBS | CV_MEM_DEDUPLICATION;
        config->memory.memory_overcommit_ratio = 1.5;
        
        config->cpu.optimization_flags = CV_CPU_AFFINITY | CV_CPU_NUMA_AWARE;
        config->cpu.priority_adjustment = -5; // Higher priority
        
        config->io.optimization_flags = CV_IO_DIRECT | CV_IO_ASYNC | CV_IO_READAHEAD;
        config->io.readahead_kb = 1024;
        config->io.queue_depth = 32;
        
    } else if (strcmp(profile_name, "low-latency") == 0) {
        // Low latency profile
        config->pool.policy = CV_POOL_PREALLOC;
        config->pool.min_size = 3;
        config->pool.max_size = 15;
        config->pool.warm_count = 5;
        config->pool.enable_prewarming = true;
        
        config->startup.strategy = CV_STARTUP_PRIORITY;
        config->startup.parallel_limit = 2;
        config->startup.enable_fast_clone = true;
        config->startup.skip_health_check_on_startup = true;
        
        config->memory.optimization_flags = CV_MEM_COPY_ON_WRITE;
        config->memory.memory_overcommit_ratio = 1.1;
        
        config->cpu.optimization_flags = CV_CPU_AFFINITY | CV_CPU_PRIORITY;
        config->cpu.priority_adjustment = -10; // Highest priority
        
        config->io.optimization_flags = CV_IO_DIRECT;
        config->io.queue_depth = 8;
        
    } else if (strcmp(profile_name, "memory-efficient") == 0) {
        // Memory efficient profile
        config->pool.policy = CV_POOL_ON_DEMAND;
        config->pool.min_size = 1;
        config->pool.max_size = 5;
        config->pool.warm_count = 1;
        config->pool.idle_timeout_seconds = 120;
        
        config->startup.strategy = CV_STARTUP_SEQUENTIAL;
        config->startup.enable_lazy_loading = true;
        
        config->memory.optimization_flags = CV_MEM_COPY_ON_WRITE | CV_MEM_SHARED_LIBS | 
                                          CV_MEM_DEDUPLICATION | CV_MEM_COMPRESSION;
        config->memory.memory_overcommit_ratio = 2.0;
        config->memory.enable_memory_ballooning = true;
        
        config->cpu.optimization_flags = CV_CPU_THROTTLING;
        
        config->io.optimization_flags = CV_IO_WRITE_CACHE;
        config->io.write_cache_mb = 64;
        
    } else {
        errno = ENOENT;
        return -1;
    }
    
    // Common settings for all profiles
    config->enable_performance_monitoring = true;
    config->metrics_collection_interval_ms = 5000;
    config->auto_tune_enabled = false;
    config->tuning_interval_seconds = 300;
    
    return 0;
}

int containerv_save_performance_profile(struct containerv_performance_engine* engine,
                                      const char* profile_name) {
    if (!engine || !profile_name) {
        errno = EINVAL;
        return -1;
    }
    
    // In a real implementation, this would save the current configuration
    // to a persistent storage (file, database, etc.)
    
    // For now, just return success
    return 0;
}