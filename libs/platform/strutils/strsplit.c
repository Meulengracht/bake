/**
 * Copyright 2022, Philip Meulengracht
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

#include <errno.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>

char** strsplit(const char* text, char sep)
{
	char** results;
	int    count = 1; // add zero terminator
	int    index = 0;

	for (const char* p = text;; p++) {
		if (*p == '\0' || *p == sep) {
			count++;
			
			if (*p == '\0') {
			    break;
			}
		}
	}
	
	results = (char**)malloc(sizeof(char*) * count);
	if (results == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	memset(results, 0, sizeof(char*) * count);

	for (const char* p = text;; p++) {
		if (*p == '\0' || *p == sep) {
			results[index] = (char*)malloc(p - text + 1);
			if (results[index] == NULL) {
			    // cleanup
				for (int i = 0; i < index; i++) {
					free(results[i]);
				}
				free(results);
				return NULL;
			}

			memcpy(results[index], text, p - text);
			results[index][p - text] = '\0';
			text = p + 1;
			index++;
			
			if (*p == '\0') {
			    break;
			}
		}
	}
	return results;
}

void strsplit_free(char** strings)
{
	if (strings == NULL) {
		return;
	}

	for (int i = 0; strings[i] != NULL; i++) {
		free(strings[i]);
	}
	free(strings);
}
