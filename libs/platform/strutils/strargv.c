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

#include <errno.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <string.h>

static int __get_arg_count(const char* arguments)
{
    const char* arg   = arguments;
    int         count = 0;

    if (arg == NULL) {
        return 0;
    }
    
    // count the initial argument
    if (*arg) {
        count++;
    }

    while (*arg) {
        // Whitespace denote the end of an argument
        if (*arg == ' ') {
            while (*arg && *arg == ' ') {
                arg++;
            }
            count++;
            continue;
        }

        // We must take quoted arguments into account, even whitespaces
        // in quoted paramters don't mean the end of an argument
        if (*arg == '"') {
            arg++;
            while (*arg && *arg != '"') {
                arg++;
            }
        }
        arg++;
    }
    return count;
}

char** strargv(char* arguments, const char* arg0, int* argc)
{
    char** argv;
    int    count = (arg0 != NULL) ? 1 : 0;
    int    i = 0;

    if (arguments != NULL) {
        while (*arguments && *arguments == ' ') {
            arguments++;
        }
    }

    // count the arguments
    count += __get_arg_count(arguments);
    if (argc != NULL) {
        *argc = count;
    }

    argv = calloc(count + 1, sizeof(char*));
    if (argv == NULL) {
        return NULL;
    }

    // set the provided initial arguments
    if (arg0 != NULL) {
        argv[i++] = (char*)arg0;
    }

    // skip the rest of this if arguments were
    // not supplied
    if (arguments == NULL || count == 1) {
        goto exit;
    }

    // set second initial argument
    argv[i++] = (char*)arguments;

    // parse the rest of the arguments
    while (*arguments) {
        // Whitespace denote the end of an argument
        if (*arguments == ' ') {
            // end the argument
            *arguments = '\0';
            arguments++;
            
            // trim leading spaces
            while (*arguments && *arguments == ' ') {
                arguments++;
            }

            // store the next argument
            argv[i++] = (char*)arguments;
            continue;
        }

        // We must take quoted arguments into account, even whitespaces
        // in quoted paramters don't mean the end of an argument
        if (*arguments == '"') {
            arguments++;

            // because of how spawn works, we need to strip the quotes
            // from the argument
            argv[i - 1] = (char*)arguments; // skip the first quote

            // now skip through the argument
            while (*arguments && *arguments != '"') {
                arguments++;
            }

            // at this point, we need to end the argument, so we replace
            // the closing quote with a null terminator
            if (!(*arguments)) {
                break;
            }
            *arguments = '\0';
        }
        arguments++;
    }

exit:
    return argv;
}

void strargv_free(char** argv)
{
    if (argv == NULL) {
        return;
    }
    free(argv);
}
