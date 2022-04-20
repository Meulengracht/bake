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
#include <libplatform.h>
#include <stdlib.h>
#include <string.h>

char* strreplace(char* text, const char* find, const char* replaceWith)
{
    char* result;            // the return string
    char* ins;               // the next insert point
    char* tmp;               // varies
    int   lengthFind;        // length of find (the string to remove)
    int   lengthReplaceWith; // length of replaceWith (the string to replace find with)
    int   lengthFront;       // distance between find and end of last find
    int   count;             // number of replacements

    // sanity checks and initialization
    if (!text || !find) {
        errno = EINVAL;
        return NULL;
    }

    // empty find causes infinite loop during count
    lengthFind = strlen(find);
    if (lengthFind == 0) {
        errno = EINVAL;
        return NULL;
    }

    if (!replaceWith) {
        replaceWith = "";
    }
    lengthReplaceWith = strlen(replaceWith);

    // count the number of replacements needed
    ins = text;
    for (count = 0; tmp = strstr(ins, find); ++count) {
        ins = tmp + lengthFind;
    }

    tmp = result = malloc(strlen(text) + (lengthReplaceWith - lengthFind) * count + 1);
    if (!result) {
        errno = ENOMEM;
        return NULL;
    }

    // first time through the loop, all the variable are set correctly
    // from here on,
    //    tmp points to the end of the result string
    //    ins points to the next occurrence of find in text
    //    text points to the remainder of text after "end of find"
    while (count--) {
        ins = strstr(text, find);
        lengthFront = ins - text;
        tmp = strncpy(tmp, text, lengthFront) + lengthFront;
        tmp = strcpy(tmp, replaceWith) + lengthReplaceWith;
        text += lengthFront + lengthFind; // move to next "end of find"
    }
    strcpy(tmp, text);
    return result;
}
