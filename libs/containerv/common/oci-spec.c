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
 * 
 */

#include <chef/environment.h>

#include <ctype.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "oci-spec.h"
#include "json-util.h"
#include "standard-mounts.h"

struct __oci_mount_spec {
    const char* destination;
    const char* type;
    const char* source;
    const char* const* options;
};

struct __oci_device_spec {
    const char* path;
    char        type;
    int         major;
    int         minor;
    const char* permissions;
    int         file_mode;
    int         uid;
    int         gid;
};

static const char* const __masked_paths[] = {
    "/proc/kcore",
    "/proc/latency_stats",
    "/proc/timer_list",
    "/proc/sched_debug",
    "/proc/scsi",
    "/sys/firmware",
    NULL,
};

static const char* const __readonly_paths[] = {
    "/proc/asound",
    "/proc/bus",
    "/proc/fs",
    "/proc/irq",
    "/proc/sys",
    "/proc/sysrq-trigger",
    NULL,
};

static const struct __oci_device_spec __g_devices[] = {
    {"/dev/null",   'c', 1, 3, "rwm", 0666, 0, 0},
    {"/dev/zero",   'c', 1, 5, "rwm", 0666, 0, 0},
    {"/dev/full",   'c', 1, 7, "rwm", 0666, 0, 0},
    {"/dev/random", 'c', 1, 8, "rwm", 0666, 0, 0},
    {"/dev/urandom",'c', 1, 9, "rwm", 0666, 0, 0},
    {"/dev/tty",    'c', 5, 0, "rwm", 0666, 0, 0},
    {NULL, 0, 0, 0, NULL, 0, 0, 0}
};

static const char* const __opts_proc[] = {"nosuid", "noexec", "nodev", NULL};
static const char* const __opts_sys[] = {"nosuid", "noexec", "nodev", "ro", NULL};
static const char* const __opts_dev[] = {"nosuid", "strictatime", "mode=755", "size=65536k", NULL};
static const char* const __opts_devpts[] = {"nosuid", "noexec", "newinstance", "ptmxmode=0666", "mode=0620", "gid=5", NULL};
static const char* const __opts_shm[] = {"nosuid", "noexec", "nodev", "mode=1777", "size=65536k", NULL};
static const char* const __opts_mqueue[] = {"nosuid", "noexec", "nodev", NULL};
static const char* const __opts_cgroup[] = {"nosuid", "noexec", "nodev", "relatime", NULL};

static const struct __oci_mount_spec __g_mount_specs[] = {
    {.destination = "/proc", .type = "proc", .source = "proc", .options = __opts_proc},
    {.destination = "/sys", .type = "sysfs", .source = "sysfs", .options = __opts_sys},
    {.destination = "/sys/fs/cgroup", .type = "cgroup", .source = "cgroup", .options = __opts_cgroup},
    {.destination = "/dev", .type = "tmpfs", .source = "tmpfs", .options = __opts_dev},
    {.destination = "/dev/pts", .type = "devpts", .source = "devpts", .options = __opts_devpts},
    {.destination = "/dev/shm", .type = "tmpfs", .source = "shm", .options = __opts_shm},
    {.destination = "/dev/mqueue", .type = "mqueue", .source = "mqueue", .options = __opts_mqueue},
};

static const struct __oci_mount_spec* __find_oci_mount_spec(const char* destination)
{
    if (destination == NULL) {
        return NULL;
    }
    for (size_t i = 0; i < (sizeof(__g_mount_specs) / sizeof(__g_mount_specs[0])); ++i) {
        if (strcmp(__g_mount_specs[i].destination, destination) == 0) {
            return &__g_mount_specs[i];
        }
    }
    return NULL;
}

static json_t* __oci_mount_to_json(const struct __oci_mount_spec* ms)
{
    if (ms == NULL) {
        return NULL;
    }

    json_t* obj = json_object();
    json_t* opts = json_array();
    if (obj == NULL || opts == NULL) {
        json_decref(obj);
        json_decref(opts);
        return NULL;
    }

    if (containerv_json_object_set_string(obj, "destination", ms->destination) != 0 ||
        containerv_json_object_set_string(obj, "type", ms->type) != 0 ||
        containerv_json_object_set_string(obj, "source", ms->source) != 0) {
        json_decref(obj);
        json_decref(opts);
        return NULL;
    }

    if (ms->options != NULL) {
        for (size_t i = 0; ms->options[i] != NULL; ++i) {
            if (containerv_json_array_append_string(opts, ms->options[i]) != 0) {
                json_decref(obj);
                json_decref(opts);
                return NULL;
            }
        }
    }

    if (json_object_set_new(obj, "options", opts) != 0) {
        json_decref(obj);
        json_decref(opts);
        return NULL;
    }

    return obj;
}

