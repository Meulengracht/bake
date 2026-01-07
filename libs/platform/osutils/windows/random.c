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
#include <bcrypt.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>

int platform_secure_random_bytes(void* buffer, size_t length)
{
    if (buffer == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    if (length > (size_t)ULONG_MAX) {
        errno = EINVAL;
        return -1;
    }

    NTSTATUS status = BCryptGenRandom(
        NULL,
        (PUCHAR)buffer,
        (ULONG)length,
        BCRYPT_USE_SYSTEM_PREFERRED_RNG
    );

    if (status != 0) {
        errno = EIO;
        return -1;
    }

    return 0;
}

static const char g_az09_alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

int platform_secure_random_string(char* out, size_t length)
{
    size_t i = 0;

    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }

    while (i < length) {
        uint8_t b;
        int     status = platform_secure_random_bytes(&b, sizeof(b));
        if (status) {
            return status;
        }

        // Rejection sampling to avoid modulo bias.
        // 252 is the largest multiple of 36 < 256.
        if (b >= 252) {
            continue;
        }

        out[i++] = g_az09_alphabet[b % 36];
    }

    out[length] = 0;
    return 0;
}

char* platform_secure_random_string_new(size_t length)
{
    char* out = malloc(length + 1);
    if (out == NULL) {
        return NULL;
    }

    if (platform_secure_random_string(out, length)) {
        free(out);
        return NULL;
    }

    return out;
}
