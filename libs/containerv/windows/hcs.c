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
 *
 */

#include <windows.h>
#include <chef/containerv.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "private.h"

// HCS API function pointers (loaded dynamically from computecore.dll or vmcompute.dll)
typedef HRESULT (WINAPI *HcsCreateComputeSystemFunc)(
    LPCWSTR id,
    LPCWSTR configuration,
    HANDLE identity,
    PVOID* computeSystem,
    LPWSTR* result
);

typedef HRESULT (WINAPI *HcsStartComputeSystemFunc)(
    PVOID computeSystem,
    LPCWSTR options,
    LPWSTR* result
);

typedef HRESULT (WINAPI *HcsShutdownComputeSystemFunc)(
    PVOID computeSystem,
    LPCWSTR options,
    LPWSTR* result
);

typedef HRESULT (WINAPI *HcsTerminateComputeSystemFunc)(
    PVOID computeSystem,
    LPCWSTR options,
    LPWSTR* result
);

typedef HRESULT (WINAPI *HcsCloseComputeSystemFunc)(PVOID computeSystem);

typedef HRESULT (WINAPI *HcsCreateProcessFunc)(
    PVOID computeSystem,
    LPCWSTR processParameters,
    PVOID* process,
    LPWSTR* result
);

typedef HRESULT (WINAPI *HcsCloseProcessFunc)(PVOID process);

typedef HRESULT (WINAPI *HcsTerminateProcessFunc)(PVOID process, LPWSTR* result);

typedef HRESULT (WINAPI *HcsGetProcessInfoFunc)(
    PVOID process,
    DWORD* processId,
    LPWSTR* result
);

static struct {
    HMODULE hcs_module;
    HcsCreateComputeSystemFunc create_compute_system;
    HcsStartComputeSystemFunc start_compute_system;
    HcsShutdownComputeSystemFunc shutdown_compute_system;
    HcsTerminateComputeSystemFunc terminate_compute_system;
    HcsCloseComputeSystemFunc close_compute_system;
    HcsCreateProcessFunc create_process;
    HcsCloseProcessFunc close_process;
    HcsTerminateProcessFunc terminate_process;
    HcsGetProcessInfoFunc get_process_info;
    int initialized;
} g_hcs = { 0 };

int containerv_hcs_initialize(void)
{
    if (g_hcs.initialized) {
        return 0;
    }

    // Try to load vmcompute.dll (Windows Server 2016+)
    g_hcs.hcs_module = LoadLibraryW(L"vmcompute.dll");
    if (g_hcs.hcs_module == NULL) {
        // Try computecore.dll (Windows 10+)
        g_hcs.hcs_module = LoadLibraryW(L"computecore.dll");
        if (g_hcs.hcs_module == NULL) {
            VLOG_ERROR("containerv", "Failed to load HCS library (vmcompute.dll or computecore.dll)\n");
            return -1;
        }
    }

    // Load function pointers
    g_hcs.create_compute_system = (HcsCreateComputeSystemFunc)GetProcAddress(g_hcs.hcs_module, "HcsCreateComputeSystem");
    g_hcs.start_compute_system = (HcsStartComputeSystemFunc)GetProcAddress(g_hcs.hcs_module, "HcsStartComputeSystem");
    g_hcs.shutdown_compute_system = (HcsShutdownComputeSystemFunc)GetProcAddress(g_hcs.hcs_module, "HcsShutdownComputeSystem");
    g_hcs.terminate_compute_system = (HcsTerminateComputeSystemFunc)GetProcAddress(g_hcs.hcs_module, "HcsTerminateComputeSystem");
    g_hcs.close_compute_system = (HcsCloseComputeSystemFunc)GetProcAddress(g_hcs.hcs_module, "HcsCloseComputeSystem");
    g_hcs.create_process = (HcsCreateProcessFunc)GetProcAddress(g_hcs.hcs_module, "HcsCreateProcess");
    g_hcs.close_process = (HcsCloseProcessFunc)GetProcAddress(g_hcs.hcs_module, "HcsCloseProcess");
    g_hcs.terminate_process = (HcsTerminateProcessFunc)GetProcAddress(g_hcs.hcs_module, "HcsTerminateProcess");
    g_hcs.get_process_info = (HcsGetProcessInfoFunc)GetProcAddress(g_hcs.hcs_module, "HcsGetProcessInfo");

    if (!g_hcs.create_compute_system || !g_hcs.start_compute_system || 
        !g_hcs.close_compute_system || !g_hcs.create_process) {
        VLOG_ERROR("containerv", "Failed to load required HCS functions\n");
        FreeLibrary(g_hcs.hcs_module);
        g_hcs.hcs_module = NULL;
        return -1;
    }

    g_hcs.initialized = 1;
    VLOG_DEBUG("containerv", "HCS subsystem initialized\n");
    return 0;
}