static json_t* __process_create_json(json_t* spec, const char* cwd)
{
    json_t* process = json_object();
    if (process == NULL) {
        return NULL;
    }

    if (containerv_json_object_set_bool(process, "terminal", 0) ||
        containerv_json_object_set_string(process, "cwd", cwd)) {
        json_decref(process);
        return NULL;
    }

    if (json_object_set(spec, "process", process) != 0) {
        json_decref(process);
        return NULL;
    }

    return process;
}

static int __build_args(json_t* process, const char* args_json)
{
    json_t* args = NULL;
    if (args_json != NULL && args_json[0] != '\0') {
        json_error_t err;
        args = json_loads(args_json, 0, &err);
        if (args != NULL && !json_is_array(args)) {
            json_decref(args);
            args = NULL;
        }
    }
    
    if (args == NULL) {
        args = json_array();
        if (args == NULL) {
            return -1;
        }
    }

    if (json_object_set(process, "args", args)) {
        json_decref(args);
        return -1;
    }
    return 0;
}

static int __build_environment(json_t* process, const char* const* envv)
{
    json_t* env = json_array();
    if (env == NULL) {
        return -1;
    }
    
    if (!environment_contains_key_insensitive(envv, "PATH")) {
        if (containerv_json_array_append_string(env, "PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin") != 0) {
            json_decref(env);
            return -1;
        }
    }
    
    if (envv != NULL) {
        for (int i = 0; envv[i] != NULL; ++i) {
            if (containerv_json_array_append_string(env, envv[i]) != 0) {
                json_decref(env);
                return -1;
            }
        }
    }

    if (json_object_set(process, "env", env) != 0) {
        json_decref(env);
        return -1;
    }
    return 0;
}

static int __configure_user(json_t* process)
{
    json_t* user = json_object();
    if (user == NULL) {
        return -1;
    }

    if (containerv_json_object_set_int(user, "uid", 0) != 0 || 
        containerv_json_object_set_int(user, "gid", 0) != 0) {
        json_decref(user);
        return -1;
    }

    if (json_object_set(process, "user", user) != 0) {
        json_decref(user);
        return -1;
    }
    return 0;
}

static int __build_root(json_t* spec, const char* path)
{
    json_t* root = json_object();
    if (root == NULL) {
        return -1;
    }

    if (containerv_json_object_set_string(root, "path", path) != 0 ||
        containerv_json_object_set_bool(root, "readonly", 0) != 0) {
        json_decref(root);
        return -1;
    }

    if (json_object_set(spec, "root", root) != 0) {
        json_decref(root);
        return -1;
    }
    return 0;
}

static int __build_mounts(json_t* spec)
{
    json_t* mounts = json_array();
    if (mounts == NULL) {
        return -1;
    }
    
    for (const char* const* mp = containerv_standard_linux_mountpoints(); mp != NULL && *mp != NULL; ++mp) {
        const struct __oci_mount_spec* ms = __find_oci_mount_spec(*mp);
        json_t*                        mobj;
        if (ms == NULL) {
            continue;
        }
        
        mobj = __oci_mount_to_json(ms);
        if (mobj == NULL || json_array_append_new(mounts, mobj) != 0) {
            json_decref(mobj);
            json_decref(mounts);
            return -1;
        }
    }
    
    if (json_object_set(spec, "mounts", mounts) != 0) {
        json_decref(mounts);
        return -1;
    }
    return 0;
}

static int __append_bind_mount(json_t* mounts, const char* source, const char* destination, int readonly)
{
    json_t* obj = NULL;
    json_t* opts = NULL;

    if (mounts == NULL || source == NULL || destination == NULL) {
        return -1;
    }

    obj = json_object();
    opts = json_array();
    if (obj == NULL || opts == NULL) {
        json_decref(obj);
        json_decref(opts);
        return -1;
    }

    if (containerv_json_object_set_string(obj, "destination", destination) != 0 ||
        containerv_json_object_set_string(obj, "type", "bind") != 0 ||
        containerv_json_object_set_string(obj, "source", source) != 0) {
        json_decref(obj);
        json_decref(opts);
        return -1;
    }

    if (containerv_json_array_append_string(opts, "rbind") != 0 ||
        containerv_json_array_append_string(opts, "rprivate") != 0 ||
        containerv_json_array_append_string(opts, readonly ? "ro" : "rw") != 0) {
        json_decref(obj);
        json_decref(opts);
        return -1;
    }

    if (json_object_set_new(obj, "options", opts) != 0) {
        json_decref(obj);
        json_decref(opts);
        return -1;
    }

    if (json_array_append_new(mounts, obj) != 0) {
        json_decref(obj);
        return -1;
    }

    return 0;
}

