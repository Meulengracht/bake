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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <vlog.h>

// Windows Networking API includes
#include <winsock2.h>
#include <iphlpapi.h>
#include <netioapi.h>

#include "private.h"

// PowerShell command buffer size
#define PS_CMD_BUFFER_SIZE 2048

static int __ipv4_netmask_to_prefix(const char* netmask, int* prefix_out)
{
    if (netmask == NULL || prefix_out == NULL) {
        return -1;
    }

    // If the caller already passed a prefix length, accept it.
    int all_digits = 1;
    for (const char* p = netmask; *p; ++p) {
        if (*p < '0' || *p > '9') {
            all_digits = 0;
            break;
        }
    }
    if (all_digits) {
        int v = atoi(netmask);
        if (v < 0 || v > 32) {
            return -1;
        }
        *prefix_out = v;
        return 0;
    }

    unsigned int a = 0, b = 0, c = 0, d = 0;
    if (sscanf(netmask, "%u.%u.%u.%u", &a, &b, &c, &d) != 4) {
        return -1;
    }
    if (a > 255 || b > 255 || c > 255 || d > 255) {
        return -1;
    }

    uint32_t m = ((uint32_t)a << 24) | ((uint32_t)b << 16) | ((uint32_t)c << 8) | (uint32_t)d;

    // Count leading ones; require a contiguous prefix.
    int prefix = 0;
    uint32_t bit = 0x80000000u;
    while (bit != 0 && (m & bit) != 0) {
        prefix++;
        bit >>= 1;
    }
    // Remaining bits must be zero.
    if (bit != 0 && (m & (bit - 1)) != 0) {
        return -1;
    }

    *prefix_out = prefix;
    return 0;
}

/**
 * @brief Execute PowerShell command for network management
 * Windows HyperV networking is primarily managed via PowerShell cmdlets
 */
static int __execute_powershell_command(const char* command)
{
    char ps_command[PS_CMD_BUFFER_SIZE];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exit_code;
    int result;

    // Build PowerShell command with error handling
    snprintf(ps_command, sizeof(ps_command), 
        "powershell.exe -ExecutionPolicy Bypass -NoProfile -Command \""
        "try { %s } catch { Write-Error $_.Exception.Message; exit 1 }\""
        , command);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;  // Hide PowerShell window
    ZeroMemory(&pi, sizeof(pi));

    VLOG_DEBUG("containerv[net]", "executing: %s\n", ps_command);

    // Execute PowerShell command
    result = CreateProcessA(
        NULL,           // Application name
        ps_command,     // Command line
        NULL,           // Process security attributes
        NULL,           // Thread security attributes
        FALSE,          // Inherit handles
        0,              // Creation flags
        NULL,           // Environment
        NULL,           // Current directory
        &si,            // Startup info
        &pi             // Process information
    );

    if (!result) {
        VLOG_ERROR("containerv[net]", "failed to execute PowerShell: %lu\n", GetLastError());
        return -1;
    }

    // Wait for completion
    WaitForSingleObject(pi.hProcess, 30000);  // 30 second timeout

    // Get exit code
    if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
        if (exit_code != 0) {
            VLOG_ERROR("containerv[net]", "PowerShell command failed with exit code: %lu\n", exit_code);
            result = -1;
        } else {
            result = 0;
        }
    } else {
        VLOG_ERROR("containerv[net]", "failed to get PowerShell exit code: %lu\n", GetLastError());
        result = -1;
    }

    // Cleanup
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

/**
 * @brief Create HyperV virtual switch if it doesn't exist
 * Equivalent to Linux bridge creation
 */
int __windows_create_virtual_switch(const char* switch_name, const char* adapter_name)
{
    char command[PS_CMD_BUFFER_SIZE];

    if (!switch_name || strlen(switch_name) == 0) {
        switch_name = "containerv-switch";
    }

    VLOG_DEBUG("containerv[net]", "creating virtual switch: %s\n", switch_name);

    // Check if switch already exists
    snprintf(command, sizeof(command),
        "$switch = Get-VMSwitch -Name '%s' -ErrorAction SilentlyContinue; "
        "if ($switch) { Write-Host 'Switch exists'; exit 0 }; "
        "New-VMSwitch -Name '%s' -SwitchType Internal -Notes 'Created by containerv'; "
        "Write-Host 'Switch created'",
        switch_name, switch_name);

    return __execute_powershell_command(command);
}

/**
 * @brief Configure VM network adapter with IP settings
 * Equivalent to Linux veth configuration
 */
int __windows_configure_vm_network(
    struct containerv_container* container,
    struct containerv_options* options)
{
    char command[PS_CMD_BUFFER_SIZE];
    const char* switch_name;

    if (!container || !options || !options->network.enable) {
        return 0;  // No network configuration needed
    }

    switch_name = options->network.switch_name ? options->network.switch_name : "containerv-switch";

    VLOG_DEBUG("containerv[net]", "configuring VM network for container %s\n", container->id);

    // Create virtual switch if needed
    if (__windows_create_virtual_switch(switch_name, NULL) != 0) {
        VLOG_WARNING("containerv[net]", "failed to create/verify virtual switch, continuing anyway\n");
    }

    // Configure VM network adapter to use the switch
    // This is done through HCS configuration rather than PowerShell for running VMs
    // The actual IP configuration will be done inside the VM via HCS process execution

    VLOG_DEBUG("containerv[net]", "VM network configuration prepared for container %s\n", container->id);
    return 0;
}

/**
 * @brief Configure network inside the VM (equivalent to Linux container network setup)
 */
int __windows_configure_container_network(
    struct containerv_container* container,
    struct containerv_options* options)
{
    struct __containerv_spawn_options spawn_opts = {0};
    int status;
    int exit_code = 0;

