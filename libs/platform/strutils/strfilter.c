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

#include <ctype.h>
#include <errno.h>
#include <chef/platform.h>

#define FOLD(c) ((flags & FILTER_FOLDCASE) != 0 ? tolower((c)) : (c))

int strfilter(const char* filter, const char* text, int flags)
{
    const char* fi = filter;
    const char* i  = text;
    
    if (filter == NULL || text == NULL) {
        return -1;
    }
    
    // is it a negation filter?
    if (*fi == '!') {
        // it must not match
        return strfilter(fi + 1, text, flags) != 0 ? 0 : -1;
    }
    
    while (*fi && *i) {
        // match against text
        switch (*fi) {
            case '*':
                // wildcard match (string)
                // * does not match '/'
                while (*i != CHEF_PATH_SEPARATOR && FOLD(*i) != FOLD(*(fi + 1))) {
                    i++;
                }
                break;
            case '?':
                // wildcard match (one character)
                // ? does not match '/'
                if (*i != CHEF_PATH_SEPARATOR) {
                    i++;
                }
                break;
            
            // handle escapes, intentional fall-through
            case '\\':
                fi++;
            default:
                if (FOLD(*fi) != FOLD(*i)) {
                    return -1;
                }
                i++;
                break;
        }
        
        // catch any double-increment issues that would
        // ruin our termination condition
        if (*fi) {
            fi++;
        }
    }
    return 0;
}
