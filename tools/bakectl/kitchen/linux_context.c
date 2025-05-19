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

#include <chef/bake.h>
#include <chef/fridge.h>
#include <chef/ingredient.h>
#include <chef/pkgmgr.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static const char* __get_platform(void) {
    const char* platform = getenv("CHEF_TARGET_PLATFORM");
    if (platform == NULL) {
        platform = CHEF_PLATFORM_STR;
    }
    return platform;
}

static const char* __get_architecture(void) {
    const char* platform = getenv("CHEF_TARGET_ARCH");
    if (platform == NULL) {
        platform = CHEF_ARCHITECTURE_STR;
    }
    return platform;
}

static const char* __get_pkgmgr(void) {
    const char* pkgsys = getenv("CHEF_PACKAGE_MANAGER");
    // default to pkg-config
    if (pkgsys == NULL) {
        pkgsys = "pkg-config";
    }
    return pkgsys;
}

static struct pkgmngr* __setup_pkg_environment(void)
{
    static struct {
        const char* environment;
        struct pkgmngr* (*create)(struct pkgmngr_options*);
    } systems[] = {
        { "pkg-config", pkgmngr_pkgconfig_new },
        { NULL, NULL }
    };
    const char* env = __get_pkgmgr();

    for (int i = 0; systems[i].environment != NULL; i++) {
        if (strcmp(env, systems[i].environment) == 0) {
            VLOG_TRACE("bakectl", "package manager: %s\n", env);
            return systems[i].create(&(struct pkgmngr_options) {
                .root = "", 
                .target_platform = __get_platform(),
                .target_architecture = __get_architecture()
            });
        }
    }
    return NULL;
}

static char* __fmt_env_option(const char* name, const char* value)
{
    char  tmp[512];
    char* result;
    snprintf(&tmp[0], sizeof(tmp), "%s=%s", name, value);
    result = strdup(&tmp[0]);
    if (result == NULL) {
        VLOG_FATAL("bakectl", "failed to allocate memory for environment option\n");
    }
    return result;
}

static char** __initialize_env(struct pkgmngr* pkg, const char* const* parentEnv)
{
    char** env;
    int    count = 0;

    // count parent number
    while (parentEnv[count++]);

    // add for chef placeholders
    // 5 + 1 (ssl)
    count += 5 + 1;
    if (pkg != NULL) {
        count += 5;
    }

    // encount for terminating entry
    env = calloc(count + 1, sizeof(char*));
    if (env == NULL) {
        VLOG_FATAL("bakectl", "failed to allocate memory for environment\n");
        return NULL;
    }

    // copy parent entries
    count = 0;
    while (parentEnv[count]) {
        env[count] = parentEnv[count];
        count++;
    }

    // placeholders, to be filled when iterating
    // build ingredients
    env[count++] = __fmt_env_option("CHEF_BUILD_PATH", "");
    env[count++] = __fmt_env_option("CHEF_BUILD_INCLUDE", "");
    env[count++] = __fmt_env_option("CHEF_BUILD_LIBS", "");
    env[count++] = __fmt_env_option("CHEF_BUILD_CCFLAGS", "");
    env[count++] = __fmt_env_option("CHEF_BUILD_LDFLAGS", "");

    // Not guaranteed that ca-certificates is in the rootfs when building,
    // so let us add this to avoid git checking for now.
    env[count++] = __fmt_env_option("GIT_SSL_NO_VERIFY", "1");

    // env[13-17] are pkgmgr overrides
    if (pkg != NULL) {
        pkg->add_overrides(pkg, env);
    }
    return env;
}

static int __update_build_envs(struct __bakelib_context* context)
{
    struct list_item* i;
    int               status;

    list_foreach(&context->recipe->environment.build.ingredients, i) {
        struct recipe_ingredient* recIngredient = (struct recipe_ingredient*)i;
        struct ingredient*        ingredient;
        const char*               path = NULL;
        
        status = fridge_ingredient_path(&(struct fridge_ingredient) {
            .name = recIngredient->name,
            .channel = recIngredient->channel,
            .version = recIngredient->version,
            .arch = context->build_architecture,
            .platform = context->build_platform
        }, &path);

        status = ingredient_open(path, &ingredient);
        if (status) {
            VLOG_ERROR("bakectl", "__update_build_envs: failed to open %s\n", path);
            return -1;
        }

        if (environment_append_keyv(context->build_environment, "CHEF_BUILD_PATH", ingredient->options->bin_dirs, ";") |
            environment_append_keyv(context->build_environment, "CHEF_BUILD_INCLUDE", ingredient->options->inc_dirs, ";") |
            environment_append_keyv(context->build_environment, "CHEF_BUILD_LIBS", ingredient->options->lib_dirs, ";") |
            environment_append_keyv(context->build_environment, "CHEF_BUILD_CCFLAGS", ingredient->options->compiler_flags, ";") |
            environment_append_keyv(context->build_environment, "CHEF_BUILD_LDFLAGS", ingredient->options->linker_flags, ";")) {
            VLOG_ERROR("bakectl", "__update_build_envs: failed to build environment values\n");
            return -1;
        }

        ingredient_close(ingredient);
        if (status) {
            VLOG_ERROR("bakectl", "__update_build_envs: failed to make %s available\n", recIngredient->name);
            return -1;
        }
    }
    return 0;
}

// ** /chef/project
// * This is mapped in by the host, and contains a RO path of the
// * source code for the project
// ** /chef/fridge & /chef/store
// * This is mapped by the host, and contains a RO path of the 
// * hosts fridge storage. We use this to load packs and toolchains
// * needed
struct __bakelib_context* __bakelib_context_new(
    struct recipe*     recipe,
    const char*        recipe_path,
    const char* const* envp)
{
    struct __bakelib_context* context;
    struct pkgmngr*           pkg;

    context = calloc(1, sizeof(struct __bakelib_context));
    if (context == NULL) {
        return NULL;
    }

    if (recipe_cache_create(recipe, "/chef", &context->cache)) {
        VLOG_ERROR("bakectl", "failed to create build cache\n");
        free(context);
        return NULL;
    }

    context->recipe = recipe;
    context->recipe_path = recipe_path;
    context->pkg_manager = __setup_pkg_environment();

    context->build_platform = __get_platform();
    context->build_architecture = __get_architecture();
    context->build_environment = __initialize_env(context->pkg_manager, envp);

    context->build_directory = strpathjoin(
        "chef", "build", 
        context->build_platform, 
        context->build_architecture,
        NULL
    );
    
    context->build_ingredients_directory = strpathjoin(
        "chef", "build", 
        context->build_platform, 
        context->build_architecture,
        NULL
    );

    context->build_toolchains_directory = strpathjoin(
        "chef", "toolchains", NULL
    );

    context->install_directory = strpathjoin(
        "chef", "install", 
        context->build_platform, 
        context->build_architecture,
        NULL
    );
}

void __bakelib_context_delete(struct __bakelib_context* context)
{

}
