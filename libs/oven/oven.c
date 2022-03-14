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

#include <backend.h>
#include <errno.h>
#include <liboven.h>
#include <libplatform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct oven_recipe_context {
    const char* name;
    const char* relative_path;

    const char* build_root;
    const char* install_root;
};

struct oven_context {
    const char**               process_environment;
    const char*                build_root;
    const char*                install_root;
    const char*                fridge_prep_directory;
    struct oven_recipe_context recipe;
};

struct generate_backend {
    const char* name;
    int       (*generate)(struct oven_backend_data* data);
};

struct build_backend {
    const char* name;
    int       (*build)(struct oven_backend_data* data);
};

static struct generate_backend g_genbackends[] = {
    { "configure", configure_main },
    { "cmake",     cmake_main     }
};

static struct build_backend g_buildbackends[] = {
    { "make", make_main }
};

static struct oven_context g_ovenContext = { 0 };

static int __get_root_directory(char** bufferOut)
{
    char*  cwd;
    int    status;

    cwd = malloc(1024);
    if (cwd == NULL) {
        return -1;
    }

    status = platform_getcwd(cwd, 1024);
    if (status) {
        free(cwd);
        return -1;
    }
    *bufferOut = cwd;
    return 0;
}

// oven is the work-area for the build and pack
// .oven/build
// .oven/install
int oven_initialize(char** envp, const char* fridgePrepDirectory)
{
    int   status;
    char* cwd;
    char* buildRoot;
    char* installRoot;

    // get the current working directory
    status = __get_root_directory(&cwd);
    if (status) {
        return -1;
    }

    // make sure to add the last path seperator if not already
    // present in cwd
    if (cwd[strlen(cwd) - 1] != '/') {
        strcat(cwd, "/");
    }

    // initialize oven paths
    buildRoot = malloc(sizeof(char) * (strlen(cwd) + strlen(".oven/build") + 1));
    if (!buildRoot) {
        free(cwd);
        return -1;
    }

    installRoot = malloc(sizeof(char) * (strlen(cwd) + strlen(".oven/install") + 1));
    if (!installRoot) {
        free(buildRoot);
        free(cwd);
        return -1;
    }
    
    sprintf(buildRoot, "%s%s", cwd, ".oven/build");
    sprintf(installRoot, "%s%s", cwd, ".oven/install");
    free(cwd);

    // update oven context
    g_ovenContext.process_environment   = (const char**)envp;
    g_ovenContext.build_root            = buildRoot;
    g_ovenContext.install_root          = installRoot;
    g_ovenContext.fridge_prep_directory = fridgePrepDirectory;

    // no active recipe
    g_ovenContext.recipe.name          = NULL;
    g_ovenContext.recipe.relative_path = NULL;
    g_ovenContext.recipe.build_root    = NULL;
    g_ovenContext.recipe.install_root  = NULL;

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

int oven_recipe_start(struct oven_recipe_options* options)
{
    char*  buildRoot;
    char*  installRoot;
    size_t relativePathLength;

    if (g_ovenContext.recipe.name) {
        fprintf(stderr, "oven: recipe already started\n");
        return -1;
    }

    g_ovenContext.recipe.name          = strdup(options->name);
    g_ovenContext.recipe.relative_path = strdup(options->relative_path);
    
    // generate build and install directories
    buildRoot = malloc(sizeof(char) * (strlen(g_ovenContext.build_root) + strlen(g_ovenContext.recipe.relative_path) + 2));
    if (!buildRoot) {
        return -1;
    }

    installRoot = malloc(sizeof(char) * (strlen(g_ovenContext.install_root) + strlen(g_ovenContext.recipe.relative_path) + 2));
    if (!installRoot) {
        free(buildRoot);
        return -1;
    }

    relativePathLength = strlen(g_ovenContext.recipe.relative_path);
    if (relativePathLength > 0) {
        if (g_ovenContext.recipe.relative_path[0] != '/') {
            sprintf(buildRoot, "%s/%s", g_ovenContext.build_root, g_ovenContext.recipe.relative_path);
            sprintf(installRoot, "%s/%s", g_ovenContext.install_root, g_ovenContext.recipe.relative_path);
        } else {
            sprintf(buildRoot, "%s%s", g_ovenContext.build_root, g_ovenContext.recipe.relative_path);
            sprintf(installRoot, "%s%s", g_ovenContext.install_root, g_ovenContext.recipe.relative_path);
        }
    } else {
        sprintf(buildRoot, "%s", g_ovenContext.build_root);
        sprintf(installRoot, "%s", g_ovenContext.install_root);
    }

    // store members as const
    g_ovenContext.recipe.build_root   = buildRoot;
    g_ovenContext.recipe.install_root = installRoot;
    return 0;
}

void oven_recipe_end(void)
{
    if (g_ovenContext.recipe.name) {
        free((void*)g_ovenContext.recipe.name);
        g_ovenContext.recipe.name = NULL;
    }

    if (g_ovenContext.recipe.relative_path) {
        free((void*)g_ovenContext.recipe.relative_path);
        g_ovenContext.recipe.relative_path = NULL;
    }

    if (g_ovenContext.recipe.build_root) {
        free((void*)g_ovenContext.recipe.build_root);
        g_ovenContext.recipe.build_root = NULL;
    }

    if (g_ovenContext.recipe.install_root) {
        free((void*)g_ovenContext.recipe.install_root);
        g_ovenContext.recipe.install_root = NULL;
    }
}

static const char* __build_argument_string(struct list* argumentList)
{
    size_t            argumentLength = 0;
    char*             argumentString;
    char*             argumentItr;
    struct list_item* item;

    // build argument length first
    list_foreach(argumentList, item) {
        struct oven_value_item* value = (struct oven_value_item*)item;
        size_t                  valueLength = strlen(value->value);

        // add one for the space
        if (valueLength > 0) {
            argumentLength += valueLength + 1;
        }
    }

    // allocate memory for the string
    argumentString = (char*)malloc(argumentLength + 1);
    if (argumentString == NULL) {
        return NULL;
    }
    memset(argumentString, 0, argumentLength + 1);

    // copy arguments into buffer
    argumentItr = argumentString;
    list_foreach(argumentList, item) {
        struct oven_value_item* value = (struct oven_value_item*)item;
        size_t                  valueLength = strlen(value->value);

        if (valueLength > 0) {
            strcpy(argumentItr, value->value);
            argumentItr += strlen(value->value);
            if (item->next) {
                *argumentItr = ' ';
                argumentItr++;
            }
        }
    }
    return argumentString;
}

static int __get_project_directory(const char* cwd, const char* projectPath, char** bufferOut)
{
    char* result;

    if (projectPath) {
        // append the relative path to the root directory
        result = malloc(strlen(cwd) + strlen(projectPath) + 2);
        if (!result) {
            return -1;
        }
        sprintf(result, "%s/%s", cwd, projectPath);        
    }
    else {
        result = strdup(cwd);
        if (!result) {
            return -1;
        }
    }
    
    *bufferOut = result;
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

static int __initialize_backend_data(struct oven_backend_data* data, const char* profile, struct list* arguments, struct list* environment)
{
    int                      status;
    char*                    path;

    status = __get_root_directory(&path);
    if (status) {
        return status;
    }
    data->root_directory = path;
    
    status = __get_project_directory(data->root_directory, g_ovenContext.recipe.relative_path, &path);
    if (status) {
        free((void*)data->root_directory);
        return status;
    }
    data->project_directory = path;

    data->project_name        = g_ovenContext.recipe.name;
    data->profile_name        = profile != NULL ? profile : "release";
    data->install_directory   = g_ovenContext.recipe.install_root;
    data->build_directory     = g_ovenContext.recipe.build_root;
    data->process_environment = g_ovenContext.process_environment;
    data->fridge_directory    = g_ovenContext.fridge_prep_directory;
    data->environment         = environment;
    data->arguments           = __build_argument_string(arguments);
    if (!data->arguments) {
        free((void*)data->root_directory);
        free((void*)data->project_directory);
        return -1;
    }
    return 0;
}

int oven_configure(struct oven_generate_options* options)
{
    struct generate_backend* backend;
    struct oven_backend_data data;
    int                      status;
    char*                    path;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_generate_backend(options->system);
    if (!backend) {
        errno = ENOSYS;
        return -1;
    }

    // build the backend data
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        goto cleanup;
    }

    status = backend->generate(&data);

    // cleanup
cleanup:
    free((void*)data.project_directory);
    free((void*)data.root_directory);
    return status;
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
    struct build_backend*    backend;
    struct oven_backend_data data;
    int                      status;
    char*                    path;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_build_backend(options->system);
    if (!backend) {
        errno = ENOSYS;
        return -1;
    }

    // build the backend data
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        goto cleanup;
    }

    status = backend->build(&data);

    // cleanup
cleanup:
    free((void*)data.project_directory);
    free((void*)data.root_directory);
    return status;
}

void oven_cleanup(void)
{
    if (g_ovenContext.build_root) {
        free((void*)g_ovenContext.build_root);
        g_ovenContext.build_root = NULL;
    }
    
    if (g_ovenContext.install_root) {
        free((void*)g_ovenContext.install_root);
        g_ovenContext.install_root = NULL;
    }
}
