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

#include "json-util.h"
#include <stdlib.h>

int containerv_json_object_set_string(json_t* obj, const char* key, const char* value)
{
    if (obj == NULL || key == NULL) {
        return -1;
    }
    if (value == NULL) {
        value = "";
    }
    return json_object_set_new(obj, key, json_string(value));
}

int containerv_json_object_set_bool(json_t* obj, const char* key, int value)
{
    if (obj == NULL || key == NULL) {
        return -1;
    }
    return json_object_set_new(obj, key, value ? json_true() : json_false());
}

int containerv_json_object_set_int(json_t* obj, const char* key, json_int_t value)
{
    if (obj == NULL || key == NULL) {
        return -1;
    }
    return json_object_set_new(obj, key, json_integer(value));
}

int containerv_json_object_set_uint64(json_t* obj, const char* key, uint64_t value)
{
    if (obj == NULL || key == NULL) {
        return -1;
    }
    
    // json_int_t is typically int64_t. Clamp to signed range to avoid UB.
    if (value > (uint64_t)INT64_MAX) {
        return json_object_set_new(obj, key, json_integer((json_int_t)INT64_MAX));
    }
    return json_object_set_new(obj, key, json_integer((json_int_t)value));
}

int containerv_json_array_append_string(json_t* arr, const char* value)
{
    if (arr == NULL || !json_is_array(arr)) {
        return -1;
    }
    if (value == NULL) {
        value = "";
    }
    return json_array_append_new(arr, json_string(value));
}

int containerv_json_dumps_compact(json_t* root, char** jsonOut)
{
    char* dumped;

    if (root == NULL || jsonOut == NULL) {
        return -1;
    }

    dumped = json_dumps(root, JSON_COMPACT);
    if (dumped == NULL) {
        return -1;
    }

    *jsonOut = dumped;
    return 0;
}
