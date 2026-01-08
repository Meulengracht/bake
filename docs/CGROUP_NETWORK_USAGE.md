# Cgroup and Network Isolation Usage Guide

This document describes how to use the cgroup and network isolation features in containerv.

## Cgroup Resource Limits

Cgroups allow you to limit container resource usage. Configure limits using `containerv_options_set_cgroup_limits()`:

```c
#include <chef/containerv.h>

struct containerv_options* options = containerv_options_new();

// Enable cgroup capability
containerv_options_set_caps(options, CV_CAP_CGROUPS | CV_CAP_FILESYSTEM);

// Configure resource limits
// memory_max: "1G", "512M", "max" (for no limit)
// cpu_weight: 1-10000 (default 100, higher = more CPU time)
// pids_max: "256", "max" (for no limit)
containerv_options_set_cgroup_limits(
    options,
    "1G",      // 1GB memory limit
    "100",     // Standard CPU weight
    "256"      // Max 256 processes
);

// Create container with these limits
struct containerv_container* container;
int status = containerv_create("/path/to/rootfs", options, &container);
```

### Cgroup Parameters

- **memory_max**: Maximum memory the container can use
  - Examples: `"512M"`, `"1G"`, `"2G"`, `"max"` (unlimited)
  - Default: `"1G"` if not specified

- **cpu_weight**: Relative CPU time allocation (cgroup v2)
  - Range: 1-10000
  - Default: `"100"` if not specified
  - Higher values = more CPU time relative to other cgroups

- **pids_max**: Maximum number of processes/threads
  - Examples: `"64"`, `"256"`, `"1024"`, `"max"` (unlimited)
  - Default: `"256"` if not specified

## Network Isolation

Network isolation creates a separate network namespace for the container with a virtual ethernet (veth) pair connecting it to the host.

```c
#include <chef/containerv.h>

struct containerv_options* options = containerv_options_new();

// Enable network capability
containerv_options_set_caps(options, CV_CAP_NETWORK | CV_CAP_FILESYSTEM);

// Configure networking with a virtual bridge
containerv_options_set_network(
    options,
    "10.0.0.2",        // Container IP address
    "255.255.255.0",   // Network mask
    "10.0.0.1"         // Host-side veth IP (gateway for container)
);

// Create container with networking
struct containerv_container* container;
int status = containerv_create("/path/to/rootfs", options, &container);
```

### Network Configuration

The network setup creates:
- A veth pair (virtual ethernet devices)
- Host-side interface: `veth{container_id}` with IP from `host_ip` parameter
- Container-side interface: `veth{partial_id}c` with IP from `container_ip` parameter
- Loopback interface (`lo`) at `127.0.0.1` inside the container

### Network Parameters

- **container_ip**: IP address for the container's network interface
  - Must be in the same subnet as host_ip
  - Example: `"10.0.0.2"`, `"192.168.100.10"`

- **container_netmask**: Subnet mask for the network
  - Example: `"255.255.255.0"`, `"255.255.0.0"`

- **host_ip**: IP address for the host-side veth interface
  - Acts as the gateway for the container
  - Must be in the same subnet as container_ip
  - Example: `"10.0.0.1"`, `"192.168.100.1"`

## Combining Cgroup and Network Isolation

You can enable both features simultaneously:

```c
#include <chef/containerv.h>

struct containerv_options* options = containerv_options_new();

// Enable multiple capabilities
containerv_options_set_caps(options, 
    CV_CAP_CGROUPS | CV_CAP_NETWORK | CV_CAP_FILESYSTEM | CV_CAP_PROCESS_CONTROL
);

// Configure resource limits
containerv_options_set_cgroup_limits(options, "2G", "200", "512");

// Configure networking
containerv_options_set_network(options, "10.0.0.2", "255.255.255.0", "10.0.0.1");

// Create fully isolated container
struct containerv_container* container;
int status = containerv_create("/path/to/rootfs", options, &container);

if (status == 0) {
    // Container is now running with:
    // - Limited to 2GB RAM
    // - CPU weight of 200 (2x normal)
    // - Max 512 processes
    // - Network isolated with veth bridge
    
    // Use the container...
    
    // Cleanup (automatically removes cgroups and network interfaces)
    containerv_destroy(container);
}

containerv_options_delete(options);
```

## Default Values

If you enable `CV_CAP_CGROUPS` but don't call `containerv_options_set_cgroup_limits()`:
- Memory: 1GB
- CPU weight: 100
- PIDs: 256

Network isolation requires explicit configuration via `containerv_options_set_network()`.

## Notes

- Cgroups require cgroup v2 to be mounted at `/sys/fs/cgroup`
- Network isolation requires `CAP_NET_ADMIN` capability
- Both features require root privileges or appropriate capabilities
- Cgroups are automatically cleaned up when the container is destroyed
- Network interfaces are automatically removed when the container is destroyed
