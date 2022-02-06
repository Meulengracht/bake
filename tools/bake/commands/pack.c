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
#include <recipe.h>
#include <stdio.h>
#include <string.h>

static void __initialize_generator_options(struct oven_generate_options* options, struct recipe* recipe)
{
    options->system = NULL;
    options->arguments = NULL;
}

static void __initialize_build_options(struct oven_build_options* options, struct recipe* recipe)
{
    options->system = NULL;
    options->arguments = NULL;
}

static void __initialize_pack_options(struct oven_pack_options* options, struct recipe* recipe)
{
    options->compression = NULL;
}

int pack_main(int argc, char** argv, struct recipe* recipe)
{
    struct oven_generate_options genOptions;
    struct oven_build_options    buildOptions;
    struct oven_pack_options     packOptions;
    int                          status;

    status = oven_initialize();
    if (status != 0) {
        fprintf(stderr, "bake: failed to initialize oven: %s\n", strerror(errno));
        return status;
    }

    __initialize_generator_options(&genOptions, recipe);
    status = oven_configure(&genOptions);
    if (status != 0) {
        fprintf(stderr, "bake: failed to configure target: %s\n", strerror(errno));
        return status;
    }

    __initialize_build_options(&buildOptions, recipe);
    status = oven_build(&buildOptions);
    if (status != 0) {
        fprintf(stderr, "bake: failed to build target: %s\n", strerror(errno));
        return status;
    }

    __initialize_pack_options(&packOptions, recipe);
    status = oven_pack(&packOptions);
    if (status != 0) {
        fprintf(stderr, "bake: failed to pack target: %s\n", strerror(errno));
        return status;
    }

    status = oven_cleanup();
    if (status != 0) {
        fprintf(stderr, "bake: failed to cleanup target: %s\n", strerror(errno));
        return status;
    }
    return 0;
}
