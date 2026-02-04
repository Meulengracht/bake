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

static int __pid1d_rpc(struct containerv_container* container, const char* reqLine, char* resp, size_t respCap);
static int __pid1d_ensure(struct containerv_container* container);

// Acquire the shared PID1 service for a container instance.
static int __pid1_acquire_for_container(struct containerv_container* container)
{
    LONG after;

    if (container == NULL) {
        return -1;
    }

    after = InterlockedIncrement(&g_pid1_container_refcount);
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

// Release the shared PID1 service when a container is done.
static void __pid1_release_for_container(void)
{
    LONG after;

    after = InterlockedDecrement(&g_pid1_container_refcount);
    if (after == 0) {
        if (InterlockedExchange(&g_pid1_ready, 0) == 1) {
            (void)pid1_cleanup();
        }
    }
}

// Send a JSON request to pid1d and read the response.
static int __pid1d_rpc_json(struct containerv_container* container, json_t* req, char* resp, size_t respCap)
{
    char* reqUtf8;
    int   rc;

    if (container == NULL || req == NULL || resp == NULL || respCap == 0) {
        return -1;
    }

    reqUtf8 = NULL;
    rc = -1;
    if (containerv_json_dumps_compact(req, &reqUtf8) != 0) {
        return -1;
    }

    rc = __pid1d_rpc(container, reqUtf8, resp, respCap);
    free(reqUtf8);
    return rc;
}

// Write all bytes to a pid1d pipe handle.
static int __pid1d_write_all(HANDLE handle, const char* data, size_t len)
{
    size_t writtenTotal;
    DWORD  written;
    BOOL   ok;

    if (handle == NULL || data == NULL) {
        return -1;
    }

    writtenTotal = 0;
    written = 0;
    ok = FALSE;
    while (writtenTotal < len) {
        written = 0;
        ok = WriteFile(handle, data + writtenTotal, (DWORD)(len - writtenTotal), &written, NULL);
        if (!ok) {
            return -1;
        }
        writtenTotal += (size_t)written;
    }
    return 0;
}

// Read a single line from pid1d into the output buffer.
static int __pid1d_read_line(HANDLE handle, char* out, size_t outCap)
{
    size_t length;
    char   ch;
    DWORD  read;
    BOOL   ok;

    if (handle == NULL || out == NULL || outCap == 0) {
        return -1;
    }

    length = 0;
    ch = 0;
    read = 0;
    ok = FALSE;
    for (;;) {
        ch = 0;
        read = 0;
        ok = ReadFile(handle, &ch, 1, &read, NULL);
        if (!ok || read == 0) {
            return -1;
        }

        if (ch == '\n') {
            break;
        }
        if (ch == '\r') {
            continue;
        }

        if (length + 1 >= outCap) {
            return -1;
        }
        out[length++] = ch;
    }

    out[length] = '\0';
    return 0;
}

// Return non-zero if the pid1d response indicates success.
static int __pid1d_resp_ok(const char* resp)
{
    if (resp == NULL) {
        return 0;
    }
    return strstr(resp, "\"ok\":true") != NULL;
}

// Parse a uint64 field from a pid1d JSON response.
static int __pid1d_parse_uint64_field(const char* resp, const char* key, uint64_t* outValue)
{
    char        needle[64];
    const char* p;
    char*       endp;
    unsigned long long value;

    if (resp == NULL || key == NULL || outValue == NULL) {
        return -1;
    }

    snprintf(needle, sizeof(needle), "\"%s\":", key);
    p = strstr(resp, needle);
    if (p == NULL) {
        return -1;
    }
    p += strlen(needle);

    while (*p == ' ' || *p == '\t') {
        p++;
    }

    endp = NULL;
    value = strtoull(p, &endp, 10);
    if (endp == p) {
        return -1;
    }
    *outValue = (uint64_t)value;
    return 0;
}

// Parse an int field from a pid1d JSON response.
static int __pid1d_parse_int_field(const char* resp, const char* key, int* outValue)
{
    uint64_t value;

    value = 0;
    if (__pid1d_parse_uint64_field(resp, key, &value) != 0) {
        return -1;
    }
    *outValue = (int)value;
    return 0;
}

// Parse a boolean field from a pid1d JSON response.
static int __pid1d_parse_bool_field(const char* resp, const char* key, int* outValue)
{
    char        needle[64];
    const char* p;

    if (resp == NULL || key == NULL || outValue == NULL) {
        return -1;
    }

    snprintf(needle, sizeof(needle), "\"%s\":", key);
    p = strstr(resp, needle);
    if (p == NULL) {
        return -1;
    }
    p += strlen(needle);
    while (*p == ' ' || *p == '\t') {
        p++;
    }
    if (strncmp(p, "true", 4) == 0) {
        *outValue = 1;
        return 0;
    }
    if (strncmp(p, "false", 5) == 0) {
        *outValue = 0;
        return 0;
    }
    return -1;
}

// Parse a string field from a pid1d JSON response and return a copy.
static char* __pid1d_parse_string_field_alloc(const char* resp, const char* key)
{
    char        needle[80];
    const char* p;
    const char* end;
    size_t      length;
    char*       outValue;

    if (resp == NULL || key == NULL) {
        return NULL;
    }

    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    p = strstr(resp, needle);
    if (p == NULL) {
        return NULL;
    }
    p += strlen(needle);

    // We expect base64 here (no escapes), so copy until next quote.
    end = strchr(p, '"');
    if (end == NULL || end < p) {
        return NULL;
    }
    length = (size_t)(end - p);
    outValue = calloc(length + 1, 1);
    if (outValue == NULL) {
        return NULL;
    }
    memcpy(outValue, p, length);
    outValue[length] = '\0';
    return outValue;
}

// Encode a buffer to base64 and return a newly allocated string.
static char* __base64_encode_alloc(const unsigned char* data, size_t len)
{
    DWORD outLen;
    char* outValue;

    if (data == NULL && len != 0) {
        return NULL;
    }

    outLen = 0;
    if (!CryptBinaryToStringA((const BYTE*)data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL, &outLen)) {
        return NULL;
    }

    outValue = calloc(outLen + 1, 1);
    if (outValue == NULL) {
        return NULL;
    }

    if (!CryptBinaryToStringA((const BYTE*)data, (DWORD)len, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, outValue, &outLen)) {
        free(outValue);
        return NULL;
    }
    outValue[outLen] = 0;
    return outValue;
}

// Decode a base64 string into a newly allocated buffer.
static unsigned char* __base64_decode_alloc(const char* b64, size_t* outLen)
{
    DWORD          binLen;
    unsigned char* outValue;

    if (outLen != NULL) {
        *outLen = 0;
    }
    if (b64 == NULL) {
        return NULL;
    }

    binLen = 0;
    if (!CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, NULL, &binLen, NULL, NULL)) {
        return NULL;
    }

    outValue = malloc((size_t)binLen);
    if (outValue == NULL) {
        return NULL;
    }

    if (!CryptStringToBinaryA(b64, 0, CRYPT_STRING_BASE64, (BYTE*)outValue, &binLen, NULL, NULL)) {
        free(outValue);
        return NULL;
    }
    if (outLen != NULL) {
        *outLen = (size_t)binLen;
    }
    return outValue;
}

