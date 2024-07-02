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

#include <linux/capability.h>
#include <linux/prctl.h>
#include <sys/capability.h>
#include <sys/prctl.h>
#include <stdio.h>

int sec_set_caps(void) {
  int drop_caps[] = {
      CAP_AUDIT_CONTROL,   CAP_AUDIT_READ,   CAP_AUDIT_WRITE, CAP_BLOCK_SUSPEND,
      CAP_DAC_READ_SEARCH, CAP_FSETID,       CAP_IPC_LOCK,    CAP_MAC_ADMIN,
      CAP_MAC_OVERRIDE,    CAP_MKNOD,        CAP_SETFCAP,     CAP_SYSLOG,
      CAP_SYS_ADMIN,       CAP_SYS_BOOT,     CAP_SYS_MODULE,  CAP_SYS_NICE,
      CAP_SYS_RAWIO,       CAP_SYS_RESOURCE, CAP_SYS_TIME,    CAP_WAKE_ALARM};

  int num_caps = sizeof(drop_caps) / sizeof(*drop_caps);
  for (int i = 0; i < num_caps; i++) {
    if (prctl(PR_CAPBSET_DROP, drop_caps[i], 0, 0, 0)) {
      log_error("failed to prctl cap %d: %m", drop_caps[i]);
      return 1;
    }
  }

  cap_t caps = NULL;
  if (!(caps = cap_get_proc()) ||
      cap_set_flag(caps, CAP_INHERITABLE, num_caps, drop_caps, CAP_CLEAR) ||
      cap_set_proc(caps)) {
    
    if (caps) {
      cap_free(caps);
    }
    return -1;
  }

  cap_free(caps);
  return 0;
}