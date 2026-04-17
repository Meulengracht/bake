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

#include <chef/bits/package.h>
#include <chef/config.h>
#include <chef/dirs.h>
#include <chef/environment.h>
#include <chef/package_manifest.h>
#include <chef/platform.h>
#include <chef/utils_vafs.h>
#include <errno.h>
#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <strings.h>
#endif
#include <vafs/vafs.h>
#include <vlog.h>

#include <utils.h>

#include "chef_cvd_service_client.h"

static gracht_client_t* g_containerClient = NULL;
static struct chef_config* g_servedConfig = NULL;
static void*               g_packNetworkSection = NULL;

#if defined(__linux__)
#include <arpa/inet.h>
#include <sys/un.h>

static int __abstract_socket_size(const char* address) {
    return offsetof(struct sockaddr_un, sun_path) + strlen(address);
}

static int __local_size(const char* address) {
    // If the address starts with '@', it is an abstract socket path.
    // Abstract socket paths are not null-terminated, so we need to account for that.
    if (address[0] == '@') {
        return __abstract_socket_size(address);
    }
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_LOCAL;

    // sanitize the address length
    if (strlen(address) >= sizeof(local->sun_path)) {
        VLOG_ERROR("served", "__configure_local: address too long for local socket: %s\n", address);
        return -1;
    }

    // handle abstract socket paths
    if (address[0] == '@') {
        local->sun_path[0] = '\0';
        strncpy(local->sun_path + 1, address + 1, sizeof(local->sun_path) - 1);
    } else {
        strncpy(local->sun_path, address, sizeof(local->sun_path));
    }
    return 0;
}

static int __configure_local_bind(struct gracht_link_socket* link)
{
    struct sockaddr_storage storage = { 0 };
    struct sockaddr_un*     address = (struct sockaddr_un*)&storage;

    address->sun_family = AF_LOCAL;
    snprintf(&address->sun_path[1],
        sizeof(address->sun_path) - 2,
        "/chef/cvd/clients/%u",
        getpid()
    );

    gracht_link_socket_set_bind_address(link, &storage, __abstract_socket_size(&address->sun_path[1]));
    return 0;
}
#elif defined(_WIN32)
#include <windows.h>
#include <ws2ipdef.h>
#include <process.h>
// Windows 10 Insider build 17063 ++ 
#include <afunix.h>

static int __abstract_socket_size(const char* address) {
    return (int)(offsetof(struct sockaddr_un, sun_path) + strlen(address));
}

static int __local_size(const char* address) {
    return sizeof(struct sockaddr_un);
}

static int __configure_local(struct sockaddr_storage* storage, const char* address)
{
    struct sockaddr_un* local = (struct sockaddr_un*)storage;

    local->sun_family = AF_UNIX;
    strncpy(local->sun_path, address, sizeof(local->sun_path));
    return 0;
}

static int __configure_local_bind(struct gracht_link_socket* link)
{
    struct sockaddr_storage storage = { 0 };
    struct sockaddr_un*     address = (struct sockaddr_un*)&storage;

    address->sun_family = AF_UNIX;
    snprintf(&address->sun_path[1],
        sizeof(address->sun_path) - 2,
        "/chef/cvd/clients/%u",
        _getpid()
    );

    gracht_link_socket_set_bind_address(link, &storage, __abstract_socket_size(&address->sun_path[1]));
    return 0;
}
#endif

// The format of the base can be either of 
// ubuntu:24
// windows:servercore-ltsc2022
// windows:nanoserver-ltsc2022
// windows:ltsc2022
// From this, derive the guest type
static enum chef_guest_type __guest_type_from_base(const char* platform)
{
    if (strncmp(platform, "windows:", 8) == 0 || strncmp(platform, "windows", 7) == 0) {
        return CHEF_GUEST_TYPE_WINDOWS;
    }
    return CHEF_GUEST_TYPE_LINUX;
}

#ifdef _WIN32
static int __configure_windows_guest_options(struct chef_create_parameters* params)
{
    (void)params;

    if (params->gtype != CHEF_GUEST_TYPE_LINUX) {
        return 0;
    }

    return 0;
}
#endif

static void __configure_inet4(struct sockaddr_storage* storage, struct chef_config_address* config)
{
    struct sockaddr_in* inet4 = (struct sockaddr_in*)storage;

    inet4->sin_family = AF_INET;
    inet4->sin_addr.s_addr = inet_addr(config->address);
    inet4->sin_port = htons(config->port);
}

