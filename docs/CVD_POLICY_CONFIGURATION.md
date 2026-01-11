# CVD Security Policy Configuration Guide

## Overview

The Chef Container Virtualization Daemon (cvd) supports flexible security policy configuration for containers. This document describes how to configure security policies at both the global and per-container level.

## Configuration File Location

CVD reads its configuration from:
- Linux: `/etc/chef/cvd.json`
- Windows: `%PROGRAMDATA%\chef\cvd.json`

The configuration file is automatically created with default values on first startup if it doesn't exist.

## Configuration Structure

```json
{
  "api-address": {
    "type": "local",
    "address": "@/chef/cvd/api"
  },
  "security": {
    "default_policy": "minimal",
    "custom_paths": [
      {
        "path": "/opt/app",
        "access": "read,execute"
      }
    ]
  }
}
```

## Security Section

### default_policy

**Type**: `string`  
**Default**: `"minimal"` or empty (which means minimal only)  
**Valid values**: `"minimal"`, `"build"`, `"network"`, or comma-separated combinations like `"build,network"`

Specifies the default security policy features for all containers. Policies are **composable building blocks** that extend the base minimal policy.

#### How Policies Work

All containers start with a **minimal base policy** that includes essential syscalls for basic operations. Additional policy features can be added to extend permissions:

- **No policy specified** → Minimal policy only (basic CLI operations)
- **One feature** (e.g., `"build"`) → Minimal + build features
- **Multiple features** (e.g., `"build,network"`) → Minimal + build features + network features

#### Policy Features

- **minimal**: Base policy (always included)
  - Essential syscalls: read, write, open, close, fork, exec, etc.
  - Basic system paths: /lib, /usr/lib, /bin, /usr/bin
  - Device files: /dev/null, /dev/zero, /dev/urandom

- **build**: Adds compilation and build tool paths
  - Additional paths: /usr/include, /usr/share/pkgconfig, /usr/lib/pkgconfig
  - Combined with minimal for full build environment

- **network**: Adds network service paths
  - Additional paths: /etc/ssl, /etc/ca-certificates, /etc/resolv.conf, /etc/hosts
  - Combined with minimal for network-enabled applications

#### Policy Composition Examples

```json
{
  "security": {
    "default_policy": ""  // Empty or "minimal" - basic operations only
  }
}
```

```json
{
  "security": {
    "default_policy": "build"  // Minimal + build paths
  }
}
```

```json
{
  "security": {
    "default_policy": "build,network"  // Minimal + build paths + network paths
  }
}
```

### custom_paths

**Type**: `array of objects`  
**Default**: `[]`

Array of filesystem paths with specific access modes. These paths are added to the container's security policy in addition to the default system paths.

Each path object has the following fields:

#### path
**Type**: `string`  
**Required**: Yes

The filesystem path to allow access to. Currently supports exact path matching.

#### access
**Type**: `string`  
**Required**: Yes  
**Format**: Comma-separated list of access modes

Access modes:
- `read`: Read files and list directories
- `write`: Modify files, create and delete entries
- `execute`: Execute files

**Examples**:
- `"read"` - Read-only access
- `"read,execute"` - Read and execute (for libraries/binaries)
- `"read,write,execute"` - Full access

## Configuration Examples

### Example 1: Minimal Policy (Default)

```json
{
  "security": {
    "default_policy": "minimal"
  }
}
```

Containers get minimal permissions suitable for basic CLI tools.

### Example 2: Build Environment

```json
{
  "security": {
    "default_policy": "build",
    "custom_paths": [
      {
        "path": "/workspace",
        "access": "read,write,execute"
      },
      {
        "path": "/opt/toolchain",
        "access": "read,execute"
      }
    ]
  }
}
```

Containers can perform builds with full access to `/workspace` and read-only access to toolchain.

### Example 3: Web Application

```json
{
  "security": {
    "default_policy": "network",
    "custom_paths": [
      {
        "path": "/etc/ssl",
        "access": "read"
      },
      {
        "path": "/var/www",
        "access": "read"
      },
      {
        "path": "/var/log/app",
        "access": "read,write"
      }
    ]
  }
}
```

Containers can access network, read SSL certificates and web content, and write logs.

### Example 4: Restricted Custom Policy

```json
{
  "security": {
    "default_policy": "custom",
    "custom_paths": [
      {
        "path": "/app",
        "access": "read,execute"
      },
      {
        "path": "/data",
        "access": "read,write"
      }
    ]
  }
}
```

Containers have no default syscalls beyond absolute minimum. Must be combined with explicit syscall configuration in application code.

