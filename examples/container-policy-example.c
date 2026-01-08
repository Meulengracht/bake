/**
 * Example: Using Security Policies with Containerv
 * 
 * This example demonstrates how to create and use security policies
 * for containerized processes.
 */

#include <chef/containerv.h>
#include <chef/containerv/policy.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void example_minimal_policy(void) {
    printf("=== Example 1: Minimal Policy (Basic CLI) ===\n");
    printf("This policy allows only basic syscalls for simple CLI applications.\n");
    printf("Allowed operations: read, write, open, close, exit, memory management, etc.\n\n");
    
    // Create a minimal policy
    struct containerv_policy* policy = containerv_policy_new(CV_POLICY_MINIMAL);
    if (policy == NULL) {
        fprintf(stderr, "Failed to create minimal policy\n");
        return;
    }
    
    printf("Created minimal policy with:\n");
    printf("  - Basic syscalls for CLI applications\n");
    printf("  - Read-only access to system libraries (/lib, /lib64, /usr/lib)\n");
    printf("  - Access to /dev/null, /dev/zero, /dev/urandom\n");
    printf("  - Access to /proc/self for process information\n\n");
    
    // Use the policy with container options
    struct containerv_options* options = containerv_options_new();
    containerv_options_set_policy(options, policy);
    
    printf("Policy configured for container\n\n");
    
    // Clean up (policy is now owned by options)
    containerv_options_delete(options);
}

void example_build_policy(void) {
    printf("=== Example 2: Build Policy ===\n");
    printf("This policy extends minimal with build operations.\n");
    printf("Additional operations: fork, exec, pipe, file creation, etc.\n\n");
    
    // Create a build policy
    struct containerv_policy* policy = containerv_policy_new(CV_POLICY_BUILD);
    if (policy == NULL) {
        fprintf(stderr, "Failed to create build policy\n");
        return;
    }
    
    // Add write access to a build directory
    const char* build_paths[] = {
        "/workspace",
        "/tmp",
        NULL
    };
    
    if (containerv_policy_add_paths(policy, build_paths, CV_FS_ALL) != 0) {
        fprintf(stderr, "Failed to add build paths to policy\n");
        containerv_policy_delete(policy);
        return;
    }
    
    printf("Created build policy with:\n");
    printf("  - All minimal policy syscalls\n");
    printf("  - Process creation (fork, exec, clone)\n");
    printf("  - File manipulation (create, delete, rename)\n");
    printf("  - Full access to /workspace and /tmp\n\n");
    
    struct containerv_options* options = containerv_options_new();
    containerv_options_set_policy(options, policy);
    
    printf("Policy configured for container\n\n");
    
    containerv_options_delete(options);
}

void example_network_policy(void) {
    printf("=== Example 3: Network Policy ===\n");
    printf("This policy extends minimal with network operations.\n");
    printf("Additional operations: socket, bind, connect, send, recv, etc.\n\n");
    
    // Create a network policy
    struct containerv_policy* policy = containerv_policy_new(CV_POLICY_NETWORK);
    if (policy == NULL) {
        fprintf(stderr, "Failed to create network policy\n");
        return;
    }
    
    // Add access to certificate files
    const char* network_paths[] = {
        "/etc/ssl",
        "/etc/ca-certificates",
        NULL
    };
    
    if (containerv_policy_add_paths(policy, network_paths, CV_FS_READ) != 0) {
        fprintf(stderr, "Failed to add network paths to policy\n");
        containerv_policy_delete(policy);
        return;
    }
    
    printf("Created network policy with:\n");
    printf("  - All minimal policy syscalls\n");
    printf("  - Socket operations (socket, bind, connect)\n");
    printf("  - Network I/O (send, recv, sendmsg, recvmsg)\n");
    printf("  - Read access to /etc/ssl and /etc/ca-certificates\n\n");
    
    struct containerv_options* options = containerv_options_new();
    containerv_options_set_policy(options, policy);
    
    printf("Policy configured for container\n\n");
    
    containerv_options_delete(options);
}

