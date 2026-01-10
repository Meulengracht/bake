# GitHub Issues for BPF Integration TODOs

This document contains draft GitHub issues for the known TODOs from the BPF integration PR.

---

## Issue 1: Add public iterator API for containerv policy paths

**Priority**: High  
**Labels**: enhancement, refactoring, api

### Description

Currently, the cvd daemon's BPF manager directly includes `policy-internal.h` to access policy structure fields. This creates tight coupling and breaks encapsulation between modules.

### Proposed Solution

Create a public iterator API in the containerv policy module:

```c
typedef int (*containerv_policy_path_callback)(
    const char* path, 
    int access, 
    void* userdata
);

int containerv_policy_foreach_path(
    struct containerv_policy* policy,
    containerv_policy_path_callback callback,
    void* userdata
);
```

### Benefits

- Eliminates need for internal header access
- Improves module boundaries and maintainability
- Provides cleaner abstraction for policy iteration
- Makes it easier to change internal policy representation

### Current Workaround

The BPF manager currently includes `../../../libs/containerv/linux/policy-internal.h` to directly access policy fields. This works but violates encapsulation principles.

### Files Affected

- `libs/containerv/include/chef/containerv/policy.h` - Add new API
- `libs/containerv/linux/policy.c` - Implement iterator
- `daemons/cvd/bpf_manager.c` - Use new API instead of internal header

### Related Code

See `daemons/cvd/bpf_manager.c` lines 33-47 for current workaround and TODO comment.

---

## Issue 2: Extract shared cgroup ID utility function

**Priority**: Medium  
**Labels**: refactoring, code-quality

### Description

The `__get_cgroup_id()` function is duplicated in two places:
1. `daemons/cvd/bpf_manager.c`
2. `libs/containerv/linux/policy-ebpf.c`

This code duplication leads to:
- Maintenance burden (changes must be made in two places)
- Risk of inconsistent behavior if implementations diverge
- Larger codebase footprint

### Proposed Solution

Extract the function to a shared utility module that can be used by both:

**Option 1**: Add to containerv public API
```c
// In libs/containerv/include/chef/containerv/cgroups.h
unsigned long long containerv_cgroup_get_id(const char* hostname);
```

**Option 2**: Add to containerv internal utilities
```c
// In libs/containerv/linux/utils.h
unsigned long long containerv_utils_get_cgroup_id(const char* hostname);
```

### Implementation Notes

The function should include:
- Hostname validation (prevent path traversal)
- Check for dot-prefixed hostnames
- Proper error handling and logging
- Path construction: `/sys/fs/cgroup/{hostname}`
- Use `fstat()` to get inode number as cgroup ID

### Files Affected

- New: `libs/containerv/linux/cgroups.c` (or add to existing utils.c)
- New: `libs/containerv/linux/cgroups.h` (or add to existing utils.h)
- Modified: `daemons/cvd/bpf_manager.c` - Use shared function
- Modified: `libs/containerv/linux/policy-ebpf.c` - Use shared function

### Related Code

- `daemons/cvd/bpf_manager.c` lines 178-231
- `libs/containerv/linux/policy-ebpf.c` lines 133-199

---

## Issue 3: Add configuration support for container security policies

**Priority**: Medium  
**Labels**: enhancement, feature

### Description

Currently, cvd daemon uses a hardcoded minimal default policy for all containers. This limits flexibility and requires code changes to modify security policies.

### Current Behavior

All containers get the same minimal policy with hardcoded system paths:
```c
static const char* DEFAULT_SYSTEM_PATHS[] = {
    "/lib", "/lib64", "/usr/lib",
    "/bin", "/usr/bin",
    "/dev/null", "/dev/zero", "/dev/urandom",
    NULL
};
```

### Proposed Solution

Add multi-level configuration support:

1. **Global Default Policy** (in cvd configuration file)
   ```yaml
   # /etc/chef/cvd.yaml
   security:
     default_policy: minimal  # or build, network, custom
     custom_paths:
       - path: /opt/app
         access: read,execute
   ```

2. **Per-Container Policy** (passed from client)
   ```c
   // In chef_create_parameters
   struct chef_policy_spec {
       enum chef_policy_type type;  // minimal, build, network, custom
       const char** allowed_paths;
       int path_count;
   };
   ```

3. **Policy Profiles**
   - Minimal: Current default
   - Build: Add compiler, build tools access
   - Network: Add SSL certificates, DNS
   - Custom: Fully specified by user

### Benefits

- Flexibility without code changes
- Per-container policy customization
- Support for different workload types
- Better security through least-privilege

### Implementation Steps

1. Extend cvd configuration parser to support policy section
2. Add policy specification to container creation protocol
3. Modify `cvd_create()` to use configured/passed policy
4. Add validation for policy specifications
5. Document policy configuration format

### Files Affected

- `daemons/cvd/config.c` - Parse policy configuration
- `daemons/cvd/server/server.c` - Use configured policies
- `protocols/chef_cvd_service.proto` - Add policy spec to create params
- `docs/` - Add policy configuration documentation

### Related Code

See `daemons/cvd/server/server.c` lines 218-247 for current hardcoded policy.

---

## Issue 4: Optimize BPF map cleanup with entry tracking

**Priority**: Low  
**Labels**: optimization, performance

### Description

Currently, cleaning up BPF map entries on container destroy requires iterating through the entire policy map using `BPF_MAP_GET_NEXT_KEY` to find and delete entries matching the container's cgroup_id. This is O(n) where n is the total number of map entries across all containers.

