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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static int __count_occurrences(FILE* stream, const char* needle)
{
    char   buffer[4096];
    size_t read;
    int    count;
    char*  cursor;

    rewind(stream);
    read = fread(&buffer[0], 1, sizeof(buffer) - 1, stream);
    buffer[read] = '\0';

    count = 0;
    cursor = &buffer[0];
    while ((cursor = strstr(cursor, needle)) != NULL) {
        count++;
        cursor += strlen(needle);
    }
    return count;
}

int main(void)
{
    FILE* stream;

    stream = tmpfile();
    if (stream == NULL) {
        fprintf(stderr, "tmpfile failed\n");
        return 1;
    }

    vlog_initialize(VLOG_LEVEL_DISABLED);
    if (vlog_sink_add_text(stream, 0) != 0) {
        fprintf(stderr, "failed to add file sink\n");
        vlog_cleanup();
        fclose(stream);
        return 1;
    }
    vlog_set_output_level(stream, VLOG_LEVEL_WARNING);

    // default decoration includes "tag[level]" ahead of the message
    VLOG_WARNING("demo", "decorated-message\n");
    vlog_flush();
    if (__count_occurrences(stream, "demo[W]") != 1) {
        fprintf(stderr, "expected decorated output to include tag and level marker\n");
        vlog_cleanup();
        fclose(stream);
        return 1;
    }

    // NODECO should suppress the tag/level prefix for subsequent messages
    vlog_set_output_options(stream, VLOG_OUTPUT_OPTION_NODECO);
    VLOG_WARNING("demo", "plain-message\n");
    vlog_flush();

    vlog_cleanup();

    if (__count_occurrences(stream, "demo[W]") != 1 ||
        __count_occurrences(stream, "plain-message") != 1) {
        fprintf(stderr, "NODECO option did not suppress decoration on new output\n");
        fclose(stream);
        return 1;
    }

    fclose(stream);
    return 0;
}