void containerv_hcs_cleanup(void)
{
    if (g_hcs.hcs_module) {
        FreeLibrary(g_hcs.hcs_module);
        g_hcs.hcs_module = NULL;
    }
    g_hcs.initialized = 0;
}

static wchar_t* __create_hcs_configuration(
    const char* rootfs,
    const char* id,
    struct containerv_options* options)
{
    // Create a JSON configuration for HCS
    // This is a simplified version - real implementation would need proper JSON construction
    char config_utf8[4096];
    wchar_t* config_wide;
    int len;

    // Build basic configuration
    snprintf(config_utf8, sizeof(config_utf8),
        "{"
        "  \"SchemaVersion\": {"
        "    \"Major\": 2,"
        "    \"Minor\": 1"
        "  },"
        "  \"Owner\": \"chef-containerv\","
        "  \"HostName\": \"%s\","
        "  \"Storage\": {"
        "    \"Layers\": ["
        "      {"
        "        \"Path\": \"%s\""
        "      }"
        "    ]"
        "  },"
        "  \"MappedDirectories\": []"
        "}",
        id, rootfs
    );

    // Convert to wide string
    len = MultiByteToWideChar(CP_UTF8, 0, config_utf8, -1, NULL, 0);
    config_wide = malloc(len * sizeof(wchar_t));
    if (config_wide == NULL) {
        return NULL;
    }
    MultiByteToWideChar(CP_UTF8, 0, config_utf8, -1, config_wide, len);

    return config_wide;
}

int containerv_hcs_create(
    const char* rootfs,
    struct containerv_options* options,
    struct containerv_container** containerOut)
{
    wchar_t id_wide[__CONTAINER_ID_LENGTH + 1];
    wchar_t* config_wide = NULL;
    LPWSTR result = NULL;
    PVOID compute_system = NULL;
    HRESULT hr;
    int status = -1;
    struct containerv_container* container = *containerOut;

    if (containerv_hcs_initialize() != 0) {
        VLOG_ERROR("containerv", "Failed to initialize HCS\n");
        return -1;
    }

    // Convert container ID to wide string
    MultiByteToWideChar(CP_UTF8, 0, container->id, -1, id_wide, __CONTAINER_ID_LENGTH + 1);

    // Create HCS configuration
    config_wide = __create_hcs_configuration(rootfs, container->id, options);
    if (config_wide == NULL) {
        VLOG_ERROR("containerv", "Failed to create HCS configuration\n");
        return -1;
    }

    VLOG_DEBUG("containerv", "Creating HCS compute system with id: %s\n", container->id);

    // Create the compute system
    hr = g_hcs.create_compute_system(id_wide, config_wide, NULL, &compute_system, &result);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv", "HcsCreateComputeSystem failed with hr=0x%08x\n", hr);
        if (result) {
            VLOG_ERROR("containerv", "HCS error result: %S\n", result);
            LocalFree(result);
        }
        goto cleanup;
    }

    container->hcs_handle = compute_system;

    // Start the container
    hr = g_hcs.start_compute_system(compute_system, NULL, &result);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv", "HcsStartComputeSystem failed with hr=0x%08x\n", hr);
        if (result) {
            VLOG_ERROR("containerv", "HCS error result: %S\n", result);
            LocalFree(result);
        }
        g_hcs.close_compute_system(compute_system);
        container->hcs_handle = NULL;
        goto cleanup;
    }

    status = 0;
    VLOG_DEBUG("containerv", "HCS container started successfully\n");

