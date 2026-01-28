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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include "private.h"

// Global HCS API structure
struct hcs_api g_hcs = { 0 };

// HCS operation callback (stub for now)
static void CALLBACK __hcs_operation_callback(HCS_OPERATION operation, void* context)
{
    // For now, we use synchronous operations
    // In the future, this could handle async completion
    VLOG_DEBUG("containerv[hcs]", "__hcs_operation_callback called\n");
}

int __hcs_initialize(void)
{
    if (g_hcs.hVmCompute != NULL) {
        return 0; // Already initialized
    }

    VLOG_DEBUG("containerv[hcs]", "initializing HCS API\n");

    // Load vmcompute.dll
    g_hcs.hVmCompute = LoadLibraryA("vmcompute.dll");
    if (g_hcs.hVmCompute == NULL) {
        DWORD error = GetLastError();
        VLOG_ERROR("containerv[hcs]", "failed to load vmcompute.dll: %lu\n", error);
        
        // Check if HyperV is available
        if (error == ERROR_MOD_NOT_FOUND) {
            VLOG_ERROR("containerv[hcs]", "HyperV/HCS not available on this system\n");
        }
        return -1;
    }

    // Load HCS function pointers
    g_hcs.HcsCreateComputeSystem = (HcsCreateComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCreateComputeSystem");
    g_hcs.HcsStartComputeSystem = (HcsStartComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsStartComputeSystem");
    g_hcs.HcsShutdownComputeSystem = (HcsShutdownComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsShutdownComputeSystem");
    g_hcs.HcsTerminateComputeSystem = (HcsTerminateComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsTerminateComputeSystem");
    g_hcs.HcsCreateProcess = (HcsCreateProcess_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCreateProcess");
    g_hcs.HcsCreateOperation = (HcsCreateOperation_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCreateOperation");
    g_hcs.HcsCloseOperation = (HcsCloseOperation_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCloseOperation");
    g_hcs.HcsCloseComputeSystem = (HcsCloseComputeSystem_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCloseComputeSystem");
    g_hcs.HcsCloseProcess = (HcsCloseProcess_t)
        GetProcAddress(g_hcs.hVmCompute, "HcsCloseProcess");

    // Check that we got all required functions
    if (!g_hcs.HcsCreateComputeSystem || !g_hcs.HcsStartComputeSystem ||
        !g_hcs.HcsShutdownComputeSystem || !g_hcs.HcsTerminateComputeSystem ||
        !g_hcs.HcsCreateProcess || !g_hcs.HcsCreateOperation ||
        !g_hcs.HcsCloseOperation || !g_hcs.HcsCloseComputeSystem ||
        !g_hcs.HcsCloseProcess) {
        
        VLOG_ERROR("containerv[hcs]", "failed to load required HCS functions\n");
        FreeLibrary(g_hcs.hVmCompute);
        memset(&g_hcs, 0, sizeof(g_hcs));
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "HCS API initialized successfully\n");
    return 0;
}

void __hcs_cleanup(void)
{
    if (g_hcs.hVmCompute != NULL) {
        VLOG_DEBUG("containerv[hcs]", "cleaning up HCS API\n");
        FreeLibrary(g_hcs.hVmCompute);
        memset(&g_hcs, 0, sizeof(g_hcs));
    }
}

wchar_t* __hcs_create_vm_config(
    struct containerv_container* container,
    struct containerv_options* options)
{
    wchar_t* config;
    size_t config_size = 4096;  // Start with 4KB, expand if needed
    int written;

    if (!container) {
        return NULL;
    }

    config = calloc(config_size, sizeof(wchar_t));
    if (!config) {
        return NULL;
    }

    // Create HCS configuration JSON with configurable options
    // Use options from containerv_options for VM resources and networking
    unsigned int memory_mb = options ? options->vm.memory_mb : 1024;
    unsigned int cpu_count = options ? options->vm.cpu_count : 2;
    const char* switch_name = (options && options->network.enable) ? options->network.switch_name : "";
    
    written = swprintf(config, config_size,
        L"{"
        L"\"SchemaVersion\":{\"Major\":2,\"Minor\":1},"
        L"\"VirtualMachine\":{"
            L"\"StopOnReset\":true,"
            L"\"Chipset\":{\"Uefi\":{\"BootThis\":true}},"
            L"\"ComputeTopology\":{"
                L"\"Memory\":{\"SizeInMB\":%u},"
                L"\"Processor\":{\"Count\":%u}"
            L"},"
            L"\"Devices\":{"
                L"\"Scsi\":{"
                    L"\"scsi0\":{"
                        L"\"Attachments\":{"
                            L"\"0\":{"
                                L"\"Type\":\"VirtualDisk\","
                                L"\"Path\":\"%s\\\\container.vhdx\","
                                L"\"ReadOnly\":false"
                            L"}"
                        L"}"
                    L"}"
                L"}"
                // Network configuration added conditionally
                L"%s"  // Network adapter JSON inserted here
            L"}"
        L"},"
        L"\"ShouldTerminateOnLastHandleClosed\":true"
        L"}",
        memory_mb,
        cpu_count,
        container->runtime_dir,
        (options && options->network.enable) ? 
            L",\"NetworkAdapters\":{"
                L"\"net0\":{"
                    L"\"EndpointId\":\"\","
                    L"\"MacAddress\":\"\""
                L"}"
            L"}" : L""
    );

    if (written < 0 || written >= (int)config_size) {
        VLOG_ERROR("containerv[hcs]", "HCS config too large for buffer\n");
        free(config);
        return NULL;
    }

    VLOG_DEBUG("containerv[hcs]", "created HCS VM config for container %s\n", container->id);
    return config;
}

int __hcs_create_vm(
    struct containerv_container* container,
    struct containerv_options* options)
{
    HCS_OPERATION operation = NULL;
    wchar_t* config = NULL;
    HRESULT hr;
    int status = -1;

    if (!container || !container->vm_id) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating HyperV VM for container %s\n", container->id);

    // Ensure HCS API is initialized
    if (__hcs_initialize() != 0) {
        return -1;
    }

    // Create operation handle
    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
        return -1;
    }

    // Generate VM configuration
    config = __hcs_create_vm_config(container, options);
    if (!config) {
        VLOG_ERROR("containerv[hcs]", "failed to create VM configuration\n");
        g_hcs.HcsCloseOperation(operation);
        return -1;
    }

    // Create the compute system (VM)
    hr = g_hcs.HcsCreateComputeSystem(
        container->vm_id,
        config,
        operation,
        NULL,  // Security descriptor
        &container->hcs_system
    );

    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create compute system: 0x%lx\n", hr);
        
        // Provide more specific error information
        if (hr == HCS_E_SERVICE_NOT_AVAILABLE) {
            VLOG_ERROR("containerv[hcs]", "HCS service not available - ensure HyperV is enabled\n");
        } else if (hr == HCS_E_OPERATION_NOT_SUPPORTED) {
            VLOG_ERROR("containerv[hcs]", "HCS operation not supported on this Windows version\n");
        }
        
        goto cleanup;
    }

    // Start the VM
    hr = g_hcs.HcsStartComputeSystem(container->hcs_system, operation, NULL);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to start compute system: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    container->vm_started = 1;
    status = 0;

    VLOG_DEBUG("containerv[hcs]", "successfully created and started VM for container %s\n", 
               container->id);

cleanup:
    if (config) {
        free(config);
    }
    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }

    return status;
}

int __hcs_destroy_vm(struct containerv_container* container)
{
    HCS_OPERATION operation = NULL;
    HRESULT hr;
    int status = 0;

    if (!container || !container->hcs_system) {
        return 0;  // Nothing to destroy
    }

    VLOG_DEBUG("containerv[hcs]", "destroying HyperV VM for container %s\n", container->id);

    // Create operation handle
    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_WARNING("containerv[hcs]", "failed to create operation for VM destroy: 0x%lx\n", hr);
        // Continue without operation handle
    }

    if (container->vm_started) {
        // Try graceful shutdown first
        hr = g_hcs.HcsShutdownComputeSystem(container->hcs_system, operation, NULL);
        if (FAILED(hr)) {
            VLOG_WARNING("containerv[hcs]", "graceful shutdown failed, forcing termination: 0x%lx\n", hr);
            
            // Force termination
            hr = g_hcs.HcsTerminateComputeSystem(container->hcs_system, operation, NULL);
            if (FAILED(hr)) {
                VLOG_ERROR("containerv[hcs]", "failed to terminate compute system: 0x%lx\n", hr);
                status = -1;
            }
        }
        container->vm_started = 0;
    }

    // Close the compute system handle
    hr = g_hcs.HcsCloseComputeSystem(container->hcs_system);
    if (FAILED(hr)) {
        VLOG_WARNING("containerv[hcs]", "failed to close compute system handle: 0x%lx\n", hr);
        status = -1;
    }

    container->hcs_system = NULL;

    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }

    VLOG_DEBUG("containerv[hcs]", "destroyed VM for container %s\n", container->id);
    return status;
}

int __hcs_create_process(
    struct containerv_container* container,
    struct __containerv_spawn_options* options,
    HCS_PROCESS* processOut)
{
    HCS_OPERATION operation = NULL;
    wchar_t* process_config = NULL;
    wchar_t* wide_path = NULL;
    HCS_PROCESS process = NULL;
    HRESULT hr;
    int status = -1;
    size_t config_size = 2048;
    int written;

    if (!container || !container->hcs_system || !options || !options->path) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating process in VM: %s\n", options->path);

    // Convert path to wide string
    size_t path_len = strlen(options->path);
    wide_path = calloc(path_len + 1, sizeof(wchar_t));
    if (!wide_path) {
        return -1;
    }
    
    if (MultiByteToWideChar(CP_UTF8, 0, options->path, -1, wide_path, (int)path_len + 1) == 0) {
        VLOG_ERROR("containerv[hcs]", "failed to convert path to wide string\n");
        goto cleanup;
    }

    // Create operation handle
    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
        goto cleanup;
    }

    // Create process configuration JSON
    process_config = calloc(config_size, sizeof(wchar_t));
    if (!process_config) {
        goto cleanup;
    }

    // Enhanced process configuration with better I/O handling
    written = swprintf(process_config, config_size,
        L"{"
        L"\"CommandLine\":\"%s\","
        L"\"WorkingDirectory\":\"C:\\\\\","
        L"\"Environment\":{"
            L"\"PATH\":\"C:\\\\Windows\\\\System32;C:\\\\Windows\""
        L"},"
        L"\"EmulateConsole\":true,"
        L"\"CreateStdInPipe\":true,"   // Enable stdin for interactive processes
        L"\"CreateStdOutPipe\":true,"  // Enable stdout capture
        L"\"CreateStdErrPipe\":true"   // Enable stderr capture
        L"}",
        wide_path
    );

    if (written < 0 || written >= (int)config_size) {
        VLOG_ERROR("containerv[hcs]", "process config too large for buffer\n");
        goto cleanup;
    }

    // Create the process in the VM
    hr = g_hcs.HcsCreateProcess(
        container->hcs_system,
        process_config,
        operation,
        NULL,  // Security descriptor
        &process
    );

    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create process in VM: 0x%lx\n", hr);
        goto cleanup;
    }

    if (processOut) {
        *processOut = process;
    }

    status = 0;
    VLOG_DEBUG("containerv[hcs]", "successfully created process in VM\n");

cleanup:
    if (wide_path) {
        free(wide_path);
    }
    if (process_config) {
        free(process_config);
    }
    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }

    return status;
}

