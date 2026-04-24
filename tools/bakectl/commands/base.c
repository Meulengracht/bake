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

#include <chef/config.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <jansson.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: bakectl base <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  show <base>\n");
    printf("      Show the registered base image path for one base\n");
    printf("  import <base> <source-dir>\n");
    printf("      Normalize and register a Windows base directory for bake\n");
}

static int __path_exists(const char* path)
{
    struct platform_stat st;

    return (path != NULL && platform_stat(path, &st) == 0) ? 1 : 0;
}

static int __path_is_directory(const char* path)
{
    struct platform_stat st;

    return (path != NULL && platform_stat(path, &st) == 0 && st.type == PLATFORM_FILETYPE_DIRECTORY) ? 1 : 0;
}

static char* __find_windowsfilter_root(const char* source_root)
{
    char* candidate;
    char* layerchain;

    if (source_root == NULL || source_root[0] == '\0') {
        return NULL;
    }

    layerchain = strpathcombine(source_root, "layerchain.json");
    if (layerchain != NULL && __path_exists(layerchain)) {
        free(layerchain);
        return platform_strdup(source_root);
    }
    free(layerchain);

    candidate = strpathcombine(source_root, "windowsfilter");
    if (candidate == NULL) {
        return NULL;
    }

    layerchain = strpathcombine(candidate, "layerchain.json");
    if (layerchain != NULL && __path_exists(layerchain)) {
        free(layerchain);
        return candidate;
    }

    free(layerchain);
    free(candidate);
    return NULL;
}

static int __write_base_manifest(const char* target_dir, const char* base_name, int has_utility_vm)
{
    json_t* root;
    json_t* files;
    char*   manifest_path;
    int     status = -1;

    manifest_path = strpathcombine(target_dir, "base.json");
    if (manifest_path == NULL) {
        return -1;
    }

    root = json_object();
    files = json_object();
    if (root == NULL || files == NULL) {
        goto cleanup;
    }

    if (json_object_set_new(root, "kind", json_string("windows-base")) != 0 ||
        json_object_set_new(root, "format_version", json_integer(1)) != 0 ||
        json_object_set_new(root, "base", json_string(base_name)) != 0 ||
        json_object_set_new(files, "rootfs", json_string("windowsfilter")) != 0 ||
        json_object_set_new(files, "utility_vm", has_utility_vm ? json_string("UtilityVM") : json_null()) != 0 ||
        json_object_set_new(root, "files", files) != 0) {
        goto cleanup;
    }

    files = NULL;
    status = json_dump_file(root, manifest_path, JSON_INDENT(2)) == 0 ? 0 : -1;

cleanup:
    free(manifest_path);
    if (files != NULL) {
        json_decref(files);
    }
    if (root != NULL) {
        json_decref(root);
    }
    return status;
}

static int __normalize_base_directory(const char* base_name, const char* source_dir, const char* output_dir)
{
    char* absolute_source;
    char* windowsfilter_root;
    char* target_rootfs;
    char* utilityvm_source = NULL;
    char* utilityvm_target = NULL;
    char* parent = NULL;
    int   has_utility_vm = 0;
    int   status = -1;

    absolute_source = platform_abspath(source_dir);
    if (absolute_source == NULL) {
        fprintf(stderr, "bakectl: source directory not found: %s\n", source_dir);
        return -1;
    }

    windowsfilter_root = __find_windowsfilter_root(absolute_source);
    if (windowsfilter_root == NULL) {
        fprintf(stderr, "bakectl: no windowsfilter folder with layerchain.json found under %s\n", absolute_source);
        goto cleanup;
    }

    (void)platform_rmdir(output_dir);
    if (platform_mkdir(output_dir) != 0) {
        fprintf(stderr, "bakectl: failed to create output directory %s\n", output_dir);
        goto cleanup;
    }

    target_rootfs = strpathcombine(output_dir, "windowsfilter");
    if (target_rootfs == NULL) {
        goto cleanup;
    }

    if (platform_copydir(windowsfilter_root, target_rootfs) != 0) {
        fprintf(stderr, "bakectl: failed to copy windowsfilter contents from %s\n", windowsfilter_root);
        free(target_rootfs);
        goto cleanup;
    }
    free(target_rootfs);

    utilityvm_source = strpathcombine(absolute_source, "UtilityVM");
    if (utilityvm_source == NULL) {
        goto cleanup;
    }
    if (!__path_is_directory(utilityvm_source)) {
        free(utilityvm_source);
        parent = platform_strdup(windowsfilter_root);
        if (parent == NULL) {
            goto cleanup;
        }

        {
            char* separator = strrchr(parent, '\\');
            if (separator == NULL) {
                separator = strrchr(parent, '/');
            }
            if (separator != NULL) {
                *separator = '\0';
            }
        }

        utilityvm_source = strpathcombine(parent, "UtilityVM");
        free(parent);
        parent = NULL;
        if (utilityvm_source == NULL) {
            goto cleanup;
        }
    }

    if (__path_is_directory(utilityvm_source)) {
        utilityvm_target = strpathcombine(output_dir, "UtilityVM");
        if (utilityvm_target == NULL) {
            goto cleanup;
        }

        if (platform_copydir(utilityvm_source, utilityvm_target) != 0) {
            fprintf(stderr, "bakectl: failed to copy UtilityVM contents from %s\n", utilityvm_source);
            goto cleanup;
        }
        has_utility_vm = 1;
    }

    if (__write_base_manifest(output_dir, base_name, has_utility_vm) != 0) {
        fprintf(stderr, "bakectl: failed to write base manifest for %s\n", base_name);
        goto cleanup;
    }

    printf("Windows base prepared at %s\n", output_dir);
    status = 0;

cleanup:
    free(absolute_source);
    free(windowsfilter_root);
    free(utilityvm_source);
    free(utilityvm_target);
    free(parent);
    return status;
}

