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
#include <inttypes.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

#include <jansson.h>

#include "json-util.h"

#include <pid1_windows.h>

#include "private.h"

#include "standard-mounts.h"
#include "oci-bundle.h"

#define MIN_REMAINING_PATH_LENGTH 20  // Minimum space needed for "containerv-XXXXXX" + null

// PID1 is currently implemented as a process-global service. We reference count
// active containers so we can init/cleanup once.
static volatile LONG g_pid1_container_refcount = 0;
static volatile LONG g_pid1_ready = 0;

static int __pid1d_rpc(struct containerv_container* container, const char* req_line, char* resp, size_t resp_cap);
static int __pid1d_ensure(struct containerv_container* container);

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

static int __pid1d_rpc_json(struct containerv_container* container, json_t* req, char* resp, size_t resp_cap)
{
    if (container == NULL || req == NULL || resp == NULL || resp_cap == 0) {
        return -1;
    }

    char* req_utf8 = NULL;
    if (containerv_json_dumps_compact(req, &req_utf8) != 0) {
        return -1;
    }

    int rc = __pid1d_rpc(container, req_utf8, resp, resp_cap);
    free(req_utf8);
    return rc;
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

static int __pid1d_parse_bool_field(const char* resp, const char* key, int* out)
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
    if (strncmp(p, "true", 4) == 0) {
        *out = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *out = 0;
        return 0;
    }
    return -1;
}

static char* __pid1d_parse_string_field_alloc(const char* resp, const char* key)
{
    if (resp == NULL || key == NULL) {
        return NULL;
    }

    char needle[80];
    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    const char* p = strstr(resp, needle);
    if (p == NULL) {
        return NULL;
    }
    p += strlen(needle);

    // We expect base64 here (no escapes), so copy until next quote.
    const char* end = strchr(p, '"');
    if (end == NULL || end < p) {
        return NULL;
    }
    size_t n = (size_t)(end - p);
    char* out = calloc(n + 1, 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return out;
}

static char* __base64_encode_alloc(const unsigned char* data, size_t len)
{
    if (data == NULL && len != 0) {
        return NULL;
    }

    DWORD out_len = 0;
    if (!CryptBinaryToStringA((const BYTE*)data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &out_len)) {
        return NULL;
    }

    char* out = calloc(out_len + 1, 1);
    if (out == NULL) {
        return NULL;
    }

    if (!CryptBinaryToStringA((const BYTE*)data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out, &out_len)) {
        free(out);
        return NULL;
    }
    out[out_len] = 0;
    return out;
}

static unsigned char* __base64_decode_alloc(const char* b64, size_t* out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (b64 == NULL) {
        return NULL;
    }

    DWORD bin_len = 0;
    if (!CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, NULL, &bin_len, NULL, NULL)) {
        return NULL;
    }

    unsigned char* out = malloc((size_t)bin_len);
    if (out == NULL) {
        return NULL;
    }

    if (!CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, (BYTE*)out, &bin_len, NULL, NULL)) {
        free(out);
        return NULL;
    }
    if (out_len != NULL) {
        *out_len = (size_t)bin_len;
    }
    return out;
}

static int __ensure_parent_dir_hostpath(const char* host_path)
{
    if (host_path == NULL) {
        return -1;
    }
    char tmp[MAX_PATH];
    strncpy_s(tmp, sizeof(tmp), host_path, _TRUNCATE);

    char* last_slash = strrchr(tmp, '\\');
    char* last_fslash = strrchr(tmp, '/');
    char* sep = last_slash;
    if (last_fslash != NULL && (sep == NULL || last_fslash > sep)) {
        sep = last_fslash;
    }
    if (sep == NULL) {
        return 0;
    }
    *sep = 0;
    if (tmp[0] == 0) {
        return 0;
    }
    (void)SHCreateDirectoryExA(NULL, tmp, NULL);
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

static int __pid1d_file_write_b64(
    struct containerv_container* container,
    const char*                  path,
    const unsigned char*         data,
    size_t                       len,
    int                          append,
    int                          mkdirs)
{
    if (container == NULL || path == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    char* b64 = __base64_encode_alloc(data, len);
    if (b64 == NULL) {
        return -1;
    }

    json_t* req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "file_write_b64") != 0 ||
        containerv_json_object_set_string(req, "path", path) != 0 ||
        containerv_json_object_set_string(req, "data", b64) != 0 ||
        containerv_json_object_set_bool(req, "append", append) != 0 ||
        containerv_json_object_set_bool(req, "mkdirs", mkdirs) != 0) {
        json_decref(req);
        free(b64);
        return -1;
    }
    free(b64);

    char resp[8192];
    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0) {
        json_decref(req);
        return -1;
    }
    json_decref(req);

    if (!__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d file_write_b64 failed: %s\n", resp);
        return -1;
    }
    return 0;
}

