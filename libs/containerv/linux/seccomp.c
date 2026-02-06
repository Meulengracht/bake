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

#define __SC_MAX_ARGS 5

#include <chef/platform.h>
#include <errno.h>
#include <seccomp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <vlog.h>

// import the policy structure details
#include "../policies/private.h"

static uint32_t __determine_default_action(void)
{
    const char* logMode;

    // Check if seccomp logging is enabled via environment variable
    // CONTAINERV_SECCOMP_LOG=1 enables SCMP_ACT_LOG for debugging
    logMode = getenv("CONTAINERV_SECCOMP_LOG");
    if (logMode != NULL && strcmp(logMode, "1") == 0) {
        VLOG_DEBUG("containerv", "policy_seccomp: logging mode enabled (SCMP_ACT_LOG)\n");
        return SCMP_ACT_LOG;
    }
    
    VLOG_DEBUG("containerv", "policy_seccomp: errno mode (SCMP_ACT_ERRNO). Set CONTAINERV_SECCOMP_LOG=1 to enable logging\n");
    return SCMP_ACT_ERRNO(EPERM);
}

static int __parse_number(
    const char* string,
    const char* syscallName,
    uint32_t    syscallFlags,
    uint64_t*   valueOut)
{
    char*    endptr;
    uint64_t value;
    
    value = strtoull(string, &endptr, 10);
    if (*endptr == '\0') {
        *valueOut = value;
        return 0;
    }

    // try to parse as a signed number if the flag is set
    if ((syscallFlags & SYSCALL_FLAG_NEGATIVE_ARG)) {
        *valueOut = (uint64_t)((uint32_t)atoi(string));
        return 0;
    }
    return -1;
}

static int __parse_masked_equal(
    const char* string,
    const char* syscallName,
    uint32_t    syscallFlags,
    uint64_t*   valueOut,
    uint64_t*   value2Out)
{
    char** args;
    int    argCount = 0;
    int    status;


	args = strsplit(string, '|');
	if (args == NULL) {
		return -1;
	}

    while (args[argCount] != NULL) {
        argCount++;
    }
    if (argCount != 2) {
        VLOG_ERROR(
            "containerv",
            "policy_seccomp: invalid masked equality argument '%s' for syscall '%s', expected format 'value|mask'\n",
            string, syscallName
        );
        goto cleanup;
    }
	
    status = __parse_number(args[0], syscallName, syscallFlags, valueOut);
	if (status) {
		goto cleanup;
	}
	
    status = __parse_number(args[1], syscallName, syscallFlags, value2Out);

cleanup:
    strsplit_free(args);
    return status;
}


