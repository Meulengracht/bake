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

// GCS RPC Documentation
// build\lcow-hcsshim\internal\gcs\prot\protocol.go
// build\lcow-hcsshim\internal\gcs\bridge.go
// build\lcow-hcsshim\internal\guest\bridge\bridge_v2.go
// build\lcow-hcsshim\internal\gcs\container.go
// build\lcow-hcsshim\internal\gcs\process.go
// build\lcow-hcsshim\internal\guest\runtime\hcsv2\uvm.go

// Debugging GCS RPC
// Keep -loglevel debug on GCS and serial console capture enabled.
// Log every host bridge packet as we do now:
// GCS RPC request type=... payload=...
// GCS RPC response type=... payload=...
// Decode RPC IDs from protocol.go:
// - 0x10100101 create
// - 0x10100501 execute
// - 0x10100601 wait
// - 0x10100a01 modify
// - 0x10100b01 negotiate.
// When GCS returns an HRESULT, inspect the guest span 
// immediately above it; those spans include the real hcsshim 
// file/function stack when available.

#include <winsock2.h>
#include <ws2tcpip.h>

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <vlog.h>

#include "json-util.h"

#include <chef/platform.h>
#include <chef/containerv/layers.h>

#include "oci-spec.h"
#include "oci-bundle.h"

#include "private.h"

#ifndef AF_HYPERV
#define AF_HYPERV 34
#endif

#ifndef HV_PROTOCOL_RAW
#define HV_PROTOCOL_RAW 1
#endif

#define LCOW_GCS_VSOCK_PORT 0x40000000u
#define GCS_HDR_SIZE 16u
#define GCS_MSG_REQUEST 0x10000000u
#define GCS_MSG_RESPONSE 0x20000000u
#define GCS_RPC_CREATE 0x00100101u
#define GCS_RPC_EXECUTE_PROCESS 0x00100501u
#define GCS_RPC_WAIT_FOR_PROCESS 0x00100601u
#define GCS_RPC_MODIFY_SETTINGS 0x00100a01u
#define GCS_RPC_NEGOTIATE_PROTOCOL 0x00100b01u

struct chef_sockaddr_hv {
    ADDRESS_FAMILY Family;
    USHORT         Reserved;
    GUID           VmId;
    GUID           ServiceId;
};

static int __appendf(char** buf, size_t* cap, size_t* len, const char* fmt, ...);

// Some Windows SDKs don't expose these HCS_* HRESULTs unless additional headers are used.
// They are only used for improved error messages, so provide reasonable fallbacks.
#ifndef HCS_E_OPERATION_NOT_SUPPORTED
#define HCS_E_OPERATION_NOT_SUPPORTED HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED)
#endif

#ifndef HCS_E_SERVICE_NOT_AVAILABLE
#define HCS_E_SERVICE_NOT_AVAILABLE HRESULT_FROM_WIN32(ERROR_SERVICE_NOT_ACTIVE)
#endif

#define HCS_OPERATION_TIMEOUT_MS 120000

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

static void __gcs_u32le(unsigned char* p, uint32_t v)
{
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static void __gcs_u64le(unsigned char* p, uint64_t v)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (unsigned char)((v >> (i * 8)) & 0xffu);
    }
}

static uint32_t __gcs_read_u32le(const unsigned char* p)
{
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t __gcs_read_u64le(const unsigned char* p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) {
        v |= ((uint64_t)p[i]) << (i * 8);
    }
    return v;
}

