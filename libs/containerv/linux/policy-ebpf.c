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

#include "policy-ebpf.h"
#include "policy-internal.h"
#include "private.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <linux/bpf.h>
#include <stdint.h>
#include <vlog.h>

/* Permission bits - matching BPF program definitions */
#define PERM_READ  0x1
#define PERM_WRITE 0x2
#define PERM_EXEC  0x4

/* Policy key: (cgroup_id, dev, ino) - must match BPF program */
struct policy_key {
    unsigned long long cgroup_id;
    unsigned long long dev;
    unsigned long long ino;
};

/* Policy value: permission mask (bit flags for deny) */
struct policy_value {
    unsigned int allow_mask;
};

/* Internal structure to track loaded eBPF programs */
struct policy_ebpf_context {
    int lsm_prog_fd;
    int lsm_link_fd;
    int policy_map_fd;
    unsigned long long cgroup_id;
};

/* eBPF syscall wrapper */
static inline int bpf(int cmd, union bpf_attr *attr, unsigned int size)
{
    return syscall(__NR_bpf, cmd, attr, size);
}

/* Check if BPF LSM is available on the system */
static int check_bpf_lsm_available(void)
{
    FILE *fp;
    char lsm_list[1024];
    int available = 0;
    
    /* Check /sys/kernel/security/lsm for "bpf" */
    fp = fopen("/sys/kernel/security/lsm", "r");
    if (!fp) {
        VLOG_DEBUG("containerv", "policy_ebpf: cannot read LSM list: %s\n", strerror(errno));
        return 0;
    }
    
    if (fgets(lsm_list, sizeof(lsm_list), fp)) {
        /* Look for "bpf" as a complete word (not substring) */
        char *ptr = lsm_list;
        while (ptr) {
            /* Find next occurrence of "bpf" */
            ptr = strstr(ptr, "bpf");
            if (!ptr) break;
            
            /* Check if it's a complete word (surrounded by comma, newline, or string boundaries) */
            int is_start = (ptr == lsm_list || ptr[-1] == ',');
            int is_end = (ptr[3] == '\0' || ptr[3] == ',' || ptr[3] == '\n');
            
            if (is_start && is_end) {
                available = 1;
                break;
            }
            
            ptr += 3;  /* Move past "bpf" to continue searching */
        }
    }
    
    fclose(fp);
    
    if (!available) {
        VLOG_DEBUG("containerv", "policy_ebpf: BPF LSM not enabled in kernel (add 'bpf' to LSM list)\n");
    }
    
    return available;
}

/* Get cgroup ID for the container */
static unsigned long long get_container_cgroup_id(const char* hostname)
{
    char cgroup_path[512];
    int fd;
    struct stat st;
    unsigned long long cgroup_id;
    const char *c;
    
    if (hostname == NULL) {
        errno = EINVAL;
        return 0;
    }
    
    /* Validate hostname to prevent path traversal
     * Only allow alphanumeric, hyphen, underscore, and period */
    for (c = hostname; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') ||
              (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') ||
              *c == '-' || *c == '_' || *c == '.')) {
            VLOG_ERROR("containerv", "policy_ebpf: invalid hostname contains illegal character: %s\n", 
                       hostname);
            errno = EINVAL;
            return 0;
        }
    }
    
    /* Ensure hostname doesn't start with . or .. */
    if (hostname[0] == '.') {
        VLOG_ERROR("containerv", "policy_ebpf: invalid hostname starts with dot: %s\n", hostname);
        errno = EINVAL;
        return 0;
    }
    
    /* Build cgroup path */
    snprintf(cgroup_path, sizeof(cgroup_path), "/sys/fs/cgroup/%s", hostname);
    
    /* Open cgroup directory */
    fd = open(cgroup_path, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to open cgroup %s: %s\n", 
                   cgroup_path, strerror(errno));
        return 0;
    }
    
    /* Get inode number which serves as cgroup ID */
    if (fstat(fd, &st) < 0) {
        VLOG_ERROR("containerv", "policy_ebpf: failed to stat cgroup: %s\n", strerror(errno));
        close(fd);
        return 0;
    }
    
    cgroup_id = st.st_ino;
    close(fd);
    
    VLOG_DEBUG("containerv", "policy_ebpf: cgroup %s has ID %llu\n", hostname, 
               (unsigned long long)cgroup_id);
    
    return cgroup_id;
}

