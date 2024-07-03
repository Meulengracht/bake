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

#include <errno.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int platform_script(const char *script) 
{
    int    status;
    int    sfilefd;
    FILE*  sfile;
    char   tmpPath[64];
    char   cmdBuffer[128];
    
    snprintf(&tmpPath[0], sizeof(tmpPath), "C:\\Windows\\Temp\\scriptXXXXXX");

    // create a temporary file
    sfilefd = mkstemp(&tmpPath[0]);
    if (sfilefd < 1) {
        fprintf(stderr, "platform_script: mkstemp failed for path %s: %s\n", &tmpPath[0], strerror(errno));
        return -1;
    }

    // set executable
    status = _chmod(tmpPath);
    if (status) {
        fprintf(stderr, "platform_script: failed to set executable bit for %s: %s\n", &tmpPath[0], strerror(errno));
        return status;
    }

    sfile = _fdopen(sfilefd, "w+");
    if (sfile == NULL) {
        fprintf(stderr, "platform_script: fdopen failed for path %s: %s\n", &tmpPath[0], strerror(errno));
        return -1;
    }
    
    fputs(script, sfile);
    fclose(sfile);

    // execute and unlink
    snprintf(&cmdBuffer[0], sizeof(cmdBuffer), "powershell -ExecutionPolicy Bypass -File %s", tmpPath);
    status = system(&cmdBuffer[0]);
    _unlink(&tmpPath[0]);
    if (status) {
        fprintf(stderr, "platform_script: exec %s failed: %s\n", &tmpPath[0], strerror(errno));
    }
    return status;
}