    if (!container || !options || !options->network.enable) {
        return 0;
    }

    if (!options->network.container_ip || !options->network.container_netmask) {
        VLOG_ERROR("containerv[net]", "network enabled but IP/netmask not specified\n");
        return -1;
    }

    VLOG_DEBUG("containerv[net]", "configuring network inside VM for container %s\n", container->id);
    VLOG_DEBUG("containerv[net]", "container IP: %s, netmask: %s\n", 
               options->network.container_ip, options->network.container_netmask);

    // Execute inside the guest via pid1d.
    if (container->guest_is_windows) {
        char ps[1024];
        snprintf(
            ps,
            sizeof(ps),
            "$if = (Get-NetAdapter | Where-Object { $_.Status -eq 'Up' -and $_.Name -notlike '*Loopback*' } | Select-Object -First 1).Name; "
            "if (-not $if) { $if = 'Ethernet' }; "
            "netsh interface ip set address name=\"$if\" static %s %s;",
            options->network.container_ip,
            options->network.container_netmask);

        const char* const argv[] = {
            "powershell.exe",
            "-NoProfile",
            "-ExecutionPolicy",
            "Bypass",
            "-Command",
            ps,
            NULL
        };

        spawn_opts.path = "powershell.exe";
        spawn_opts.argv = argv;
        spawn_opts.envv = NULL;
        spawn_opts.flags = CV_SPAWN_WAIT;

        status = __windows_exec_in_vm_via_pid1d(container, &spawn_opts, &exit_code);
        if (status != 0) {
            VLOG_ERROR("containerv[net]", "pid1d guest network config failed (Windows guest)\n");
            return -1;
        }
        if (exit_code != 0) {
            VLOG_ERROR("containerv[net]", "guest network config exited with %d (Windows guest)\n", exit_code);
            return -1;
        }
    } else {
        int prefix = 0;
        if (__ipv4_netmask_to_prefix(options->network.container_netmask, &prefix) != 0) {
            VLOG_ERROR("containerv[net]", "invalid netmask/prefix: %s\n", options->network.container_netmask);
            return -1;
        }

        char sh[1024];
        snprintf(
            sh,
            sizeof(sh),
            "set -e; "
            "IF=; "
            "for c in eth0 ens3 ens4 enp0s3 enp1s0; do ip link show \"$c\" >/dev/null 2>&1 && IF=\"$c\" && break; done; "
            "if [ -z \"$IF\" ]; then IF=$(ip -o link show | awk -F': ' '$2!=\"lo\"{print $2; exit}'); fi; "
            "[ -n \"$IF\" ]; "
            "ip link set dev \"$IF\" up; "
            "ip addr flush dev \"$IF\" 2>/dev/null || true; "
            "ip addr add %s/%d dev \"$IF\";",
            options->network.container_ip,
            prefix);

        const char* const argv[] = { "/bin/sh", "-c", sh, NULL };
        spawn_opts.path = "/bin/sh";
        spawn_opts.argv = argv;
        spawn_opts.envv = NULL;
        spawn_opts.flags = CV_SPAWN_WAIT;

        status = __windows_exec_in_vm_via_pid1d(container, &spawn_opts, &exit_code);
        if (status != 0) {
            VLOG_ERROR("containerv[net]", "pid1d guest network config failed (Linux guest)\n");
            return -1;
        }
        if (exit_code != 0) {
            VLOG_ERROR("containerv[net]", "guest network config exited with %d (Linux guest)\n", exit_code);
            return -1;
        }
    }

    VLOG_DEBUG("containerv[net]", "network configuration completed for container %s\n", container->id);
    return 0;
}

/**
 * @brief Setup host-side network interface (equivalent to Linux host veth)
 */
int __windows_configure_host_network(
    struct containerv_container* container,
    struct containerv_options* options)
{
    char command[PS_CMD_BUFFER_SIZE];
    const char* switch_name;

    if (!container || !options || !options->network.enable) {
        return 0;
    }

    if (!options->network.host_ip) {
        VLOG_DEBUG("containerv[net]", "no host IP specified, skipping host network config\n");
        return 0;
    }

    switch_name = options->network.switch_name ? options->network.switch_name : "containerv-switch";

    VLOG_DEBUG("containerv[net]", "configuring host network interface for switch %s\n", switch_name);

    // Configure the host-side virtual adapter IP
    // This is equivalent to configuring the Linux host veth interface
    snprintf(command, sizeof(command),
        "$adapter = Get-NetAdapter | Where-Object {$_.Name -like '*%s*'} | Select-Object -First 1; "
        "if ($adapter) { "
            "New-NetIPAddress -InterfaceAlias $adapter.Name -IPAddress %s -PrefixLength 24 -ErrorAction SilentlyContinue; "
            "Write-Host 'Host IP configured' "
        "} else { "
            "Write-Warning 'No adapter found for switch' "
        "}",
        switch_name, options->network.host_ip);

    int result = __execute_powershell_command(command);
    if (result != 0) {
        VLOG_WARNING("containerv[net]", "host network configuration may have failed, but continuing\n");
        // Don't fail container creation due to host network config issues
        return 0;
    }

    VLOG_DEBUG("containerv[net]", "host network configuration completed\n");
    return 0;
}

/**
 * @brief Clean up network configuration for container
 */
int __windows_cleanup_network(
    struct containerv_container* container,
    struct containerv_options* options)
{
    // For now, we don't actively clean up the virtual switch
    // as it might be used by other containers
    // In a production implementation, we might:
    // 1. Reference count switch usage
    // 2. Remove switch if no containers are using it
    // 3. Clean up any specific network endpoints

    VLOG_DEBUG("containerv[net]", "network cleanup for container %s (minimal implementation)\n", 
               container ? container->id : "unknown");
    
    return 0;
}
