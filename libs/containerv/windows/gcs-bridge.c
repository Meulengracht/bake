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

// LCOW GCS transport: HvSocket setup, framed RPC and protocol negotiation.
// Wire format and message IDs follow hcsshim internal/gcs/prot/protocol.go.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>
#include <jansson.h>

#include "json-util.h"
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
#define HCS_OPERATION_TIMEOUT_MS 120000

struct chef_sockaddr_hv {
    ADDRESS_FAMILY Family;
    USHORT         Reserved;
    GUID           VmId;
    GUID           ServiceId;
};

static void __gcs_u32le(unsigned char* p, uint32_t value)
{
    p[0] = (unsigned char)(value & 0xffu);
    p[1] = (unsigned char)((value >> 8) & 0xffu);
    p[2] = (unsigned char)((value >> 16) & 0xffu);
    p[3] = (unsigned char)((value >> 24) & 0xffu);
}

static void __gcs_u64le(unsigned char* p, uint64_t value)
{
    for (int i = 0; i < 8; i++) {
        p[i] = (unsigned char)((value >> (i * 8)) & 0xffu);
    }
}

static uint32_t __gcs_read_u32le(const unsigned char* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t __gcs_read_u64le(const unsigned char* p)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= ((uint64_t)p[i]) << (i * 8);
    }
    return value;
}

static int __gcs_send_all(SOCKET socket, const unsigned char* data, size_t length)
{
    while (length > 0) {
        int chunk = (length > INT_MAX) ? INT_MAX : (int)length;
        int sent = send(socket, (const char*)data, chunk, 0);
        if (sent <= 0) {
            errno = EIO;
            return -1;
        }
        data += sent;
        length -= (size_t)sent;
    }
    return 0;
}