static int __pid1d_file_read_b64(
    struct containerv_container* container,
    const char*                  path,
    uint64_t                     offset,
    uint64_t                     max_bytes,
    char**                       b64_out,
    uint64_t*                    bytes_out,
    int*                         eof_out)
{
    if (container == NULL || path == NULL || b64_out == NULL || bytes_out == NULL || eof_out == NULL) {
        return -1;
    }
    *b64_out = NULL;
    *bytes_out = 0;
    *eof_out = 0;

    if (__pid1d_ensure(container) != 0) {
        return -1;
    }


    json_t* req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "file_read_b64") != 0 ||
        containerv_json_object_set_string(req, "path", path) != 0 ||
        containerv_json_object_set_uint64(req, "offset", offset) != 0 ||
        containerv_json_object_set_uint64(req, "max_bytes", max_bytes) != 0) {
        json_decref(req);
        return -1;
    }

    char resp[8192];
    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0) {
        json_decref(req);
        return -1;
    }
    json_decref(req);

    if (!__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d file_read_b64 failed: %s\n", resp);
        return -1;
    }

    uint64_t bytes = 0;
    int eof = 0;
    if (__pid1d_parse_uint64_field(resp, "bytes", &bytes) != 0) {
        return -1;
    }
    if (__pid1d_parse_bool_field(resp, "eof", &eof) != 0) {
        eof = 0;
    }

    char* b64 = __pid1d_parse_string_field_alloc(resp, "data");
    if (b64 == NULL) {
        // For zero-byte reads, pid1d should still return "data":"".
        b64 = _strdup("");
        if (b64 == NULL) {
            return -1;
        }
    }

    *b64_out = b64;
    *bytes_out = bytes;
    *eof_out = eof;
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
    json_t* req = json_object();
    if (req == NULL || containerv_json_object_set_string(req, "op", "ping") != 0) {
        json_decref(req);
        __pid1d_close_session(container);
        return -1;
    }

    int ping_rc = __pid1d_rpc_json(container, req, resp, sizeof(resp));
    json_decref(req);
    if (ping_rc != 0 || !__pid1d_resp_ok(resp)) {
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

    json_t* req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "spawn") != 0 ||
        containerv_json_object_set_string(req, "command", options->path) != 0 ||
        containerv_json_object_set_bool(req, "wait", (options->flags & CV_SPAWN_WAIT) != 0) != 0) {
        json_decref(req);
        return -1;
    }

    if (options->argv != NULL) {
        json_t* args = json_array();
        if (args == NULL || json_object_set_new(req, "args", args) != 0) {
            json_decref(args);
            json_decref(req);
            return -1;
        }
        for (int i = 0; options->argv[i] != NULL; ++i) {
            if (containerv_json_array_append_string(args, options->argv[i]) != 0) {
                json_decref(req);
                return -1;
            }
        }
    }

    if (options->envv != NULL) {
        json_t* env = json_array();
        if (env == NULL || json_object_set_new(req, "env", env) != 0) {
            json_decref(env);
            json_decref(req);
            return -1;
        }
        for (int i = 0; options->envv[i] != NULL; ++i) {
            if (containerv_json_array_append_string(env, options->envv[i]) != 0) {
                json_decref(req);
                return -1;
            }
        }
    }

    char resp[8192];
    int rc = __pid1d_rpc_json(container, req, resp, sizeof(resp));
    json_decref(req);
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

    json_t* req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "wait") != 0 ||
        containerv_json_object_set_uint64(req, "id", id) != 0) {
        json_decref(req);
        return -1;
    }
    char resp[8192];
    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        json_decref(req);
        VLOG_ERROR("containerv", "pid1d: wait failed: %s\n", resp);
        return -1;
    }
    json_decref(req);

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

    json_t* req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "kill") != 0 ||
        containerv_json_object_set_uint64(req, "id", id) != 0 ||
        containerv_json_object_set_bool(req, "reap", 1) != 0) {
        json_decref(req);
        return -1;
    }
    char resp[8192];
    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        json_decref(req);
        VLOG_ERROR("containerv", "pid1d: kill failed: %s\n", resp);
        return -1;
    }
    json_decref(req);
    return 0;
}

