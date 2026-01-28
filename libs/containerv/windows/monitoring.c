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
 */

#include <vlog.h>
#include <stdio.h>
#include <string.h>
#include <psapi.h>
#include <pdh.h>
#include <winperf.h>
#include <iphlpapi.h>

#pragma comment(lib, "pdh.lib")

#include "private.h"

/**
 * @brief Get current timestamp in nanoseconds
 * @return Timestamp in nanoseconds since epoch
 */
static uint64_t __get_current_timestamp(void)
{
    FILETIME ft;
    ULARGE_INTEGER ui;
    
    GetSystemTimeAsFileTime(&ft);
    ui.LowPart = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    
    // Convert from 100ns intervals since 1601 to nanoseconds since Unix epoch
    return (ui.QuadPart - 116444736000000000ULL) * 100;
}

/**
 * @brief Get network statistics for container (Windows VM-based)
 * @param container Container to get network stats for
 * @param rx_bytes Output for bytes received
 * @param tx_bytes Output for bytes transmitted
 * @param rx_packets Output for packets received
 * @param tx_packets Output for packets transmitted
 */
static void __get_vm_network_stats(struct containerv_container* container,
                                  uint64_t* rx_bytes, uint64_t* tx_bytes,
                                  uint64_t* rx_packets, uint64_t* tx_packets)
{
    MIB_IF_TABLE2* if_table = NULL;
    DWORD result;
    
    *rx_bytes = *tx_bytes = *rx_packets = *tx_packets = 0;
    
    // Get interface table
    result = GetIfTable2(&if_table);
    if (result != NO_ERROR) {
        VLOG_WARNING("containerv[windows]", "failed to get interface table: %lu\n", result);
        return;
    }
    
    // Look for HyperV virtual network interfaces associated with our container
    for (ULONG i = 0; i < if_table->NumEntries; i++) {
        MIB_IF_ROW2* row = &if_table->Table[i];
        
        // Check for HyperV virtual interface (type 6 = Ethernet, description contains HyperV)
        if (row->Type == IF_TYPE_ETHERNET_CSMACD) {
            // Convert description to narrow string for comparison
            char desc[256];
            WideCharToMultiByte(CP_UTF8, 0, row->Description, -1, desc, sizeof(desc), NULL, NULL);
            
            // Look for HyperV or Virtual Machine interfaces
            if (strstr(desc, "Hyper-V") || strstr(desc, "Virtual") || strstr(desc, container->id)) {
                *rx_bytes += row->InOctets;
                *tx_bytes += row->OutOctets;
                *rx_packets += row->InUcastPkts + row->InNUcastPkts;
                *tx_packets += row->OutUcastPkts + row->OutNUcastPkts;
                
                VLOG_DEBUG("containerv[windows]", "network stats from interface %ws: rx=%llu tx=%llu\n",
                          row->Description, row->InOctets, row->OutOctets);
                break;
            }
        }
    }
    
    FreeMibTable(if_table);
}

/**
 * @brief Get comprehensive container statistics for Windows
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
    
    VLOG_DEBUG("containerv[windows]", "collecting stats for container %s\n", container->id);
    
    // Get timestamp
    stats->timestamp = __get_current_timestamp();
    
    // Get Job Object statistics if available
    if (container->job_object) {
        struct containerv_resource_stats job_stats;
        if (__windows_get_job_statistics(container->job_object, &job_stats) == 0) {
            stats->cpu_time_ns = job_stats.cpu_time_ns;
            stats->memory_usage = job_stats.memory_usage;
            stats->memory_peak = job_stats.memory_peak;
            stats->read_bytes = job_stats.read_bytes;
            stats->write_bytes = job_stats.write_bytes;
            stats->read_ops = job_stats.read_ops;
            stats->write_ops = job_stats.write_ops;
            stats->active_processes = job_stats.active_processes;
            stats->total_processes = job_stats.total_processes;
        }
    } else {
        // Fallback: aggregate statistics from individual processes
        struct list_item* item;
        uint64_t total_memory = 0;
        uint32_t process_count = 0;
        
        list_foreach(&container->processes, item) {
            struct containerv_container_process* proc = 
                (struct containerv_container_process*)item;
            
            if (proc->handle != INVALID_HANDLE_VALUE) {
                PROCESS_MEMORY_COUNTERS pmc;
                
                if (GetProcessMemoryInfo(proc->handle, &pmc, sizeof(pmc))) {
                    total_memory += pmc.WorkingSetSize;
                    if (pmc.PeakWorkingSetSize > stats->memory_peak) {
                        stats->memory_peak = pmc.PeakWorkingSetSize;
                    }
                }
                
                process_count++;
            }
        }
        
        stats->memory_usage = total_memory;
        stats->active_processes = process_count;
    }
    
    // Get network statistics for VM
    __get_vm_network_stats(container,
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
            
            // Cap at 100% (can exceed due to multiple cores)
            if (stats->cpu_percent > 100.0) {
                stats->cpu_percent = 100.0;
            }
        }
    }
    
    last_cpu_time = stats->cpu_time_ns;
    last_timestamp = stats->timestamp;
    
    VLOG_DEBUG("containerv[windows]", "stats: mem=%llu cpu_ns=%llu processes=%u cpu_pct=%.1f%%\n", 
              stats->memory_usage, stats->cpu_time_ns, stats->active_processes, stats->cpu_percent);
    
    return 0;
}

/**
 * @brief Get process list for Windows container
 * @param container Container to get processes for
 * @param processes Output array of process information
 * @param max_processes Maximum number of processes to return
 * @return Number of processes returned, or -1 on error
 */