int policy_ebpf_load(
    struct containerv_container*  container,
    struct containerv_policy*     policy)
{
    if (container == NULL || policy == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    VLOG_TRACE("containerv", "policy_ebpf: loading policy (type=%d, syscalls=%d, paths=%d)\n",
              policy->type, policy->syscall_count, policy->path_count);
    
    /* Check if BPF LSM is available */
    if (!check_bpf_lsm_available()) {
        VLOG_DEBUG("containerv", "policy_ebpf: BPF LSM not available, using seccomp fallback\n");
        return 0;  /* Non-fatal, fall back to seccomp */
    }
    
    /* Note: Full BPF LSM program loading would require:
     * 1. Loading the compiled BPF object file (fs_lsm.bpf.o)
     * 2. Getting the policy_map FD from the object
     * 3. Attaching the LSM program
     * 4. Storing FDs in container for cleanup
     * 
     * This is a foundational implementation that will be extended
     * when the BPF program is compiled and packaged.
     * For now, we return success but don't actually load anything.
     */
    
    VLOG_DEBUG("containerv", "policy_ebpf: BPF LSM infrastructure ready, awaiting full implementation\n");
    
    return 0;
}

int policy_ebpf_unload(struct containerv_container* container)
{
    if (container == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    VLOG_DEBUG("containerv", "policy_ebpf: unloading policy\n");
    
    /* In full implementation:
     * 1. Detach eBPF programs
     * 2. Close program FDs
     * 3. Close map FDs
     */
    
    return 0;
}

/**
 * @brief Add a path-based allow rule to the BPF policy map
 * @param policy_map_fd File descriptor of the policy BPF map
 * @param cgroup_id Cgroup ID for the container
 * @param path Filesystem path to allow
 * @param allow_mask Bitmask of allowed permissions (PERM_READ | PERM_WRITE | PERM_EXEC)
 * @return 0 on success, -1 on error
 */
int policy_ebpf_add_path_allow(int policy_map_fd, unsigned long long cgroup_id,
                               const char* path, unsigned int allow_mask)
{
    struct stat st;
    struct policy_key key = {};
    struct policy_value value = {};
    union bpf_attr attr = {};
    
    if (policy_map_fd < 0 || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    /* Resolve path to (dev, ino) */
    if (stat(path, &st) < 0) {
        VLOG_ERROR("containerv", "policy_ebpf_add_path_deny: failed to stat %s: %s\n",
                   path, strerror(errno));
        return -1;
    }
    
    /* Build policy key */
    key.cgroup_id = cgroup_id;
    key.dev = st.st_dev;
    key.ino = st.st_ino;
    
    /* Build policy value */
    value.allow_mask = allow_mask;
    
    /* Update BPF map */
    memset(&attr, 0, sizeof(attr));
    attr.map_fd = policy_map_fd;
    attr.key = (uintptr_t)&key;
    attr.value = (uintptr_t)&value;
    attr.flags = BPF_ANY;
    
    if (bpf(BPF_MAP_UPDATE_ELEM, &attr, sizeof(attr)) < 0) {
        VLOG_ERROR("containerv", "policy_ebpf_add_path_allow: failed to update map: %s\n",
                   strerror(errno));
        return -1;
    }
    
    VLOG_DEBUG("containerv", "policy_ebpf: added allow rule for %s (dev=%lu, ino=%lu, mask=0x%x)\n",
               path, (unsigned long)st.st_dev, (unsigned long)st.st_ino, allow_mask);
    
    return 0;
}

/**
 * @brief Add a path-based deny rule to the BPF policy map
 *
 * Compatibility wrapper around the allow-list map semantics.
 */
int policy_ebpf_add_path_deny(int policy_map_fd, unsigned long long cgroup_id,
                              const char* path, unsigned int deny_mask)
{
    const unsigned int all = (PERM_READ | PERM_WRITE | PERM_EXEC);
    return policy_ebpf_add_path_allow(policy_map_fd, cgroup_id, path, all & ~deny_mask);
}
