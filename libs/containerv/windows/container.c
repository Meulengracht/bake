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

#include <windows.h>
#include <wincrypt.h>
#include <objbase.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <chef/platform.h>
#include <chef/containerv.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include <pid1_windows.h>

#include "private.h"

#define MIN_REMAINING_PATH_LENGTH 20  // Minimum space needed for "containerv-XXXXXX" + null

// PID1 is currently implemented as a process-global service. We reference count
// active containers so we can init/cleanup once.
static volatile LONG g_pid1_container_refcount = 0;
static volatile LONG g_pid1_ready = 0;

static int __pid1_acquire_for_container(struct containerv_container* container)
{
    if (container == NULL) {
        return -1;
    }

    LONG after = InterlockedIncrement(&g_pid1_container_refcount);
    if (after == 1) {
        if (pid1_init() != 0) {
            InterlockedDecrement(&g_pid1_container_refcount);
            return -1;
        }
        InterlockedExchange(&g_pid1_ready, 1);
    }

    container->pid1_acquired = 1;
    return 0;
}

static void __pid1_release_for_container(void)
{
    LONG after = InterlockedDecrement(&g_pid1_container_refcount);
    if (after == 0) {
        if (InterlockedExchange(&g_pid1_ready, 0) == 1) {
            (void)pid1_cleanup();
        }
    }
}

static int __appendf(char** buf, size_t* cap, size_t* len, const char* fmt, ...)
{
    if (buf == NULL || cap == NULL || len == NULL || fmt == NULL) {
        return -1;
    }

    va_list ap;
    va_start(ap, fmt);
    int needed = _vscprintf(fmt, ap);
    va_end(ap);
    if (needed < 0) {
        return -1;
    }

    size_t required = *len + (size_t)needed + 1;
    if (*buf == NULL || *cap < required) {
        size_t new_cap = (*cap == 0) ? 256 : *cap;
        while (new_cap < required) {
            new_cap *= 2;
        }
        char* nb = realloc(*buf, new_cap);
        if (nb == NULL) {
            return -1;
        }
        *buf = nb;
        *cap = new_cap;
    }

    va_start(ap, fmt);
    int written = vsnprintf(*buf + *len, *cap - *len, fmt, ap);
    va_end(ap);
    if (written < 0) {
        return -1;
    }
    *len += (size_t)written;
    return 0;
}

