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

#include <chef/kitchen.h>
#include <chef/platform.h>
#include <chef/user.h>
#include <libingredient.h>
#include <libpkgmgr.h>
#include <vlog.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include "private.h"

static struct pkgmngr* __setup_pkg_environment(struct kitchen_init_options* options, const char* chroot)
{
    static struct {
        const char* environment;
        struct pkgmngr* (*create)(struct pkgmngr_options*);
    } systems[] = {
        { "pkg-config", pkgmngr_pkgconfig_new },
        { NULL, NULL }
    };
    const char* env = options->pkg_environment;

    // default to pkg-config
    if (env == NULL) {
        env = "pkg-config";
    }

    for (int i = 0; systems[i].environment != NULL; i++) {
        if (strcmp(env, systems[i].environment) == 0) {
            VLOG_TRACE("kitchen", "initializing %s environment\n", env);
            return systems[i].create(&(struct pkgmngr_options) {
                .root = chroot, 
                .target_platform = options->target_platform,
                .target_architecture = options->target_architecture
            });
        }
    }
    return NULL;
}

int __get_kitchen_root(char* buffer, size_t maxLength, const char* uuid)
{
    char root[PATH_MAX];
    int  status;

    status = platform_getuserdir(&root[0], sizeof(root));
    if (status) {
        VLOG_ERROR("kitchen", "__get_kitchen_root: failed to resolve user homedir\n");
        return -1;
    }

    if (uuid != NULL) {
        snprintf(buffer, maxLength, "%s/.chef/kitchen/%s", &root[0], uuid);
    } else {
        snprintf(buffer, maxLength, "%s/.chef/kitchen", &root[0]);
    }
    return 0;
}

static char* __fmt_env_option(const char* name, const char* value)
{
    char  tmp[512];
    char* result;
    snprintf(&tmp[0], sizeof(tmp), "%s=%s", name, value);
    result = strdup(&tmp[0]);
    if (result == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for environment option\n");
    }
    return result;
}

static char** __initialize_env(struct kitchen* kitchen, const char* const* parentEnv)
{
    struct containerv_user* user;
    char**                  env;
    
    env = calloc(12, sizeof(char*));
    if (env == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for environment\n");
        return NULL;
    }

    user = containerv_user_new();
    if (user == NULL) {
        VLOG_FATAL("kitchen", "failed to allocate memory for user information\n");
        free(env);
        return NULL;
    }

    // we are not using the parent environment yet
    (void)parentEnv;

    env[0] = __fmt_env_option("USER", user->caller_name);
    env[1] = __fmt_env_option("USERNAME", user->caller_name);
    env[2] = __fmt_env_option("HOME", "/chef");
    env[3] = __fmt_env_option("PATH", "/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin:");
    env[4] = __fmt_env_option("LD_LIBRARY_PATH", "/usr/local/lib");
    env[5] = __fmt_env_option("CHEF_TARGET_ARCH", kitchen->target_architecture);
    env[6] = __fmt_env_option("CHEF_TARGET_PLATFORM", kitchen->target_platform);
    // env[7-10] are pkgmgr overrides
    if (kitchen->pkg_manager) {
        kitchen->pkg_manager->add_overrides(kitchen->pkg_manager, env);
    }
    containerv_user_delete(user);
    return env;
}

// <root>/.kitchen/output
// <root>/.kitchen/<recipe>/bin
// <root>/.kitchen/<recipe>/lib
// <root>/.kitchen/<recipe>/share
// <root>/.kitchen/<recipe>/usr/...
// <root>/.kitchen/<recipe>/chef/build
// <root>/.kitchen/<recipe>/chef/ingredients
// <root>/.kitchen/<recipe>/chef/toolchains
// <root>/.kitchen/<recipe>/chef/install => <root>/.kitchen/output
// <root>/.kitchen/<recipe>/chef/project => <root>
static int __kitchen_construct(struct kitchen_init_options* options, struct kitchen* kitchen)
{
    char buff[PATH_MAX*2];
    char root[PATH_MAX] = { 0 };
    int  status;
    VLOG_DEBUG("kitchen", "__kitchen_construct(name=%s)\n", options->recipe->project.name);

    status = __get_kitchen_root(&root[0], sizeof(root) - 1, recipe_cache_uuid());
    if (status) {
        VLOG_ERROR("kitchen", "__kitchen_construct: failed to resolve root directory\n");
        return -1;
    }

    memset(kitchen, 0, sizeof(struct kitchen));
    kitchen->target_platform = strdup(options->target_platform);
    kitchen->target_architecture = strdup(options->target_architecture);
    kitchen->host_cwd = strdup(options->project_path);
    kitchen->magic = __KITCHEN_INIT_MAGIC;
    kitchen->recipe = options->recipe;
    kitchen->recipe_path = strdup(options->recipe_path);

    kitchen->host_kitchen_project_root = strdup(&root[0]);

    // Format external chroot paths that are arch/platform agnostic
    snprintf(&buff[0], sizeof(buff), "%s/ns", &root[0]);
    kitchen->host_chroot = strdup(&buff[0]);
    kitchen->pkg_manager = __setup_pkg_environment(options, &buff[0]);

    // Before paths, but after all the other setup, setup base environment
    kitchen->base_environment = __initialize_env(kitchen, options->envp);

    snprintf(&buff[0], sizeof(buff), "%s/ns/chef/project", &root[0]);
    kitchen->host_project_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/ns/chef/install", &root[0]);
    kitchen->host_install_root = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/ns/chef/toolchains", &root[0]);
    kitchen->host_build_toolchains_path = strdup(&buff[0]);

    // Build/ingredients/install/checkpoint paths are different for each target
    snprintf(&buff[0], sizeof(buff), "%s/ns/chef/build/%s/%s",
        &root[0], options->target_platform, options->target_architecture
    );
    kitchen->host_build_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/ns/chef/ingredients/%s/%s",
        &root[0], options->target_platform, options->target_architecture
    );
    kitchen->host_build_ingredients_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "%s/ns/chef/install/%s/%s",
        &root[0], options->target_platform, options->target_architecture
    );
    kitchen->host_install_path = strdup(&buff[0]);

    // Format the internal chroot paths, again, we have paths that are shared between
    // platforms and archs
    kitchen->project_root = strdup("/chef/project");
    kitchen->build_toolchains_path = strdup("/chef/toolchains");
    kitchen->install_root = strdup("/chef/install");
    kitchen->bakectl_path = strdup("/usr/bin/bakectl");

    // And those that are not
    snprintf(&buff[0], sizeof(buff), "/chef/build/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->build_root = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/ingredients/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->build_ingredients_path = strdup(&buff[0]);

    snprintf(&buff[0], sizeof(buff), "/chef/install/%s/%s",
        options->target_platform, options->target_architecture
    );
    kitchen->install_path = strdup(&buff[0]);
    return 0;
}

int kitchen_initialize(struct kitchen_init_options* options, struct kitchen* kitchen)
{
    if (options == NULL || kitchen == NULL) {
        errno = EINVAL;
        return -1;
    }

    if (recipe_cache_initialize(options->recipe, options->project_path)) {
        VLOG_ERROR("kitchen", "failed to initialize recipe cache\n");
        return -1;
    }
    return __kitchen_construct(options, kitchen);
}
