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

#include <chef/platform.h>
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
#include <string.h>
#include <vlog.h>

#include "private.h"

int __platform_host(lua_State* vm) {
    lua_pushstring(vm, CHEF_PLATFORM_STR);
    return 1;
}

int __arch_host(lua_State* vm) {
    lua_pushstring(vm, CHEF_ARCHITECTURE_STR);
    return 1;
}

int __platform_target(lua_State* vm) {
    lua_pushstring(vm, __oven_instance()->variables.target_platform);
    return 1;
}

int __arch_target(lua_State* vm) {
    lua_pushstring(vm, __oven_instance()->variables.target_arch);
    return 1;
}

const struct luaL_Reg g_subsystem_namespace[] = {
    { "platform_host",   __platform_host },
    { "platform_target", __platform_target },
    { "arch_host",       __arch_host },
    { "arch_target",     __arch_target },

    // EOT
    { NULL, NULL },
};

int __paths_install(lua_State* vm) {
    lua_pushstring(vm, __oven_instance()->paths.install_root);
    return 1;
}

const struct luaL_Reg g_paths_namespace[] = {
    { "install",   __paths_install },

    // EOT
    { NULL, NULL },
};

int __build_shell(lua_State* vm) {
    const char* path = luaL_checkstring(vm, 1);
    const char* args = luaL_checkstring(vm, 2);
    const char* cwd;
    int         status;

    if (path == NULL || strlen(path) == 0) {
        VLOG_ERROR("build.shell", "path must be supplied\n");
        return lua_error(vm);
    }

    lua_getglobal(vm, "SCRIPT_CWD");
    if (lua_isnil(vm, -1)) {
        VLOG_ERROR("build.shell", "SCRIPT_CWD has been corrupted, aborting\n");
        return lua_error(vm);
    }
    cwd = lua_tostring(vm, -1);

    status = platform_spawn(
        path,
        args,
        __oven_instance()->process_environment, 
        &(struct platform_spawn_options) {
            .cwd = cwd,
        }
    );

    // cleanup globals
    lua_pop(vm, 1);

    // handle status code
    if (status) {
        VLOG_ERROR("build.shell", "failed to execute %s: %i\n", path, status);
        VLOG_ERROR("build.shell", "cwd=%s, args=%s\n", cwd, args);
        return lua_error(vm);
    }
    return 0;
}

const struct luaL_Reg g_build_namespace[] = {
    { "shell", __build_shell },

    // EOT
    { NULL, NULL },
};

static const char* __cwd_from_enum(enum oven_script_root_dir root_dir)
{
    switch (root_dir) {
        case OVEN_SCRIPT_ROOT_DIR_PROJECT:
            return __oven_instance()->paths.project_root;
        case OVEN_SCRIPT_ROOT_DIR_SOURCE:
            return __oven_instance()->recipe.source_root;
        case OVEN_SCRIPT_ROOT_DIR_BUILD:
            return __oven_instance()->recipe.build_root;
    }
    return NULL;
}

static lua_State* __create_vm(struct oven_script_options* options)
{
    lua_State*  vm = luaL_newstate();
    const char* cwd;
    if (vm == NULL) {
        return NULL;
    }

    // for now we do full access to libraries, this may change
    // in the future
    luaL_openlibs(vm);

    // push global script variables (NULL-safe)
    cwd = __cwd_from_enum(options->root_dir);
    if (cwd == NULL) {
        // This should never happen, so exit and cleanup
        lua_close(vm);
        return NULL;
    }

    lua_pushstring(vm, cwd);
    lua_setglobal(vm, "SCRIPT_CWD");

    // Create the `build` table once, leave it on the stack while attaching
    // sub-libraries, then set it as a global. This avoids repeated
    // global lookups and simplifies stack handling.
    luaL_newlib(vm, g_build_namespace);

    // attach subsystem: stack = [build]
    // create subsystem table (push), set as field on build (consumes value)
    luaL_newlib(vm, g_subsystem_namespace);
    lua_setfield(vm, -2, "subsystem"); // sets build.subsystem, pops subsystem

    // attach paths: stack = [build]
    luaL_newlib(vm, g_paths_namespace);
    lua_setfield(vm, -2, "paths"); // sets build.paths, pops paths

    // finally set global 'build' and pop it from the stack
    lua_setglobal(vm, "build");

    return vm;
}

static void __delete_vm(lua_State* vm)
{
    lua_close(vm);
}

int oven_script(const char* script, struct oven_script_options* options)
{
    lua_State* vm = __create_vm(options);
    int        status = 0;

    if (vm == NULL) {
        return -1;
    }

    // execute the script given
    if (luaL_dostring(vm, script) != LUA_OK) {
        VLOG_ERROR("oven", "failed to execute script: %s\n", lua_tostring(vm, lua_gettop(vm)));
        status = -1;

        // remove the error from stack
        lua_pop(vm, lua_gettop(vm));
    }

    // Do we need to pop more off the stack here?

    // cleanup
    __delete_vm(vm);
    return status;
}
