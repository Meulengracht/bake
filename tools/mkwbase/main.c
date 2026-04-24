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
#include <chef/platform.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>
#include "chef-config.h"

#if defined(_MSC_VER)
#define popen _popen
#define pclose _pclose
#endif

struct mkwbase_options {
    const char* base;
    const char* source_dir;
    const char* image;
    const char* output_dir;
    int         force;
};

static void __print_help(void)
{
    printf("Usage: mkwbase [global-options] <command> [command-options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  normalize\n");
    printf("      Normalize a Windows base directory into Chef's expected layout\n");
    printf("  construct\n");
    printf("      Build a Windows base directory from a Docker/MCR image\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  -v, --version\n");
    printf("      Print the version of mkwbase\n");
    printf("\n");
    printf("Command Options:\n");
    printf("  -b, --base <base>\n");
    printf("      Logical base name, for example windows:ltsc2022\n");
    printf("  -s, --source <dir>\n");
    printf("      Source directory for normalize\n");
    printf("  -i, --image <image>\n");
    printf("      Container image for construct\n");
    printf("  -o, --output <dir>\n");
    printf("      Output directory for the normalized base\n");
    printf("  -f, --force\n");
    printf("      Replace the output directory if it already exists\n");
    printf("  -h, --help\n");
    printf("      Print this help message\n");
}

static void __spawn_output_handler(const char* line, enum platform_spawn_output_type type)
{
    FILE* stream = type == PLATFORM_SPAWN_OUTPUT_TYPE_STDERR ? stderr : stdout;

    fputs(line, stream);
    if (strchr(line, '\n') == NULL) {
        fputc('\n', stream);
    }
}

static int __append_fragment(char* buffer, size_t buffer_size, size_t* index, const char* fragment)
{
    int written;

    if (*index >= buffer_size) {
        return -1;
    }

    written = snprintf(buffer + *index, buffer_size - *index, "%s", fragment);
    if (written < 0 || (size_t)written >= buffer_size - *index) {
        return -1;
    }

    *index += (size_t)written;
    return 0;
}

static int __append_token(char* buffer, size_t buffer_size, size_t* index, const char* token)
{
    if (*index != 0 && __append_fragment(buffer, buffer_size, index, " ") != 0) {
        return -1;
    }

    if (__append_fragment(buffer, buffer_size, index, "\"") != 0) {
        return -1;
    }

    for (const char* p = token; *p != '\0'; ++p) {
        char piece[3] = { 0 };

        if (*p == '\"') {
            piece[0] = '\\';
            piece[1] = '\"';
        } else {
            piece[0] = *p;
        }

        if (__append_fragment(buffer, buffer_size, index, piece) != 0) {
            return -1;
        }
    }

    return __append_fragment(buffer, buffer_size, index, "\"");
}

static const char* __errno_message(void)
{
    const char* message = strerror(errno);

    return message != NULL ? message : "unknown";
}

static void __report_errno_path(const char* operation, const char* path)
{
    fprintf(stderr,
        "mkwbase: %s failed for %s (errno=%d: %s)\n",
        operation,
        path != NULL ? path : "(null)",
        errno,
        __errno_message());
}

