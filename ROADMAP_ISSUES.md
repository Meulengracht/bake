# Roadmap Issues

This document contains GitHub issue descriptions for missing features identified in the README.md roadmap.

## Version 1.5 Release Issues

### Issue 1: Complete Served Daemon Implementation

**Title:** Complete served daemon initial feature implementation

**Labels:** enhancement, daemon, v1.5

**Description:**

The `served` daemon is the application backend for Chef package management that needs to be fully implemented on an OS basis. According to the roadmap, we need to complete the initial feature set for the served daemon.

**Current Status:**
- Linux implementation is in progress and requires libfuse3
- Windows implementation still needs to be done
- The serve protocol specification exists in `protocols/served.gr`

**Acceptance Criteria:**
- [ ] Complete Linux served daemon implementation with full libfuse3 integration
- [ ] Implement all protocol functions defined in served.gr:
  - [ ] install(packageName)
  - [ ] remove(packageName)
  - [ ] info(packageName)
  - [ ] listcount()
  - [ ] list()
  - [ ] update(packageName)
  - [ ] update_all()
- [ ] Implement all protocol events:
  - [ ] package_installed
  - [ ] package_removed
  - [ ] package_updated
- [ ] Ensure served daemon starts correctly on Linux
- [ ] Add comprehensive testing for daemon functionality
- [ ] Update documentation in `daemons/served/README.md`

**Related Files:**
- `daemons/served/`
- `protocols/served.gr`
- `tools/serve/`

---

### Issue 2: Implement Remote Management Commands

**Title:** Add remote management commands: `bake remote list` and `bake remote info`

**Labels:** enhancement, feature, remote, v1.5

**Description:**

Add remote management commands to allow users to query and inspect remote build agents. These commands will provide visibility into available remote build infrastructure.

**Current Status:**
- Remote build functionality exists (`remote init`, `remote build`, `remote resume`, `remote download`)
- Missing: `remote list` and `remote info` commands for agent management

**Required Commands:**

1. **`bake remote list --arch=<architecture>`**
   - List available remote build agents
   - Filter by architecture (optional parameter)
   - Display agent status, capabilities, and availability
   - Example output format:
     ```
     Available Remote Agents:
     - agent-01 [online]  Architectures: amd64, arm64  Load: 2/4
     - agent-02 [online]  Architectures: i386, amd64   Load: 0/4
     - agent-03 [offline] Architectures: arm64, rv64   Load: -
     ```

2. **`bake remote info [agent]`**
   - Display detailed information about a specific remote agent
   - Show supported architectures, platforms, current jobs, capacity
   - Display agent version, capabilities, and health status
   - Example output format:
     ```
     Agent: agent-01
     Status: Online
     Architectures: amd64, arm64
     Platforms: linux, windows
     Current Jobs: 2
     Total Capacity: 4
     Version: 1.5.0
     Uptime: 3 days 4 hours
     Last Health Check: 2 minutes ago
     ```

**Acceptance Criteria:**
- [ ] Implement `bake remote list` command
- [ ] Add `--arch` filter parameter to list command
- [ ] Implement `bake remote info [agent]` command
- [ ] Add appropriate error handling for offline/unavailable agents
- [ ] Include command help text and usage examples
- [ ] Update documentation in README.md
- [ ] Add tests for new commands

**Related Files:**
- `tools/bake/commands/remote.c`
- `tools/bake/commands/remote-helpers/`
- `libs/remote/`

---

## Version 1.6 Release Issues

### Issue 3: Initial Windows Support - Platform Layer

**Title:** Complete Windows platform layer implementation

**Labels:** enhancement, platform, windows, v1.6

**Description:**

Extend Chef to support Windows as a build and target platform. This is the first part of initial Windows support, focusing on completing the platform abstraction layer.

**Current Status:**
- Basic Windows platform code exists in various `*-windows.c` files
- Platform abstraction layer in `libs/platform/` needs Windows-specific implementations completed
- Some tools have partial Windows support

**Scope:**

1. **Platform Layer Completion:**
   - Complete all Windows-specific implementations in platform abstraction layer
   - Ensure proper path handling (Windows vs Unix paths)
   - Implement Windows-specific file operations
   - Add Windows process management
   - Support Windows environment variable handling
   - Implement Windows-specific permission and capability management

