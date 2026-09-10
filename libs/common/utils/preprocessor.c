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

#include <chef/build-common.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

/**
 * @brief Append a byte range to a NUL-terminated, heap-owned buffer.
 *
 * The helper keeps the buffer terminated after every successful append. That
 * makes it safe for callers to pass the buffer to string functions while they
 * are still building the result.
 */
static int __append(char** buffer, size_t* length, const char* value, size_t size)
{
    char* grown;

    // Reject the append before doing arithmetic that could wrap around.
    if (*length == (size_t)-1 || size > (size_t)-1 - *length - 1) {
        errno = EOVERFLOW;
        return -1;
    }

    grown = realloc(*buffer, *length + size + 1);
    if (grown == NULL) {
        return -1;
    }

    *buffer = grown;
    memcpy(grown + *length, value, size);
    *length += size;
    grown[*length] = '\0';
    return 0;
}

char* chef_preprocess_text(const char* original, const char* (*resolve)(const char*, void*), void* context)
{
    char* result;
    size_t length = 0;
    const char* at = original;

    if (original == NULL) {
        errno = EINVAL;
        return NULL;
    }

    result = calloc(1, 1);
    if (result == NULL) {
        return NULL;
    }

    // Leading spaces are formatting around the complete expression, not data.
    while (*at == ' ') {
        at++;
    }

    // Scan until every input byte has either been copied or expanded.
    while (*at != '\0') {
        // A delimiter starts either a Chef variable or an environment lookup.
        if (strncmp(at, "$[", 2) == 0) {
            int variable = at[2] == '[';
            const char* start = at + (variable ? 3 : 2);
            const char* end = variable ? strstr(start, "]]") : strchr(start, ']');
            const char* trimmed;
            const char* value;
            char* name;

            // A missing closing delimiter makes the expression malformed.
            if (end == NULL) {
                errno = EINVAL;
                goto error;
            }

            at = end + (variable ? 2 : 1);
            // Ignore spaces immediately inside the opening delimiter.
            while (start < end && *start == ' ') {
                start++;
            }

            trimmed = end;
            // Ignore spaces immediately before the closing delimiter.
            while (trimmed > start && trimmed[-1] == ' ') {
                trimmed--;
            }

            // An empty name cannot identify either kind of variable.
            if (trimmed == start) {
                errno = EINVAL;
                goto error;
            }

            name = platform_strndup(start, trimmed - start);
            if (name == NULL) {
                goto error;
            }

            value = variable ? (resolve ? resolve(name, context) : NULL) : getenv(name);
            free(name);

            // Expansion cannot continue when the requested name is unknown.
            if (value == NULL) {
                errno = ENOENT;
                goto error;
            }

            // Append the resolved value and keep scanning after its delimiter.
            if (__append(&result, &length, value, strlen(value)) != 0) {
                goto error;
            }
        } else {
            const char* end = strstr(at, "$[");

            // Copy ordinary text in one range to avoid reallocating per byte.
            if (end == NULL) {
                end = at + strlen(at);
            }

            if (__append(&result, &length, at, end - at) != 0) {
                goto error;
            }

            at = end;
        }
    }

    return result;

error:
    free(result);
    return NULL;
}

/**
 * @brief Expand and join a recipe argument list.
 *
 * Each list item is expanded independently before being joined with one
 * whitespace character. The returned string is owned by the caller.
 */
const char* chef_process_argument_list(struct list* argumentList,
    const char* (*resolve)(const char*, void*), void* context)
{
    struct list_item* item;
    char* result = calloc(1, 1);
    size_t length = 0;

    if (result == NULL) {
        return NULL;
    }

    // A missing recipe list represents an empty argument string.
    if (argumentList == NULL) {
        return result;
    }

    // Expand each list entry independently so its variables use the same context.
    list_foreach(argumentList, item) {
        struct list_item_string* value = (struct list_item_string*)item;
        char* expanded = chef_preprocess_text(value->value, resolve, context);
        size_t size;

        // Stop immediately because returning a partial command would be unsafe.
        if (expanded == NULL) {
            goto error;
        }

        size = strlen(expanded);
        // Separate two non-empty expanded arguments with one command space.
        if (length != 0 && size != 0 && __append(&result, &length, " ", 1) != 0) {
            free(expanded);
            goto error;
        }

        // Append through the shared overflow-checked buffer helper.
        if (__append(&result, &length, expanded, size) != 0) {
            free(expanded);
            goto error;
        }

        free(expanded);
    }

    return result;

error:
    free(result);
    return NULL;
}
