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

#include <chef/ingredient.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

#include "private.h"

static struct oven_backend g_backends[] = {
    { "autotools", configure_main,    NULL,             NULL },
    { "cmake",     cmake_main,        NULL,             NULL },
    { "meson",     meson_config_main, meson_build_main, meson_clean_main },
    { "make",      NULL,              make_build_main,  make_clean_main },
    { "ninja",     NULL,              ninja_build_main, ninja_clean_main },
};

static struct oven_context g_oven = { 0 };
struct oven_context* __oven_instance() { return &g_oven; }

int oven_initialize(struct oven_initialize_options* parameters)
{
    VLOG_DEBUG("oven", "oven_initialize()\n");

    if (parameters == NULL) {
        errno = EINVAL;
        return -1;
    }

    // copy relevant paths
    g_oven.paths.project_root = platform_strdup(parameters->paths.project_root);
    g_oven.paths.source_root = platform_strdup(parameters->paths.source_root);
    g_oven.paths.build_root = platform_strdup(parameters->paths.build_root);
    g_oven.paths.install_root = platform_strdup(parameters->paths.install_root);
    g_oven.paths.toolchains_root = platform_strdup(parameters->paths.toolchains_root);
    g_oven.paths.build_ingredients_root = platform_strdup(parameters->paths.build_ingredients_root);
    
    // update oven variables
    g_oven.variables.target_platform = platform_strdup(parameters->target_platform);
    g_oven.variables.target_arch     = platform_strdup(parameters->target_architecture);

    // update oven context
    g_oven.process_environment = parameters->envp;

    // no active recipe
    memset(&g_oven.recipe, 0, sizeof(struct oven_recipe_context));
    return 0;
}

void oven_cleanup(void)
{
    // cleanup paths
    free((void*)g_oven.paths.project_root);
    free((void*)g_oven.paths.source_root);
    free((void*)g_oven.paths.build_root);
    free((void*)g_oven.paths.install_root);
    free((void*)g_oven.paths.toolchains_root);
    free((void*)g_oven.paths.build_ingredients_root);

    // cleanup variables
    free((void*)g_oven.variables.target_platform);
    free((void*)g_oven.variables.target_arch);

    memset(&g_oven, 0, sizeof(struct oven_context));
}

static int __ensure_recipe_dirs(struct oven_recipe_options* options)
{
    if (platform_mkdir(g_oven.recipe.build_root)) {
        VLOG_ERROR("oven", "__ensure_recipe_dirs: failed to create %s\n", g_oven.recipe.build_root);
        return -1;
    }
    return 0;
}

int oven_recipe_start(struct oven_recipe_options* options)
{
    VLOG_DEBUG("oven", "oven_recipe_start(name=%s)\n", options->name);

    if (g_oven.recipe.name) {
        VLOG_ERROR("oven", "oven_recipe_start: recipe already started\n");
        errno = ENOSYS;
        return -1;
    }
    g_oven.recipe.name = platform_strdup(options->name);

    // construct the recipe paths
    g_oven.recipe.source_root = strpathcombine(g_oven.paths.source_root, options->name);
    g_oven.recipe.build_root = strpathcombine(g_oven.paths.build_root, options->name);

    // build the toolchain path
    if (options->toolchain != NULL) {
        g_oven.recipe.toolchain = strpathcombine(g_oven.paths.toolchains_root, options->toolchain);
    }

    // create directories, last as it uses the global access
    if (__ensure_recipe_dirs(options)) {
        VLOG_ERROR("oven", "oven_recipe_start: failed to create directories for recipe\n");
        goto error;
    }
    return 0;

error:
    oven_recipe_end();
    return -1;
}

void oven_recipe_end(void)
{
    VLOG_DEBUG("oven", "oven_recipe_end()\n");
    free((void*)g_oven.recipe.name);
    free((void*)g_oven.recipe.toolchain);
    free((void*)g_oven.recipe.source_root);
    free((void*)g_oven.recipe.build_root);
    memset(&g_oven.recipe, 0, sizeof(struct oven_recipe_context));
}

