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
#include <vafs/vafs.h>

struct oven_recipe_context {
    const char* name;
    const char* relative_path;

    const char* build_root;
    const char* install_root;
};

struct oven_context {
    const char*                environment;
    const char*                build_root;
    const char*                install_root;
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

// expose the following variables to the build process
// BAKE_BUILD_DIR
// BAKE_ARTIFACT_DIR
static int __oven_setup_environment(void)
{
    char** environment;
    int    status;

    environment = (char**)malloc(sizeof(char*) * 3);
    if (!environment) {
        return -1;
    }

    environment[0] = (char*)malloc(sizeof(char) * (strlen("BAKE_BUILD_DIR=") + strlen(g_ovenContext.build_root) + 1));
    if (!environment[0]) {
        free(environment);
        return -1;
    }

    environment[1] = (char*)malloc(sizeof(char) * (strlen("BAKE_ARTIFACT_DIR=") + strlen(g_ovenContext.install_root) + 1));
    if (!environment[1]) {
        free(environment[0]);
        free(environment);
        return -1;
    }

    sprintf(environment[0], "BAKE_BUILD_DIR=%s", g_ovenContext.build_root);
    sprintf(environment[1], "BAKE_ARTIFACT_DIR=%s", g_ovenContext.install_root);
    environment[2] = NULL;
    return 0;
}

// oven is the work-area for the build and pack
// .oven/build
// .oven/install
int oven_initialize(void)
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
    g_ovenContext.build_root = buildRoot;
    g_ovenContext.install_root = installRoot;

    // no active recipe
    g_ovenContext.recipe.name          = NULL;
    g_ovenContext.recipe.relative_path = NULL;
    g_ovenContext.recipe.build_root    = NULL;
    g_ovenContext.recipe.install_root  = NULL;

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

int oven_recipe_start(struct oven_recipe_options* options)
{
    char* buildRoot;
    char* installRoot;

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

    sprintf(buildRoot, "%s/%s", g_ovenContext.build_root, g_ovenContext.recipe.relative_path);
    sprintf(installRoot, "%s/%s", g_ovenContext.install_root, g_ovenContext.recipe.relative_path);

    // store members as const
    g_ovenContext.recipe.build_root    = buildRoot;
    g_ovenContext.recipe.install_root  = installRoot;
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

static char** combine_environments(const char** lh, const char** rh)
{
    int    numEnvironments = 0;
    int    i               = 0;
    char** environment;
    
    while (lh && lh[i++]) {
        numEnvironments++;
    }

    i = 0;
    while (rh && rh[i++]) {
        numEnvironments++;
    }

    environment = (char**)malloc(sizeof(char*) * (numEnvironments + 1));
    if (!environment) {
        return NULL;
    }

    // last entry should always be null
    environment[numEnvironments] = NULL;

    // now we copy the environments from both sides
    numEnvironments = 0;
    i               = 0;
    while (lh && lh[i]) {
        environment[numEnvironments++] = strdup(lh[i++]);
    }

    i = 0;
    while (rh && rh[i]) {
        environment[numEnvironments++] = strdup(rh[i++]);
    }
    return environment;
}

static void __cleanup_environment(char** environment)
{
    int i = 0;
    while (environment[i]) {
        free(environment[i]);
        i++;
    }
    free(environment);
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
    status = __get_root_directory(&path);
    if (status) {
        return status;
    }
    data.root_directory = path;
    
    status = __get_project_directory(data.root_directory, g_ovenContext.recipe.relative_path, &path);
    if (status) {
        free((void*)data.root_directory);
        return status;
    }
    data.project_directory = path;

    data.arguments   = options->arguments;
    data.environment = (const char* const*)combine_environments(options->environment, NULL);
    if (!data.environment) {
        free((void*)data.root_directory);
        free((void*)data.project_directory);
        return -1;
    }

    status = platform_chdir(g_ovenContext.recipe.build_root);
    if (status) {
        goto cleanup;
    }

    status = backend->generate(&data);

    // restore working directory
    (void)platform_chdir(data.root_directory);
    
    // cleanup
cleanup:
    __cleanup_environment((void*)data.environment);
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
    status = __get_root_directory(&path);
    if (status) {
        return status;
    }
    data.root_directory = path;
    
    status = __get_project_directory(data.root_directory, g_ovenContext.recipe.relative_path, &path);
    if (status) {
        free((void*)data.root_directory);
        return status;
    }
    data.project_directory = path;

    data.arguments   = options->arguments;
    data.environment = (const char* const*)combine_environments(options->environment, NULL);
    if (!data.environment) {
        free((void*)data.root_directory);
        free((void*)data.project_directory);
        return -1;
    }

    status = platform_chdir(g_ovenContext.recipe.build_root);
    if (status) {
        goto cleanup;
    }

    status = backend->build(&data);

    // restore working directory
    (void)platform_chdir(data.root_directory);
    
    // cleanup
cleanup:
    __cleanup_environment((void*)data.environment);
    free((void*)data.project_directory);
    free((void*)data.root_directory);
    return status;
}

int oven_pack(struct oven_pack_options* options)
{
    struct VaFs* vafs;
    int          status;

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    // TODO arch
    status = vafs_create("app.image", VaFsArchitecture_X64, &vafs);
    if (status) {
        return status;
    }



    status = vafs_close(vafs);


    return status;
}

int oven_cleanup(void)
{
    // TODO figure out what to clean
    return 0;
}
