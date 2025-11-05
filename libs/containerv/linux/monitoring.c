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
 */

#include "private.h"
#include "cgroups.h"
#include <vlog.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <time.h>
#include <dirent.h>

/**
 * @brief Parse cgroup memory usage from memory.current file
 * @param hostname Container hostname for cgroup path
 * @return Memory usage in bytes, or 0 if unavailable
 */
static uint64_t __get_cgroup_memory_usage(const char* hostname) 
{
    char path[PATH_MAX];
    FILE* file;
    uint64_t usage = 0;
    
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/memory.current", hostname);
    
    file = fopen(path, "r");
    if (file) {
        if (fscanf(file, "%lu", &usage) != 1) {
            usage = 0;
        }
        fclose(file);
    }
    
    return usage;
}

/**
 * @brief Parse cgroup memory peak usage from memory.peak file
 * @param hostname Container hostname for cgroup path
 * @return Peak memory usage in bytes, or 0 if unavailable
 */
static uint64_t __get_cgroup_memory_peak(const char* hostname)
{
    char path[PATH_MAX];
    FILE* file;
    uint64_t peak = 0;
    
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/memory.peak", hostname);
    
    file = fopen(path, "r");
    if (file) {
        if (fscanf(file, "%lu", &peak) != 1) {
            peak = 0;
        }
        fclose(file);
    }
    
    return peak;
}

/**
 * @brief Parse cgroup CPU usage from cpu.stat file
 * @param hostname Container hostname for cgroup path
 * @return CPU time in microseconds, or 0 if unavailable
 */
static uint64_t __get_cgroup_cpu_usage(const char* hostname)
{
    char path[PATH_MAX];
    FILE* file;
    char line[256];
    uint64_t usage_usec = 0;
    
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cpu.stat", hostname);
    
    file = fopen(path, "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            if (strncmp(line, "usage_usec ", 11) == 0) {
                sscanf(line + 11, "%lu", &usage_usec);
                break;
            }
        }
        fclose(file);
    }
    
    return usage_usec * 1000; // Convert to nanoseconds
}

/**
 * @brief Get number of active processes in container cgroup
 * @param hostname Container hostname for cgroup path
 * @return Number of processes, or 0 if unavailable
 */
static uint32_t __get_cgroup_process_count(const char* hostname)
{
    char path[PATH_MAX];
    FILE* file;
    char line[32];
    uint32_t count = 0;
    
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cgroup.procs", hostname);
    
    file = fopen(path, "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            if (strlen(line) > 1) { // Skip empty lines
                count++;
            }
        }
        fclose(file);
    }
    
    return count;
}

/**
 * @brief Parse I/O statistics from io.stat file
 * @param hostname Container hostname for cgroup path
 * @param read_bytes Output for bytes read
 * @param write_bytes Output for bytes written
 * @param read_ops Output for read operations
 * @param write_ops Output for write operations
 */
static void __get_cgroup_io_stats(const char* hostname, uint64_t* read_bytes, 
                                 uint64_t* write_bytes, uint64_t* read_ops,
                                 uint64_t* write_ops)
{
    char path[PATH_MAX];
    FILE* file;
    char line[512];
    
    *read_bytes = *write_bytes = *read_ops = *write_ops = 0;
    
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/io.stat", hostname);
    
    file = fopen(path, "r");
    if (file) {
        while (fgets(line, sizeof(line), file)) {
            // Parse lines like "8:0 rbytes=1234 wbytes=5678 rios=10 wios=20"
            char* rbytes_pos = strstr(line, "rbytes=");
            char* wbytes_pos = strstr(line, "wbytes=");
            char* rios_pos = strstr(line, "rios=");
            char* wios_pos = strstr(line, "wios=");
            
            if (rbytes_pos) {
                *read_bytes += strtoull(rbytes_pos + 7, NULL, 10);
            }
            if (wbytes_pos) {
                *write_bytes += strtoull(wbytes_pos + 7, NULL, 10);
            }
            if (rios_pos) {
                *read_ops += strtoull(rios_pos + 5, NULL, 10);
            }
            if (wios_pos) {
                *write_ops += strtoull(wios_pos + 5, NULL, 10);
            }
        }
        fclose(file);
    }
}

/**
 * @brief Get network statistics for container interface
 * @param container Container to get network stats for
 * @param rx_bytes Output for bytes received
 * @param tx_bytes Output for bytes transmitted
 * @param rx_packets Output for packets received 
 * @param tx_packets Output for packets transmitted
 */
static void __get_network_stats(struct containerv_container* container,
                               uint64_t* rx_bytes, uint64_t* tx_bytes,
                               uint64_t* rx_packets, uint64_t* tx_packets)
{
    char path[PATH_MAX];
    FILE* file;
    char interface[32];
    
    *rx_bytes = *tx_bytes = *rx_packets = *tx_packets = 0;
    
    // Generate expected interface name (veth + container ID prefix)
    snprintf(interface, sizeof(interface), "veth%.8s", container->id);
    
    // Read network statistics from /proc/net/dev
    file = fopen("/proc/net/dev", "r");
    if (file) {
        char line[512];
        
        // Skip header lines
        fgets(line, sizeof(line), file);
        fgets(line, sizeof(line), file);
        
        while (fgets(line, sizeof(line), file)) {
            char iface[32];
            uint64_t rx_bytes_val, rx_packets_val, tx_bytes_val, tx_packets_val;
            
            if (sscanf(line, "%31[^:]: %lu %lu %*u %*u %*u %*u %*u %*u %lu %lu",
                      iface, &rx_bytes_val, &rx_packets_val, &tx_bytes_val, &tx_packets_val) == 5) {
                
                // Remove whitespace from interface name
                char* trimmed = iface;
                while (*trimmed == ' ') trimmed++;
                
                if (strcmp(trimmed, interface) == 0) {
                    *rx_bytes = rx_bytes_val;
                    *rx_packets = rx_packets_val;
                    *tx_bytes = tx_bytes_val;
                    *tx_packets = tx_packets_val;
                    break;
                }
            }
        }
        
        fclose(file);
    }
}

