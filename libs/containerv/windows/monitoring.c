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

// winsock2.h must be included before windows.h to avoid winsock.h conflicts.
// We need Vista+ IP Helper APIs (GetIfTable2/MIB_IF_TABLE2).
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef WINVER
#define WINVER _WIN32_WINNT
#endif

#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <psapi.h>
#include <pdh.h>
#include <winperf.h>

#include "private.h"

#include <vlog.h>
#include <stdio.h>
#include <string.h>

#pragma comment(lib, "pdh.lib")
#pragma comment(lib, "iphlpapi.lib")

// Get current timestamp in nanoseconds since epoch.
static uint64_t __get_current_timestamp(void)
{
    FILETIME       fileTime;
    ULARGE_INTEGER timeValue;
    
    GetSystemTimeAsFileTime(&fileTime);
    timeValue.LowPart = fileTime.dwLowDateTime;
    timeValue.HighPart = fileTime.dwHighDateTime;
    
    // Convert from 100ns intervals since 1601 to nanoseconds since Unix epoch
    return (timeValue.QuadPart - 116444736000000000ULL) * 100;
}

/**
 * @brief Get network statistics for container (Windows VM-based)
 * @param container Container to get network stats for
 * @param rx_bytes Output for bytes received
 * @param tx_bytes Output for bytes transmitted
 * @param rx_packets Output for packets received
 * @param tx_packets Output for packets transmitted
 */
// Collect VM network stats by scanning Hyper-V interfaces.
static void __get_vm_network_stats(
    struct containerv_container* container,
    uint64_t*                    rxBytes,
    uint64_t*                    txBytes,
    uint64_t*                    rxPackets,
    uint64_t*                    txPackets)
{
    MIB_IF_TABLE2* ifTable;
    DWORD          result;
    ULONG          i;
    MIB_IF_ROW2*   row;
    char           desc[256];

    ifTable = NULL;
    result = 0;
    i = 0;
    row = NULL;
    memset(desc, 0, sizeof(desc));

    *rxBytes = 0;
    *txBytes = 0;
    *rxPackets = 0;
    *txPackets = 0;
    
    // Get interface table
    result = GetIfTable2(&ifTable);
    if (result != NO_ERROR) {
        VLOG_WARNING("containerv[windows]", "failed to get interface table: %lu\n", result);
        return;
    }
    
    // Look for HyperV virtual network interfaces associated with our container
    for (i = 0; i < ifTable->NumEntries; i++) {
        row = &ifTable->Table[i];
        
        // Check for HyperV virtual interface (type 6 = Ethernet, description contains HyperV)
        if (row->Type == IF_TYPE_ETHERNET_CSMACD) {
            // Convert description to narrow string for comparison
            WideCharToMultiByte(CP_UTF8, 0, row->Description, -1, desc, sizeof(desc), NULL, NULL);
            
            // Look for HyperV or Virtual Machine interfaces
            if (strstr(desc, "Hyper-V") || strstr(desc, "Virtual") || strstr(desc, container->id)) {
                *rxBytes += row->InOctets;
                *txBytes += row->OutOctets;
                *rxPackets += row->InUcastPkts + row->InNUcastPkts;
                *txPackets += row->OutUcastPkts + row->OutNUcastPkts;
                
                VLOG_DEBUG("containerv[windows]", "network stats from interface %ws: rx=%llu tx=%llu\n",
                          row->Description, row->InOctets, row->OutOctets);
                break;
            }
        }
    }
    
    FreeMibTable(ifTable);
}

