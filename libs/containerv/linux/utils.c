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

#include <linux/capability.h>
#include <linux/prctl.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <stdlib.h>
#include <unistd.h>
#include "private.h"
#include <vlog.h>

int containerv_drop_capabilities(void)
{
    int capsToDrop[] = {
        CAP_AUDIT_CONTROL,   CAP_AUDIT_READ,   CAP_AUDIT_WRITE, CAP_BLOCK_SUSPEND,
        CAP_DAC_READ_SEARCH, CAP_FSETID,       CAP_IPC_LOCK,    CAP_MAC_ADMIN,
        CAP_MAC_OVERRIDE,    CAP_MKNOD,        CAP_SETFCAP,     CAP_SYSLOG,
        CAP_SYS_ADMIN,       CAP_SYS_BOOT,     CAP_SYS_MODULE,  CAP_SYS_NICE,
        CAP_SYS_RAWIO,       CAP_SYS_RESOURCE, CAP_SYS_TIME,    CAP_WAKE_ALARM
    };
    int   capsCount;
    cap_t caps = NULL;

    capsCount = sizeof(capsToDrop) / sizeof(*capsToDrop);
    for (int i = 0; i < capsCount; i++) {
        if (prctl(PR_CAPBSET_DROP, capsToDrop[i], 0, 0, 0)) {
            VLOG_ERROR("containerv", "failed to prctl cap %d: %m", capsToDrop[i]);
            return -1;
        }
    }

    if (!(caps = cap_get_proc()) ||
        cap_set_flag(caps, CAP_INHERITABLE, capsCount, capsToDrop, CAP_CLEAR) ||
        cap_set_proc(caps)) {
        if (caps) {
            cap_free(caps);
        }
        return -1;
    }

    cap_free(caps);
    return 0;
}

#define __INHERITTED_CAP_COUNT 9
static const cap_value_t g_inherittedCaps[__INHERITTED_CAP_COUNT] = {
    CAP_CHOWN, CAP_DAC_OVERRIDE, CAP_DAC_READ_SEARCH, CAP_FOWNER,
    CAP_FSETID, CAP_SETGID, CAP_SETUID, CAP_SYS_ADMIN, CAP_SETFCAP
};

int containerv_switch_user_with_capabilities(uid_t uid, gid_t gid)
{
    int   status;
    cap_t caps;
    VLOG_DEBUG("containerv[child]", "containerv_switch_user_with_capabilities(%u, %u)\n", uid, gid);

    // ensure we have root capabilities at this point
    if (geteuid() != 0 && setresuid(0, 0, 0)) {
        VLOG_ERROR("containerv[child]", "__drop_to_user: the container must have setuid privileges\n");
        return -1;
    }

    /* Add need_caps to current capabilities. */
    caps = cap_get_proc();
    if (cap_set_flag(caps, CAP_PERMITTED,   __INHERITTED_CAP_COUNT, g_inherittedCaps, CAP_SET) ||
        cap_set_flag(caps, CAP_EFFECTIVE,   __INHERITTED_CAP_COUNT, g_inherittedCaps, CAP_SET) ||
        cap_set_flag(caps, CAP_INHERITABLE, __INHERITTED_CAP_COUNT, g_inherittedCaps, CAP_SET)) {
        VLOG_ERROR("containerv[child]", "failed to update the list of capabilities to refresh/inherit\n");
        status = -1;
        goto cleanup;
    }

    // update the current capabilities
    status = cap_set_proc(caps);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to set current capabilities\n");
        goto cleanup;
    }

    // make sure we keep our capabilities after the setresuid/setresgid
    status = prctl(PR_SET_KEEPCAPS, 1L);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to enable inherit of capabilities\n");
        goto cleanup;
    }
    
    // switch to the user we want to be
    status = setresuid(uid, uid, uid);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to switch user: %i (uid=%i)\n", status, uid);
        goto cleanup;
    }

    status = setresgid(gid, gid, gid);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to switch group: %i (gid=%i)\n", status, gid);
        goto cleanup;
    }

    // once the identity changes, we must refresh the capabilities
    // effective for the current user.
    status = cap_set_proc(caps);
    if (status) {
        VLOG_ERROR("containerv[child]", "failed to refresh current capabilities\n");
        goto cleanup;
    }

cleanup:
    cap_free(caps);
    return status;
}

int containerv_set_init_process(void)
{
    pid_t pid;

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