int containerv_get_processes(struct containerv_container* container,
                           struct containerv_process_info* processes,
                           int max_processes)
{
    int count = 0;
    
    if (!container || !processes || max_processes <= 0) {
        return -1;
    }
    
    // If we have a Job Object, enumerate its processes
    if (container->job_object) {
        DWORD process_ids[1024];
        DWORD returned_size;
        
        if (QueryInformationJobObject(container->job_object, JobObjectBasicProcessIdList,
                                     process_ids, sizeof(process_ids), &returned_size)) {
            
            DWORD process_count = returned_size / sizeof(DWORD);
            if (process_count > (DWORD)max_processes) {
                process_count = max_processes;
            }
            
            for (DWORD i = 0; i < process_count; i++) {
                HANDLE proc_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                               FALSE, process_ids[i]);
                if (proc_handle) {
                    processes[count].pid = (process_handle_t)process_ids[i];
                    
                    // Get process name
                    char process_name[MAX_PATH];
                    if (GetProcessImageFileNameA(proc_handle, process_name, sizeof(process_name))) {
                        // Extract just the filename
                        char* filename = strrchr(process_name, '\\');
                        strncpy(processes[count].name, filename ? filename + 1 : process_name, 
                               sizeof(processes[count].name) - 1);
                        processes[count].name[sizeof(processes[count].name) - 1] = '\0';
                    } else {
                        strcpy(processes[count].name, "unknown");
                    }
                    
                    // Get memory usage
                    PROCESS_MEMORY_COUNTERS pmc;
                    if (GetProcessMemoryInfo(proc_handle, &pmc, sizeof(pmc))) {
                        processes[count].memory_kb = pmc.WorkingSetSize / 1024;
                    } else {
                        processes[count].memory_kb = 0;
                    }
                    
                    // CPU percentage would require tracking over time
                    processes[count].cpu_percent = 0.0;
                    
                    CloseHandle(proc_handle);
                    count++;
                }
            }
        } else {
            VLOG_WARNING("containerv[windows]", "failed to enumerate job processes: %lu\n", GetLastError());
        }
    } else {
        // Fallback: use container's process list
        struct list_item* item;
        
        list_foreach(&container->processes, item) {
            if (count >= max_processes) {
                break;
            }
            
            struct containerv_container_process* proc = 
                (struct containerv_container_process*)item;
            
            if (proc->handle != INVALID_HANDLE_VALUE) {
                processes[count].pid = (process_handle_t)proc->pid;
                
                // Get process name from handle
                char process_name[MAX_PATH];
                if (GetProcessImageFileNameA(proc->handle, process_name, sizeof(process_name))) {
                    char* filename = strrchr(process_name, '\\');
                    strncpy(processes[count].name, filename ? filename + 1 : process_name,
                           sizeof(processes[count].name) - 1);
                    processes[count].name[sizeof(processes[count].name) - 1] = '\0';
                } else {
                    strcpy(processes[count].name, "unknown");
                }
                
                // Get memory usage
                PROCESS_MEMORY_COUNTERS pmc;
                if (GetProcessMemoryInfo(proc->handle, &pmc, sizeof(pmc))) {
                    processes[count].memory_kb = pmc.WorkingSetSize / 1024;
                } else {
                    processes[count].memory_kb = 0;
                }
                
                processes[count].cpu_percent = 0.0;
                count++;
            }
        }
    }
    
    VLOG_DEBUG("containerv[windows]", "found %d processes in container %s\n", count, container->id);
    return count;
}
