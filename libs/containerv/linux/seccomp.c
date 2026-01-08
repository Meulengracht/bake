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

#include <asm-generic/ioctls.h>
#include <errno.h>
#include <linux/sched.h>
#include <seccomp.h>
#include <stdio.h>
#include <sys/stat.h>

// Used to represent the result of a seccomp rule.
#define SEC_SCMP_FAIL SCMP_ACT_ERRNO(EPERM)

// Blocks sensitive system calls based on Docker's default seccomp profile
// and other obsolete or dangerous system calls.
// The syscalls disallowed by the default Docker policy but permitted by this
// code are:
// - get_mempolicy, getpagesize, pciconfig_iobase, ustat, sysfs: These
// reveal information abour memory layout, PCI devices, filesystem
// - uselib: allows loading a shared library in userspace
//
// Other syscalls are (either):
// - already prevented by the capabilities set
// - available only on particular architectures
// - newer versions or aliases of other syscalls
//
// New Linux syscalls are added to the kernel over time, so this list
// should be updated periodically.
int sec_set_seccomp(void) {
  scmp_filter_ctx ctx = NULL;

  if (!(ctx = seccomp_init(SCMP_ACT_ALLOW)) ||
      // Calls that allow creating new setuid / setgid executables.
      // The contained process could created a setuid binary that can be used
      // by an user to get root in absence of user namespaces.
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(chmod), 1,
                       SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(chmod), 1,
                       SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmod), 1,
                       SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmod), 1,
                       SCMP_A1(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmodat), 1,
                       SCMP_A2(SCMP_CMP_MASKED_EQ, S_ISUID, S_ISUID)) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(fchmodat), 1,
                       SCMP_A2(SCMP_CMP_MASKED_EQ, S_ISGID, S_ISGID)) ||

      // Calls that allow contained processes to start new user namespaces
      // and possibly allow processes to gain new capabilities.
      seccomp_rule_add(
          ctx, SEC_SCMP_FAIL, SCMP_SYS(unshare), 1,
          SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER)) ||
      seccomp_rule_add(
          ctx, SEC_SCMP_FAIL, SCMP_SYS(clone), 1,
          SCMP_A0(SCMP_CMP_MASKED_EQ, CLONE_NEWUSER, CLONE_NEWUSER)) ||

      // Allows contained processes to write to the controlling terminal
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(ioctl), 1,
                       SCMP_A1(SCMP_CMP_MASKED_EQ, TIOCSTI, TIOCSTI)) ||

      // The kernel keyring system is not namespaced
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(keyctl), 0) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(add_key), 0) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(request_key), 0) ||

      // Before Linux 4.8, ptrace breaks seccomp
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(ptrace), 0) ||

      // Calls that let processes assign NUMA nodes. These could be used to deny
      // service to other NUMA-aware application on the host.
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(mbind), 0) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(migrate_pages), 0) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(move_pages), 0) ||
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(set_mempolicy), 0) ||

      // Alows userspace to handle page faults It can be used to pause execution
      // in the kernel by triggering page faults in system calls, a mechanism
      // often used in kernel exploits.
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(userfaultfd), 0) ||

      // This call could leak a lot of information on the host.
      // It can theoretically be used to discover kernel addresses and
      // uninitialized memory.
      seccomp_rule_add(ctx, SEC_SCMP_FAIL, SCMP_SYS(perf_event_open), 0) ||

      // Prevents setuid and setcap'd binaries from being executed
      // with additional privileges. This has some security benefits, but due to
      // weird side-effects, the ping command will not work in a process for
      // an unprivileged user.
      seccomp_attr_set(ctx, SCMP_FLTATR_CTL_NNP, 0) || seccomp_load(ctx)) {

    // Apply restrictions to the process and release the context.
    if (ctx) {
      seccomp_release(ctx);
    }

    return -1;
  }

  seccomp_release(ctx);
  return 0;
}
