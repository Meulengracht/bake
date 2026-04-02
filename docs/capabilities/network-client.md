# network-client

**Type:** System  
**Since:** 0.1.0

## Summary

Declares that this package needs to make outbound network connections. This is the most common network capability — use it for applications that call APIs, connect to databases, fetch resources, or otherwise initiate network communication.

Without this capability, the container has no network interface beyond loopback and cannot reach external hosts.

## Recipe Syntax

### Minimal (allow all outbound)

```yaml
capabilities:
  - name: network-client
```

### With configuration

```yaml
capabilities:
  - name: network-client
    config:
      # Restrict which destinations are reachable (optional).
      # If omitted, all outbound connections are allowed.
      allow:
        - proto: tcp
          ports: [80, 443]
        - proto: udp
          ports: [53]
```

## Configuration Reference

| Key | Type | Required | Default | Description |
|-----|------|----------|---------|-------------|
| `allow` | list | No | Allow all | Restrict outbound to specific protocol/port combinations |
| `allow[].proto` | string | Yes (in entry) | — | Protocol: `tcp` or `udp` |
| `allow[].ports` | list of int | Yes (in entry) | — | Allowed destination ports |

> **Note:** Support for `allow` rule parsing requires a parser enhancement and is tracked as a follow-up.

## What It Enables

### Network Namespace
- Creates a virtual ethernet (veth) pair between host and container
- Assigns an IP address to the container interface
- Configures routing and DNS resolution
- Sets `CV_CAP_NETWORK` on the container options

### Seccomp (syscall filtering)
The base policy already permits the syscalls needed for outbound connections:
- `socket`, `connect`, `sendto`, `recvfrom`, `sendmsg`, `recvmsg`, `setsockopt`, `getsockopt`, `getsockname`, `getpeername`, `shutdown`

These are part of the minimal policy because the container's PID1 process requires a unix domain control socket.

### eBPF — Filesystem
Adds read access to network-related system files via the `"network"` eBPF policy plugin:
- `/etc/ssl` — TLS certificates
- `/etc/ca-certificates` — CA bundle
- `/etc/resolv.conf` — DNS configuration  
- `/etc/hosts` — Host name resolution

### eBPF — Network LSM
When `allow` rules are specified, generates `containerv_policy_net_rule` entries:
- `family`: `AF_INET` / `AF_INET6`
- `protocol`: `IPPROTO_TCP` or `IPPROTO_UDP`
- `port`: from `allow[].ports`
- `allow_mask`: `CV_NET_CONNECT | CV_NET_SEND`

When no `allow` rules are specified, the eBPF network LSM allows all outbound by default (connect/send on any port).

## Interaction with Other Capabilities

| Capability | Relationship |
|---|---|
| `network-server` | Independent. Both create a network namespace, but `network-server` additionally allows bind/listen/accept. A package can declare both if it needs to serve and also make outbound calls to different destinations with separate rules. |
| `service-client` | Does NOT require `network-client`. Service-client uses AF_UNIX sockets which don't need IP networking. |

## Examples

### Simple HTTP client application
```yaml
packs:
  - name: my-cli-tool
    type: application
    capabilities:
      - name: network-client
    commands:
      - name: my-cli-tool
        path: /bin/my-cli-tool
        type: executable
```

### API consumer with restricted outbound
```yaml
packs:
  - name: weather-app
    type: application
    capabilities:
      - name: network-client
        config:
          allow:
            - proto: tcp
              ports: [443]
            - proto: udp
              ports: [53]
    commands:
      - name: weather
        path: /bin/weather
        type: executable
```