static int __base_cache_root(const char* base_name, char** path_out)
{
    char sanitized[256] = { 0 };
    size_t index = 0;
    const char* cache_dir;

    if (base_name == NULL || path_out == NULL) {
        return -1;
    }

    cache_dir = chef_dirs_cache();
    if (cache_dir == NULL) {
        return -1;
    }

    for (const char* p = base_name; *p != '\0' && index < sizeof(sanitized) - 1; ++p) {
        char ch = *p;
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')) {
            sanitized[index++] = ch;
        } else {
            sanitized[index++] = '-';
        }
    }

    *path_out = strpathjoin(cache_dir, "bases", sanitized, NULL);
    return *path_out == NULL ? -1 : 0;
}

static int __load_config(struct chef_config** config_out, void** section_out)
{
    struct chef_config* config;
    void*               section;

    config = chef_config_load(chef_dirs_config());
    if (config == NULL) {
        return -1;
    }

    section = chef_config_section(config, "base-images");
    if (section == NULL) {
        return -1;
    }

    *config_out = config;
    *section_out = section;
    return 0;
}

static int __show_one(const char* key)
{
    struct chef_config* config;
    void*               section;
    const char*         value;

    if (__load_config(&config, &section) != 0) {
        fprintf(stderr, "bakectl: failed to load configuration\n");
        return -1;
    }

    value = chef_config_get_string(config, section, key);
    printf("%s = %s\n", key, value != NULL ? value : "(null)");
    return 0;
}

static int __register_base(const char* base_name, const char* path)
{
    struct chef_config* config;
    void*               section;

    if (__load_config(&config, &section) != 0) {
        fprintf(stderr, "bakectl: failed to load configuration\n");
        return -1;
    }

    if (chef_config_set_string(config, section, base_name, path) != 0 || chef_config_save(config) != 0) {
        fprintf(stderr, "bakectl: failed to save base registration\n");
        return -1;
    }

    printf("Registered base %s -> %s\n", base_name, path);
    return 0;
}

int base_main(int argc, char** argv, struct __bakelib_context* context, struct bakectl_command_options* options)
{
    char*       output_dir = NULL;
    const char* command;
    const char* base_name;

    (void)context;
    (void)options;

    if (argc < 3 || !strcmp(argv[2], "-h") || !strcmp(argv[2], "--help")) {
        __print_help();
        return argc < 3 ? -1 : 0;
    }

    if (chef_dirs_initialize(CHEF_DIR_SCOPE_BAKE) != 0) {
        fprintf(stderr, "bakectl: failed to initialize directory code\n");
        return -1;
    }

    command = argv[2];
    if (!strcmp(command, "show")) {
        if (argc >= 4) {
            return __show_one(argv[3]);
        }
        fprintf(stderr, "bakectl: show currently requires a base identifier\n");
        return -1;
    }

    if (argc < 5) {
        fprintf(stderr, "bakectl: missing arguments\n");
        __print_help();
        return -1;
    }

    base_name = argv[3];
    if (__base_cache_root(base_name, &output_dir) != 0) {
        fprintf(stderr, "bakectl: failed to allocate cache path\n");
        return -1;
    }

    if (!strcmp(command, "import")) {
        if (__normalize_base_directory(base_name, argv[4], output_dir) != 0) {
            free(output_dir);
            fprintf(stderr, "bakectl: base import failed\n");
            return -1;
        }
        return __register_base(base_name, output_dir);
    }

    free(output_dir);
    fprintf(stderr, "bakectl: unknown base command %s\n", command);
    __print_help();
    return -1;
}
