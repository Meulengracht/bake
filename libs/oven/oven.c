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
#include <stdio.h>
#include <string.h>

struct generate_backend {
    const char* name;
};

struct build_backend {
    const char* name;
};

static struct generate_backend g_genbackends[] = {
    { "configure" },
    { "cmake" }
};

static struct build_backend g_buildbackends[] = {
    { "make" }
};

// expose the following variables to the build process
// BAKE_BUILD_DIR
// BAKE_ARTIFACT_DIR
static int __oven_setup_environment(void)
{
    int status;

    status = platform_setenv("BAKE_BUILD_DIR", ".oven/build");
    if (status) {
        fprintf(stderr, "bake: failed to initialize build environment: %s\n", strerror(errno));
        return status;
    }

    status = platform_setenv("BAKE_ARTIFACT_DIR", ".oven/install");
    if (status) {
        fprintf(stderr, "bake: failed to initialize build environment: %s\n", strerror(errno));
        return status;
    }
    return 0;
}

// oven is the work-area for the build and pack
// .oven/build
// .oven/install
int oven_initialize(void)
{
    int status;
    
    status = platform_mkdir(".oven/build");
    if (status) {
        if (errno != EEXIST) {
            printf("bake: failed to create initialize build space\n");
            return -1;
        }
    }
    
    status = platform_mkdir(".oven/install");
    if (status) {
        if (errno != EEXIST) {
            printf("bake: failed to create initialize artifact space\n");
            return -1;
        }
    }
    return 0;
}

int oven_configure(void)
{
    return 0;
}

int oven_build(void)
{
    return 0;
}

int oven_pack(void)
{
    return 0;
}

int oven_cleanup(void)
{
    return 0;
}