## Per-Container Policy Override

Containers can override the global default policy by specifying policy features in the create request:

```c
struct chef_create_parameters params = {
    .id = "my-container",
    .layers = layers,
    .layers_count = layer_count,
    .policy = {
        .profiles = "build,network"  // Compose multiple features
    }
};
```

The `profiles` string can specify:
- Empty string or `NULL` - Use global default from configuration
- Single feature: `"build"` - Minimal + build features
- Multiple features: `"build,network"` - Minimal + build + network features
- Just minimal: `"minimal"` or empty when global default has features

### Policy Composition

Policies are **additive building blocks**:
- Base: Minimal policy (always included, provides essential syscalls)
- Features: build, network, etc. (add specific paths and permissions)

**Examples**:
- `""` → Minimal only
- `"build"` → Minimal + build paths (/usr/include, pkgconfig, etc.)
- `"network"` → Minimal + network paths (/etc/ssl, /etc/resolv.conf, etc.)
- `"build,network"` → Minimal + build paths + network paths

**Note**: Custom paths from the global configuration are **always applied** to all containers, in addition to the requested policy features.

## Default System Paths

Regardless of the configured policy, all containers receive access to these essential system paths:

- `/lib` (read, execute)
- `/lib64` (read, execute)
- `/usr/lib` (read, execute)
- `/bin` (read, execute)
- `/usr/bin` (read, execute)
- `/dev/null` (read, execute)
- `/dev/zero` (read, execute)
- `/dev/urandom` (read, execute)

These paths are necessary for basic container functionality and cannot be removed.

## Policy Enforcement

### BPF LSM (Linux Kernel 5.7+)
When available, policies are enforced at the kernel level using BPF LSM:
- Kernel-level enforcement at file_open
- Inode-based (immune to path manipulation)
- Per-container isolation via cgroup IDs

### Seccomp Fallback
When BPF LSM is unavailable:
- Syscall filtering via seccomp-bpf
- Path-based restrictions not enforced (log warning)
- Still provides significant security

### Checking Enforcement Method
CVD logs indicate which enforcement method is active:
```
[DEBUG] policy_ebpf: BPF LSM enforcement active
```
or
```
[DEBUG] policy_ebpf: BPF LSM not available, using seccomp fallback
```

## Best Practices

1. **Start minimal**: Begin with the minimal policy and add permissions as needed
2. **Use appropriate base policy**: Choose the base policy that matches your workload type
3. **Least privilege**: Only grant the access actually required
4. **Test thoroughly**: Verify containers work correctly with the configured policy
5. **Monitor logs**: Watch for policy violations that indicate missing permissions
6. **Document custom paths**: Add comments explaining why each custom path is needed

## Troubleshooting

### Container fails with "Operation not permitted"

The container tried to perform an operation not allowed by the security policy.

**Solution**:
1. Check CVD logs for policy violation details
2. Use `strace` to identify the needed syscall (outside container)
3. Consider switching to a less restrictive base policy (e.g., from minimal to build)
4. Add required custom paths to the configuration

### Build fails inside container

Build operations require more permissions than minimal policy provides.

**Solution**: Use `"build"` policy:
```json
{
  "security": {
    "default_policy": "build"
  }
}
```

### Network application cannot connect

Network operations require network policy.

**Solution**: Use `"network"` policy:
```json
{
  "security": {
    "default_policy": "network"
  }
}
```

### Custom paths not working

Per-container policy overrides ignore global custom_paths.

**Solution**: Either:
1. Use global default policy (don't specify per-container policy)
2. Add paths programmatically using the containerv policy API

## Migration from Hardcoded Policy

If you're upgrading from a version with hardcoded minimal policy:

1. **No action required**: The default configuration uses minimal policy
2. **To use a different default**: Edit `/etc/chef/cvd.json` and set `default_policy`
3. **Restart CVD**: `sudo systemctl restart cvd` (or equivalent)

## Related Documentation

- [Container Security Policies](CONTAINER_SECURITY_POLICIES.md) - Detailed policy system architecture
- [BPF LSM Implementation](BPF_LSM_IMPLEMENTATION.md) - Low-level enforcement details
- Example: `/examples/container-policy-example.c` - Code examples for using policies

## Configuration Validation

CVD validates the configuration on startup:
- Unknown policy types fall back to minimal
- Invalid access modes are logged and ignored
- Malformed JSON prevents CVD startup

Check logs for validation warnings:
```bash
sudo journalctl -u cvd | grep -i policy
```