static int __gcs_send_all(SOCKET s, const unsigned char* data, size_t length)
{
    while (length > 0) {
        int chunk = (length > INT_MAX) ? INT_MAX : (int)length;
        int sent = send(s, (const char*)data, chunk, 0);
        if (sent <= 0) {
            errno = EIO;
            return -1;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int __gcs_recv_all(SOCKET s, unsigned char* data, size_t length)
{
    while (length > 0) {
        int chunk = (length > INT_MAX) ? INT_MAX : (int)length;
        int got = recv(s, (char*)data, chunk, 0);
        if (got <= 0) {
            errno = EIO;
            return -1;
        }
        data += got;
        length -= (size_t)got;
    }
    return 0;
}

static void __gcs_vsock_service_guid(uint32_t port, GUID* guid)
{
    memset(guid, 0, sizeof(*guid));
    guid->Data1 = port;
    guid->Data2 = 0xfacb;
    guid->Data3 = 0x11e6;
    guid->Data4[0] = 0xbd;
    guid->Data4[1] = 0x58;
    guid->Data4[2] = 0x64;
    guid->Data4[3] = 0x00;
    guid->Data4[4] = 0x6a;
    guid->Data4[5] = 0x79;
    guid->Data4[6] = 0x86;
    guid->Data4[7] = 0xd3;
}

static int __lcow_gcs_bridge_listen(struct containerv_container* container)
{
    WSADATA                 wsa;
    SOCKET                  listener;
    GUID                    vmid;
    wchar_t                 vmidBraced[__CONTAINER_ID_LENGTH + 3];
    struct chef_sockaddr_hv addr;
    u_long                  nonblocking = 1;

    if (container == NULL || container->vm_id == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        VLOG_ERROR("containerv[gcs]", "WSAStartup failed: %d\n", WSAGetLastError());
        errno = EIO;
        return -1;
    }

    if (swprintf(vmidBraced, sizeof(vmidBraced) / sizeof(vmidBraced[0]), L"{%ls}", container->vm_id) < 0 ||
        CLSIDFromString(vmidBraced, &vmid) != S_OK) {
        VLOG_ERROR("containerv[gcs]", "failed to parse LCOW VMID for HvSocket listener\n");
        errno = EINVAL;
        return -1;
    }

    listener = socket(AF_HYPERV, SOCK_STREAM, HV_PROTOCOL_RAW);
    if (listener == INVALID_SOCKET) {
        VLOG_ERROR("containerv[gcs]", "failed to create HvSocket listener: %d\n", WSAGetLastError());
        errno = EIO;
        return -1;
    }

    memset(&addr, 0, sizeof(addr));
    addr.Family = AF_HYPERV;
    addr.VmId = vmid;
    __gcs_vsock_service_guid(LCOW_GCS_VSOCK_PORT, &addr.ServiceId);

    if (bind(listener, (const struct sockaddr*)&addr, sizeof(addr)) != 0 || listen(listener, 1) != 0) {
        VLOG_ERROR("containerv[gcs]", "failed to bind/listen HvSocket GCS bridge: %d\n", WSAGetLastError());
        closesocket(listener);
        errno = EIO;
        return -1;
    }

    if (ioctlsocket(listener, FIONBIO, &nonblocking) != 0) {
        closesocket(listener);
        errno = EIO;
        return -1;
    }

    container->lcow_gcs_listener = (uintptr_t)listener;
    VLOG_DEBUG("containerv[gcs]", "listening for LCOW GCS bridge on vsock port 0x%08x\n", LCOW_GCS_VSOCK_PORT);
    return 0;
}

static int __lcow_gcs_bridge_accept(struct containerv_container* container)
{
    SOCKET    listener;
    SOCKET    client;
    ULONGLONG deadline;
    u_long    blocking;

    if (container == NULL || container->lcow_gcs_listener == (uintptr_t)INVALID_SOCKET) {
        errno = EINVAL;
        return -1;
    }

    listener = (SOCKET)container->lcow_gcs_listener;
    deadline = GetTickCount64() + HCS_OPERATION_TIMEOUT_MS;
    while (GetTickCount64() < deadline) {
        client = accept(listener, NULL, NULL);
        if (client != INVALID_SOCKET) {
            blocking = 0;
            if (ioctlsocket(client, FIONBIO, &blocking) != 0) {
                VLOG_ERROR("containerv[gcs]", "failed to switch LCOW GCS bridge socket to blocking mode: %d\n", WSAGetLastError());
                closesocket(client);
                errno = EIO;
                return -1;
            }
            container->lcow_gcs_socket = (uintptr_t)client;
            closesocket(listener);
            container->lcow_gcs_listener = (uintptr_t)INVALID_SOCKET;
            VLOG_DEBUG("containerv[gcs]", "accepted LCOW GCS bridge connection\n");
            return 0;
        }
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            VLOG_ERROR("containerv[gcs]", "failed to accept LCOW GCS bridge: %d\n", WSAGetLastError());
            errno = EIO;
            return -1;
        }
        Sleep(50);
    }

    VLOG_ERROR("containerv[gcs]", "timed out waiting for LCOW GCS bridge connection\n");
    errno = ETIMEDOUT;
    return -1;
}

static int __lcow_gcs_rpc(struct containerv_container* container, uint32_t rpc, const char* request_json, char** response_json_out)
{
    SOCKET         s;
    uint64_t       id;
    size_t         payloadLen;
    size_t         totalLen;
    unsigned char* out = NULL;
    unsigned char  header[GCS_HDR_SIZE];
    unsigned char* payload = NULL;
    uint32_t       responseType;
    uint32_t       responseSize;
    uint64_t       responseId;
    json_t*        response = NULL;
    json_error_t   jerr;
    json_t*        resultValue;
    int            result;
    int            status = -1;

    if (response_json_out != NULL) {
        *response_json_out = NULL;
    }

    if (container == NULL || container->lcow_gcs_socket == (uintptr_t)INVALID_SOCKET || request_json == NULL) {
        errno = EINVAL;
        return -1;
    }

    s = (SOCKET)container->lcow_gcs_socket;
    id = container->lcow_gcs_next_id++;
    payloadLen = strlen(request_json) + 1;
    totalLen = GCS_HDR_SIZE + payloadLen;
    if (totalLen > 0x10000u) {
        errno = EMSGSIZE;
        return -1;
    }

    out = calloc(totalLen, 1);
    if (out == NULL) {
        errno = ENOMEM;
        return -1;
    }
    __gcs_u32le(out, rpc | GCS_MSG_REQUEST);
    __gcs_u32le(out + 4, (uint32_t)totalLen);
    __gcs_u64le(out + 8, id);
    memcpy(out + GCS_HDR_SIZE, request_json, payloadLen - 1);
    out[totalLen - 1] = '\n';

    VLOG_DEBUG("containerv[gcs]", "GCS RPC request type=0x%08x id=%llu payload=%s\n",
        rpc | GCS_MSG_REQUEST, (unsigned long long)id, request_json);

    if (__gcs_send_all(s, out, totalLen) != 0) {
        goto cleanup;
    }

    for (;;) {
        free(payload);
        payload = NULL;

        if (__gcs_recv_all(s, header, sizeof(header)) != 0) {
            goto cleanup;
        }

        responseType = __gcs_read_u32le(header);
        responseSize = __gcs_read_u32le(header + 4);
        responseId = __gcs_read_u64le(header + 8);
        if (responseSize < GCS_HDR_SIZE || responseSize > 0x10000u) {
            errno = EPROTO;
            goto cleanup;
        }

        payloadLen = responseSize - GCS_HDR_SIZE;
        payload = calloc(payloadLen + 1, 1);
        if (payload == NULL) {
            errno = ENOMEM;
            goto cleanup;
        }
        if (__gcs_recv_all(s, payload, payloadLen) != 0) {
            goto cleanup;
        }

        VLOG_DEBUG("containerv[gcs]", "GCS RPC frame type=0x%08x id=%llu payload=%s\n",
            responseType, (unsigned long long)responseId, payload);

        if ((responseType & GCS_MSG_RESPONSE) != 0 && responseId == id &&
            (responseType & ~GCS_MSG_RESPONSE) == rpc) {
            break;
        }
    }

    memset(&jerr, 0, sizeof(jerr));
    response = json_loads((const char*)payload, 0, &jerr);
    if (response == NULL) {
        errno = EPROTO;
        goto cleanup;
    }

    resultValue = json_object_get(response, "Result");
    result = resultValue != NULL ? (int)json_integer_value(resultValue) : 0;
    if (result != 0) {
        json_t* message = json_object_get(response, "ErrorMessage");
        VLOG_ERROR("containerv[gcs]", "GCS RPC failed: 0x%x %s\n", result,
            json_is_string(message) ? json_string_value(message) : "");
        errno = EIO;
        goto cleanup;
    }

    if (response_json_out != NULL) {
        *response_json_out = _strdup((const char*)payload);
        if (*response_json_out == NULL) {
            errno = ENOMEM;
            goto cleanup;
        }
    }

    status = 0;

cleanup:
    json_decref(response);
    free(payload);
    free(out);
    return status;
}

static int __lcow_gcs_bridge_negotiate(struct containerv_container* container)
{
    char request[512];

    snprintf(
        request,
        sizeof(request),
        "{\"ContainerId\":\"%s\",\"ActivityId\":\"00000000-0000-0000-0000-000000000000\",\"MinimumVersion\":4,\"MaximumVersion\":4}",
        container->id);
    return __lcow_gcs_rpc(container, GCS_RPC_NEGOTIATE_PROTOCOL, request, NULL);
}

static void __lcow_plan9_share_name(const char* host_path, char* buffer, size_t buffer_size)
{
    unsigned long long hash = 1469598103934665603ull;

    if (buffer == NULL || buffer_size == 0) {
        return;
    }

    for (const unsigned char* p = (const unsigned char*)host_path; p != NULL && *p; ++p) {
        hash ^= (unsigned long long)(*p);
        hash *= 1099511628211ull;
    }
    snprintf(buffer, buffer_size, "%llu", (unsigned long long)(hash & 0xffffffffu));
}

static int __lcow_plan9_share_append(json_t* shares, const char* name, const char* host_path, int readonly)
{
    json_t* share;

    if (shares == NULL || name == NULL || host_path == NULL || host_path[0] == '\0') {
        return -1;
    }

    if (windows_grant_vm_group_access(host_path) != 0) {
        VLOG_ERROR("containerv[hcs]", "failed to grant VM group access to Plan9 share %s (%s)\n", name, host_path);
        return -1;
    }

    share = json_object();
    if (share == NULL ||
        containerv_json_object_set_string(share, "Name", name) != 0 ||
        containerv_json_object_set_string(share, "AccessName", name) != 0 ||
        containerv_json_object_set_string(share, "Path", host_path) != 0 ||
        containerv_json_object_set_int(share, "Port", 564) != 0 ||
        containerv_json_object_set_int(share, "Flags", 4 | (readonly ? 1 : 0)) != 0 ||
        json_array_append_new(shares, share) != 0) {
        json_decref(share);
        return -1;
    }

    return 0;
}

struct __lcow_plan9_share_ctx {
    json_t* shares;
};

static int __lcow_plan9_share_layer_cb(const char* host_path, const char* container_path, int readonly, void* user_context)
{
    struct __lcow_plan9_share_ctx* ctx = user_context;
    char                           name[64];

    (void)container_path;

    if (ctx == NULL || host_path == NULL || host_path[0] == '\0') {
        return 0;
    }

    __lcow_plan9_share_name(host_path, name, sizeof(name));
    return __lcow_plan9_share_append(ctx->shares, name, host_path, readonly);
}

static int __lcow_gcs_modify(struct containerv_container* container, const char* modification_json)
{
    char*  request = NULL;
    size_t requestCap = 0;
    size_t requestLen = 0;
    int    status;

    if (modification_json == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (__appendf(
            &request,
            &requestCap,
            &requestLen,
            "{\"ContainerId\":\"00000000-0000-0000-0000-000000000000\",\"ActivityId\":\"00000000-0000-0000-0000-000000000000\",\"Request\":%s}",
            modification_json) != 0) {
        return -1;
    }

    status = __lcow_gcs_rpc(container, GCS_RPC_MODIFY_SETTINGS, request, NULL);
    free(request);
    return status;
}

static int __json_set_request_base(json_t* obj, const char* container_id)
{
    return containerv_json_object_set_string(obj, "ContainerId", container_id) != 0 ||
           containerv_json_object_set_string(obj, "ActivityId", "00000000-0000-0000-0000-000000000000") != 0;
}

static int __lcow_gcs_create_container(struct containerv_container* container, const char* container_id, const char* oci_spec_utf8)
{
    json_t* req = NULL;
    json_t* hosted = NULL;
    json_t* schema = NULL;
    json_t* oci = NULL;
    char*   hosted_json = NULL;
    char*   req_json = NULL;
    char    bundle_path[128];
    char    scratch_path[160];
    json_error_t jerr;
    int     status = -1;

    if (container == NULL || container_id == NULL || oci_spec_utf8 == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&jerr, 0, sizeof(jerr));
    oci = json_loads(oci_spec_utf8, 0, &jerr);
    if (oci == NULL) {
        errno = EPROTO;
        return -1;
    }

    req = json_object();
    hosted = json_object();
    schema = json_object();
    if (req == NULL || hosted == NULL || schema == NULL) {
        goto cleanup;
    }

    snprintf(bundle_path, sizeof(bundle_path), "/run/gcs/c/%s", container_id);
    snprintf(scratch_path, sizeof(scratch_path), "/run/gcs/c/%s/scratch/%s", container_id, container_id);

    if (__json_set_request_base(req, container_id) != 0 ||
        containerv_json_object_set_int(schema, "Major", 2) != 0 ||
        containerv_json_object_set_int(schema, "Minor", 1) != 0 ||
        json_object_set_new(hosted, "SchemaVersion", schema) != 0) {
        schema = NULL;
        goto cleanup;
    }
    schema = NULL;

    if (containerv_json_object_set_string(hosted, "OciBundlePath", bundle_path) != 0 ||
        json_object_set_new(hosted, "OciSpecification", oci) != 0 ||
        containerv_json_object_set_string(hosted, "ScratchDirPath", scratch_path) != 0) {
        oci = NULL;
        goto cleanup;
    }
    oci = NULL;

    if (containerv_json_dumps_compact(hosted, &hosted_json) != 0 ||
        containerv_json_object_set_string(req, "ContainerConfig", hosted_json) != 0 ||
        containerv_json_dumps_compact(req, &req_json) != 0) {
        goto cleanup;
    }

    status = __lcow_gcs_rpc(container, GCS_RPC_CREATE, req_json, NULL);

cleanup:
    json_decref(req);
    json_decref(hosted);
    json_decref(schema);
    json_decref(oci);
    free(hosted_json);
    free(req_json);
    return status;
}

static int __lcow_gcs_execute_container_process(struct containerv_container* container, const char* container_id, uint32_t* pid_out)
{
    json_t* req = NULL;
    json_t* settings = NULL;
    json_t* params = NULL;
    char*   params_json = NULL;
    char*   req_json = NULL;
    char*   response_json = NULL;
    json_t* response = NULL;
    json_error_t jerr;
    json_t* pid;
    int     status = -1;

    if (container == NULL || container_id == NULL || pid_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *pid_out = 0;

    req = json_object();
    settings = json_object();
    params = json_object();
    if (req == NULL || settings == NULL || params == NULL) {
        goto cleanup;
    }

    if (__json_set_request_base(req, container_id) != 0 ||
        containerv_json_object_set_bool(params, "CreateStdInPipe", 0) != 0 ||
        containerv_json_object_set_bool(params, "CreateStdOutPipe", 0) != 0 ||
        containerv_json_object_set_bool(params, "CreateStdErrPipe", 0) != 0 ||
        containerv_json_dumps_compact(params, &params_json) != 0 ||
        containerv_json_object_set_string(settings, "ProcessParameters", params_json) != 0 ||
        json_object_set_new(req, "Settings", settings) != 0) {
        settings = NULL;
        goto cleanup;
    }
    settings = NULL;

    if (containerv_json_dumps_compact(req, &req_json) != 0 ||
        __lcow_gcs_rpc(container, GCS_RPC_EXECUTE_PROCESS, req_json, &response_json) != 0) {
        goto cleanup;
    }

    memset(&jerr, 0, sizeof(jerr));
    response = json_loads(response_json, 0, &jerr);
    if (response == NULL) {
        errno = EPROTO;
        goto cleanup;
    }
    pid = json_object_get(response, "ProcessId");
    if (!json_is_integer(pid)) {
        errno = EPROTO;
        goto cleanup;
    }

    *pid_out = (uint32_t)json_integer_value(pid);
    status = 0;

cleanup:
    json_decref(req);
    json_decref(settings);
    json_decref(params);
    json_decref(response);
    free(params_json);
    free(req_json);
    free(response_json);
    return status;
}

static void __lcow_gcs_bridge_close(struct containerv_container* container)
{
    if (container == NULL) {
        return;
    }
    if (container->lcow_gcs_socket != (uintptr_t)INVALID_SOCKET) {
        closesocket((SOCKET)container->lcow_gcs_socket);
        container->lcow_gcs_socket = (uintptr_t)INVALID_SOCKET;
    }
    if (container->lcow_gcs_listener != (uintptr_t)INVALID_SOCKET) {
        closesocket((SOCKET)container->lcow_gcs_listener);
        container->lcow_gcs_listener = (uintptr_t)INVALID_SOCKET;
    }
}

int __hcs_wait_lcow_gcs_process(
    struct containerv_container* container,
    const struct containerv_lcow_gcs_process* process,
    unsigned long* exit_code)
{
    json_t* req = NULL;
    char*   req_json = NULL;
    char*   response_json = NULL;
    json_t* response = NULL;
    json_error_t jerr;
    json_t* value;
    int     status = -1;

    if (container == NULL || process == NULL || process->magic != CONTAINERV_LCOW_GCS_PROCESS_MAGIC) {
        errno = EINVAL;
        return -1;
    }

    req = json_object();
    if (req == NULL) {
        errno = ENOMEM;
        return -1;
    }

    if (__json_set_request_base(req, process->container_id) != 0 ||
        containerv_json_object_set_int(req, "ProcessId", (int)process->pid) != 0 ||
        json_object_set_new(req, "TimeoutInMs", json_integer(0xffffffffu)) != 0 ||
        containerv_json_dumps_compact(req, &req_json) != 0 ||
        __lcow_gcs_rpc(container, GCS_RPC_WAIT_FOR_PROCESS, req_json, &response_json) != 0) {
        goto cleanup;
    }

    memset(&jerr, 0, sizeof(jerr));
    response = json_loads(response_json, 0, &jerr);
    if (response == NULL) {
        errno = EPROTO;
        goto cleanup;
    }

    value = json_object_get(response, "ExitCode");
    if (!json_is_integer(value)) {
        errno = EPROTO;
        goto cleanup;
    }
    if (exit_code != NULL) {
        *exit_code = (unsigned long)json_integer_value(value);
    }
    status = 0;

cleanup:
    json_decref(req);
    json_decref(response);
    free(req_json);
    free(response_json);
    return status;
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

static char* __wide_to_utf8_alloc(const wchar_t* s)
{
    if (s == NULL) {
        return NULL;
    }

    int needed = WideCharToMultiByte(CP_UTF8, 0, s, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) {
        return NULL;
    }

    char* out = calloc((size_t)needed, 1);
    if (out == NULL) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, s, -1, out, needed, NULL, NULL) == 0) {
        free(out);
        return NULL;
    }
    return out;
}

static int __path_exists_utf8(const char* path)
{
    DWORD attributes;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES;
}

static int __lcow_bundle_uses_vhd_boot(const char* image_path)
{
    static const char* const rootfs_candidates[] = { "rootfs.vhd", "rootfs.vhdx", NULL };

    if (image_path == NULL || image_path[0] == '\0') {
        return 0;
    }

    for (int i = 0; rootfs_candidates[i] != NULL; ++i) {
        char* candidate_path = strpathcombine(image_path, rootfs_candidates[i]);
        int   exists = __path_exists_utf8(candidate_path);

        free(candidate_path);
        if (exists) {
            return 1;
        }
    }
    return 0;
}

static int __looks_like_guid(const char* s)
{
    if (s == NULL) {
        return 0;
    }
    // Expected: 8-4-4-4-12 (36 chars)
    if (strlen(s) != 36) {
        return 0;
    }
    const int dash_pos[] = {8, 13, 18, 23};
    for (int i = 0; i < 4; i++) {
        if (s[dash_pos[i]] != '-') {
            return 0;
        }
    }
    for (int i = 0; i < 36; i++) {
        if (s[i] == '-') {
            continue;
        }
        char c = s[i];
        int is_hex = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if (!is_hex) {
            return 0;
        }
    }
    return 1;
}

static char* __normalize_container_path_win_alloc(const char* p)
{
    if (p == NULL) {
        return NULL;
    }
    if (p[0] == '\\' || (strlen(p) >= 2 && p[1] == ':')) {
        return _strdup(p);
    }
    if (p[0] == '/') {
        // Map POSIX-ish paths to C:\<path> for convenience.
        size_t n = strlen(p);
        // Worst case: same length + "C:" prefix
        char* out = calloc(n + 3, 1);
        if (out == NULL) {
            return NULL;
        }
        out[0] = 'C';
        out[1] = ':';
        size_t j = 2;
        for (size_t i = 0; i < n; i++) {
            char c = p[i];
            if (c == '/') {
                out[j++] = '\\';
            } else {
                out[j++] = c;
            }
        }
        out[j] = 0;
        return out;
    }
    // Relative-ish: treat as C:\<p>
    size_t n = strlen(p);
    char* out = calloc(n + 4, 1);
    if (out == NULL) {
        return NULL;
    }
    snprintf(out, n + 4, "C:\\%s", p);
    for (char* q = out; *q; ++q) {
        if (*q == '/') {
            *q = '\\';
        }
    }
    return out;
}

struct __mapped_dir_build_ctx {
    json_t* arr;
    int     linux_container;
    const char* linux_container_prefix;
};

static char* __normalize_container_path_linux_alloc(const char* p);
static void CALLBACK __hcs_operation_callback(HCS_OPERATION operation, void* context);

static DWORD WINAPI __lcow_console_reader(void* context)
{
    struct containerv_container* container = context;
    char                         buffer[1024];
    char                         line[4096];
    size_t                       lineLength = 0;
    DWORD                        bytesRead;
    BOOL                         connected;

    if (container == NULL || container->lcow_console_pipe == INVALID_HANDLE_VALUE) {
        return 1;
    }

    connected = ConnectNamedPipe(container->lcow_console_pipe, NULL)
        ? TRUE
        : GetLastError() == ERROR_PIPE_CONNECTED;
    if (!connected) {
        return 1;
    }

    while (ReadFile(container->lcow_console_pipe, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
        for (DWORD i = 0; i < bytesRead; i++) {
            char c = buffer[i];

            if (c == '\r') {
                continue;
            }
            if (c == '\n' || lineLength + 1 >= sizeof(line)) {
                line[lineLength] = '\0';
                if (lineLength > 0) {
                    VLOG_DEBUG("containerv[lcow-console]", "%s\n", line);
                }
                lineLength = 0;
                continue;
            }
            line[lineLength++] = c;
        }
    }

    if (lineLength > 0) {
        line[lineLength] = '\0';
        VLOG_DEBUG("containerv[lcow-console]", "%s\n", line);
    }
    return 0;
}

static int __lcow_console_start(struct containerv_container* container, char* path, size_t pathSize)
{
    if (container == NULL || path == NULL || pathSize == 0) {
        return -1;
    }

    if (snprintf(path, pathSize, "\\\\.\\pipe\\chef-lcow-%s", container->id) < 0) {
        return -1;
    }

    container->lcow_console_pipe = CreateNamedPipeA(
        path,
        PIPE_ACCESS_DUPLEX,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        4096,
        4096,
        0,
        NULL);
    if (container->lcow_console_pipe == INVALID_HANDLE_VALUE) {
        VLOG_ERROR("containerv[hcs]", "failed to create LCOW console pipe %s: %lu\n", path, GetLastError());
        return -1;
    }

    container->lcow_console_thread = CreateThread(NULL, 0, __lcow_console_reader, container, 0, NULL);
    if (container->lcow_console_thread == NULL) {
        CloseHandle(container->lcow_console_pipe);
        container->lcow_console_pipe = INVALID_HANDLE_VALUE;
        return -1;
    }
    return 0;
}

static int __hcs_wait_for_guest_connection(struct containerv_container* container)
{
    static const wchar_t query[] = L"{\"PropertyTypes\":[\"GuestConnection\"]}";
    ULONGLONG deadline;

    if (container == NULL || container->hcs_system == NULL ||
        g_hcs.HcsGetComputeSystemProperties == NULL || g_hcs.HcsWaitForOperationResult == NULL) {
        errno = EINVAL;
        return -1;
    }

    deadline = GetTickCount64() + HCS_OPERATION_TIMEOUT_MS;
    while (GetTickCount64() < deadline) {
        HCS_OPERATION operation;
        PWSTR         resultDoc = NULL;
        HRESULT       hr;

        operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
        if (operation == NULL) {
            return -1;
        }

        hr = g_hcs.HcsGetComputeSystemProperties(container->hcs_system, operation, query);
        if (SUCCEEDED(hr)) {
            hr = g_hcs.HcsWaitForOperationResult(operation, 1000, &resultDoc);
        }

        if (SUCCEEDED(hr) && resultDoc != NULL) {
            char* resultUtf8 = __wide_to_utf8_alloc(resultDoc);
            if (resultUtf8 != NULL && strstr(resultUtf8, "GuestConnectionInfo") != NULL) {
                VLOG_DEBUG("containerv[hcs]", "HCS guest connection is ready\n");
                free(resultUtf8);
                __hcs_localfree_wstr(resultDoc);
                g_hcs.HcsCloseOperation(operation);
                return 0;
            }
            free(resultUtf8);
        }

        __hcs_localfree_wstr(resultDoc);
        g_hcs.HcsCloseOperation(operation);
        Sleep(250);
    }

    VLOG_ERROR("containerv[hcs]", "timed out waiting for HCS guest connection\n");
    errno = ETIMEDOUT;
    return -1;
}

// Build a Windows-compatible command line from spawn options.
static int __build_windows_cmdline(const struct __containerv_spawn_options* options, char** cmdline_out)
{
    size_t     cmdCap;
    size_t     cmdLen;
    char*      cmdline;
    int        i;
    int        needsQuotes;
    const char* arg;
    const char* p;

    if (options == NULL || options->path == NULL || cmdline_out == NULL) {
        return -1;
    }

    cmdCap = 0;
    cmdLen = 0;
    cmdline = NULL;

    if (__appendf(&cmdline, &cmdCap, &cmdLen, "%s", options->path) != 0) {
        free(cmdline);
        return -1;
    }

    if (options->argv != NULL) {
        for (i = 1; options->argv[i] != NULL; ++i) {
            arg = options->argv[i];
            if (arg == NULL) {
                continue;
            }

            needsQuotes = 0;
            for (p = arg; *p; ++p) {
                if (*p == ' ' || *p == '\t' || *p == '"') {
                    needsQuotes = 1;
                    break;
                }
            }

            if (!needsQuotes) {
                if (__appendf(&cmdline, &cmdCap, &cmdLen, " %s", arg) != 0) {
                    free(cmdline);
                    return -1;
                }
                continue;
            }

            if (__appendf(&cmdline, &cmdCap, &cmdLen, " \"") != 0) {
                free(cmdline);
                return -1;
            }
            for (p = arg; *p; ++p) {
                if (*p == '"') {
                    if (__appendf(&cmdline, &cmdCap, &cmdLen, "\\\"") != 0) {
                        free(cmdline);
                        return -1;
                    }
                } else {
                    if (__appendf(&cmdline, &cmdCap, &cmdLen, "%c", *p) != 0) {
                        free(cmdline);
                        return -1;
                    }
                }
            }
            if (__appendf(&cmdline, &cmdCap, &cmdLen, "\"") != 0) {
                free(cmdline);
                return -1;
            }
        }
    }

    *cmdline_out = cmdline;
    return 0;
}

// Build a Linux command line and argv array JSON from spawn options.
static int __build_linux_cmdline_and_args(
    const struct __containerv_spawn_options* options,
    char**                                  cmdline_out,
    json_t**                                cmd_args_out,
    char**                                  args_json_out)
{
    const char* const* argv;
    const char*        defaultArgvBuf[2];
    size_t             cmdCap;
    size_t             cmdLen;
    char*              cmdline;
    json_t*            cmdArgs;
    char*              argsJson;
    int                i;

    if (options == NULL || options->path == NULL || cmdline_out == NULL || cmd_args_out == NULL || args_json_out == NULL) {
        return -1;
    }

    argv = options->argv;
    defaultArgvBuf[0] = NULL;
    defaultArgvBuf[1] = NULL;
    if (argv == NULL) {
        defaultArgvBuf[0] = options->path;
        defaultArgvBuf[1] = NULL;
        argv = defaultArgvBuf;
    }

    cmdCap = 0;
    cmdLen = 0;
    cmdline = NULL;
    if (__appendf(&cmdline, &cmdCap, &cmdLen, "%s", argv[0]) != 0) {
        free(cmdline);
        return -1;
    }
    for (i = 1; argv[i] != NULL; ++i) {
        if (__appendf(&cmdline, &cmdCap, &cmdLen, " %s", argv[i]) != 0) {
            free(cmdline);
            return -1;
        }
    }

    cmdArgs = json_array();
    if (cmdArgs == NULL) {
        free(cmdline);
        return -1;
    }
    for (i = 0; argv[i] != NULL; ++i) {
        if (containerv_json_array_append_string(cmdArgs, argv[i]) != 0) {
            json_decref(cmdArgs);
            free(cmdline);
            return -1;
        }
    }

    argsJson = NULL;
    if (containerv_json_dumps_compact(cmdArgs, &argsJson) != 0) {
        json_decref(cmdArgs);
        free(cmdline);
        return -1;
    }

    *cmdline_out = cmdline;
    *cmd_args_out = cmdArgs;
    *args_json_out = argsJson;
    return 0;
}

// Build an OCI spec JSON for LCOW and persist it for inspection.
static char* __normalize_container_path_linux_alloc(const char* p);
static char* __join_linux_prefix_alloc(const char* prefix, const char* container_path);

struct __lcow_mount_ctx {
    struct containerv_oci_mount_entry* mounts;
    size_t                             count;
    size_t                             cap;
    const char*                        root_prefix;
};

static void __lcow_mounts_free(struct __lcow_mount_ctx* ctx)
{
    if (ctx == NULL || ctx->mounts == NULL) {
        return;
    }
    for (size_t i = 0; i < ctx->count; ++i) {
        free((void*)ctx->mounts[i].source);
        free((void*)ctx->mounts[i].destination);
    }
    free(ctx->mounts);
    ctx->mounts = NULL;
    ctx->count = 0;
    ctx->cap = 0;
}

static int __lcow_mounts_append(struct __lcow_mount_ctx* ctx, const char* source, const char* destination, int readonly)
{
    if (ctx == NULL || source == NULL || destination == NULL) {
        return -1;
    }

    if (ctx->count == ctx->cap) {
        size_t newCap = ctx->cap == 0 ? 4 : ctx->cap * 2;
        struct containerv_oci_mount_entry* newMounts = realloc(ctx->mounts, newCap * sizeof(*newMounts));
        if (newMounts == NULL) {
            return -1;
        }
        ctx->mounts = newMounts;
        ctx->cap = newCap;
    }

    ctx->mounts[ctx->count].source = _strdup(source);
    ctx->mounts[ctx->count].destination = _strdup(destination);
    ctx->mounts[ctx->count].readonly = readonly;
    if (ctx->mounts[ctx->count].source == NULL || ctx->mounts[ctx->count].destination == NULL) {
        free((void*)ctx->mounts[ctx->count].source);
        free((void*)ctx->mounts[ctx->count].destination);
        return -1;
    }
    ctx->count++;
    return 0;
}

static int __lcow_collect_mount_cb(const char* host_path, const char* container_path, int readonly, void* user_context)
{
    struct __lcow_mount_ctx* ctx = (struct __lcow_mount_ctx*)user_context;
    char* src;
    char* dst;

    (void)host_path;

    if (ctx == NULL || container_path == NULL || container_path[0] == '\0') {
        return 0;
    }

    dst = __normalize_container_path_linux_alloc(container_path);
    src = __join_linux_prefix_alloc(ctx->root_prefix, container_path);
    if (dst == NULL || src == NULL) {
        free(dst);
        free(src);
        return -1;
    }

    if (__lcow_mounts_append(ctx, src, dst, readonly) != 0) {
        free(dst);
        free(src);
        return -1;
    }

    free(dst);
    free(src);
    return 0;
}

static int __build_oci_spec_if_needed(
    struct containerv_container*            container,
    const struct __containerv_spawn_options* options,
    const char*                             args_json_utf8,
    int                                     guest_is_windows,
    char**                                  oci_spec_out)
{
    struct containerv_oci_linux_spec_params params;
    struct __lcow_mount_ctx                 mountCtx;
    const char*                             rootfs_host;
    char*                                   ociSpec;

    if (oci_spec_out == NULL) {
        return -1;
    }
    *oci_spec_out = NULL;

    if (guest_is_windows) {
        return 0;
    }

    rootfs_host = NULL;
    if (container != NULL && container->runtime_dir != NULL) {
        struct containerv_oci_bundle_paths bundlePaths;
        memset(&bundlePaths, 0, sizeof(bundlePaths));
        if (containerv_oci_bundle_get_paths(container->runtime_dir, &bundlePaths) == 0) {
            if (bundlePaths.rootfs_dir != NULL && PathFileExistsA(bundlePaths.rootfs_dir)) {
                rootfs_host = bundlePaths.rootfs_dir;
            }
            containerv_oci_bundle_paths_delete(&bundlePaths);
        }
    }

    if (rootfs_host == NULL && container != NULL && container->rootfs != NULL && PathFileExistsA(container->rootfs)) {
        // Schema2 LCOW maps the prepared host rootfs directly at /chef/rootfs.
        rootfs_host = container->rootfs;
    }

    // If we don't have a rootfs mapped, fail LCOW process creation.
    if (rootfs_host == NULL || rootfs_host[0] == '\0') {
        VLOG_ERROR("containerv[hcs]", "LCOW OCI spec failed: rootfs not mapped for container %s\n",
                   container ? container->id : "<unknown>");
        return -1;
    }

    memset(&mountCtx, 0, sizeof(mountCtx));
    mountCtx.root_prefix = "/chef/rootfs";

    // Always mount the staging directory when using OCI-in-UVM.
    {
        char* stage_src = __join_linux_prefix_alloc(mountCtx.root_prefix, "/chef/staging");
        char* stage_dst = __normalize_container_path_linux_alloc("/chef/staging");
        if (stage_src == NULL || stage_dst == NULL ||
            __lcow_mounts_append(&mountCtx, stage_src, stage_dst, 0) != 0) {
            free(stage_src);
            free(stage_dst);
            __lcow_mounts_free(&mountCtx);
            return -1;
        }
        free(stage_src);
        free(stage_dst);
    }

    if (container != NULL && container->layers != NULL) {
        if (containerv_layers_iterate(container->layers, CONTAINERV_LAYER_HOST_DIRECTORY, __lcow_collect_mount_cb, &mountCtx) != 0) {
            __lcow_mounts_free(&mountCtx);
            return -1;
        }
    }

    params.args_json = (args_json_utf8 != NULL) ? args_json_utf8 : "[]";
    params.envv = (const char* const*)options->envv;
    params.root_path = "/chef/rootfs";
    params.cwd = "/";
    params.hostname = "chef";
    params.mounts = mountCtx.mounts;
    params.mounts_count = mountCtx.count;

    ociSpec = NULL;
    if (containerv_oci_build_linux_spec_json(&params, &ociSpec) != 0) {
        __lcow_mounts_free(&mountCtx);
        return -1;
    }

    if (container != NULL) {
        struct containerv_oci_bundle_paths bundle;
        memset(&bundle, 0, sizeof(bundle));
        if (containerv_oci_bundle_get_paths(container->runtime_dir, &bundle) == 0) {
            (void)containerv_oci_bundle_write_config(&bundle, ociSpec);
            containerv_oci_bundle_paths_delete(&bundle);
        }
    }

    *oci_spec_out = ociSpec;
    __lcow_mounts_free(&mountCtx);
    return 0;
}

// Build environment JSON and compute whether to include security fields.
static int __build_env_object(
    const struct __containerv_spawn_options* options,
    int                                     guest_is_windows,
    const struct containerv_policy*         policy,
    enum containerv_security_level          security_level,
    int                                     win_use_app_container,
    const char*                             win_integrity_level,
    const char* const*                      win_capability_sids,
    int                                     win_capability_sid_count,
    json_t**                                env_obj_out,
    int*                                    try_security_fields_out)
{
    json_t*     envObj;
    const char* kv;
    const char* eq;
    const char* val;
    char*       key;
    size_t      keyLen;
    int         setRc;
    int         i;
    int         hasPath;
    int         hasPolicyLevel;
    int         hasPolicyAppc;
    int         hasPolicyIntegrity;
    int         hasPolicyCaps;
    const char* defaultPath;
    const char* lvl;
    char*       caps;
    size_t      capsCap;
    size_t      capsLen;

    if (env_obj_out == NULL || try_security_fields_out == NULL) {
        return -1;
    }
    *env_obj_out = NULL;
    *try_security_fields_out = 0;

    envObj = NULL;
    kv = NULL;
    eq = NULL;
    val = NULL;
    key = NULL;
    keyLen = 0;
    setRc = 0;
    i = 0;
    hasPath = 0;
    hasPolicyLevel = 0;
    hasPolicyAppc = 0;
    hasPolicyIntegrity = 0;
    hasPolicyCaps = 0;
    defaultPath = NULL;
    lvl = NULL;
    caps = NULL;
    capsCap = 0;
    capsLen = 0;

    if (options->envv != NULL) {
        for (i = 0; options->envv[i] != NULL; ++i) {
            if (_strnicmp(options->envv[i], "PATH=", 5) == 0) {
                hasPath = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_SECURITY_LEVEL=", 30) == 0) {
                hasPolicyLevel = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_USE_APPCONTAINER=", 38) == 0) {
                hasPolicyAppc = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_INTEGRITY_LEVEL=", 36) == 0) {
                hasPolicyIntegrity = 1;
                continue;
            }
            if (_strnicmp(options->envv[i], "CHEF_CONTAINERV_WIN_CAPABILITY_SIDS=", 39) == 0) {
                hasPolicyCaps = 1;
                continue;
            }
        }
    }

    envObj = json_object();
    if (envObj == NULL) {
        return -1;
    }

    if (!hasPath) {
        defaultPath = guest_is_windows
            ? "C:\\Windows\\System32;C:\\Windows"
            : "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin";
        if (containerv_json_object_set_string(envObj, "PATH", defaultPath) != 0) {
            json_decref(envObj);
            return -1;
        }
    }

    if (options->envv != NULL) {
        for (i = 0; options->envv[i] != NULL; ++i) {
            kv = options->envv[i];
            eq = strchr(kv, '=');
            if (eq == NULL || eq == kv) {
                continue;
            }
            keyLen = (size_t)(eq - kv);
            key = calloc(keyLen + 1, 1);
            if (key == NULL) {
                json_decref(envObj);
                return -1;
            }
            memcpy(key, kv, keyLen);
            key[keyLen] = '\0';
            val = eq + 1;

            setRc = containerv_json_object_set_string(envObj, key, val);
            free(key);
            if (setRc != 0) {
                json_decref(envObj);
                return -1;
            }
        }
    }

    if (policy != NULL) {
        if (!hasPolicyLevel) {
            lvl = "default";
            if (security_level >= CV_SECURITY_STRICT) {
                lvl = "strict";
            } else if (security_level >= CV_SECURITY_RESTRICTED) {
                lvl = "restricted";
            }
            if (containerv_json_object_set_string(envObj, "CHEF_CONTAINERV_SECURITY_LEVEL", lvl) != 0) {
                json_decref(envObj);
                return -1;
            }
        }

        if (guest_is_windows && !hasPolicyAppc) {
            if (containerv_json_object_set_string(envObj, "CHEF_CONTAINERV_WIN_USE_APPCONTAINER", win_use_app_container ? "1" : "0") != 0) {
                json_decref(envObj);
                return -1;
            }
        }

        if (guest_is_windows && !hasPolicyIntegrity && win_integrity_level != NULL && win_integrity_level[0] != '\0') {
            if (containerv_json_object_set_string(envObj, "CHEF_CONTAINERV_WIN_INTEGRITY_LEVEL", win_integrity_level) != 0) {
                json_decref(envObj);
                return -1;
            }
        }

        if (guest_is_windows && !hasPolicyCaps && win_capability_sids != NULL && win_capability_sid_count > 0) {
            caps = NULL;
            capsCap = 0;
            capsLen = 0;
            for (i = 0; i < win_capability_sid_count; i++) {
                if (win_capability_sids[i] == NULL) {
                    continue;
                }
                if (__appendf(&caps, &capsCap, &capsLen, "%s%s", (capsLen == 0) ? "" : ",", win_capability_sids[i]) != 0) {
                    free(caps);
                    json_decref(envObj);
                    return -1;
                }
            }
            if (caps != NULL && caps[0] != '\0') {
                setRc = containerv_json_object_set_string(envObj, "CHEF_CONTAINERV_WIN_CAPABILITY_SIDS", caps);
                free(caps);
                if (setRc != 0) {
                    json_decref(envObj);
                    return -1;
                }
            } else {
                free(caps);
            }
        }
    }

    if (guest_is_windows && policy != NULL) {
        if (security_level >= CV_SECURITY_RESTRICTED || win_use_app_container || (win_integrity_level != NULL && win_integrity_level[0] != '\0')) {
            *try_security_fields_out = 1;
        }
    }

    *env_obj_out = envObj;
    return 0;
}

// Build HCS process JSON configuration for Windows or LCOW.
static int __build_process_config_json(
    int                                     guest_is_windows,
    const struct __containerv_spawn_options* options,
    json_t*                                 env_obj,
    const char*                             cmdline_utf8,
    json_t*                                 cmd_args,
    const char*                             linux_cmdline_utf8,
    const char*                             oci_spec_utf8,
    int                                     include_security,
    int                                     lcow_use_oci,
    int                                     lcow_use_command_args,
    int                                     create_in_uvm,
    char**                                  json_utf8_out)
{
    const char* workingDir;
    int         emulateConsole;
    json_t*     procCfg;
    json_error_t jerr;
    json_t*     oci;
    char*       jsonUtf8;

    if (json_utf8_out == NULL) {
        return -1;
    }
    *json_utf8_out = NULL;

    workingDir = guest_is_windows ? "C:\\\\" : "/";
    emulateConsole = guest_is_windows ? 1 : 0;
    procCfg = json_object();
    if (procCfg == NULL) {
        return -1;
    }

    if (guest_is_windows) {
        if (cmdline_utf8 == NULL) {
            json_decref(procCfg);
            return -1;
        }
        if (containerv_json_object_set_string(procCfg, "CommandLine", cmdline_utf8) != 0 ||
            containerv_json_object_set_string(procCfg, "WorkingDirectory", workingDir) != 0 ||
            json_object_set(procCfg, "Environment", env_obj) != 0 ||
            containerv_json_object_set_bool(procCfg, "EmulateConsole", emulateConsole) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateStdInPipe", options->create_stdio_pipes != 0) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateStdOutPipe", options->create_stdio_pipes != 0) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateStdErrPipe", options->create_stdio_pipes != 0) != 0) {
            json_decref(procCfg);
            return -1;
        }
        if (include_security) {
            if (containerv_json_object_set_string(procCfg, "User", "ContainerUser") != 0) {
                json_decref(procCfg);
                return -1;
            }
        }
    } else {
        if (lcow_use_command_args && cmd_args == NULL) {
            json_decref(procCfg);
            return -1;
        }

        if ((lcow_use_command_args && json_object_set(procCfg, "CommandArgs", cmd_args) != 0) ||
            (!lcow_use_command_args && linux_cmdline_utf8 && containerv_json_object_set_string(procCfg, "CommandLine", linux_cmdline_utf8) != 0) ||
            containerv_json_object_set_string(procCfg, "WorkingDirectory", workingDir) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateInUtilityVm", create_in_uvm) != 0 ||
            json_object_set(procCfg, "Environment", env_obj) != 0 ||
            containerv_json_object_set_bool(procCfg, "EmulateConsole", emulateConsole) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateStdInPipe", options->create_stdio_pipes != 0) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateStdOutPipe", options->create_stdio_pipes != 0) != 0 ||
            containerv_json_object_set_bool(procCfg, "CreateStdErrPipe", options->create_stdio_pipes != 0) != 0) {
            json_decref(procCfg);
            return -1;
        }

        if (lcow_use_oci) {
            memset(&jerr, 0, sizeof(jerr));
            oci = json_loads(oci_spec_utf8, 0, &jerr);
            if (oci == NULL) {
                json_decref(procCfg);
                return -1;
            }
            if (json_object_set_new(procCfg, "OCISpecification", oci) != 0) {
                json_decref(oci);
                json_decref(procCfg);
                return -1;
            }
            if (containerv_json_object_set_bool(procCfg, "CreateInUtilityVm", 0) != 0) {
                json_decref(procCfg);
                return -1;
            }
        }
    }

    jsonUtf8 = NULL;
    if (containerv_json_dumps_compact(procCfg, &jsonUtf8) != 0) {
        json_decref(procCfg);
        return -1;
    }

    json_decref(procCfg);
    *json_utf8_out = jsonUtf8;
    return 0;
}

static int __starts_with_path_prefix(const char* s, const char* prefix)
{
    if (s == NULL || prefix == NULL) {
        return 0;
    }
    size_t ps = strlen(prefix);
    if (ps == 0) {
        return 1;
    }
    if (strncmp(s, prefix, ps) != 0) {
        return 0;
    }
    // Exact match or prefix followed by '/'
    return (s[ps] == '\0' || s[ps] == '/');
}

static char* __join_linux_prefix_alloc(const char* prefix, const char* container_path)
{
    char* norm = __normalize_container_path_linux_alloc(container_path);
    if (norm == NULL) {
        return NULL;
    }

    if (prefix == NULL || prefix[0] == '\0') {
        return norm;
    }

    // Normalize prefix: ensure leading '/', no trailing '/'.
    const char* pfx = prefix;
    char* pfx_norm = NULL;
    if (pfx[0] != '/') {
        size_t n = strlen(pfx);
        pfx_norm = calloc(n + 2, 1);
        if (pfx_norm == NULL) {
            free(norm);
            return NULL;
        }
        pfx_norm[0] = '/';
        memcpy(pfx_norm + 1, pfx, n + 1);
        pfx = pfx_norm;
    }

    size_t pfx_len = strlen(pfx);
    while (pfx_len > 1 && pfx[pfx_len - 1] == '/') {
        pfx_len--;
    }

    if (__starts_with_path_prefix(norm, pfx)) {
        free(pfx_norm);
        return norm;
    }

    // Join pfx + norm (drop leading '/' from norm).
    const char* rel = norm;
    while (*rel == '/') {
        rel++;
    }

    size_t rel_len = strlen(rel);
    size_t out_len = pfx_len + 1 + rel_len + 1;
    char* out = calloc(out_len, 1);
    if (out == NULL) {
        free(pfx_norm);
        free(norm);
        return NULL;
    }

    memcpy(out, pfx, pfx_len);
    out[pfx_len] = '/';
    memcpy(out + pfx_len + 1, rel, rel_len);
    out[pfx_len + 1 + rel_len] = '\0';

    free(pfx_norm);
    free(norm);
    return out;
}

static char* __normalize_container_path_linux_alloc(const char* p)
{
    if (p == NULL || p[0] == '\0') {
        return _strdup("/");
    }

    // Normalize Windows-ish paths to Linux container paths.
    // - Strip extended prefix "\\?\\".
    // - Strip drive letters ("C:").
    // - Collapse leading slashes/backslashes.
    // - Replace backslashes with forward slashes.
    const char* s = p;

    if (s[0] == '\\' && s[1] == '\\' && s[2] == '?' && s[3] == '\\') {
        s += 4;
    }

    if (((s[0] >= 'A' && s[0] <= 'Z') || (s[0] >= 'a' && s[0] <= 'z')) && s[1] == ':') {
        s += 2;
    }

    while (*s == '/' || *s == '\\') {
        s++;
    }

    size_t n = strlen(s);
    char* out = calloc(n + 2, 1); // leading '/' + NUL
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    out[j++] = '/';
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        out[j++] = (c == '\\') ? '/' : c;
    }
    out[j] = '\0';
    return out;
}

static int __append_mapped_dir_entry(
    struct __mapped_dir_build_ctx* ctx,
    const char* host_path,
    const char* container_path,
    int readonly)
{
    if (ctx == NULL || ctx->arr == NULL || !json_is_array(ctx->arr)) {
        return -1;
    }

    if (host_path == NULL || host_path[0] == '\0' || container_path == NULL) {
        return -1;
    }

    DWORD attrs = GetFileAttributesA(host_path);
    if (attrs == INVALID_FILE_ATTRIBUTES) {
        if (readonly) {
            VLOG_ERROR("containerv[hcs]", "mapped dir missing (readonly): %s\n", host_path);
            return -1;
        }
        // Best-effort create for writable mounts.
        int mk = SHCreateDirectoryExA(NULL, host_path, NULL);
        if (mk != ERROR_SUCCESS && mk != ERROR_ALREADY_EXISTS) {
            VLOG_ERROR("containerv[hcs]", "failed to create mapped dir %s (err=%d)\n", host_path, mk);
            return -1;
        }
    } else if ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
        VLOG_ERROR("containerv[hcs]", "mapped dir host path is not a directory: %s\n", host_path);
        return -1;
    }

    char* norm_container = NULL;
    if (ctx->linux_container) {
        norm_container = __join_linux_prefix_alloc(ctx->linux_container_prefix, container_path);
    } else {
        norm_container = __normalize_container_path_win_alloc(container_path);
    }
    if (norm_container == NULL) {
        free(norm_container);
        return -1;
    }

    json_t* obj = json_object();
    if (obj == NULL ||
        containerv_json_object_set_string(obj, "HostPath", host_path) != 0 ||
        containerv_json_object_set_string(obj, "ContainerPath", norm_container) != 0 ||
        containerv_json_object_set_bool(obj, "ReadOnly", readonly != 0) != 0) {
        free(norm_container);
        json_decref(obj);
        return -1;
    }
    free(norm_container);

    if (ctx->linux_container) {
        if (containerv_json_object_set_bool(obj, "LinuxMetadata", 1) != 0) {
            json_decref(obj);
            return -1;
        }
    }

    if (json_array_append_new(ctx->arr, obj) != 0) {
        json_decref(obj);
        return -1;
    }
    return 0;
}

