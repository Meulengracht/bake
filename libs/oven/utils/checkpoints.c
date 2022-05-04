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
#include <utils.h>

int oven_checkpoint_create(const char* path, const char* checkpoint)
{
    // write checkpoint to file
    FILE* file = fopen(path, "a");
    if (!file) {
        fprintf(stderr, "oven: failed to create checkpoint file: %s\n", strerror(errno));
        return -1;
    }

    fprintf(file, "%s\n", checkpoint);
    fclose(file);
    return 0;
}

int oven_checkpoint_remove(const char* path, const char* checkpoint)
{
    FILE*   file;
    char*   line = NULL;
    char*   contents;
    size_t  lineLength = 0;
    size_t  fileSize;
    ssize_t read;
    int     status;

    // read checkpoint from file
    file = fopen(path, "r+");
    if (!file) {
        return 0;
    }

    // get size of file
    fseek(file, 0, SEEK_END);
    fileSize = ftell(file);
    rewind(file);

    // allocate a buffer to hold new file contents
    contents = calloc(1, fileSize);
    if (!contents) {
        fclose(file);
        return -1;
    }

    // read and filter file content matching checkpoint
    while ((read = getline(&line, &lineLength, file)) != -1) {
        // remove newline
        if (line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }

        status = strcmp(line, checkpoint);
        if (status != 0) {
            strcat(contents, line);
            strcat(contents, "\n");
        }
        free(line);
        line = NULL;
    }

    // write new file contents
    rewind(file);
    platform_chsize(fileno(file), 0);
    fwrite(contents, 1, strlen(contents), file);
    fclose(file);
    return 0;
}

int oven_checkpoint_contains(const char* path, const char* checkpoint)
{
    FILE*   file;
    char*   line = NULL;
    size_t  lineLength = 0;
    ssize_t read;
    int     status;

    // read checkpoint from file
    file = fopen(path, "r");
    if (!file) {
        return 0;
    }

    while ((read = getline(&line, &lineLength, file)) != -1) {
        // remove newline
        if (line[read - 1] == '\n') {
            line[read - 1] = '\0';
        }

        status = strcmp(line, checkpoint);
        free(line);
        line = NULL;
        
        if (status == 0) {
            fclose(file);
            return 1;
        }
    }

    fclose(file);
    return 0;
}
