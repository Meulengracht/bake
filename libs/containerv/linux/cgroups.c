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

#include "cgroups.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vlog.h>

// Default cgroups limits
#define CGROUPS_DEFAULT_MEMORY_MAX "1G"
#define CGROUPS_DEFAULT_CPU_WEIGHT "100"
#define CGROUPS_DEFAULT_PIDS_MAX "256"
#define CGROUPS_CGROUP_PROCS "cgroup.procs"
enum { CGROUPS_CONTROL_FIELD_SIZE = 256 };

// This struct is used to store cgroups settings.
struct cgroups_setting {
  char name[CGROUPS_CONTROL_FIELD_SIZE];
  char value[CGROUPS_CONTROL_FIELD_SIZE];
};

// cgroups settings are written to the cgroups v2 filesystem as follows:
// - create a directory for the new cgroup
// - settings files are created automatically
// - write the settings to the corresponding files
int cgroups_init(const char* hostname, pid_t pid, const struct containerv_cgroup_limits* limits) {
  char cgroup_dir[PATH_MAX] = {0};

  // Use provided limits or defaults
  const char* memory_max = limits && limits->memory_max ? limits->memory_max : CGROUPS_DEFAULT_MEMORY_MAX;
  const char* cpu_weight = limits && limits->cpu_weight ? limits->cpu_weight : CGROUPS_DEFAULT_CPU_WEIGHT;
  const char* pids_max = limits && limits->pids_max ? limits->pids_max : CGROUPS_DEFAULT_PIDS_MAX;

  // The "cgroup.procs" setting is used to add a process to a cgroup.
  // It is prepared here with the pid of the calling process, so that it can be
  // added to the cgroup later.
  struct cgroups_setting *procs_setting =
      &(struct cgroups_setting){.name = CGROUPS_CGROUP_PROCS, .value = ""};
  ;
  if (snprintf(procs_setting->value, CGROUPS_CONTROL_FIELD_SIZE, "%d", pid) ==
      -1) {
    VLOG_ERROR("containerv", "cgroups_init: failed to setup cgroup.procs setting: %s\n", strerror(errno));
    return -1;
  }
  // Cgroups let us limit resources allocated to a process to prevent it from
  // denying services to the rest of the system. The cgroups must be created
  // before the process enters a cgroups namespace. The following settings are
  // applied:
  // - memory.max: process memory limit (default 1GB)
  // - cpu.weight: CPU time weight (1-10000, default 100)
  // - pids.max: max number of processes (default 256)
  // - cgroup.procs: the calling process is added to the cgroup
  struct cgroups_setting *cgroups_setting_list[] = {
      &(struct cgroups_setting){.name = "memory.max",
                                .value = ""},
      &(struct cgroups_setting){.name = "cpu.weight",
                                .value = ""},
      &(struct cgroups_setting){.name = "pids.max", .value = ""},
      procs_setting, NULL};

  // Copy the limit values and ensure null termination
  strncpy(cgroups_setting_list[0]->value, memory_max, CGROUPS_CONTROL_FIELD_SIZE - 1);
  cgroups_setting_list[0]->value[CGROUPS_CONTROL_FIELD_SIZE - 1] = '\0';
  
  strncpy(cgroups_setting_list[1]->value, cpu_weight, CGROUPS_CONTROL_FIELD_SIZE - 1);
  cgroups_setting_list[1]->value[CGROUPS_CONTROL_FIELD_SIZE - 1] = '\0';
  
  strncpy(cgroups_setting_list[2]->value, pids_max, CGROUPS_CONTROL_FIELD_SIZE - 1);
  cgroups_setting_list[2]->value[CGROUPS_CONTROL_FIELD_SIZE - 1] = '\0';

  VLOG_DEBUG("containerv", "cgroups_init: setting cgroups for %s...\n", hostname);

  // Create the cgroup directory.
  if (snprintf(cgroup_dir, sizeof(cgroup_dir), "/sys/fs/cgroup/%s", hostname) ==
      -1) {
    VLOG_ERROR("containerv", "cgroups_init: failed to setup path: %s\n", strerror(errno));
    return -1;
  }

  VLOG_DEBUG("containerv", "cgroups_init: creating %s...\n", cgroup_dir);
  if (mkdir(cgroup_dir, S_IRUSR | S_IWUSR | S_IXUSR)) {
    VLOG_ERROR("containerv", "cgroups_init: failed to mkdir %s: %s\n", cgroup_dir, strerror(errno));
    return -1;
  }

  // Loop through and write settings to the corresponding files in the cgroup
  // directory.
  for (struct cgroups_setting **setting = cgroups_setting_list; *setting;
       setting++) {
    char setting_dir[PATH_MAX] = {0};
    int fd = 0;

    VLOG_DEBUG("containerv", "cgroups_init: setting %s to %s...\n", (*setting)->name, (*setting)->value);
    if (snprintf(setting_dir, sizeof(setting_dir), "%s/%s", cgroup_dir,
                 (*setting)->name) == -1) {
      VLOG_ERROR("containerv", "cgroups_init: failed to setup path: %s\n", strerror(errno));
      return -1;
    }

    VLOG_TRACE("containerv", "cgroups_init: opening %s...\n", setting_dir);
    if ((fd = open(setting_dir, O_WRONLY)) == -1) {
      VLOG_ERROR("containerv", "cgroups_init: failed to open %s: %s\n", setting_dir, strerror(errno));
      return -1;
    }

    VLOG_TRACE("containerv", "cgroups_init: writing %s to setting\n", (*setting)->value);
    if (write(fd, (*setting)->value, strlen((*setting)->value)) == -1) {
      VLOG_ERROR("containerv", "cgroups_init: failed to write %s: %s\n", setting_dir, strerror(errno));
      close(fd);
      return -1;
    }

    VLOG_TRACE("containerv", "cgroups_init: closing %s...\n", setting_dir);
    if (close(fd)) {
      VLOG_ERROR("containerv", "cgroups_init: failed to close %s: %s\n", setting_dir, strerror(errno));
      return -1;
    }
  }

  VLOG_DEBUG("containerv", "cgroups_init: cgroups set successfully\n");
  return 0;
}

// Clean up the cgroups for the process. Since we write the PID of the child
// process to the cgroup.procs file, all that is needed is to remove the cgroups
// directory after the child process has exited.
int cgroups_free(const char* hostname) {
  char dir[PATH_MAX] = {0};

  VLOG_DEBUG("containerv", "cgroups_free: freeing cgroups for %s...\n", hostname);

  if (snprintf(dir, sizeof(dir), "/sys/fs/cgroup/%s", hostname) == -1) {
    VLOG_ERROR("containerv", "cgroups_free: failed to setup paths: %s\n", strerror(errno));
    return -1;
  }

  VLOG_DEBUG("containerv", "cgroups_free: removing %s...\n", dir);
  if (rmdir(dir)) {
    VLOG_ERROR("containerv", "cgroups_free: failed to rmdir %s: %s\n", dir, strerror(errno));
    return -1;
  }

  VLOG_DEBUG("containerv", "cgroups_free: cgroups released successfully\n");
  return 0;
}
