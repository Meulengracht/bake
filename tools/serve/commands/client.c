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

#include <gracht/link/socket.h>
#include <gracht/client.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

#define LOCAL_UPLOAD_STREAM_BUFFER_SIZE (256 * 1024)
#define LOCAL_UPLOAD_STREAM_BUFFER_COUNT 4

// client protocol
#include "chef_served_local_upload_service_client.h"
#include "chef_served_service_client.h"

// Transaction watcher state — set by __chef_client_watch_transaction(),
// updated by event callbacks, checked by __chef_client_await_transaction().
static struct {
    unsigned int                 id;
    int                          active;
    int                          completed;
    enum chef_transaction_result result;
} g_txn_watcher = { 0 };

#if defined(__linux__)
#include <sys/un.h>

static const char* clientsPath = "/tmp/served";

static void init_socket_config(struct gracht_link_socket* link)
{
    struct sockaddr_un addr = { 0 };
    
    addr.sun_family = AF_LOCAL;
    //strncpy (addr->sun_path, dgramPath, sizeof(addr->sun_path));
    strncpy (addr.sun_path, clientsPath, sizeof(addr.sun_path));
    addr.sun_path[sizeof(addr.sun_path) - 1] = '\0';

    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_un));
    gracht_link_socket_set_domain(link, AF_LOCAL);
}

#elif defined(_WIN32)
#include <windows.h>

static void init_socket_config(struct gracht_link_socket* link)
{
    struct sockaddr_in addr = { 0 };
    
    // initialize the WSA library
    gracht_link_socket_setup();

    // AF_INET is the Internet address family.
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons(4335);

    gracht_link_socket_set_type(link, gracht_link_stream_based);
    gracht_link_socket_set_connect_address(link, (const struct sockaddr_storage*)&addr, sizeof(struct sockaddr_in));
}
#endif

int __chef_client_initialize(gracht_client_t** clientOut)
{
    struct gracht_link_socket*         link;
    struct gracht_client_configuration clientConfiguration;
    gracht_client_t*                   client = NULL;
    int                                code;

    gracht_client_configuration_init(&clientConfiguration);
    
    gracht_link_socket_create(&link);
    init_socket_config(link);

    gracht_client_configuration_set_link(&clientConfiguration, (struct gracht_link*)link);
    gracht_client_configuration_set_stream_buffer_size(
        &clientConfiguration,
        LOCAL_UPLOAD_STREAM_BUFFER_SIZE,
        LOCAL_UPLOAD_STREAM_BUFFER_COUNT
    );

    code = gracht_client_create(&clientConfiguration, &client);
    if (code) {
        printf("__chef_client_initialize: error initializing client library %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_register_protocol(client, &chef_served_client_protocol);
    if (code) {
        printf("__chef_client_initialize: error registering protocol %i, %i\n", errno, code);
        return code;
    }

    code = gracht_client_connect(client);
    if (code) {
        printf("__chef_client_initialize: failed to connect client %i, %i\n", errno, code);
    }

    *clientOut = client;
    return code;
}

void chef_served_event_transaction_started_invocation(gracht_client_t* client, const struct chef_transaction_started* info)
{
    if (!g_txn_watcher.active || info->id != g_txn_watcher.id) {
        return;
    }
    printf("transaction %u started: %s\n", info->id, info->name ? info->name : "");
}

void chef_served_event_transaction_state_changed_invocation(gracht_client_t* client, const struct chef_transaction_state_changed* info)
{
    if (!g_txn_watcher.active || info->id != g_txn_watcher.id) {
        return;
    }
    printf("  [%u/%u] %s\n", info->step, info->total_steps, info->state_name ? info->state_name : "?");
}

void chef_served_event_transaction_completed_invocation(gracht_client_t* client, const struct chef_transaction_completed* info)
{
    if (!g_txn_watcher.active || info->id != g_txn_watcher.id) {
        return;
    }
    g_txn_watcher.result    = info->result;
    g_txn_watcher.completed = 1;

    if (info->result == CHEF_TRANSACTION_RESULT_SUCCESS) {
        printf("transaction %u completed successfully\n", info->id);
    } else {
        fprintf(stderr, "transaction %u failed: %s\n", info->id, info->message ? info->message : "unknown error");
    }
}

void chef_served_event_transaction_io_progress_invocation(gracht_client_t* client, const struct chef_transaction_io_progress* info)
{
    if (!g_txn_watcher.active || info->id != g_txn_watcher.id) {
        return;
    }
    printf("  progress: %u%%\r", info->percentage);
    fflush(stdout);
}

void chef_served_event_transaction_log_invocation(gracht_client_t* client, const struct chef_transaction_log* info)
{
    if (!g_txn_watcher.active || info->id != g_txn_watcher.id) {
        return;
    }
    if (info->entry.message != NULL) {
        printf("  log: %s\n", info->entry.message);
    }
}

int __chef_client_subscribe(gracht_client_t* client)
{
    struct gracht_message_context context;
    int                           status;

    status = chef_served_subscribe(client, &context);
    if (status) {
        return status;
    }
    return gracht_client_wait_message(client, &context, GRACHT_MESSAGE_BLOCK);
}

int __chef_client_await_transaction(gracht_client_t* client, unsigned int transactionId, enum chef_transaction_result* resultOut)
{
    g_txn_watcher.id        = transactionId;
    g_txn_watcher.completed = 0;
    g_txn_watcher.result    = CHEF_TRANSACTION_RESULT_ERROR_UNKNOWN;
    g_txn_watcher.active    = 1;

    while (!g_txn_watcher.completed) {
        int status = gracht_client_wait_message(client, NULL, GRACHT_MESSAGE_BLOCK);
        if (status) {
            g_txn_watcher.active = 0;
            return status;
        }
    }

    g_txn_watcher.active = 0;
    if (resultOut != NULL) {
        *resultOut = g_txn_watcher.result;
    }
    return 0;
}
