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

#include <chef/platform.h>

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

static int __is_ipv6_addr(const char* s)
{
    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    return strchr(s, ':') != NULL;
}

static int __parse_prefix_any(const char* netmask, int* prefix_out)
{
    if (prefix_out == NULL) {
        return -1;
    }
    if (netmask == NULL || netmask[0] == '\0') {
        return -1;
    }

    int all_digits = 1;
    for (const char* p = netmask; *p; ++p) {
        if (*p < '0' || *p > '9') {
            all_digits = 0;
            break;
        }
    }
    if (all_digits) {
        int v = atoi(netmask);
        if (v < 0 || v > 128) {
            return -1;
        }
        *prefix_out = v;
        return 0;
    }

    if (strchr(netmask, '.') != NULL) {
        return __ipv4_netmask_to_prefix(netmask, prefix_out);
    }

    return -1;
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

static char* __powershell_exec_stdout(const char* command)
{
    char ps_command[PS_CMD_BUFFER_SIZE];

    if (command == NULL) {
        return NULL;
    }

    snprintf(
        ps_command,
        sizeof(ps_command),
        "powershell.exe -ExecutionPolicy Bypass -NoProfile -Command \"%s\"",
        command);

    return platform_exec(ps_command);
}

static void __trim_whitespace_inplace(char* s)
{
    if (s == NULL) {
        return;
    }

    // Trim leading
    size_t start = 0;
    while (s[start] == ' ' || s[start] == '\t' || s[start] == '\r' || s[start] == '\n') {
        start++;
    }
    if (start != 0) {
        memmove(s, s + start, strlen(s + start) + 1);
    }

    // Trim trailing
    size_t len = strlen(s);
    while (len > 0) {
        char c = s[len - 1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            s[len - 1] = '\0';
            len--;
            continue;
        }
        break;
    }
}

static char* __ps_escape_single_quoted(const char* s)
{
    if (s == NULL) {
        return platform_strdup("");
    }

    // In PowerShell single-quoted strings, escape ' by doubling it.
    size_t in_len = strlen(s);
    size_t extra = 0;
    for (size_t i = 0; i < in_len; ++i) {
        if (s[i] == '\'') {
            extra++;
        }
    }

    char* out = (char*)calloc(in_len + extra + 1, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < in_len; ++i) {
        if (s[i] == '\'') {
            out[j++] = '\'';
            out[j++] = '\'';
        } else {
            out[j++] = s[i];
        }
    }
    out[j] = '\0';
    return out;
}

static char* __windows_hns_create_and_attach_endpoint(
    const char* container_id,
    const char* switch_name,
    const char* container_ip,
    int container_prefix,
    const char* gateway_ip,
    const char* dns,
    int* policies_applied_out)
{
    // NOTE:
    // - This is best-effort and relies on HNS PowerShell helpers (commonly present on Windows).
    // - When supported by New-HnsEndpoint, we apply static IP/DNS as endpoint parameters.
    char script[PS_CMD_BUFFER_SIZE];

    if (container_id == NULL || container_id[0] == '\0') {
        return NULL;
    }

    if (switch_name == NULL || switch_name[0] == '\0') {
        switch_name = "Default Switch";
    }

    if (policies_applied_out) {
        *policies_applied_out = 0;
    }

    char* esc_sw = __ps_escape_single_quoted(switch_name);
    char* esc_cid = __ps_escape_single_quoted(container_id);
    char* esc_ip = __ps_escape_single_quoted(container_ip ? container_ip : "");
    char* esc_gw = __ps_escape_single_quoted(gateway_ip ? gateway_ip : "");
    char* esc_dns = __ps_escape_single_quoted(dns ? dns : "");
    if (esc_sw == NULL || esc_cid == NULL || esc_ip == NULL || esc_gw == NULL || esc_dns == NULL) {
        free(esc_sw);
        free(esc_cid);
        free(esc_ip);
        free(esc_gw);
        free(esc_dns);
        return NULL;
    }

    snprintf(
        script,
        sizeof(script),
        "$ErrorActionPreference='Stop'; "
        "Import-Module HNS -ErrorAction SilentlyContinue | Out-Null; "
        "$sw='%s'; $cid='%s'; $ip='%s'; $gw='%s'; $dns='%s'; $prefix=%d; "
        "$isV6=($ip -like '*:*'); "
        "$nets=Get-HnsNetwork; "
        "if (-not $nets) { throw 'No HNS networks found' }; "
        "$best=$null; $bestS=-1; "
        "foreach($n in $nets){ "
        "  $s=0; "
        "  if($n.SwitchName -eq $sw){$s=100} elseif($n.Name -eq $sw){$s=90} elseif(($n.SwitchName -like ('*'+$sw+'*')) -or ($n.Name -like ('*'+$sw+'*'))){$s=50}; "
        "  if($n.Type -eq 'NAT'){$s+=20} elseif($n.Type -eq 'ICS'){$s+=15}; "
        "  if($s -gt $bestS){$best=$n; $bestS=$s} "
        "}; "
        "$net=$best; if (-not $net) { throw 'No HNS networks found' }; "
        "$epName=('chef-' + $cid); "
        "$cmd = Get-Command New-HnsEndpoint; "
        "$keys = $cmd.Parameters.Keys; "
        "$hasIp = ($keys -contains 'IpAddress') -or ($keys -contains 'IPAddress'); "
        "$hasPrefix = ($keys -contains 'PrefixLength'); "
        "$hasGw = ($keys -contains 'GatewayAddress') -or ($keys -contains 'Gateway') -or ($keys -contains 'DefaultGateway'); "
        "$hasDns = ($keys -contains 'DnsServerList') -or ($keys -contains 'DNSServerList') -or ($keys -contains 'DnsServers') -or ($keys -contains 'DNSServers'); "
        "$applied = $false; "
        "$p = @{ NetworkId = $net.Id; Name = $epName }; "
        "if ($ip -and $ip.Length -gt 0 -and $hasIp) { $p['IpAddress'] = $ip; $applied = $true }; "
        "if ($prefix -ge 0 -and $hasPrefix) { $p['PrefixLength'] = [int]$prefix; $applied = $true }; "
        "if ($gw -and $gw.Length -gt 0 -and $hasGw) { $p['GatewayAddress'] = $gw; $applied = $true }; "
        "if ($dns -and $dns.Length -gt 0 -and $hasDns) { $p['DnsServerList'] = ($dns -split '[ ,;]+' | Where-Object { $_ -and $_.Length -gt 0 }); $applied = $true }; "
        "$ep = New-HnsEndpoint @p; "
        // If New-HnsEndpoint didn't accept policy fields, attempt to set them post-create.
        "if (-not $applied) { "
        "  $setCmd = Get-Command Set-HnsEndpoint -ErrorAction SilentlyContinue; "
        "  if ($setCmd) { "
        "    $epObj = Get-HnsEndpoint -Id $ep.Id; "
        "    if ($ip -and $ip.Length -gt 0) { $epObj.IpAddress = $ip; $applied = $true }; "
        "    if ($prefix -ge 0) { $epObj.PrefixLength = [int]$prefix; $applied = $true }; "
        "    if ($gw -and $gw.Length -gt 0) { $epObj.GatewayAddress = $gw; $applied = $true }; "
        "    if ($dns -and $dns.Length -gt 0) { $epObj.DnsServerList = ($dns -split '[ ,;]+' | Where-Object { $_ -and $_.Length -gt 0 }); $applied = $true }; "
        "    if ($applied) { Set-HnsEndpoint -InputObject $epObj | Out-Null }; "
        "  } "
        "}; "
        "Attach-HnsEndpoint -EndpointId $ep.Id -ContainerId $cid; "
        "$pp = $env:CHEF_PORTPROXY_PORTS; "
        "if ($pp -and $ip -and $ip.Length -gt 0) { "
        "  $entries = $pp -split ','; "
        "  foreach($e in $entries) { "
        "    $e = $e.Trim(); if (-not $e) { continue }; "
        "    $proto = 'tcp'; "
        "    if ($e -match '^(\d+):(\d+)(/(tcp|udp))?$') { "
        "      $hp = [int]$matches[1]; $cp = [int]$matches[2]; if ($matches[4]) { $proto = $matches[4].ToLower() }; "
        "      if ($proto -ne 'tcp') { continue }; "
        "      if ($isV6) { netsh interface portproxy add v6tov6 listenaddress=:: listenport=$hp connectaddress=$ip connectport=$cp | Out-Null; } "
        "      else { netsh interface portproxy add v4tov4 listenaddress=0.0.0.0 listenport=$hp connectaddress=$ip connectport=$cp | Out-Null; } "
        "    } "
        "  } "
        "}; "
        "Write-Output ($ep.Id + '|' + ([int]$applied));",
        esc_sw,
        esc_cid,
        esc_ip,
        esc_gw,
        esc_dns,
        container_prefix);

    free(esc_sw);
    free(esc_cid);
    free(esc_ip);
    free(esc_gw);
    free(esc_dns);

    char* out = __powershell_exec_stdout(script);
    if (out == NULL) {
        return NULL;
    }

    __trim_whitespace_inplace(out);
    if (out[0] == '\0') {
        free(out);
        return NULL;
    }

    // Parse "<endpointId>|<applied>".
    char* bar = strchr(out, '|');
    if (bar != NULL) {
        *bar = '\0';
        __trim_whitespace_inplace(out);
        char* applied_s = bar + 1;
        __trim_whitespace_inplace(applied_s);
        if (policies_applied_out) {
            *policies_applied_out = atoi(applied_s) ? 1 : 0;
        }
    }

    return out;
}

static int __windows_configure_container_network_in_hcs_container(
    struct containerv_container* container,
    struct containerv_options* options)
{
    if (!container || !options || !options->network.enable) {
        return 0;
    }

    // If the caller didn't request any in-container configuration, skip.
    if (options->network.container_ip == NULL && options->network.dns == NULL) {
        return 0;
    }

    // DNS-only is allowed. Static IP requires at least IP + netmask/prefix.
    if (options->network.container_ip != NULL && options->network.container_netmask == NULL) {
        VLOG_WARNING("containerv[net]", "container_ip provided without container_netmask; skipping static IP configuration\n");
        return 0;
    }

    int ec = 0;
    process_handle_t ph = NULL;

    if (container->guest_is_windows) {
        const char* gateway_ip = options->network.gateway_ip ? options->network.gateway_ip : options->network.host_ip;
        const char* gw = gateway_ip ? gateway_ip : "";
        const char* dns = options->network.dns ? options->network.dns : "";
        const char* ip = options->network.container_ip ? options->network.container_ip : "";
        const char* mask = options->network.container_netmask ? options->network.container_netmask : "";
        const int is_v6 = __is_ipv6_addr(ip);

        // Use netsh for broad compatibility.
        char ps[1024];
        snprintf(
            ps,
            sizeof(ps),
            "$if = (Get-NetAdapter | Where-Object { $_.Status -eq 'Up' -and $_.Name -notlike '*Loopback*' } | Select-Object -First 1).Name; "
            "if (-not $if) { $if = 'Ethernet' }; "
            "$gw = '%s'; $dns = '%s'; $ip = '%s'; $mask = '%s'; "
            "if ($ip -and $ip.Length -gt 0 -and $mask -and $mask.Length -gt 0) { "
            "  if ($ip -like '*:*') { "
            "    netsh interface ipv6 add address interface=\"$if\" address=$ip; "
            "    if ($gw -and $gw.Length -gt 0) { netsh interface ipv6 add route ::/0 \"$if\" $gw; }; "
            "  } else { "
            "    if ($gw -and $gw.Length -gt 0) { "
            "      netsh interface ip set address name=\"$if\" static $ip $mask $gw 1; "
            "    } else { "
            "      netsh interface ip set address name=\"$if\" static $ip $mask none; "
            "    }; "
            "  } "
            "}; "
            "$servers = $dns.Split(' ',',',';') | Where-Object { $_ -and $_.Length -gt 0 }; "
            "if ($servers.Count -gt 0) { "
            "  if ($ip -like '*:*') { "
            "    netsh interface ipv6 add dnsserver interface=\"$if\" address=$servers[0] index=1; "
            "    for ($i = 1; $i -lt $servers.Count; $i++) { netsh interface ipv6 add dnsserver interface=\"$if\" address=$servers[$i] index=($i+1) } "
            "  } else { "
            "    netsh interface ip set dns name=\"$if\" static $servers[0]; "
            "    for ($i = 1; $i -lt $servers.Count; $i++) { netsh interface ip add dns name=\"$if\" addr=$servers[$i] index=($i+1) } "
            "  } "
            "};",
            gw,
            dns,
            ip,
            mask);

        struct containerv_spawn_options sopts = {0};
        sopts.flags = CV_SPAWN_NONE;
        sopts.arguments = "-NoProfile -ExecutionPolicy Bypass";
        // We need -Command <script> as a single argv item; keep quoting in arguments.
        // Append the script using a second spawn to avoid oversize? Keep it simple here.
        char args[1400];
        snprintf(args, sizeof(args), "-NoProfile -ExecutionPolicy Bypass -Command \"%s\"", ps);
        sopts.arguments = args;

        if (containerv_spawn(container, "powershell.exe", &sopts, &ph) != 0) {
            VLOG_WARNING("containerv[net]", "failed to spawn in-container network configuration (Windows container)\n");
            return -1;
        }

        if (containerv_wait(container, ph, &ec) != 0 || ec != 0) {
            VLOG_WARNING("containerv[net]", "in-container network configuration failed (Windows container), exit=%d\n", ec);
            return -1;
        }

        return 0;
    }

    // Linux container (LCOW): configure via /bin/sh.
    const char* gateway_ip = options->network.gateway_ip ? options->network.gateway_ip : options->network.host_ip;
    int prefix = 0;
    if (options->network.container_ip != NULL) {
        if (__parse_prefix_any(options->network.container_netmask, &prefix) != 0) {
            VLOG_WARNING("containerv[net]", "invalid netmask/prefix: %s\n", options->network.container_netmask);
            return -1;
        }
    }

    char sh[1024];
    snprintf(
        sh,
        sizeof(sh),
        "set -e; "
        "IP='%s'; PREFIX='%d'; NETMASK='%s'; GW='%s'; DNS='%s'; "
        "IF=; for d in /sys/class/net/*; do n=${d##*/}; [ \"$n\" = lo ] && continue; IF=\"$n\"; break; done; "
        "[ -n \"$IF\" ]; "
        "if command -v ip >/dev/null 2>&1; then "
        "  ip link set dev \"$IF\" up 2>/dev/null || true; "
        "  if [ -n \"$IP\" ]; then "
        "    if echo \"$IP\" | grep -q ':'; then "
        "      ip -6 addr flush dev \"$IF\" 2>/dev/null || true; ip -6 addr add \"$IP\"/\"$PREFIX\" dev \"$IF\"; "
        "      if [ -n \"$GW\" ]; then ip -6 route replace default via \"$GW\" dev \"$IF\" 2>/dev/null || true; fi; "
        "    else "
        "      ip addr flush dev \"$IF\" 2>/dev/null || true; ip addr add \"$IP\"/\"$PREFIX\" dev \"$IF\"; "
        "      if [ -n \"$GW\" ]; then ip route replace default via \"$GW\" dev \"$IF\" 2>/dev/null || true; fi; "
        "    fi; "
        "  fi; "
        "else "
        "  if [ -n \"$IP\" ]; then ifconfig \"$IF\" \"$IP\" netmask \"$NETMASK\" up; fi; "
        "  if [ -n \"$GW\" ] && command -v route >/dev/null 2>&1; then route add default gw \"$GW\" \"$IF\" 2>/dev/null || true; fi; "
        "fi; "
        "if [ -n \"$DNS\" ]; then rm -f /etc/resolv.conf; for s in $DNS; do echo \"nameserver $s\" >> /etc/resolv.conf; done; fi;",
        options->network.container_ip ? options->network.container_ip : "",
        prefix,
        options->network.container_netmask ? options->network.container_netmask : "",
        gateway_ip ? gateway_ip : "",
        options->network.dns ? options->network.dns : "");

    struct containerv_spawn_options sopts = {0};
    sopts.flags = CV_SPAWN_NONE;
    char args[1200];
    snprintf(args, sizeof(args), "-c \"%s\"", sh);
    sopts.arguments = args;

    if (containerv_spawn(container, "/bin/sh", &sopts, &ph) != 0) {
        VLOG_WARNING("containerv[net]", "failed to spawn in-container network configuration (Linux container)\n");
        return -1;
    }

    if (containerv_wait(container, ph, &ec) != 0 || ec != 0) {
        VLOG_WARNING("containerv[net]", "in-container network configuration failed (Linux container), exit=%d\n", ec);
        return -1;
    }

    return 0;
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
    const char* gateway_ip;
    const char* dns;

    if (!container || !options || !options->network.enable) {
        return 0;
    }

    if (!options->network.container_ip || !options->network.container_netmask) {
        VLOG_ERROR("containerv[net]", "network enabled but IP/netmask not specified\n");
        return -1;
    }

    gateway_ip = options->network.gateway_ip ? options->network.gateway_ip : options->network.host_ip;
    dns = options->network.dns;

    VLOG_DEBUG("containerv[net]", "configuring network inside VM for container %s\n", container->id);
    VLOG_DEBUG("containerv[net]", "container IP: %s, netmask: %s\n", 
               options->network.container_ip, options->network.container_netmask);

    // Execute inside the guest via pid1d.
    if (container->guest_is_windows) {
        char ps[1024];
        const char* gw = gateway_ip ? gateway_ip : "";
        const char* dns_s = dns ? dns : "";

        snprintf(
            ps,
            sizeof(ps),
            "$if = (Get-NetAdapter | Where-Object { $_.Status -eq 'Up' -and $_.Name -notlike '*Loopback*' } | Select-Object -First 1).Name; "
            "if (-not $if) { $if = 'Ethernet' }; "
            "$gw = '%s'; $dns = '%s'; $ip = '%s'; $mask = '%s'; "
            "if ($gw -and $gw.Length -gt 0) { "
            "  netsh interface ip set address name=\"$if\" static $ip $mask $gw 1; "
            "} else { "
            "  netsh interface ip set address name=\"$if\" static $ip $mask none; "
            "}; "
            "$servers = $dns.Split(' ',',',';') | Where-Object { $_ -and $_.Length -gt 0 }; "
            "if ($servers.Count -gt 0) { "
            "  netsh interface ip set dns name=\"$if\" static $servers[0]; "
            "  for ($i = 1; $i -lt $servers.Count; $i++) { netsh interface ip add dns name=\"$if\" addr=$servers[$i] index=($i+1) } "
            "};",
            gw,
            dns_s,
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
        if (__parse_prefix_any(options->network.container_netmask, &prefix) != 0) {
            VLOG_ERROR("containerv[net]", "invalid netmask/prefix: %s\n", options->network.container_netmask);
            return -1;
        }

        char sh[1024];
        snprintf(
            sh,
            sizeof(sh),
            "set -e; "
            "IP='%s'; PREFIX='%d'; NETMASK='%s'; GW='%s'; DNS='%s'; "
            "IF=; "
            "for d in /sys/class/net/*; do n=${d##*/}; [ \"$n\" = lo ] && continue; IF=\"$n\"; break; done; "
            "[ -n \"$IF\" ]; "
            "if command -v ip >/dev/null 2>&1; then "
            "  ip link set dev \"$IF\" up 2>/dev/null || true; "
            "  if echo \"$IP\" | grep -q ':'; then "
            "    ip -6 addr flush dev \"$IF\" 2>/dev/null || true; "
            "    ip -6 addr add \"$IP\"/\"$PREFIX\" dev \"$IF\"; "
            "    if [ -n \"$GW\" ]; then ip -6 route replace default via \"$GW\" dev \"$IF\" 2>/dev/null || true; fi; "
            "  else "
            "    ip addr flush dev \"$IF\" 2>/dev/null || true; "
            "    ip addr add \"$IP\"/\"$PREFIX\" dev \"$IF\"; "
            "    if [ -n \"$GW\" ]; then ip route replace default via \"$GW\" dev \"$IF\" 2>/dev/null || true; fi; "
            "  fi; "
            "else "
            "  ifconfig \"$IF\" \"$IP\" netmask \"$NETMASK\" up; "
            "  if [ -n \"$GW\" ] && command -v route >/dev/null 2>&1; then route add default gw \"$GW\" \"$IF\" 2>/dev/null || true; fi; "
            "fi; "
            "if [ -n \"$DNS\" ]; then "
            "  rm -f /etc/resolv.conf; "
            "  for s in $DNS; do echo \"nameserver $s\" >> /etc/resolv.conf; done; "
            "fi;",
            options->network.container_ip,
            prefix,
            options->network.container_netmask,
            gateway_ip ? gateway_ip : "",
            dns ? dns : "");

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
    int host_prefix = 24;

    if (!container || !options || !options->network.enable) {
        return 0;
    }

    if (!options->network.host_ip) {
        VLOG_DEBUG("containerv[net]", "no host IP specified, skipping host network config\n");
        return 0;
    }

    switch_name = options->network.switch_name ? options->network.switch_name : "containerv-switch";

    if (options->network.container_netmask != NULL) {
        (void)__ipv4_netmask_to_prefix(options->network.container_netmask, &host_prefix);
    }

    VLOG_DEBUG("containerv[net]", "configuring host network interface for switch %s\n", switch_name);

    // Configure the host-side virtual adapter IP
    // This is equivalent to configuring the Linux host veth interface
    snprintf(command, sizeof(command),
        "$adapter = Get-NetAdapter | Where-Object {$_.Name -like '*%s*'} | Select-Object -First 1; "
        "if ($adapter) { "
            "New-NetIPAddress -InterfaceAlias $adapter.Name -IPAddress %s -PrefixLength %d -ErrorAction SilentlyContinue; "
            "Write-Host 'Host IP configured' "
        "} else { "
            "Write-Warning 'No adapter found for switch' "
        "}",
        switch_name, options->network.host_ip, host_prefix);

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
    (void)options;

    // Clean up any HCS container-mode endpoint we created.
    if (container && container->hns_endpoint_id && container->hns_endpoint_id[0] != '\0') {
        char script[PS_CMD_BUFFER_SIZE];
        snprintf(
            script,
            sizeof(script),
            "$ErrorActionPreference='SilentlyContinue'; "
            "Import-Module HNS -ErrorAction SilentlyContinue | Out-Null; "
            "$id='%s'; $cid='%s'; "
            "try { Detach-HnsEndpoint -EndpointId $id -ContainerId $cid | Out-Null } catch {} ; "
            "try { Remove-HnsEndpoint -Id $id | Out-Null } catch {} ;",
            container->hns_endpoint_id,
            container->id);

        (void)__execute_powershell_command(script);

        free(container->hns_endpoint_id);
        container->hns_endpoint_id = NULL;
    }

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

int __windows_configure_hcs_container_network(
    struct containerv_container* container,
    struct containerv_options* options)
{
    if (!container || !options || !options->network.enable) {
        return 0;
    }

    // Only valid for true container compute systems.
    if (container->hcs_system == NULL || container->hcs_is_vm) {
        return 0;
    }

    if (container->network_configured) {
        return 0;
    }

    const char* switch_name = options->network.switch_name ? options->network.switch_name : "Default Switch";

    int prefix = -1;
    if (options->network.container_ip != NULL && options->network.container_netmask != NULL) {
        if (__ipv4_netmask_to_prefix(options->network.container_netmask, &prefix) != 0) {
            prefix = -1;
        }
    }

    const char* gw = options->network.gateway_ip ? options->network.gateway_ip : options->network.host_ip;

    VLOG_DEBUG("containerv[net]", "creating/attaching HNS endpoint for compute system %s on switch %s\n", container->id, switch_name);

    int policies_applied = 0;
    char* endpoint_id = __windows_hns_create_and_attach_endpoint(
        container->id,
        switch_name,
        options->network.container_ip,
        prefix,
        gw,
        options->network.dns,
        &policies_applied);
    if (endpoint_id == NULL) {
        VLOG_WARNING("containerv[net]", "failed to create/attach HNS endpoint; container may have no network\n");
        return -1;
    }

    container->hns_endpoint_id = endpoint_id;

    VLOG_DEBUG("containerv[net]", "attached HNS endpoint %s\n", container->hns_endpoint_id);

    if (!policies_applied) {
        // Fallback: configure static IP/DNS inside the container (best-effort).
        if (__windows_configure_container_network_in_hcs_container(container, options) != 0) {
            // Keep endpoint attached; allow caller to proceed.
            VLOG_WARNING("containerv[net]", "container networking may be incomplete (HNS policy unsupported; in-container setup failed)\n");
        }
    } else {
        VLOG_DEBUG("containerv[net]", "HNS endpoint policies applied; skipping in-container network configuration\n");
    }

    container->network_configured = 1;
    return 0;
}