// Ensure the parent directory exists for a host path.
static int __ensure_parent_dir_hostpath(const char* hostPath)
{
    char  tempPath[MAX_PATH];
    char* lastSlash;
    char* lastFSlash;
    char* sep;

    if (hostPath == NULL) {
        return -1;
    }

    memset(tempPath, 0, sizeof(tempPath));
    strncpy_s(tempPath, sizeof(tempPath), hostPath, _TRUNCATE);

    lastSlash = strrchr(tempPath, '\\');
    lastFSlash = strrchr(tempPath, '/');
    sep = lastSlash;
    if (lastFSlash != NULL && (sep == NULL || lastFSlash > sep)) {
        sep = lastFSlash;
    }
    if (sep == NULL) {
        return 0;
    }
    *sep = 0;
    if (tempPath[0] == 0) {
        return 0;
    }
    (void)SHCreateDirectoryExA(NULL, tempPath, NULL);
    return 0;
}

// Send a raw request line to pid1d and read a response line.
static int __pid1d_rpc(struct containerv_container* container, const char* reqLine, char* resp, size_t respCap)
{
    size_t reqLen;

    if (container == NULL || reqLine == NULL || resp == NULL) {
        return -1;
    }
    if (container->pid1d_stdin == NULL || container->pid1d_stdout == NULL) {
        return -1;
    }

    reqLen = strlen(reqLine);
    if (__pid1d_write_all(container->pid1d_stdin, reqLine, reqLen) != 0) {
        return -1;
    }
    if (__pid1d_write_all(container->pid1d_stdin, "\n", 1) != 0) {
        return -1;
    }
    if (__pid1d_read_line(container->pid1d_stdout, resp, respCap) != 0) {
        return -1;
    }
    return 0;
}

// Write file contents to pid1d using base64 payloads.
static int __pid1d_file_write_b64(
    struct containerv_container* container,
    const char*                  path,
    const unsigned char*         data,
    size_t                       dataLen,
    int                          appendMode,
    int                          makeDirs)
{
    char*  b64;
    json_t* req;
    char   resp[8192];

    if (container == NULL || path == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    b64 = __base64_encode_alloc(data, dataLen);
    if (b64 == NULL) {
        return -1;
    }

    req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "file_write_b64") != 0 ||
        containerv_json_object_set_string(req, "path", path) != 0 ||
        containerv_json_object_set_string(req, "data", b64) != 0 ||
        containerv_json_object_set_bool(req, "append", appendMode) != 0 ||
        containerv_json_object_set_bool(req, "mkdirs", makeDirs) != 0) {
        json_decref(req);
        free(b64);
        return -1;
    }
    free(b64);

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

// Read file contents from pid1d as base64 payloads.
static int __pid1d_file_read_b64(
    struct containerv_container* container,
    const char*                  path,
    uint64_t                     offset,
    uint64_t                     maxBytes,
    char**                       b64Out,
    uint64_t*                    bytesOut,
    int*                         eofOut)
{
    json_t*  req;
    char     resp[8192];
    uint64_t bytes;
    int      eof;
    char*    b64;

    if (container == NULL || path == NULL || b64Out == NULL || bytesOut == NULL || eofOut == NULL) {
        return -1;
    }
    *b64Out = NULL;
    *bytesOut = 0;
    *eofOut = 0;

    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "file_read_b64") != 0 ||
        containerv_json_object_set_string(req, "path", path) != 0 ||
        containerv_json_object_set_uint64(req, "offset", offset) != 0 ||
        containerv_json_object_set_uint64(req, "max_bytes", maxBytes) != 0) {
        json_decref(req);
        return -1;
    }

    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0) {
        json_decref(req);
        return -1;
    }
    json_decref(req);

    if (!__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d file_read_b64 failed: %s\n", resp);
        return -1;
    }

    bytes = 0;
    eof = 0;
    if (__pid1d_parse_uint64_field(resp, "bytes", &bytes) != 0) {
        return -1;
    }
    if (__pid1d_parse_bool_field(resp, "eof", &eof) != 0) {
        eof = 0;
    }

    b64 = __pid1d_parse_string_field_alloc(resp, "data");
    if (b64 == NULL) {
        // For zero-byte reads, pid1d should still return "data":"".
        b64 = _strdup("");
        if (b64 == NULL) {
            return -1;
        }
    }
    
    *b64Out = b64;
    *bytesOut = bytes;
    *eofOut = eof;
    return 0;
}

// Close the pid1d session and release stdio/process handles.
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

// Ensure pid1d is running in the guest VM and ready to accept requests.
static int __pid1d_ensure(struct containerv_container* container)
{
    const char*                     pid1dPath;
    const char*                     argvLocal[2];
    const char* const*              argv;
    struct __containerv_spawn_options opts;
    HCS_PROCESS                     proc;
    HCS_PROCESS_INFORMATION         info;
    int                             status;
    char                            respBuf[8192];
    json_t*                         request;
    int                             pingRc;

    if (container == NULL || container->hcs_system == NULL) {
        return -1;
    }
    if (container->pid1d_started) {
        return 0;
    }

    pid1dPath = container->guest_is_windows ? "C:\\pid1d.exe" : "/usr/bin/pid1d";
    argvLocal[0] = pid1dPath;
    argvLocal[1] = NULL;
    argv = argvLocal;

    memset(&opts, 0, sizeof(opts));
    opts.path = pid1dPath;
    opts.argv = argv;
    opts.flags = 0;
    opts.create_stdio_pipes = 1;

    proc = NULL;
    memset(&info, 0, sizeof(info));

    status = __hcs_create_process(container, &opts, &proc, &info);
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

    request = json_object();
    if (request == NULL || containerv_json_object_set_string(request, "op", "ping") != 0) {
        json_decref(request);
        __pid1d_close_session(container);
        return -1;
    }

    pingRc = __pid1d_rpc_json(container, request, respBuf, sizeof(respBuf));
    json_decref(request);
    if (pingRc != 0 || !__pid1d_resp_ok(respBuf)) {
        VLOG_ERROR("containerv", "pid1d: ping failed: %s\n", respBuf);
        __pid1d_close_session(container);
        return -1;
    }

    VLOG_DEBUG("containerv", "pid1d: session established\n");
    return 0;
}

// Spawn a process in the guest through pid1d.
static int __pid1d_spawn(struct containerv_container* container, struct __containerv_spawn_options* options, uint64_t* idOut)
{
    json_t* req;
    json_t* args;
    json_t* env;
    char    resp[8192];
    int     rc;
    int     i;

    if (container == NULL || options == NULL || options->path == NULL || idOut == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "spawn") != 0 ||
        containerv_json_object_set_string(req, "command", options->path) != 0 ||
        containerv_json_object_set_bool(req, "wait", (options->flags & CV_SPAWN_WAIT) != 0) != 0) {
        json_decref(req);
        return -1;
    }

    if (options->argv != NULL) {
        args = json_array();
        if (args == NULL || json_object_set_new(req, "args", args) != 0) {
            json_decref(args);
            json_decref(req);
            return -1;
        }
        for (i = 0; options->argv[i] != NULL; ++i) {
            if (containerv_json_array_append_string(args, options->argv[i]) != 0) {
                json_decref(req);
                return -1;
            }
        }
    }

    if (options->envv != NULL) {
        env = json_array();
        if (env == NULL || json_object_set_new(req, "env", env) != 0) {
            json_decref(env);
            json_decref(req);
            return -1;
        }
        for (i = 0; options->envv[i] != NULL; ++i) {
            if (containerv_json_array_append_string(env, options->envv[i]) != 0) {
                json_decref(req);
                return -1;
            }
        }
    }

    rc = __pid1d_rpc_json(container, req, resp, sizeof(resp));
    json_decref(req);
    if (rc != 0 || !__pid1d_resp_ok(resp)) {
        VLOG_ERROR("containerv", "pid1d: spawn failed: %s\n", resp);
        return -1;
    }

    if (__pid1d_parse_uint64_field(resp, "id", idOut) != 0) {
        VLOG_ERROR("containerv", "pid1d: spawn missing id: %s\n", resp);
        return -1;
    }

    return 0;
}