2. **Build System:**
   - Ensure CMake builds correctly on Windows
   - Support MSVC and MinGW toolchains
   - Handle Windows-specific library dependencies
   - Test and fix any Windows-specific build issues

**Acceptance Criteria:**
- [ ] All platform abstraction functions work correctly on Windows
- [ ] CMake successfully builds all components on Windows
- [ ] Path handling works correctly for Windows paths
- [ ] Process spawning and management works on Windows
- [ ] Environment variable handling works on Windows
- [ ] File operations (read, write, permissions) work on Windows
- [ ] Unit tests pass on Windows
- [ ] Documentation updated for Windows build instructions

**Related Files:**
- `libs/platform/*-windows.c`
- `CMakeLists.txt`
- Build configuration files

---

### Issue 4: Initial Windows Support - Container Extension

**Title:** Extend containerv to support Windows HCI layer

**Labels:** enhancement, container, windows, v1.6

**Description:**

Extend the containerv (container management) library to support Windows using the Windows Host Compute Service (HCI) layer. This enables isolated build environments on Windows.

**Current Status:**
- Linux container support exists using namespaces, cgroups, and overlayfs
- Windows HCI layer implementation needed
- Container daemon (cvd) exists but needs Windows support

**Scope:**

1. **HCI Integration:**
   - Implement Windows container creation using HCI APIs
   - Support container lifecycle management (create, start, stop, destroy)
   - Implement Windows-specific filesystem isolation
   - Add Windows process isolation in containers
   - Handle Windows-specific networking for containers

2. **Container Daemon:**
   - Extend cvd daemon to support Windows
   - Implement Windows-specific container protocol handling
   - Ensure protocol compatibility with Linux implementation

**Acceptance Criteria:**
- [ ] containerv library supports Windows HCI layer
- [ ] Can create and manage Windows containers
- [ ] Filesystem isolation works on Windows
- [ ] Process isolation works on Windows
- [ ] cvd daemon runs on Windows
- [ ] Protocol operations work consistently across Linux and Windows
- [ ] Integration tests pass on Windows
- [ ] Documentation updated for Windows container support

**Related Files:**
- `libs/containerv/`
- `daemons/cvd/`
- `protocols/cvd.gr`

---

### Issue 5: Initial Windows Support - Kitchen Build Flow

**Title:** Implement Windows build flow in kitchen

**Labels:** enhancement, build, windows, v1.6

**Description:**

Implement the complete build flow in kitchen (the build backend) to support building packages on Windows. This includes supporting Windows-specific build systems and tools.

**Current Status:**
- Kitchen (oven library) supports autotools, cmake, and make on Linux
- Windows build systems need to be added (MSBuild, nmake, etc.)
- Script execution needs PowerShell support on Windows

**Scope:**

1. **Build System Support:**
   - Add MSBuild backend for Windows builds
   - Add nmake support
   - Extend CMake backend to work correctly on Windows
   - Support Visual Studio project generation

2. **Script Execution:**
   - PowerShell script execution for `script` type steps
   - Proper environment variable expansion on Windows
   - Windows command shell support as fallback

3. **Toolchain Support:**
   - Support Windows-based toolchains
   - Handle MSVC, MinGW, and Clang on Windows
   - Proper toolchain path resolution on Windows

**Acceptance Criteria:**
- [ ] MSBuild backend implemented and working
- [ ] CMake backend works correctly on Windows
- [ ] PowerShell scripts execute correctly in recipe steps
- [ ] Windows toolchains can be used for cross-compilation
- [ ] All existing recipe examples build on Windows (where applicable)
- [ ] Environment variables are correctly expanded on Windows
- [ ] Installation prefix handling works on Windows
- [ ] Integration tests pass on Windows
- [ ] Documentation includes Windows-specific build examples

**Related Files:**
- `libs/oven/backends/`
- `libs/oven/`
- `tools/bake/`
- Recipe specification in README.md

---

## Notes

These issues are based on the roadmap in README.md (as of the current version). Each issue is self-contained and can be worked on independently, though there may be some dependencies between them (especially within the Windows support issues which should be tackled in order: platform layer → containerv → kitchen).

To create these issues in GitHub:
1. Go to the repository's Issues page
2. Click "New Issue"
3. Copy the title and description from each issue above
4. Add the suggested labels
5. Assign to appropriate milestone (v1.5 or v1.6)
6. Submit the issue