static int __mapped_dir_cb(const char* host_path, const char* container_path, int readonly, void* user_context)
{
    struct __mapped_dir_build_ctx* ctx = (struct __mapped_dir_build_ctx*)user_context;
    return __append_mapped_dir_entry(ctx, host_path, container_path, readonly);
}


static void __derive_layer_id_from_path(const char* path, char out36[37])
{
    // Prefer basename if it looks like a GUID (common for windowsfilter layer folders).
    const char* base = path;
    if (path != NULL) {
        const char* s1 = strrchr(path, '\\');
        const char* s2 = strrchr(path, '/');
        base = s1;
        if (s2 != NULL && (base == NULL || s2 > base)) {
            base = s2;
        }
        if (base != NULL) {
            base++;
        } else {
            base = path;
        }
    }

    if (__looks_like_guid(base)) {
        strncpy(out36, base, 36);
        out36[36] = 0;
        return;
    }

    // Fallback: stable pseudo-GUID derived from FNV1a of the path.
    uint64_t h1 = 1469598103934665603ull;
    uint64_t h2 = 1099511628211ull;
    if (path != NULL) {
        for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
            h1 ^= (uint64_t)(*p);
            h1 *= 1099511628211ull;
        }
        for (const unsigned char* p = (const unsigned char*)path; *p; ++p) {
            h2 ^= (uint64_t)(*p);
            h2 *= 1469598103934665603ull;
        }
    }

    unsigned int a = (unsigned int)(h1 & 0xffffffffu);
    unsigned short b = (unsigned short)((h1 >> 32) & 0xffffu);
    unsigned short c = (unsigned short)((h1 >> 48) & 0xffffu);
    unsigned short d = (unsigned short)(h2 & 0xffffu);
    unsigned long long e = (unsigned long long)((h2 >> 16) & 0xffffffffffffull);
    (void)snprintf(out36, 37, "%08x-%04x-%04x-%04x-%012llx", a, b, c, d, e);
    out36[36] = 0;
}

