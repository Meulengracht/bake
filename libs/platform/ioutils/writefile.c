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
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int platform_writetextfile(const char* path, const char* text)
{
    FILE*  fp;
    size_t written, textLength;

    if (path == NULL || text == NULL) {
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path, "w");
    if (fp == NULL) {
        return -1;
    }

    textLength = strlen(text);
    written = fwrite(text, 1, textLength, fp);
    if (written != textLength) {
        (void)fclose(fp);
        return -1;
    }
    return fclose(fp);
}