static int __gcs_recv_all(SOCKET socket, unsigned char* data, size_t length)
{
    while (length > 0) {
        int chunk = (length > INT_MAX) ? INT_MAX : (int)length;
        int received = recv(socket, (char*)data, chunk, 0);
        if (received <= 0) {
            errno = EIO;
            return -1;
        }
        data += received;
        length -= (size_t)received;
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

int __lcow_gcs_bridge_listen(struct containerv_container* container)
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

int __lcow_gcs_bridge_accept(struct containerv_container* container)
{
    SOCKET    listener;
    ULONGLONG deadline;

    if (container == NULL || container->lcow_gcs_listener == (uintptr_t)INVALID_SOCKET) {
        errno = EINVAL;
        return -1;
    }

    listener = (SOCKET)container->lcow_gcs_listener;
    deadline = GetTickCount64() + HCS_OPERATION_TIMEOUT_MS;
    while (GetTickCount64() < deadline) {
        SOCKET client = accept(listener, NULL, NULL);
        if (client != INVALID_SOCKET) {
            u_long blocking = 0;
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

int __lcow_gcs_rpc(struct containerv_container* container, uint32_t rpc, const char* request_json, char** response_json_out)
{
    SOCKET         socket;
    uint64_t       id;
    size_t         payload_length;
    size_t         total_length;
    unsigned char* request = NULL;
    unsigned char  header[GCS_HDR_SIZE];
    unsigned char* payload = NULL;
    uint32_t       response_type;
    uint32_t       response_size;
    uint64_t       response_id;
    json_t*        response = NULL;
    json_error_t   error;
    json_t*        result_value;
    int            result;
    int            status = -1;

    if (response_json_out != NULL) {
        *response_json_out = NULL;
    }
    if (container == NULL || container->lcow_gcs_socket == (uintptr_t)INVALID_SOCKET || request_json == NULL) {
        errno = EINVAL;
        return -1;
    }

    socket = (SOCKET)container->lcow_gcs_socket;
    id = container->lcow_gcs_next_id++;
    payload_length = strlen(request_json) + 1;
    total_length = GCS_HDR_SIZE + payload_length;
    if (total_length > 0x10000u) {
        errno = EMSGSIZE;
        return -1;
    }

    request = calloc(total_length, 1);
    if (request == NULL) {
        errno = ENOMEM;
        return -1;
    }
    __gcs_u32le(request, rpc | GCS_MSG_REQUEST);
    __gcs_u32le(request + 4, (uint32_t)total_length);
    __gcs_u64le(request + 8, id);
    memcpy(request + GCS_HDR_SIZE, request_json, payload_length - 1);
    request[total_length - 1] = '\n';

    VLOG_DEBUG("containerv[gcs]", "GCS RPC request type=0x%08x id=%llu payload=%s\n",
        rpc | GCS_MSG_REQUEST, (unsigned long long)id, request_json);
    if (__gcs_send_all(socket, request, total_length) != 0) {
        goto cleanup;
    }

    for (;;) {
        free(payload);
        payload = NULL;
        if (__gcs_recv_all(socket, header, sizeof(header)) != 0) {
            goto cleanup;
        }

        response_type = __gcs_read_u32le(header);
        response_size = __gcs_read_u32le(header + 4);
        response_id = __gcs_read_u64le(header + 8);
        if (response_size < GCS_HDR_SIZE || response_size > 0x10000u) {
            errno = EPROTO;
            goto cleanup;
        }

        payload_length = response_size - GCS_HDR_SIZE;
        payload = calloc(payload_length + 1, 1);
        if (payload == NULL) {
            errno = ENOMEM;
            goto cleanup;
        }
        if (__gcs_recv_all(socket, payload, payload_length) != 0) {
            goto cleanup;
        }

        VLOG_DEBUG("containerv[gcs]", "GCS RPC frame type=0x%08x id=%llu payload=%s\n",
            response_type, (unsigned long long)response_id, payload);
        if ((response_type & GCS_MSG_RESPONSE) != 0 && response_id == id &&
            (response_type & ~GCS_MSG_RESPONSE) == rpc) {
            break;
        }
    }

    memset(&error, 0, sizeof(error));
    response = json_loads((const char*)payload, 0, &error);
    if (response == NULL) {
        errno = EPROTO;
        goto cleanup;
    }
    result_value = json_object_get(response, "Result");
    result = result_value != NULL ? (int)json_integer_value(result_value) : 0;
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
    free(request);
    return status;
}

int __lcow_gcs_modify_settings(struct containerv_container* container, const char* modification_json)
{
    json_t* modification = NULL;
    json_t* request = NULL;
    char*   request_json = NULL;
    int     status = -1;

    if (container == NULL || modification_json == NULL) {
        errno = EINVAL;
        return -1;
    }

    modification = json_loads(modification_json, 0, NULL);
    request = json_object();
    if (modification == NULL || request == NULL ||
        containerv_json_object_set_string(request, "ContainerId", "00000000-0000-0000-0000-000000000000") != 0 ||
        containerv_json_object_set_string(request, "ActivityId", "00000000-0000-0000-0000-000000000000") != 0 ||
        json_object_set(request, "Request", modification) != 0 ||
        containerv_json_dumps_compact(request, &request_json) != 0) {
        errno = EINVAL;
        goto cleanup;
    }

    status = __lcow_gcs_rpc(container, CONTAINERV_LCOW_GCS_RPC_MODIFY_SETTINGS, request_json, NULL);

cleanup:
    json_decref(modification);
    json_decref(request);
    free(request_json);
    return status;
}

int __lcow_gcs_bridge_negotiate(struct containerv_container* container)
{
    char request[512];

    snprintf(request, sizeof(request),
        "{\"ContainerId\":\"%s\",\"ActivityId\":\"00000000-0000-0000-0000-000000000000\",\"MinimumVersion\":4,\"MaximumVersion\":4}",
        container->id);
    return __lcow_gcs_rpc(container, CONTAINERV_LCOW_GCS_RPC_NEGOTIATE_PROTOCOL, request, NULL);
}

void __lcow_gcs_bridge_close(struct containerv_container* container)
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