static wchar_t* __hcs_create_container_config_schema1(
    struct containerv_container* container,
    struct containerv_options* options,
    const char* layer_folder_path,
    const char* const* parent_layers,
    int parent_layer_count,
    const char* utilityvm_path,
    int linux_container)
{
    if (container == NULL || parent_layer_count < 0) {
        return NULL;
    }

    int hv = 0;
    if (linux_container) {
        hv = 1;
    } else if (options != NULL && options->windows_container.isolation == WINDOWS_CONTAINER_ISOLATION_HYPERV) {
        hv = 1;
    }

    json_t* cfg = json_object();
    json_t* mapped = json_array();
    json_t* layers = json_array();
    if (cfg == NULL || mapped == NULL || layers == NULL) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    const char* hostname = (container->hostname && container->hostname[0] != '\0') ? container->hostname : container->id;

    if (containerv_json_object_set_string(cfg, "SystemType", "Container") != 0 ||
        containerv_json_object_set_string(cfg, "Name", container->id) != 0 ||
        containerv_json_object_set_string(cfg, "Owner", "chef") != 0 ||
        containerv_json_object_set_string(cfg, "HostName", hostname) != 0) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    if (!linux_container) {
        if (layer_folder_path == NULL || layer_folder_path[0] == '\0' || parent_layers == NULL) {
            json_decref(cfg);
            json_decref(mapped);
            json_decref(layers);
            return NULL;
        }
        if (containerv_json_object_set_string(cfg, "LayerFolderPath", layer_folder_path) != 0) {
            json_decref(cfg);
            json_decref(mapped);
            json_decref(layers);
            return NULL;
        }
    }

    // Mapped directories
    {
        const int lcow_has_rootfs = (linux_container && layer_folder_path != NULL && layer_folder_path[0] != '\0');

        if (lcow_has_rootfs) {
            struct __mapped_dir_build_ctx rootctx = {
                .arr = mapped,
                .linux_container = linux_container,
                .linux_container_prefix = NULL,
            };
            if (__append_mapped_dir_entry(&rootctx, layer_folder_path, "/chef/rootfs", 0) != 0) {
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
        }

        struct __mapped_dir_build_ctx mctx = {
            .arr = mapped,
            .linux_container = linux_container,
            .linux_container_prefix = lcow_has_rootfs ? "/chef/rootfs" : NULL,
        };

        char stage_host[MAX_PATH];
        snprintf(stage_host, sizeof(stage_host), "%s\\staging", container->runtime_dir);
        if (__append_mapped_dir_entry(&mctx, stage_host, linux_container ? "/chef/staging" : "C:\\chef\\staging", 0) != 0) {
            json_decref(cfg);
            json_decref(mapped);
            json_decref(layers);
            return NULL;
        }

        if (options != NULL && options->layers != NULL) {
            if (containerv_layers_iterate(options->layers, CONTAINERV_LAYER_HOST_DIRECTORY, __mapped_dir_cb, &mctx) != 0) {
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
        }

    }

    if (json_object_set(cfg, "MappedDirectories", mapped) != 0) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    if (linux_container) {
        if (containerv_json_object_set_string(cfg, "ContainerType", "Linux") != 0) {
            json_decref(cfg);
            json_decref(mapped);
            json_decref(layers);
            return NULL;
        }
    }

    // Layers
    if (!linux_container) {
        for (int i = 0; i < parent_layer_count; i++) {
            if (parent_layers[i] == NULL || parent_layers[i][0] == '\0') {
                continue;
            }

            char idbuf[37];
            __derive_layer_id_from_path(parent_layers[i], idbuf);

            json_t* lo = json_object();
            if (lo == NULL ||
                containerv_json_object_set_string(lo, "ID", idbuf) != 0 ||
                containerv_json_object_set_string(lo, "Path", parent_layers[i]) != 0) {
                json_decref(lo);
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
            if (json_array_append_new(layers, lo) != 0) {
                json_decref(lo);
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
        }
    }

    if (json_object_set(cfg, "Layers", layers) != 0) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    // Hyper-V isolation (schema1)
    if (containerv_json_object_set_bool(cfg, "HvPartition", hv) != 0) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    if (hv && utilityvm_path != NULL && utilityvm_path[0] != '\0') {
        json_t* hvrt = json_object();
        if (hvrt == NULL || containerv_json_object_set_string(hvrt, "ImagePath", utilityvm_path) != 0) {
            json_decref(hvrt);
            json_decref(cfg);
            json_decref(mapped);
            json_decref(layers);
            return NULL;
        }

        if (linux_container) {
            const int   boot_from_vhd = __lcow_bundle_uses_vhd_boot(utilityvm_path);
            const char* kf = (options && options->windows_lcow.kernel_file) ? options->windows_lcow.kernel_file : NULL;
            const char* ir = (options && options->windows_lcow.initrd_file) ? options->windows_lcow.initrd_file : NULL;
            const char* bp = (options && options->windows_lcow.boot_parameters) ? options->windows_lcow.boot_parameters : NULL;

            if (boot_from_vhd && containerv_json_object_set_string(hvrt, "BootSource", "Vhd") != 0) {
                json_decref(hvrt);
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }

            if (kf && kf[0] && containerv_json_object_set_string(hvrt, "LinuxKernelFile", kf) != 0) {
                json_decref(hvrt);
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
            if (!boot_from_vhd && ir && ir[0] && containerv_json_object_set_string(hvrt, "LinuxInitrdFile", ir) != 0) {
                json_decref(hvrt);
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
            if (bp && bp[0] && containerv_json_object_set_string(hvrt, "LinuxBootParameters", bp) != 0) {
                json_decref(hvrt);
                json_decref(cfg);
                json_decref(mapped);
                json_decref(layers);
                return NULL;
            }
        }

        if (json_object_set_new(cfg, "HvRuntime", hvrt) != 0) {
            json_decref(hvrt);
            json_decref(cfg);
            json_decref(mapped);
            json_decref(layers);
            return NULL;
        }
    }

    if (containerv_json_object_set_bool(cfg, "TerminateOnLastHandleClosed", 1) != 0) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    char* json_utf8 = NULL;
    if (containerv_json_dumps_compact(cfg, &json_utf8) != 0) {
        json_decref(cfg);
        json_decref(mapped);
        json_decref(layers);
        return NULL;
    }

    wchar_t* w = __utf8_to_wide_alloc(json_utf8);
    free(json_utf8);
    json_decref(cfg);
    json_decref(mapped);
    json_decref(layers);
    return w;
}

static wchar_t* __hcs_create_lcow_config_schema2(
    struct containerv_container*      container,
    const struct containerv_options*  options,
    const char*                       utilityvm_path,
    const char*                       rootfs_host_path,
    const char*                       console_pipe)
{
    static const char* const scsiControllerGuids[] = {
        "df6d0690-79e5-55b6-a5ec-c1e2f77f580a",
        "0110f83b-de10-5172-a266-78bca56bf50a",
        "b5d2d8d4-3a75-51bf-945b-3444dc6b8579",
        "305891a9-b251-5dfe-91a2-c25d9212275b"
    };
    const char* kernelFile;
    const char* initrdFile;
    const char* bootParameters;
    char        devicePath[PATH_MAX];
    char        kernelArgs[4096];
    json_t*     cfg = NULL;
    json_t*     schema = NULL;
    json_t*     vm = NULL;
    json_t*     chipset = NULL;
    json_t*     uefi = NULL;
    json_t*     boot = NULL;
    json_t*     topology = NULL;
    json_t*     memory = NULL;
    json_t*     processor = NULL;
    json_t*     devices = NULL;
    json_t*     scsi = NULL;
    json_t*     hvSocket = NULL;
    json_t*     hvSocketConfig = NULL;
    json_t*     serviceTable = NULL;
    json_t*     comPorts = NULL;
    json_t*     comPort = NULL;
    json_t*     plan9 = NULL;
    json_t*     plan9Shares = NULL;
    char*       jsonUtf8 = NULL;
    wchar_t*    result = NULL;

    if (container == NULL || options == NULL || utilityvm_path == NULL || utilityvm_path[0] == '\0') {
        return NULL;
    }

    kernelFile = options->windows_lcow.kernel_file;
    initrdFile = options->windows_lcow.initrd_file;
    bootParameters = options->windows_lcow.boot_parameters;
    if (kernelFile == NULL || kernelFile[0] == '\0' || initrdFile == NULL || initrdFile[0] == '\0') {
        return NULL;
    }

    if (snprintf(devicePath, sizeof(devicePath), "\\%s", kernelFile) < 0 ||
        snprintf(
            kernelArgs,
            sizeof(kernelArgs),
            "initrd=/%s initcall_blacklist=virtio_vsock_init panic=-1 console=ttyAMA0,115200 pci=off%s%s nr_cpus=2 brd.rd_nr=0 pmtmr=0 -- /bin/gcs -v4 -log-format json -loglevel debug -disable-time-sync",
            initrdFile,
            bootParameters != NULL && bootParameters[0] != '\0' ? " " : "",
            bootParameters != NULL ? bootParameters : "") < 0) {
        return NULL;
    }

    cfg = json_object();
    schema = json_object();
    vm = json_object();
    chipset = json_object();
    uefi = json_object();
    boot = json_object();
    topology = json_object();
    memory = json_object();
    processor = json_object();
    devices = json_object();
    scsi = json_object();
    hvSocket = json_object();
    hvSocketConfig = json_object();
    serviceTable = json_object();
    comPorts = json_object();
    comPort = json_object();
    plan9 = json_object();
    plan9Shares = json_array();
    if (cfg == NULL || schema == NULL || vm == NULL || chipset == NULL || uefi == NULL || boot == NULL ||
        topology == NULL || memory == NULL || processor == NULL || devices == NULL || scsi == NULL ||
        hvSocket == NULL || hvSocketConfig == NULL || serviceTable == NULL ||
        comPorts == NULL || comPort == NULL || plan9 == NULL || plan9Shares == NULL) {
        goto cleanup;
    }

    if (containerv_json_object_set_string(cfg, "Owner", "chef") != 0 ||
        containerv_json_object_set_int(schema, "Major", 2) != 0 ||
        containerv_json_object_set_int(schema, "Minor", 1) != 0 ||
        containerv_json_object_set_bool(cfg, "ShouldTerminateOnLastHandleClosed", 1) != 0 ||
        containerv_json_object_set_bool(vm, "StopOnReset", 1) != 0 ||
        containerv_json_object_set_string(boot, "DevicePath", devicePath) != 0 ||
        containerv_json_object_set_string(boot, "DeviceType", "VmbFs") != 0 ||
        containerv_json_object_set_string(boot, "VmbFsRootPath", utilityvm_path) != 0 ||
        containerv_json_object_set_string(boot, "OptionalData", kernelArgs) != 0 ||
        containerv_json_object_set_int(memory, "SizeInMB", 1024) != 0 ||
        containerv_json_object_set_bool(memory, "AllowOvercommit", 1) != 0 ||
        containerv_json_object_set_int(processor, "Count", 2) != 0 ||
        containerv_json_object_set_string(hvSocketConfig, "DefaultBindSecurityDescriptor", "D:P(A;;FA;;;SY)(A;;FA;;;BA)") != 0 ||
        containerv_json_object_set_string(hvSocketConfig, "DefaultConnectSecurityDescriptor", "D:P(A;;FA;;;SY)(A;;FA;;;BA)") != 0 ||
        containerv_json_object_set_string(comPort, "NamedPipe", console_pipe) != 0) {
        goto cleanup;
    }

    if (rootfs_host_path != NULL && rootfs_host_path[0] != '\0') {
        char stageHost[MAX_PATH];
        char stageName[64];
        struct __lcow_plan9_share_ctx shareCtx = { .shares = plan9Shares };

        if (__lcow_plan9_share_append(plan9Shares, "0", rootfs_host_path, 0) != 0) {
            goto cleanup;
        }

        snprintf(stageHost, sizeof(stageHost), "%s\\staging", container->runtime_dir);
        __lcow_plan9_share_name(stageHost, stageName, sizeof(stageName));
        if (__lcow_plan9_share_append(plan9Shares, stageName, stageHost, 0) != 0) {
            goto cleanup;
        }

        if (options->layers != NULL &&
            containerv_layers_iterate(options->layers, CONTAINERV_LAYER_HOST_DIRECTORY, __lcow_plan9_share_layer_cb, &shareCtx) != 0) {
            goto cleanup;
        }

        if (json_object_set_new(plan9, "Shares", plan9Shares) != 0) {
            goto cleanup;
        }
        plan9Shares = NULL;
    }

    {
        static const char* const serviceIds[] = {
            "00000001-facb-11e6-bd58-64006a7986d3",
            "0000006d-facb-11e6-bd58-64006a7986d3",
            "00000234-facb-11e6-bd58-64006a7986d3",
            "40000000-facb-11e6-bd58-64006a7986d3",
            "40000001-facb-11e6-bd58-64006a7986d3"
        };
        for (size_t i = 0; i < sizeof(serviceIds) / sizeof(serviceIds[0]); i++) {
            json_t* service = json_object();
            if (service == NULL ||
                containerv_json_object_set_bool(service, "AllowWildcardBinds", 1) != 0 ||
                containerv_json_object_set_string(service, "BindSecurityDescriptor", "D:P(A;;FA;;;WD)") != 0 ||
                containerv_json_object_set_string(service, "ConnectSecurityDescriptor", "D:P(A;;FA;;;SY)(A;;FA;;;BA)") != 0 ||
                json_object_set_new(serviceTable, serviceIds[i], service) != 0) {
                json_decref(service);
                goto cleanup;
            }
        }
    }

    for (size_t i = 0; i < sizeof(scsiControllerGuids) / sizeof(scsiControllerGuids[0]); i++) {
        json_t* controller = json_object();
        json_t* attachments = json_object();
        if (controller == NULL || attachments == NULL) {
            json_decref(attachments);
            json_decref(controller);
            goto cleanup;
        }
        if (json_object_set_new(controller, "Attachments", attachments) != 0) {
            json_decref(controller);
            goto cleanup;
        }
        attachments = NULL;
        if (json_object_set_new(scsi, scsiControllerGuids[i], controller) != 0) {
            goto cleanup;
        }
        controller = NULL;
    }

#define ATTACH_OBJECT(parent, key, child) \
    do { \
        if (json_object_set_new((parent), (key), (child)) != 0) { \
            goto cleanup; \
        } \
        (child) = NULL; \
    } while (0)

    ATTACH_OBJECT(uefi, "BootThis", boot);
    ATTACH_OBJECT(chipset, "Uefi", uefi);
    ATTACH_OBJECT(topology, "Memory", memory);
    ATTACH_OBJECT(topology, "Processor", processor);
    ATTACH_OBJECT(hvSocketConfig, "ServiceTable", serviceTable);
    ATTACH_OBJECT(hvSocket, "HvSocketConfig", hvSocketConfig);
    ATTACH_OBJECT(devices, "Scsi", scsi);
    ATTACH_OBJECT(devices, "HvSocket", hvSocket);
    ATTACH_OBJECT(comPorts, "0", comPort);
    ATTACH_OBJECT(devices, "ComPorts", comPorts);
    ATTACH_OBJECT(devices, "Plan9", plan9);
    ATTACH_OBJECT(vm, "Chipset", chipset);
    ATTACH_OBJECT(vm, "ComputeTopology", topology);
    ATTACH_OBJECT(vm, "Devices", devices);
    ATTACH_OBJECT(cfg, "SchemaVersion", schema);
    ATTACH_OBJECT(cfg, "VirtualMachine", vm);

#undef ATTACH_OBJECT

    if (containerv_json_dumps_compact(cfg, &jsonUtf8) != 0) {
        goto cleanup;
    }
    result = __utf8_to_wide_alloc(jsonUtf8);

cleanup:
    free(jsonUtf8);
    json_decref(cfg);
    json_decref(schema);
    json_decref(vm);
    json_decref(chipset);
    json_decref(uefi);
    json_decref(boot);
    json_decref(topology);
    json_decref(memory);
    json_decref(processor);
    json_decref(devices);
    json_decref(scsi);
    json_decref(hvSocket);
    json_decref(hvSocketConfig);
    json_decref(serviceTable);
    json_decref(comPorts);
    json_decref(comPort);
    json_decref(plan9);
    json_decref(plan9Shares);
    return result;
}

static int __grant_lcow_boot_files_access(
    const struct containerv_options* options,
    const char*                      utilityvm_path)
{
    const char* files[2];

    if (options == NULL || utilityvm_path == NULL || utilityvm_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    if (windows_grant_vm_group_access(utilityvm_path) != 0) {
        VLOG_ERROR("containerv[hcs]", "failed to grant VM group access to LCOW bundle %s\n", utilityvm_path);
        return -1;
    }

    files[0] = options->windows_lcow.kernel_file;
    files[1] = options->windows_lcow.initrd_file;
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        char* path;

        if (files[i] == NULL || files[i][0] == '\0') {
            continue;
        }

        path = strpathcombine(utilityvm_path, files[i]);
        if (path == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (windows_grant_vm_group_access(path) != 0) {
            VLOG_ERROR("containerv[hcs]", "failed to grant VM group access to LCOW boot file %s\n", path);
            free(path);
            return -1;
        }
        free(path);
    }
    return 0;
}

int __hcs_create_container_system(
    struct containerv_container* container,
    struct containerv_options* options,
    const char* layer_folder_path,
    const char* const* parent_layers,
    int parent_layer_count,
    const char* utilityvm_path,
    int linux_container)
{
    HCS_OPERATION operation = NULL;
    wchar_t* config = NULL;
    char consolePipe[MAX_PATH] = { 0 };
    HRESULT hr;
    int status = -1;

    if (!container || !container->vm_id) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating HCS container compute system for %s\n", container->id);

    if (__hcs_initialize() != 0) {
        return -1;
    }

    if (linux_container && __grant_lcow_boot_files_access(options, utilityvm_path) != 0) {
        return -1;
    }

    if (linux_container && __lcow_console_start(container, consolePipe, sizeof(consolePipe)) != 0) {
        return -1;
    }

    if (linux_container) {
        VLOG_DEBUG("containerv[hcs]", "building schema2 LCOW UEFI/VmbFs configuration\n");
        config = __hcs_create_lcow_config_schema2(container, options, utilityvm_path, layer_folder_path, consolePipe);
    } else {
        config = __hcs_create_container_config_schema1(
            container,
            options,
            layer_folder_path,
            parent_layers,
            parent_layer_count,
            utilityvm_path,
            0);
    }
    if (config == NULL) {
        VLOG_ERROR("containerv[hcs]", "failed to create container configuration\n");
        return -1;
    }

    if (linux_container) {
        char* configUtf8 = __wide_to_utf8_alloc(config);
        if (configUtf8 != NULL) {
            char configPath[MAX_PATH];
            DWORD tempLength = GetTempPathA(sizeof(configPath), configPath);
            if (tempLength > 0 && tempLength < sizeof(configPath)) {
                snprintf(configPath + tempLength, sizeof(configPath) - tempLength, "chef-hcs-%s.json", container->id);
                if (platform_writetextfile(configPath, configUtf8) == 0) {
                    VLOG_DEBUG("containerv[hcs]", "container configuration written to %s\n", configPath);
                }
            }
            free(configUtf8);
        }
    }

    operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
    if (operation == NULL) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation\n");
        goto cleanup;
    }

    VLOG_DEBUG("containerv[hcs]", "calling HcsCreateComputeSystem\n");
    hr = g_hcs.HcsCreateComputeSystem(
        container->vm_id,
        config,
        operation,
        NULL,
        &container->hcs_system);
    VLOG_DEBUG("containerv[hcs]", "HcsCreateComputeSystem returned 0x%lx\n", hr);
    if (FAILED(hr)) {
        char* configUtf8 = __wide_to_utf8_alloc(config);
        VLOG_ERROR("containerv[hcs]", "failed to create container compute system: 0x%lx\n", hr);
        if (configUtf8 != NULL) {
            char configPath[MAX_PATH];
            DWORD tempLength = GetTempPathA(sizeof(configPath), configPath);
            if (tempLength > 0 && tempLength < sizeof(configPath)) {
                snprintf(configPath + tempLength, sizeof(configPath) - tempLength, "chef-hcs-%s.json", container->id);
                if (platform_writetextfile(configPath, configUtf8) == 0) {
                    VLOG_ERROR("containerv[hcs]", "container configuration written to %s\n", configPath);
                }
            }
            free(configUtf8);
        }
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        VLOG_DEBUG("containerv[hcs]", "waiting for HCS container create operation\n");
        hr = g_hcs.HcsWaitForOperationResult(operation, HCS_OPERATION_TIMEOUT_MS, &resultDoc);
        if (FAILED(hr)) {
            char* resultUtf8 = __wide_to_utf8_alloc(resultDoc);
            VLOG_ERROR("containerv[hcs]", "container create wait failed: 0x%lx\n", hr);
            if (resultUtf8 != NULL) {
                VLOG_ERROR("containerv[hcs]", "container create result: %s\n", resultUtf8);
                free(resultUtf8);
            }
            __hcs_localfree_wstr(resultDoc);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
        __hcs_localfree_wstr(resultDoc);
    }

    if (linux_container && __lcow_gcs_bridge_listen(container) != 0) {
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    g_hcs.HcsCloseOperation(operation);
    operation = NULL;
    operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
    if (operation == NULL) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation for start\n");
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    hr = g_hcs.HcsStartComputeSystem(container->hcs_system, operation, NULL);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to start container compute system: 0x%lx\n", hr);
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        VLOG_DEBUG("containerv[hcs]", "waiting for HCS container start operation\n");
        hr = g_hcs.HcsWaitForOperationResult(operation, HCS_OPERATION_TIMEOUT_MS, &resultDoc);
        if (FAILED(hr)) {
            char* resultUtf8 = __wide_to_utf8_alloc(resultDoc);
            VLOG_ERROR("containerv[hcs]", "container start wait failed: 0x%lx\n", hr);
            if (resultUtf8 != NULL) {
                VLOG_ERROR("containerv[hcs]", "container start result: %s\n", resultUtf8);
                free(resultUtf8);
            }
            __hcs_localfree_wstr(resultDoc);
            g_hcs.HcsCloseComputeSystem(container->hcs_system);
            container->hcs_system = NULL;
            goto cleanup;
        }
        __hcs_localfree_wstr(resultDoc);
    }

    if (linux_container &&
        (__lcow_gcs_bridge_accept(container) != 0 || __lcow_gcs_bridge_negotiate(container) != 0)) {
        g_hcs.HcsCloseComputeSystem(container->hcs_system);
        container->hcs_system = NULL;
        goto cleanup;
    }

    container->vm_started = 1;
    status = 0;

cleanup:
    if (config) {
        free(config);
    }
    if (operation) {
        g_hcs.HcsCloseOperation(operation);
    }
    return status;
}

// HCS operation callback (stub for now)
static void CALLBACK __hcs_operation_callback(HCS_OPERATION operation, void* context)
{
    (void)operation;
    (void)context;
}

static int __hcs_modify_compute_system(struct containerv_container* container, const char* settings_json_utf8)
{
    HCS_OPERATION operation = NULL;
    wchar_t*      settings_w = NULL;
    HRESULT       hr;
    int           status = -1;

    if (container == NULL || container->hcs_system == NULL || settings_json_utf8 == NULL) {
        return -1;
    }

    if (g_hcs.HcsModifyComputeSystem == NULL) {
        VLOG_ERROR("containerv[hcs]", "HcsModifyComputeSystem not available\n");
        return -1;
    }

    settings_w = __utf8_to_wide_alloc(settings_json_utf8);
    if (settings_w == NULL) {
        return -1;
    }

    operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
    if (operation == NULL) {
        VLOG_ERROR("containerv[hcs]", "failed to create HCS operation\n");
        goto cleanup;
    }

    VLOG_DEBUG("containerv[hcs]", "modify compute system request: %s\n", settings_json_utf8);

    hr = g_hcs.HcsModifyComputeSystem(container->hcs_system, operation, settings_w);
    if (FAILED(hr)) {
        VLOG_ERROR("containerv[hcs]", "failed to modify compute system: 0x%lx\n", hr);
        goto cleanup;
    }

    if (g_hcs.HcsWaitForOperationResult != NULL) {
        PWSTR resultDoc = NULL;
        hr = g_hcs.HcsWaitForOperationResult(operation, HCS_OPERATION_TIMEOUT_MS, &resultDoc);
        if (FAILED(hr)) {
            char* detail = __wide_to_utf8_alloc(resultDoc);
            VLOG_ERROR("containerv[hcs]", "modify compute system wait failed: 0x%lx (%s)\n", hr, detail ? detail : "no details");
            free(detail);
            __hcs_localfree_wstr(resultDoc);
            goto cleanup;
        }
        __hcs_localfree_wstr(resultDoc);
    }

    status = 0;

cleanup:
    if (operation != NULL) {
        g_hcs.HcsCloseOperation(operation);
    }
    free(settings_w);
    return status;
}

int __hcs_plan9_share_add(
    struct containerv_container* container,
    const char*                  name,
    const char*                  host_path,
    const char*                  guest_path,
    int                          readonly)
{
    json_t* root = NULL;
    json_t* settings = NULL;
    json_t* guestRequest = NULL;
    json_t* guestSettings = NULL;
    char*   json_utf8 = NULL;
    int     status = -1;

    if (container == NULL || name == NULL || host_path == NULL || guest_path == NULL || guest_path[0] == '\0') {
        return -1;
    }

    root = json_object();
    settings = json_object();
    guestRequest = json_object();
    guestSettings = json_object();
    if (root == NULL || settings == NULL || guestRequest == NULL || guestSettings == NULL) {
        goto cleanup;
    }

    if (containerv_json_object_set_string(root, "ResourcePath", "VirtualMachine/Devices/Plan9/Shares") != 0 ||
        containerv_json_object_set_string(root, "RequestType", "Add") != 0 ||
        containerv_json_object_set_string(settings, "Name", name) != 0 ||
        containerv_json_object_set_string(settings, "AccessName", name) != 0 ||
        containerv_json_object_set_string(settings, "Path", host_path) != 0 ||
        containerv_json_object_set_int(settings, "Port", 564) != 0 ||
        containerv_json_object_set_int(settings, "Flags", 4 | (readonly ? 1 : 0)) != 0) {
        goto cleanup;
    }

    if (json_object_set_new(root, "Settings", settings) != 0) {
        settings = NULL;
        goto cleanup;
    }
    settings = NULL;

    if (containerv_json_dumps_compact(root, &json_utf8) != 0) {
        goto cleanup;
    }

    if (__hcs_modify_compute_system(container, json_utf8) != 0) {
        VLOG_ERROR("containerv[hcs]", "failed to add Plan9 host share %s (%s)\n", name, host_path);
        goto cleanup;
    }

    free(json_utf8);
    json_utf8 = NULL;
    json_decref(root);
    root = json_object();
    if (root == NULL) {
        goto cleanup;
    }

    if (containerv_json_object_set_string(guestRequest, "ResourceType", "MappedDirectory") != 0 ||
        containerv_json_object_set_string(guestRequest, "RequestType", "Add") != 0 ||
        containerv_json_object_set_string(guestSettings, "MountPath", guest_path) != 0 ||
        containerv_json_object_set_int(guestSettings, "Port", 564) != 0 ||
        containerv_json_object_set_string(guestSettings, "ShareName", name) != 0 ||
        containerv_json_object_set_bool(guestSettings, "ReadOnly", readonly != 0) != 0) {
        goto cleanup;
    }

    if (json_object_set_new(guestRequest, "Settings", guestSettings) != 0) {
        guestSettings = NULL;
        goto cleanup;
    }
    guestSettings = NULL;

    if (json_object_set_new(root, "GuestRequest", guestRequest) != 0) {
        guestRequest = NULL;
        goto cleanup;
    }
    guestRequest = NULL;

    if (containerv_json_dumps_compact(root, &json_utf8) != 0) {
        goto cleanup;
    }

    status = __lcow_gcs_modify(container, json_utf8);
    if (status != 0) {
        VLOG_ERROR("containerv[hcs]", "failed to mount Plan9 share %s at %s\n", name, guest_path);
    }

cleanup:
    json_decref(guestSettings);
    json_decref(guestRequest);
    json_decref(settings);
    json_decref(root);
    free(json_utf8);
    return status;
}

int __hcs_plan9_mapped_dir_add(
    struct containerv_container* container,
    const char*                  name,
    const char*                  guest_path,
    int                          readonly)
{
    json_t* root = NULL;
    json_t* guestSettings = NULL;
    char*   json_utf8 = NULL;
    int     status = -1;

    if (container == NULL || name == NULL || guest_path == NULL || guest_path[0] == '\0') {
        return -1;
    }

    root = json_object();
    guestSettings = json_object();
    if (root == NULL || guestSettings == NULL) {
        goto cleanup;
    }

    if (containerv_json_object_set_string(root, "ResourceType", "MappedDirectory") != 0 ||
        containerv_json_object_set_string(root, "RequestType", "Add") != 0 ||
        containerv_json_object_set_string(guestSettings, "MountPath", guest_path) != 0 ||
        containerv_json_object_set_int(guestSettings, "Port", 564) != 0 ||
        containerv_json_object_set_string(guestSettings, "ShareName", name) != 0 ||
        containerv_json_object_set_bool(guestSettings, "ReadOnly", readonly != 0) != 0) {
        goto cleanup;
    }

    if (json_object_set_new(root, "Settings", guestSettings) != 0) {
        guestSettings = NULL;
        goto cleanup;
    }
    guestSettings = NULL;

    if (containerv_json_dumps_compact(root, &json_utf8) != 0) {
        goto cleanup;
    }

    status = __lcow_gcs_modify(container, json_utf8);
    if (status != 0) {
        VLOG_ERROR("containerv[hcs]", "failed to mount Plan9 share %s at %s\n", name, guest_path);
    }

cleanup:
    json_decref(guestSettings);
    json_decref(root);
    free(json_utf8);
    return status;
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

    // Load ComputeCore.dll for operation/wait helpers (Win10 1809+).
    // Some installations export several HCS functions from computecore.dll
    // rather than vmcompute.dll, so resolve from either module below.
    g_hcs.hComputeCore = LoadLibraryA("computecore.dll");

    // Load HCS function pointers
    g_hcs.HcsOpenComputeSystem = (HcsOpenComputeSystem_t)
        __get_proc_any("HcsOpenComputeSystem");
    g_hcs.HcsCreateComputeSystem = (HcsCreateComputeSystem_t)
        __get_proc_any("HcsCreateComputeSystem");
    g_hcs.HcsStartComputeSystem = (HcsStartComputeSystem_t)
        __get_proc_any("HcsStartComputeSystem");
    g_hcs.HcsShutdownComputeSystem = (HcsShutdownComputeSystem_t)
        __get_proc_any("HcsShutdownComputeSystem");
    g_hcs.HcsTerminateComputeSystem = (HcsTerminateComputeSystem_t)
        __get_proc_any("HcsTerminateComputeSystem");
    g_hcs.HcsCreateProcess = (HcsCreateProcess_t)
        __get_proc_any("HcsCreateProcess");
    g_hcs.HcsModifyComputeSystem = (HcsModifyComputeSystem_t)
        __get_proc_any("HcsModifyComputeSystem");
    g_hcs.HcsGetComputeSystemProperties = (HcsGetComputeSystemProperties_t)
        __get_proc_any("HcsGetComputeSystemProperties");
    g_hcs.HcsCreateOperation = (HcsCreateOperation_t)
        __get_proc_any("HcsCreateOperation");
    g_hcs.HcsCloseOperation = (HcsCloseOperation_t)
        __get_proc_any("HcsCloseOperation");
    g_hcs.HcsCloseComputeSystem = (HcsCloseComputeSystem_t)
        __get_proc_any("HcsCloseComputeSystem");
    g_hcs.HcsCloseProcess = (HcsCloseProcess_t)
        __get_proc_any("HcsCloseProcess");

    g_hcs.HcsWaitForOperationResult = (HcsWaitForOperationResult_t)__get_proc_any("HcsWaitForOperationResult");
    g_hcs.HcsWaitForOperationResultAndProcessInfo = (HcsWaitForOperationResultAndProcessInfo_t)__get_proc_any("HcsWaitForOperationResultAndProcessInfo");

    // Check that we got all required functions
    if (!g_hcs.HcsOpenComputeSystem || !g_hcs.HcsCreateComputeSystem || !g_hcs.HcsStartComputeSystem ||
        !g_hcs.HcsShutdownComputeSystem || !g_hcs.HcsTerminateComputeSystem ||
        !g_hcs.HcsCreateProcess || !g_hcs.HcsModifyComputeSystem || !g_hcs.HcsGetComputeSystemProperties || !g_hcs.HcsCreateOperation ||
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

int __hcs_destroy_compute_system(struct containerv_container* container)
{
    HCS_OPERATION operation = NULL;
    HRESULT hr;
    int status = 0;

    if (!container || !container->hcs_system) {
        return 0;  // Nothing to destroy
    }

    VLOG_DEBUG("containerv[hcs]", "destroying HCS compute system for container %s\n", container->id);

    if (container->vm_started) {
        // Try graceful shutdown first
        operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
        if (operation == NULL) {
            VLOG_WARNING("containerv[hcs]", "failed to create operation for shutdown\n");
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

            operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
            if (operation == NULL) {
                VLOG_WARNING("containerv[hcs]", "failed to create operation for terminate\n");
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

    __lcow_gcs_bridge_close(container);

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

    VLOG_DEBUG("containerv[hcs]", "destroyed compute system for container %s\n", container->id);
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

    // Linux guests: argv array in JSON form (compact string) for OCI builder.
    char* args_json_utf8 = NULL;

    // LCOW: optional OCI spec JSON (raw) used by Linux GCS.
    char* oci_spec_utf8 = NULL;

    // Command representations.
    char* cmdline_utf8 = NULL;
    char* linux_cmdline_utf8 = NULL;
    json_t* cmd_args = NULL;

    // Environment object for the guest.
    json_t* env_obj = NULL;

    if (!container || !container->hcs_system || !options || !options->path) {
        return -1;
    }

    VLOG_DEBUG("containerv[hcs]", "creating process in compute system: %s\n", options->path);

    const int guest_is_windows = (container->guest_is_windows != 0);

    // Normalize paths for Linux guests (LCOW).
    // OCI-in-UVM uses /chef/rootfs as root.path, so process args must remain
    // relative to that root (for example /bin/sh, not /chef/rootfs/bin/sh).
    struct __containerv_spawn_options norm_opts = *options;

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

    // Build command representation.
    // - Windows GCS uses CommandLine (string).
    // - Linux GCS supports CommandArgs (array) and treats it as an alternative to CommandLine.
    if (guest_is_windows) {
        if (__build_windows_cmdline(options, &cmdline_utf8) != 0) {
            goto cleanup;
        }
    } else {
        if (__build_linux_cmdline_and_args(&norm_opts, &linux_cmdline_utf8, &cmd_args, &args_json_utf8) != 0) {
            goto cleanup;
        }
    }

    // If we're running a Linux container compute system (LCOW), prefer OCISpecification.
    // Rootfs is expected to be mapped into the container at /chef/rootfs.
    if (__build_oci_spec_if_needed(container, &norm_opts, args_json_utf8, guest_is_windows, &oci_spec_utf8) != 0) {
        goto cleanup;
    }

    if (!guest_is_windows) {
        struct containerv_lcow_gcs_process* gcsProcess;
        char                                gcsContainerId[__CONTAINER_ID_LENGTH + 1];
        uint32_t                            gcsPid;

        if (options->create_stdio_pipes) {
            VLOG_ERROR("containerv[hcs]", "LCOW direct GCS stdio pipes are not implemented yet\n");
            errno = ENOSYS;
            goto cleanup;
        }

        platform_guid_new_string(gcsContainerId);
        VLOG_DEBUG("containerv[gcs]", "creating LCOW hosted container %s for process %s\n", gcsContainerId, options->path);
        if (__lcow_gcs_create_container(container, gcsContainerId, oci_spec_utf8) != 0 ||
            __lcow_gcs_execute_container_process(container, gcsContainerId, &gcsPid) != 0) {
            goto cleanup;
        }

        gcsProcess = calloc(1, sizeof(*gcsProcess));
        if (gcsProcess == NULL) {
            errno = ENOMEM;
            goto cleanup;
        }
        gcsProcess->magic = CONTAINERV_LCOW_GCS_PROCESS_MAGIC;
        gcsProcess->pid = gcsPid;
        memcpy(gcsProcess->container_id, gcsContainerId, sizeof(gcsProcess->container_id));

        if (processOut != NULL) {
            *processOut = (HCS_PROCESS)gcsProcess;
        } else {
            free(gcsProcess);
        }
        if (processInfoOut != NULL) {
            memset(processInfoOut, 0, sizeof(*processInfoOut));
            processInfoOut->ProcessId = gcsPid;
        }
        status = 0;
        goto cleanup;
    }

    // Build environment object. Always include a default PATH if not provided.
    int try_security_fields = 0;
    if (__build_env_object(
        &norm_opts,
        guest_is_windows,
        policy,
        security_level,
        win_use_app_container,
        win_integrity_level,
        win_capability_sids,
        win_capability_sid_count,
        &env_obj,
        &try_security_fields) != 0) {
        goto cleanup;
    }

    int lcow_fallback = 0;
    for (int attempt = 0; attempt < 3; attempt++) {
        int include_security = (attempt == 0) && try_security_fields;

        // (Re)create operation handle
        if (operation != NULL) {
            g_hcs.HcsCloseOperation(operation);
            operation = NULL;
        }
        operation = g_hcs.HcsCreateOperation(NULL, __hcs_operation_callback);
        if (operation == NULL) {
            VLOG_ERROR("containerv[hcs]", "failed to create HCS operation\n");
            goto cleanup;
        }

        free(json_utf8);
        json_utf8 = NULL;

        const int lcow_use_oci = (!guest_is_windows && oci_spec_utf8 != NULL && !lcow_fallback);
        const int lcow_use_command_args = (!guest_is_windows && !lcow_fallback);
        const int create_in_uvm = (!guest_is_windows && !lcow_use_oci) ? 1 : 0;

        if (__build_process_config_json(
            guest_is_windows,
            &norm_opts,
            env_obj,
            cmdline_utf8,
            cmd_args,
            linux_cmdline_utf8,
            oci_spec_utf8,
            include_security,
            lcow_use_oci,
            lcow_use_command_args,
            create_in_uvm,
            &json_utf8) != 0) {
            goto cleanup;
        }

        if (process_config) {
            free(process_config);
            process_config = NULL;
        }

        process_config = __utf8_to_wide_alloc(json_utf8);
        if (process_config == NULL) {
            VLOG_ERROR("containerv[hcs]", "failed to convert process config to wide string\n");
            goto cleanup;
        }

        VLOG_DEBUG("containerv[hcs]", "process configuration: %s\n", json_utf8);

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

                if (!guest_is_windows && oci_spec_utf8 != NULL && !lcow_fallback) {
                    VLOG_WARNING("containerv[hcs]", "LCOW process create rejected OCISpecification (hr=0x%lx); retrying without it\n", whr);
                    lcow_fallback = 1;
                    continue;
                }

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

cleanup:
    free(json_utf8);
    free(args_json_utf8);
    free(oci_spec_utf8);
    free(cmdline_utf8);
    free(linux_cmdline_utf8);
    if (cmd_args) {
        json_decref(cmd_args);
    }
    if (env_obj) {
        json_decref(env_obj);
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
