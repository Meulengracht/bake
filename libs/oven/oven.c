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
#include <liboven.h>
#include <libplatform.h>
#include <stdio.h>
#include <string.h>

extern int configure_main(struct oven_generate_options* options);
extern int cmake_main(struct oven_generate_options* options);

extern int make_main(struct oven_build_options* options);

struct generate_backend {
    const char* name;
    int       (*generate)(struct oven_generate_options* options);
};

struct build_backend {
    const char* name;
    int       (*build)(struct oven_build_options* options);
};

static struct generate_backend g_genbackends[] = {
    { "configure", configure_main },
    { "cmake",     cmake_main     }
};

static struct build_backend g_buildbackends[] = {
    { "make", make_main }
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

    status = __oven_setup_environment();
    if (status) {
        fprintf(stderr, "oven: failed to initialize: %s\n", strerror(errno));
        return status;
    }
    
    status = platform_mkdir(".oven");
    if (status) {
        if (errno != EEXIST) {
            fprintf(stderr, "oven: failed to create work space: %s\n", strerror(errno));
            return -1;
        }
    }
    
    status = platform_mkdir(".oven/build");
    if (status) {
        if (errno != EEXIST) {
            fprintf(stderr, "oven: failed to create build space: %s\n", strerror(errno));
            return -1;
        }
    }
    
    status = platform_mkdir(".oven/install");
    if (status) {
        if (errno != EEXIST) {
            fprintf(stderr, "oven: failed to create artifact space: %s\n", strerror(errno));
            return -1;
        }
    }
    return 0;
}

static struct generate_backend* __get_generate_backend(const char* name)
{
    for (int i = 0; i < sizeof(g_genbackends) / sizeof(struct generate_backend); i++) {
        if (!strcmp(name, g_genbackends[i].name)) {
            return &g_genbackends[i];
        }
    }
    return NULL;
}

int oven_configure(struct oven_generate_options* options)
{
    struct generate_backend* backend;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_generate_backend(options->system);
    if (!backend) {
        errno = ENOSYS;
        return -1;
    }

    return backend->generate(options);
}

static struct build_backend* __get_build_backend(const char* name)
{
    for (int i = 0; i < sizeof(g_buildbackends) / sizeof(struct build_backend); i++) {
        if (!strcmp(name, g_buildbackends[i].name)) {
            return &g_buildbackends[i];
        }
    }
    return NULL;
}

int oven_build(struct oven_build_options* options)
{
    struct build_backend* backend;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_build_backend(options->system);
    if (!backend) {
        errno = ENOSYS;
        return -1;
    }

    return backend->build(options);
}

int oven_pack(struct oven_pack_options* options)
{
    // TODO vafs usage
    errno = ENOTSUP;
    return -1;
}

int oven_cleanup(void)
{
    // TODO figure out what to clean
    return 0;
}