static int __append_custom_mounts(json_t* spec, const struct containerv_oci_linux_spec_params* params)
{
    json_t* mounts;

    if (spec == NULL || params == NULL || params->mounts == NULL || params->mounts_count == 0) {
        return 0;
    }

    mounts = json_object_get(spec, "mounts");
    if (mounts == NULL || !json_is_array(mounts)) {
        mounts = json_array();
        if (mounts == NULL) {
            return -1;
        }
        if (json_object_set_new(spec, "mounts", mounts) != 0) {
            json_decref(mounts);
            return -1;
        }
    }

    for (size_t i = 0; i < params->mounts_count; ++i) {
        const struct containerv_oci_mount_entry* m = &params->mounts[i];
        if (m->source == NULL || m->source[0] == '\0' || m->destination == NULL || m->destination[0] == '\0') {
            continue;
        }
        if (__append_bind_mount(mounts, m->source, m->destination, m->readonly) != 0) {
            return -1;
        }
    }

    return 0;
}

static int __build_namespaces(json_t* linuxObj)
{
    json_t* namespaces = json_array();
    if (namespaces == NULL) {
        return -1;
    }

    const char* const ns_types[] = {"pid", "ipc", "uts", "mount", "network", NULL};
    for (size_t i = 0; ns_types[i] != NULL; ++i) {
        json_t* ns = json_object();
        if (ns == NULL || containerv_json_object_set_string(ns, "type", ns_types[i]) != 0 ||
            json_array_append_new(namespaces, ns) != 0) {
            json_decref(ns);
            json_decref(namespaces);
            return -1;
        }
    }

    if (json_object_set(linuxObj, "namespaces", namespaces) != 0) {
        json_decref(namespaces);
        return -1;
    }
    return 0;
}

static int __append_device(json_t* devices, json_t* resources, const struct __oci_device_spec* dev)
{
    if (devices == NULL || resources == NULL || dev == NULL || dev->path == NULL) {
        return -1;
    }

    json_t* d = json_object();
    if (d == NULL) {
        return -1;
    }

    char type_str[2] = {dev->type, '\0'};
    if (containerv_json_object_set_string(d, "path", dev->path) != 0 ||
        containerv_json_object_set_string(d, "type", type_str) != 0 ||
        containerv_json_object_set_int(d, "major", dev->major) != 0 ||
        containerv_json_object_set_int(d, "minor", dev->minor) != 0 ||
        containerv_json_object_set_int(d, "fileMode", dev->file_mode) != 0 ||
        containerv_json_object_set_int(d, "uid", dev->uid) != 0 ||
        containerv_json_object_set_int(d, "gid", dev->gid) != 0) {
        json_decref(d);
        return -1;
    }

    if (json_array_append_new(devices, d) != 0) {
        json_decref(d);
        return -1;
    }

    json_t* r = json_object();
    if (r == NULL) {
        return -1;
    }
    if (containerv_json_object_set_bool(r, "allow", 1) != 0 ||
        containerv_json_object_set_string(r, "type", type_str) != 0 ||
        containerv_json_object_set_int(r, "major", dev->major) != 0 ||
        containerv_json_object_set_int(r, "minor", dev->minor) != 0 ||
        containerv_json_object_set_string(r, "access", dev->permissions) != 0) {
        json_decref(r);
        return -1;
    }

    if (json_array_append_new(resources, r) != 0) {
        json_decref(r);
        return -1;
    }

    return 0;
}

static int __build_devices(json_t* linuxObj)
{
    json_t* devices = json_array();
    json_t* resources = json_array();
    json_t* resources_obj = json_object();
    if (devices == NULL || resources == NULL || resources_obj == NULL) {
        json_decref(devices);
        json_decref(resources);
        json_decref(resources_obj);
        return -1;
    }

    for (const struct __oci_device_spec* d = __g_devices; d->path != NULL; ++d) {
        if (__append_device(devices, resources, d) != 0) {
            json_decref(devices);
            json_decref(resources);
            json_decref(resources_obj);
            return -1;
        }
    }

    if (json_object_set_new(linuxObj, "devices", devices) != 0) {
        json_decref(devices);
        json_decref(resources);
        json_decref(resources_obj);
        return -1;
    }

    if (json_object_set_new(resources_obj, "devices", resources) != 0 ||
        json_object_set_new(linuxObj, "resources", resources_obj) != 0) {
        json_decref(resources);
        json_decref(resources_obj);
        return -1;
    }

    return 0;
}