// Wait for a pid1d process to exit and return its exit code.
static int __pid1d_wait(struct containerv_container* container, uint64_t id, int* exitCodeOut)
{
    json_t* req;
    char    resp[8192];
    int     exitCode;

    if (container == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "wait") != 0 ||
        containerv_json_object_set_uint64(req, "id", id) != 0) {
        json_decref(req);
        return -1;
    }
    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        json_decref(req);
        VLOG_ERROR("containerv", "pid1d: wait failed: %s\n", resp);
        return -1;
    }
    json_decref(req);

    exitCode = 0;
    (void)__pid1d_parse_int_field(resp, "exit_code", &exitCode);
    if (exitCodeOut != NULL) {
        *exitCodeOut = exitCode;
    }
    return 0;
}

// Terminate a pid1d process and request reaping.
static int __pid1d_kill_reap(struct containerv_container* container, uint64_t id)
{
    json_t* req;
    char    resp[8192];

    if (container == NULL) {
        return -1;
    }
    if (__pid1d_ensure(container) != 0) {
        return -1;
    }

    req = json_object();
    if (req == NULL ||
        containerv_json_object_set_string(req, "op", "kill") != 0 ||
        containerv_json_object_set_uint64(req, "id", id) != 0 ||
        containerv_json_object_set_bool(req, "reap", 1) != 0) {
        json_decref(req);
        return -1;
    }
    if (__pid1d_rpc_json(container, req, resp, sizeof(resp)) != 0 || !__pid1d_resp_ok(resp)) {
        json_decref(req);
        VLOG_ERROR("containerv", "pid1d: kill failed: %s\n", resp);
        return -1;
    }
    json_decref(req);
    return 0;
}

int __windows_exec_in_vm_via_pid1d(
    struct containerv_container*      container,
    struct __containerv_spawn_options* options,
    int*                              exitCodeOut)
{
    uint64_t id;

    if (container == NULL || options == NULL || options->path == NULL) {
        return -1;
    }

    id = 0;
    if (__pid1d_spawn(container, options, &id) != 0) {
        return -1;
    }

    if ((options->flags & CV_SPAWN_WAIT) != 0) {
        return __pid1d_wait(container, id, exitCodeOut);
    }

    if (exitCodeOut != NULL) {
        *exitCodeOut = 0;
    }
    return 0;
}

// Build a Windows environment block from an envv array.
static char* __build_environment_block(const char* const* envv)
{
    size_t total;
    char*  block;
    size_t at;
    size_t n;
    int    i;

    if (envv == NULL) {
        return NULL;
    }

    total = 1; // final terminator
    for (i = 0; envv[i] != NULL; ++i) {
        total += strlen(envv[i]) + 1;
    }

    block = calloc(total, 1);
    if (block == NULL) {
        return NULL;
    }

    at = 0;
    for (i = 0; envv[i] != NULL; ++i) {
        n = strlen(envv[i]);
        memcpy(block + at, envv[i], n);
        at += n;
        block[at++] = '\0';
    }
    block[at++] = '\0';
    return block;
}

// Convert a UTF-8 string to a newly allocated wide string.
static wchar_t* __utf8_to_wide_alloc(const char* src)
{
    int      needed;
    wchar_t* out;

    if (src == NULL) {
        return NULL;
    }
    needed = MultiByteToWideChar(CP_UTF8, 0, src, -1, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }
    out = calloc((size_t)needed, sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }
    if (MultiByteToWideChar(CP_UTF8, 0, src, -1, out, needed) == 0) {
        free(out);
        return NULL;
    }
    return out;
}

// Build a wide environment block from an envv array.
static wchar_t* __build_environment_block_wide(const char* const* envv)
{
    size_t   totalWchars;
    int      n;
    wchar_t* block;
    size_t   at;
    int      i;

    if (envv == NULL) {
        return NULL;
    }

    totalWchars = 1; // final terminator
    for (i = 0; envv[i] != NULL; ++i) {
        n = MultiByteToWideChar(CP_UTF8, 0, envv[i], -1, NULL, 0);
        if (n <= 0) {
            return NULL;
        }
        // n already includes null terminator for that string.
        totalWchars += (size_t)n;
    }

    block = calloc(totalWchars, sizeof(wchar_t));
    if (block == NULL) {
        return NULL;
    }

    at = 0;
    for (i = 0; envv[i] != NULL; ++i) {
        n = MultiByteToWideChar(CP_UTF8, 0, envv[i], -1, block + at, (int)(totalWchars - at));
        if (n <= 0) {
            free(block);
            return NULL;
        }
        at += (size_t)n;
    }
    block[at++] = L'\0';
    return block;
}

// Create a unique runtime directory under the temp path.
static char* __container_create_runtime_dir(void)
{
    char   tempPath[MAX_PATH];
    char*  directory;
    DWORD  result;
    size_t remaining;
    
    // Get temp path
    result = GetTempPathA(MAX_PATH, tempPath);
    if (result == 0 || result > MAX_PATH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to get temp path\n");
        return NULL;
    }
    
    // Create a unique subdirectory for the container
    // strcat_s second parameter is the total buffer size, not remaining space
    remaining = MAX_PATH - strlen(tempPath);
    if (remaining < MIN_REMAINING_PATH_LENGTH) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: temp path too long\n");
        return NULL;
    }
    strcat_s(tempPath, MAX_PATH, "containerv-XXXXXX");
    
    // Use _mktemp_s to create unique name
    if (_mktemp_s(tempPath, strlen(tempPath) + 1) != 0) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create unique name\n");
        return NULL;
    }
    
    // Create the directory
    if (!CreateDirectoryA(tempPath, NULL)) {
        VLOG_ERROR("containerv", "__container_create_runtime_dir: failed to create directory: %s\n", tempPath);
        return NULL;
    }
    
    directory = _strdup(tempPath);
    return directory;
}