static int __parse_entry(scmp_filter_ctx allowContext, scmp_filter_ctx denyContext, struct containerv_syscall_entry* entry)
{
    const char*          syscallName;
    int                  syscallNumber;
    uint32_t             syscallAction = SCMP_ACT_ALLOW;
    scmp_filter_ctx      context = allowContext;
    struct scmp_arg_cmp* scArgs = NULL;
    int                  scArgCount = 0;
    char**               args;
    int                  argCount = 0;
    int                  status;

    args = strsplit(entry->args, ' ');
    if (entry->args != NULL && args == NULL) {
        return -1;
    } else if (args != NULL) {
        // Verify there are no more than 5 arguments (seccomp limit)
        while (args[argCount] != NULL) {
            argCount++;
        }
        if (argCount > __SC_MAX_ARGS) {
            VLOG_ERROR(
                "containerv",
                "policy_seccomp: syscall '%s' has too many arguments (%i), max is 5\n",
                entry->name, argCount
            );
            free(args);
            errno = EINVAL;
            status = -1;
            goto cleanup;
        }
    }

	// allow the listed syscall but also support explicit denials as well by
	// prefixing the syscall name with '!'
    syscallName = entry->name;
	if (syscallName[0] == '!') {
		syscallAction = SCMP_ACT_ERRNO(EACCES);
		syscallName = &syscallName[1];
		context = denyContext;
	}
    
    syscallNumber = seccomp_syscall_resolve_name(syscallName);
    if (syscallNumber == __NR_SCMP_ERROR) {
        // Syscall might not exist on this architecture - log and continue
        VLOG_DEBUG(
            "containerv", "policy_seccomp: syscall '%s' not found on this architecture\n",
            syscallName
        );
        status = 0;
        goto cleanup;
    }

    // Allocate scArgs to be the maximum number of arguments (5) since we don't know how many there are yet
    scArgs = calloc(__SC_MAX_ARGS, sizeof(struct scmp_arg_cmp));
    if (scArgs == NULL) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to allocate memory for syscall arguments\n");
        status = -1;
        goto cleanup;
    }

    status = 0;
    for (int i = 0; i < argCount; i++) {
        enum scmp_compare cmpOp;
        uint64_t          value, value2;
        const char*       arg = args[i];

        if (strcmp(args[i], "-") == 0) {
            continue;
        }
        
		if (strncmp(arg, ">=", 2) == 0) {
			cmpOp = SCMP_CMP_GE;
			status = __parse_number(&arg[2], syscallName, entry->flags, &value);
		} else if (strncmp(arg, "<=", 2) == 0) {
			cmpOp = SCMP_CMP_LE;
			status = __parse_number(&arg[2], syscallName, entry->flags, &value);
		} else if (strncmp(arg, "!", 1) == 0) {
			cmpOp = SCMP_CMP_NE;
			status = __parse_number(&arg[1], syscallName, entry->flags, &value);
		} else if (strncmp(arg, "<", 1) == 0) {
			cmpOp = SCMP_CMP_LT;
			status = __parse_number(&arg[1], syscallName, entry->flags, &value);
		} else if (strncmp(arg, ">", 1) == 0) {
			cmpOp = SCMP_CMP_GT;
			status = __parse_number(&arg[1], syscallName, entry->flags, &value);
		} else if (strncmp(arg, "|", 1) == 0) {
			cmpOp = SCMP_CMP_MASKED_EQ;
			status = __parse_number(&arg[1], syscallName, entry->flags, &value);
			value2 = value;
		} else if (strchr(arg, '|') != NULL) {
			cmpOp = SCMP_CMP_MASKED_EQ;
			status = __parse_masked_equal(arg, syscallName, entry->flags, &value, &value2);
		} else {
			cmpOp = SCMP_CMP_EQ;
			status = __parse_number(arg, syscallName, entry->flags, &value);
		}
        
		if (status) {
            VLOG_ERROR(
                "containerv",
                "policy_seccomp: failed to parse argument '%s' for syscall '%s'\n",
                arg, syscallName
            );
            break;
		}

		// For now only support EQ with negative args. If changing
		// this, be sure to adjust readNumber accordingly and use
		// libseccomp carefully.
		if (entry->flags & SYSCALL_FLAG_NEGATIVE_ARG) {
			if (cmpOp != SCMP_CMP_EQ) {
                VLOG_ERROR(
                    "containerv",
                    "policy_seccomp: syscall '%s' with negative arguments only supports equality comparisons\n",
                    syscallName
                );
                status = -1;
                break;
			}
		}

		if (cmpOp == SCMP_CMP_MASKED_EQ) {
			struct scmp_arg_cmp scmpCond = SCMP_CMP(i, cmpOp, value, value2);
            scArgs[scArgCount] = scmpCond;
		} else if (entry->flags & SYSCALL_FLAG_NEGATIVE_ARG) {
			struct scmp_arg_cmp scmpCond = SCMP_CMP(i, SCMP_CMP_MASKED_EQ, 0xFFFFFFFF, value);
            scArgs[scArgCount] = scmpCond;
		} else {
			struct scmp_arg_cmp scmpCond = SCMP_CMP(i, cmpOp, value);
            scArgs[scArgCount] = scmpCond;
		}
        scArgCount++;
    }

    if (status) {
        goto cleanup;
    }

	// Default to adding a precise match if possible. Otherwise
	// let seccomp figure out the architecture specifics.
    status = seccomp_rule_add_exact_array(context, syscallAction, syscallNumber, scArgCount, scArgs);
    if (status) {
        status = seccomp_rule_add_array(context, SCMP_ACT_ALLOW, syscallNumber, scArgCount, scArgs);
        if (status) {
            VLOG_ERROR(
                "containerv",
                "policy_seccomp: failed to add rule for syscall '%s'\n",
                syscallName
            );
            return status;
        }
    }

cleanup:
    free(scArgs);
    strsplit_free(args);
	return status;
}

int policy_seccomp_apply(struct containerv_policy* policy)
{
    scmp_filter_ctx allowContext;
    int             status = -1;
    
    if (policy == NULL) {
        errno = EINVAL;
        return -1;
    }
    
    VLOG_TRACE("containerv", "policy_seccomp: applying policy with %d allowed syscalls\n",
              policy->syscall_count);
    
    // Create a seccomp filter with default deny action
    allowContext = seccomp_init(__determine_default_action());
    if (allowContext == NULL) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to initialize seccomp context\n");
        return -1;
    }
    
    // Add all allowed syscalls from the policy
    for (int i = 0; i < policy->syscall_count; i++) {
        const char* syscall_name = policy->syscalls[i].name;
        status = __parse_entry(allowContext, allowContext, &policy->syscalls[i]);
        if (status) {
            VLOG_ERROR("containerv", "policy_seccomp: failed to parse syscall entry for '%s'\n", syscall_name);
            goto cleanup;
        }
    }

    // Enable no_new_privs so we can load seccomp without CAP_SYS_ADMIN.
    // This also prevents future privilege escalation after the filter is active.
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to set no_new_privs: %s\n", strerror(errno));
        goto cleanup;
    }

    // Ask libseccomp to keep NNP enabled (belt-and-suspenders).
    if (seccomp_attr_set(allowContext, SCMP_FLTATR_CTL_NNP, 1) != 0) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to set NNP attribute\n");
        goto cleanup;
    }
    
    // Load the allow filter into the kernel
    if (seccomp_load(allowContext) != 0) {
        VLOG_ERROR("containerv", "policy_seccomp: failed to load seccomp filter: %s\n",
                  strerror(errno));
        goto cleanup;
    }
    
    VLOG_TRACE("containerv", "policy_seccomp: policy applied successfully\n");
    status = 0;
    
cleanup:
    if (allowContext != NULL) {
        seccomp_release(allowContext);
    }
    
    return status;
}
