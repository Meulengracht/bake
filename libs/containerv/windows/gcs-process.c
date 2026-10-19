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

// LCOW GCS operations for hosted containers, guest processes and utility-VM commands.

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <vlog.h>
#include <jansson.h>

#include "json-util.h"
#include "private.h"
#include "lcow-network-script.h"

static int __gcs_set_request_base(json_t* request, const char* container_id)
{
    return containerv_json_object_set_string(request, "ContainerId", container_id) != 0 ||
        containerv_json_object_set_string(request, "ActivityId", "00000000-0000-0000-0000-000000000000") != 0;
}

int __lcow_gcs_create_container(struct containerv_container* container, const char* container_id, const char* oci_spec_utf8)
{
    json_t* req = NULL;
    json_t* hosted = NULL;
    json_t* schema = NULL;
    json_t* oci = NULL;
    char*   hosted_json = NULL;
    char*   req_json = NULL;
    char    bundle_path[128];
    char    scratch_path[160];
    json_error_t error;
    int     status = -1;

    if (container == NULL || container_id == NULL || oci_spec_utf8 == NULL) {
        errno = EINVAL;
        return -1;
    }

    memset(&error, 0, sizeof(error));
    oci = json_loads(oci_spec_utf8, 0, &error);
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

    if (__gcs_set_request_base(req, container_id) != 0 ||
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

    status = __lcow_gcs_rpc(container, CONTAINERV_LCOW_GCS_RPC_CREATE, req_json, NULL);

cleanup:
    json_decref(req);
    json_decref(hosted);
    json_decref(schema);
    json_decref(oci);
    free(hosted_json);
    free(req_json);
    return status;
}

int __lcow_gcs_execute_container_process(struct containerv_container* container, const char* container_id, uint32_t* pid_out)
{
    json_t* req = NULL;
    json_t* settings = NULL;
    json_t* params = NULL;
    char*   params_json = NULL;
    char*   req_json = NULL;
    char*   response_json = NULL;
    json_t* response = NULL;
    json_error_t error;
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

    if (__gcs_set_request_base(req, container_id) != 0 ||
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
        __lcow_gcs_rpc(container, CONTAINERV_LCOW_GCS_RPC_EXECUTE_PROCESS, req_json, &response_json) != 0) {
        goto cleanup;
    }

    memset(&error, 0, sizeof(error));
    response = json_loads(response_json, 0, &error);
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

int __lcow_gcs_execute_host_process(
    struct containerv_container* container,
    const char* const*           argv,
    uint32_t*                    pid_out)
{
    static const char* const uvm_id = "00000000-0000-0000-0000-000000000000";
    json_t* req = NULL;
    json_t* settings = NULL;
    json_t* params = NULL;
    json_t* args = NULL;
    json_t* response = NULL;
    json_t* pid = NULL;
    char* params_json = NULL;
    char* req_json = NULL;
    char* response_json = NULL;
    json_error_t error;
    int status = -1;

    if (container == NULL || argv == NULL || argv[0] == NULL || pid_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *pid_out = 0;

    req = json_object();
    settings = json_object();
    params = json_object();
    args = json_array();
    if (req == NULL || settings == NULL || params == NULL || args == NULL) {
        goto cleanup;
    }

    for (size_t i = 0; argv[i] != NULL; ++i) {
        if (json_array_append_new(args, json_string(argv[i])) != 0) {
            goto cleanup;
        }
    }

    if (__gcs_set_request_base(req, uvm_id) != 0 ||
        json_object_set(params, "CommandArgs", args) != 0) {
        goto cleanup;
    }

    if (containerv_json_object_set_string(params, "WorkingDirectory", "/") != 0 ||
        containerv_json_object_set_bool(params, "CreateInUtilityVM", 1) != 0 ||
        containerv_json_object_set_bool(params, "CreateStdInPipe", 0) != 0 ||
        containerv_json_object_set_bool(params, "CreateStdOutPipe", 0) != 0 ||
        containerv_json_object_set_bool(params, "CreateStdErrPipe", 0) != 0 ||
        containerv_json_dumps_compact(params, &params_json) != 0 ||
        containerv_json_object_set_string(settings, "ProcessParameters", params_json) != 0 ||
        json_object_set(req, "Settings", settings) != 0) {
        goto cleanup;
    }

    if (containerv_json_dumps_compact(req, &req_json) != 0 ||
        __lcow_gcs_rpc(container, CONTAINERV_LCOW_GCS_RPC_EXECUTE_PROCESS, req_json, &response_json) != 0) {
        goto cleanup;
    }

    memset(&error, 0, sizeof(error));
    response = json_loads(response_json, 0, &error);
    pid = response != NULL ? json_object_get(response, "ProcessId") : NULL;
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
    json_decref(args);
    json_decref(response);
    free(params_json);
    free(req_json);
    free(response_json);
    return status;
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
    json_error_t error;
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

    if (__gcs_set_request_base(req, process->container_id) != 0 ||
        containerv_json_object_set_int(req, "ProcessId", (int)process->pid) != 0 ||
        json_object_set_new(req, "TimeoutInMs", json_integer(0xffffffffu)) != 0 ||
        containerv_json_dumps_compact(req, &req_json) != 0 ||
        __lcow_gcs_rpc(container, CONTAINERV_LCOW_GCS_RPC_WAIT_FOR_PROCESS, req_json, &response_json) != 0) {
        goto cleanup;
    }

    memset(&error, 0, sizeof(error));
    response = json_loads(response_json, 0, &error);
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

int __hcs_lcow_configure_network(struct containerv_container* container,
    const char* address, const char* gateway, const char* dns)
{
    static const char* const uvm_id = "00000000-0000-0000-0000-000000000000";
    const char* const argv[] = {
        "/bin/busybox",
        "sh",
        "-c",
        "exec >/dev/kmsg 2>&1; exec /bin/sh -c \"$1\" chef-lcow-network \"$2\" \"$3\" \"$4\"",
        "chef-lcow-network", containerv_lcow_network_script,
        address ? address : "", gateway ? gateway : "", dns ? dns : "",
        NULL
    };
    struct containerv_lcow_gcs_process process = {0};
    unsigned long exit_code = 0;

    if (__lcow_gcs_execute_host_process(container, argv, &process.pid) != 0) {
        VLOG_ERROR("containerv[gcs]", "failed to start LCOW network configuration\n");
        return -1;
    }

    process.magic = CONTAINERV_LCOW_GCS_PROCESS_MAGIC;
    snprintf(process.container_id, sizeof(process.container_id), "%s", uvm_id);
    if (__hcs_wait_lcow_gcs_process(container, &process, &exit_code) != 0 || exit_code != 0) {
        VLOG_ERROR("containerv[gcs]", "LCOW network configuration failed with exit code %lu\n", exit_code);
        return -1;
    }

    VLOG_DEBUG("containerv[gcs]", "LCOW network configuration completed\n");
    return 0;
}
