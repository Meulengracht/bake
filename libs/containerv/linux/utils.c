/**
 * Copyright 2024, Philip Meulengracht
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

#include <chef/platform.h>
#include <linux/capability.h>
#include <linux/prctl.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>
#include "private.h"
#include <vlog.h>

typedef uint64_t __cap_mask;
#define __CAP_TO_MASK(cap) ((__cap_mask)1 << cap)

struct __capabilities {
    __cap_mask effective;
    __cap_mask permitted;
    __cap_mask inheritable;
};

// caps we need to initialize the container
static const __cap_mask __container_caps =
    __CAP_TO_MASK(CAP_CHOWN) |
    __CAP_TO_MASK(CAP_DAC_OVERRIDE) |
    __CAP_TO_MASK(CAP_FOWNER) |
    __CAP_TO_MASK(CAP_FSETID) |
    __CAP_TO_MASK(CAP_SETPCAP) |
    __CAP_TO_MASK(CAP_NET_ADMIN) |
    __CAP_TO_MASK(CAP_SYS_ADMIN) |
    __CAP_TO_MASK(CAP_SYS_CHROOT);

// caps we keep for the primary process
static const __cap_mask __primary_caps =
    __CAP_TO_MASK(CAP_SETGID) |
    __CAP_TO_MASK(CAP_SETUID);

static int __set_capabilities(const struct __capabilities* capabilities)
{
	struct __user_cap_header_struct header = { _LINUX_CAPABILITY_VERSION_3, 0 };
	struct __user_cap_data_struct   capData[2] = { { 0 } };

	capData[0].effective = capabilities->effective & 0xffffffff;
	capData[1].effective = capabilities->effective >> 32;
	capData[0].permitted = capabilities->permitted & 0xffffffff;
	capData[1].permitted = capabilities->permitted >> 32;
	capData[0].inheritable = capabilities->inheritable & 0xffffffff;
	capData[1].inheritable = capabilities->inheritable >> 32;
	return capset(&header, capData);
}

static int __set_ambient_capabilities(__cap_mask capabilities)
{
	// Ubuntu trusty has a 4.4 kernel, but these macros are not defined
#ifndef PR_CAP_AMBIENT
#  define PR_CAP_AMBIENT             47
#  define PR_CAP_AMBIENT_IS_SET      1
#  define PR_CAP_AMBIENT_RAISE       2
#  define PR_CAP_AMBIENT_LOWER       3
#  define PR_CAP_AMBIENT_CLEAR_ALL   4
#endif

	// We would like to use cap_set_ambient(), but it's not in Debian 10; so
	// use prctl() instead.
	VLOG_DEBUG("containerv[child]", "setting ambient capabilities %lx\n", capabilities);
	if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_CLEAR_ALL, 0, 0, 0) < 0) {
		VLOG_ERROR("containerv[child]", "cannot reset ambient capabilities\n");
        return -1;
	}

	for (int i = 0; i < CAP_LAST_CAP; i++) {
		if (capabilities & __CAP_TO_MASK(i)) {
			VLOG_DEBUG("containerv[child]", "setting ambient capability %d\n", i);
			if (prctl(PR_CAP_AMBIENT, PR_CAP_AMBIENT_RAISE, i, 0, 0) < 0) {
				VLOG_ERROR("containerv[child]", "cannot set ambient capability %d\n", i);
                return -1;
			}
		}
	}
    return 0;
}

static int g_capsToDrop[] = {
    //CAP_CHOWN,
    //CAP_DAC_OVERRIDE,
    CAP_DAC_READ_SEARCH,
    //CAP_FOWNER,
    CAP_FSETID,
    //CAP_KILL,
    //CAP_SETGID,
    //CAP_SETUID,
    //CAP_SETPCAP,
    //CAP_LINUX_IMMUTABLE,
    //CAP_NET_BIND_SERVICE, 
    //CAP_NET_BROADCAST,
    //CAP_NET_ADMIN,
    //CAP_NET_RAW,
    CAP_IPC_LOCK,
    //CAP_IPC_OWNER,
    CAP_SYS_MODULE,
    CAP_SYS_RAWIO,
    //CAP_SYS_CHROOT,
    //CAP_SYS_PTRACE,
    //CAP_SYS_PACCT,
    CAP_SYS_ADMIN,
    CAP_SYS_BOOT,
    CAP_SYS_NICE,
    CAP_SYS_RESOURCE,
    CAP_SYS_TIME,
    //CAP_SYS_TTY_CONFIG,
    CAP_MKNOD,
    //CAP_LEASE,
    CAP_AUDIT_WRITE,
    CAP_AUDIT_CONTROL,
    CAP_SETFCAP,
    CAP_MAC_OVERRIDE,
    CAP_MAC_ADMIN,
    CAP_SYSLOG,
    CAP_WAKE_ALARM,
    CAP_BLOCK_SUSPEND,
    CAP_AUDIT_READ,
    //CAP_PERFMON,
    //CAP_BPF,
    //CAP_CHECKPOINT_RESTORE
};

int containerv_drop_capabilities(void)
{
    int   capsCount;
    cap_t caps = NULL;

    capsCount = sizeof(g_capsToDrop) / sizeof(*g_capsToDrop);
    for (int i = 0; i < capsCount; i++) {
        if (prctl(PR_CAPBSET_DROP, g_capsToDrop[i], 0, 0, 0)) {
            VLOG_ERROR("containerv", "failed to prctl cap %d: %m\n", g_capsToDrop[i]);
            return -1;
        }
    }

    if (!(caps = cap_get_proc()) ||
        cap_set_flag(caps, CAP_INHERITABLE, capsCount, g_capsToDrop, CAP_CLEAR) ||
        cap_set_proc(caps)) {
        if (caps) {
            cap_free(caps);
        }
        return -1;
    }

    cap_free(caps);
    return 0;
}

static void __dump_caps(const char* prefix)
{
    cap_t caps = cap_get_proc();
    char* capsMessage = cap_to_text(caps, NULL);

    VLOG_DEBUG("containerv[child]", "%s: %s\n", prefix, capsMessage);
    
    cap_free(capsMessage);
    cap_free(caps);
}

int containerv_switch_user_with_capabilities(uid_t uid, gid_t gid)
{
    int                   status;
    struct __capabilities caps;
    VLOG_DEBUG("containerv[child]", "containerv_switch_user_with_capabilities(%u, %u)\n", uid, gid);

    // Don't lose the permitted capabilities when switching user.
    // Note that there's no need to undo this operation later, since this
    // flag is automatically cleared on execve().
    status = prctl(PR_SET_KEEPCAPS, 1L);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to enable inherit of capabilities\n");
        return status;
    }

    // drop into the real user
    status = setgid(gid);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to switch group: %i (gid=%i)\n", status, gid);
        return status;
    }

    status = setuid(uid);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to switch user: %i (uid=%i)\n", status, uid);
        return status;
    }
    
    // ensure that we actually lost privileges
    if (getuid() == 0 || geteuid() == 0) {
        VLOG_ERROR("containerv[child]", "failed to drop the root capabilities, aborting\n");
        return status;
    }

    if (gid != 0 && (getgid() == 0 || getegid() == 0)) {
        VLOG_ERROR("containerv[child]", "failed to drop the root capabilities, aborting\n");
        return status;
    }

    // Setup permitted caps, effective caps and inheritable
    caps.effective = __container_caps;
    caps.permitted = __container_caps | __primary_caps;
    caps.inheritable = __primary_caps;
    status = __set_capabilities(&caps);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to set container capabilities\n");
        return status;
    }

    status = __set_ambient_capabilities(__primary_caps);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to set ambient capabilities\n");
        return status;
    }
    
    __dump_caps("current capabilities for setup");
    return status;
}

int containerv_set_init_process(void)
{
    pid_t pid;
    VLOG_DEBUG("containerv[child]", "containerv_set_init_process()\n");

    pid = setsid();
    if (pid < 0) {
        return -1;
    }

    pid = fork();
    if (pid < 0) {
        return -1;
    }

    if (pid != 0) {
        // skip any CRT cleanup here
        _exit(EXIT_SUCCESS);
    }
    return 0;
}

static int __directory_exists(
    const char* path)
{
    struct stat st;
    if (stat(path, &st)) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    return S_ISDIR(st.st_mode) ? 1 : -1;
}

static int __mkdir_if_not_exists(const char* root, const char* path, unsigned int mode)
{
    char* destination;
    int   status;
    VLOG_DEBUG("containerv[child]", "__mkdir_if_not_exists(%s%s)\n", root, path);

    destination = strpathcombine(root, path);
    if (destination == NULL) {
        return -1;
    }

    status = __directory_exists(destination);
    if (status == 1) {
        free(destination);
        return 0;
    } else if (status < 0) {
        VLOG_ERROR("containerv[child]", "failed to stat %s\n", destination);
        free(destination);
        return status;
    }
    
    status = mkdir(destination, mode);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to create path %s\n", destination);
    }
    free(destination);
    return status;
}

int containerv_mkdir(const char* root, const char* path, unsigned int mode)
{
    char   ccpath[PATH_MAX];
    size_t length;
    int    status;
    VLOG_DEBUG("containerv[child]", "containerv_mkdir(root=%s, path=%s)\n", root, path);

    status = snprintf(ccpath, sizeof(ccpath), "%s", path);
    if (status >= sizeof(ccpath)) {
        errno = ENAMETOOLONG;
        return -1; 
    }

    length = strlen(ccpath);
    if (ccpath[length - 1] == '/') {
        ccpath[length - 1] = 0;
    }

    for (char* p = ccpath + 1; *p; p++) {
        if (*p == '/') {
            
            // temporarily cut off the path string for operations
            *p = 0;
            
            // only do stuff if the folder does not already exist
            status = __mkdir_if_not_exists(root, &ccpath[0], mode);
            if (status) {
                VLOG_ERROR("containerv[child]", "failed to create path %s\n", &ccpath[0]);
                return status;
            }

            // restore the path string
            *p = '/';
        }
    }
    return __mkdir_if_not_exists(root, ccpath, mode);
}