static const char* __get_variable(const char* name, void* context)
{
    // fixed values
    static const char* hostArch = CHEF_ARCHITECTURE_STR;
    static const char* hostPlatform = CHEF_PLATFORM_STR;

    static const struct {
        const char*  name;
        const char** value;
    } variables[] = {
        { "CHEF_TARGET_PLATFORM", &g_oven.variables.target_platform },
        { "CHEF_TARGET_ARCHITECTURE", &g_oven.variables.target_arch },
        { "CHEF_HOST_PLATFORM", &hostPlatform },
        { "CHEF_HOST_ARCHITECTURE", &hostArch },
        { "TOOLCHAIN_PREFIX", &g_oven.recipe.toolchain },
        { "PROJECT_PATH", &g_oven.paths.project_root },
        { "INSTALL_PREFIX", &g_oven.paths.install_root },
        { "BUILD_INGREDIENTS_PREFIX", &g_oven.paths.build_ingredients_root },
        { NULL, NULL }
    };

    for (int i = 0; variables[i].name != NULL; i++) {
        if (strcmp(variables[i].name, name) == 0) {
            printf("RESOLVED: %s => %s\n", variables[i].name, *(variables[i].value));
            return *(variables[i].value);
        }
    }
    return NULL;
}

char* oven_preprocess_text(const char* original)
{
    return chef_preprocess_text(original, __get_variable, NULL);
}

static struct oven_backend* __get_backend(const char* name)
{
    for (int i = 0; i < sizeof(g_backends) / sizeof(struct oven_backend); i++) {
        if (!strcmp(name, g_backends[i].name)) {
            return &g_backends[i];
        }
    }
    return NULL;
}

static void __cleanup_environment(struct list* keypairs)
{
    struct list_item* item;

    if (keypairs == NULL) {
        return;
    }

    for (item = keypairs->head; item != NULL;) {
        struct list_item*         next    = item->next;
        struct chef_keypair_item* keypair = (struct chef_keypair_item*)item;

        free((void*)keypair->key);
        free((void*)keypair->value);
        free(keypair);

        item = next;
    }
    free(keypairs);
}

static void __cleanup_backend_data(struct oven_backend_data* data)
{
    __cleanup_environment(data->environment);
    free((void*)data->arguments);
}

static struct chef_keypair_item* __preprocess_keypair(struct chef_keypair_item* original)
{
    struct chef_keypair_item* keypair;

    keypair = (struct chef_keypair_item*)malloc(sizeof(struct chef_keypair_item));
    if (!keypair) {
        return NULL;
    }

    keypair->key   = platform_strdup(original->key);
    keypair->value = chef_preprocess_text(original->value, __get_variable, NULL);
    return keypair;
}

static struct list* __preprocess_keypair_list(struct list* original)
{
    struct list*      processed = malloc(sizeof(struct list));
    struct list_item* item;

    if (!processed) {
        VLOG_ERROR("oven", "failed to allocate memory environment preprocessor\n");
        return original;
    }

    list_init(processed);
    list_foreach(original, item) {
        struct chef_keypair_item* keypair          = (struct chef_keypair_item*)item;
        struct chef_keypair_item* processedKeypair = __preprocess_keypair(keypair);
        if (!processedKeypair) {
            VLOG_ERROR("oven", "failed to allocate memory environment preprocessor\n");
            break;
        }

        list_add(processed, &processedKeypair->list_header);
    }
    return processed;
}

static const char* __append_ingredients_system_path(const char* original, const char* systemRoot)
{
    // '-isystem-after ' (15)
    // '/include' (8)
    // space between (1)
    // zero terminator (1)
    size_t length = strlen(original) + strlen(systemRoot) + 26;
    char*  buffer = calloc(length, 1);
    if (buffer == NULL) {
        return NULL;
    }

    snprintf(buffer, length - 1, "%s -isystem-after %s/include", original, systemRoot);
    return buffer;
}

static struct chef_keypair_item* __compose_keypair(const char* key, const char* value)
{
    struct chef_keypair_item* item = calloc(sizeof(struct chef_keypair_item), 1);
    if (item == NULL) {
        return NULL;
    }
    item->key = platform_strdup(key);
    if (item->key == NULL) {
        free(item);
    }
    item->value = platform_strdup(value);
    if (item->value == NULL) {
        free((void*)item->key);
        free(item);
    }
    return item;
}

static struct chef_keypair_item* __build_ingredients_system_path_keypair(const char* key, const char* systemRoot)
{
    char tmp[512] = { 0 };
    snprintf(&tmp[0], sizeof(tmp), "-isystem-after %s/include", systemRoot);
    return __compose_keypair(key, &tmp[0]);
}