cleanup:
    free(config_wide);
    if (result) {
        LocalFree(result);
    }
    return status;
}

int containerv_hcs_start(struct containerv_container* container)
{
    // Already started in create
    return 0;
}

int containerv_hcs_stop(struct containerv_container* container)
{
    LPWSTR result = NULL;
    HRESULT hr;

    if (container == NULL || container->hcs_handle == NULL) {
        return -1;
    }

    hr = g_hcs.shutdown_compute_system(container->hcs_handle, NULL, &result);
    if (FAILED(hr)) {
        VLOG_WARNING("containerv", "HcsShutdownComputeSystem failed, attempting terminate\n");
        if (result) {
            LocalFree(result);
            result = NULL;
        }
        hr = g_hcs.terminate_compute_system(container->hcs_handle, NULL, &result);
    }

    if (result) {
        LocalFree(result);
    }

    return SUCCEEDED(hr) ? 0 : -1;
}

void containerv_hcs_destroy(struct containerv_container* container)
{
    if (container == NULL) {
        return;
    }

    if (container->hcs_handle) {
        containerv_hcs_stop(container);
        g_hcs.close_compute_system(container->hcs_handle);
        container->hcs_handle = NULL;
    }
}

int containerv_hcs_spawn(
    struct containerv_container* container,
    const char* path,
    struct containerv_spawn_options* options,
    HANDLE* handleOut)
{
    wchar_t* process_params_wide = NULL;
    PVOID process_handle = NULL;
    LPWSTR result = NULL;
    DWORD process_id = 0;
    HRESULT hr;
    char process_params[4096];
    int len;
    int status = -1;

    if (container == NULL || container->hcs_handle == NULL || path == NULL) {
        return -1;
    }

    // Build process parameters JSON
    snprintf(process_params, sizeof(process_params),
        "{"
        "  \"CommandLine\": \"%s\","
        "  \"WorkingDirectory\": \"\\\\\","
        "  \"CreateStdInPipe\": false,"
        "  \"CreateStdOutPipe\": false,"
        "  \"CreateStdErrPipe\": false"
        "}",
        path
    );

    // Convert to wide string
    len = MultiByteToWideChar(CP_UTF8, 0, process_params, -1, NULL, 0);
    process_params_wide = malloc(len * sizeof(wchar_t));
    if (process_params_wide == NULL) {
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, process_params, -1, process_params_wide, len);

    hr = g_hcs.create_process(container->hcs_handle, process_params_wide, &process_handle, &result);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv", "HcsCreateProcess failed with hr=0x%08x\n", hr);
        if (result) {
            VLOG_ERROR("containerv", "HCS error result: %S\n", result);
            LocalFree(result);
        }
        goto cleanup;
    }

    // Get process ID
    if (g_hcs.get_process_info) {
        hr = g_hcs.get_process_info(process_handle, &process_id, &result);
        if (FAILED(hr)) {
            VLOG_WARNING("containerv", "Failed to get process info\n");
        }
        if (result) {
            LocalFree(result);
            result = NULL;
        }
    }

    if (handleOut) {
        *handleOut = process_handle;
    }

    status = 0;
    VLOG_DEBUG("containerv", "Process spawned successfully with PID: %lu\n", process_id);

cleanup:
    free(process_params_wide);
    return status;
}

int containerv_hcs_kill(struct containerv_container* container, HANDLE handle)
{
    LPWSTR result = NULL;
    HRESULT hr;

    if (container == NULL || handle == NULL) {
        return -1;
    }

    hr = g_hcs.terminate_process(handle, &result);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv", "HcsTerminateProcess failed with hr=0x%08x\n", hr);
        if (result) {
            LocalFree(result);
        }
        return -1;
    }

    if (result) {
        LocalFree(result);
    }

    g_hcs.close_process(handle);
    return 0;
}