static void __report_errno_copy(const char* operation, const char* source, const char* target)
{
    fprintf(stderr,
        "mkwbase: %s failed (source=%s, target=%s, errno=%d: %s)\n",
        operation,
        source != NULL ? source : "(null)",
        target != NULL ? target : "(null)",
        errno,
        __errno_message());

#if defined(_WIN32) || defined(_WIN64)
    {
        unsigned long win32_error = platform_copydir_lasterror();
        const char*   win32_operation = platform_copydir_lasterror_operation();

        if (win32_error != 0 && win32_operation != NULL) {
            fprintf(stderr,
                "mkwbase: copydir win32 failure during %s (win32=%lu)\n",
                win32_operation,
                win32_error);
        }
    }
#endif
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

static int __copy_directory_recursive(const char* source_dir, const char* target_dir)
{
    if (platform_copydir(source_dir, target_dir) != 0) {
        __report_errno_copy("failed to copy directory", source_dir, target_dir);
        return -1;
    }
    return 0;
}

static const char* __leaf_name(const char* path)
{
    const char* separator;

    if (path == NULL) {
        return NULL;
    }

    separator = strrchr(path, '\\');
    if (separator == NULL) {
        separator = strrchr(path, '/');
    }
    return separator != NULL ? separator + 1 : path;
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

static int __prepare_output_directory(const char* output_dir, int force)
{
    if (__path_exists(output_dir)) {
        if (!force) {
            fprintf(stderr, "mkwbase: output path already exists: %s\n", output_dir);
            return -1;
        }
        (void)platform_rmdir(output_dir);
    }

    if (platform_mkdir(output_dir) != 0) {
        fprintf(stderr, "mkwbase: failed to create output directory %s\n", output_dir);
        return -1;
    }
    return 0;
}

static int __normalize_base_directory(const char* base_name, const char* source_dir, const char* output_dir, int force)
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
        fprintf(stderr, "mkwbase: source directory not found: %s\n", source_dir);
        return -1;
    }

    windowsfilter_root = __find_windowsfilter_root(absolute_source);
    if (windowsfilter_root == NULL) {
        fprintf(stderr, "mkwbase: no windowsfilter folder with layerchain.json found under %s\n", absolute_source);
        goto cleanup;
    }

    if (__prepare_output_directory(output_dir, force) != 0) {
        goto cleanup;
    }

    target_rootfs = strpathcombine(output_dir, "windowsfilter");
    if (target_rootfs == NULL) {
        goto cleanup;
    }

    if (__copy_directory_recursive(windowsfilter_root, target_rootfs) != 0) {
        fprintf(stderr, "mkwbase: failed to copy windowsfilter contents from %s\n", windowsfilter_root);
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
        utilityvm_source = NULL;

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
        if (utilityvm_source == NULL) {
            goto cleanup;
        }
    }

    if (__path_is_directory(utilityvm_source)) {
        utilityvm_target = strpathcombine(output_dir, "UtilityVM");
        if (utilityvm_target == NULL) {
            goto cleanup;
        }

        if (__copy_directory_recursive(utilityvm_source, utilityvm_target) != 0) {
            fprintf(stderr, "mkwbase: failed to copy UtilityVM contents from %s\n", utilityvm_source);
            goto cleanup;
        }
        has_utility_vm = 1;
    }

    if (__write_base_manifest(output_dir, base_name, has_utility_vm) != 0) {
        fprintf(stderr, "mkwbase: failed to write base manifest for %s\n", base_name);
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

static char* __temp_directory_new(const char* prefix)
{
    char* tmpdir;
    char  guid[40];
    char  name[128];
    char* path;
    static int seeded = 0;

    tmpdir = platform_tmpdir();
    if (tmpdir == NULL) {
        return NULL;
    }

    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = 1;
    }

    platform_guid_new_string(guid);
    snprintf(name, sizeof(name), "%s%s", prefix, guid);
    path = strpathcombine(tmpdir, name);
    free(tmpdir);
    if (path == NULL) {
        return NULL;
    }

    if (platform_mkdir(path) != 0) {
        free(path);
        return NULL;
    }
    return path;
}

static int __run_command(const char* program, const char* arguments)
{
    return platform_spawn(program, arguments, NULL, &(struct platform_spawn_options) {
        .output_handler = __spawn_output_handler
    });
}

static int __docker_pull(const char* image)
{
    char   arguments[4096] = { 0 };
    size_t index = 0;

    if (__append_token(arguments, sizeof(arguments), &index, "pull") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, image) != 0) {
        return -1;
    }
    return __run_command("docker", arguments);
}

static int __docker_create(const char* container_name, const char* image)
{
    char   arguments[4096] = { 0 };
    size_t index = 0;

    if (__append_token(arguments, sizeof(arguments), &index, "create") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--name") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, container_name) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, image) != 0) {
        return -1;
    }
    return __run_command("docker", arguments);
}

