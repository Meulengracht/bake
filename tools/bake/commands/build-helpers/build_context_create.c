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

#include <chef/dirs.h>
#include <chef/list.h>
#include <chef/platform.h>
#include <stdlib.h>
#include <vlog.h>

#include "build.h"

#ifdef CHEF_ON_LINUX
#include <unistd.h>
static char* __get_username(void) {
    char tmp[512] = { 0 };
    getlogin_r(&tmp[0], sizeof(tmp)-1);
    return platform_strdup(&tmp[0]);
}

static int __construct_paths(struct __bake_build_context* bctx) {
    const char* rootfs = chef_dirs_kitchen(build_cache_uuid(bctx->build_cache));

    bctx->rootfs_path = rootfs;
    bctx->install_path = strpathjoin(rootfs, "chef", "install");
    bctx->build_ingredients_path = strpathjoin(
        rootfs, "chef", "ingredients", bctx->target_platform, bctx->target_architecture);
    return 0;
}

#else
static char* __get_username(void) {
    return platform_strdup("none");
}
#endif

static char* __fmt_env_option(const char* name, const char* value)
{
    char  tmp[512];
    char* result;
    snprintf(&tmp[0], sizeof(tmp), "%s=%s", name, value);
    result = platform_strdup(&tmp[0]);
    if (result == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for environment option\n");
    }
    return result;
}

static char** __initialize_env(struct __bake_build_options* options)
{
    char*  username;
    char** env;
    
    username = __get_username();
    if (username == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for username string\n");
        return NULL;
    }

    env = calloc(13, sizeof(char*));
    if (env == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for environment\n");
        free(username);
        return NULL;
    }

    env[0] = __fmt_env_option("USER", username);
    env[1] = __fmt_env_option("USERNAME", username);
    env[2] = __fmt_env_option("HOME", "/chef");
    env[3] = __fmt_env_option("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:");
    env[4] = __fmt_env_option("LD_LIBRARY_PATH", "/usr/local/lib");
    env[5] = __fmt_env_option("CHEF_TARGET_ARCH", options->target_architecture);
    env[6] = __fmt_env_option("CHEF_TARGET_PLATFORM", options->target_platform);

    // placeholders, to be filled in setup.c when iterating
    // build ingredients
    env[7] = __fmt_env_option("CHEF_BUILD_PATH", "");
    env[8] = __fmt_env_option("CHEF_BUILD_INCLUDE", "");
    env[9] = __fmt_env_option("CHEF_BUILD_LIBS", "");
    env[10] = __fmt_env_option("CHEF_BUILD_CCFLAGS", "");
    env[11] = __fmt_env_option("CHEF_BUILD_LDFLAGS", "");

    // Not guaranteed that ca-certificates is in the rootfs when building,
    // so let us add this to avoid git checking for now.
    env[12] = __fmt_env_option("GIT_SSL_NO_VERIFY", "1");

    free(username);
    return env;
}

struct __bake_build_context* build_context_create(struct __bake_build_options* options)
{
    struct __bake_build_context* bctx;

    bctx = calloc(1, sizeof(struct __bake_build_context));
    if (bctx == NULL) {
        return NULL;
    }

    bctx->recipe = options->recipe;
    bctx->build_cache = options->build_cache;
    bctx->bakectl_path = platform_strdup("/usr/bin/bakectl");
    bctx->host_cwd = platform_strdup(options->cwd);
    bctx->recipe_path = platform_strdup(options->recipe_path);
    bctx->target_platform = platform_strdup(options->target_platform);
    bctx->target_architecture = platform_strdup(options->target_architecture);
    memcpy(&bctx->cvd_address, options->cvd_address, sizeof(struct chef_config_address));

    // Before paths, but after all the other setup, setup base environment
    bctx->base_environment = __initialize_env(options);

    if (__construct_paths(bctx)) {
        VLOG_ERROR("bake", "build_context_create: failed to allocate memory for paths\n");
    }

    // initialize cvd client
    if (bake_client_initialize(bctx)) {
        VLOG_ERROR("bake", "build_context_create: failed to initialize client\n");
    }
    return bctx;
}