void containerv_generate_id(char* buffer, size_t length)
{
    const char charSet[] = "0123456789abcdef";
    HCRYPTPROV cryptoProvider;
    BYTE       randomBytes[__CONTAINER_ID_LENGTH / 2];  // Each byte generates 2 hex chars
    ULONGLONG  tickCount;
    DWORD      processId;
    ULONGLONG  combinedValue;
    size_t     i;
    
    if (length < __CONTAINER_ID_LENGTH + 1) {
        return;
    }
    
    // Use Windows Crypto API for random generation
    if (CryptAcquireContext(&cryptoProvider, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        if (CryptGenRandom(cryptoProvider, sizeof(randomBytes), randomBytes)) {
            for (i = 0; i < sizeof(randomBytes); i++) {
                buffer[i * 2] = charSet[(randomBytes[i] >> 4) & 0x0F];
                buffer[i * 2 + 1] = charSet[randomBytes[i] & 0x0F];
            }
            buffer[__CONTAINER_ID_LENGTH] = '\0';
            CryptReleaseContext(cryptoProvider, 0);
            return;
        }
        CryptReleaseContext(cryptoProvider, 0);
    }
    
    // If crypto API fails, use GetTickCount64 + process ID as fallback
    // This is not cryptographically secure but better than rand()
    tickCount = GetTickCount64();
    processId = GetCurrentProcessId();
    combinedValue = (tickCount << 32) | processId;
    
    for (i = 0; i < __CONTAINER_ID_LENGTH; i++) {
        buffer[i] = charSet[(combinedValue >> (i * 4)) & 0x0F];
    }
    buffer[__CONTAINER_ID_LENGTH] = '\0';
}

// Allocate and initialize a new container object.
static struct containerv_container* __container_new(void)
{
    struct containerv_container* container;
    char                         stagingPath[MAX_PATH];
    DWORD                        errorCode;
    size_t                       idLen;

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
    sprintf_s(stagingPath, sizeof(stagingPath), "%s\\staging", container->runtime_dir);
    if (!CreateDirectoryA(stagingPath, NULL)) {
        errorCode = GetLastError();
        if (errorCode != ERROR_ALREADY_EXISTS) {
            VLOG_WARNING("containerv", "failed to create staging directory: %lu\n", errorCode);
        }
    }
    
    // Generate container ID
    containerv_generate_id(container->id, sizeof(container->id));

    // Convert container ID to wide string for HCS
    idLen = strlen(container->id);
    container->vm_id = calloc(idLen + 1, sizeof(wchar_t));
    if (container->vm_id == NULL) {
        free(container->runtime_dir);
        free(container);
        return NULL;
    }
    
    if (MultiByteToWideChar(CP_UTF8, 0, container->id, -1, container->vm_id, (int)idLen + 1) == 0) {
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

    container->hns_endpoint_id = NULL;
    container->pid1d_stdin = NULL;
    container->pid1d_stdout = NULL;
    container->pid1d_stderr = NULL;
    container->pid1d_started = 0;
    container->pid1_acquired = 0;

    return container;
}

// Return non-zero if HCS should run in LCOW mode.
static int __is_hcs_lcow_mode(const struct containerv_options* options)
{
    if (options == NULL) {
        return 0;
    }
    return (options->windows_container_type == WINDOWS_CONTAINER_TYPE_LINUX);
}

// Ensure LCOW rootfs mountpoint directories exist under the host path.
static void __ensure_lcow_rootfs_mountpoint_dirs(const char* rootfsHostPath)
{
    char        chefDir[MAX_PATH];
    char        stagingDir[MAX_PATH];
    const char* s;
    char        rel[MAX_PATH];
    size_t      j;
    char        full[MAX_PATH];

    if (rootfsHostPath == NULL || rootfsHostPath[0] == '\0') {
        return;
    }

    snprintf(chefDir, sizeof(chefDir), "%s\\chef", rootfsHostPath);
    snprintf(stagingDir, sizeof(stagingDir), "%s\\chef\\staging", rootfsHostPath);

    // Best-effort: these are only mountpoints for bind mounts.
    CreateDirectoryA(chefDir, NULL);
    CreateDirectoryA(stagingDir, NULL);

    // Standard Linux mountpoints (stored as Linux-style absolute paths).
    for (const char* const* mp = containerv_standard_linux_mountpoints(); mp != NULL && *mp != NULL; ++mp) {
        s = *mp;
        if (s == NULL || s[0] == '\0') {
            continue;
        }

        // Convert "/dev/pts" -> "dev\\pts" and join under rootfs_host_path.
        j = 0;
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

        snprintf(full, sizeof(full), "%s\\%s", rootfsHostPath, rel);
        CreateDirectoryA(full, NULL);
    }
}

struct __lcow_bind_dir_ctx {
    const struct containerv_oci_bundle_paths* paths;
};

// Prepare LCOW bind mount target directories for OCI bundle paths.
static int __lcow_prepare_bind_dir_cb(
    const char* host_path,
    const char* container_path,
    int         readonly,
    void*       user_context)
{
    struct __lcow_bind_dir_ctx* ctx;

    ctx = (struct __lcow_bind_dir_ctx*)user_context;
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

// Escape single quotes for safe inclusion in single-quoted shell strings.
static char* __escape_sh_single_quotes_alloc(const char* src)
{
    size_t len;
    size_t extra;
    char*  out;
    size_t j;
    size_t i;

    if (src == NULL) {
        return _strdup("");
    }

    len = strlen(src);
    extra = 0;
    for (i = 0; i < len; i++) {
        if (src[i] == '\'') {
            extra += 3; // ' -> '\'' (4 chars instead of 1)
        }
    }

    out = calloc(len + extra + 1, 1);
    if (out == NULL) {
        return NULL;
    }

    j = 0;
    for (i = 0; i < len; i++) {
        if (src[i] == '\'') {
            out[j++] = '\'';
            out[j++] = '\\';
            out[j++] = '\'';
            out[j++] = '\'';
        } else {
            out[j++] = src[i];
        }
    }
    out[j] = '\0';
    return out;
}

// Write a layerchain.json file with the provided layer paths.
static int __write_layerchain_json(const char* layerFolderPath, char* const* paths, int count)
{
    char   chainPath[MAX_PATH];
    int    rc;
    json_t* root;
    int    i;
    json_t* entry;

    if (layerFolderPath == NULL || layerFolderPath[0] == '\0' || paths == NULL || count <= 0) {
        return -1;
    }

    rc = snprintf(chainPath, sizeof(chainPath), "%s\\layerchain.json", layerFolderPath);
    if (rc < 0 || (size_t)rc >= sizeof(chainPath)) {
        return -1;
    }

    root = json_array();
    if (root == NULL) {
        return -1;
    }

    for (i = 0; i < count; i++) {
        if (paths[i] == NULL || paths[i][0] == '\0') {
            continue;
        }
        entry = json_string(paths[i]);
        if (entry == NULL) {
            json_decref(root);
            return -1;
        }
        json_array_append_new(root, entry);
    }

    if (json_dump_file(root, chainPath, JSON_INDENT(2)) != 0) {
        json_decref(root);
        return -1;
    }

    json_decref(root);
    return 0;
}

// Read layerchain.json and return resolved parent layer paths.
static int __read_layerchain_json(const char* layerFolderPath, char*** pathsOut, int* countOut)
{
    char        chainPath[MAX_PATH];
    int         rc;
    json_error_t jerr;
    json_t*     root;
    size_t      n;
    char**      out;
    int         outCount;
    size_t      i;
    json_t*     item;
    const char* valueStr;
    int         j;
    int         changed;
    char        resolved[MAX_PATH];
    const char* base;
    int         rr;

    if (pathsOut == NULL || countOut == NULL || layerFolderPath == NULL || layerFolderPath[0] == '\0') {
        return -1;
    }
    *pathsOut = NULL;
    *countOut = 0;

    rc = snprintf(chainPath, sizeof(chainPath), "%s\\layerchain.json", layerFolderPath);
    if (rc < 0 || (size_t)rc >= sizeof(chainPath)) {
        return -1;
    }

    memset(&jerr, 0, sizeof(jerr));
    root = json_load_file(chainPath, 0, &jerr);
    if (root == NULL) {
        VLOG_ERROR("containerv", "failed to parse layerchain.json at %s: %s (line %d)\n", chainPath, jerr.text, jerr.line);
        return -1;
    }

    if (!json_is_array(root)) {
        json_decref(root);
        VLOG_ERROR("containerv", "layerchain.json is not an array: %s\n", chainPath);
        return -1;
    }

    n = json_array_size(root);
    if (n == 0) {
        json_decref(root);
        VLOG_ERROR("containerv", "layerchain.json is empty: %s\n", chainPath);
        return -1;
    }

    out = calloc(n, sizeof(char*));
    if (out == NULL) {
        json_decref(root);
        return -1;
    }

    outCount = 0;
    for (i = 0; i < n; i++) {
        item = json_array_get(root, i);
        if (!json_is_string(item)) {
            continue;
        }
        valueStr = json_string_value(item);
        if (valueStr == NULL || valueStr[0] == '\0') {
            continue;
        }
        out[outCount++] = _strdup(valueStr);
        if (out[outCount - 1] == NULL) {
            for (j = 0; j < outCount - 1; j++) {
                free(out[j]);
            }
            free(out);
            json_decref(root);
            return -1;
        }
    }

    json_decref(root);

    if (outCount == 0) {
        free(out);
        return -1;
    }

    changed = 0;
    for (i = 0; i < (size_t)outCount; i++) {
        valueStr = out[i];
        if (valueStr == NULL || valueStr[0] == '\0') {
            continue;
        }
        if (PathFileExistsA(valueStr)) {
            continue;
        }

        resolved[0] = '\0';

        if (PathIsRelativeA(valueStr)) {
            rr = snprintf(resolved, sizeof(resolved), "%s\\%s", layerFolderPath, valueStr);
            if (rr > 0 && (size_t)rr < sizeof(resolved) && PathFileExistsA(resolved)) {
                // resolved relative path
            } else {
                resolved[0] = '\0';
            }
        }

        if (resolved[0] == '\0') {
            base = strrchr(valueStr, '\\');
            if (base == NULL) {
                base = strrchr(valueStr, '/');
            }
            if (base != NULL) {
                base++;
            } else {
                base = valueStr;
            }
            rr = snprintf(resolved, sizeof(resolved), "%s\\parents\\%s", layerFolderPath, base);
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
                valueStr,
                layerFolderPath);
            for (j = 0; j < outCount; j++) {
                free(out[j]);
            }
            free(out);
            return -1;
        }

        free(out[i]);
        out[i] = _strdup(resolved);
        if (out[i] == NULL) {
            for (j = 0; j < outCount; j++) {
                free(out[j]);
            }
            free(out);
            return -1;
        }
        changed = 1;
    }

    if (changed) {
        if (__write_layerchain_json(layerFolderPath, out, outCount) != 0) {
            VLOG_WARNING("containerv", "failed to rewrite layerchain.json with resolved paths under %s\n", layerFolderPath);
        }
    }

    *pathsOut = out;
    *countOut = outCount;
    return 0;
}

// Return non-zero if layerchain.json exists under the layer folder.
static int __windowsfilter_layerchain_exists(const char* layerFolderPath)
{
    char chainPath[MAX_PATH];
    int  rc;

    if (layerFolderPath == NULL || layerFolderPath[0] == '\0') {
        return 0;
    }

    rc = snprintf(chainPath, sizeof(chainPath), "%s\\layerchain.json", layerFolderPath);
    if (rc < 0 || (size_t)rc >= sizeof(chainPath)) {
        return 0;
    }

    return PathFileExistsA(chainPath) ? 1 : 0;
}

// Free a vector of strings with a known count.
static void __free_strv(char** values, int count)
{
    int i;

    if (values == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(values[i]);
    }
    free(values);
}

// Derive a UtilityVM path from options or parent layers.
static char* __derive_utilityvm_path(
    const struct containerv_options* options,
    const char* const*               parentLayers,
    int                              parentLayerCount)
{
    const char* base;
    char        candidate[MAX_PATH];
    int         rc;
    DWORD       attrs;

    // Caller requested Hyper-V isolation.
    if (options != NULL && options->windows_container.utilityvm_path != NULL && options->windows_container.utilityvm_path[0] != '\0') {
        return _strdup(options->windows_container.utilityvm_path);
    }

    // Best-effort: base layer path + "\\UtilityVM".
    // layerchain.json usually ends at the base OS layer.
    base = NULL;
    if (parentLayers != NULL && parentLayerCount > 0) {
        base = parentLayers[parentLayerCount - 1];
    }
    if (base == NULL || base[0] == '\0') {
        return NULL;
    }

    rc = snprintf(candidate, sizeof(candidate), "%s\\UtilityVM", base);
    if (rc < 0 || (size_t)rc >= sizeof(candidate)) {
        return NULL;
    }

    attrs = GetFileAttributesA(candidate);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        return NULL;
    }

    return _strdup(candidate);
}

// Validate a UtilityVM path and provide a reason on failure.
static int __validate_utilityvm_path(const char* path, char* reason, size_t reasonCap)
{
    DWORD attrs;
    char  vhdx[MAX_PATH];
    char  filesDir[MAX_PATH];
    int   rv;
    int   rf;
    int   vhdxExists;
    int   filesExists;

    if (reason && reasonCap > 0) {
        reason[0] = '\0';
    }

    if (path == NULL || path[0] == '\0') {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM path is empty");
        }
        return 0;
    }

    attrs = GetFileAttributesA(path);
    if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM path is not a directory");
        }
        return 0;
    }

    rv = snprintf(vhdx, sizeof(vhdx), "%s\\UtilityVM.vhdx", path);
    rf = snprintf(filesDir, sizeof(filesDir), "%s\\Files", path);
    if (rv < 0 || (size_t)rv >= sizeof(vhdx) || rf < 0 || (size_t)rf >= sizeof(filesDir)) {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM path is too long");
        }
        return 0;
    }

    vhdxExists = PathFileExistsA(vhdx) ? 1 : 0;
    filesExists = (GetFileAttributesA(filesDir) != INVALID_FILE_ATTRIBUTES) ? 1 : 0;
    if (!vhdxExists && !filesExists) {
        if (reason && reasonCap > 0) {
            snprintf(reason, reasonCap, "UtilityVM missing UtilityVM.vhdx and Files directory");
        }
        return 0;
    }

    return 1;
}

