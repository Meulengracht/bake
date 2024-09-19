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
                if (*(fi + 1) == '*') {
                    // recursive wildcard match
                    const char* j = i;
                    while (*j) {
                        if (strfilter(fi + 2, j, flags) == 0) {
                            break;
                        }
                        
                        // level did not match, keep descending
                        while (*j && *(++j) != CHEF_PATH_SEPARATOR);
                    }
                    if (!*j) {
                        return -1;
                    }
                    i = j;
                    fi++;
                    
                } else {
                    // wildcard match
                    // * does not match '/'
                    while (*i != CHEF_PATH_SEPARATOR && FOLD(*i) != FOLD(*(fi + 1))) {
                        i++;
                    }
                }
                break;
            case '?':
                // wildcard match (one character)
                // ? does not match '/'
                if (*i != CHEF_PATH_SEPARATOR) {
                    i++;
                }
                break;
            case '[':
                // match either of the characters inside the bracket
                int match = 0;
                while (*(++fi) != ']') {
                    // is it a range (i.e a-zA-Z) 
                    if (*(fi + 1) == '-' && isalnum(*(fi + 2))) {
                        char start = *fi;
                        char end = *(fi + 2);
                        if (FOLD(*i) >= FOLD(start) && FOLD(*i) <= FOLD(end)) {
                            match = 1;
                            break;
                        }
                        fi += 2;
                    } else if (FOLD(*i) == FOLD(*fi)) {
                        match = 1;
                        break;
                    }
                }
                if (!match) {
                    // not found, not a match
                    return -1;
                }
                // skip over rest of the case
                while (*(++fi) != ']');
                i++;
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

#if 0

int main()
{
    printf("positive tests:\n");
    printf("/my/test/path vs /my/test/path: %i\n", strfilter("/my/test/path", "/my/test/path", 0));
    printf("\\!/my/test/path vs !/my/test/path: %i\n", strfilter("\\!/my/test/path", "!/my/test/path", 0));
    printf("/my/*/path vs /my/test/path: %i\n", strfilter("/my/*/path", "/my/test/path", 0));
    printf("/**/path vs /my/test/path: %i\n", strfilter("/**/path", "/my/test/path", 0));
    printf("/my/** vs /my/test/path: %i\n", strfilter("/my/**", "/my/test/path", 0));
    printf("/my/[Tt]est/path vs /my/test/path: %i\n", strfilter("/my/[tT]est/path", "/my/test/path", 0));
    printf("/my/[a-Z]est/path vs /my/test/path: %i\n", strfilter("/my/[tT]est/path", "/my/test/path", 0));
    
    printf("\nnegation tests:\n");
    printf("!/my/test/path vs /my/test/path: %i\n", strfilter("!/my/test/path", "/my/test/path", 0));
    printf("/**/path/two vs /my/test/path/one: %i\n", strfilter("/**/path/two", "/my/test/path/one", 0));

    return 0;
}

#endif