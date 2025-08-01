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

#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char        g_templateGuid[] = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";
static const char* g_hexValues = "0123456789ABCDEF-";
static uint8_t     g_guidmap[16] = { 6, 4, 2, 0, 11, 9, 16, 14, 19, 21, 24, 26, 28, 30, 32, 34 };

// yes i am well aware that this provides _very_ poor randomness
// but we do not require a cryptographically secure guid for this purpose

void platform_guid_new_string(char strbuffer[40])
{
    for (int t = 0; t < (sizeof(g_templateGuid) - 1); t++) {
        int  r = rand() % 16;
        char c = ' ';

        switch (g_templateGuid[t]) {
            case 'x' : { c = g_hexValues[r]; } break;
            case 'y' : { c = g_hexValues[(r & 0x03) | 0x08]; } break;
            case '-' : { c = '-'; } break;
            case '4' : { c = '4'; } break;
        }
        strbuffer[t] = c;
    }
    strbuffer[sizeof(g_templateGuid) - 1] = 0;
}

void platform_guid_new(unsigned char guid[16])
{
    int i = 0;
    for (int t = 0; t < (sizeof(g_templateGuid) - 1); t++) {
        int r0 = rand() % 16;
        int r1 = rand() % 16;

        switch (g_templateGuid[t]) {
            case '4' : 
                r0 = 4; 
            case 'x' : { 
                guid[i++] = r0 << 8 | r1;
                t++; // two is one byte
            } break;
            case 'y' : { 
                guid[i++] = ((r0 & 0x03) | 0x08) << 8 | r1; 
                t++; // two is one byte
            } break;
        }
    }
}

static uint8_t __char_to_hex(char c)
{
    if (c <= '9') {
        return ((uint8_t) (c - '0'));
    }
    if (c <= 'F') {
        return ((uint8_t) (c - 0x37));
    }
    return ((uint8_t) (c - 0x57));
}

static void __string_to_hex(size_t gi, uint8_t* out, const char* string, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        uint8_t m = g_guidmap[gi + i];
        out[i] = (__char_to_hex(string[m]) << 4) | __char_to_hex(string[m + 1]);
    }
}

static int __validate_guid(const char* string)
{
    size_t len = strlen(string);
    if (len != 36) {
        return -1;
    }

    for (size_t i = 0; i < len; ++i) {
        char c = string[i];
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (c != '-') {
                return -1;
            }
        }
        else if (!((c >= '0') && (c <= '9')) && !((c >= 'A') && (c <= 'F')) && !((c >= 'a') && (c <= 'f'))) {
            return -1;
        }
    }
    return 0;
}

void platform_guid_parse(unsigned char guid[16], char* str)
{
    if (__validate_guid(str)) {
        return;
    }

    __string_to_hex(0,  (uint8_t*)&guid[0],  str, 4);
    __string_to_hex(4,  (uint8_t*)&guid[4],  str, 2);
    __string_to_hex(6,  (uint8_t*)&guid[6],  str, 2);
    __string_to_hex(8,  (uint8_t*)&guid[8],  str, 2);
    __string_to_hex(10, (uint8_t*)&guid[10], str, 6);
}