/**
 * @brief Get comprehensive container monitoring statistics
 * @param container Container to monitor
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
int containerv_get_stats(struct containerv_container* container, 
                        struct containerv_stats* stats)
{
    if (!container || !stats) {
        return -1;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    VLOG_DEBUG("containerv", "collecting stats for container %s\n", container->id);
    
    // Get timestamp
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == 0) {
        stats->timestamp = ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }
    
    // Get cgroup-based resource statistics if available
    if (container->hostname) {
        stats->memory_usage = __get_cgroup_memory_usage(container->hostname);
        stats->memory_peak = __get_cgroup_memory_peak(container->hostname);
        stats->cpu_time_ns = __get_cgroup_cpu_usage(container->hostname);
        stats->active_processes = __get_cgroup_process_count(container->hostname);
        
        __get_cgroup_io_stats(container->hostname, 
                             &stats->read_bytes, &stats->write_bytes,
                             &stats->read_ops, &stats->write_ops);
    }
    
    // Get network statistics
    __get_network_stats(container,
                       &stats->network_rx_bytes, &stats->network_tx_bytes,
                       &stats->network_rx_packets, &stats->network_tx_packets);
    
    // Calculate CPU percentage (requires previous measurement)
    static uint64_t last_cpu_time = 0;
    static uint64_t last_timestamp = 0;
    
    if (last_timestamp > 0 && stats->timestamp > last_timestamp) {
        uint64_t cpu_delta = stats->cpu_time_ns - last_cpu_time;
        uint64_t time_delta = stats->timestamp - last_timestamp;
        
        if (time_delta > 0) {
            // CPU percentage = (cpu_time_used / real_time_elapsed) * 100
            stats->cpu_percent = (double)(cpu_delta * 100) / (double)time_delta;
        }
    }
    
    last_cpu_time = stats->cpu_time_ns;
    last_timestamp = stats->timestamp;
    
    VLOG_DEBUG("containerv", "stats: mem=%lu cpu_ns=%lu processes=%u\n", 
              stats->memory_usage, stats->cpu_time_ns, stats->active_processes);
    
    return 0;
}

/**
 * @brief Get process list for container
 * @param container Container to get processes for
 * @param processes Output array of process information
 * @param max_processes Maximum number of processes to return
 * @return Number of processes returned, or -1 on error
 */
int containerv_get_processes(struct containerv_container* container,
                           struct containerv_process_info* processes,
                           int max_processes)
{
    char path[PATH_MAX];
    FILE* file;
    char line[32];
    int count = 0;
    
    if (!container || !processes || max_processes <= 0) {
        return -1;
    }
    
    if (!container->hostname) {
        return 0; // No cgroup tracking
    }
    
    snprintf(path, sizeof(path), "/sys/fs/cgroup/%s/cgroup.procs", container->hostname);
    
    file = fopen(path, "r");
    if (!file) {
        return -1;
    }
    
    while (fgets(line, sizeof(line), file) && count < max_processes) {
        pid_t pid = atoi(line);
        if (pid > 0) {
            processes[count].pid = pid;
            
            // Get process name from /proc/PID/comm
            char comm_path[64];
            FILE* comm_file;
            
            snprintf(comm_path, sizeof(comm_path), "/proc/%d/comm", pid);
            comm_file = fopen(comm_path, "r");
            if (comm_file) {
                if (fgets(processes[count].name, sizeof(processes[count].name), comm_file)) {
                    // Remove trailing newline
                    size_t len = strlen(processes[count].name);
                    if (len > 0 && processes[count].name[len-1] == '\n') {
                        processes[count].name[len-1] = '\0';
                    }
                }
                fclose(comm_file);
            } else {
                strcpy(processes[count].name, "unknown");
            }
            
            // Get memory usage from /proc/PID/status
            char status_path[64];
            FILE* status_file;
            
            snprintf(status_path, sizeof(status_path), "/proc/%d/status", pid);
            status_file = fopen(status_path, "r");
            if (status_file) {
                char status_line[256];
                while (fgets(status_line, sizeof(status_line), status_file)) {
                    if (strncmp(status_line, "VmRSS:", 6) == 0) {
                        int kb;
                        if (sscanf(status_line + 6, "%d", &kb) == 1) {
                            processes[count].memory_kb = kb;
                        }
                        break;
                    }
                }
                fclose(status_file);
            }
            
            count++;
        }
    }
    
    fclose(file);
    
    VLOG_DEBUG("containerv", "found %d processes in container %s\n", count, container->id);
    return count;
}