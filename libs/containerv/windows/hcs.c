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
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include "private.h"

// Some Windows SDKs don't expose these HCS_* HRESULTs unless additional headers are used.
// They are only used for improved error messages, so provide reasonable fallbacks.
#ifndef HCS_E_OPERATION_NOT_SUPPORTED
#define HCS_E_OPERATION_NOT_SUPPORTED HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
#endif

#ifndef HCS_E_SERVICE_NOT_AVAILABLE
#define HCS_E_SERVICE_NOT_AVAILABLE HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)
#endif

// Global HCS API structure
struct hcs_api g_hcs = { 0 };

static FARPROC __get_proc_any(const char* name)
{
    if (name == NULL) {
        return NULL;
    }

    FARPROC p = NULL;
    if (g_hcs.hComputeCore != NULL) {
        p = GetProcAddress(g_hcs.hComputeCore, name);
        if (p != NULL) {
            return p;
        }
    }

    if (g_hcs.hVmCompute != NULL) {
        p = GetProcAddress(g_hcs.hVmCompute, name);
        if (p != NULL) {
            return p;
        }
    }
    return NULL;
}

static void __hcs_localfree_wstr(PWSTR s)
{
    if (s != NULL) {
        LocalFree(s);
    }
}

static int __appendf(char** buf, size_t* cap, size_t* len, const char* fmt, ...)
{
    if (buf == NULL || cap == NULL || len == NULL || fmt == NULL) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);

    for (;;) {
        if (*buf == NULL) {
            *cap = 4096;
            *len = 0;
            *buf = calloc(*cap, 1);
            if (*buf == NULL) {
                va_end(args);
                return -1;
            }
        }

        va_list args2;
        va_copy(args2, args);
        int n = vsnprintf(*buf + *len, *cap - *len, fmt, args2);
        va_end(args2);

        if (n < 0) {
            va_end(args);
            return -1;
        }

        if (*len + (size_t)n < *cap) {
            *len += (size_t)n;
            va_end(args);
            return 0;
        }

        // Need more space
        size_t new_cap = *cap * 2;
        while (*len + (size_t)n >= new_cap) {
            new_cap *= 2;
        }
        char* tmp = realloc(*buf, new_cap);
        if (tmp == NULL) {
            va_end(args);
            return -1;
        }
        *buf = tmp;
        memset(*buf + *cap, 0, new_cap - *cap);
        *cap = new_cap;
    }
}