static int __append_or_update_environ_flags(struct list* environment, const char* systemRoot)
{
    // Look and update/add the following language flags to account for
    // ingredient include paths
    struct list_item* item;
    struct {
        const char* ident;
        int         fixed;
    } idents[] = {
        { "CFLAGS", 0 },
        { "CXXFLAGS", 0 },
        { NULL, 0 }
    };

    // Update any environmental variable already provided by recipe
    list_foreach(environment, item) {
        struct chef_keypair_item* keypair = (struct chef_keypair_item*)item;
        for (int i = 0; idents[i].ident != NULL; i++) {
            if (!strcmp(keypair->key, idents[i].ident)) {
                const char* tmp = keypair->value;
                keypair->value = __append_ingredients_system_path(tmp, systemRoot);
                if (keypair->value == NULL) {
                    keypair->value = tmp;
                    return -1;
                }
                free((void*)tmp);
                idents[i].fixed = 1;
            }
        }
    }

    // Add any that was not provided
    for (int i = 0; idents[i].ident != NULL; i++) {
        if (!idents[i].fixed) {
            item = (struct list_item*)__build_ingredients_system_path_keypair(idents[i].ident, systemRoot);
            if (item == NULL) {
                return -1;
            }
            list_add(environment, item);
        }
    }
    return 0;
}

static int __initialize_backend_data(struct oven_backend_data* data, const char* profile, struct list* arguments, struct list* environment)
{
    // reset the datastructure
    memset(data, 0, sizeof(struct oven_backend_data));

    // setup expected paths
    data->paths.root              = g_oven.paths.project_root;
    data->paths.install           = g_oven.paths.install_root;
    data->paths.source            = g_oven.recipe.source_root;
    data->paths.build             = g_oven.recipe.build_root;
    data->paths.build_ingredients = g_oven.paths.build_ingredients_root;
    data->paths.project           = g_oven.recipe.source_root;

    data->project_name        = g_oven.recipe.name;
    data->profile_name        = profile != NULL ? profile : "Release";
    data->process_environment = g_oven.process_environment;

    data->platform.host_platform = CHEF_PLATFORM_STR;
    data->platform.host_architecture = CHEF_ARCHITECTURE_STR;
    data->platform.target_platform = g_oven.variables.target_platform;
    data->platform.target_architecture = g_oven.variables.target_arch;
    
    data->environment = __preprocess_keypair_list(environment);
    if (!data->environment) {
        __cleanup_backend_data(data);
        return -1;
    }

    //if (__append_or_update_environ_flags(data->environment)) {
    //    __cleanup_backend_data(data);
    //    return -1;
    //}

    data->arguments = chef_process_argument_list(arguments, __get_variable, NULL);
    if (!data->arguments) {
        __cleanup_backend_data(data);
        return -1;
    }
    return 0;
}

int oven_configure(struct oven_generate_options* options)
{
    struct oven_backend*     backend;
    struct oven_backend_data data;
    int                      status;
    VLOG_DEBUG("oven", "oven_configure(name=%s, system=%s)\n", options->name, options->system);

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_backend(options->system);
    if (backend == NULL || backend->generate == NULL) {
        errno = ENOSYS;
        return -1;
    }

    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }
    
    status = backend->generate(&data, options->system_options);
    __cleanup_backend_data(&data);
    return status;
}

int oven_build(struct oven_build_options* options)
{
    struct oven_backend*     backend;
    struct oven_backend_data data;
    int                      status;
    VLOG_DEBUG("oven", "oven_build(name=%s, system=%s)\n", options->name, options->system);

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_backend(options->system);
    if (backend == NULL || backend->build == NULL) {
        errno = ENOSYS;
        return -1;
    }

    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }
    
    status = backend->build(&data, options->system_options);
    __cleanup_backend_data(&data);
    return status;
}

int oven_clean(struct oven_clean_options* options)
{
    struct oven_backend*     backend;
    struct oven_backend_data data;
    int                      status;
    VLOG_DEBUG("oven", "oven_clean(name=%s, system=%s)\n", options->name, options->system);

    if (!options) {
        errno = EINVAL;
        return -1;
    }

    backend = __get_backend(options->system);
    if (backend == NULL || backend->clean == NULL) {
        errno = ENOSYS;
        return -1;
    }

    VLOG_TRACE("oven", "running step %s\n", options->name);
    status = __initialize_backend_data(&data, options->profile, options->arguments, options->environment);
    if (status) {
        return status;
    }
    
    status = backend->clean(&data, options->system_options);
    __cleanup_backend_data(&data);
    return status;
}