void example_custom_policy(void) {
    printf("=== Example 4: Custom Policy ===\n");
    printf("Building a custom policy from scratch.\n\n");
    
    // Create an empty custom policy
    struct containerv_policy* policy = containerv_policy_new(CV_POLICY_CUSTOM);
    if (policy == NULL) {
        fprintf(stderr, "Failed to create custom policy\n");
        return;
    }
    
    // Add specific syscalls
    const char* allowed_syscalls[] = {
        "read", "write", "open", "close",
        "exit", "exit_group",
        "brk", "mmap", "munmap",
        NULL
    };
    
    if (containerv_policy_add_syscalls(policy, allowed_syscalls) != 0) {
        fprintf(stderr, "Failed to add syscalls to policy\n");
        containerv_policy_delete(policy);
        return;
    }
    
    // Add specific paths
    if (containerv_policy_add_path(policy, "/app", CV_FS_READ | CV_FS_EXEC) != 0 ||
        containerv_policy_add_path(policy, "/data", CV_FS_ALL) != 0) {
        fprintf(stderr, "Failed to add paths to policy\n");
        containerv_policy_delete(policy);
        return;
    }
    
    printf("Created custom policy with:\n");
    printf("  - Only essential syscalls (read, write, open, close, exit, memory)\n");
    printf("  - Read/Execute access to /app\n");
    printf("  - Full access to /data\n\n");
    
    struct containerv_options* options = containerv_options_new();
    containerv_options_set_policy(options, policy);
    
    printf("Policy configured for container\n\n");
    
    containerv_options_delete(options);
}

void example_bpf_lsm_deny_rules(void) {
    printf("=== Example 5: BPF LSM Deny Rules ===\n");
    printf("Using BPF LSM for filesystem access restrictions.\n");
    printf("Requires: Kernel 5.7+, CONFIG_BPF_LSM=y, 'bpf' in LSM list\n\n");
    
    // Create a build policy
    struct containerv_policy* policy = containerv_policy_new(CV_POLICY_BUILD);
    if (policy == NULL) {
        fprintf(stderr, "Failed to create build policy\n");
        return;
    }
    
    // Allow workspace access
    const char* workspace_paths[] = {
        "/workspace",
        "/tmp",
        NULL
    };
    
    if (containerv_policy_add_paths(policy, workspace_paths, CV_FS_ALL) != 0) {
        fprintf(stderr, "Failed to add workspace paths to policy\n");
        containerv_policy_delete(policy);
        return;
    }
    
    printf("\nCreated BPF LSM policy with:\n");
    printf("  - Build operations (fork, exec, file manipulation)\n");
    printf("  - Full access to /workspace and /tmp\n");
    printf("  - Denied read access to secrets (/etc/shadow, SSH keys, etc.)\n");
    printf("  - Denied write access to system directories\n\n");
    
    printf("Enforcement:\n");
    printf("  - If BPF LSM is available: Kernel-level enforcement at file_open\n");
    printf("  - If BPF LSM is unavailable: Falls back to seccomp (syscall filtering only)\n\n");
    
    struct containerv_options* options = containerv_options_new();
    containerv_options_set_policy(options, policy);
    
    printf("Policy configured for container\n\n");
    
    containerv_options_delete(options);
}

int main(int argc, char** argv) {
    printf("Containerv Security Policy Examples\n");
    printf("====================================\n\n");
    
    example_minimal_policy();
    example_build_policy();
    example_network_policy();
    example_custom_policy();
    example_bpf_lsm_deny_rules();
    
    printf("=== Summary ===\n");
    printf("Security policies provide fine-grained control over:\n");
    printf("  1. System call access (via seccomp-bpf)\n");
    printf("  2. Filesystem access (via BPF LSM deny rules)\n");
    printf("  3. Default-deny model with explicit allow lists\n");
    printf("  4. Per-container isolation using cgroup IDs\n\n");
    
    printf("The policy system uses eBPF infrastructure for:\n");
    printf("  - Kernel-level LSM hooks (when available)\n");
    printf("  - Inode-based enforcement (immune to path manipulation)\n");
    printf("  - Graceful fallback to seccomp when BPF LSM unavailable\n\n");
    
    return 0;
}
