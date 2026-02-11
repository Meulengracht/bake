/**
 * Copyright, Philip Meulengracht
 *
 * This program is free software : you can redistribute it and / or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
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

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vlog.h>

#include <linux/bpf.h>
#include <bpf/bpf.h>

#include "private.h"

static int __find_in_file(FILE* fp, const char* target)
{
    char  buffer[1024];
    char* ptr = &buffer[0];
    size_t target_len;
    
    if (fgets(buffer, sizeof(buffer), fp) == NULL) {
        return 0;
    }

    if (target == NULL || target[0] == 0) {
        return 0;
    }
    target_len = strlen(target);

    // Look for the target as a complete token (not substring)
    while (ptr) {
        // Find next occurrence of the target
        ptr = strstr(ptr, target);
        if (!ptr) {
            break;
        }
        
        // Check if it's a complete token (surrounded by comma/whitespace/newline/boundaries)
        int isStart = (ptr == &buffer[0] || ptr[-1] == ',' || ptr[-1] == ' ' || ptr[-1] == '\t');
        int isEnd = (ptr[target_len] == '\0' || ptr[target_len] == ',' || ptr[target_len] == '\n' ||
                     ptr[target_len] == ' ' || ptr[target_len] == '\t');
        if (isStart && isEnd) {
            return 1;
        }
        
        // Move past match to continue searching
        ptr += target_len;
    }
    return 0;
}

unsigned long long bpf_get_cgroup_id(const char* hostname)
{
    char               cgroupPath[512];
    int                fd;
    struct stat        st;
    unsigned long long cgroupID;
    const char*        c;
    
    if (hostname == NULL) {
        errno = EINVAL;
        return 0;
    }
    
    // Validate hostname to prevent path traversal
    for (c = hostname; *c; c++) {
        if (!((*c >= 'a' && *c <= 'z') ||
              (*c >= 'A' && *c <= 'Z') ||
              (*c >= '0' && *c <= '9') ||
              *c == '-' || *c == '_' || *c == '.')) {
            VLOG_ERROR("containerv", "bpf_helpers: invalid hostname: %s\n", hostname);
            errno = EINVAL;
            return 0;
        }
    }
    
    // Ensure hostname doesn't start with .
    if (hostname[0] == '.') {
        VLOG_ERROR("containerv", "bpf_helpers: invalid hostname starts with dot: %s\n", hostname);
        errno = EINVAL;
        return 0;
    }
    
    // Build cgroup path
    snprintf(cgroupPath, sizeof(cgroupPath), "/sys/fs/cgroup/%s", hostname);
    
    // Open cgroup directory
    fd = open(cgroupPath, O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        VLOG_ERROR("containerv", "bpf_helpers: failed to open cgroup %s: %s\n", 
                   cgroupPath, strerror(errno));
        return 0;
    }
    
    // Get inode number which serves as cgroup ID
    if (fstat(fd, &st) < 0) {
        VLOG_ERROR("containerv", "bpf_helpers: failed to stat cgroup: %s\n", strerror(errno));
        close(fd);
        return 0;
    }
    
    cgroupID = st.st_ino;
    close(fd);
    
    VLOG_DEBUG("containerv", "bpf_helpers: cgroup %s has ID %llu\n", hostname, cgroupID);
    
    return cgroupID;
}

int bpf_bump_memlock_rlimit(void)
{
    struct rlimit rlim = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    return setrlimit(RLIMIT_MEMLOCK, &rlim);
}

int bpf_check_lsm_available(void)
{
    FILE* fp;
    int   available = 0;
    
    // Check /sys/kernel/security/lsm for "bpf"
    fp = fopen("/sys/kernel/security/lsm", "r");
    if (!fp) {
        VLOG_DEBUG("containerv", "bpf_helpers: cannot read LSM list: %s\n", strerror(errno));
        return 0;
    }
    
    available = __find_in_file(fp, "bpf");
    fclose(fp);
    
    if (!available) {
        VLOG_DEBUG("containerv", "bpf_helpers: BPF LSM not enabled in kernel (add 'bpf' to LSM list)\n");
    }
    
    return available;
}

static int __safe_close(int fd)
{
    if (fd >= 0) {
        return close(fd);
    }
    return 0;
}

int containerv_bpf_sanity_check_pins(void)
{
    int map_fd = bpf_obj_get("/sys/fs/bpf/cvd/policy_map");
    int dir_map_fd = bpf_obj_get("/sys/fs/bpf/cvd/dir_policy_map");
    int basename_map_fd = bpf_obj_get("/sys/fs/bpf/cvd/basename_policy_map");
    int net_create_fd = bpf_obj_get("/sys/fs/bpf/cvd/net_create_map");
    int net_tuple_fd = bpf_obj_get("/sys/fs/bpf/cvd/net_tuple_map");
    int net_unix_fd = bpf_obj_get("/sys/fs/bpf/cvd/net_unix_map");

    __safe_close(map_fd);
    __safe_close(dir_map_fd);
    __safe_close(basename_map_fd);
    __safe_close(net_create_fd);
    __safe_close(net_tuple_fd);
    __safe_close(net_unix_fd);

    if (map_fd < 0 || dir_map_fd < 0 || basename_map_fd < 0 || 
        net_create_fd < 0 || net_tuple_fd < 0 || net_unix_fd < 0) {
        VLOG_WARNING("containerv",
                     "BPF LSM sanity check failed (pinned map=%s, pinned dir_map=%s, pinned link=%s, pinned basename_map=%s, "
                     "pinned net_create=%s, pinned net_tuple=%s, pinned net_unix=%s). "
                     "Enforcement may be misconfigured or stale pins exist.\n",
                     (map_fd >= 0) ? "ok" : "missing",
                     (dir_map_fd >= 0) ? "ok" : "missing",
                     (basename_map_fd >= 0) ? "ok" : "missing",
                     (basename_map_fd >= 0) ? "ok" : "missing",
                     (net_create_fd >= 0) ? "ok" : "missing",
                     (net_tuple_fd >= 0) ? "ok" : "missing",
                     (net_unix_fd >= 0) ? "ok" : "missing");
        errno = ENOENT;
        return -1;
    }

    VLOG_DEBUG("containerv", "BPF LSM sanity check ok (pinned map + link present)\n");
    return 0;
}
