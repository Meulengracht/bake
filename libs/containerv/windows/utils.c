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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

// Small helpers shared by the Windows backend modules.

#include <chef/platform.h>
#include <stdlib.h>
#include <vlog.h>

#include "private.h"

int __windows_prepare_share_dir(const char* host_path, int readonly)
{
    struct platform_stat st;

    if (platform_stat(host_path, &st) != 0) {
        if (readonly) {
            VLOG_ERROR("containerv", "shared directory missing (readonly): %s\n", host_path);
            return -1;
        }
        if (platform_mkdir(host_path) != 0) {
            VLOG_ERROR("containerv", "failed to create shared directory %s\n", host_path);
            return -1;
        }
        return 0;
    }

    if (st.type != PLATFORM_FILETYPE_DIRECTORY) {
        VLOG_ERROR("containerv", "shared host path is not a directory: %s\n", host_path);
        return -1;
    }
    return 0;
}

void __windows_strv_free(char** values, int count)
{
    if (values == NULL) {
        return;
    }
    for (int i = 0; i < count; i++) {
        free(values[i]);
    }
    free(values);
}

wchar_t* __windows_utf8_to_wide_alloc(const char* text)
{
    int      needed;
    wchar_t* out;

    if (text == NULL) {
        return NULL;
    }

    needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    if (needed <= 0) {
        return NULL;
    }

    out = calloc((size_t)needed, sizeof(wchar_t));
    if (out == NULL) {
        return NULL;
    }

    if (MultiByteToWideChar(CP_UTF8, 0, text, -1, out, needed) == 0) {
        free(out);
        return NULL;
    }
    return out;
}

char* __windows_wide_to_utf8_alloc(const wchar_t* text)
{
    int   needed;
    char* out;

    if (text == NULL) {
        return NULL;
    }

    needed = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
    if (needed <= 0) {
        return NULL;
    }

    out = calloc((size_t)needed, 1);
    if (out == NULL) {
        return NULL;
    }

    if (WideCharToMultiByte(CP_UTF8, 0, text, -1, out, needed, NULL, NULL) == 0) {
        free(out);
        return NULL;
    }
    return out;
}
