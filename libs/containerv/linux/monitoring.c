/**
 * Copyright, Philip Meulengracht
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

#include "private.h"

#include <errno.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <vlog.h>

#define __CONTAINER_VETH_HOST_OFFSET 0
#define __CONTAINER_VETH_CONT_OFFSET 4

static uint64_t __now_realtime_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        return 0;
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static int __read_file_to_string(const char* path, char* buffer, size_t buffer_len)
{
    FILE* f;
    size_t n;

    if (!path || !buffer || buffer_len == 0) {
        errno = EINVAL;
        return -1;
    }

    f = fopen(path, "rb");
    if (!f) {
        return -1;
    }

    n = fread(buffer, 1, buffer_len - 1, f);
    buffer[n] = '\0';
    fclose(f);
    return 0;
}

static int __read_file_u64(const char* path, uint64_t* value_out)
{
    char buf[256];
    char* endptr;

    if (!value_out) {
        errno = EINVAL;
        return -1;
    }

    if (__read_file_to_string(path, buf, sizeof(buf)) != 0) {
        return -1;
    }

    errno = 0;
    *value_out = strtoull(buf, &endptr, 10);
    if (errno != 0) {
        return -1;
    }

    return 0;
}

static void __read_network_stats(const char* container_id,
                                 uint64_t* rx_bytes, uint64_t* tx_bytes,
                                 uint64_t* rx_packets, uint64_t* tx_packets)
{
    char host_veth[16];
    char path[PATH_MAX];

    *rx_bytes = *tx_bytes = *rx_packets = *tx_packets = 0;

    if (!container_id || strlen(container_id) < 5) {
        return;
    }

    // Match naming in linux/container.c (host side stays in host netns)
    snprintf(host_veth, sizeof(host_veth), "veth%s", &container_id[__CONTAINER_VETH_HOST_OFFSET]);

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_bytes", host_veth);
    (void)__read_file_u64(path, rx_bytes);

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_bytes", host_veth);
    (void)__read_file_u64(path, tx_bytes);

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/rx_packets", host_veth);
    (void)__read_file_u64(path, rx_packets);

    snprintf(path, sizeof(path), "/sys/class/net/%s/statistics/tx_packets", host_veth);
    (void)__read_file_u64(path, tx_packets);
}

static void __parse_cpu_stat(const char* cpu_stat_contents, uint64_t* cpu_time_ns_out)
{
    // cgroup v2 cpu.stat contains e.g.:
    // usage_usec 123
    // user_usec  10
    // system_usec 5
    // Some kernels may expose usage_nsec.
    const char* p = cpu_stat_contents;

    *cpu_time_ns_out = 0;

    if (!p) {
        return;
    }

    while (*p) {
        char key[64];
        unsigned long long value;
        int consumed = 0;

        if (sscanf(p, "%63s %llu%n", key, &value, &consumed) == 2 && consumed > 0) {
            if (strcmp(key, "usage_usec") == 0) {
                *cpu_time_ns_out = (uint64_t)value * 1000ULL;
                return;
            }
            if (strcmp(key, "usage_nsec") == 0) {
                *cpu_time_ns_out = (uint64_t)value;
                return;
            }
        }

        // Advance to next line
        while (*p && *p != '\n') {
            p++;
        }
        if (*p == '\n') {
            p++;
        }
    }
}

static void __parse_io_stat(const char* io_stat_contents,
                            uint64_t* read_bytes, uint64_t* write_bytes,
                            uint64_t* read_ops, uint64_t* write_ops)
{
    const char* p = io_stat_contents;

    *read_bytes = *write_bytes = *read_ops = *write_ops = 0;

    if (!p) {
        return;
    }

    while (*p) {
        // A line looks like:
        // 8:0 rbytes=123 wbytes=456 rios=7 wios=8 dbytes=... dios=...
        // We'll scan key=value pairs for rbytes/wbytes/rios/wios.
        const char* line_end = strchr(p, '\n');
        size_t line_len = line_end ? (size_t)(line_end - p) : strlen(p);

        char line[1024];
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, p, line_len);
        line[line_len] = '\0';

        char* saveptr = NULL;
        char* token = strtok_r(line, " ", &saveptr);
        while (token) {
            unsigned long long v;

            if (sscanf(token, "rbytes=%llu", &v) == 1) {
                *read_bytes += (uint64_t)v;
            } else if (sscanf(token, "wbytes=%llu", &v) == 1) {
                *write_bytes += (uint64_t)v;
            } else if (sscanf(token, "rios=%llu", &v) == 1) {
                *read_ops += (uint64_t)v;
            } else if (sscanf(token, "wios=%llu", &v) == 1) {
                *write_ops += (uint64_t)v;
            }

            token = strtok_r(NULL, " ", &saveptr);
        }

        if (!line_end) {
            break;
        }
        p = line_end + 1;
    }
}

int containerv_get_stats(struct containerv_container* container, struct containerv_stats* stats)
{
    char cgroup_base[PATH_MAX];
    char path[PATH_MAX];

    if (!container || !stats) {
        errno = EINVAL;
        return -1;
    }

    memset(stats, 0, sizeof(*stats));

    stats->timestamp = __now_realtime_ns();

    // cgroup v2 base directory is created by cgroups_init as /sys/fs/cgroup/<hostname>
    if (container->hostname && container->hostname[0]) {
        snprintf(cgroup_base, sizeof(cgroup_base), "/sys/fs/cgroup/%s", container->hostname);

        // CPU
        {
            char cpu_buf[1024];
            snprintf(path, sizeof(path), "%s/cpu.stat", cgroup_base);
            if (__read_file_to_string(path, cpu_buf, sizeof(cpu_buf)) == 0) {
                __parse_cpu_stat(cpu_buf, &stats->cpu_time_ns);
            }
        }

        // Memory
        snprintf(path, sizeof(path), "%s/memory.current", cgroup_base);
        (void)__read_file_u64(path, &stats->memory_usage);

        snprintf(path, sizeof(path), "%s/memory.peak", cgroup_base);
        (void)__read_file_u64(path, &stats->memory_peak);

        // I/O
        {
            char io_buf[4096];
            snprintf(path, sizeof(path), "%s/io.stat", cgroup_base);
            if (__read_file_to_string(path, io_buf, sizeof(io_buf)) == 0) {
                __parse_io_stat(io_buf, &stats->read_bytes, &stats->write_bytes, &stats->read_ops, &stats->write_ops);
            }
        }

        // PIDs
        {
            uint64_t pids_current = 0;
            snprintf(path, sizeof(path), "%s/pids.current", cgroup_base);
            if (__read_file_u64(path, &pids_current) == 0) {
                if (pids_current > UINT32_MAX) {
                    pids_current = UINT32_MAX;
                }
                stats->active_processes = (uint32_t)pids_current;
            }
        }

        // Total processes created is not available directly via cgroup v2.
        stats->total_processes = 0;
    }

    // Network (host-side veth interface stats)
    __read_network_stats(container->id,
                         &stats->network_rx_bytes, &stats->network_tx_bytes,
                         &stats->network_rx_packets, &stats->network_tx_packets);

    // CPU percentage based on per-container deltas
    if (container->last_stats_timestamp_ns > 0 && stats->timestamp > container->last_stats_timestamp_ns) {
        uint64_t cpu_delta = stats->cpu_time_ns - container->last_stats_cpu_time_ns;
        uint64_t time_delta = stats->timestamp - container->last_stats_timestamp_ns;

        if (time_delta > 0) {
            stats->cpu_percent = (double)(cpu_delta * 100ULL) / (double)time_delta;
        }
    }

    container->last_stats_cpu_time_ns = stats->cpu_time_ns;
    container->last_stats_timestamp_ns = stats->timestamp;

    VLOG_DEBUG("containerv[linux]", "stats: mem=%llu cpu_ns=%llu pids=%u cpu_pct=%.1f%%\n",
              (unsigned long long)stats->memory_usage,
              (unsigned long long)stats->cpu_time_ns,
              stats->active_processes,
              stats->cpu_percent);

    return 0;
}

int containerv_get_processes(
    struct containerv_container*    container,
    struct containerv_process_info* processes,
    int                             maxProcesses)
{
    char path[PATH_MAX];
    FILE* file;
    char line[32];
    int count = 0;
    
    if (!container || !processes || maxProcesses <= 0) {
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
    
    while (fgets(line, sizeof(line), file) && count < maxProcesses) {
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