static char* __json_escape_utf8(const char* s)
{
    if (s == NULL) {
        return _strdup("");
    }

    size_t in_len = strlen(s);
    // Worst-case expand ~6x for \u00XX
    size_t cap = (in_len * 6) + 1;
    char* out = calloc(cap, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < in_len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c == '\b') {
            out[j++] = '\\'; out[j++] = 'b';
        } else if (c == '\f') {
            out[j++] = '\\'; out[j++] = 'f';
        } else if (c == '\n') {
            out[j++] = '\\'; out[j++] = 'n';
        } else if (c == '\r') {
            out[j++] = '\\'; out[j++] = 'r';
        } else if (c == '\t') {
            out[j++] = '\\'; out[j++] = 't';
        } else if (c < 0x20) {
            // control chars as \u00XX
            int n = snprintf(out + j, cap - j, "\\u%04x", (unsigned int)c);
            if (n < 0) {
                free(out);
                return NULL;
            }
            j += (size_t)n;
        } else {
            out[j++] = (char)c;
        }
    }
    out[j] = '\0';
    return out;
}

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

    // Load ComputeCore.dll for synchronous wait helpers (Win10 1809+).
    // Some installations may still export these from vmcompute.dll, so we resolve from either.
    g_hcs.hComputeCore = LoadLibraryA("computecore.dll");
    g_hcs.HcsWaitForOperationResult = (HcsWaitForOperationResult_t)__get_proc_any("HcsWaitForOperationResult");
    g_hcs.HcsWaitForOperationResultAndProcessInfo = (HcsWaitForOperationResultAndProcessInfo_t)__get_proc_any("HcsWaitForOperationResultAndProcessInfo");

    // Check that we got all required functions
    if (!g_hcs.HcsCreateComputeSystem || !g_hcs.HcsStartComputeSystem ||
        !g_hcs.HcsShutdownComputeSystem || !g_hcs.HcsTerminateComputeSystem ||
        !g_hcs.HcsCreateProcess || !g_hcs.HcsCreateOperation ||
        !g_hcs.HcsCloseOperation || !g_hcs.HcsCloseComputeSystem ||
        !g_hcs.HcsCloseProcess) {
        
        VLOG_ERROR("containerv[hcs]", "failed to load required HCS functions\n");
        if (g_hcs.hComputeCore != NULL) {
            FreeLibrary(g_hcs.hComputeCore);
        }
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
        if (g_hcs.hComputeCore != NULL) {
            FreeLibrary(g_hcs.hComputeCore);
        }
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
                                L"\"Path\":\"%hs\\\\container.vhdx\","
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

    // Generate VM configuration
    config = __hcs_create_vm_config(container, options);
    if (!config) {
        VLOG_ERROR("containerv[hcs]", "failed to create VM configuration\n");
        return -1;
    }

    // Create operation handle
    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
        goto cleanup;
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

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        __hcs_localfree_wstr(resultDoc);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "compute system create wait failed: 0x%lx\n", hr);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
    } else {
        VLOG_WARNING("containerv[hcs]", "HcsWaitForOperationResult not available; VM start may be unreliable\n");
    }

    g_hcs.HcsCloseOperation(operation);
    operation = NULL;

    hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation for start: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
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

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
        __hcs_localfree_wstr(resultDoc);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "compute system start wait failed: 0x%lx\n", hr);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
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

    if (container->vm_started) {
        // Try graceful shutdown first
        hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
        if (FAILED(hr)) {
            VLOG_WARNING("containerv[hcs]", "failed to create operation for shutdown: 0x%lx\n", hr);
            operation = NULL;
        }

        hr = g_hcs.HcsShutdownComputeSystem(container->hcs_system, operation, NULL);
        if (SUCCEEDED(hr) && g_hcs.HcsWaitForOperationResult != NULL && operation != NULL) {
            PWSTR resultDoc = NULL;
            HRESULT whr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
            __hcs_localfree_wstr(resultDoc);
            if (FAILED(whr)) {
                hr = whr;
            }
        }

        if (operation != NULL) {
            g_hcs.HcsCloseOperation(operation);
            operation = NULL;
        }

        if (FAILED(hr)) {
            VLOG_WARNING("containerv[hcs]", "graceful shutdown failed, forcing termination: 0x%lx\n", hr);

            hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
            if (FAILED(hr)) {
                VLOG_WARNING("containerv[hcs]", "failed to create operation for terminate: 0x%lx\n", hr);
                operation = NULL;
            }

            hr = g_hcs.HcsTerminateComputeSystem(container->hcs_system, operation, NULL);
            if (SUCCEEDED(hr) && g_hcs.HcsWaitForOperationResult != NULL && operation != NULL) {
                PWSTR resultDoc = NULL;
                HRESULT whr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
                __hcs_localfree_wstr(resultDoc);
                if (FAILED(whr)) {
                    hr = whr;
                }
            }

            if (FAILED(hr)) {
                VLOG_ERROR("containerv[hcs]", "failed to terminate compute system: 0x%lx\n", hr);
                status = -1;
            }

            if (operation != NULL) {
                g_hcs.HcsCloseOperation(operation);
                operation = NULL;
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
    HCS_PROCESS* processOut,
    HCS_PROCESS_INFORMATION* processInfoOut)
{
    HCS_OPERATION operation = NULL;
    wchar_t* process_config = NULL;
    HCS_PROCESS process = NULL;
    HRESULT hr;
    int status = -1;
    char* json_utf8 = NULL;
    size_t json_cap = 0;
    size_t json_len = 0;

    if (!container || !container->hcs_system || !options || !options->path) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating process in VM: %s\n", options->path);

    const int guest_is_windows = (container->guest_is_windows != 0);

    const struct containerv_policy* policy = container->policy;
    enum containerv_security_level security_level = CV_SECURITY_DEFAULT;
    int win_use_app_container = 0;
    const char* win_integrity_level = NULL;
    const char* const* win_capability_sids = NULL;
    int win_capability_sid_count = 0;
    if (policy != NULL) {
        security_level = containerv_policy_get_security_level(policy);
        (void)containerv_policy_get_windows_isolation(
            policy,
            &win_use_app_container,
            &win_integrity_level,
            &win_capability_sids,
            &win_capability_sid_count);
    }

    // Build command line: path + optional argv (quoted conservatively)
    char* cmd_utf8 = NULL;
    size_t cmd_cap = 0;
    size_t cmd_len = 0;
    if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "%s", options->path) != 0) {
        goto cleanup;
    }

    if (options->argv != NULL) {
        for (int i = 1; options->argv[i] != NULL; ++i) {
            const char* arg = options->argv[i];
            if (arg == NULL) {
                continue;
            }

            int needs_quotes = 0;
            for (const char* p = arg; *p; ++p) {
                if (*p == ' ' || *p == '\t' || *p == '"') {
                    needs_quotes = 1;
                    break;
                }
            }

            if (!needs_quotes) {
                if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, " %s", arg) != 0) {
                    goto cleanup;
                }
                continue;
            }

            // Quote and escape quotes.
            if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, " \"") != 0) {
                goto cleanup;
            }
            for (const char* p = arg; *p; ++p) {
                if (*p == '"') {
                    if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "\\\"") != 0) {
                        goto cleanup;
                    }
                } else {
                    if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "%c", *p) != 0) {
                        goto cleanup;
                    }
                }
            }
            if (__appendf(&cmd_utf8, &cmd_cap, &cmd_len, "\"") != 0) {
                goto cleanup;
            }
        }
    }

    char* esc_cmd = __json_escape_utf8(cmd_utf8);
    free(cmd_utf8);
    if (esc_cmd == NULL) {
        goto cleanup;
    }

    // Build environment object. Always include a default PATH if not provided.
    int has_path = 0;
    int has_policy_level = 0;
    int has_policy_appc = 0;
    int has_policy_integrity = 0;
    int has_policy_caps = 0;
    if (options->envv != NULL) {
        for (int i = 0; options->envv[i] != NULL; ++i) {
            if (_strnicmp(options->envv[i], "PATH=", 5) == 0) {
                has_path = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_SECURITY_LEVEL=", 30) == 0) {
                has_policy_level = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_USE_APPCONTAINER=", 38) == 0) {
                has_policy_appc = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_INTEGRITY_LEVEL=", 36) == 0) {
                has_policy_integrity = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_CAPABILITY_SIDS=", 39) == 0) {
                has_policy_caps = 1;
                continue;
            }
        }
    }

    char* env_utf8 = NULL;
    size_t env_cap = 0;
    size_t env_len = 0;
    if (__appendf(&env_utf8, &env_cap, &env_len, "{") != 0) {
        free(esc_cmd);
        goto cleanup;
    }

    int first = 1;
    if (!has_path) {
        char* esc_key = __json_escape_utf8("PATH");
        const char* default_path = guest_is_windows
            ? "C:\\Windows\\System32;C:\\Windows"
            : "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
        char* esc_val = __json_escape_utf8(default_path);
        if (esc_key == NULL || esc_val == NULL) {
            free(esc_key);
            free(esc_val);
            free(env_utf8);
            free(esc_cmd);
            goto cleanup;
        }
        if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
            free(esc_key);
            free(esc_val);
            free(env_utf8);
            free(esc_cmd);
            goto cleanup;
        }
        free(esc_key);
        free(esc_val);
        first = 0;
    }

    if (options->envv != NULL) {
        for (int i = 0; options->envv[i] != NULL; ++i) {
            const char* kv = options->envv[i];
            const char* eq = strchr(kv, '=');
            if (eq == NULL || eq == kv) {
                continue;
            }
            size_t key_len = (size_t)(eq - kv);
            char* key = calloc(key_len + 1, 1);
            if (key == NULL) {
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            memcpy(key, kv, key_len);
            key[key_len] = '\0';
            const char* val = eq + 1;

            char* esc_key = __json_escape_utf8(key);
            char* esc_val = __json_escape_utf8(val);
            free(key);
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }

            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }
    }

    // Inject policy metadata for in-VM agents/workloads (best-effort).
    if (policy != NULL) {
        if (!has_policy_level) {
            const char* lvl = "default";
            if (security_level >= CV_SECURITY_STRICT) {
                lvl = "strict";
            } else if (security_level >= CV_SECURITY_RESTRICTED) {
                lvl = "restricted";
            }
            char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_SECURITY_LEVEL");
            char* esc_val = __json_escape_utf8(lvl);
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }

        // Windows-specific policy metadata only applies to Windows guests.
        if (guest_is_windows && !has_policy_appc) {
            char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_WIN_USE_APPCONTAINER");
            char* esc_val = __json_escape_utf8(win_use_app_container ? "1" : "0");
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }

        if (guest_is_windows && !has_policy_integrity && win_integrity_level != NULL && win_integrity_level[0] != '\0') {
            char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_WIN_INTEGRITY_LEVEL");
            char* esc_val = __json_escape_utf8(win_integrity_level);
            if (esc_key == NULL || esc_val == NULL) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                free(esc_key);
                free(esc_val);
                free(env_utf8);
                free(esc_cmd);
                goto cleanup;
            }
            free(esc_key);
            free(esc_val);
            first = 0;
        }

        if (guest_is_windows && !has_policy_caps && win_capability_sids != NULL && win_capability_sid_count > 0) {
            // Comma-separated list.
            char* caps = NULL;
            size_t caps_cap = 0;
            size_t caps_len = 0;
            for (int i = 0; i < win_capability_sid_count; i++) {
                if (win_capability_sids[i] == NULL) {
                    continue;
                }
                if (__appendf(&caps, &caps_cap, &caps_len, "%s%s", (caps_len == 0) ? "" : ",", win_capability_sids[i]) != 0) {
                    free(caps);
                    free(env_utf8);
                    free(esc_cmd);
                    goto cleanup;
                }
            }
            if (caps != NULL && caps[0] != '\0') {
                char* esc_key = __json_escape_utf8("CHEF_CONTAINERV_WIN_CAPABILITY_SIDS");
                char* esc_val = __json_escape_utf8(caps);
                free(caps);
                if (esc_key == NULL || esc_val == NULL) {
                    free(esc_key);
                    free(esc_val);
                    free(env_utf8);
                    free(esc_cmd);
                    goto cleanup;
                }
                if (__appendf(&env_utf8, &env_cap, &env_len, "%s\"%s\":\"%s\"", first ? "" : ",", esc_key, esc_val) != 0) {
                    free(esc_key);
                    free(esc_val);
                    free(env_utf8);
                    free(esc_cmd);
                    goto cleanup;
                }
                free(esc_key);
                free(esc_val);
                first = 0;
            } else {
                free(caps);
            }
        }
    }

    if (__appendf(&env_utf8, &env_cap, &env_len, "}") != 0) {
        free(env_utf8);
        free(esc_cmd);
        goto cleanup;
    }

    int try_security_fields = 0;
    if (guest_is_windows && policy != NULL) {
        if (security_level >= CV_SECURITY_RESTRICTED || win_use_app_container || (win_integrity_level != NULL && win_integrity_level[0] != '\0')) {
            try_security_fields = 1;
        }
    }

    for (int attempt = 0; attempt < 2; attempt++) {
        int include_security = (attempt == 0) && try_security_fields;

        // (Re)create operation handle
        if (operation != NULL) {
            g_hcs.HcsCloseOperation(operation);
            operation = NULL;
        }
        hr = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback, &operation);
        if (FAILED(hr)) {
            VLOG_ERROR("containerv[hcs]", "failed to create HCS operation: 0x%lx\n", hr);
            goto cleanup;
        }

        free(json_utf8);
        json_utf8 = NULL;
        json_cap = 0;
        json_len = 0;

        // Build process configuration JSON (UTF-8) then convert to wide.
        const char* working_dir = guest_is_windows ? "C:\\\\" : "/";
        const char* emulate_console = guest_is_windows ? "true" : "false";
        if (__appendf(
                &json_utf8,
                &json_cap,
                &json_len,
                "{"
                "\"CommandLine\":\"%s\","
                "\"WorkingDirectory\":\"%s\","
                "%s"
                "\"Environment\":%s,"
                "\"EmulateConsole\":%s,"
            "\"CreateStdInPipe\":%s,"
            "\"CreateStdOutPipe\":%s,"
            "\"CreateStdErrPipe\":%s"
                "}",
                esc_cmd,
                working_dir,
                (guest_is_windows && include_security) ? "\"User\":\"ContainerUser\"," : "",
            env_utf8,
            emulate_console,
            options->create_stdio_pipes ? "true" : "false",
            options->create_stdio_pipes ? "true" : "false",
            options->create_stdio_pipes ? "true" : "false") != 0) {
            goto cleanup;
        }

        if (process_config) {
            free(process_config);
            process_config = NULL;
        }

        int wlen = MultiByteToWideChar(CP_UTF8, 0, json_utf8, -1, NULL, 0);
        if (wlen <= 0) {
            VLOG_ERROR("containerv[hcs]", "failed to size wide process config\n");
            goto cleanup;
        }

        process_config = calloc((size_t)wlen, sizeof(wchar_t));
        if (!process_config) {
            goto cleanup;
        }

        if (MultiByteToWideChar(CP_UTF8, 0, json_utf8, -1, process_config, wlen) == 0) {
            VLOG_ERROR("containerv[hcs]", "failed to convert process config to wide string\n");
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

        if (SUCCEEDED(hr)) {
            // Wait for completion and optionally fetch stdio handles.
            PWSTR resultDoc = NULL;
            HCS_PROCESS_INFORMATION pi;
            memset(&pi, 0, sizeof(pi));

            HRESULT whr = S_OK;
            if (g_hcs.HcsWaitForOperationResultAndProcessInfo != NULL) {
                HCS_PROCESS_INFORMATION* piPtr = NULL;
                if (options->create_stdio_pipes) {
                    piPtr = &pi;
                }
                whr = g_hcs.HcsWaitForOperationResultAndProcessInfo(operation, INFINITE, piPtr, &resultDoc);
            } else if (g_hcs.HcsWaitForOperationResult != NULL) {
                whr = g_hcs.HcsWaitForOperationResult(operation, INFINITE, &resultDoc);
            } else {
                VLOG_WARNING("containerv[hcs]", "no operation wait API available; process creation may be unreliable\n");
            }

            __hcs_localfree_wstr(resultDoc);

            if (FAILED(whr)) {
                if (g_hcs.HcsCloseProcess) {
                    g_hcs.HcsCloseProcess(process);
                }
                process = NULL;

                if (include_security) {
                    VLOG_WARNING("containerv[hcs]", "process create wait rejected security fields (hr=0x%lx); retrying without them\n", whr);
                    continue;
                }

                VLOG_ERROR("containerv[hcs]", "failed to wait for process creation: 0x%lx\n", whr);
                break;
            }

            if (processOut) {
                *processOut = process;
            }

            if (options->create_stdio_pipes) {
                if (processInfoOut != NULL) {
                    *processInfoOut = pi;
                } else {
                    if (pi.StdInput != NULL) {
                        CloseHandle(pi.StdInput);
                    }
                    if (pi.StdOutput != NULL) {
                        CloseHandle(pi.StdOutput);
                    }
                    if (pi.StdError != NULL) {
                        CloseHandle(pi.StdError);
                    }
                }
            }

            status = 0;
            VLOG_DEBUG("containerv[hcs]", "successfully created process in VM\n");
            break;
        }

        if (include_security) {
            VLOG_WARNING("containerv[hcs]", "process create rejected security fields (hr=0x%lx); retrying without them\n", hr);
            continue;
        }

        VLOG_ERROR("containerv[hcs]", "failed to create process in VM: 0x%lx\n", hr);
        break;
    }

    free(env_utf8);
    env_utf8 = NULL;
    free(esc_cmd);
    esc_cmd = NULL;