int __windows_exec_in_vm_via_pid1d(
    struct containerv_container* container,
    struct __containerv_spawn_options* options,
    int* exit_code_out)
{
    if (container == NULL || options == NULL || options->path == NULL) {
        return -1;
    }

    uint64_t id = 0;
    if (__pid1d_spawn(container, options, &id) != 0) {
        return -1;
    }

    if ((options->flags & CV_SPAWN_WAIT) != 0) {
        return __pid1d_wait(container, id, exit_code_out);
    }

    if (exit_code_out != NULL) {
        *exit_code_out = 0;
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
    list_init(&container->processes);
    container->policy = NULL;

    container->guest_is_windows = 1;
    container->pid1d_process = NULL;

    container->hns_endpoint_id = NULL;
    container->pid1d_stdin = NULL;
    container->pid1d_stdout = NULL;
    container->pid1d_stderr = NULL;
    container->pid1d_started = 0;
    container->pid1_acquired = 0;

    return container;
}

static int __is_hcs_lcow_mode(const struct containerv_options* options)
{
    if (options == NULL) {
        return 0;
    }
    return (options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX);
}

static void __ensure_lcow_rootfs_mountpoint_dirs(const char* rootfs_host_path)
{
    if (rootfs_host_path == NULL || rootfs_host_path[0] == '\0') {
        return;
    }

    char chef_dir[MAX_PATH];
    char staging_dir[MAX_PATH];

    snprintf(chef_dir, sizeof(chef_dir), "%s\\chef", rootfs_host_path);
    snprintf(staging_dir, sizeof(staging_dir), "%s\\chef\\staging", rootfs_host_path);

    // Best-effort: these are only mountpoints for bind mounts.
    CreateDirectoryA(chef_dir, NULL);
    CreateDirectoryA(staging_dir, NULL);

    // Standard Linux mountpoints (stored as Linux-style absolute paths).
    for (const char* const* mp = containerv_standard_linux_mountpoints(); mp != NULL && *mp != NULL; ++mp) {
        const char* s = *mp;
        if (s == NULL || s[0] == '\0') {
            continue;
        }

        // Convert "/dev/pts" -> "dev\\pts" and join under rootfs_host_path.
        char rel[MAX_PATH];
        size_t j = 0;
        while (*s == '/') {
            s++;
        }
        for (; *s && j + 1 < sizeof(rel); ++s) {
            rel[j++] = (*s == '/') ? '\\' : *s;
        }
        rel[j] = '\0';
        if (rel[0] == '\0') {
            continue;
        }

        char full[MAX_PATH];
        snprintf(full, sizeof(full), "%s\\%s", rootfs_host_path, rel);
        CreateDirectoryA(full, NULL);
    }
}

struct __lcow_bind_dir_ctx {
    const struct containerv_oci_bundle_paths* paths;
};

static int __lcow_prepare_bind_dir_cb(
    const char* host_path,
    const char* container_path,
    int readonly,
    void* user_context)
{
    struct __lcow_bind_dir_ctx* ctx = (struct __lcow_bind_dir_ctx*)user_context;
    (void)host_path;
    (void)readonly;

    if (ctx == NULL || ctx->paths == NULL) {
        return -1;
    }
    if (container_path == NULL || container_path[0] == '\0') {
        return 0;
    }

    if (containerv_oci_bundle_prepare_rootfs_dir(ctx->paths, container_path, 0755) != 0) {
        VLOG_WARNING("containerv", "LCOW: failed to prepare bind mount target %s\n", container_path);
        return -1;
    }

    return 0;
}

static char* __escape_sh_single_quotes_alloc(const char* s)
{
    if (s == NULL) {
        return _strdup("");
    }

    size_t len = strlen(s);
    size_t extra = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            extra += 3; // ' -> '\'' (4 chars instead of 1)
        }
    }

    char* out = calloc(len + extra + 1, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\'') {
            out[j++] = '\'';
            out[j++] = '\\';
            out[j++] = '\'';
            out[j++] = '\'';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static int __write_layerchain_json(const char* layer_folder_path, char* const* paths, int count)
{
    if (layer_folder_path == NULL || layer_folder_path[0] == '\0' || paths == NULL || count <= 0) {
        return -1;
    }

    char chain_path[MAX_PATH];
    int rc = snprintf(chain_path, sizeof(chain_path), "%s\\layerchain.json", layer_folder_path);
    if (rc < 0 || (size_t)rc >= sizeof(chain_path)) {
        return -1;
    }

    json_t* root = json_array();
    if (root == NULL) {
        return -1;
    }

    for (int i = 0; i < count; i++) {
        if (paths[i] == NULL || paths[i][0] == '\0') {
            continue;
        }
        json_t* s = json_string(paths[i]);
        if (s == NULL) {
            json_decref(root);
            return -1;
        }
        json_array_append_new(root, s);
    }

    if (json_dump_file(root, chain_path, JSON_INDENT(2)) != 0) {
        json_decref(root);
        return -1;
    }

    json_decref(root);
    return 0;
}

static int __read_layerchain_json(const char* layer_folder_path, char*** paths_out, int* count_out)
{
    if (paths_out == NULL || count_out == NULL || layer_folder_path == NULL || layer_folder_path[0] == '\0') {
        return -1;
    }
    *paths_out = NULL;
    *count_out = 0;

    char chain_path[MAX_PATH];
    int rc = snprintf(chain_path, sizeof(chain_path), "%s\\layerchain.json", layer_folder_path);
    if (rc < 0 || (size_t)rc >= sizeof(chain_path)) {
        return -1;
    }

    json_error_t jerr;
    json_t* root = json_load_file(chain_path, 0, &jerr);
    if (root == NULL) {
        VLOG_ERROR("containerv", "failed to parse layerchain.json at %s: %s (line %d)\n", chain_path, jerr.text, jerr.line);
        return -1;
    }

    if (!json_is_array(root)) {
        json_decref(root);
        VLOG_ERROR("containerv", "layerchain.json is not an array: %s\n", chain_path);
        return -1;
    }

    size_t n = json_array_size(root);
    if (n == 0) {
        json_decref(root);
        VLOG_ERROR("containerv", "layerchain.json is empty: %s\n", chain_path);
        return -1;
    }

    char** out = calloc(n, sizeof(char*));
    if (out == NULL) {
        json_decref(root);
        return -1;
    }

    int out_count = 0;
    for (size_t i = 0; i < n; i++) {
        json_t* item = json_array_get(root, i);
        if (!json_is_string(item)) {
            continue;
        }
        const char* s = json_string_value(item);
        if (s == NULL || s[0] == '\0') {
            continue;
        }
        out[out_count++] = _strdup(s);
        if (out[out_count - 1] == NULL) {
            for (int j = 0; j < out_count - 1; j++) {
                free(out[j]);
            }
            free(out);
            json_decref(root);
            return -1;
        }
    }

    json_decref(root);

    if (out_count == 0) {
        free(out);
        return -1;
    }

    int changed = 0;
    for (int i = 0; i < out_count; i++) {
        const char* s = out[i];
        if (s == NULL || s[0] == '\0') {
            continue;
        }
        if (PathFileExistsA(s)) {
            continue;
        }

        char resolved[MAX_PATH];
        resolved[0] = '\0';

        if (PathIsRelativeA(s)) {
            int rr = snprintf(resolved, sizeof(resolved), "%s\\%s", layer_folder_path, s);
            if (rr > 0 && (size_t)rr < sizeof(resolved) && PathFileExistsA(resolved)) {
                // resolved relative path
            } else {
                resolved[0] = '\0';
            }
        }

        if (resolved[0] == '\0') {
            const char* base = strrchr(s, '\\');
            if (base == NULL) {
                base = strrchr(s, '/');
            }
            if (base != NULL) {
                base++;
            } else {
                base = s;
            }
            int rr = snprintf(resolved, sizeof(resolved), "%s\\parents\\%s", layer_folder_path, base);
            if (rr > 0 && (size_t)rr < sizeof(resolved) && PathFileExistsA(resolved)) {
                // resolved parents path
            } else {
                resolved[0] = '\0';
            }
        }

        if (resolved[0] == '\0') {
            VLOG_ERROR(
                "containerv",
                "layerchain.json entry does not exist and could not be resolved: %s (base %s)\n",
                s,
                layer_folder_path);
            for (int j = 0; j < out_count; j++) {
                free(out[j]);
            }
            free(out);
            return -1;
        }

        free(out[i]);
        out[i] = _strdup(resolved);
        if (out[i] == NULL) {
            for (int j = 0; j < out_count; j++) {
                free(out[j]);
            }
            free(out);
            return -1;
        }
        changed = 1;
    }

    if (changed) {
        if (__write_layerchain_json(layer_folder_path, out, out_count) != 0) {
            VLOG_WARNING("containerv", "failed to rewrite layerchain.json with resolved paths under %s\n", layer_folder_path);
        }
    }

    *paths_out = out;
    *count_out = out_count;
    return 0;
}

static int __windowsfilter_layerchain_exists(const char* layer_folder_path)
{
    if (layer_folder_path == NULL || layer_folder_path[0] == '\0') {
        return 0;
    }

    char chain_path[MAX_PATH];
    int rc = snprintf(chain_path, sizeof(chain_path), "%s\\layerchain.json", layer_folder_path);
    if (rc < 0 || (size_t)rc >= sizeof(chain_path)) {
        return 0;
    }

    return PathFileExistsA(chain_path) ? 1 : 0;
}

static void __free_strv(char** v, int count)
{
    if (v == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(v[i]);
    }
    free(v);
}

static char* __derive_utilityvm_path(const struct containerv_options* options, const char* const* parent_layers, int parent_layer_count)
{
    // Caller requested Hyper-V isolation.
    if (options != NULL && options->windows_container.utilityvm_path != NULL && options->windows_container.utilityvm_path[0] != '\0') {
        return _strdup(options->windows_container.utilityvm_path);
    }

    // Best-effort: base layer path + "\\UtilityVM".
    // layerchain.json usually ends at the base OS layer.
    const char* base = NULL;
    if (parent_layers != NULL && parent_layer_count > 0) {
        base = parent_layers[parent_layer_count - 1];
    }
    if (base == NULL || base[0] == '\0') {
        return NULL;
    }

    char candidate[MAX_PATH];
    int rc = snprintf(candidate, sizeof(candidate), "%s\\UtilityVM", base);
    if (rc < 0 || (size_t)rc >= sizeof(candidate)) {
        return NULL;
    }

    DWORD attrs = GetFileAttributesA(candidate);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return NULL;
    }

    return _strdup(candidate);
}

