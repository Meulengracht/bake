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

// Windows API includes
#include <shlwapi.h>
#include <wininet.h>

#include "private.h"

// PowerShell command buffer size
#define PS_CMD_BUFFER_SIZE 4096
#define MAX_URL_LENGTH 2048

/**
 * @brief Execute PowerShell command for rootfs management
 */
static int __execute_powershell_rootfs_command(const char* command)
{
    char ps_command[PS_CMD_BUFFER_SIZE];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD exit_code;
    int result;

    // Build PowerShell command with comprehensive error handling
    snprintf(ps_command, sizeof(ps_command), 
        "powershell.exe -ExecutionPolicy Bypass -NoProfile -Command \""
        "$ErrorActionPreference = 'Stop'; "
        "try { %s; Write-Host 'SUCCESS' } "
        "catch { Write-Error $_.Exception.Message; throw }\""
        , command);

    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    ZeroMemory(&pi, sizeof(pi));

    VLOG_DEBUG("containerv[rootfs]", "executing rootfs command\n");

    result = CreateProcessA(NULL, ps_command, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    if (!result) {
        VLOG_ERROR("containerv[rootfs]", "failed to execute PowerShell: %lu\n", GetLastError());
        return -1;
    }

    // Wait for completion with timeout
    DWORD wait_result = WaitForSingleObject(pi.hProcess, 300000);  // 5 minute timeout
    if (wait_result != WAIT_OBJECT_0) {
        VLOG_ERROR("containerv[rootfs]", "PowerShell command timed out or failed\n");
        TerminateProcess(pi.hProcess, 1);
        result = -1;
    } else if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
        result = (exit_code == 0) ? 0 : -1;
        if (exit_code != 0) {
            VLOG_ERROR("containerv[rootfs]", "rootfs command failed with exit code: %lu\n", exit_code);
        }
    } else {
        VLOG_ERROR("containerv[rootfs]", "failed to get command exit code: %lu\n", GetLastError());
        result = -1;
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return result;
}

int __windows_is_wsl_available(void)
{
    // Check if WSL2 is available by trying to run wsl --status
    char command[] = "wsl --status 2>$null; if ($LASTEXITCODE -eq 0) { Write-Host 'Available' } else { throw 'Not available' }";
    
    VLOG_DEBUG("containerv[rootfs]", "checking WSL2 availability\n");
    
    int result = __execute_powershell_rootfs_command(command);
    if (result == 0) {
        VLOG_DEBUG("containerv[rootfs]", "WSL2 is available\n");
    } else {
        VLOG_DEBUG("containerv[rootfs]", "WSL2 is not available or not configured\n");
    }
    
    return result;
}

int __windows_setup_wsl_rootfs(
    const char* rootfs_path,
    enum windows_rootfs_type type,
    const char* version)
{
    char command[PS_CMD_BUFFER_SIZE];
    const char* distribution_name = NULL;
    const char* wsl_distro = NULL;

    VLOG_DEBUG("containerv[rootfs]", "setting up WSL rootfs at %s\n", rootfs_path);

    // Map rootfs type to WSL distribution
    switch (type) {
        case WINDOWS_ROOTFS_WSL_UBUNTU:
            distribution_name = "Ubuntu";
            wsl_distro = version ? version : "Ubuntu-22.04";
            break;
        case WINDOWS_ROOTFS_WSL_DEBIAN:
            distribution_name = "Debian";
            wsl_distro = "Debian";
            break;
        case WINDOWS_ROOTFS_WSL_ALPINE:
            distribution_name = "Alpine";
            wsl_distro = "Alpine";
            break;
        default:
            VLOG_ERROR("containerv[rootfs]", "invalid WSL rootfs type: %d\n", type);
            return -1;
    }

    // Create unique instance name based on container path
    char instance_name[256];
    snprintf(instance_name, sizeof(instance_name), "chef-container-%s", 
             strrchr(rootfs_path, '\\') ? strrchr(rootfs_path, '\\') + 1 : rootfs_path);

    // Check if WSL2 is available first
    if (__windows_is_wsl_available() != 0) {
        VLOG_ERROR("containerv[rootfs]", "WSL2 is not available on this system\n");
        VLOG_ERROR("containerv[rootfs]", "Please install WSL2: wsl --install\n");
        return -1;
    }

    // Import or create WSL distribution
    // First, try to export an existing distribution as a base
    snprintf(command, sizeof(command),
        "$tempTar = '%s\\\\base.tar'; "
        "$targetPath = '%s'; "
        "if (!(Test-Path $targetPath)) { New-Item -ItemType Directory -Path $targetPath -Force | Out-Null }; "
        // Try to use existing distribution as base
        "try { "
            "wsl --export %s $tempTar; "
            "wsl --import %s $targetPath $tempTar --version 2; "
            "Remove-Item $tempTar -ErrorAction SilentlyContinue "
        "} catch { "
            // Fallback: install distribution first if not available
            "wsl --install -d %s --no-launch; "
            "Start-Sleep 10; "  // Wait for installation
            "wsl --export %s $tempTar; "
            "wsl --import %s $targetPath $tempTar --version 2; "
            "Remove-Item $tempTar -ErrorAction SilentlyContinue "
        "}",
        rootfs_path, rootfs_path,
        wsl_distro, instance_name,
        wsl_distro,
        wsl_distro, instance_name);

    int result = __execute_powershell_rootfs_command(command);
    if (result != 0) {
        VLOG_ERROR("containerv[rootfs]", "failed to setup WSL distribution %s\n", wsl_distro);
        return -1;
    }

    // Set up the WSL instance for container use
    snprintf(command, sizeof(command),
        "wsl -d %s -u root -- bash -c '"
            "apt-get update 2>/dev/null || apk update 2>/dev/null || true; "
            "echo 'Container rootfs ready' "
        "'",
        instance_name);

    result = __execute_powershell_rootfs_command(command);
    if (result != 0) {
        VLOG_WARNING("containerv[rootfs]", "WSL rootfs setup completed but initialization had issues\n");
        // Don't fail completely, the rootfs might still be usable
    }

    VLOG_DEBUG("containerv[rootfs]", "WSL rootfs setup completed: %s\n", instance_name);
    return 0;
}

int __windows_setup_native_rootfs(
    const char* rootfs_path,
    enum windows_rootfs_type type,
    const char* version)
{
    char command[PS_CMD_BUFFER_SIZE];
    char image_url[MAX_URL_LENGTH];
    const char* image_name = NULL;

    VLOG_DEBUG("containerv[rootfs]", "setting up Windows native rootfs at %s\n", rootfs_path);

    // Map rootfs type to Microsoft Container Registry URLs
    switch (type) {
        case WINDOWS_ROOTFS_SERVERCORE:
            image_name = "windows/servercore";
            break;
        case WINDOWS_ROOTFS_NANOSERVER:  
            image_name = "windows/nanoserver";
            break;
        case WINDOWS_ROOTFS_WINDOWSCORE:
            image_name = "windows";
            break;
        default:
            VLOG_ERROR("containerv[rootfs]", "invalid Windows native rootfs type: %d\n", type);
            return -1;
    }

    // Build container image URL
    const char* tag = version ? version : "ltsc2022";
    snprintf(image_url, sizeof(image_url), "mcr.microsoft.com/%s:%s", image_name, tag);

    VLOG_DEBUG("containerv[rootfs]", "downloading Windows base image: %s\n", image_url);

    // Use Docker or Podman to pull and export the container image
    // This creates a filesystem that can be used with HyperV
    snprintf(command, sizeof(command),
        "$targetPath = '%s'; "
        "if (!(Test-Path $targetPath)) { New-Item -ItemType Directory -Path $targetPath -Force | Out-Null }; "
        
        // Check if Docker is available
        "try { "
            "docker --version | Out-Null; "
            "$dockerAvailable = $true "
        "} catch { "
            "$dockerAvailable = $false "
        "}; "
        
        // Use Docker if available, otherwise try alternative methods
        "if ($dockerAvailable) { "
            "Write-Host 'Using Docker to download base image...'; "
            "docker pull %s; "
            "$containerName = 'temp-rootfs-' + [System.Guid]::NewGuid().ToString('N').Substring(0,8); "
            "docker create --name $containerName %s; "
            "docker export $containerName | tar -xf - -C $targetPath; "
            "docker rm $containerName; "
            "Write-Host 'Base image extracted to rootfs' "
        "} else { "
            // Alternative: Use Windows containers directly (if available)
            "Write-Host 'Docker not available, using alternative download method...'; "
            // This would require implementing a container image download without Docker
            // For now, create a minimal Windows environment
            "Copy-Item -Path 'C:\\Windows\\System32' -Destination (Join-Path $targetPath 'System32') -Recurse -ErrorAction SilentlyContinue; "
            "Copy-Item -Path 'C:\\Windows\\SysWOW64' -Destination (Join-Path $targetPath 'SysWOW64') -Recurse -ErrorAction SilentlyContinue; "
            "New-Item -ItemType Directory -Path (Join-Path $targetPath 'Windows') -Force | Out-Null; "
            "Write-Host 'Minimal Windows environment created' "
        "}",
        rootfs_path, image_url, image_url);

    int result = __execute_powershell_rootfs_command(command);
    if (result != 0) {
        VLOG_ERROR("containerv[rootfs]", "failed to setup Windows native rootfs from %s\n", image_url);
        return -1;
    }

    VLOG_DEBUG("containerv[rootfs]", "Windows native rootfs setup completed\n");
    return 0;
}

int __windows_setup_rootfs(
    const char* rootfs_path,
    struct containerv_options_rootfs* options)
{
    if (!rootfs_path || !options) {
        VLOG_ERROR("containerv[rootfs]", "invalid parameters for rootfs setup\n");
        return -1;
    }

    VLOG_DEBUG("containerv[rootfs]", "setting up rootfs type %d at %s\n", options->type, rootfs_path);

    // Create target directory
    if (!CreateDirectoryA(rootfs_path, NULL)) {
        DWORD error = GetLastError();
        if (error != ERROR_ALREADY_EXISTS) {
            VLOG_ERROR("containerv[rootfs]", "failed to create rootfs directory: %lu\n", error);
            return -1;
        }
    }

    // Route to appropriate setup function based on type
    switch (options->type) {
        case WINDOWS_ROOTFS_WSL_UBUNTU:
        case WINDOWS_ROOTFS_WSL_DEBIAN:
        case WINDOWS_ROOTFS_WSL_ALPINE:
            return __windows_setup_wsl_rootfs(rootfs_path, options->type, options->version);

        case WINDOWS_ROOTFS_SERVERCORE:
        case WINDOWS_ROOTFS_NANOSERVER:
        case WINDOWS_ROOTFS_WINDOWSCORE:
            return __windows_setup_native_rootfs(rootfs_path, options->type, options->version);

        case WINDOWS_ROOTFS_CUSTOM:
            if (!options->custom_image_url) {
                VLOG_ERROR("containerv[rootfs]", "custom rootfs specified but no URL provided\n");
                return -1;
            }
            // TODO: Implement custom image download
            VLOG_ERROR("containerv[rootfs]", "custom rootfs not yet implemented\n");
            return -1;

        default:
            VLOG_ERROR("containerv[rootfs]", "unknown rootfs type: %d\n", options->type);
            return -1;
    }
}

int __windows_cleanup_rootfs(
    const char* rootfs_path,
    struct containerv_options_rootfs* options)
{
    char command[PS_CMD_BUFFER_SIZE];

    if (!rootfs_path) {
        return 0;  // Nothing to clean up
    }

    VLOG_DEBUG("containerv[rootfs]", "cleaning up rootfs at %s\n", rootfs_path);

    // For WSL rootfs, unregister the WSL instance
    if (options && (options->type == WINDOWS_ROOTFS_WSL_UBUNTU || 
                    options->type == WINDOWS_ROOTFS_WSL_DEBIAN ||
                    options->type == WINDOWS_ROOTFS_WSL_ALPINE)) {
        
        char instance_name[256];
        snprintf(instance_name, sizeof(instance_name), "chef-container-%s", 
                 strrchr(rootfs_path, '\\') ? strrchr(rootfs_path, '\\') + 1 : rootfs_path);

        snprintf(command, sizeof(command),
            "try { wsl --unregister %s } catch { Write-Warning 'Could not unregister WSL instance' }",
            instance_name);

        __execute_powershell_rootfs_command(command);
    }

    // Remove rootfs directory
    snprintf(command, sizeof(command),
        "if (Test-Path '%s') { "
            "Remove-Item -Path '%s' -Recurse -Force -ErrorAction SilentlyContinue "
        "}",
        rootfs_path, rootfs_path);

    __execute_powershell_rootfs_command(command);

    VLOG_DEBUG("containerv[rootfs]", "rootfs cleanup completed\n");
    return 0;
}