static int __build_annotations(json_t* spec, const char* root_path)
{
    json_t* annotations = json_object();
    if (annotations == NULL) {
        return -1;
    }

    if (containerv_json_object_set_string(annotations, "com.chef.lcow", "true") != 0 ||
        containerv_json_object_set_string(annotations, "com.chef.gcs", "true") != 0) {
        json_decref(annotations);
        return -1;
    }

    if (root_path != NULL && root_path[0] != '\0') {
        if (containerv_json_object_set_string(annotations, "com.chef.rootfs", root_path) != 0) {
            json_decref(annotations);
            return -1;
        }
    }

    if (json_object_set_new(spec, "annotations", annotations) != 0) {
        json_decref(annotations);
        return -1;
    }

    return 0;
}

static int __build_masked_and_readonly_paths(json_t* spec)
{
    json_t* masked = json_array();
    json_t* readonly = json_array();
    if (masked == NULL || readonly == NULL) {
        json_decref(masked);
        json_decref(readonly);
        return -1;
    }

    for (const char* const* p = __masked_paths; p != NULL && *p != NULL; ++p) {
        if (json_array_append_new(masked, json_string(*p)) != 0) {
            json_decref(masked);
            json_decref(readonly);
            return -1;
        }
    }

    for (const char* const* p = __readonly_paths; p != NULL && *p != NULL; ++p) {
        if (json_array_append_new(readonly, json_string(*p)) != 0) {
            json_decref(masked);
            json_decref(readonly);
            return -1;
        }
    }

    if (json_object_set_new(spec, "maskedPaths", masked) != 0 ||
        json_object_set_new(spec, "readonlyPaths", readonly) != 0) {
        json_decref(masked);
        json_decref(readonly);
        return -1;
    }

    return 0;
}

static int __configure_hostname(json_t* spec, const char* hostname)
{
    return containerv_json_object_set_string(spec, "hostname", hostname);
}

int containerv_oci_build_linux_spec_json(
    const struct containerv_oci_linux_spec_params* params,
    char**                                         jsonOut)
{
    const char* cwd;
    json_t*     spec = NULL;
    json_t*     process = NULL;
    json_t*     linuxObj;
    int         status;

    if (jsonOut == NULL) {
        return -1;
    }

    if (params == NULL || params->root_path == NULL || params->root_path[0] == '\0') {
        return -1;
    }

    cwd = (params->cwd != NULL && params->cwd[0] != '\0') ? params->cwd : "/";

    spec = json_object();
    if (spec == NULL) {
        return -1;
    }

    containerv_json_object_set_string(spec, "ociVersion", "1.0.2");

    process = __process_create_json(spec, cwd);
    if (process == NULL) {
        status = -1;
        goto cleanup;
    }

    linuxObj = json_object();
    if (linuxObj == NULL || json_object_set(spec, "linux", linuxObj)) {
        status = -1;
        goto cleanup;
    }

    status = __build_args(process, params->args_json);
    if (status) {
        goto cleanup;
    }

    status = __build_environment(process, params->envv);
    if (status) {
        goto cleanup;
    }

    status = __configure_user(process);
    if (status) {
        goto cleanup;
    }

    status = __build_mounts(spec);
    if (status) {
        goto cleanup;
    }

    if (__append_custom_mounts(spec, params) != 0) {
        json_decref(spec);
        return -1;
    }

    status = __build_namespaces(linuxObj);
    if (status) {
        goto cleanup;
    }

    status = __build_devices(linuxObj);
    if (status) {
        goto cleanup;
    }
    
    status = __build_root(spec, params->root_path);
    if (status) {
        goto cleanup;
    }

    status = __build_annotations(spec, params->root_path);
    if (status) {
        goto cleanup;
    }

    status = __build_masked_and_readonly_paths(spec);
    if (status) {
        goto cleanup;
    }

    if (params->hostname != NULL && params->hostname[0] != '\0') {
        status = __configure_hostname(spec, params->hostname);
        if (status) {
            goto cleanup;
        }
    }

    status = containerv_json_dumps_compact(spec, jsonOut);

cleanup:
    json_decref(process);
    json_decref(linuxObj);
    json_decref(spec);
    return status;
}
