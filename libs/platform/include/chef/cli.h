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

#ifndef __PLATFORM_CLI_H__
#define __PLATFORM_CLI_H__

#include <chef/list.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

static uint64_t __parse_quantity(const char* size)
{
    char*    end = NULL;
    uint64_t number = strtoull(size, &end, 10);
    switch (*end)  {
        case 'G':
            number *= 1024;
            // fallthrough
        case 'M':
            number *= 1024;
            // fallthrough
        case 'K':
            number *= 1024;
            break;
        default:
            break;
    }
    return number;
}

static char* __split_switch(char** argv, int argc, int* i)
{
    char* split = strchr(argv[*i], '=');
    if (split != NULL) {
        return split + 1;
    }
    if ((*i + 1) < argc) {
        return argv[++(*i)];
    }
    return NULL;
}

static int __parse_string_switch(char** argv, int argc, int* i, const char* s, size_t sl, const char* l, size_t ll, const char* defaultValue, char** out)
{
    if (strncmp(argv[*i], s, sl) == 0 || strncmp(argv[*i], l, ll) == 0) {
        char* value = __split_switch(argv, argc, i);
        *out = value != NULL ? value : (char*)defaultValue;
        return 0;
    }
    return -1;
}

static int __parse_quantity_switch(char** argv, int argc, int* i, const char* s, size_t sl, const char* l, size_t ll, uint64_t defaultValue, uint64_t* out)
{
    if (strncmp(argv[*i], s, sl) == 0 || strncmp(argv[*i], l, ll) == 0) {
        char* value = __split_switch(argv, argc, i);
        *out = value != NULL ? __parse_quantity(value) : defaultValue;
        return 0;
    }
    return -1;
}

static int __add_string_to_list(const char* str, struct list* out)
{
    struct list_item_string* item = malloc(sizeof(struct list_item_string));
    if (item == NULL) {
        return -1;
    }
    item->value = str;
    list_add(out, &item->list_header);
    return 0;
}

static int __split_stringv_into_list(char* str, struct list* out)
{
    char* p = str;
    int   status;

    // dont return an error on a null value
    if (str == NULL) {
        return 0;
    }

    // add the initial string
    status = __add_string_to_list(p, out);
    if (status) {
        return status;
    }

    // iterate through and add additional strings, we modify the original
    // string in place, which is fine.
    while (*p) {
        if (*p == ',') {
            *p = '\0';

            status = __add_string_to_list(p + 1, out);
            if (status) {
                return status;
            }
        }
        p++;
    }
    return 0;
}

static int __parse_stringv_switch(char** argv, int argc, int* i, const char* s, size_t sl, const char* l, size_t ll, const char* defaultValue, struct list* out)
{
    if (strncmp(argv[*i], s, sl) == 0 || strncmp(argv[*i], l, ll) == 0) {
        char* value = __split_switch(argv, argc, i);
        return __split_stringv_into_list(value != NULL ? value : (char*)defaultValue, out);
    }
    return -1;
}

#endif //!__PLATFORM_CLI_H__