static void __docker_remove(const char* container_name)
{
    char   arguments[4096] = { 0 };
    size_t index = 0;

    if (__append_token(arguments, sizeof(arguments), &index, "rm") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "-f") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, container_name) != 0) {
        return;
    }
    (void)platform_spawn("docker", arguments, NULL, &(struct platform_spawn_options) { 0 });
}

static int __capture_line(const char* command, char** line_out)
{
    FILE* pipe;
    char  buffer[PATH_MAX] = { 0 };
    char* line;
    int   status;

    if (line_out == NULL) {
        return -1;
    }
    *line_out = NULL;

    pipe = popen(command, "r");
    if (pipe == NULL) {
        return -1;
    }

    if (fgets(buffer, sizeof(buffer), pipe) == NULL) {
        status = pclose(pipe);
        (void)status;
        return -1;
    }

    status = pclose(pipe);
    if (status != 0) {
        return -1;
    }

    line = platform_strdup(buffer);
    if (line == NULL) {
        return -1;
    }

    while (line[0] != '\0') {
        size_t length = strlen(line);
        if (length == 0 || (line[length - 1] != '\n' && line[length - 1] != '\r')) {
            break;
        }
        line[length - 1] = '\0';
    }

    *line_out = line;
    return 0;
}

static int __docker_inspect_layer_path(const char* container_name, char** layer_path_out)
{
    char command[4096] = { 0 };

    if (snprintf(
            command,
            sizeof(command),
            "docker inspect --format \"{{if .GraphDriver.Data.Dir}}{{.GraphDriver.Data.Dir}}{{else if .GraphDriver.Data.dir}}{{.GraphDriver.Data.dir}}{{else}}{{.GraphDriver.Data.UpperDir}}{{end}}\" \"%s\"",
            container_name) < 0) {
        return -1;
    }

    return __capture_line(command, layer_path_out);
}

static int __copy_parent_layers(const char* layer_dir, const char* windowsfilter_dir)
{
    char*  chain_path = NULL;
    char*  target_chain_path = NULL;
    char*  parents_dir = NULL;
    json_t* source = NULL;
    json_t* rewritten = NULL;
    json_error_t error;
    size_t index;
    json_t* entry;
    int     status = -1;

    chain_path = strpathcombine(layer_dir, "layerchain.json");
    if (chain_path == NULL) {
        goto cleanup;
    }

    if (!__path_exists(chain_path)) {
        status = 0;
        goto cleanup;
    }

    source = json_load_file(chain_path, 0, &error);
    if (source == NULL || !json_is_array(source)) {
        fprintf(stderr,
            "mkwbase: failed to parse %s (line=%d, column=%d, error=%s)\n",
            chain_path,
            error.line,
            error.column,
            error.text);
        goto cleanup;
    }

    parents_dir = strpathcombine(windowsfilter_dir, "parents");
    if (parents_dir == NULL) {
        fprintf(stderr,
            "mkwbase: failed to allocate parents directory path under %s\n",
            windowsfilter_dir != NULL ? windowsfilter_dir : "(null)");
        goto cleanup;
    }
    if (!__path_is_directory(parents_dir) && platform_mkdir(parents_dir) != 0) {
        __report_errno_path("failed to create parents directory", parents_dir);
        goto cleanup;
    }

    target_chain_path = strpathcombine(windowsfilter_dir, "layerchain.json");
    if (target_chain_path == NULL) {
        fprintf(stderr,
            "mkwbase: failed to allocate rewritten layerchain path under %s\n",
            windowsfilter_dir != NULL ? windowsfilter_dir : "(null)");
        goto cleanup;
    }

    rewritten = json_array();
    if (rewritten == NULL) {
        fprintf(stderr, "mkwbase: failed to allocate rewritten parent layer array\n");
        goto cleanup;
    }

    json_array_foreach(source, index, entry) {
        const char* parent_path = json_string_value(entry);
        const char* name;
        char*       destination;
        char        relative[PATH_MAX];

        if (parent_path == NULL || parent_path[0] == '\0') {
            continue;
        }

        name = __leaf_name(parent_path);
        if (name == NULL || name[0] == '\0') {
            continue;
        }

        destination = strpathcombine(parents_dir, name);
        if (destination == NULL) {
            fprintf(stderr,
                "mkwbase: failed to allocate parent layer destination for %s under %s\n",
                name,
                parents_dir != NULL ? parents_dir : "(null)");
            goto cleanup;
        }

        if (!__path_is_directory(destination) && __copy_directory_recursive(parent_path, destination) != 0) {
            fprintf(stderr,
                "mkwbase: failed to copy parent layer directory %s into %s\n",
                parent_path,
                destination);
            free(destination);
            goto cleanup;
        }
        free(destination);

        snprintf(relative, sizeof(relative), "parents\\%s", name);
        if (json_array_append_new(rewritten, json_string(relative)) != 0) {
            fprintf(stderr,
                "mkwbase: failed to append rewritten parent layer entry %s\n",
                relative);
            goto cleanup;
        }
    }

    if (json_dump_file(rewritten, target_chain_path, JSON_INDENT(2)) != 0) {
        __report_errno_path("failed to write rewritten layerchain", target_chain_path);
        goto cleanup;
    }

    status = 0;

cleanup:
    free(chain_path);
    free(target_chain_path);
    free(parents_dir);
    if (source != NULL) {
        json_decref(source);
    }
    if (rewritten != NULL) {
        json_decref(rewritten);
    }
    return status;
}