// Format a UtilityVM candidate path from a base path.
static char* __format_utilityvm_candidate(const char* base)
{
    char candidate[MAX_PATH];
    int  rc;

    if (base == NULL || base[0] == '\0') {
        return NULL;
    }

    rc = snprintf(candidate, sizeof(candidate), "%s\\UtilityVM", base);
    if (rc < 0 || (size_t)rc >= sizeof(candidate)) {
        return NULL;
    }

    return _strdup(candidate);
}

// Release resources associated with a container instance.
static void __container_delete(struct containerv_container* container)
{
    struct list_item* i;
    int               pid1Acquired;
    
    if (!container) {
        return;
    }

    pid1Acquired = container->pid1_acquired;
    
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
    if (pid1Acquired) {
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
    HRESULT                      hr;
    BOOL                         rootfsExists;
    char**                       parentLayers;
    int                          parentLayerCount;
    int                          hv;
    char*                        utilityVm;
    const char*                  baseLayer;
    char*                        candidate;
    char                         reasonBuf[256];
    const char*                  imagePath;
    struct containerv_oci_bundle_paths bundlePaths;
    const char*                  lcowRootfsHost;
    
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
    rootfsExists = PathFileExistsA(rootFs);
    if (!rootfsExists) {
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
    if (__is_hcs_lcow_mode(options) && rootfsExists) {
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
            parentLayers = NULL;
            parentLayerCount = 0;
            if (__read_layerchain_json(rootFs, &parentLayers, &parentLayerCount) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to parse layerchain.json under %s\n", rootFs);
                __container_delete(container);
                return -1;
            }

            hv = (options && options->windows_container.isolation == WINDOWS_CONTAINER_ISOLATION_HYPERV);
            utilityVm = NULL;
            if (hv) {
                utilityVm = __derive_utilityvm_path(options, (const char* const*)parentLayers, parentLayerCount);
                if (utilityVm == NULL) {
                    baseLayer = (parentLayers != NULL && parentLayerCount > 0) ? parentLayers[parentLayerCount - 1] : NULL;
                    candidate = __format_utilityvm_candidate(baseLayer);
                    if (candidate != NULL) {
                        VLOG_ERROR(
                            "containerv",
                            "containerv_create: Hyper-V isolation requires UtilityVM path (set via containerv_options_set_windows_container_utilityvm_path or ensure base layer has UtilityVM at %s)\n",
                            candidate);
                    } else {
                        VLOG_ERROR("containerv", "containerv_create: Hyper-V isolation requires UtilityVM path (set via containerv_options_set_windows_container_utilityvm_path or ensure base layer has UtilityVM)\n");
                    }
                    free(candidate);
                    __free_strv(parentLayers, parentLayerCount);
                    __container_delete(container);
                    errno = ENOENT;
                    return -1;
                }

                if (!__validate_utilityvm_path(utilityVm, reasonBuf, sizeof(reasonBuf))) {
                    VLOG_ERROR(
                        "containerv",
                        "containerv_create: UtilityVM validation failed for %s (%s)\n",
                        utilityVm,
                        reasonBuf[0] ? reasonBuf : "invalid UtilityVM path");
                    free(utilityVm);
                    __free_strv(parentLayers, parentLayerCount);
                    __container_delete(container);
                    errno = ENOENT;
                    return -1;
                }
            }

            // Create WCOW container compute system.
            if (__hcs_create_container_system(container, options, rootFs, (const char* const*)parentLayers, parentLayerCount, utilityVm, 0) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to create HCS container compute system\n");
                free(utilityVm);
                __free_strv(parentLayers, parentLayerCount);
                __container_delete(container);
                return -1;
            }

            free(utilityVm);
            __free_strv(parentLayers, parentLayerCount);
        } else {
            // LCOW container compute system (bring-up scaffolding): uses ContainerType=Linux and HvRuntime.
            // NOTE: OCI spec + rootfs plumbing is added in a subsequent step.
            imagePath = (options && options->windows_lcow.image_path) ? options->windows_lcow.image_path : NULL;
            if (imagePath == NULL || imagePath[0] == '\0') {
                VLOG_ERROR("containerv", "containerv_create: LCOW requires HvRuntime.ImagePath (set via containerv_options_set_windows_lcow_hvruntime)\n");
                __container_delete(container);
                errno = ENOENT;
                return -1;
            }

            // If a host rootfs was provided, prepare a per-container OCI bundle under runtime_dir.
            // This gives us a stable rootfs directory for mapping into the UVM.
            memset(&bundlePaths, 0, sizeof(bundlePaths));

            lcowRootfsHost = NULL;
            if (rootfsExists) {
                if (containerv_oci_bundle_get_paths(container->runtime_dir, &bundlePaths) != 0) {
                    VLOG_ERROR("containerv", "containerv_create: failed to compute OCI bundle paths\n");
                    __container_delete(container);
                    return -1;
                }
                if (containerv_oci_bundle_prepare_rootfs(&bundlePaths, rootFs) != 0) {
                    VLOG_ERROR("containerv", "containerv_create: failed to prepare OCI bundle rootfs\n");
                    containerv_oci_bundle_paths_delete(&bundlePaths);
                    __container_delete(container);
                    return -1;
                }
                (void)containerv_oci_bundle_prepare_rootfs_mountpoints(&bundlePaths);
                (void)containerv_oci_bundle_prepare_rootfs_standard_files(
                    &bundlePaths,
                    container->hostname,
                    (options && options->network.dns) ? options->network.dns : NULL);
                (void)containerv_oci_bundle_prepare_rootfs_dir(&bundlePaths, "/chef", 0755);
                (void)containerv_oci_bundle_prepare_rootfs_dir(&bundlePaths, "/chef/staging", 0755);

                if (options != NULL && options->layers != NULL) {
                    struct __lcow_bind_dir_ctx bctx = {.paths = &bundlePaths};
                    (void)containerv_layers_iterate(
                        options->layers,
                        CONTAINERV_LAYER_HOST_DIRECTORY,
                        __lcow_prepare_bind_dir_cb,
                        &bctx);
                }
                lcowRootfsHost = bundlePaths.rootfs_dir;
            }

            if (__hcs_create_container_system(container, options, lcowRootfsHost, NULL, 0, imagePath, 1) != 0) {
                VLOG_ERROR("containerv", "containerv_create: failed to create LCOW HCS container compute system\n");
                containerv_oci_bundle_paths_delete(&bundlePaths);
                __container_delete(container);
                return -1;
            }

            containerv_oci_bundle_paths_delete(&bundlePaths);
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
        int wantJob = 1;
        if (options && (options->capabilities & CV_CAP_CGROUPS)) {
            if (options->limits.memory_max || options->limits.cpu_percent || options->limits.process_count) {
                wantJob = 1;
            }
        }
        if (container->policy) {
            enum containerv_security_level level = containerv_policy_get_security_level(container->policy);
            if (level >= CV_SECURITY_RESTRICTED) {
                wantJob = 1;
            }
        }

        if (wantJob) {
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
    STARTUPINFOA                    startupInfo;
    PROCESS_INFORMATION             processInfo;
    HCS_PROCESS_INFORMATION         hcsProcessInfo;
    BOOL                            result;
    struct containerv_container_process* proc;
    char                            cmdline[4096];
    size_t                          cmdlineLen;
    int                             i;
    size_t                          argLen;
    HCS_PROCESS                     hcsProcess;
    
    if (!container || !options || !options->path) {
        return -1;
    }
    
    VLOG_DEBUG("containerv", "__containerv_spawn(path=%s)\n", options->path);
    
    ZeroMemory(&startupInfo, sizeof(startupInfo));
    startupInfo.cb = sizeof(startupInfo);
    ZeroMemory(&processInfo, sizeof(processInfo));
    
    // Build command line from path and arguments
    cmdlineLen = strlen(options->path);
    if (cmdlineLen >= sizeof(cmdline)) {
        VLOG_ERROR("containerv", "__containerv_spawn: path too long\n");
        return -1;
    }
    strcpy_s(cmdline, sizeof(cmdline), options->path);
    
    if (options->argv) {
        for (i = 1; options->argv[i] != NULL; i++) {
            argLen = strlen(options->argv[i]);
            // Check if adding " " + argument would overflow (current + space + arg + null)
            if (cmdlineLen + 1 + argLen + 1 > sizeof(cmdline)) {
                VLOG_ERROR("containerv", "__containerv_spawn: command line too long\n");
                return -1;
            }
            strcat_s(cmdline, sizeof(cmdline), " ");
            strcat_s(cmdline, sizeof(cmdline), options->argv[i]);
            cmdlineLen += 1 + argLen;
        }
    }
    
    // Check if we have an HCS compute system to run the process in
    if (container->hcs_system != NULL) {
        // HCS container compute system (WCOW/LCOW): spawn via HCS process APIs.
        hcsProcess = NULL;
        memset(&hcsProcessInfo, 0, sizeof(hcsProcessInfo));
        if (__hcs_create_process(container, options, &hcsProcess, &hcsProcessInfo) != 0) {
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
            if (g_hcs.HcsCloseProcess != NULL && hcsProcess != NULL) {
                g_hcs.HcsCloseProcess(hcsProcess);
            }
            return -1;
        }

        proc->handle = (HANDLE)hcsProcess;
        proc->pid = hcsProcessInfo.ProcessId;
        proc->is_guest = 0;
        proc->guest_id = 0;
        list_add(&container->processes, &proc->list_header);

        if (handleOut) {
            *handleOut = proc->handle;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned process via HCS (pid=%lu)\n", (unsigned long)hcsProcessInfo.ProcessId);
        return 0;
    } else {
        // Fallback to host process creation (for testing/debugging)
        VLOG_WARNING("containerv", "__containerv_spawn: no HCS compute system, creating host process as fallback\n");

        int didSecure;
        didSecure = 0;
        if (container->policy) {
            enum containerv_security_level level = containerv_policy_get_security_level(container->policy);
            int         useAppContainer;
            const char* integrityLevel;
            const char* const* capabilitySids;
            int         capabilitySidCount;

                useAppContainer = 0;
                integrityLevel = NULL;
                capabilitySids = NULL;
                capabilitySidCount = 0;
                if (containerv_policy_get_windows_isolation(
                    container->policy,
                    &useAppContainer,
                    &integrityLevel,
                    &capabilitySids,
                    &capabilitySidCount) == 0) {
                if (level != CV_SECURITY_DEFAULT || useAppContainer || integrityLevel || (capabilitySids && capabilitySidCount > 0)) {
                    wchar_t* cmdlineWide = __utf8_to_wide_alloc(cmdline);
                    wchar_t* cwdWide = __utf8_to_wide_alloc(container->rootfs);
                    wchar_t* envWide = __build_environment_block_wide(options->envv);

                    if (cmdlineWide != NULL) {
                        if (windows_create_secure_process_ex(
                                container->policy,
                                cmdlineWide,
                                cwdWide,
                                envWide,
                                &processInfo) == 0) {
                            didSecure = 1;
                            // Resume and close thread handle (CreateProcessAsUserW used CREATE_SUSPENDED)
                            ResumeThread(processInfo.hThread);
                            CloseHandle(processInfo.hThread);
                            processInfo.hThread = NULL;
                        }
                    }

                    free(cmdlineWide);
                    free(cwdWide);
                    free(envWide);
                }
            }
        }

        if (!didSecure) {
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

                if (pid1_spawn_process(&popts, &processInfo.hProcess) != 0) {
                    VLOG_ERROR("containerv", "__containerv_spawn: pid1_spawn_process failed\n");
                    return -1;
                }

                // We don't have a Windows PID value here (pid1 returns a HANDLE).
                processInfo.dwProcessId = 0;
                processInfo.hThread = NULL;
            } else {
                char* envBlock = __build_environment_block(options->envv);

                result = CreateProcessA(
                    NULL,           // Application name
                    cmdline,        // Command line
                    NULL,           // Process security attributes
                    NULL,           // Thread security attributes
                    FALSE,          // Inherit handles
                    0,              // Creation flags
                    envBlock,       // Environment (NULL = inherit)
                    container->rootfs, // Current directory
                    &startupInfo,   // Startup info
                    &processInfo    // Process information
                );

                free(envBlock);

                if (!result) {
                    VLOG_ERROR("containerv", "__containerv_spawn: CreateProcess failed: %lu\n", GetLastError());
                    return -1;
                }

                // Close thread handle, we don't need it
                CloseHandle(processInfo.hThread);
            }
        }

        if (processInfo.hThread != NULL) {
            CloseHandle(processInfo.hThread);
        }

        // Add process to container's process list
        proc = calloc(1, sizeof(struct containerv_container_process));
        if (proc) {
            proc->handle = processInfo.hProcess;
            proc->pid = processInfo.dwProcessId;
            list_add(&container->processes, &proc->list_header);
            
            // Apply job object resource limits if configured
            if (container->job_object) {
                if (AssignProcessToJobObject(container->job_object, processInfo.hProcess)) {
                    VLOG_DEBUG("containerv", "__containerv_spawn: assigned process %lu to job object\n", processInfo.dwProcessId);
                } else {
                    VLOG_WARNING("containerv", "__containerv_spawn: failed to assign process %lu to job: %lu\n", 
                               processInfo.dwProcessId, GetLastError());
                }
            }
        } else {
            CloseHandle(processInfo.hProcess);
            return -1;
        }
        
        if (options->flags & CV_SPAWN_WAIT) {
            WaitForSingleObject(processInfo.hProcess, INFINITE);
        }

        if (handleOut) {
            *handleOut = processInfo.hProcess;
        }

        VLOG_DEBUG("containerv", "__containerv_spawn: spawned host process %lu\n", processInfo.dwProcessId);
        return 0;
    }
}

int containerv_spawn(
    struct containerv_container*     container,
    const char*                      path,
    struct containerv_spawn_options* options,
    process_handle_t*                pidOut)
{
    struct __containerv_spawn_options spawnOpts = {0};
    HANDLE                           handle;
    int                              status;
    char*                            argsCopy = NULL;
    char**                           argvList = NULL;
    
    if (!container || !path) {
        return -1;
    }
    
    // Validate and copy path
    size_t pathLen = strlen(path);
    if (pathLen == 0 || pathLen >= MAX_PATH) {
        VLOG_ERROR("containerv", "containerv_spawn: invalid path length\n");
        return -1;
    }
    
    spawnOpts.path = path;
    if (options) {
        spawnOpts.flags = options->flags;

        // Parse arguments string into argv.
        // Matches Linux semantics where `arguments` is a whitespace-delimited string supporting quotes.
        if (options->arguments && options->arguments[0] != '\0') {
            argsCopy = _strdup(options->arguments);
            if (argsCopy == NULL) {
                return -1;
            }
        }

        argvList = strargv(argsCopy, path, NULL);
        if (argvList == NULL) {
            free(argsCopy);
            return -1;
        }
        spawnOpts.argv = (const char* const*)argvList;

        // Environment is a NULL-terminated array of KEY=VALUE strings.
        spawnOpts.envv = options->environment;
    }

    status = __containerv_spawn(container, &spawnOpts, &handle);
    if (status == 0 && pidOut) {
        *pidOut = handle;
    }

    strargv_free(argvList);
    free(argsCopy);
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
                int exitCode = 0;
                if (__pid1d_wait(container, proc->guest_id, &exitCode) != 0) {
                    return -1;
                }
                if (exit_code_out != NULL) {
                    *exit_code_out = exitCode;
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
        int exitCode = 0;
        if (pid1_wait_process((HANDLE)pid, &exitCode) != 0) {
            VLOG_ERROR("containerv", "containerv_wait: pid1_wait_process failed\n");
            return -1;
        }
        if (exit_code_out != NULL) {
            *exit_code_out = exitCode;
        }
    } else {
        // HCS_PROCESS and normal process handles are both waitable HANDLEs.
        DWORD waitResult = WaitForSingleObject((HANDLE)pid, INFINITE);
        if (waitResult != WAIT_OBJECT_0) {
            VLOG_ERROR("containerv", "containerv_wait: WaitForSingleObject failed: %lu\n", GetLastError());
            return -1;
        }

        unsigned long exitCode = 0;
        if (container->hcs_system != NULL) {
            if (__hcs_get_process_exit_code((HCS_PROCESS)pid, &exitCode) != 0) {
                VLOG_ERROR("containerv", "containerv_wait: failed to get process exit code\n");
                return -1;
            }
        } else {
            DWORD processExitCode = 0;
            if (!GetExitCodeProcess((HANDLE)pid, &processExitCode)) {
                VLOG_ERROR("containerv", "containerv_wait: GetExitCodeProcess failed: %lu\n", GetLastError());
                return -1;
            }
            exitCode = (unsigned long)processExitCode;
        }

        if (exit_code_out != NULL) {
            *exit_code_out = (int)exitCode;
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
            char stageHost[MAX_PATH];
            char stageGuest[MAX_PATH];
            char tmpName[64];
            snprintf(tmpName, sizeof(tmpName), "upload-%d.tmp", i);
            snprintf(stageHost, sizeof(stageHost), "%s\\staging\\%s", container->runtime_dir, tmpName);
            if (container->guest_is_windows) {
                snprintf(stageGuest, sizeof(stageGuest), "C:\\chef\\staging\\%s", tmpName);
            } else {
                snprintf(stageGuest, sizeof(stageGuest), "/chef/staging/%s", tmpName);
            }

            if (!CopyFileA(hostPaths[i], stageHost, FALSE)) {
                VLOG_ERROR("containerv", "containerv_upload: failed to stage %s: %lu\n", hostPaths[i], GetLastError());
                return -1;
            }

            // Best-effort: copy staged file to destination inside the container.
            char cmd[2048];
            struct containerv_spawn_options spawnOpts = {0};
            process_handle_t               processHandle;
            spawnOpts.flags = CV_SPAWN_WAIT;

            if (container->guest_is_windows) {
                snprintf(cmd, sizeof(cmd), "/c copy /Y \"%s\" \"%s\"", stageGuest, containerPaths[i]);
                spawnOpts.arguments = cmd;
                if (containerv_spawn(container, "cmd.exe", &spawnOpts, &processHandle) != 0) {
                    return -1;
                }
            } else {
                char* srcEsc = __escape_sh_single_quotes_alloc(stageGuest);
                char* dstEsc = __escape_sh_single_quotes_alloc(containerPaths[i]);
                if (srcEsc == NULL || dstEsc == NULL) {
                    free(srcEsc);
                    free(dstEsc);
                    return -1;
                }
                snprintf(cmd, sizeof(cmd), "-c \"cp -f -- '%s' '%s'\"", srcEsc, dstEsc);
                free(srcEsc);
                free(dstEsc);

                spawnOpts.arguments = cmd;
                if (containerv_spawn(container, "/bin/sh", &spawnOpts, &processHandle) != 0) {
                    return -1;
                }
            }

            int exitCode = 0;
            if (containerv_wait(container, processHandle, &exitCode) != 0 || exitCode != 0) {
                VLOG_ERROR("containerv", "containerv_upload: in-container copy failed (exit=%d)\n", exitCode);
                return -1;
            }
        } else {
            // Host process container: direct file copy
            char   destPath[MAX_PATH];
            size_t rootfsLen = strlen(container->rootfs);
            size_t containerPathLen = strlen(containerPaths[i]);
            
            if (rootfsLen + 1 + containerPathLen + 1 > MAX_PATH) {
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

            char stageHost[MAX_PATH];
            char stageGuest[MAX_PATH];
            char tmpName[64];
            snprintf(tmpName, sizeof(tmpName), "download-%d.tmp", i);
            snprintf(stageHost, sizeof(stageHost), "%s\\staging\\%s", container->runtime_dir, tmpName);
            if (container->guest_is_windows) {
                snprintf(stageGuest, sizeof(stageGuest), "C:\\chef\\staging\\%s", tmpName);
            } else {
                snprintf(stageGuest, sizeof(stageGuest), "/chef/staging/%s", tmpName);
            }

            char cmd[2048];
            struct containerv_spawn_options spawnOpts = {0};
            process_handle_t               processHandle;
            spawnOpts.flags = CV_SPAWN_WAIT;

            if (container->guest_is_windows) {
                snprintf(cmd, sizeof(cmd), "/c copy /Y \"%s\" \"%s\"", containerPaths[i], stageGuest);
                spawnOpts.arguments = cmd;
                if (containerv_spawn(container, "cmd.exe", &spawnOpts, &processHandle) != 0) {
                    return -1;
                }
            } else {
                char* srcEsc = __escape_sh_single_quotes_alloc(containerPaths[i]);
                char* dstEsc = __escape_sh_single_quotes_alloc(stageGuest);
                if (srcEsc == NULL || dstEsc == NULL) {
                    free(srcEsc);
                    free(dstEsc);
                    return -1;
                }
                snprintf(cmd, sizeof(cmd), "-c \"cp -f -- '%s' '%s'\"", srcEsc, dstEsc);
                free(srcEsc);
                free(dstEsc);

                spawnOpts.arguments = cmd;
                if (containerv_spawn(container, "/bin/sh", &spawnOpts, &processHandle) != 0) {
                    return -1;
                }
            }

            int exitCode = 0;
            if (containerv_wait(container, processHandle, &exitCode) != 0 || exitCode != 0) {
                VLOG_ERROR("containerv", "containerv_download: in-container stage copy failed (exit=%d)\n", exitCode);
                return -1;
            }

            if (!CopyFileA(stageHost, hostPaths[i], FALSE)) {
                VLOG_ERROR("containerv", "containerv_download: failed to copy staged file to host: %lu\n", GetLastError());
                return -1;
            }
        } else {
            // Host process container: direct file copy
            char   srcPath[MAX_PATH];
            size_t rootfsLen = strlen(container->rootfs);
            size_t containerPathLen = strlen(containerPaths[i]);
            
            if (rootfsLen + 1 + containerPathLen + 1 > MAX_PATH) {
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