/**
 * @brief Get comprehensive container statistics for Windows
 * @param container Container to monitor
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
int containerv_get_stats(
    struct containerv_container* container,
    struct containerv_stats*     stats)
{
    struct list_item* item;
    uint64_t           totalMemory;
    uint32_t           processCount;
    struct containerv_container_process* proc;
    PROCESS_MEMORY_COUNTERS pmc;
    static uint64_t     lastCpuTime = 0;
    static uint64_t     lastTimestamp = 0;
    uint64_t           cpuDelta;
    uint64_t           timeDelta;

    if (!container || !stats) {
        return -1;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    VLOG_DEBUG("containerv[windows]", "collecting stats for container %s\n", container->id);
    
    // Get timestamp
    stats->timestamp = __get_current_timestamp();
    
    // Get Job Object statistics if available
    if (container->job_object) {
        struct containerv_resource_stats jobStats;
        if (__windows_get_job_statistics(container->job_object, &jobStats) == 0) {
            stats->cpu_time_ns = jobStats.cpu_time_ns;
            stats->memory_usage = jobStats.memory_usage;
            stats->memory_peak = jobStats.memory_peak;
            stats->read_bytes = jobStats.read_bytes;
            stats->write_bytes = jobStats.write_bytes;
            stats->read_ops = jobStats.read_ops;
            stats->write_ops = jobStats.write_ops;
            stats->active_processes = jobStats.active_processes;
            stats->total_processes = jobStats.total_processes;
        }
    } else {
        // Fallback: aggregate statistics from individual processes
        item = NULL;
        totalMemory = 0;
        processCount = 0;
        proc = NULL;

        list_foreach(&container->processes, item) {
            proc = (struct containerv_container_process*)item;

            if (proc->is_guest) {
                continue;
            }
            
            if (proc->handle != INVALID_HANDLE_VALUE) {
                if (GetProcessMemoryInfo(proc->handle, &pmc, sizeof(pmc))) {
                    totalMemory += pmc.WorkingSetSize;
                    if (pmc.PeakWorkingSetSize > stats->memory_peak) {
                        stats->memory_peak = pmc.PeakWorkingSetSize;
                    }
                }

                processCount++;
            }
        }

        stats->memory_usage = totalMemory;
        stats->active_processes = processCount;
    }
    
    // Get network statistics for VM
    __get_vm_network_stats(container,
                          &stats->network_rx_bytes, &stats->network_tx_bytes,
                          &stats->network_rx_packets, &stats->network_tx_packets);
    
    // Calculate CPU percentage (requires previous measurement)
    if (lastTimestamp > 0 && stats->timestamp > lastTimestamp) {
        cpuDelta = stats->cpu_time_ns - lastCpuTime;
        timeDelta = stats->timestamp - lastTimestamp;

        if (timeDelta > 0) {
            // CPU percentage = (cpu_time_used / real_time_elapsed) * 100
            stats->cpu_percent = (double)(cpuDelta * 100) / (double)timeDelta;
            // Note: May exceed 100% on multi-core systems.
        }
    }

    lastCpuTime = stats->cpu_time_ns;
    lastTimestamp = stats->timestamp;
    
    VLOG_DEBUG("containerv[windows]", "stats: mem=%llu cpu_ns=%llu processes=%u cpu_pct=%.1f%%\n", 
              stats->memory_usage, stats->cpu_time_ns, stats->active_processes, stats->cpu_percent);
    
    return 0;
}

int containerv_get_processes(
    struct containerv_container*    container,
    struct containerv_process_info* processes,
    int                             maxProcesses)
{
    int   count;
    DWORD processIds[1024];
    DWORD returnedSize;
    DWORD processCount;
    DWORD i;
    HANDLE procHandle;
    char   processName[MAX_PATH];
    char*  filename;
    PROCESS_MEMORY_COUNTERS pmc;
    struct list_item* item;
    struct containerv_container_process* proc;
    
    if (!container || !processes || maxProcesses <= 0) {
        return -1;
    }

    count = 0;
    memset(processIds, 0, sizeof(processIds));
    returnedSize = 0;
    processCount = 0;
    i = 0;
    procHandle = NULL;
    processName[0] = '\0';
    filename = NULL;
    memset(&pmc, 0, sizeof(pmc));
    item = NULL;
    proc = NULL;
    
    // If we have a Job Object, enumerate its processes
    if (container->job_object) {
        if (QueryInformationJobObject(container->job_object, JobObjectBasicProcessIdList,
                                     processIds, sizeof(processIds), &returnedSize)) {
            
            processCount = returnedSize / sizeof(DWORD);
            if (processCount > (DWORD)maxProcesses) {
                processCount = (DWORD)maxProcesses;
            }
            
            for (i = 0; i < processCount; i++) {
                procHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                       FALSE, processIds[i]);
                if (procHandle) {
                    processes[count].pid = (process_handle_t)processIds[i];
                    
                    // Get process name
                    if (GetProcessImageFileNameA(procHandle, processName, sizeof(processName))) {
                        // Extract just the filename
                        filename = strrchr(processName, '\\');
                        strncpy(processes[count].name, filename ? filename + 1 : processName, 
                               sizeof(processes[count].name) - 1);
                        processes[count].name[sizeof(processes[count].name) - 1] = '\0';
                    } else {
                        strcpy(processes[count].name, "unknown");
                    }
                    
                    // Get memory usage
                    if (GetProcessMemoryInfo(procHandle, &pmc, sizeof(pmc))) {
                        processes[count].memory_kb = pmc.WorkingSetSize / 1024;
                    } else {
                        processes[count].memory_kb = 0;
                    }
                    
                    // CPU percentage would require tracking over time
                    processes[count].cpu_percent = 0.0;
                    
                    CloseHandle(procHandle);
                    count++;
                }
            }
        } else {
            VLOG_WARNING("containerv[windows]", "failed to enumerate job processes: %lu\n", GetLastError());
        }
    } else {
        // Fallback: use container's process list
        list_foreach(&container->processes, item) {
            if (count >= maxProcesses) {
                break;
            }
            
            proc = (struct containerv_container_process*)item;

            if (proc->is_guest) {
                continue;
            }
            
            if (proc->handle != INVALID_HANDLE_VALUE) {
                processes[count].pid = (process_handle_t)proc->pid;
                
                // Get process name from handle
                if (GetProcessImageFileNameA(proc->handle, processName, sizeof(processName))) {
                    filename = strrchr(processName, '\\');
                    strncpy(processes[count].name, filename ? filename + 1 : processName,
                           sizeof(processes[count].name) - 1);
                    processes[count].name[sizeof(processes[count].name) - 1] = '\0';
                } else {
                    strcpy(processes[count].name, "unknown");
                }
                
                // Get memory usage
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
