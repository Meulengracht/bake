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
#include <stdint.h>
#include <stdlib.h>

#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>

static const char g_az09_alphabet[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static int __read_full(int fd, uint8_t* buffer, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        ssize_t n = read(fd, buffer + offset, length - offset);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (n == 0) {
            errno = EIO;
            return -1;
        }
        offset += (size_t)n;
    }

    return 0;
}

int platform_secure_random_bytes(void* buffer, size_t length)
{
    uint8_t* out = (uint8_t*)buffer;

    if (out == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (length == 0) {
        return 0;
    }

    // Prefer getrandom(2) when available.
    size_t offset = 0;
    while (offset < length) {
        ssize_t n = getrandom(out + offset, length - offset, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == ENOSYS) {
                break;
            }
            return -1;
        }
        offset += (size_t)n;
    }

    if (offset == length) {
        return 0;
    }

    // Fallback: /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    int status = __read_full(fd, out + offset, length - offset);
    close(fd);
    return status;
}

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