static int __validate_utilityvm_path(const char* path, char* reason, size_t reason_cap)
{
    if (reason && reason_cap > 0) {
        reason[0] = '\0';
    }

    if (path == NULL || path[0] == '\0') {
        if (reason && reason_cap > 0) {
            snprintf(reason, reason_cap, "UtilityVM path is empty");
        }
        return 0;
    }

    DWORD attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        if (reason && reason_cap > 0) {
            snprintf(reason, reason_cap, "UtilityVM path is not a directory");
        }
        return 0;
    }

    char vhdx[MAX_PATH];
    char files_dir[MAX_PATH];
    int rv = snprintf(vhdx, sizeof(vhdx), "%s\\UtilityVM.vhdx", path);
    int rf = snprintf(files_dir, sizeof(files_dir), "%s\\Files", path);
    if (rv < 0 || (size_t)rv >= sizeof(vhdx) || rf < 0 || (size_t)rf >= sizeof(files_dir)) {
        if (reason && reason_cap > 0) {
            snprintf(reason, reason_cap, "UtilityVM path is too long");
        }
        return 0;
    }

    const int vhdx_exists = PathFileExistsA(vhdx) ? 1 : 0;
    const int files_exists = (GetFileAttributesA(files_dir) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    if (!vhdx_exists && !files_exists) {
        if (reason && reason_cap > 0) {
            snprintf(reason, reason_cap, "UtilityVM missing UtilityVM.vhdx and Files directory");
        }
        return 0;
    }

    return 1;
}

