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

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

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

// Parse memory limit string (e.g., "1G", "512M") to bytes.
static SIZE_T __parse_memory_limit(const char* memoryStr) 
{
    char*  end;
    double value;

    if (!memoryStr || strcmp(memoryStr, "max") == 0) {
        return 0; // Unlimited
    }
    
    end = NULL;
    value = strtod(memoryStr, &end);
    
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

// Parse CPU limit string to percentage (1-100).
static DWORD __parse_cpu_limit(const char* cpuStr) 
{
    int cpuPercent;

    if (!cpuStr) {
        return WINDOWS_DEFAULT_CPU_PERCENT;
    }
    
    cpuPercent = atoi(cpuStr);
    if (cpuPercent <= 0) {
        return WINDOWS_DEFAULT_CPU_PERCENT;
    }
    if (cpuPercent > 100) {
        return 100;
    }
    
    return (DWORD)cpuPercent;
}

// Parse process limit string to count.
static DWORD __parse_process_limit(const char* processStr) 
{
    int count;

    if (!processStr || strcmp(processStr, "max") == 0) {
        return 0; // Unlimited
    }
    
    count = atoi(processStr);
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
    struct containerv_container*            container,
    const struct containerv_resource_limits* limits)
{
    HANDLE                              job;
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo;
    JOBOBJECT_BASIC_UI_RESTRICTIONS      uiRestrictions;
    wchar_t                             jobName[256];
    SIZE_T                              memoryBytes;
    DWORD                               cpuPercent;
    DWORD                               processLimit;
    
    VLOG_DEBUG("containerv[windows]", "creating job object for container %s\n", container->id);

    memset(&jobInfo, 0, sizeof(jobInfo));
    memset(&uiRestrictions, 0, sizeof(uiRestrictions));
    memoryBytes = 0;
    cpuPercent = 0;
    processLimit = 0;
    
    // Create unique job name
    if (swprintf(jobName, sizeof(jobName)/sizeof(wchar_t), 
                 L"ChefContainer_%hs", container->id) < 0) {
        VLOG_ERROR("containerv[windows]", "failed to create job name\n");
        return NULL;
    }
    
    // Create named job object
    job = CreateJobObjectW(NULL, jobName);
    if (!job) {
        VLOG_ERROR("containerv[windows]", "failed to create job object: %lu\n", GetLastError());
        return NULL;
    }
    
    // Configure memory limits
    if (limits && limits->memory_max) {
        memoryBytes = __parse_memory_limit(limits->memory_max);
        if (memoryBytes > 0) {
            jobInfo.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_PROCESS_MEMORY;
            jobInfo.ProcessMemoryLimit = memoryBytes;
            
            // Also limit job memory (all processes combined)
            jobInfo.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_JOB_MEMORY;
            jobInfo.JobMemoryLimit = memoryBytes * 2; // Allow some overhead
            
            VLOG_DEBUG("containerv[windows]", "set memory limit to %llu bytes\n", 
                      (unsigned long long)memoryBytes);
        }
    }
    
    // Configure CPU limits (applied after ExtendedLimitInformation)
    if (limits && limits->cpu_percent) {
        cpuPercent = __parse_cpu_limit(limits->cpu_percent);
        VLOG_DEBUG("containerv[windows]", "set CPU limit to %lu%%\n", cpuPercent);
    }
    
    // Configure process limits
    if (limits && limits->process_count) {
        processLimit = __parse_process_limit(limits->process_count);
        if (processLimit > 0) {
            jobInfo.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
            jobInfo.BasicLimitInformation.ActiveProcessLimit = processLimit;
            
            VLOG_DEBUG("containerv[windows]", "set process limit to %lu\n", processLimit);
        }
    }
    
    // Configure UI restrictions (security)
    uiRestrictions.UIRestrictionsClass = 
        JOB_OBJECT_UILIMIT_DESKTOP |           // Can't create desktop
        JOB_OBJECT_UILIMIT_DISPLAYSETTINGS |   // Can't change display
        JOB_OBJECT_UILIMIT_EXITWINDOWS |       // Can't shutdown system
        JOB_OBJECT_UILIMIT_READCLIPBOARD |     // Can't read clipboard
        JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS |  // Can't change system params
        JOB_OBJECT_UILIMIT_WRITECLIPBOARD;     // Can't write clipboard
        
    // Always terminate processes when job closes
    jobInfo.BasicLimitInformation.LimitFlags |= JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    
    // Apply job limits
    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, 
                                &jobInfo, sizeof(jobInfo))) {
        VLOG_ERROR("containerv[windows]", "failed to set job limits: %lu\n", GetLastError());
        CloseHandle(job);
        return NULL;
    }

    // Apply CPU rate control separately (not part of JOBOBJECT_EXTENDED_LIMIT_INFORMATION)
    if (cpuPercent > 0) {
        JOBOBJECT_CPU_RATE_CONTROL_INFORMATION cpuInfo;
        memset(&cpuInfo, 0, sizeof(cpuInfo));

        // CpuRate is 1/100th of a percent. 100% == 10000.
        cpuInfo.ControlFlags = JOB_OBJECT_CPU_RATE_CONTROL_ENABLE | JOB_OBJECT_CPU_RATE_CONTROL_HARD_CAP;
        cpuInfo.CpuRate = cpuPercent * 100;

        if (!SetInformationJobObject(job, JobObjectCpuRateControlInformation, &cpuInfo, sizeof(cpuInfo))) {
            // Not fatal on older OS / restricted environments.
            VLOG_WARNING("containerv[windows]", "failed to set CPU rate control: %lu\n", GetLastError());
        }
    }
    
    // Apply UI restrictions
    if (!SetInformationJobObject(job, JobObjectBasicUIRestrictions,
                                &uiRestrictions, sizeof(uiRestrictions))) {
        VLOG_WARNING("containerv[windows]", "failed to set UI restrictions: %lu\n", GetLastError());
        // Not critical, continue
    }
    
    VLOG_DEBUG("containerv[windows]", "successfully created job object %ls\n", jobName);
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
    HANDLE                       job_handle)
{
    struct list_item* item;
    struct containerv_container_process* process;
    int               appliedCount;
    
    if (!job_handle) {
        return -1;
    }

    item = NULL;
    process = NULL;
    appliedCount = 0;
    
    VLOG_DEBUG("containerv[windows]", "applying job limits to container processes\n");
    
    // Apply to all container processes
    list_foreach(&container->processes, item) {
        process = (struct containerv_container_process*)item;
        if (process->is_guest) {
            continue;
        }
        if (process->handle && process->handle != INVALID_HANDLE_VALUE) {
            if (AssignProcessToJobObject(job_handle, process->handle)) {
                appliedCount++;
                VLOG_DEBUG("containerv[windows]", "assigned process %lu to job\n", process->pid);
            } else {
                VLOG_WARNING("containerv[windows]", "failed to assign process %lu to job: %lu\n", 
                           process->pid, GetLastError());
            }
        }
    }
    
    VLOG_DEBUG("containerv[windows]", "applied job limits to %d processes\n", appliedCount);
    return appliedCount > 0 ? 0 : -1;
}

