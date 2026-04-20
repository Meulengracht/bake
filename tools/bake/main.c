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

#include <errno.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <vlog.h>
#include "chef-config.h"
#include "commands/commands.h"

extern int init_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int run_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int sign_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int clean_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int store_main(int argc, char** argv, char** envp, struct bake_command_options* options);
extern int remote_main(int argc, char** argv, char** envp, struct bake_command_options* options);

struct command_handler {
    char* name;
    int (*handler)(int argc, char** argv, char** envp, struct bake_command_options* options);
};

static struct command_handler g_commands[] = {
    { "init",   init_main },
    { "build",  run_main },
    { "sign",   sign_main },
    { "clean",  clean_main },
    { "store",  store_main },
    { "remote", remote_main }
};

enum bake_global_action {
    BAKE_GLOBAL_ACTION_NONE,
    BAKE_GLOBAL_ACTION_HELP,
    BAKE_GLOBAL_ACTION_VERSION
};

struct bake_global_options {
    int                     log_level;
    enum bake_global_action action;
};

static void __print_help(void)
{
    printf("Usage: bake [global-options] <command> [command-options]\n");
    printf("\n");
    printf("Build-oriented commands search for default recipe names when no recipe is supplied:\n");
    printf("  chef/recipe.yaml\n");
    printf("  recipe.yaml\n");
    printf("\n");
    printf("Commands:\n");
    printf("  init\n");
    printf("              initializes a new recipe in the current directory\n");
    printf("  build [recipe]\n");
    printf("              builds the provided bake recipe\n");
    printf("  clean [recipe]\n");
    printf("              cleanup all build and intermediate directories\n");
    printf("  sign <package>\n");
    printf("              sign the provided package, this is only required for local installs\n");
    printf("  remote {init, build, resume, download, list, info}\n");
    printf("              used for building recipes remotely for any given configured\n");
    printf("              build server, parallel builds can be initiated for multiple\n");
    printf("              architectures by using the command-local --archs switch\n");
    printf("  store {list, update, remove, clean}\n");
    printf("              manage ingredients used for building\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  --root <path>\n");
    printf("      Set a custom root path for all state and data files\n");
    printf("  -v..\n");
    printf("      Controls the verbosity of bake\n");
    printf("  --version\n");
    printf("      Print the version of bake\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static struct command_handler* __get_command(const char* command)
{
    for (int i = 0; i < sizeof(g_commands) / sizeof(struct command_handler); i++) {
        if (!strcmp(command, g_commands[i].name)) {
            return &g_commands[i];
        }
    }
    return NULL;
}

static int __get_cwd(char** bufferOut)
{
    char*  cwd;
    int    status;

    cwd = malloc(4096);
    if (cwd == NULL) {
        return -1;
    }

    status = platform_getcwd(cwd, 4096);
    if (status) {
        // buffer was too small
        fprintf(stderr, "bake: could not get current working directory, buffer too small?\n");
        free(cwd);
        return -1;
    }
    *bufferOut = cwd;
    return 0;
}

static int __file_exists(const char* path)
{
    struct platform_stat stats;
    return platform_stat(path, &stats) == 0 ? 1 : 0;
}

#ifdef CHEF_AS_SNAP
#include <stdlib.h>
static unsigned int __get_snap_uid(void)
{
    char* uidstr = getenv("SNAP_UID");
    if (uidstr == NULL) {
        // fallback
        return getuid();
    }
    return (unsigned int)atoi(uidstr);
}
#endif

static int __parse_global_option(int argc, char** argv, int* index, void* context)
{
    struct bake_global_options* options = context;
    int                         verbosity;

    if (__cli_is_help_switch(argv[*index])) {
        options->action = BAKE_GLOBAL_ACTION_HELP;
        return CLI_PARSE_RESULT_HANDLED;
    }
    if (!strcmp(argv[*index], "--version")) {
        options->action = BAKE_GLOBAL_ACTION_VERSION;
        return CLI_PARSE_RESULT_HANDLED;
    }

    if (!strncmp(argv[*index], "--root", 6)) {
        char* root = __split_switch(argv, argc, index);

        if (root == NULL) {
            fprintf(stderr, "bake: missing path for --root\n");
            return CLI_PARSE_RESULT_ERROR;
        }
        chef_dirs_set_root(root);
        return CLI_PARSE_RESULT_HANDLED;
    }

    verbosity = __cli_parse_verbosity_switch(argv[*index]);
    if (verbosity > 0) {
        options->log_level += verbosity;
        return CLI_PARSE_RESULT_HANDLED;
    }

    if (argv[*index][0] == '-') {
        fprintf(stderr, "bake: invalid global option %s\n", argv[*index]);
        return CLI_PARSE_RESULT_ERROR;
    }
    return CLI_PARSE_RESULT_UNHANDLED;
}

int main(int argc, char** argv, char** envp)
{
    struct command_handler*     command = NULL;
    struct bake_command_options options = { 0 };
    struct bake_global_options  global_options = {
        .log_level = VLOG_LEVEL_TRACE,
        .action = BAKE_GLOBAL_ACTION_NONE
    };
    int                         commandIndex = argc;
    int                         status;
    
#if __linux__
    // make sure we're not actually running as root
#ifdef CHEF_AS_SNAP
    if (__get_snap_uid() == 0) {
        fprintf(stderr, "bake: should not be run as root, aborting.\n");
        errno = EPERM;
        return -1;
    }
#else
    if (getuid() == 0 || getgid() == 0) {
        fprintf(stderr, "bake: should not be run as root, aborting.\n");
        errno = EPERM;
        return -1;
    }
#endif
#endif

    status = __cli_parse_staged_global_options(argc, argv, __parse_global_option, &global_options, &commandIndex);
    if (status) {
        return -1;
    }

    if (global_options.action == BAKE_GLOBAL_ACTION_HELP) {
        __print_help();
        return 0;
    }
    if (global_options.action == BAKE_GLOBAL_ACTION_VERSION) {
        printf("bake: version " PROJECT_VER "\n");
        return 0;
    }

    if (commandIndex >= argc) {
        __print_help();
        return 0;
    }

    command = __get_command(argv[commandIndex]);
    if (command == NULL) {
        if (__file_exists(argv[commandIndex])) {
            fprintf(stderr, "bake: recipe paths must be supplied to 'bake build <recipe>'\n");
        } else {
            fprintf(stderr, "bake: invalid command %s\n", argv[commandIndex]);
        }
        return -1;
    }

    // get the current working directory
    status = __get_cwd((char**)&options.cwd);
    if (status) {
        return -1;
    }

    // initialize the logging system
    vlog_initialize(global_options.log_level);

    // initialize directories
    status = chef_dirs_initialize(CHEF_DIR_SCOPE_BAKE);
    if (status) {
        fprintf(stderr, "bake: failed to initialize directories\n");
        free((void*)options.cwd);
        vlog_cleanup();
        return -1;
    }

    status = command->handler(argc - commandIndex, &argv[commandIndex], envp, &options);
    bake_command_options_reset(&options);
    free((void*)options.cwd);
    vlog_cleanup();
    return status;
}
