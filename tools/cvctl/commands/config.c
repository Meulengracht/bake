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
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: cvctl config <param> [value]\n");
    printf("\n");
    printf("For end-to-end LCOW bundle setup, prefer `cvctl uvm import` or `cvctl uvm fetch`.\n");
    printf("\n");
    printf("LCOW settings:\n");
    printf("  cvctl config lcow.uvm-image-path <path>\n");
    printf("  cvctl config lcow.uvm-url        <url>\n");
    printf("  cvctl config lcow.kernel-file    <name>\n");
    printf("  cvctl config lcow.initrd-file    <name>\n");
    printf("  cvctl config lcow.boot-parameters <value>\n");
    printf("  cvctl config lcow.uvm-image-path --unset\n");
    printf("\n");
    printf("Options:\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
    printf("  --unset\n");
    printf("      Remove the configuration key\n");
}

static const char* __map_lcow_key(const char* option)
{
    if (!strcmp(option, "lcow.uvm-image-path")) {
        return "uvm-image-path";
    }
    if (!strcmp(option, "lcow.uvm-url")) {
        return "uvm-url";
    }
    if (!strcmp(option, "lcow.kernel-file")) {
        return "kernel-file";
    }
    if (!strcmp(option, "lcow.initrd-file")) {
        return "initrd-file";
    }
    if (!strcmp(option, "lcow.boot-parameters")) {
        return "boot-parameters";
    }
    return NULL;
}

static json_t* __load_root(const char* path)
{
    json_error_t error;
    json_t*      root;

    root = json_load_file(path, 0, &error);
    if (root != NULL) {
        return root;
    }

    if (json_error_code(&error) != json_error_cannot_open_file) {
        fprintf(stderr, "cvctl: failed to parse %s\n", path);
        return NULL;
    }

    root = json_object();
    if (root == NULL) {
        fprintf(stderr, "cvctl: failed to allocate configuration root\n");
        return NULL;
    }
    return root;
}

static int __handle_option(const char* option, const char* value, int unset)
{
    const char* key;
    const char* current;
    json_t*     root;
    json_t*     lcow;
    char        path[PATH_MAX] = { 0 };

    key = __map_lcow_key(option);
    if (key == NULL) {
        fprintf(stderr, "cvctl: unsupported config option '%s'\n", option);
        return -1;
    }

    if (chef_dirs_initialize(CHEF_DIR_SCOPE_DAEMON) != 0) {
        fprintf(stderr, "cvctl: failed to initialize directory code\n");
        return -1;
    }

    snprintf(&path[0], sizeof(path), "%s" CHEF_PATH_SEPARATOR_S "cvd.json", chef_dirs_config());
    root = __load_root(&path[0]);
    if (root == NULL) {
        return -1;
    }

    lcow = json_object_get(root, "lcow");
    if (lcow == NULL) {
        lcow = json_object();
        if (lcow == NULL) {
            json_decref(root);
            fprintf(stderr, "cvctl: failed to allocate lcow section\n");
            return -1;
        }
        json_object_set_new(root, "lcow", lcow);
    }

    if (unset) {
        if (value != NULL) {
            json_decref(root);
            fprintf(stderr, "cvctl: --unset cannot be combined with a value\n");
            return -1;
        }
        json_object_del(lcow, key);
        if (json_dump_file(root, &path[0], JSON_INDENT(2)) < 0) {
            json_decref(root);
            fprintf(stderr, "cvctl: failed to save configuration: %s\n", strerror(errno));
            return -1;
        }
        json_decref(root);
        return 0;
    }

    if (value == NULL) {
        json_t* value_object = json_object_get(lcow, key);
        current = value_object != NULL ? json_string_value(value_object) : NULL;
        printf("%s = %s\n", option, current != NULL ? current : "(null)");
        json_decref(root);
        return 0;
    }

    json_object_set_new(lcow, key, json_string(value));
    if (json_dump_file(root, &path[0], JSON_INDENT(2)) < 0) {
        json_decref(root);
        fprintf(stderr, "cvctl: failed to save configuration: %s\n", strerror(errno));
        return -1;
    }

    json_decref(root);
    return 0;
}

int config_main(int argc, char** argv, char** envp, struct cvctl_command_options* options)
{
    const char* option = NULL;
    const char* value = NULL;
    int         unset = 0;

    (void)envp;
    (void)options;

    if (argc > 2) {
        for (int i = 2; i < argc; i++) {
            if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
                __print_help();
                return 0;
            } else if (!strcmp(argv[i], "--unset")) {
                unset = 1;
            } else if (option == NULL) {
                option = argv[i];
            } else if (value == NULL) {
                value = argv[i];
            }
        }
    }

    if (option == NULL) {
        fprintf(stderr, "cvctl: missing option\n");
        __print_help();
        return -1;
    }

    return __handle_option(option, value, unset);
}