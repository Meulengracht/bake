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

#include <chef/platform.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

char** strargv(char* arguments, const char* arg0, int* argc)
{
    size_t count = 0;
    size_t capacity = 8;
    char** argv;
    char*  read = arguments;
    char*  write = arguments;

    if (argc != NULL) {
        *argc = 0;
    }

    argv = calloc(capacity, sizeof(char*));
    if (argv == NULL) {
        return NULL;
    }

    if (arg0 != NULL) {
        argv[count++] = (char*)arg0;
    }

    // parse from the read pointer
    while (read != NULL && *read != '\0') {
        char quote = 0;

        // Whitespace outside quotes separates the next argument.
        while (isspace((unsigned char)*read)) {
            read++;
        }

        // Ignore trailing whitespaces
        if (*read == '\0') {
            break;
        }

        // ensure there is space in the argv array
        if (count + 1 >= capacity) {
            char** grown;

            // sanity check the capacity
            if (capacity > SIZE_MAX / sizeof(char*) / 2) {
                goto invalid;
            }

            capacity *= 2;
            grown = realloc(argv, capacity * sizeof(char*));
            if (grown == NULL) {
                free(argv);
                return NULL;
            }

            argv = grown;
        }

        argv[count++] = write;
        
        // Copy arguments but support quoted arguments
        while (*read != '\0' && (quote != 0 || !isspace((unsigned char)*read))) {
            // Preserve Windows paths; backslashes only escape double quotes.
            if (*read == '\\' && quote != '\'') {
                size_t slashes = 0;

                // Count consecutive backslashes before applying CRT rules.
                while (*read == '\\') {
                    read++;
                    slashes++;
                }

                // Two slashes is a slash, one slash is an escape
                if (*read == '"') {
                    for (size_t i = 0; i < slashes / 2; i++) {
                        *write++ = '\\';
                    }

                    if (slashes % 2 != 0) {
                        *write++ = *read++;
                        continue;
                    }
                } else {
                    // Backslashes before ordinary text are copied literally.
                    while (slashes > 0) {
                        *write++ = '\\';
                        slashes--;
                    }

                    continue;
                }
            }

            // handle quoted arguments
            if (*read == '"' || *read == '\'') {
                // we found the start of a quote
                if (quote == 0) {
                    quote = *read++;
                    continue;
                }

                // we found the end of the quote
                if (quote == *read) {
                    quote = 0;
                    read++;
                    continue;
                }
            }

            *write++ = *read++;
        }

        // ensure the quote has been closed, otherwise reject
        // the argument
        if (quote != 0) {
            goto invalid;
        }

        // Advance before terminating, as read and write may be equal.
        while (isspace((unsigned char)*read)) {
            read++;
        }
        *write++ = '\0';
    }

    // sanity check
    if (count > INT_MAX) {
        goto invalid;
    }

    argv[count] = NULL;
    if (argc != NULL) {
        *argc = (int)count;
    }

    return argv;

invalid:
    free(argv);
    errno = EINVAL;
    return NULL;
}

void strargv_free(char** argv)
{
    free(argv);
}

char* strargv_windows(const char* const* arguments)
{
    size_t length = 1;
    char* result;
    char* out;

    // Calculate the exact serialized size before allocating the command line.
    for (size_t i = 0; arguments != NULL && arguments[i] != NULL; i++) {
        size_t size = strlen(arguments[i]);

        // Each argument may double its slashes and needs quotes plus a separator.
        if (length > SIZE_MAX - 3 ||
            size > (SIZE_MAX - length - 3) / 2) {
            errno = EOVERFLOW;
            return NULL;
        }

        length += size * 2 + 3;
    }

    result = malloc(length);
    if (result == NULL) {
        return NULL;
    }

    out = result;
    
    // the point here is to serialize all arguments in sequence to avoid
    // shell expansion
    for (size_t i = 0; arguments != NULL && arguments[i] != NULL; i++) {
        const char* in = arguments[i];

        // insert seperator for all subsequent arguments
        if (i != 0) {
            *out++ = ' ';
        }
        *out++ = '"';

        // we want to parse the argument, and preserve the needed quotes
        for (;;) {
            size_t slashes = 0;
            size_t count;

            // Group slashes so only those before quotes or the closing quote double.
            while (*in == '\\') {
                slashes++;
                in++;
            }

            // CRT: double backslashes before a quote or the closing quote.
            count = (*in == '"' || !*in) ? slashes * 2 : slashes;
            for (size_t i = 0; i < count; i++) {
                *out++ = '\\';
            }

            // done with this argument
            if (*in == '\0') {
                break;
            }

            // CRT: embedded quotes need one extra slash for parsing
            if (*in == '"') {
                *out++ = '\\';
            }

            *out++ = *in++;
        }

        *out++ = '"';
    }

    *out = '\0';
    return result;
}
