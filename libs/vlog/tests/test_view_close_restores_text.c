#define _XOPEN_SOURCE 600

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
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <vlog.h>

static int __open_terminal(FILE** streamOut, int* masterOut)
{
    char* name;
    int   master;
    int   slave;

    master = posix_openpt(O_RDWR | O_NOCTTY);
    if (master < 0) {
        fprintf(stderr, "posix_openpt failed: %s\n", strerror(errno));
        return -1;
    }
    if (grantpt(master) != 0 || unlockpt(master) != 0) {
        fprintf(stderr, "failed to prepare pty: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    name = ptsname(master);
    if (name == NULL) {
        fprintf(stderr, "ptsname failed: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    slave = open(name, O_RDWR | O_NOCTTY);
    if (slave < 0) {
        fprintf(stderr, "failed to open pty slave: %s\n", strerror(errno));
        close(master);
        return -1;
    }

    *streamOut = fdopen(slave, "w+");
    if (*streamOut == NULL) {
        fprintf(stderr, "fdopen failed: %s\n", strerror(errno));
        close(slave);
        close(master);
        return -1;
    }

    *masterOut = master;
    return 0;
}

static int __read_contains(int fd, const char* needle)
{
    char    buffer[8192];
    ssize_t count;

    count = read(fd, &buffer[0], sizeof(buffer) - 1);
    if (count < 0) {
        fprintf(stderr, "failed to read pty output: %s\n", strerror(errno));
        return 0;
    }

    buffer[count] = '\0';
    return strstr(&buffer[0], needle) != NULL;
}

int main(void)
{
    struct vlog_step step = { 0 };
    FILE*            stream;
    int              master;

    if (__open_terminal(&stream, &master) != 0) {
        return 1;
    }

    vlog_initialize(VLOG_LEVEL_TRACE);
    vlog_view_open(stream, "view", "footer");
    vlog_step_open(&step, "step");
    vlog_step_close(&step, VLOG_CONTENT_STATUS_DONE, "done");
    vlog_view_close();

    VLOG_TRACE("after", "post-close text output\n");
    vlog_flush();
    vlog_cleanup();
    fclose(stream);

    if (!__read_contains(master, "post-close text output")) {
        fprintf(stderr, "post-close log was not restored to text output\n");
        close(master);
        return 1;
    }

    close(master);
    return 0;
}
