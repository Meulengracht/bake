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

#ifndef __CONTAINERV_JSON_UTIL_H__
#define __CONTAINERV_JSON_UTIL_H__

#include <jansson.h>
#include <stdint.h>

// Internal helpers for constructing and serializing JSON documents with Jansson.
// Not part of the public API.

extern int containerv_json_object_set_string(json_t* obj, const char* key, const char* value);
extern int containerv_json_object_set_bool(json_t* obj, const char* key, int value);
extern int containerv_json_object_set_int(json_t* obj, const char* key, json_int_t value);
extern int containerv_json_object_set_uint64(json_t* obj, const char* key, uint64_t value);

extern int containerv_json_array_append_string(json_t* arr, const char* value);

// Serializes JSON into a newly allocated UTF-8 buffer (malloc/free).
// Caller owns *jsonOut and must free() it.
extern int containerv_json_dumps_compact(json_t* root, char** jsonOut);

#endif // !__CONTAINERV_JSON_UTIL_H__