static char* __find_utilityvm_path(const char* layer_dir)
{
    char*  candidate = NULL;
    char*  chain_path = NULL;
    json_t* chain = NULL;
    json_error_t error;
    size_t index;
    json_t* entry;

    candidate = strpathcombine(layer_dir, "UtilityVM");
    if (candidate != NULL && __path_is_directory(candidate)) {
        return candidate;
    }
    free(candidate);

    chain_path = strpathcombine(layer_dir, "layerchain.json");
    if (chain_path == NULL || !__path_exists(chain_path)) {
        free(chain_path);
        return NULL;
    }

    chain = json_load_file(chain_path, 0, &error);
    free(chain_path);
    if (chain == NULL || !json_is_array(chain)) {
        if (chain != NULL) {
            json_decref(chain);
        }
        return NULL;
    }

    json_array_foreach(chain, index, entry) {
        const char* parent_path = json_string_value(entry);

        if (parent_path == NULL || parent_path[0] == '\0') {
            continue;
        }

        candidate = strpathcombine(parent_path, "UtilityVM");
        if (candidate != NULL && __path_is_directory(candidate)) {
            json_decref(chain);
            return candidate;
        }
        free(candidate);
    }

    json_decref(chain);
    return NULL;
}

static int __construct_base(const char* base_name, const char* image, const char* output_dir, int force)
{
    char* temp_root = NULL;
    char* windowsfilter_dir = NULL;
    char* utilityvm_dir = NULL;
    char* container_name = NULL;
    char* layer_path = NULL;
    char* utilityvm_source = NULL;
    int   status = -1;

    temp_root = __temp_directory_new("chef-windows-base-");
    if (temp_root == NULL) {
        fprintf(stderr, "mkwbase: failed to create temporary work directory\n");
        goto cleanup;
    }

    windowsfilter_dir = strpathcombine(temp_root, "windowsfilter");
    utilityvm_dir = strpathcombine(temp_root, "UtilityVM");
    if (windowsfilter_dir == NULL || utilityvm_dir == NULL) {
        goto cleanup;
    }

    if (platform_mkdir(windowsfilter_dir) != 0 || platform_mkdir(utilityvm_dir) != 0) {
        fprintf(stderr, "mkwbase: failed to prepare temporary directories\n");
        goto cleanup;
    }

    printf("Pulling %s\n", image);
    if (__docker_pull(image) != 0) {
        fprintf(stderr, "mkwbase: failed to pull %s\n", image);
        goto cleanup;
    }

    container_name = __temp_directory_new("chef-base-");
    if (container_name == NULL) {
        goto cleanup;
    }
    {
        const char* leaf = __leaf_name(container_name);
        char* tmp = platform_strdup(leaf);
        free(container_name);
        container_name = tmp;
    }
    if (container_name == NULL) {
        goto cleanup;
    }

    if (__docker_create(container_name, image) != 0) {
        fprintf(stderr, "mkwbase: failed to create container from %s\n", image);
        goto cleanup;
    }

    if (__docker_inspect_layer_path(container_name, &layer_path) != 0 || layer_path == NULL || layer_path[0] == '\0') {
        fprintf(stderr, "mkwbase: failed to resolve windowsfilter layer path for %s\n", image);
        goto cleanup;
    }

    if (__copy_directory_recursive(layer_path, windowsfilter_dir) != 0) {
        fprintf(stderr, "mkwbase: failed to copy windowsfilter contents from %s\n", layer_path);
        goto cleanup;
    }

    if (__copy_parent_layers(layer_path, windowsfilter_dir) != 0) {
        fprintf(stderr,
            "mkwbase: failed to rewrite parent layers from %s into %s\n",
            layer_path,
            windowsfilter_dir);
        goto cleanup;
    }

    utilityvm_source = __find_utilityvm_path(layer_path);
    if (utilityvm_source != NULL && __copy_directory_recursive(utilityvm_source, utilityvm_dir) != 0) {
        fprintf(stderr, "mkwbase: failed to copy UtilityVM contents from %s\n", utilityvm_source);
        goto cleanup;
    }

    status = __normalize_base_directory(base_name, temp_root, output_dir, force);

cleanup:
    if (container_name != NULL) {
        __docker_remove(container_name);
    }
    if (temp_root != NULL) {
        (void)platform_rmdir(temp_root);
    }
    free(temp_root);
    free(windowsfilter_dir);
    free(utilityvm_dir);
    free(container_name);
    free(layer_path);
    free(utilityvm_source);
    return status;
}

