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

#include <chef/environment.h>
#include <chef/platform.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char* __append_valuev(const char* value, char** values, char* sep)
{
    size_t length;
    if (values == NULL) {
        return 0;
    }
    return strflatten((const char* const*)values, sep, &length);
}

static char* __fmt_env_kv(const char* key, const char* value)
{
    char  tmp[512];
    snprintf(&tmp[0], sizeof(tmp), "%s%s", key, value);
    return platform_strdup(&tmp[0]);
}

int environment_append_keyv(char** envp, char* key, char** values, char* sep)
{
    char skey[512];
    int  skeyLength;

    skeyLength = snprintf(&skey[0], sizeof(skey), "%s=", key);
    for (int i = 0; envp[i] != NULL; i++) {
        if (strncmp(envp[i], &skey[0], skeyLength) == 0) {
            // found, split value from key, we can assume it
            // exists (the separator), since we matched with it
            char* value = strchr(envp[i], '=');
            
            // skip over '='
            value++;

            // now do the actual replacing
            value = __append_valuev(value, values, sep);
            if (value != NULL) {
                char* updated = __fmt_env_kv(&skey[0], value);
                if (updated == NULL) {
                    free(value);
                    return -1;
                }
                free(envp[i]);
                envp[i] = updated;
            }
            return 0;
        }
    }

    errno = ENOENT;
    return -1;
}

static int __contains_envkey(struct list* list, const char* key)
{
    struct list_item* item;

    list_foreach(list, item) {
        struct chef_keypair_item* keypair = (struct chef_keypair_item*)item;
        const char*               end     = strchr(key, '=');

        // we need to make sure lengths are equal as well to avoid false positives
        if (end != NULL && strncmp(keypair->key, key, end - key) == 0 && strlen(keypair->key) == (end - key)) {
            return 1;
        }
    }
    return 0;
}

char** environment_create(const char* const* parent, struct list* additional)
{
    struct list_item* item;
    char**            environment;
    int               entryCount = additional->count;
    int               i = 0;
    int               j = 0; // keeps track of index into environment

    // unreadable mess that simply uses 'entryCount' as an
    // iterator from 0.
    while (parent[entryCount - additional->count]) {
        entryCount++;
    }

    environment = (char**)calloc(entryCount + 1, sizeof(char*));
    if (!environment) {
        return NULL;
    }

    // copy all variables over, but we skip those that are provided in additional
    // list, as we want to use that one instead
    while (parent[i]) {
        if (!__contains_envkey(additional, parent[i])) {
            environment[j++] = platform_strdup(parent[i]);
        }
        i++;
    }

    list_foreach(additional, item) {
        struct chef_keypair_item* keypair    = (struct chef_keypair_item*)item;
        size_t                    lineLength = strlen(keypair->key) + strlen(keypair->value) + 2;
        char*                     line       = (char*)calloc(lineLength, sizeof(char));
        if (line == NULL) {
            return NULL;
        }

        snprintf(line, lineLength, "%s=%s", keypair->key, keypair->value);
        environment[j++] = line;
    }
    
    return environment;
}

int environment_contains_key_insensitive(const char* const* environment, const char* key)
{
    size_t keyLength;

    if (environment == NULL || key == NULL || key[0] == '\0') {
        return 0;
    }

    keyLength = strlen(key);
    for (int i = 0; environment[i] != NULL; i++) {
        const char* kv = environment[i];
        if (kv == NULL) {
            continue;
        }
        
        // Compare KEY + '=' case-insensitively.
        for (size_t j = 0; j < keyLength; ++j) {
            char a = kv[j];
            char b = key[j];
            if (a == '\0') {
                goto next;
            }
            if (tolower(a) != tolower(b)) {
                goto next;
            }
        }

        if (kv[keyLength] == '=') {
            return 1;
        }

    next:
        (void)0;
    }

    return 0;
}

char* environment_flatten(const char* const* environment, size_t* lengthOut)
{
    char*  flatEnvironment;
    size_t flatLength = 1; // second nil
    size_t i = 0, j = 0;

    while (environment[i]) {
        flatLength += strlen(environment[i++]) + 1;
    }

    flatEnvironment = calloc(flatLength, 1);
    if (flatEnvironment == NULL) {
        return NULL;
    }

    i = 0;
    while (environment[i]) {
        size_t len = strlen(environment[i]) + 1;
        memcpy(&flatEnvironment[j], environment[i], len);
        j += len;
        i++;
    }
    *lengthOut = flatLength;
    return flatEnvironment;
}

char** environment_unflatten(const char* text)
{
	char** results;
	int    count = 1; // add zero terminator
	int    index = 0;

	if (text == NULL) {
		return NULL;
	}

	for (const char* p = text;; p++) {
		if (*p == '\0') {
			count++;
			
			if (*p == '\0' && *(p + 1) == '\0') {
			    break;
			}
		}
	}
	
	results = (char**)calloc(count, sizeof(char*));
	if (results == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	for (const char* p = text;; p++) {
		if (*p == '\0') {
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
			
			if (*p == '\0' && *(p + 1) == '\0') {
			    break;
			}
		}
	}
	return results;
}

void environment_destroy(char** environment)
{
    int i = 0;
    
    if (!environment) {
        return;
    }

    while (environment[i]) {
        free(environment[i]);
        i++;
    }
    free((void*)environment);
}