static int init_link_config(struct gracht_link_socket* link, enum gracht_link_type type, struct chef_config_address* config)
{
    struct sockaddr_storage addr_storage = { 0 };
    socklen_t               size;
    int                     domain = 0;
    int                     status;
    VLOG_DEBUG("served", "init_link_config(link=%i, type=%s)\n", type, config->type);

    if (!strcmp(config->type, "local")) {
        status = __configure_local_bind(link);
        if (status) {
            VLOG_ERROR("served", "__init_link_config failed to configure local bind address\n");
            return status;
        }

        status = __configure_local(&addr_storage, config->address);
        if (status) {
            VLOG_ERROR("served", "init_link_config failed to configure local link\n");
            return status;
        }
        domain = addr_storage.ss_family;
        size = __local_size(config->address);

        VLOG_DEBUG("served", "connecting to %s\n", config->address);
    } else if (!strcmp(config->type, "inet4")) {
        __configure_inet4(&addr_storage, config);
        domain = AF_INET;
        size = sizeof(struct sockaddr_in);
        
        VLOG_DEBUG("served", "connecting to %s:%u\n", config->address, config->port);
    } else if (!strcmp(config->type, "inet6")) {
        // TODO
        domain = AF_INET6;
        size = sizeof(struct sockaddr_in6);
    } else {
        VLOG_ERROR("served", "init_link_config invalid link type %s\n", config->type);
        return -1;
    }

    gracht_link_socket_set_type(link, type);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr_storage, size);
    gracht_link_socket_set_domain(link, domain);
    return 0;
}

