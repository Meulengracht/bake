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
 * https://github.com/torvalds/linux/blob/master/include/linux/lsm_hook_defs.h
 * sb_mount
 * sb_remount
 * sb_umount
 */

#include <vmlinux.h>

#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>

#include "common.h"
#include "tracing.h"

#include <protecc/bpf/mount.h>

/* Mount permission bits */
#define MOUNT_PERM_MOUNT 0x1

/* Protecc action values (must match protecc_action_t). */
#define PROTECC_ACTION_ALLOW 0u
#define PROTECC_ACTION_DENY  1u

#ifndef PROTECC_PROFILE_MAP_MAX_ENTRIES
#define PROTECC_PROFILE_MAP_MAX_ENTRIES 1024u
#endif

#define MOUNT_TEXT_SMALL_MAX 256u

struct profile_value {
	__u32 size;
	__u8  data[PROTECC_BPF_MAX_PROFILE_SIZE];
};

struct per_cpu_data {
	char source[MOUNT_TEXT_SMALL_MAX];
	char target[PATH_BUFFER_SIZE];
	char fstype[MOUNT_TEXT_SMALL_MAX];
	char options[MOUNT_TEXT_SMALL_MAX];
};

/**
 * @brief BPF map: mount profiles per cgroup
 * The key is cgroup_id, value is a serialized protecc mount profile blob.
 */
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__type(key, __u64);
	__type(value, struct profile_value);
	__uint(max_entries, PROTECC_PROFILE_MAP_MAX_ENTRIES);
} mount_profile_map SEC(".maps");

/* Per-CPU scratch buffer to avoid large stack allocations. */
struct {
	__uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
	__type(key, __u32);
	__type(value, struct per_cpu_data);
	__uint(max_entries, 1);
} per_cpu_data_map SEC(".maps");

static __always_inline struct per_cpu_data* __cpu_data(void)
{
	__u32                key = 0;
	struct per_cpu_data* scratch = bpf_map_lookup_elem(&per_cpu_data_map, &key);
	if (!scratch) {
		return NULL;
	}
	return scratch;
}

static __always_inline __u32 __bounded_str_len(const char* text, __u32 maxLen)
{
	__u32 length = 0;

	if (!text || maxLen == 0) {
		return 0;
	}

	bpf_for (length, 0, MOUNT_TEXT_SMALL_MAX) {
		if (length >= maxLen) {
			break;
		}
		if (text[length] == '\0') {
			return length;
		}
	}

	return maxLen;
}

static __always_inline int __read_user_or_kernel_string(
	const char* in,
	char*       out,
	__u32       outSize,
	__u32*      lenOut)
{
	int read;

	if (!out || !lenOut || outSize == 0) {
		return -EACCES;
	}

	out[0] = '\0';
	*lenOut = 0;

	if (!in) {
		return 0;
	}

	read = bpf_probe_read_str(out, outSize, in);
	if (read <= 0) {
		return -EACCES;
	}

	*lenOut = (__u32)(read - 1);
	return 0;
}

static __always_inline int __check_allow_mount_request(
	__u64                             cgroupId,
	const protecc_bpf_mount_request_t* request,
	__u32                             required,
	__u32                             hookId)
{
	struct profile_value* profile;
	__u8                  action = PROTECC_ACTION_ALLOW;
	bool                  match;

	if (cgroupId == 0) {
		return 0;
	}

	profile = bpf_map_lookup_elem(&mount_profile_map, &cgroupId);
	if (profile == NULL) {
		return 0;
	}

	if (profile->size == 0 || profile->size > PROTECC_BPF_MAX_PROFILE_SIZE) {
		__emit_deny_event_basic(cgroupId, required, hookId);
		return -EACCES;
	}

	match = protecc_bpf_match_mount(profile->data, request, &action);
	if (!match) {
		return 0;
	}

	if (action != PROTECC_ACTION_DENY) {
		return 0;
	}

	__emit_deny_event_basic(cgroupId, required, hookId);
	return -EACCES;
}

SEC("lsm/sb_mount")
int BPF_PROG(sb_mount_restrict,
			 const char* dev_name,
			 const struct path* path,
			 const char* type,
			 unsigned long flags,
			 void* data,
			 int ret)
{
	struct per_cpu_data*        scratch;
	struct dentry*              dentry = NULL;
	protecc_bpf_mount_request_t request = {};
	__u64                       cgroupId;
	__u32                       sourceLen = 0;
	__u32                       targetLen = 0;
	__u32                       fstypeLen = 0;
	__u32                       optionsLen = 0;
	__u32                       targetStart = 0;

	if (ret) {
		return ret;
	}

	cgroupId = get_current_cgroup_id();
	if (cgroupId == 0) {
		return 0;
	}

	scratch = __cpu_data();
	if (!scratch) {
		__emit_deny_event_basic(cgroupId, MOUNT_PERM_MOUNT, DENY_HOOK_SB_MOUNT);
		return -EACCES;
	}

	if (__read_user_or_kernel_string(dev_name, &scratch->source[0], MOUNT_TEXT_SMALL_MAX, &sourceLen) != 0) {
		__emit_deny_event_basic(cgroupId, MOUNT_PERM_MOUNT, DENY_HOOK_SB_MOUNT);
		return -EACCES;
	}

	if (__read_user_or_kernel_string(type, &scratch->fstype[0], MOUNT_TEXT_SMALL_MAX, &fstypeLen) != 0) {
		__emit_deny_event_basic(cgroupId, MOUNT_PERM_MOUNT, DENY_HOOK_SB_MOUNT);
		return -EACCES;
	}

	if (__read_user_or_kernel_string((const char*)data,
									 &scratch->options[0],
									 MOUNT_TEXT_SMALL_MAX,
									 &optionsLen) != 0) {
		scratch->options[0] = '\0';
		optionsLen = 0;
	}

	if (path) {
		CORE_READ_INTO(&dentry, path, dentry);
		if (dentry) {
			targetLen = __resolve_dentry_path(&scratch->target[0], dentry, &targetStart);
			if (targetStart < PATH_BUFFER_SIZE) {
				targetLen = __bounded_str_len(&scratch->target[targetStart], PATH_BUFFER_SIZE - targetStart);
			} else {
				targetLen = 0;
				targetStart = 0;
			}
		}
	}

	request.flags = (__u32)flags;
	request.source.data = (sourceLen > 0) ? (const __u8*)&scratch->source[0] : NULL;
	request.source.len = sourceLen;
	request.target.data = (targetLen > 0) ? (const __u8*)&scratch->target[targetStart] : NULL;
	request.target.len = targetLen;
	request.fstype.data = (fstypeLen > 0) ? (const __u8*)&scratch->fstype[0] : NULL;
	request.fstype.len = fstypeLen;
	request.options.data = (optionsLen > 0) ? (const __u8*)&scratch->options[0] : NULL;
	request.options.len = optionsLen;

	return __check_allow_mount_request(cgroupId, &request, MOUNT_PERM_MOUNT, DENY_HOOK_SB_MOUNT);
}

char LICENSE[] SEC("license") = "GPL";