/**
 * @brief Query job object resource usage statistics
 * @param job_handle Job object to query
 * @param stats Output statistics structure
 * @return 0 on success, -1 on failure
 */
int __windows_get_job_statistics(
    HANDLE                          job_handle,
    struct containerv_resource_stats* stats)
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION       basicInfo;
    JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION ioInfo;
    PROCESS_MEMORY_COUNTERS                      memoryInfo;
    HANDLE                                       processHandle;
    DWORD                                        processIds[1024];
    DWORD                                        returnedCount;
    
    if (!job_handle || !stats) {
        return -1;
    }
    
    memset(stats, 0, sizeof(*stats));
    
    // Get basic job statistics
    if (QueryInformationJobObject(job_handle, JobObjectBasicAccountingInformation,
                                 &basicInfo, sizeof(basicInfo), NULL)) {
        stats->cpu_time_ns = basicInfo.TotalUserTime.QuadPart * 100; // Convert to nanoseconds
    }

    if (QueryInformationJobObject(job_handle, JobObjectBasicAndIoAccountingInformation,
                                 &ioInfo, sizeof(ioInfo), NULL)) {
        stats->read_bytes = ioInfo.IoInfo.ReadTransferCount;
        stats->write_bytes = ioInfo.IoInfo.WriteTransferCount;
        stats->read_ops = ioInfo.IoInfo.ReadOperationCount;
        stats->write_ops = ioInfo.IoInfo.WriteOperationCount;
    }
    
    // Memory usage requires per-process enumeration
    // For now, we'll get approximate usage from the first process
    if (QueryInformationJobObject(job_handle, JobObjectBasicProcessIdList,
                                 processIds, sizeof(processIds), &returnedCount)) {
        if (returnedCount >= sizeof(DWORD)) {
            processHandle = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ,
                                       FALSE, processIds[0]);
            if (processHandle) {
                if (GetProcessMemoryInfo(processHandle, &memoryInfo, sizeof(memoryInfo))) {
                    stats->memory_usage = memoryInfo.WorkingSetSize;
                    stats->memory_peak = memoryInfo.PeakWorkingSetSize;
                }
                CloseHandle(processHandle);
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