### Current Behavior

For each container destroy:
1. Iterate through all map entries (potentially thousands)
2. Check if `key.cgroup_id` matches container
3. Delete matching entries

This can be slow if the map contains many entries from multiple containers.

### Proposed Solutions

**Option 1: In-Memory Entry Tracking**
- Track policy entries per container in cvd daemon memory
- Store (dev, ino) pairs during population
- On destroy, delete only tracked entries directly
- Trade memory for speed

**Option 2: Batch Deletion API**
- Add BPF helper for batch deletion by cgroup_id
- Requires kernel 5.6+ for `BPF_MAP_DELETE_BATCH`
- Single syscall to delete multiple entries
- More efficient than iteration

**Option 3: Secondary Index Map**
- Create BPF map: `cgroup_id → list of (dev, ino)`
- Look up entries to delete in O(1)
- Requires managing two maps

### Trade-offs

- Option 1: Simple, uses userspace memory, works on all kernels
- Option 2: Most efficient, requires newer kernel
- Option 3: Complex, more BPF map memory usage

### Recommendation

Start with Option 1 (entry tracking) as it's simple and effective. Consider Option 2 if minimum kernel version is raised to 5.6+.

### Files Affected

- `daemons/cvd/bpf_manager.c` - Add entry tracking
- `daemons/cvd/bpf_manager.h` - Update API if needed

### Related Code

See `daemons/cvd/bpf_manager.c` `cvd_bpf_manager_cleanup_policy()` for current implementation.

---

## Issue 5: Add monitoring and metrics for BPF policy enforcement

**Priority**: Low  
**Labels**: enhancement, monitoring

### Description

Add observability for BPF policy enforcement to help with debugging, performance monitoring, and security auditing.

### Proposed Metrics

1. **Policy Map Metrics**
   - Current entry count per container
   - Total map capacity used
   - Entry addition/deletion rates

2. **Policy Violations**
   - Count of denied file accesses
   - Most frequently denied paths
   - Containers with most violations

3. **Performance Metrics**
   - Map lookup latency (from BPF program)
   - Policy population time
   - Cleanup operation duration

### Proposed Implementation

**Option 1: BPF Stats Map**
```c
struct policy_stats {
    __u64 lookups;
    __u64 hits;
    __u64 misses;
    __u64 denials;
};

// BPF map: cgroup_id → stats
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __type(key, __u64);  // cgroup_id
    __type(value, struct policy_stats);
    __uint(max_entries, 1024);
} stats_map SEC(".maps");
```

**Option 2: Userspace Metrics**
- Track metrics in cvd daemon
- Expose via endpoint or log periodically
- No BPF program changes needed

### Use Cases

- Detect misconfigured policies (high denial rates)
- Capacity planning (map usage trends)
- Performance debugging
- Security audit logs

### Files Affected

- `libs/containerv/linux/bpf/fs-lsm.bpf.c` - Add stats collection
- `daemons/cvd/bpf_manager.c` - Read and expose metrics
- `daemons/cvd/server/api.c` - Add metrics endpoint (optional)

---

## Issue 6: Add automated tests for BPF integration

**Priority**: High  
**Labels**: testing, ci

### Description

Currently, BPF integration testing is entirely manual. This makes it difficult to catch regressions and requires manual setup with BPF LSM support.

### Proposed Test Strategy

1. **Unit Tests**
   - Mock BPF syscalls for testing manager logic
   - Test policy population without kernel support
   - Test cleanup iteration logic
   - Test error handling paths

2. **Integration Tests**
   - Test with mock BPF maps (userspace)
   - Test container lifecycle with BPF manager
   - Test fallback behavior

3. **Functional Tests** (Optional)
   - Require BPF LSM-enabled system
   - Test actual policy enforcement
   - Verify denials work correctly

### Testing Challenges

- BPF LSM requires Linux 5.7+ with specific config
- Need root/CAP_BPF for program loading
- CI runners may not have BPF support

### Proposed Approach

1. Use BPF mocking library (like `libbpf-mock`)
2. Test userspace logic without kernel
3. Add optional functional tests for BPF-enabled systems
4. Document manual testing procedures

### Files to Create

- `daemons/cvd/tests/test_bpf_manager.c` - Unit tests
- `daemons/cvd/tests/test_bpf_integration.c` - Integration tests
- `.github/workflows/bpf-tests.yml` - CI workflow (optional)

### Related Documentation

See `daemons/cvd/IMPLEMENTATION_SUMMARY.md` Testing section for manual test scenarios.

---

## Priority Summary

**High Priority**:
1. Add public iterator API for policy paths (#1)
6. Add automated tests for BPF integration (#6)

**Medium Priority**:
2. Extract shared cgroup ID utility function (#2)
3. Add configuration support for policies (#3)

**Low Priority**:
4. Optimize BPF map cleanup (#4)
5. Add monitoring and metrics (#5)

---

## Implementation Order Recommendation

1. **Issue #6** (Tests) - Establish test infrastructure first
2. **Issue #1** (Iterator API) - Improve code quality
3. **Issue #2** (Shared utilities) - Remove duplication
4. **Issue #3** (Configuration) - Add flexibility
5. **Issue #4** (Optimization) - Performance improvements
6. **Issue #5** (Monitoring) - Observability

This order ensures quality (tests), maintainability (refactoring), then features (configuration, monitoring).
