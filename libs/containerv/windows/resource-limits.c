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
#include <stdlib.h>
#include <psapi.h>

#include "private.h"

// Default resource limits for Windows containers
#define WINDOWS_DEFAULT_MEMORY_MB 1024      // 1GB
#define WINDOWS_DEFAULT_CPU_PERCENT 50      // 50% CPU 
#define WINDOWS_DEFAULT_PROCESS_COUNT 256   // 256 processes
#define WINDOWS_DEFAULT_IO_BANDWIDTH 100    // 100 MB/s I/O

/**
 * @brief Parse memory limit string (e.g., "1G", "512M") to bytes
 * @param memory_str Memory limit string
 * @return Memory in bytes, or 0 if unlimited
 */
static SIZE_T __parse_memory_limit(const char* memory_str) 
{
    if (!memory_str || strcmp(memory_str, "max") == 0) {
        return 0; // Unlimited
    }
    
    char* end;
    double value = strtod(memory_str, &end);
    
    if (value <= 0) {
        return WINDOWS_DEFAULT_MEMORY_MB * 1024 * 1024;
    }
    
    // Convert based on suffix
    switch (*end) {
        case 'G':
        case 'g':
            return (SIZE_T)(value * 1024 * 1024 * 1024);
        case 'M':
        case 'm':
            return (SIZE_T)(value * 1024 * 1024);
        case 'K':
        case 'k':
            return (SIZE_T)(value * 1024);
        case '\0':
            return (SIZE_T)value; // Assume bytes
        default:
            VLOG_WARNING("containerv[windows]", "unknown memory suffix '%c', using default\n", *end);
            return WINDOWS_DEFAULT_MEMORY_MB * 1024 * 1024;
    }
}

/**
 * @brief Parse CPU limit string to percentage (1-100)
 * @param cpu_str CPU limit string
 * @return CPU percentage (1-100)
 */
static DWORD __parse_cpu_limit(const char* cpu_str) 
{
    if (!cpu_str) {
        return WINDOWS_DEFAULT_CPU_PERCENT;
    }
    
    int cpu_percent = atoi(cpu_str);
    if (cpu_percent <= 0) {
        return WINDOWS_DEFAULT_CPU_PERCENT;
    }
    if (cpu_percent > 100) {
        return 100;
    }
    
    return (DWORD)cpu_percent;
}

/**
 * @brief Parse process limit string to count
 * @param process_str Process limit string
 * @return Process count, or 0 if unlimited
 */
static DWORD __parse_process_limit(const char* process_str) 
{
    if (!process_str || strcmp(process_str, "max") == 0) {
        return 0; // Unlimited
    }
    
    int count = atoi(process_str);
    if (count <= 0) {
        return WINDOWS_DEFAULT_PROCESS_COUNT;
    }
    
    return (DWORD)count;
}

/**
 * @brief Create a Windows Job Object for resource limits
 * @param container Container to create job for
 * @param limits Resource limit configuration
 * @return Job handle, or NULL on failure
 */
HANDLE __windows_create_job_object(
    struct containerv_container* container,
    const struct containerv_resource_limits* limits)
{
    HANDLE job;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION job_info = {0};
    JOBOBJECT_BASIC_UI_RESTRICTIONS ui_restrictions = {0};
    wchar_t job_name[256];
    
    VLOG_DEBUG("containerv[windows]", "creating job object for container %s\n", container->id);
    
    // Create unique job name
    if (swprintf(job_name, sizeof(job_name)/sizeof(wchar_t), 
                 L"ChefContainer_%hs", container->id) < 0) {
        VLOG_ERROR("containerv[windows]", "failed to create job name\n");
        return NULL;
    }
    
    // Create named job object
    job = CreateJobObjectW(NULL, job_name);
    if (!job) {
        VLOG_ERROR("containerv[windows]", "failed to create job object: %lu\n", GetLastError());
        return NULL;
    }
    
    // Configure memory limits
    if (limits && limits->memory_max) {
        SIZE_T memory_bytes = __parse_memory_limit(limits->memory_max);
        if (memory_bytes > 0) {
            job_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            job_info.ProcessMemoryLimit = memory_bytes;
            
            // Also limit job memory (all processes combined)
            job_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
            job_info.JobMemoryLimit = memory_bytes * 2; // Allow some overhead
            
            VLOG_DEBUG("containerv[windows]", "set memory limit to %llu bytes\n", 
                      (unsigned long long)memory_bytes);
        }
    }
    
    // Configure CPU limits
    if (limits && limits->cpu_percent) {
        DWORD cpu_percent = __parse_cpu_limit(limits->cpu_percent);
        
        // CPU rate control (Windows 8+)
        job_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_CPU_RATE_CONTROL;
        
        // Set CPU rate as weight (0-10000, where 10000 = 100%)
        job_info.CpuRateControlLimit = cpu_percent * 100;
        job_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_TIME;
        
        VLOG_DEBUG("containerv[windows]", "set CPU limit to %lu%%\n", cpu_percent);
    }
    
    // Configure process limits
    if (limits && limits->process_count) {
        DWORD process_limit = __parse_process_limit(limits->process_count);
        if (process_limit > 0) {
            job_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
            job_info.BasicLimitInformation.ActiveProcessLimit = process_limit;
            
            VLOG_DEBUG("containerv[windows]", "set process limit to %lu\n", process_limit);
        }
    }
    
    // Configure UI restrictions (security)
    ui_restrictions.UIRestrictionsClass = 
        JOB_OBJECT_UILIMIT_DESKTOP |           // Can't create desktop
        JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |   // Can't change display
        JOB_OBJECT_UILIMIT_EXITWINDOWS |       // Can't shutdown system
        JOB_OBJECT_UILIMIT_READCLIPBOARD |     // Can't read clipboard
        JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS |  // Can't change system params
        JOB_OBJECT_UILIMIT_WRITECLIPBOARD;     // Can't write clipboard
        
    // Always terminate processes when job closes
    job_info.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    
    // Apply job limits
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, 
                                &job_info, sizeof(job_info))) {
        VLOG_ERROR("containerv[windows]", "failed to set job limits: %lu\n", GetLastError());
        CloseHandle(job);
        return NULL;
    }
    
    // Apply UI restrictions
    if (!SetInformationJobObject(job, JobObjectBasicUIRestrictions,
                                &ui_restrictions, sizeof(ui_restrictions))) {
        VLOG_WARNING("containerv[windows]", "failed to set UI restrictions: %lu\n", GetLastError());
        // Not critical, continue
    }
    
    VLOG_DEBUG("containerv[windows]", "successfully created job object %ls\n", job_name);
    return job;
}

