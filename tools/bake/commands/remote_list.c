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
    printf("Usage: bake remote list [options]\n");
    printf("  List available remote build agents and their current status.\n\n");
    printf("Options:\n");
    printf("  --arch=<architecture>\n");
    printf("      Filter agents by architecture (i386, amd64, armhf, arm64, riscv64)\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static enum chef_build_architecture __parse_arch(const char* arch_str)
{
    if (strcmp(arch_str, "i386") == 0) {
        return CHEF_BUILD_ARCHITECTURE_X86;
    } else if (strcmp(arch_str, "amd64") == 0) {
        return CHEF_BUILD_ARCHITECTURE_X64;
    } else if (strcmp(arch_str, "armhf") == 0) {
        return CHEF_BUILD_ARCHITECTURE_ARMHF;
    } else if (strcmp(arch_str, "arm64") == 0) {
        return CHEF_BUILD_ARCHITECTURE_ARM64;
    } else if (strcmp(arch_str, "riscv64") == 0) {
        return CHEF_BUILD_ARCHITECTURE_RISCV64;
    }
    return 0;
}

static const char* __arch_to_string(enum chef_build_architecture arch)
{
    static char buffer[128];
    int offset = 0;
    
    buffer[0] = '\0';
    
    if (arch & CHEF_BUILD_ARCHITECTURE_X86) {
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "i386");
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_X64) {
        if (offset > 0) offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "amd64");
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_ARMHF) {
        if (offset > 0) offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "armhf");
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_ARM64) {
        if (offset > 0) offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "arm64");
    }
    if (arch & CHEF_BUILD_ARCHITECTURE_RISCV64) {
        if (offset > 0) offset += snprintf(buffer + offset, sizeof(buffer) - offset, ", ");
        offset += snprintf(buffer + offset, sizeof(buffer) - offset, "riscv64");
    }
    
    return buffer;
}

int remote_list_main(int argc, char** argv, char** envp, struct bake_command_options* options)
{
    gracht_client_t*          client = NULL;
    enum chef_build_architecture arch_filter = 0;
    int                       status;
    int                       i;

    // Parse arguments
    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 0;
        }
        
        if (!strncmp(argv[i], "--arch=", 7)) {
            arch_filter = __parse_arch(argv[i] + 7);
            if (arch_filter == 0) {
                fprintf(stderr, "bake: invalid architecture: %s\n", argv[i] + 7);
                return -1;
            }
        }
    }

    // Create remote client
    status = remote_client_create(&client);
    if (status != 0) {
        fprintf(stderr, "bake: failed to connect to remote server\n");
        return -1;
    }

    // Call list_agents
    struct gracht_message_context context;
    chef_waiterd_list_agents(client, &context, arch_filter);
    
    int count = 0;
    struct chef_waiter_agent_info agents[32]; // Allocate space for up to 32 agents
    memset(agents, 0, sizeof(agents));
    
    chef_waiterd_list_agents_result(client, &context, &count, agents, 32);

    if (count == 0) {
        printf("No remote agents available");
        if (arch_filter != 0) {
            printf(" for architecture: %s", __arch_to_string(arch_filter));
        }
        printf("\n");
    } else {
        printf("Available Remote Agents:\n");
        for (i = 0; i < count && i < 32; i++) {
            printf("- %s [%s]  Architectures: %s  Load: ",
                agents[i].name,
                agents[i].online ? "online" : "offline",
                __arch_to_string(agents[i].architectures));
            
            if (agents[i].online) {
                printf("%d\n", agents[i].queue_size);
            } else {
                printf("-\n");
            }
        }
    }

    // Cleanup
    for (i = 0; i < count && i < 32; i++) {
        chef_waiter_agent_info_destroy(&agents[i]);
    }

    gracht_client_shutdown(client);
    return 0;
}
