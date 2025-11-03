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
#define _GNU_SOURCE

#include <errno.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "chef_waiterd_service_client.h"
#include "commands.h"
#include "remote-helpers/remote.h"

static void __print_help(void)
{
    printf("Usage: bake remote info [agent]\n");
    printf("  Display detailed information about a specific remote agent.\n\n");
    printf("Options:\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static const char* __arch_to_string(enum chef_build_architecture arch)
{
    static char buffer[128];
    int offset = 0;
    int ret;
    
    buffer[0] = '\0';
    
    if (arch & CHEF_BUILD_ARCHITECTURE_X86) {
        ret = snprintf(buffer + offset, sizeof(buffer) - offset, "i386");
        if (ret > 0 && ret < sizeof(buffer) - offset) {
            offset += ret;
        }
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_X64) {
        if (offset > 0 && offset < sizeof(buffer) - 3) {
            ret = snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
            if (ret > 0 && ret < sizeof(buffer) - offset) {
                offset += ret;
            }
        }
        ret = snprintf(buffer + offset, sizeof(buffer) - offset, "amd64");
        if (ret > 0 && ret < sizeof(buffer) - offset) {
            offset += ret;
        }
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_ARMHF) {
        if (offset > 0 && offset < sizeof(buffer) - 3) {
            ret = snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
            if (ret > 0 && ret < sizeof(buffer) - offset) {
                offset += ret;
            }
        }
        ret = snprintf(buffer + offset, sizeof(buffer) - offset, "armhf");
        if (ret > 0 && ret < sizeof(buffer) - offset) {
            offset += ret;
        }
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_ARM64) {
        if (offset > 0 && offset < sizeof(buffer) - 3) {
            ret = snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
            if (ret > 0 && ret < sizeof(buffer) - offset) {
                offset += ret;
            }
        }
        ret = snprintf(buffer + offset, sizeof(buffer) - offset, "arm64");
        if (ret > 0 && ret < sizeof(buffer) - offset) {
            offset += ret;
        }
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_RISCV64) {
        if (offset > 0 && offset < sizeof(buffer) - 3) {
            ret = snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
            if (ret > 0 && ret < sizeof(buffer) - offset) {
                offset += ret;
            }
        }
        ret = snprintf(buffer + offset, sizeof(buffer) - offset, "riscv64");
        if (ret > 0 && ret < sizeof(buffer) - offset) {
            offset += ret;
        }
    }
    
    return buffer;
}

int remote_info_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    gracht_client_t* client = NULL;
    const char*      agent_name = NULL;
    int              status;
    int              i;

    // Parse arguments - find "info" first, then the agent name
    int found_info = 0;
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "info")) {
            found_info = 1;
            // Next argument should be the agent name if it exists
            if (i + 1 < argc && strcmp(argv[i + 1], "-h") != 0 && strcmp(argv[i + 1], "--help") != 0) {
                agent_name = argv[i + 1];
            }
            continue;
        }
        
        if (found_info && (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))) {
            __print_help();
            return 0;
        }
    }

    if (agent_name == NULL) {
        fprintf(stderr, "bake: agent name required\n");
        __print_help();
        return -1;
    }

    // Create remote client
    status = remote_client_create(&client);
    if (status != 0) {
        fprintf(stderr, "bake: failed to connect to remote server\n");
        return -1;
    }

    // Call agent_info
    struct gracht_message_context context;
    chef_waiterd_agent_info(client, &context, agent_name);
    
    struct chef_waiter_agent_info info;
    memset(&info, 0, sizeof(info));
    chef_waiterd_agent_info_result(client, &context, &info);

    if (info.online == 0 && (info.name == NULL || info.name[0] == '\0')) {
        printf("Agent not found: %s\n", agent_name);
    } else {
        printf("Agent: %s\n", info.name ? info.name : agent_name);
        printf("Status: %s\n", info.online ? "Online" : "Offline");
        printf("Architectures: %s\n", __arch_to_string(info.architectures));
        
        if (info.online) {
            printf("Current Load: %d\n", info.queue_size);
        } else {
            printf("Current Load: -\n");
        }
    }

    // Cleanup
    chef_waiter_agent_info_destroy(&info);
    gracht_client_shutdown(client);
    
    return 0;
}