static char* __format_utilityvm_candidate(const char* base)
{
    if (base == NULL || base[0] == '\0') {
        return NULL;
    }

    char candidate[MAX_PATH];
    int rc = snprintf(candidate, sizeof(candidate), "%s\\UtilityVM", base);
    if (rc < 0 || (size_t)rc >= sizeof(candidate)) {
        return NULL;
    }

    return _strdup(candidate);
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
        __hcs_destroy_compute_system(container);
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

    if (options == NULL) {
        VLOG_ERROR("containerv", "containerv_create: options are required on Windows\n");
        errno = EINVAL;
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
    // For HCS container mode, this is controlled explicitly by options->windows_container_type.
    container->guest_is_windows = __is_hcs_lcow_mode(options) ? 0 : 1;

    // Rootfs preparation for HCS container mode expects BASE_ROOTFS to point at a
    // pre-prepared windowsfilter container folder (WCOW) or an OCI rootfs (LCOW).
    BOOL rootfs_exists = PathFileExistsA(rootFs);
    if (!rootfs_exists) {
        if (__is_hcs_lcow_mode(options)) {
            // For LCOW, BASE_ROOTFS will eventually become an OCI bundle/rootfs reference.
            // For now, we allow it to be absent and rely on UVM bring-up.
            VLOG_WARNING("containerv", "containerv_create: LCOW selected but rootfs path does not exist (%s); continuing (UVM bring-up only)\n", rootFs);
        } else {
            VLOG_ERROR("containerv", "containerv_create: HCS container mode requires an existing windowsfilter container folder at %s\n", rootFs);
            __container_delete(container);
            return -1;
        }
    }
    if (__is_hcs_lcow_mode(options) && rootfs_exists) {
        __ensure_lcow_rootfs_mountpoint_dirs(rootFs);
    }
    
    // Initialize COM for HyperV operations
    hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        VLOG_ERROR("containerv", "containerv_create: failed to initialize COM: 0x%lx\n", hr);
        __container_delete(container);
        return -1;
    }
    
    {
        if (!__is_hcs_lcow_mode(options)) {
            if (!__windowsfilter_layerchain_exists(rootFs)) {
                VLOG_ERROR(
                    "containerv",
                    "containerv_create: HCS container mode requires a windowsfilter folder with layerchain.json at %s (VAFS/overlay materialization is not supported)\n",
                    rootFs);
                __container_delete(container);
                return -1;
            }

            // True Windows containers (WCOW): rootfs must be a windowsfilter container folder.
            // Parse its parent chain from layerchain.json.
            char** parent_layers = NULL;
            int parent_layer_count = 0;
            if (__read_layerchain_json(rootFs, &parent_layers, &parent_layer_count) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to parse layerchain.json under %s\n", rootFs);
                __container_delete(container);
                return -1;
            }

            const int hv = (options && options->windows_container.isolation == WINDOWS_CONTAINER_ISOLATION_HYPERV);
            char* utilityvm = NULL;
            if (hv) {
                utilityvm = __derive_utilityvm_path(options, (const char* const*)parent_layers, parent_layer_count);
                if (utilityvm == NULL) {
                    const char* base = (parent_layers != NULL && parent_layer_count > 0) ? parent_layers[parent_layer_count - 1] : NULL;
                    char* candidate = __format_utilityvm_candidate(base);
                    if (candidate != NULL) {
                        VLOG_ERROR(
                            "containerv",
                            "containerv_create: Hyper-V isolation requires UtilityVM path (set via containerv_options_set_windows_container_utilityvm_path or ensure base layer has UtilityVM at %s)\n",
                            candidate);
                    } else {
                        VLOG_ERROR("containerv", "containerv_create: Hyper-V isolation requires UtilityVM path (set via containerv_options_set_windows_container_utilityvm_path or ensure base layer has UtilityVM)\n");
                    }
                    free(candidate);
                    __free_strv(parent_layers, parent_layer_count);
                    __container_delete(container);
                    errno = ENOENT;
                    return -1;
                }

                char reason[256];
                if (!__validate_utilityvm_path(utilityvm, reason, sizeof(reason))) {
                    VLOG_ERROR(
                        "containerv",
                        "containerv_create: UtilityVM validation failed for %s (%s)\n",
                        utilityvm,
                        reason[0] ? reason : "invalid UtilityVM path");
                    free(utilityvm);
                    __free_strv(parent_layers, parent_layer_count);
                    __container_delete(container);
                    errno = ENOENT;
                    return -1;
                }
            }

            // Create WCOW container compute system.
            if (__hcs_create_container_system(container, options, rootFs, (const char* const*)parent_layers, parent_layer_count, utilityvm, 0) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to create HCS container compute system\n");
                free(utilityvm);
                __free_strv(parent_layers, parent_layer_count);
                __container_delete(container);
                return -1;
            }

            free(utilityvm);
            __free_strv(parent_layers, parent_layer_count);
        } else {
            // LCOW container compute system (bring-up scaffolding): uses ContainerType=Linux and HvRuntime.
            // NOTE: OCI spec + rootfs plumbing is added in a subsequent step.
            const char* image_path = (options && options->windows_lcow.image_path) ? options->windows_lcow.image_path : NULL;
            if (image_path == NULL || image_path[0] == '\0') {
                VLOG_ERROR("containerv", "containerv_create: LCOW requires HvRuntime.ImagePath (set via containerv_options_set_windows_lcow_hvruntime)\n");
                __container_delete(container);
                errno = ENOENT;
                return -1;
            }

            // If a host rootfs was provided, prepare a per-container OCI bundle under runtime_dir.
            // This gives us a stable rootfs directory for mapping into the UVM.
            struct containerv_oci_bundle_paths bundle_paths;
            memset(&bundle_paths, 0, sizeof(bundle_paths));

            const char* lcow_rootfs_host = NULL;
            if (rootfs_exists) {
                if (containerv_oci_bundle_get_paths(container->runtime_dir, &bundle_paths) != 0) {
                    VLOG_ERROR("containerv", "containerv_create: failed to compute OCI bundle paths\n");
                    __container_delete(container);
                    return -1;
                }
                if (containerv_oci_bundle_prepare_rootfs(&bundle_paths, rootFs) != 0) {
                    VLOG_ERROR("containerv", "containerv_create: failed to prepare OCI bundle rootfs\n");
                    containerv_oci_bundle_paths_delete(&bundle_paths);
                    __container_delete(container);
                    return -1;
                }
                (void)containerv_oci_bundle_prepare_rootfs_mountpoints(&bundle_paths);
                (void)containerv_oci_bundle_prepare_rootfs_standard_files(
                    &bundle_paths,
                    container->hostname,
                    (options && options->network.dns) ? options->network.dns : NULL);
                (void)containerv_oci_bundle_prepare_rootfs_dir(&bundle_paths, "/chef", 0755);
                (void)containerv_oci_bundle_prepare_rootfs_dir(&bundle_paths, "/chef/staging", 0755);

                if (options != NULL && options->layers != NULL) {
                    struct __lcow_bind_dir_ctx bctx = {.paths = &bundle_paths};
                    (void)containerv_layers_iterate(
                        options->layers,
                        CONTAINERV_LAYER_HOST_DIRECTORY,
                        __lcow_prepare_bind_dir_cb,
                        &bctx);
                }
                lcow_rootfs_host = bundle_paths.rootfs_dir;
            }

            if (__hcs_create_container_system(container, options, lcow_rootfs_host, NULL, 0, image_path, 1) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to create LCOW HCS container compute system\n");
                containerv_oci_bundle_paths_delete(&bundle_paths);
                __container_delete(container);
                return -1;
            }

            containerv_oci_bundle_paths_delete(&bundle_paths);
        }
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
    
    // Configure host-side networking after compute system is created
    if (options && (options->capabilities & CV_CAP_NETWORK)) {
        if (container->hcs_system != NULL) {
            // True container compute system (WCOW/LCOW): attach HNS endpoint on the host.
            if (__windows_configure_hcs_container_network(container, options) != 0) {
                VLOG_WARNING("containerv", "containerv_create: HCS container network setup encountered issues\n");
            }
        }
    }

    VLOG_DEBUG("containerv", "containerv_create: created HCS container %s\n", container->id);
    
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
    
    // Check if we have an HCS compute system to run the process in
    if (container->hcs_system != NULL) {
        // HCS container compute system (WCOW/LCOW): spawn via HCS process APIs.
        HCS_PROCESS hproc = NULL;
        HCS_PROCESS_INFORMATION info;
        memset(&info, 0, sizeof(info));
        if (__hcs_create_process(container, options, &hproc, &info) != 0) {
            VLOG_ERROR("containerv", "__containerv_spawn: HCS create process failed\n");
            return -1;
        }

        // Configure container networking on first process spawn
        if (!container->network_configured) {
            container->network_configured = 1;
            VLOG_DEBUG("containerv", "__containerv_spawn: network setup deferred (would configure here)\n");
        }

        proc = calloc(1, sizeof(struct containerv_container_process));
        if (proc == NULL) {
            if (g_hcs.HcsCloseProcess != NULL && hproc != NULL) {
                g_hcs.HcsCloseProcess(hproc);
            }
            return -1;
        }

        proc->handle = (HANDLE)hproc;
        proc->pid = info.ProcessId;
        proc->is_guest = 0;
        proc->guest_id = 0;
        list_add(&container->processes, &proc->list_header);

        if (handleOut) {
            *handleOut = proc->handle;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned process via HCS (pid=%lu)\n", (unsigned long)info.ProcessId);
        return 0;
    } else {
        // Fallback to host process creation (for testing/debugging)
        VLOG_WARNING("containerv", "__containerv_spawn: no HCS compute system, creating host process as fallback\n");

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
            // HCS container: use mapped staging folder + in-container copy.
            char stage_host[MAX_PATH];
            char stage_guest[MAX_PATH];
            char tmpname[64];
            snprintf(tmpname, sizeof(tmpname), "upload-%d.tmp", i);
            snprintf(stage_host, sizeof(stage_host), "%s\\staging\\%s", container->runtime_dir, tmpname);
            if (container->guest_is_windows) {
                snprintf(stage_guest, sizeof(stage_guest), "C:\\chef\\staging\\%s", tmpname);
            } else {
                snprintf(stage_guest, sizeof(stage_guest), "/chef/staging/%s", tmpname);
            }

            if (!CopyFileA(hostPaths[i], stage_host, FALSE)) {
                VLOG_ERROR("containerv", "containerv_upload: failed to stage %s: %lu\n", hostPaths[i], GetLastError());
                return -1;
            }

            // Best-effort: copy staged file to destination inside the container.
            char cmd[2048];
            struct containerv_spawn_options sopts = {0};
            sopts.flags = CV_SPAWN_WAIT;
            process_handle_t ph;

            if (container->guest_is_windows) {
                snprintf(cmd, sizeof(cmd), "/c copy /Y \"%s\" \"%s\"", stage_guest, containerPaths[i]);
                sopts.arguments = cmd;
                if (containerv_spawn(container, "cmd.exe", &sopts, &ph) != 0) {
                    return -1;
                }
            } else {
                char* src_esc = __escape_sh_single_quotes_alloc(stage_guest);
                char* dst_esc = __escape_sh_single_quotes_alloc(containerPaths[i]);
                if (src_esc == NULL || dst_esc == NULL) {
                    free(src_esc);
                    free(dst_esc);
                    return -1;
                }
                snprintf(cmd, sizeof(cmd), "-c \"cp -f -- '%s' '%s'\"", src_esc, dst_esc);
                free(src_esc);
                free(dst_esc);

                sopts.arguments = cmd;
                if (containerv_spawn(container, "/bin/sh", &sopts, &ph) != 0) {
                    return -1;
                }
            }

            int ec = 0;
            if (containerv_wait(container, ph, &ec) != 0 || ec != 0) {
                VLOG_ERROR("containerv", "containerv_upload: in-container copy failed (exit=%d)\n", ec);
                return -1;
            }
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
            // HCS container: stage in guest then copy out from host staging directory.
            (void)__ensure_parent_dir_hostpath(hostPaths[i]);

            char stage_host[MAX_PATH];
            char stage_guest[MAX_PATH];
            char tmpname[64];
            snprintf(tmpname, sizeof(tmpname), "download-%d.tmp", i);
            snprintf(stage_host, sizeof(stage_host), "%s\\staging\\%s", container->runtime_dir, tmpname);
            if (container->guest_is_windows) {
                snprintf(stage_guest, sizeof(stage_guest), "C:\\chef\\staging\\%s", tmpname);
            } else {
                snprintf(stage_guest, sizeof(stage_guest), "/chef/staging/%s", tmpname);
            }

            char cmd[2048];
            struct containerv_spawn_options sopts = {0};
            sopts.flags = CV_SPAWN_WAIT;
            process_handle_t ph;

            if (container->guest_is_windows) {
                snprintf(cmd, sizeof(cmd), "/c copy /Y \"%s\" \"%s\"", containerPaths[i], stage_guest);
                sopts.arguments = cmd;
                if (containerv_spawn(container, "cmd.exe", &sopts, &ph) != 0) {
                    return -1;
                }
            } else {
                char* src_esc = __escape_sh_single_quotes_alloc(containerPaths[i]);
                char* dst_esc = __escape_sh_single_quotes_alloc(stage_guest);
                if (src_esc == NULL || dst_esc == NULL) {
                    free(src_esc);
                    free(dst_esc);
                    return -1;
                }
                snprintf(cmd, sizeof(cmd), "-c \"cp -f -- '%s' '%s'\"", src_esc, dst_esc);
                free(src_esc);
                free(dst_esc);

                sopts.arguments = cmd;
                if (containerv_spawn(container, "/bin/sh", &sopts, &ph) != 0) {
                    return -1;
                }
            }

            int ec = 0;
            if (containerv_wait(container, ph, &ec) != 0 || ec != 0) {
                VLOG_ERROR("containerv", "containerv_download: in-container stage copy failed (exit=%d)\n", ec);
                return -1;
            }

            if (!CopyFileA(stage_host, hostPaths[i], FALSE)) {
                VLOG_ERROR("containerv", "containerv_download: failed to copy staged file to host: %lu\n", GetLastError());
                return -1;
            }
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

int containerv_guest_is_windows(struct containerv_container* container)
{
    if (container == NULL) {
        return 0;
    }
    if (container->hcs_system == NULL) {
        return 0;
    }
    return (container->guest_is_windows != 0);
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
    
    // Shut down and delete the HCS compute system
    if (container->hcs_system) {
        __hcs_destroy_compute_system(container);
    }
    
    // Remove runtime directory (recursively if needed)
    if (container->runtime_dir) {
        if (platform_rmdir(container->runtime_dir) != 0) {
            VLOG_WARNING("containerv", "__containerv_destroy: failed to remove runtime dir: %s\n", strerror(errno));
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
    // This would attach to an existing HCS compute system
    
    return -1; // Not implemented
}

const char* containerv_id(struct containerv_container* container)
{
    if (!container) {
        return NULL;
    }
    return container->id;
}