cleanup:
    free(json_utf8);
    free(env_utf8);
    free(esc_cmd);
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
    if (!process) {
        return -1;
    }
    
    DWORD wait_ms = (DWORD)timeout_ms;
    VLOG_DEBUG("containerv[hcs]", "waiting for process completion (timeout: %lu ms)\n", (unsigned long)wait_ms);

    DWORD wr = WaitForSingleObject((HANDLE)process, wait_ms);
    if (wr == WAIT_OBJECT_0) {
        VLOG_DEBUG("containerv[hcs]", "process wait completed\n");
        return 0;
    }
    if (wr == WAIT_TIMEOUT) {
        errno = ETIMEDOUT;
        return -1;
    }

    VLOG_ERROR("containerv[hcs]", "WaitForSingleObject failed: %lu\n", GetLastError());
    errno = EIO;
    return -1;
}

/**
 * @brief Get HCS process exit code
 */
int __hcs_get_process_exit_code(HCS_PROCESS process, unsigned long* exit_code)
{
    if (!process || !exit_code) {
        return -1;
    }

    DWORD code = 0;
    if (!GetExitCodeProcess((HANDLE)process, &code)) {
        VLOG_ERROR("containerv[hcs]", "GetExitCodeProcess failed: %lu\n", GetLastError());
        errno = EIO;
        return -1;
    }

    *exit_code = (unsigned long)code;
    return 0;
}
