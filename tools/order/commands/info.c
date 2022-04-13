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
#include <chef/client.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void __print_help(void)
{
    printf("Usage: order info <publisher/pack> [options]\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static int __parse_packname(char* pack, struct chef_info_params* params)
{
    // pack names are in the form of publisher/name, we need to seperate those
    char* slash = strchr(pack, '/');
    if (!slash) {
        errno = EINVAL;
        return -1;
    }

    *slash = '\0';

    params->publisher = pack;
    params->package   = slash + 1;
    return 0;
}

static void __print_channel(struct chef_channel* channel)
{
    printf("      * %s   %i.%i.%i (%i)",
        channel->name, 
        channel->current_version.major,
        channel->current_version.minor,
        channel->current_version.patch,
        channel->current_version.revision
    );

    if (channel->current_version.tag != NULL) {
        printf("%s", channel->current_version.tag);
    }
    printf("\n");
}

static void __print_architecture(struct chef_architecture* architecture)
{
    printf("    * %s\n", architecture->name);
    printf("      Channels:\n");
    for (int i = 0; i < architecture->channels_count; i++) {
        __print_channel(&architecture->channels[i]);
    }
}

static void __print_platform(struct chef_platform* platform)
{
    printf("  * %s\n", platform->name);
    printf("    Architectures:\n");
    for (int i = 0; i < platform->architectures_count; i++) {
        __print_architecture(&platform->architectures[i]);
    }
}

static char* __strip_newlines(const char* text)
{
    char* result = strdup(text);
    char* ptr    = result;
    while (*ptr != '\0') {
        if (*ptr == '\n') {
            *ptr = ' ';
        }
        ptr++;
    }
    return result;
}

// Given a text, split the text into lines of max length <lineWidth>
// but do not breakup words.
static char** __word_wrap(const char* text, const char* prefix, const char* padding, int lineWidth)
{
    char** result        = NULL;
    int    i             = 0;
    int    lineCount     = 0;
    size_t prefixLength  = 0;
    size_t paddingLength = 0;
    
    if (text == NULL || lineWidth <= 0) {
        errno = EINVAL;
        return NULL;
    }
    
    if (prefixLength >= lineWidth || paddingLength >= lineWidth) {
        errno = EINVAL;
        return NULL;
    }
    
    if (prefix != NULL) {
        prefixLength = strlen(prefix);
    }
    if (padding != NULL) {
        paddingLength = strlen(padding);
    }

    while (text[i]) {
        int j;
        int lastSpace;
        int lineLength = lineWidth;
        
        // skip leading spaces
        while (text[i] == ' ') {
            i++;
        }
        
        j = i;
        lastSpace = i;

        // correct lineLength, for the first line we use prefix and for the
        // remaining lines we use padding
        if (i == 0) {
            lineLength -= prefixLength;
        } else {
            lineLength -= paddingLength;
        }
        
        // keep skipping words while we are under limit
        while ((j - i) < lineLength) {
            if (text[j] == '\0') {
                lastSpace = j;
                break;
            }
            if (text[j] == ' ') {
                lastSpace = j;
            }
            j++;
        }
        
        // correct j by going back to last space
        j = lastSpace;
        
        // did we not find a space in time?
        if (lastSpace == i) {
            // accept line is going to be long
            j = i;
            while (text[j] && text[j] != ' ') {
                j++;
            }
        }
        
        // i is now start, j is end and points to space
        lineCount++;
        result = (char**)realloc(result, (lineCount + 1) * sizeof(char*));
        if (!result) {
            errno = ENOMEM;
            return NULL;
        }
        
        // now we create the line
        result[lineCount - 1] = malloc(lineWidth - lineLength + (j - i) + 1);
        result[lineCount]     = NULL;
        
        // build line
        if (prefix != NULL && i == 0) {
            strcpy(result[lineCount - 1], prefix);
            strncat(result[lineCount - 1], &text[i], j - i);
        } else if (padding != NULL) {
            strcpy(result[lineCount - 1], padding);
            strncat(result[lineCount - 1], &text[i], j - i);
        } else {
            strncpy(result[lineCount - 1], &text[i], j - i);
            result[lineCount - 1][j - i] = 0;
        }
        
        i = j;
    }
    return result;
}

static int __print_description(const char* prefix, const char* padding, const char* description)
{
    char*  stripped = __strip_newlines(description);
    char** lines    = __word_wrap(stripped, prefix, padding, 65);
    int    i        = 0;

    free(stripped);
    if (!lines) {
        return -1;
    }

    while (lines[i]) {
        printf("%s\n", lines[i]);
        free(lines[i]);
        i++;
    }
    free(lines);
    return 0;
}

static void __print_package(struct chef_package* package)
{
    printf("Name:             %s\n", package->package);
    printf("Publisher:        %s\n", package->publisher);
    printf("Summary:          %s\n", package->summary);
    __print_description("Description:      ", "    ", package->description);
    printf("Homepage:         %s\n", package->homepage);
    printf("License:          %s\n", package->license);
    printf("Maintainer:       %s\n", package->maintainer);
    printf("Maintainer Email: %s\n", package->maintainer_email);
    printf("EULA:             %s\n", (package->eula != NULL ? "yes" : "no"));
    
    printf("Platforms:\n");
    for (int i = 0; i < package->platforms_count; i++) {
        __print_platform(&package->platforms[i]);
    }

    printf("\n");
}

int info_main(int argc, char** argv)
{
    struct chef_info_params params   = { 0 };
    struct chef_package*    package;
    char*                   packCopy = NULL;
    int                     status;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
            else {
                // do this only once
                if (params.publisher == NULL) {
                // assume this is the pack name
                    packCopy = strdup(argv[i]);
                    status   = __parse_packname(packCopy, &params);
                    if (status != 0) {
                        free(packCopy);
                        fprintf(stderr, "order: failed to parse pack name: %s\n", strerror(errno));
                        return status;
                    }
                }
                else {
                    free(packCopy);
                    fprintf(stderr, "order: too many arguments\n");
                    __print_help();
                    return -1;
                }
            }
        }
    }

    if (params.publisher == NULL) {
        fprintf(stderr, "order: missing pack name\n");
        __print_help();
        return -1;
    }

    // initialize chefclient
    status = chefclient_initialize();
    if (status != 0) {
        free(packCopy);
        fprintf(stderr, "order: failed to initialize chefclient: %s\n", strerror(errno));
        return -1;
    }
    atexit(chefclient_cleanup);

    // retrieve information about the pack
    status = chefclient_pack_info(&params, &package);
    if (status != 0) {
        printf("order: failed to retrieve information: %s\n", strerror(errno));
        goto cleanup;
    }

    __print_package(package);
    chef_package_free(package);

cleanup:
    free(packCopy);
    return status;
}
