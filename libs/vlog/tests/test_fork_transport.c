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

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vlog.h>

static int __file_contains(const char* path, const char* needle)
{
    char   buffer[4096];
    FILE*  file;
    size_t read;
    int    found;

    file = fopen(path, "r");
    if (file == NULL) {
        fprintf(stderr, "failed to open log file %s: %s\n", path, strerror(errno));
        return 0;
    }

    read = fread(&buffer[0], 1, sizeof(buffer) - 1, file);
    if (ferror(file)) {
        fprintf(stderr, "failed to read log file %s\n", path);
        fclose(file);
        return 0;
    }
    buffer[read] = '\0';

    found = strstr(&buffer[0], needle) != NULL;
    fclose(file);
    return found;
}

int main(void)
{
    char  path[] = "/tmp/vlog-fork-transport-XXXXXX";
    FILE* file;
    int   fd;
    int   childStatus;
    pid_t pid;

    fd = mkstemp(&path[0]);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed: %s\n", strerror(errno));
        return 1;
    }

    file = fdopen(fd, "w+");
    if (file == NULL) {
        fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
        close(fd);
        unlink(&path[0]);
        return 1;
    }

    vlog_initialize(VLOG_LEVEL_TRACE);
    if (vlog_sink_add_text(file, 1) != 0) {
        fprintf(stderr, "failed to add file sink\n");
        fclose(file);
        unlink(&path[0]);
        return 1;
    }
    vlog_set_output_options(file, VLOG_OUTPUT_OPTION_NODECO);
    vlog_set_output_level(file, VLOG_LEVEL_TRACE);

    VLOG_TRACE("parent", "parent before fork\n");

    pid = fork();
    if (pid < 0) {
        fprintf(stderr, "fork failed: %s\n", strerror(errno));
        vlog_cleanup();
        unlink(&path[0]);
        return 1;
    }

    if (pid == 0) {
        VLOG_TRACE("child", "child after fork\n");
        _Exit(0);
    }

    if (waitpid(pid, &childStatus, 0) < 0) {
        fprintf(stderr, "waitpid failed: %s\n", strerror(errno));
        vlog_cleanup();
        unlink(&path[0]);
        return 1;
    }
    if (!WIFEXITED(childStatus) || WEXITSTATUS(childStatus) != 0) {
        fprintf(stderr, "child exited unexpectedly\n");
        vlog_cleanup();
        unlink(&path[0]);
        return 1;
    }

    VLOG_TRACE("parent", "parent after fork\n");
    vlog_flush();
    vlog_cleanup();

    if (!__file_contains(&path[0], "parent before fork") ||
        !__file_contains(&path[0], "child after fork") ||
        !__file_contains(&path[0], "parent after fork")) {
        fprintf(stderr, "fork transport log did not contain expected messages\n");
        unlink(&path[0]);
        return 1;
    }

    unlink(&path[0]);
    return 0;
}