int container_client_initialize(struct chef_config_address* config)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    int                                code;
    VLOG_DEBUG("served", "container_client_initialize()\n");

    code = gracht_link_socket_create(&link);
    if (code) {
        VLOG_ERROR("served", "container_client_initialize: failed to initialize socket\n");
        return code;
    }

    init_link_config(link, gracht_link_packet_based, config);

    gracht_client_configuration_init(&clientConfiguration);
    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);

    code = gracht_client_create(&clientConfiguration, &g_containerClient);
    if (code) {
        VLOG_ERROR("served", "container_client_initialize: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(g_containerClient);
    if (code) {
        VLOG_ERROR("served", "container_client_initialize: failed to connect client %i, %i\n", errno, code);
        gracht_client_shutdown(g_containerClient);
        return code;
    }

    return code;
}

void container_client_shutdown(void)
{
    VLOG_DEBUG("served", "container_client_shutdown()\n");
    if (g_containerClient == NULL) {
        return;
    }
    gracht_client_shutdown(g_containerClient);
    g_containerClient = NULL;
}

static int __to_errno_code(enum chef_status status)
{
    switch (status) {
        case CHEF_STATUS_SUCCESS:
            return 0;
        case CHEF_STATUS_CONTAINER_EXISTS:
            errno = EEXIST;
            return -1;
        case CHEF_STATUS_INTERNAL_ERROR:
            errno = EFAULT;
            return -1;
        case CHEF_STATUS_FAILED_ROOTFS_SETUP:
            errno = EIO;
            return -1;
        case CHEF_STATUS_INVALID_MOUNTS:
            errno = EINVAL;
            return -1;
        case CHEF_STATUS_INVALID_CONTAINER_ID:
            errno = ENOENT;
            return -1;
        default:
            errno = EINVAL;
            return -1;
    }
}

static void __ensure_config_loaded(void)
{
    if (g_servedConfig != NULL) {
        return;
    }

    // served initializes chef dirs at startup, so this should be safe.
    g_servedConfig = chef_config_load(chef_dirs_config());
    if (g_servedConfig != NULL) {
        // Note: chef_config_section() creates the section if it does not exist.
        // We keep this to a single section to avoid creating many objects during lookups.
        g_packNetworkSection = chef_config_section(g_servedConfig, "pack-network");
    }
}

static char* __dup_pack_network_config_value(const char* pack_id, const char* suffix)
{
    char key[CHEF_PACKAGE_ID_LENGTH_MAX + 64];
    const char* value;

    if (pack_id == NULL || suffix == NULL) {
        return NULL;
    }

    __ensure_config_loaded();
    if (g_servedConfig == NULL || g_packNetworkSection == NULL) {
        return NULL;
    }

    snprintf(&key[0], sizeof(key), "%s.%s", pack_id, suffix);
    value = chef_config_get_string(g_servedConfig, g_packNetworkSection, &key[0]);
    if (value == NULL || value[0] == '\0') {
        return NULL;
    }
    return platform_strdup(value);
}

static int __load_pack_network_defaults(const char* packPath, char** gatewayOut, char** dnsOut)
{
    struct VaFs* vafs = NULL;
    struct chef_vafs_feature_package_network* header = NULL;
    struct VaFsGuid guid = CHEF_PACKAGE_NETWORK_GUID;
    int status;

    if (packPath == NULL) {
        return 0;
    }

    status = vafs_open_file(packPath, &vafs);
    if (status != 0) {
        return 0;
    }

    status = vafs_feature_query(vafs, &guid, (struct VaFsFeatureHeader**)&header);
    if (status != 0) {
        vafs_close(vafs);
        return 0;
    }

    char* data = (char*)header + sizeof(struct chef_vafs_feature_package_network);
    if (gatewayOut && *gatewayOut == NULL && header->gateway_length > 0) {
        *gatewayOut = platform_strndup(data, header->gateway_length);
    }
    data += header->gateway_length;
    if (dnsOut && *dnsOut == NULL && header->dns_length > 0) {
        *dnsOut = platform_strndup(data, header->dns_length);
    }

    vafs_close(vafs);
    return 0;
}

// Signature for per-capability config handlers.
// Each handler translates raw capability config entries into the protocol-level
// plugin config variant. Return 0 on success.
typedef int (*plugin_config_fn)(
    struct chef_policy_plugin*                     plugin,
    const struct chef_package_manifest_capability* capability
);

static int __configure_network_client(
    struct chef_policy_plugin*                     plugin,
    const struct chef_package_manifest_capability* capability);

// Table of recognised system capabilities.
// Each entry maps a capability name to the policy plugin name that cvd expects,
// plus an optional config handler. To add a new capability, append a row here
// and (if it carries config) implement the handler.
static const struct {
    const char*      capability_name;
    const char*      plugin_name;
    plugin_config_fn configure;
} g_capabilityTable[] = {
    { "network-client",     "network",            __configure_network_client },
    { "file-control",       "file-control",       NULL },
    { "process-control",    "process-control",    NULL },
    { "package-management", "package-management", NULL },
};

#define CAPABILITY_TABLE_COUNT (sizeof(g_capabilityTable) / sizeof(g_capabilityTable[0]))

// Returns the table index for a recognised capability, or -1.
static int __find_capability_entry(const char* name)
{
    for (int i = 0; i < (int)CAPABILITY_TABLE_COUNT; i++) {
        if (strcmp(name, g_capabilityTable[i].capability_name) == 0) {
            return i;
        }
    }
    return -1;
}

// ---------------------------------------------------------------------------
// network-client config handler
//
// Parses "allow" config entries into chef_policy_plugin_network_client.
// Each value in the "allow" list is a flattened object string produced by
// the recipe parser from structured YAML:
//   "proto=tcp;ports=80,443"   -> TCP, ports [{80,80},{443,443}]
//   "proto=udp;ports=53"       -> UDP, ports [{53,53}]
//   "proto=tcp;ports=8000-9000"-> TCP, ports [{8000,9000}]
// ---------------------------------------------------------------------------

static int __count_port_specs(const char* str)
{
    int count = 1;
    for (const char* p = str; *p != '\0'; p++) {
        if (*p == ',') {
            count++;
        }
    }
    return count;
}

// Validate a parsed port number. Port 0 is rejected because these rules
// describe allowed destinations, not wildcard binds.
static int __valid_port(unsigned long val)
{
    return (val > 0 && val <= 65535) ? 0 : -1;
}

static int __parse_port_spec(
    const char*                    str,
    int                            len,
    struct chef_network_rule_port* port)
{
    char          buf[32]; // max "65535-65535\0" = 12 chars; 32 is generous
    char*         dash;
    char*         end;
    unsigned long val;

    if (len <= 0 || len >= (int)sizeof(buf)) {
        return -1;
    }
    memcpy(buf, str, len);
    buf[len] = '\0';

    dash = strchr(buf, '-');
    if (dash != NULL) {
        *dash = '\0';
        val = strtoul(buf, &end, 10);
        if (*end != '\0' || __valid_port(val) != 0) {
            return -1;
        }
        port->start = (uint16_t)val;

        val = strtoul(dash + 1, &end, 10);
        if (*end != '\0' || __valid_port(val) != 0) {
            return -1;
        }
        port->end = (uint16_t)val;
    } else {
        val = strtoul(buf, &end, 10);
        if (*end != '\0' || __valid_port(val) != 0) {
            return -1;
        }
        port->start = (uint16_t)val;
        port->end   = port->start;
    }
    return 0;
}

// Look up a field value in a "key=value;key=value" object string.
// Returns a pointer to the value (inside str) and sets *out_len to
// the length of the value, or returns NULL if not found.
static const char* __object_field(
    const char* str,
    const char* key,
    int*        out_len)
{
    size_t klen = strlen(key);
    const char* p = str;

    while (*p != '\0') {
        const char* semi = strchr(p, ';');
        int         seg  = semi ? (int)(semi - p) : (int)strlen(p);
        const char* eq   = memchr(p, '=', seg);

        if (eq != NULL && (size_t)(eq - p) == klen && strncmp(p, key, klen) == 0) {
            const char* val = eq + 1;
            *out_len = (int)(p + seg - val);
            return val;
        }
        p = semi ? semi + 1 : p + seg;
    }
    return NULL;
}

static int __parse_network_rule(const char* str, struct chef_network_rule* rule)
{
    const char* proto_val;
    const char* ports_val;
    int         proto_len;
    int         ports_len;
    int         port_count;
    int         idx;

    proto_val = __object_field(str, "proto", &proto_len);
    if (proto_val == NULL) {
        return -1;
    }

    if (proto_len == 3 && strncmp(proto_val, "tcp", 3) == 0) {
        rule->protocol = CHEF_NETWORK_RULE_PROTOCOL_TCP;
    } else if (proto_len == 3 && strncmp(proto_val, "udp", 3) == 0) {
        rule->protocol = CHEF_NETWORK_RULE_PROTOCOL_UDP;
    } else {
        return -1;
    }

    ports_val = __object_field(str, "ports", &ports_len);
    if (ports_val == NULL || ports_len == 0) {
        return -1;
    }

    // Work with a NUL-terminated copy of the ports substring.
    {
        char  ports_buf[256];
        const char* p;

        if (ports_len >= (int)sizeof(ports_buf)) {
            return -1;
        }
        memcpy(ports_buf, ports_val, ports_len);
        ports_buf[ports_len] = '\0';

        port_count = __count_port_specs(ports_buf);
        chef_network_rule_ports_add(rule, port_count);

        idx = 0;
        p = ports_buf;
        while (*p != '\0') {
            const char* comma = strchr(p, ',');
            int         len   = comma ? (int)(comma - p) : (int)strlen(p);

            if (__parse_port_spec(p, len, chef_network_rule_ports_get(rule, idx)) != 0) {
                VLOG_ERROR("served", "invalid port spec in network rule: %s\n", str);
                return -1;
            }
            idx++;
            p = comma ? comma + 1 : p + len;
        }
    }
    return 0;
}

static int __configure_network_client(
    struct chef_policy_plugin*                     plugin,
    const struct chef_package_manifest_capability* capability)
{
    struct chef_policy_plugin_network_client nc;
    size_t                                 i;
    int                                    rule_idx;
    int                                    valid_count = 0;

    chef_policy_plugin_network_client_init(&nc);

    for (i = 0; i < capability->allow_list.count; i++) {
        struct chef_network_rule probe;

        chef_network_rule_init(&probe);
        if (__parse_network_rule(capability->allow_list.values[i], &probe) == 0) {
            valid_count++;
        } else {
            VLOG_ERROR("served", "skipping invalid network allow rule: %s\n", capability->allow_list.values[i]);
        }
        chef_network_rule_destroy(&probe);
    }

    if (valid_count == 0) {
        chef_policy_plugin_config_set_policy_plugin_network_client(plugin, &nc);
        chef_policy_plugin_network_client_destroy(&nc);
        return 0;
    }

    chef_policy_plugin_network_client_allow_add(&nc, valid_count);
    rule_idx = 0;
    for (i = 0; i < capability->allow_list.count; i++) {
        struct chef_network_rule* rule =
            chef_policy_plugin_network_client_allow_get(&nc, rule_idx);

        if (__parse_network_rule(capability->allow_list.values[i], rule) == 0) {
            rule_idx++;
        }
    }

    chef_policy_plugin_config_set_policy_plugin_network_client(plugin, &nc);
    chef_policy_plugin_network_client_destroy(&nc);
    return 0;
}

// ---------------------------------------------------------------------------
// Generic capability → plugin translation
// ---------------------------------------------------------------------------

static int __load_pack_capabilities_as_plugins(
    const char*                packPath,
    struct chef_policy_spec*   policyOut)
{
    struct VaFs*                    vafs = NULL;
    struct chef_package_manifest*   manifest = NULL;
    int                             pluginCount = 0;
    int                             pluginIdx = 0;
    int                             status;

    if (packPath == NULL) {
        return 0;
    }

    status = vafs_open_file(packPath, &vafs);
    if (status != 0) {
        return 0;
    }

    status = chef_package_manifest_load_vafs(vafs, &manifest);
    vafs_close(vafs);
    if (status != 0 || manifest == NULL || manifest->capabilities_count == 0) {
        return 0;
    }

    for (size_t i = 0; i < manifest->capabilities_count; i++) {
        if (__find_capability_entry(manifest->capabilities[i].name) >= 0) {
            pluginCount++;
        }
    }

    if (pluginCount > 0) {
        chef_policy_spec_plugins_add(policyOut, pluginCount);
        for (size_t i = 0; i < manifest->capabilities_count; i++) {
            int entry = __find_capability_entry(manifest->capabilities[i].name);
            struct chef_policy_plugin* plugin;

            if (entry < 0) {
                continue;
            }

            plugin = chef_policy_spec_plugins_get(policyOut, pluginIdx);
            plugin->name = platform_strdup(g_capabilityTable[entry].plugin_name);

            if (g_capabilityTable[entry].configure != NULL) {
                g_capabilityTable[entry].configure(plugin, &manifest->capabilities[i]);
            }
            pluginIdx++;
        }
    }

    chef_package_manifest_free(manifest);
    return pluginCount;
}

static enum chef_status __create_container(
    gracht_client_t* client,
    const char*      id,
    const char*      pack_id,
    const char*      rootfs,
    const char*      package)
{
    struct gracht_message_context context;
    struct chef_create_parameters params;
    struct chef_layer_descriptor* layer;
    int                           status;
    enum chef_status              chstatus;
    char                          cvdid[CHEF_PACKAGE_ID_LENGTH_MAX];
    VLOG_DEBUG("served", "__create_container(id=%s)\n", id);

    chef_create_parameters_init(&params);
    
    params.id = (char*)id;
    params.gtype = __guest_type_from_base(rootfs);

#ifdef _WIN32
    if (__configure_windows_guest_options(&params) != 0) {
        chef_create_parameters_destroy(&params);
        return CHEF_STATUS_FAILED_ROOTFS_SETUP;
    }
#endif

    // Optional network settings.
    // Precedence:
    // - served per-pack override (bake.json section "pack-network")
    // - package defaults (from CHEF_PACKAGE_NETWORK_GUID)
    //
    // Keys are stored per pack identifier (<publisher>/<package>) as strings:
    //   <publisher>/<package>.gateway
    //   <publisher>/<package>.dns
    // Optional (if you want to fully specify network):
    //   <publisher>/<package>.container-ip
    //   <publisher>/<package>.container-netmask
    //   <publisher>/<package>.host-ip
    params.network.container_ip = __dup_pack_network_config_value(pack_id, "container-ip");
    params.network.container_netmask = __dup_pack_network_config_value(pack_id, "container-netmask");
    params.network.host_ip = __dup_pack_network_config_value(pack_id, "host-ip");

    params.network.gateway_ip = __dup_pack_network_config_value(pack_id, "gateway");
    params.network.dns = __dup_pack_network_config_value(pack_id, "dns");
    if (params.network.gateway_ip == NULL || params.network.dns == NULL) {
        (void)__load_pack_network_defaults(package, &params.network.gateway_ip, &params.network.dns);
        (void)__load_pack_network_defaults(rootfs, &params.network.gateway_ip, &params.network.dns);
    }

    // Load capabilities from the package and convert system capabilities
    // (network-client, file-control, process-control, package-management) into
    // policy plugins for the container security policy.
    chef_policy_spec_init(&params.policy);
    (void)__load_pack_capabilities_as_plugins(package, &params.policy);

    // On Windows HCS containers, OVERLAY layers are not supported.
#ifdef _WIN32
    chef_create_parameters_layers_add(&params, 2);
#else
    chef_create_parameters_layers_add(&params, 3);
#endif
    
    // initialize the base rootfs layer, this is a layer from
    // the base package
    layer = chef_create_parameters_layers_get(&params, 0);
    layer->type = CHEF_LAYER_TYPE_VAFS_PACKAGE;
    layer->source = platform_strdup(rootfs);
    layer->target = platform_strdup("/");
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

    // initialize the application layer, this is a layer from
    // the application package
    layer = chef_create_parameters_layers_get(&params, 1);
    layer->type = CHEF_LAYER_TYPE_VAFS_PACKAGE;
    layer->source = platform_strdup(package);
    layer->target = platform_strdup("/");
    layer->options = CHEF_MOUNT_OPTIONS_READONLY;

#ifndef _WIN32
    // initialize the overlay layer, this is an writable layer
    // on top of the base and application layers
    layer = chef_create_parameters_layers_get(&params, 2);
    layer->type = CHEF_LAYER_TYPE_OVERLAY;
#endif
    
    status = chef_cvd_create(client, &context, &params);
    if (status) {
        chef_create_parameters_destroy(&params);
        VLOG_ERROR("served", "__create_container failed to create client\n");
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_create_result(client, &context, &cvdid[0], sizeof(cvdid) - 1, &chstatus);
    chef_create_parameters_destroy(&params);
    return chstatus;
}

int container_client_create_container(struct container_options* options)
{
    VLOG_DEBUG("served", "container_client_create_container(id=%s, rootfs=%s)\n", options->id, options->rootfs);
    return __to_errno_code(__create_container(
        g_containerClient,
        options->id,
        options->pack_id,
        options->rootfs,
        options->package
    ));
}

static enum chef_status __container_spawn(
    gracht_client_t*             client,
    const char*                  id,
    const char* const*           environment,
    const char*                  command,
    enum chef_spawn_options      options,
    unsigned int*                pidOut)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    uint8_t*                      flatenv = NULL;
    size_t                        flatenvLength = 0;
    VLOG_DEBUG("served", "__container_spawn(cmd=%s)\n", command);

    if (environment != NULL) {
        flatenv = environment_flatten(environment, &flatenvLength);
        if (flatenv == NULL) {
            return CHEF_STATUS_INTERNAL_ERROR;
        }
    }

    status = chef_cvd_spawn(
        client,
        &context,
        &(struct chef_spawn_parameters) {
            .container_id = (char*)id,
            .command = (char*)command,
            .options = options,
            .environment = flatenv,
            .environment_count = (uint32_t)flatenvLength
            /* .user = */
        }
    );
    if (status != 0) {
        VLOG_ERROR("served", "__container_spawn: failed to execute %s\n", command);
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_spawn_result(client, &context, pidOut, &chstatus);
    return chstatus;
}

int container_client_spawn(
    const char*        id,
    const char* const* environment,
    const char*        command,
    unsigned int*      pidOut)
{
    VLOG_DEBUG("served", "container_client_spawn(id=%s, cmd=%s)\n", id, command);
    return __to_errno_code(__container_spawn(
        g_containerClient,
        id,
        environment,
        command,
        0,
        pidOut
    ));
}

enum chef_status __container_kill(
    gracht_client_t*             client,
    const char*                  id,
    unsigned int                 pid)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_DEBUG("served", "__container_kill()\n");

    status = chef_cvd_kill(client, &context, id, pid);
    if (status != 0) {
        VLOG_ERROR("served", "__container_kill: failed to invoke destroy\n");
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_kill_result(client, &context, &chstatus);
    return chstatus;
}

int container_client_kill(const char*  id, unsigned int pid)
{
    VLOG_DEBUG("served", "container_client_kill(id=%s, pid=%u)\n", id, pid);
    return __to_errno_code(__container_kill(
        g_containerClient,
        id,
        pid
    ));
}

enum chef_status __container_destroy(
    gracht_client_t*             client,
    const char*                  id)
{
    struct gracht_message_context context;
    int                           status;
    enum chef_status              chstatus;
    VLOG_DEBUG("served", "__container_destroy()\n");

    status = chef_cvd_destroy(client, &context, id);
    if (status != 0) {
        VLOG_ERROR("served", "__container_destroy: failed to invoke destroy\n");
        return status;
    }
    gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
    chef_cvd_destroy_result(client, &context, &chstatus);
    return chstatus;
}

int container_client_destroy_container(const char* id)
{
    VLOG_DEBUG("served", "container_client_destroy_container(id=%s)\n", id);
    return __to_errno_code(__container_destroy(
        g_containerClient,
        id
    ));
}