/**
 * @brief Apply job object to HCS processes for resource control
 * @param container Container with running processes
 * @param job_handle Job object to apply
 * @return 0 on success, -1 on failure
 */
int __windows_apply_job_to_processes(
    struct containerv_container* container,
    HANDLE job_handle)
{
    struct containerv_container_process* process;
    int applied_count = 0;
    
    if (!job_handle) {
        return -1;
    }
    
    VLOG_DEBUG("containerv[windows]", "applying job limits to container processes\n");
    
    // Apply to all container processes
    list_foreach(&container->processes, process) {
        if (process->handle && process->handle != INVALID_HANDLE_VALUE) {
            if (AssignProcessToJobObject(job_handle, process->handle)) {
                applied_count++;
                VLOG_DEBUG("containerv[windows]", "assigned process %lu to job\n", process->pid);
            } else {
                VLOG_WARNING("containerv[windows]", "failed to assign process %lu to job: %lu\n", 
                           process->pid, GetLastError());
            }
        }
    }
    
    VLOG_DEBUG("containerv[windows]", "applied job limits to %d processes\n", applied_count);
    return applied_count > 0 ? 0 : -1;
}

/**
 * @brief Query job object resource usage statistics
 * @param job_handle Job object to query
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
int __windows_get_job_statistics(
    HANDLE job_handle,
    struct containerv_resource_stats* stats)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION basic_info;
    JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION io_info;
    PROCESS_MEMORY_COUNTERS memory_info;
    HANDLE process_handle;
    
    if (!job_handle || !stats) {
        return -1;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    // Get basic job statistics
    if (QueryInformationJobObject(job_handle, JobObjectBasicAccountingInformation,
                                 &basic_info, sizeof(basic_info), NULL)) {
        stats->cpu_time_ns = basic_info.TotalUserTime.QuadPart * 100; // Convert to nanoseconds
        stats->active_processes = basic_info.ActiveProcesses;
        stats->total_processes = basic_info.TotalProcesses;
    }
    
    // Get I/O statistics
    if (QueryInformationJobObject(job_handle, JobObjectBasicAndIoAccountingInformation,
                                 &io_info, sizeof(io_info), NULL)) {
        stats->read_bytes = io_info.IoInfo.ReadTransferCount;
        stats->write_bytes = io_info.IoInfo.WriteTransferCount;
        stats->read_ops = io_info.IoInfo.ReadOperationCount;
        stats->write_ops = io_info.IoInfo.WriteOperationCount;
    }
    
    // Memory usage requires per-process enumeration
    // For now, we'll get approximate usage from the first process
    DWORD process_ids[1024];
    DWORD returned_count;
    
    if (QueryInformationJobObject(job_handle, JobObjectBasicProcessIdList,
                                 process_ids, sizeof(process_ids), &returned_count)) {
        if (returned_count >= sizeof(DWORD)) {
            process_handle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                       FALSE, process_ids[0]);
            if (process_handle) {
                if (GetProcessMemoryInfo(process_handle, &memory_info, sizeof(memory_info))) {
                    stats->memory_usage = memory_info.WorkingSetSize;
                    stats->memory_peak = memory_info.PeakWorkingSetSize;
                }
                CloseHandle(process_handle);
            }
        }
    }
    
    return 0;
}

/**
 * @brief Cleanup job object and associated resources
 * @param job_handle Job object to cleanup
 */
void __windows_cleanup_job_object(HANDLE job_handle)
{
    if (job_handle && job_handle != INVALID_HANDLE_VALUE) {
        VLOG_DEBUG("containerv[windows]", "terminating job object\n");
        
        // Terminate all processes in job
        TerminateJobObject(job_handle, 0);
        
        // Close job handle
        CloseHandle(job_handle);
    }
}