static int __parse_options(int argc, char** argv, struct mkwbase_options* options)
{
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "-b") || !strcmp(argv[i], "--base")) {
            if (++i >= argc) {
                return -1;
            }
            options->base = argv[i];
        } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--source")) {
            if (++i >= argc) {
                return -1;
            }
            options->source_dir = argv[i];
        } else if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--image")) {
            if (++i >= argc) {
                return -1;
            }
            options->image = argv[i];
        } else if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (++i >= argc) {
                return -1;
            }
            options->output_dir = argv[i];
        } else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--force")) {
            options->force = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 1;
        } else {
            fprintf(stderr, "mkwbase: unknown option %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    const char*            command;
    struct mkwbase_options options = { 0 };
    int                    result;

    if (argc < 2) {
        __print_help();
        return -1;
    }

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        __print_help();
        return 0;
    }
    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
        printf("mkwbase: version " PROJECT_VER "\n");
        return 0;
    }
    if (argv[1][0] == '-') {
        fprintf(stderr, "mkwbase: invalid global option %s\n", argv[1]);
        return -1;
    }

    command = argv[1];
    result = __parse_options(argc, argv, &options);
    if (result == 1) {
        return 0;
    }
    if (result != 0) {
        __print_help();
        return -1;
    }

    if (options.base == NULL || options.output_dir == NULL) {
        __print_help();
        return -1;
    }

    if (!strcmp(command, "normalize")) {
        if (options.source_dir == NULL) {
            __print_help();
            return -1;
        }
        return __normalize_base_directory(options.base, options.source_dir, options.output_dir, options.force);
    }

    if (!strcmp(command, "construct")) {
        if (options.image == NULL) {
            __print_help();
            return -1;
        }
        return __construct_base(options.base, options.image, options.output_dir, options.force);
    }

    fprintf(stderr, "mkwbase: unknown command %s\n", command);
    __print_help();
    return -1;
}