/**
 * @brief Wait for HCS process completion (similar to Linux waitpid)
 */
int __hcs_wait_process(HCS_PROCESS process, unsigned int timeout_ms)
{
    // TODO: In a full implementation, this would use HCS APIs to:
    // 1. Query process state via HcsGetProcessInfo
    // 2. Wait for process completion events
    // 3. Return exit code
    // For now, this is a placeholder that simulates waiting
    
    if (!process) {
        return -1;
    }
    
    VLOG_DEBUG("containerv[hcs]", "waiting for process completion (timeout: %u ms)\n", timeout_ms);
    
    // Placeholder: In reality we'd use HCS process monitoring APIs
    // that are part of the extended HCS API set
    Sleep(timeout_ms > 10000 ? 10000 : timeout_ms);  // Cap at 10 seconds for placeholder
    
    VLOG_DEBUG("containerv[hcs]", "process wait completed (simulated)\n");
    return 0;  // Assume success for now
}

/**
 * @brief Get HCS process exit code
 */
int __hcs_get_process_exit_code(HCS_PROCESS process, unsigned long* exit_code)
{
    // TODO: Implement using HcsGetProcessInfo or similar HCS API
    // For now, return a default success code
    
    if (!process || !exit_code) {
        return -1;
    }
    
    *exit_code = 0;  // Assume success
    return 0;
}
