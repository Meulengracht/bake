/**
 * Copyright 2024, Philip Meulengracht
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
#include <chef/bake.h>
#include <chef/platform.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bakectl source [options]\n");
    printf("\n");
    printf("Options:\n");
    printf("  -s,  --step\n");
    printf("      If provided, sources only the provided part/step configuration\n");
    printf("  -h,  --help\n");
    printf("      Shows this help message\n");
}

static void __cleanup_systems(int sig)
{
    (void)sig;
    printf("termination requested, cleaning up\n"); // not safe
    _Exit(0);
}

struct __source_options {
    const char* source_root;
    const char* project_root;
    const char* part;
    char**      envp;
};

static int __prepare_path(const char* root, const char* relativePath, struct __source_options* options)
{
    char* project;
    int   status;
    VLOG_DEBUG("bakectl", "__prepare_path()\n");

    project = strpathjoin(options->project_root, relativePath, NULL);
    if (project == NULL) {
        VLOG_ERROR("bakectl", "__prepare_path: failed to allocate memory for project path\n");
        return -1;
    }

    // Create a link from the source folder to the source folder in the
    // project.
    status = platform_symlink(root, project, 1);
    if (status) {
        VLOG_ERROR("bakectl", "__prepare_path: failed to link %s to %s\n", root, project);
    }
    
    free(project);
    return status;
}

static int __prepare_url(const char* root, const char* url)
{
    VLOG_DEBUG("bakectl", "__prepare_url(url=%s)\n", url);
    errno = ENOSYS;
    return -1;
}

static int __prepare_git(const char* root, struct recipe_part_source_git* git, struct __source_options* options)
{
    int  status;
    char buffer[512];
    VLOG_DEBUG("bakectl", "__prepare_git()\n");

    status = platform_mkdir(root);
    if (status) {
        VLOG_ERROR("bakectl", "__prepare_git: failed to create directory: %s\n", strerror(errno));
        return -1;
    }

    snprintf(&buffer[0], sizeof(buffer),
        "clone -q %s .",
        git->url
    );

    // start out by checking out main repo
    VLOG_TRACE("bakectl", "Cloning repository for %s\n", options->part);
    status = platform_spawn(
        "git", &buffer[0],
        (const char* const*)options->envp, 
        &(struct platform_spawn_options) {
            .cwd = root
        }
    );
    if (status) {
        VLOG_ERROR("bakectl", "__prepare_git: failed to checkout %s\n", git->url);
        return -1;
    }

    // switch branch / commit
    if (git->branch != NULL || git->commit != NULL) {
        if (git->branch != NULL) {
            snprintf(&buffer[0], sizeof(buffer),
                "checkout %s",
                git->branch
            );
        } else {
            snprintf(&buffer[0], sizeof(buffer),
                "checkout %s",
                git->commit
            );
        }

        status = platform_spawn(
            "git", &buffer[0],
            (const char* const*)options->envp, 
            &(struct platform_spawn_options) {
                .cwd = root
            }
        );
        if (status) {
            VLOG_ERROR("bakectl", "__prepare_git: failed to %s\n", &buffer[0]);
            return -1;
        }
    }

    // checkout submodules if any
    VLOG_TRACE("bakectl", "Cloning submodules for %s\n", options->part);
    status = platform_spawn(
        "git", "submodule update -q --init --recursive",
        (const char* const*)options->envp, 
        &(struct platform_spawn_options) {
            .cwd = root
        }
    );
    if (status) {
        VLOG_ERROR("bakectl", "__prepare_git: failed to checkout submodules\n");
    }

    return status;
}

static int __cleanup_existing(const char* path)
{
    int status;

    // It may be a symbolic link and not actually a directory, so we
    // try to unlink it first
    status = platform_unlink(path);
    if (status && errno != ENOENT) {
        // we should probably verify it failed for the correct reasons
        // here, but should it fail to unlink, then assume it's because
        // it's actually a directory with files in it, and not a symlink
        status = platform_rmdir(path);
        if (status) {
            if (errno != ENOENT) {
                VLOG_ERROR("bakectl", "__recreate_dir: failed to remove directory: %s\n", strerror(errno));
                return -1;
            }
        }
    }
    return 0;
}

static int __execute_source_script(const char* root, const char* part, struct recipe_part_source* source)
{
    VLOG_DEBUG("bakectl", "__execute_source_script()\n");

    // no script no problem
    if (source->script == NULL) {
        return 0;
    }
    
    VLOG_TRACE("bakectl", "Executing source script for %s\n", part);
    return oven_script(
        source->script,
        &(struct oven_script_options) {
            .root_dir = OVEN_SCRIPT_ROOT_DIR_SOURCE
        }
    );
}

static int __prepare_source(const char* part, struct recipe_part_source* source, struct __source_options* options)
{
    char* sourceRoot;
    int   status;
    VLOG_DEBUG("bakectl", "__prepare_source(part=%s)\n", part);

    sourceRoot = strpathjoin(options->source_root, part, NULL);
    if (sourceRoot == NULL) {
        VLOG_ERROR("bakectl", "__prepare_source: failed to allocate memory for source path\n");
        return -1;
    }

    // ensure that the source root exists
    status = platform_mkdir(options->source_root);
    if (status) {
        VLOG_ERROR("bakectl", "__prepare_git: failed to create directory: %s\n", strerror(errno));
        free(sourceRoot);
        return -1;
    }

    // ensure a clean version exists of the part source
    status = __cleanup_existing(sourceRoot);
    if (status) {
        VLOG_ERROR("bakectl", "__prepare_source: failed to create %s\n", sourceRoot);
        free(sourceRoot);
        return -1;
    }

    switch (source->type) {
        case RECIPE_PART_SOURCE_TYPE_PATH: {
            status = __prepare_path(sourceRoot, source->path.path, options);
        } break;
        case RECIPE_PART_SOURCE_TYPE_GIT: {
            status = __prepare_git(sourceRoot, &source->git, options);
        } break;
        case RECIPE_PART_SOURCE_TYPE_URL: {
            status =__prepare_url(sourceRoot, source->url.url);
        } break;
        default:
            errno = ENOSYS;
            status = -1;
            break;
    }

    if (status == 0) {
        status = __execute_source_script(sourceRoot, options->part, source);
        if (status) {
            VLOG_ERROR("bakectl", "__prepare_source: failed to execute source script\n");
        }
    } else {
        VLOG_ERROR("bakectl", "__prepare_source: failed to prepare source for %s\n", part);
    }
    
    free(sourceRoot);
    return status;
}

static int __source_part(struct recipe* recipe, struct __source_options* options)
{
    struct list_item* item;
    int               status;
    VLOG_DEBUG("bakectl", "__source_part(part=%s)\n", options->part);

    list_foreach(&recipe->parts, item) {
        struct recipe_part* part = (struct recipe_part*)item;

        // find the correct recipe part
        if (options->part != NULL && strcmp(part->name, options->part)) {
            continue;
        }

        status = oven_recipe_start(&(struct oven_recipe_options) {
            .name = part->name
        });
        if (status) {
            break;
        }
        
        status = __prepare_source(part->name, &part->source, options);
        oven_recipe_end();
        if (status) {
            VLOG_ERROR("bakectl", "__clean_part: failed to build recipe %s\n", part->name);
            break;
        }

        // done if a specific part was provided
        if (options->part != NULL) {
            break;
        }
    }

    return status;
}

int source_main(int argc, char** argv, struct __bakelib_context* context, struct bakectl_command_options* options)
{
    struct oven_initialize_options ovenOpts = { 0 };
    int                            status;
    int                            purge = 0;

    // catch CTRL-C
    signal(SIGINT, __cleanup_systems);

    // handle individual help command
    if (argc > 1) {
        for (int i = 1; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            }
        }
    }

    status = __initialize_oven_options(&ovenOpts, context);
    if (status) {
        fprintf(stderr, "bakectl: failed to allocate memory for options\n");
        goto cleanup;
    }

    status = oven_initialize(&ovenOpts);
    if (status) {
        fprintf(stderr, "bakectl: failed to initialize oven: %s\n", strerror(errno));
        goto cleanup;
    }

    status = __source_part(context->recipe, &(struct __source_options) {
        .project_root = ovenOpts.paths.project_root,
        .source_root = ovenOpts.paths.source_root,
        .part = options->part,
        .envp = context->build_environment
    });
    if (status) {
        fprintf(stderr, "bakectl: failed to source part '%s': %s\n", 
            options->part, strerror(errno));
    }
    
    oven_cleanup();

cleanup:
    __destroy_oven_options(&ovenOpts);
    return status;
}