static char* __json_escape_utf8_simple(const char* s)
{
    if (s == NULL) {
        return NULL;
    }

    char* out = NULL;
    size_t cap = 0;
    size_t len = 0;

    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        unsigned char c = *p;
        switch (c) {
            case '"':
                if (__appendf(&out, &cap, &len, "\\\"") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            case '\\':
                if (__appendf(&out, &cap, &len, "\\\\") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            case '\b':
                if (__appendf(&out, &cap, &len, "\\b") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            case '\f':
                if (__appendf(&out, &cap, &len, "\\f") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            case '\n':
                if (__appendf(&out, &cap, &len, "\\n") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            case '\r':
                if (__appendf(&out, &cap, &len, "\\r") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            case '\t':
                if (__appendf(&out, &cap, &len, "\\t") != 0) {
                    free(out);
                    return NULL;
                }
                break;
            default:
                if (c < 0x20) {
                    if (__appendf(&out, &cap, &len, "\\u%04x", (unsigned int)c) != 0) {
                        free(out);
                        return NULL;
                    }
                } else {
                    if (__appendf(&out, &cap, &len, "%c", (char)c) != 0) {
                        free(out);
                        return NULL;
                    }
                }
                break;
        }
    }

    if (out == NULL) {
        out = _strdup("");
    }
    return out;
}

static int __pid1d_write_all(HANDLE h, const char* data, size_t len)
{
    if (h == NULL || data == NULL) {
        return -1;
    }

    size_t written_total = 0;
    while (written_total < len) {
        DWORD written = 0;
        BOOL ok = WriteFile(h, data + written_total, (DWORD)(len - written_total), &written, NULL);
        if (!ok) {
            return -1;
        }
        written_total += (size_t)written;
    }
    return 0;
}

static int __pid1d_read_line(HANDLE h, char* out, size_t out_cap)
{
    if (h == NULL || out == NULL || out_cap == 0) {
        return -1;
    }

    size_t n = 0;
    for (;;) {
        char ch = 0;
        DWORD read = 0;
        BOOL ok = ReadFile(h, &ch, 1, &read, NULL);
        if (!ok || read == 0) {
            return -1;
        }

        if (ch == '\n') {
            break;
        }
        if (ch == '\r') {
            continue;
        }

        if (n + 1 >= out_cap) {
            return -1;
        }
        out[n++] = ch;
    }

    out[n] = '\0';
    return 0;
}

static int __pid1d_resp_ok(const char* resp)
{
    if (resp == NULL) {
        return 0;
    }
    return strstr(resp, "\"ok\":true") != NULL;
}

static int __pid1d_parse_uint64_field(const char* resp, const char* key, uint64_t* out)
{
    if (resp == NULL || key == NULL || out == NULL) {
        return -1;
    }

    char needle[64];
    snprintf(needle, sizeof(needle), "\"%s\":", key);
    const char* p = strstr(resp, needle);
    if (p == NULL) {
        return -1;
    }
    p += strlen(needle);

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    char* endp = NULL;
    unsigned long long v = strtoull(p, &endp, 10);
    if (endp == p) {
        return -1;
    }
    *out = (uint64_t)v;
    return 0;
}

static int __pid1d_parse_int_field(const char* resp, const char* key, int* out)
{
    uint64_t v = 0;
    if (__pid1d_parse_uint64_field(resp, key, &v) != 0) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static int __pid1d_rpc(struct containerv_container* container, const char* req_line, char* resp, size_t resp_cap)
{
    if (container == NULL || req_line == NULL || resp == NULL) {
        return -1;
    }
    if (container->pid1d_stdin == NULL || container->pid1d_stdout == NULL) {
        return -1;
    }

    size_t req_len = strlen(req_line);
    if (__pid1d_write_all(container->pid1d_stdin, req_line, req_len) != 0) {
        return -1;
    }
    if (__pid1d_write_all(container->pid1d_stdin, "\n", 1) != 0) {
        return -1;
    }
    if (__pid1d_read_line(container->pid1d_stdout, resp, resp_cap) != 0) {
        return -1;
    }
    return 0;
}

static void __pid1d_close_session(struct containerv_container* container)
{
    if (container == NULL) {
        return;
    }

    if (container->pid1d_stdin != NULL) {
        CloseHandle(container->pid1d_stdin);
        container->pid1d_stdin = NULL;
    }
    if (container->pid1d_stdout != NULL) {
        CloseHandle(container->pid1d_stdout);
        container->pid1d_stdout = NULL;
    }
    if (container->pid1d_stderr != NULL) {
        CloseHandle(container->pid1d_stderr);
        container->pid1d_stderr = NULL;
    }

    if (container->pid1d_process != NULL) {
        if (g_hcs.HcsCloseProcess != NULL) {
            g_hcs.HcsCloseProcess(container->pid1d_process);
        } else {
            CloseHandle((HANDLE)container->pid1d_process);
        }
        container->pid1d_process = NULL;
    }

    container->pid1d_started = 0;
}

static int __pid1d_ensure(struct containerv_container* container)
{
    if (container == NULL || container->hcs_system == NULL) {
        return -1;
    }
    if (container->pid1d_started) {
        return 0;
    }

    const char* pid1d_path = container->guest_is_windows ? "C:\\pid1d.exe" : "/usr/bin/pid1d";
    const char* const argv[] = { pid1d_path, NULL };

    struct __containerv_spawn_options opts;
    memset(&opts, 0, sizeof(opts));
    opts.path = pid1d_path;
    opts.argv = argv;
    opts.flags = 0;
    opts.create_stdio_pipes = 1;

    HCS_PROCESS proc = NULL;
    HCS_PROCESS_INFORMATION info;
    memset(&info, 0, sizeof(info));

    int status = __hcs_create_process(container, &opts, &proc, &info);
    if (status != 0) {
        VLOG_ERROR("containerv", "pid1d: failed to start in VM\n");
        return -1;
    }

    if (info.StdInput == NULL || info.StdOutput == NULL) {
        VLOG_ERROR("containerv", "pid1d: missing stdio pipes (ComputeCore wait API unavailable?)\n");
        if (g_hcs.HcsCloseProcess != NULL && proc != NULL) {
            g_hcs.HcsCloseProcess(proc);
        }
        return -1;
    }

    container->pid1d_process = proc;
    container->pid1d_stdin = info.StdInput;
    container->pid1d_stdout = info.StdOutput;
    container->pid1d_stderr = info.StdError;
    container->pid1d_started = 1;

    char resp[8192];
    if (__pid1d_rpc(container, "{\"op\":\"ping\"}", resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d: ping failed: %s\n", resp);
        __pid1d_close_session(container);
        return -1;
    }

    VLOG_DEBUG("containerv", "pid1d: session established\n");
    return 0;
}

static int __pid1d_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, uint64_t* id_out)
{
    if (container == NULL || options == NULL || options->path == NULL || id_out == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    char* req = NULL;
    size_t cap = 0;
    size_t len = 0;

    char* esc_cmd = __json_escape_utf8_simple(options->path);
    if (esc_cmd == NULL) {
        return -1;
    }

    if (__appendf(&req, &cap, &len, "{\"op\":\"spawn\",\"command\":\"%s\"", esc_cmd) != 0) {
        free(esc_cmd);
        free(req);
        return -1;
    }
    free(esc_cmd);

    if (__appendf(&req, &cap, &len, ",\"wait\":%s", (options->flags & CV_SPAWN_WAIT) ? "true" : "false") != 0) {
        free(req);
        return -1;
    }

    if (options->argv != NULL) {
        if (__appendf(&req, &cap, &len, ",\"args\":[") != 0) {
            free(req);
            return -1;
        }
        int first = 1;
        for (int i = 0; options->argv[i] != NULL; ++i) {
            char* esc = __json_escape_utf8_simple(options->argv[i]);
            if (esc == NULL) {
                free(req);
                return -1;
            }
            if (__appendf(&req, &cap, &len, "%s\"%s\"", first ? "" : ",", esc) != 0) {
                free(esc);
                free(req);
                return -1;
            }
            free(esc);
            first = 0;
        }
        if (__appendf(&req, &cap, &len, "]") != 0) {
            free(req);
            return -1;
        }
    }

    if (options->envv != NULL) {
        if (__appendf(&req, &cap, &len, ",\"env\":[") != 0) {
            free(req);
            return -1;
        }
        int first = 1;
        for (int i = 0; options->envv[i] != NULL; ++i) {
            char* esc = __json_escape_utf8_simple(options->envv[i]);
            if (esc == NULL) {
                free(req);
                return -1;
            }
            if (__appendf(&req, &cap, &len, "%s\"%s\"", first ? "" : ",", esc) != 0) {
                free(esc);
                free(req);
                return -1;
            }
            free(esc);
            first = 0;
        }
        if (__appendf(&req, &cap, &len, "]") != 0) {
            free(req);
            return -1;
        }
    }

    if (__appendf(&req, &cap, &len, "}") != 0) {
        free(req);
        return -1;
    }

    char resp[8192];
    int rc = __pid1d_rpc(container, req, resp, sizeof(resp));
    free(req);
    if (rc != 0 || !__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d: spawn failed: %s\n", resp);
        return -1;
    }

    if (__pid1d_parse_uint64_field(resp, "id", id_out) != 0) {
        VLOG_ERROR("containerv", "pid1d: spawn missing id: %s\n", resp);
        return -1;
    }

    return 0;
}

static int __pid1d_wait(struct containerv_container* container, uint64_t id, int* exit_code_out)
{
    if (container == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    char req[256];
    snprintf(req, sizeof(req), "{\"op\":\"wait\",\"id\":%llu}", (unsigned long long)id);
    char resp[8192];
    if (__pid1d_rpc(container, req, resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d: wait failed: %s\n", resp);
        return -1;
    }

    int ec = 0;
    (void)__pid1d_parse_int_field(resp, "exit_code", &ec);
    if (exit_code_out != NULL) {
        *exit_code_out = ec;
    }
    return 0;
}

static int __pid1d_kill_reap(struct containerv_container* container, uint64_t id)
{
    if (container == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    char req[256];
    snprintf(req, sizeof(req), "{\"op\":\"kill\",\"id\":%llu,\"reap\":true}", (unsigned long long)id);
    char resp[8192];
    if (__pid1d_rpc(container, req, resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d: kill failed: %s\n", resp);
        return -1;
    }
    return 0;
}

static char* __build_environment_block(const char* const* envv)
{
    if (envv == NULL) {
        return NULL;
    }

    size_t total = 1; // final terminator
    for (int i = 0; envv[i] != NULL; ++i) {
        total += strlen(envv[i]) + 1;
    }

    char* block = calloc(total, 1);
    if (block == NULL) {
        return NULL;
    }

    size_t at = 0;
    for (int i = 0; envv[i] != NULL; ++i) {
        size_t n = strlen(envv[i]);
        memcpy(block + at, envv[i], n);
        at += n;
        block[at++] = '\0';
    }
    block[at++] = '\0';
    return block;
}

static wchar_t* __utf8_to_wide_alloc(const char* s)
{
    if (s == NULL) {
        return NULL;
    }
    int needed = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }
    wchar_t* out = calloc((size_t)needed, sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, s, -1, out, needed) == 0) {
        free(out);
        return NULL;
    }
    return out;
}

static wchar_t* __build_environment_block_wide(const char* const* envv)
{
    if (envv == NULL) {
        return NULL;
    }

    size_t total_wchars = 1; // final terminator
    for (int i = 0; envv[i] != NULL; ++i) {
        int n = MultiByteToWideChar(CP_UTF8, 0, envv[i], -1, NULL, 0);
        if (n <= 0) {
            return NULL;
        }
        // n already includes null terminator for that string.
        total_wchars += (size_t)n;
    }

    wchar_t* block = calloc(total_wchars, sizeof(wchar_t));
    if (block == NULL) {
        return NULL;
    }

    size_t at = 0;
    for (int i = 0; envv[i] != NULL; ++i) {
        int n = MultiByteToWideChar(CP_UTF8, 0, envv[i], -1, block + at, (int)(total_wchars - at));
        if (n <= 0) {
            free(block);
            return NULL;
        }
        at += (size_t)n;
    }
    block[at++] = L'\0';
    return block;
}

static char* __container_create_runtime_dir(void)
{
    char template[MAX_PATH];
    char* directory;
    DWORD result;
    
    // Get temp path
    result = GetTempPathA(MAX_PATH, template);
    if (result == 0 || result > MAX_PATH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to get temp path\n");
        return NULL;
    }
    
    // Create a unique subdirectory for the container
    // strcat_s second parameter is the total buffer size, not remaining space
    size_t remaining = MAX_PATH - strlen(template);
    if (remaining < MIN_REMAINING_PATH_LENGTH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: temp path too long\n");
        return NULL;
    }
    strcat_s(template, MAX_PATH, "containerv-XXXXXX");
    
    // Use _mktemp_s to create unique name
    if (_mktemp_s(template, strlen(template) + 1) != 0) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create unique name\n");
        return NULL;
    }
    
    // Create the directory
    if (!CreateDirectoryA(template, NULL)) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create directory: %s\n", template);
        return NULL;
    }
    
    directory = _strdup(template);
    return directory;
}

void containerv_generate_id(char* buffer, size_t length)
{
    const char charset[] = "0123456789abcdef";
    HCRYPTPROV hCryptProv;
    BYTE random_bytes[__CONTAINER_ID_LENGTH / 2];  // Each byte generates 2 hex chars
    
    if (length < __CONTAINER_ID_LENGTH + 1) {
        return;
    }
    
    // Use Windows Crypto API for random generation
    if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptGenRandom(hCryptProv, sizeof(random_bytes), random_bytes)) {
            for (size_t i = 0; i < sizeof(random_bytes); i++) {
                buffer[i * 2] = charset[(random_bytes[i] >> 4) & 0x0F];
                buffer[i * 2 + 1] = charset[random_bytes[i] & 0x0F];
            }
            buffer[__CONTAINER_ID_LENGTH] = '\0';
            CryptReleaseContext(hCryptProv, 0);
            return;
        }
        CryptReleaseContext(hCryptProv, 0);
    }
    
    // If crypto API fails, use GetTickCount64 + process ID as fallback
    // This is not cryptographically secure but better than rand()
    ULONGLONG tick = GetTickCount64();
    DWORD pid = GetCurrentProcessId();
    ULONGLONG combined = (tick << 32) | pid;
    
    for (size_t i = 0; i < __CONTAINER_ID_LENGTH; i++) {
        buffer[i] = charset[(combined >> (i * 4)) & 0x0F];
    }
    buffer[__CONTAINER_ID_LENGTH] = '\0';
}

static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;

    container = calloc(1, sizeof(struct containerv_container));
    if (container == NULL) {
        return NULL;
    }

    container->runtime_dir = __container_create_runtime_dir();
    if (container->runtime_dir == NULL) {
        free(container);
        return NULL;
    }
    
    // Create staging directory for file transfers
    char staging_path[MAX_PATH];
    sprintf_s(staging_path, sizeof(staging_path), "%s\\staging", container->runtime_dir);
    if (!CreateDirectoryA(staging_path, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            VLOG_WARNING("containerv", "failed to create staging directory: %lu\n", error);
        }
    }
    
    // Generate container ID
    containerv_generate_id(container->id, sizeof(container->id));

    // Convert container ID to wide string for HCS
    size_t id_len = strlen(container->id);
    container->vm_id = calloc(id_len + 1, sizeof(wchar_t));
    if (container->vm_id == NULL) {
        free(container->runtime_dir);
        free(container);
        return NULL;
    }
    
    if (MultiByteToWideChar(CP_UTF8, 0, container->id, -1, container->vm_id, (int)id_len + 1) == 0) {
        free(container->vm_id);
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    // Use container ID as hostname
    container->hostname = _strdup(container->id);
    if (container->hostname == NULL) {
        free(container->vm_id);
        free(container->runtime_dir);
        free(container);
        return NULL;
    }

    container->hcs_system = NULL;
    container->host_pipe = INVALID_HANDLE_VALUE;
    container->child_pipe = INVALID_HANDLE_VALUE;
    container->vm_started = 0;
    list_init(&container->processes);
    container->policy = NULL;

    container->guest_is_windows = 1;
    container->pid1d_process = NULL;
    container->pid1d_stdin = NULL;
    container->pid1d_stdout = NULL;
    container->pid1d_stderr = NULL;
    container->pid1d_started = 0;
    container->pid1_acquired = 0;

    return container;
}

static void __container_delete(struct containerv_container* container)
{
    struct list_item* i;
    int pid1_acquired;
    
    if (!container) {
        return;
    }

    pid1_acquired = container->pid1_acquired;
    
    // Clean up processes
    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;
        
        if (proc->handle != NULL) {
            if (container->hcs_system != NULL) {
                if (proc->is_guest) {
                    free(proc->handle);
                } else {
                    if (g_hcs.HcsCloseProcess != NULL) {
                        g_hcs.HcsCloseProcess((HCS_PROCESS)proc->handle);
                    } else {
                        CloseHandle(proc->handle);
                    }
                }
            } else {
                if (g_pid1_ready) {
                    pid1_windows_untrack(proc->handle);
                }
                CloseHandle(proc->handle);
            }
        }
        free(proc);
    }

    if (container->hcs_system != NULL) {
        __pid1d_close_session(container);
        __hcs_destroy_vm(container);
    }

    // Closing the job object triggers JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE.
    if (container->job_object != NULL) {
        CloseHandle(container->job_object);
        container->job_object = NULL;
    }
    
    if (container->host_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(container->host_pipe);
    }
    
    if (container->child_pipe != INVALID_HANDLE_VALUE) {
        CloseHandle(container->child_pipe);
    }
    
    free(container->vm_id);
    free(container->hostname);
    free(container->runtime_dir);
    free(container->rootfs);

    if (container->policy != NULL) {
        containerv_policy_delete(container->policy);
        container->policy = NULL;
    }
    free(container);

    // Release PID1 service reference (kills remaining managed host processes on last container).
    if (pid1_acquired) {
        __pid1_release_for_container();
    }
}

int containerv_create(
    const char*                   containerId,
    struct containerv_options*    options,
    struct containerv_container** containerOut)
{
    struct containerv_container* container;
    const char*                  rootFs;
    HRESULT hr;
    
    VLOG_DEBUG("containerv", "containerv_create(containerId=%s)\n", containerId);
    
    if (containerId == NULL || containerOut == NULL) {
        return -1;
    }
    
    container = __container_new();
    if (container == NULL) {
        VLOG_ERROR("containerv", "containerv_create: failed to allocate container\n");
        return -1;
    }
    
    rootFs = containerv_layers_get_rootfs(options->layers);
    container->rootfs = _strdup(rootFs);
    if (container->rootfs == NULL) {
        __container_delete(container);
        return -1;
    }

    // Track whether the guest rootfs is expected to be Windows or Linux.
    // WSL rootfs types are Linux guests; native Windows rootfs types are Windows guests.
    container->guest_is_windows = 1;
    if (options != NULL) {
        switch (options->rootfs.type) {
            case WINDOWS_ROOTFS_WSL_UBUNTU:
            case WINDOWS_ROOTFS_WSL_DEBIAN:
            case WINDOWS_ROOTFS_WSL_ALPINE:
                container->guest_is_windows = 0;
                break;
            default:
                container->guest_is_windows = 1;
                break;
        }
    }

    // Set up rootfs if it doesn't exist or if specific rootfs type is requested
    BOOL rootfs_exists = PathFileExistsA(rootFs);
    if (!rootfs_exists || (options && options->rootfs.type != WINDOWS_ROOTFS_WSL_UBUNTU)) {
        VLOG_DEBUG("containerv", "setting up rootfs at %s\n", rootFs);
        
        struct containerv_options_rootfs* rootfs_opts = options ? &options->rootfs : NULL;
        if (__windows_setup_rootfs(rootFs, rootfs_opts) != 0) {
            VLOG_ERROR("containerv", "containerv_create: failed to setup rootfs\n");
            __container_delete(container);
            return -1;
        }
        
        VLOG_DEBUG("containerv", "rootfs setup completed at %s\n", rootFs);
    } else {
        VLOG_DEBUG("containerv", "using existing rootfs at %s\n", rootFs);
    }
    
    // Initialize COM for HyperV operations
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        VLOG_ERROR("containerv", "containerv_create: failed to initialize COM: 0x%lx\n", hr);
        __container_delete(container);
        return -1;
    }
    
    // Setup VM networking before creating the VM
    if (options && (options->capabilities & CV_CAP_NETWORK)) {
        if (__windows_configure_vm_network(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: VM network setup encountered issues\n");
            // Don't fail container creation, network might still work
        }
    }

    // Ensure we have a VM disk image available for the HCS VM.
    // For Windows guests, this supports the B1 strategy where a bootable container.vhdx is shipped in a VAFS layer.
    if (__windows_prepare_vm_disk(container, options) != 0) {
        VLOG_ERROR("containerv", "containerv_create: failed to prepare VM disk image\n");
        __container_delete(container);
        return -1;
    }

    // Create HyperV VM using Windows HCS (Host Compute Service) API
    if (__hcs_create_vm(container, options) != 0) {
        VLOG_ERROR("containerv", "containerv_create: failed to create HyperV VM\n");
        __container_delete(container);
        return -1;
    }

    // Initialize PID1 service (used for host process lifecycle management; VM processes
    // are managed through HCS). Reference-counted across containers.
    if (__pid1_acquire_for_container(container) != 0) {
        VLOG_ERROR("containerv", "containerv_create: failed to initialize PID1 service\n");
        __container_delete(container);
        return -1;
    }

    // Take ownership of the policy (options may be deleted after create)
    if (options && options->policy) {
        container->policy = options->policy;
        options->policy = NULL;
    }
    
    // Setup resource limits and/or security restrictions using Job Objects
    {
        // Always create a job object so host-spawned processes are terminated when the
        // container is destroyed (PID1-like behavior). Limits and security are layered on.
        int want_job = 1;
        if (options && (options->capabilities & CV_CAP_CGROUPS)) {
            if (options->limits.memory_max || options->limits.cpu_percent || options->limits.process_count) {
                want_job = 1;
            }
        }
        if (container->policy) {
            enum containerv_security_level level = containerv_policy_get_security_level(container->policy);
            if (level >= CV_SECURITY_RESTRICTED) {
                want_job = 1;
            }
        }

        if (want_job) {
            if (options) {
                container->resource_limits = options->limits;
            }
            container->job_object = __windows_create_job_object(
                container,
                (options && (options->limits.memory_max || options->limits.cpu_percent || options->limits.process_count))
                    ? &options->limits
                    : NULL);

            if (!container->job_object) {
                VLOG_WARNING("containerv", "containerv_create: failed to create job object\n");
            } else {
                VLOG_DEBUG("containerv", "containerv_create: created job object\n");
                if (container->policy) {
                    if (windows_apply_job_security(container->job_object, container->policy) != 0) {
                        VLOG_WARNING("containerv", "containerv_create: failed to apply job security\n");
                    }
                }
            }
        }
    }
    
    // Setup volumes and mounts for container
    if (options && (options->capabilities & CV_CAP_FILESYSTEM)) {
        if (__windows_setup_volumes(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: volume setup encountered issues\n");
            // Continue anyway, basic filesystem might still work
        }
    }
    
    // Configure host-side networking after VM is created
    if (options && (options->capabilities & CV_CAP_NETWORK)) {
        if (__windows_configure_host_network(container, options) != 0) {
            VLOG_WARNING("containerv", "containerv_create: host network setup encountered issues\n");
            // Continue anyway, VM networking might still work
        }
    }
    
    VLOG_DEBUG("containerv", "containerv_create: created container %s with HyperV VM\n", container->id);
    
    *containerOut = container;
    return 0;
}

int __containerv_spawn(
    struct containerv_container*       container,
    struct __containerv_spawn_options* options,
    HANDLE*                            handleOut)
{
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    BOOL result;
    struct containerv_container_process* proc;
    char cmdline[4096];
    
    if (!container || !options || !options->path) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_spawn(path=%s)\n", options->path);
    
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    
    // Build command line from path and arguments
    size_t cmdline_len = strlen(options->path);
    if (cmdline_len >= sizeof(cmdline)) {
        VLOG_ERROR("containerv", "__containerv_spawn: path too long\n");
        return -1;
    }
    strcpy_s(cmdline, sizeof(cmdline), options->path);
    
    if (options->argv) {
        for (int i = 1; options->argv[i] != NULL; i++) {
            size_t arg_len = strlen(options->argv[i]);
            // Check if adding " " + argument would overflow (current + space + arg + null)
            if (cmdline_len + 1 + arg_len + 1 > sizeof(cmdline)) {
                VLOG_ERROR("containerv", "__containerv_spawn: command line too long\n");
                return -1;
            }
            strcat_s(cmdline, sizeof(cmdline), " ");
            strcat_s(cmdline, sizeof(cmdline), options->argv[i]);
            cmdline_len += 1 + arg_len;
        }
    }
    
    // Check if we have a HyperV VM to run the process in
    if (container->hcs_system != NULL) {
        uint64_t guest_id = 0;
        if (__pid1d_spawn(container, options, &guest_id) != 0) {
            VLOG_ERROR("containerv", "__containerv_spawn: pid1d spawn failed\n");
            return -1;
        }

        // Configure container networking on first process spawn
        if (!container->network_configured) {
            container->network_configured = 1;
            VLOG_DEBUG("containerv", "__containerv_spawn: network setup deferred (would configure here)\n");
        }

        // Add guest process token to container's process list
        proc = calloc(1, sizeof(struct containerv_container_process));
        if (proc == NULL) {
            (void)__pid1d_kill_reap(container, guest_id);
            return -1;
        }

        void* token = malloc(1);
        if (token == NULL) {
            free(proc);
            (void)__pid1d_kill_reap(container, guest_id);
            return -1;
        }

        proc->handle = (HANDLE)token;
        proc->pid = 0;
        proc->is_guest = 1;
        proc->guest_id = guest_id;
        list_add(&container->processes, &proc->list_header);

        if (handleOut) {
            *handleOut = proc->handle;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned guest process via pid1d (id=%llu)\n", (unsigned long long)guest_id);
        return 0;
    } else {
        // Fallback to host process creation (for testing/debugging)
        VLOG_WARNING("containerv", "__containerv_spawn: no HyperV VM, creating host process as fallback\n");

        int did_secure = 0;
        if (container->policy) {
            enum containerv_security_level level = containerv_policy_get_security_level(container->policy);
            int use_app_container = 0;
            const char* integrity_level = NULL;
            const char* const* capability_sids = NULL;
            int capability_sid_count = 0;

            if (containerv_policy_get_windows_isolation(
                    container->policy,
                    &use_app_container,
                    &integrity_level,
                    &capability_sids,
                    &capability_sid_count) == 0) {
                if (level != CV_SECURITY_DEFAULT || use_app_container || integrity_level || (capability_sids && capability_sid_count > 0)) {
                    wchar_t* cmdline_w = __utf8_to_wide_alloc(cmdline);
                    wchar_t* cwd_w = __utf8_to_wide_alloc(container->rootfs);
                    wchar_t* env_w = __build_environment_block_wide(options->envv);

                    if (cmdline_w != NULL) {
                        if (windows_create_secure_process_ex(
                                container->policy,
                                cmdline_w,
                                cwd_w,
                                env_w,
                                &pi) == 0) {
                            did_secure = 1;
                            // Resume and close thread handle (CreateProcessAsUserW used CREATE_SUSPENDED)
                            ResumeThread(pi.hThread);
                            CloseHandle(pi.hThread);
                            pi.hThread = NULL;
                        }
                    }

                    free(cmdline_w);
                    free(cwd_w);
                    free(env_w);
                }
            }
        }

        if (!did_secure) {
            // Prefer PID1 abstraction for host processes: it assigns processes to a Job Object
            // for kill-on-close and maintains internal tracking.
            if (g_pid1_ready) {
                if (container->job_object != NULL) {
                    (void)pid1_windows_set_job_object_borrowed(container->job_object);
                }

                pid1_process_options_t popts = {0};
                popts.command = options->path;
                popts.args = options->argv;
                popts.environment = options->envv;
                popts.working_directory = container->rootfs;
                popts.log_path = NULL;
                popts.memory_limit_bytes = 0;
                popts.cpu_percent = 0;
                popts.process_limit = 0;
                popts.uid = 0;
                popts.gid = 0;
                popts.wait_for_exit = (options->flags & CV_SPAWN_WAIT) ? 1 : 0;
                popts.forward_signals = 1;

                if (pid1_spawn_process(&popts, &pi.hProcess) != 0) {
                    VLOG_ERROR("containerv", "__containerv_spawn: pid1_spawn_process failed\n");
                    return -1;
                }

                // We don't have a Windows PID value here (pid1 returns a HANDLE).
                pi.dwProcessId = 0;
                pi.hThread = NULL;
            } else {
                char* env_block = __build_environment_block(options->envv);

                result = CreateProcessA(
                    NULL,           // Application name
                    cmdline,        // Command line
                    NULL,           // Process security attributes
                    NULL,           // Thread security attributes
                    FALSE,          // Inherit handles
                    0,              // Creation flags
                    env_block,      // Environment (NULL = inherit)
                    container->rootfs, // Current directory
                    &si,            // Startup info
                    &pi             // Process information
                );

                free(env_block);

                if (!result) {
                    VLOG_ERROR("containerv", "__containerv_spawn: CreateProcess failed: %lu\n", GetLastError());
                    return -1;
                }

                // Close thread handle, we don't need it
                CloseHandle(pi.hThread);
            }
        }

        if (pi.hThread != NULL) {
            CloseHandle(pi.hThread);
        }

        // Add process to container's process list
        proc = calloc(1, sizeof(struct containerv_container_process));
        if (proc) {
            proc->handle = pi.hProcess;
            proc->pid = pi.dwProcessId;
            list_add(&container->processes, &proc->list_header);
            
            // Apply job object resource limits if configured
            if (container->job_object) {
                if (AssignProcessToJobObject(container->job_object, pi.hProcess)) {
                    VLOG_DEBUG("containerv", "__containerv_spawn: assigned process %lu to job object\n", pi.dwProcessId);
                } else {
                    VLOG_WARNING("containerv", "__containerv_spawn: failed to assign process %lu to job: %lu\n", 
                               pi.dwProcessId, GetLastError());
                }
            }
        } else {
            CloseHandle(pi.hProcess);
            return -1;
        }
        
        if (options->flags & CV_SPAWN_WAIT) {
            WaitForSingleObject(pi.hProcess, INFINITE);
        }

        if (handleOut) {
            *handleOut = pi.hProcess;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned host process %lu\n", pi.dwProcessId);
        return 0;
    }
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct __containerv_spawn_options spawn_opts = {0};
    HANDLE handle;
    int status;
    char* args_copy = NULL;
    char** argv = NULL;
    
    if (!container || !path) {
        return -1;
    }
    
    // Validate and copy path
    size_t path_len = strlen(path);
    if (path_len == 0 || path_len >= MAX_PATH) {
        VLOG_ERROR("containerv", "containerv_spawn: invalid path length\n");
        return -1;
    }
    
    spawn_opts.path = path;
    if (options) {
        spawn_opts.flags = options->flags;

        // Parse arguments string into argv.
        // Matches Linux semantics where `arguments` is a whitespace-delimited string supporting quotes.
        if (options->arguments && options->arguments[0] != '\0') {
            args_copy = _strdup(options->arguments);
            if (args_copy == NULL) {
                return -1;
            }
        }

        argv = strargv(args_copy, path, NULL);
        if (argv == NULL) {
            free(args_copy);
            return -1;
        }
        spawn_opts.argv = (const char* const*)argv;

        // Environment is a NULL-terminated array of KEY=VALUE strings.
        spawn_opts.envv = options->environment;
    }

    status = __containerv_spawn(container, &spawn_opts, &handle);
    if (status == 0 && pidOut) {
        *pidOut = handle;
    }

    strargv_free(argv);
    free(args_copy);
    return status;
}

int __containerv_kill(struct containerv_container* container, HANDLE handle)
{
    BOOL result;
    
    if (!container || handle == NULL) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_kill(handle=%p)\n", handle);

    // Find the tracked process entry first so we can interpret opaque guest tokens safely.
    struct list_item* i;
    struct containerv_container_process* found = NULL;
    for (i = container->processes.head; i != NULL; i = i->next) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->handle == handle) {
            found = proc;
            break;
        }
    }

    if (container->hcs_system != NULL && found != NULL && found->is_guest) {
        if (__pid1d_kill_reap(container, found->guest_id) != 0) {
            return -1;
        }

        list_remove(&container->processes, &found->list_header);
        free(found->handle);
        free(found);
        return 0;
    }
    
    if (container->hcs_system == NULL && g_pid1_ready) {
        if (pid1_kill_process(handle) != 0) {
            VLOG_ERROR("containerv", "__containerv_kill: pid1_kill_process failed\n");
            return -1;
        }
    } else {
        result = TerminateProcess(handle, 1);
        if (!result) {
            VLOG_ERROR("containerv", "__containerv_kill: TerminateProcess failed: %lu\n", GetLastError());
            return -1;
        }
    }
    
    // Remove from process list
    for (i = container->processes.head; i != NULL; i = i->next) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->handle == handle) {
            list_remove(&container->processes, i);
            if (container->hcs_system != NULL) {
                if (g_hcs.HcsCloseProcess != NULL) {
                    g_hcs.HcsCloseProcess((HCS_PROCESS)proc->handle);
                } else {
                    CloseHandle(proc->handle);
                }
            } else {
                if (g_pid1_ready) {
                    pid1_windows_untrack(proc->handle);
                }
                CloseHandle(proc->handle);
            }
            free(proc);
            break;
        }
    }
    
    return 0;
}

int containerv_kill(struct containerv_container* container, process_handle_t pid)
{
    return __containerv_kill(container, pid);
}

int containerv_wait(struct containerv_container* container, process_handle_t pid, int* exit_code_out)
{
    if (container == NULL || pid == NULL) {
        return -1;
    }

    // If this is a VM container and the pid is one of our opaque guest tokens, wait via pid1d.
    if (container->hcs_system != NULL) {
        struct list_item* it;
        for (it = container->processes.head; it != NULL; it = it->next) {
            struct containerv_container_process* proc = (struct containerv_container_process*)it;
            if (proc->handle == (HANDLE)pid && proc->is_guest) {
                int ec = 0;
                if (__pid1d_wait(container, proc->guest_id, &ec) != 0) {
                    return -1;
                }
                if (exit_code_out != NULL) {
                    *exit_code_out = ec;
                }

                list_remove(&container->processes, it);
                free(proc->handle);
                free(proc);
                return 0;
            }
        }
    }

    // If PID1 is enabled for host processes, let it own the wait+tracking.
    // For VM/HCS processes we still do the direct wait path.
    if (container->hcs_system == NULL && g_pid1_ready) {
        int ec = 0;
        if (pid1_wait_process((HANDLE)pid, &ec) != 0) {
            VLOG_ERROR("containerv", "containerv_wait: pid1_wait_process failed\n");
            return -1;
        }
        if (exit_code_out != NULL) {
            *exit_code_out = ec;
        }
    } else {
        // HCS_PROCESS and normal process handles are both waitable HANDLEs.
        DWORD wr = WaitForSingleObject((HANDLE)pid, INFINITE);
        if (wr != WAIT_OBJECT_0) {
            VLOG_ERROR("containerv", "containerv_wait: WaitForSingleObject failed: %lu\n", GetLastError());
            return -1;
        }

        unsigned long exit_code = 0;
        if (container->hcs_system != NULL) {
            if (__hcs_get_process_exit_code((HCS_PROCESS)pid, &exit_code) != 0) {
                VLOG_ERROR("containerv", "containerv_wait: failed to get process exit code\n");
                return -1;
            }
        } else {
            DWORD ec = 0;
            if (!GetExitCodeProcess((HANDLE)pid, &ec)) {
                VLOG_ERROR("containerv", "containerv_wait: GetExitCodeProcess failed: %lu\n", GetLastError());
                return -1;
            }
            exit_code = (unsigned long)ec;
        }

        if (exit_code_out != NULL) {
            *exit_code_out = (int)exit_code;
        }
    }

    // Remove from process list and close.
    struct list_item* i;
    for (i = container->processes.head; i != NULL; i = i->next) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        if (proc->handle == (HANDLE)pid) {
            list_remove(&container->processes, i);
            if (container->hcs_system != NULL) {
                if (g_hcs.HcsCloseProcess != NULL) {
                    g_hcs.HcsCloseProcess((HCS_PROCESS)pid);
                } else {
                    CloseHandle((HANDLE)pid);
                }
            } else {
                if (g_pid1_ready) {
                    pid1_windows_untrack((HANDLE)pid);
                }
                CloseHandle((HANDLE)pid);
            }
            free(proc);
            break;
        }
    }

    return 0;
}

int containerv_upload(
    struct containerv_container* container,
    const char* const*           hostPaths,
    const char* const*           containerPaths,
    int                          count
)
{
    VLOG_DEBUG("containerv", "containerv_upload(count=%d)\n", count);
    
    if (!container || !hostPaths || !containerPaths || count <= 0) {
        return -1;
    }
    
    // Enhanced file upload using VM-aware mechanisms
    for (int i = 0; i < count; i++) {
        VLOG_DEBUG("containerv", "uploading: %s -> %s\n", hostPaths[i], containerPaths[i]);
        
        if (container->hcs_system) {
            // VM-based container: use HCS to copy files into VM
            // TODO: In a full implementation, this would:
            // 1. Use PowerShell Direct to copy files into running VM
            // 2. Or use HCS file system operations if available
            // 3. Handle VM filesystem paths correctly
            
            // For now, copy to staging area that can be mounted in VM
            char stagingPath[MAX_PATH];
            sprintf_s(stagingPath, sizeof(stagingPath), "%s\\staging\\%s", 
                     container->runtime_dir, containerPaths[i]);
            
            // Create directory structure if needed
            char* lastSlash = strrchr(stagingPath, '\\');
            if (lastSlash) {
                *lastSlash = '\0';
                SHCreateDirectoryExA(NULL, stagingPath, NULL);
                *lastSlash = '\\';
            }
            
            if (!CopyFileA(hostPaths[i], stagingPath, FALSE)) {
                VLOG_ERROR("containerv", "containerv_upload: failed to stage file %s: %lu\n",
                          hostPaths[i], GetLastError());
                return -1;
            }
            
            VLOG_DEBUG("containerv", "staged file to: %s\n", stagingPath);
            
        } else {
            // Host process container: direct file copy
            char destPath[MAX_PATH];
            size_t rootfs_len = strlen(container->rootfs);
            size_t container_path_len = strlen(containerPaths[i]);
            
            if (rootfs_len + 1 + container_path_len + 1 > MAX_PATH) {
                VLOG_ERROR("containerv", "containerv_upload: combined path too long\n");
                return -1;
            }
            
            sprintf_s(destPath, sizeof(destPath), "%s\\%s", container->rootfs, containerPaths[i]);
            
            if (!CopyFileA(hostPaths[i], destPath, FALSE)) {
                VLOG_ERROR("containerv", "containerv_upload: failed to copy %s to %s: %lu\n",
                          hostPaths[i], destPath, GetLastError());
                return -1;
            }
        }
    }
    
    return 0;
}

int containerv_download(
    struct containerv_container* container,
    const char* const*           containerPaths,
    const char* const*           hostPaths,
    int                          count
)
{
    VLOG_DEBUG("containerv", "containerv_download(count=%d)\n", count);
    
    if (!container || !hostPaths || !containerPaths || count <= 0) {
        return -1;
    }
    
    // Enhanced file download using VM-aware mechanisms
    for (int i = 0; i < count; i++) {
        VLOG_DEBUG("containerv", "downloading: %s -> %s\n", containerPaths[i], hostPaths[i]);
        
        if (container->hcs_system) {
            // VM-based container: retrieve files from VM
            // TODO: In a full implementation, this would:
            // 1. Use PowerShell Direct to copy files out of VM
            // 2. Or use HCS file system operations if available
            // 3. Handle VM filesystem paths correctly
            
            // For now, copy from staging area
            char stagingPath[MAX_PATH];
            sprintf_s(stagingPath, sizeof(stagingPath), "%s\\staging\\%s", 
                     container->runtime_dir, containerPaths[i]);
            
            if (!CopyFileA(stagingPath, hostPaths[i], FALSE)) {
                VLOG_ERROR("containerv", "containerv_download: failed to retrieve file %s: %lu\n",
                          stagingPath, GetLastError());
                return -1;
            }
            
            VLOG_DEBUG("containerv", "retrieved file from: %s\n", stagingPath);
            
        } else {
            // Host process container: direct file copy
            char srcPath[MAX_PATH];
            size_t rootfs_len = strlen(container->rootfs);
            size_t container_path_len = strlen(containerPaths[i]);
            
            if (rootfs_len + 1 + container_path_len + 1 > MAX_PATH) {
                VLOG_ERROR("containerv", "containerv_download: combined path too long\n");
                return -1;
            }
            
            sprintf_s(srcPath, sizeof(srcPath), "%s\\%s", container->rootfs, containerPaths[i]);
            
            if (!CopyFileA(srcPath, hostPaths[i], FALSE)) {
                VLOG_ERROR("containerv", "containerv_download: failed to copy %s to %s: %lu\n",
                          srcPath, hostPaths[i], GetLastError());
                return -1;
            }
        }
    }
    
    return 0;
}

void __containerv_destroy(struct containerv_container* container)
{
    if (!container) {
        return;
    }
    
    VLOG_DEBUG("containerv", "__containerv_destroy(id=%s)\n", container->id);
    
    // Terminate all running processes
    struct list_item* i;
    for (i = container->processes.head; i != NULL;) {
        struct containerv_container_process* proc = (struct containerv_container_process*)i;
        i = i->next;
        
        if (proc->handle != NULL) {
            TerminateProcess(proc->handle, 0);
            CloseHandle(proc->handle);
        }
    }
    
    // Clean up job object for resource limits
    if (container->job_object) {
        __windows_cleanup_job_object(container->job_object);
        container->job_object = NULL;
    }
    
    // Clean up volumes and mounts
    __windows_cleanup_volumes(container);
    
    // Clean up network configuration
    __windows_cleanup_network(container, NULL);  // We don't have options here, but that's OK
    
    // Shut down and delete the HyperV VM using HCS
    if (container->hcs_system) {
        __hcs_destroy_vm(container);
    }
    
    // Clean up rootfs (optional - may want to preserve for reuse)
    // Note: In production, you might want to make this configurable
    if (container->rootfs) {
        VLOG_DEBUG("containerv", "cleaning up rootfs at %s\n", container->rootfs);
        __windows_cleanup_rootfs(container->rootfs, NULL);
    }
    
    // Remove runtime directory (recursively if needed)
    if (container->runtime_dir) {
        // RemoveDirectoryA only works on empty directories
        // For a complete implementation, we should recursively delete contents first
        // or use SHFileOperationA for recursive deletion
        if (!RemoveDirectoryA(container->runtime_dir)) {
            DWORD error = GetLastError();
            if (error != ERROR_DIR_NOT_EMPTY) {
                VLOG_WARNING("containerv", "__containerv_destroy: failed to remove runtime dir: %lu\n", error);
            } else {
                VLOG_DEBUG("containerv", "__containerv_destroy: runtime dir not empty, may need manual cleanup\n");
            }
        }
    }
}

int containerv_destroy(struct containerv_container* container)
{
    if (!container) {
        return -1;
    }
    
    __containerv_destroy(container);
    __container_delete(container);
    
    return 0;
}

int containerv_join(const char* containerId)
{
    VLOG_DEBUG("containerv", "containerv_join(id=%s)\n", containerId);
    
    // TODO: Implement joining an existing container
    // This would attach to an existing HyperV VM
    
    return -1; // Not implemented
}

const char* containerv_id(struct containerv_container* container)
{
    if (!container) {
        return NULL;
    }
    return container->id;
}
