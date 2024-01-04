/**
 * Copyright 2022, Philip Meulengracht
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
#include <unistd.h>

int platform_script(const char* script)
{
    int    status;
    int    sfilefd;
    FILE*  sfile;
    char   tmpPath[64];
    char   cmdBuffer[128];
    
    snprintf(&tmpPath[0], sizeof(tmpPath), "/tmp/script_XXXXXX");

    // create a temporary file
    sfilefd = mkstemp(&tmpPath[0]);
    if (sfilefd < 1) {
        fprintf(stderr, "platform_script: mkstemp failed for path %s: %s\n", &tmpPath[0], strerror(errno));
        return -1;
    }

    sfile = fdopen(sfilefd, "w+");
    if (sfile == NULL) {
        fprintf(stderr, "platform_script: fdopen failed for path %s: %s\n", &tmpPath[0], strerror(errno));
        return -1;
    }
    
    fprintf(sfile, "#!/bin/bash\n");
    fputs(script, sfile);
    fclose(sfile);

    // set executable
    snprintf(&cmdBuffer[0], sizeof(cmdBuffer), "chmod +x %s", &tmpPath[0]);
    status = system(&cmdBuffer[0]);
    if (status) {
        fprintf(stderr, "platform_script: chmod +x %s failed: %s\n", &cmdBuffer[0], strerror(errno));
        return status;
    }

    // execute and unlink
    status = system(&tmpPath[0]);
    unlink(&tmpPath[0]);
    if (status) {
        fprintf(stderr, "platform_script: exec %s failed: %s\n", &tmpPath[0], strerror(errno));
    }
    return status;
}
