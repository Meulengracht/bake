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

static int __stream_contains(FILE* stream, const char* needle)
{
    char   buffer[4096];
    size_t read;

    rewind(stream);
    read = fread(&buffer[0], 1, sizeof(buffer) - 1, stream);
    buffer[read] = '\0';
    return strstr(&buffer[0], needle) != NULL;
}

int main(void)
{
    FILE* stream;

    stream = tmpfile();
    if (stream == NULL) {
        fprintf(stderr, "tmpfile failed\n");
        return 1;
    }

    // start disabled so the default stdout sink stays quiet during the test
    vlog_initialize(VLOG_LEVEL_DISABLED);
    if (vlog_sink_add_text(stream, 0) != 0) {
        fprintf(stderr, "failed to add file sink\n");
        vlog_cleanup();
        fclose(stream);
        return 1;
    }
    vlog_set_output_options(stream, VLOG_OUTPUT_OPTION_NODECO);
    vlog_set_output_level(stream, VLOG_LEVEL_WARNING);

    VLOG_ERROR("test", "error-line\n");
    VLOG_WARNING("test", "warning-line\n");
    VLOG_TRACE("test", "trace-line\n");
    VLOG_DEBUG("test", "debug-line\n");
    vlog_flush();

    vlog_sink_remove(stream);
    vlog_cleanup();

    if (!__stream_contains(stream, "error-line") ||
        !__stream_contains(stream, "warning-line") ||
        __stream_contains(stream, "trace-line") ||
        __stream_contains(stream, "debug-line")) {
        fprintf(stderr, "sink level filtering did not match expectations\n");
        fclose(stream);
        return 1;
    }

    fclose(stream);
    return 0;
}
