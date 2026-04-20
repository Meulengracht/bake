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
#include <chef/containerv/disk/lcow.h>
#include <chef/platform.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <vlog.h>
#include "chef-config.h"

struct mkuvm_options {
    const char* output_dir;
    const char* source_dir;
    const char* source_url;
    const char* archive_path;
    const char* working_directory;
    const char* hcsshim_dir;
    const char* linuxkit_bin;
    const char* bash_bin;
    const char* architecture;
    const char* boot_parameters;
    int         force;
};

struct bundle_source_info {
    const char* mode;
    const char* url;
    const char* source_dir;
    const char* hcsshim;
    const char* linuxkit;
};

static void __print_help(void)
{
    printf("Usage: mkuvm [global-options] <command> [command-options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  normalize\n");
    printf("      Normalize a raw LCOW UVM bundle directory\n");
    printf("  fetch\n");
    printf("      Download a zipped LCOW UVM bundle and normalize it\n");
    printf("  archive\n");
    printf("      Archive a normalized LCOW UVM bundle\n");
    printf("  construct\n");
    printf("      Build an LCOW UVM bundle using hcsshim + LinuxKit\n");
    printf("\n");
    printf("Global Options:\n");
    printf("  -v, --version\n");
    printf("      Print the version of mkuvm\n");
    printf("\n");
    printf("Command Options:\n");
    printf("  -o, --output <dir>\n");
    printf("      Output directory for normalize, fetch, and construct\n");
    printf("  -s, --source <dir>\n");
    printf("      Source directory for normalize and archive\n");
    printf("  -u, --url <url>\n");
    printf("      Bundle archive URL for fetch\n");
    printf("  -a, --archive <path>\n");
    printf("      Optional archive output path, or required for archive command\n");
    printf("  -w, --working-directory <dir>\n");
    printf("      Working directory for construct\n");
    printf("  --hcsshim-dir <dir>\n");
    printf("      Existing hcsshim checkout for construct\n");
    printf("  --linuxkit-bin <path>\n");
    printf("      LinuxKit executable, default is linuxkit\n");
    printf("  --bash-bin <path>\n");
    printf("      Bash executable, default is bash\n");
    printf("  --arch <arch>\n");
    printf("      Bundle architecture, default is arm64\n");
    printf("  -p, --boot-parameters <text>\n");
    printf("      Override boot parameters written into the normalized bundle\n");
    printf("  -f, --force\n");
    printf("      Replace the output directory or archive if it already exists\n");
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

static int __run_command(const char* program, const char* arguments)
{
    return platform_spawn(program, arguments, NULL, &(struct platform_spawn_options) {
        .output_handler = __spawn_output_handler
    });
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

static int __prepare_output_directory(const char* path, int force)
{
    if (__path_exists(path)) {
        if (!force) {
            fprintf(stderr, "mkuvm: output path already exists: %s\n", path);
            return -1;
        }
        (void)platform_rmdir(path);
    }

    if (platform_mkdir(path) != 0) {
        fprintf(stderr, "mkuvm: failed to create output directory %s\n", path);
        return -1;
    }
    return 0;
}

static int __prepare_archive_path(const char* path, int force)
{
    if (__path_exists(path)) {
        if (!force) {
            fprintf(stderr, "mkuvm: archive already exists: %s\n", path);
            return -1;
        }
        if (platform_unlink(path) != 0) {
            return -1;
        }
    }
    return 0;
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

static int __read_text_file(const char* path, char** text_out)
{
    void*  buffer = NULL;
    size_t length = 0;
    char*  text;
    char*  start;
    char*  end;

    if (text_out == NULL) {
        return -1;
    }
    *text_out = NULL;

    if (platform_readfile(path, &buffer, &length) != 0) {
        return -1;
    }

    text = calloc(length + 1, 1);
    if (text == NULL) {
        free(buffer);
        return -1;
    }

    if (length != 0) {
        memcpy(text, buffer, length);
    }
    free(buffer);

    start = text;
    while (*start != '\0' && (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n')) {
        start++;
    }

    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n')) {
        end--;
    }
    *end = '\0';

    if (start != text) {
        memmove(text, start, (size_t)(end - start) + 1);
    }

    *text_out = text;
    return 0;
}

static int __matches_exact(const char* name, const char* const* exact_names)
{
    if (exact_names == NULL) {
        return 0;
    }

    for (int i = 0; exact_names[i] != NULL; ++i) {
        if (_stricmp(name, exact_names[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

static int __matches_prefix(const char* name, const char* const* prefixes)
{
    if (prefixes == NULL) {
        return 0;
    }

    for (int i = 0; prefixes[i] != NULL; ++i) {
        size_t length = strlen(prefixes[i]);
        if (_strnicmp(name, prefixes[i], length) == 0) {
            return 1;
        }
    }
    return 0;
}

static int __matches_suffix(const char* name, const char* suffix)
{
    size_t name_length;
    size_t suffix_length;

    if (suffix == NULL) {
        return 0;
    }

    name_length = strlen(name);
    suffix_length = strlen(suffix);
    if (name_length < suffix_length) {
        return 0;
    }
    return _stricmp(name + name_length - suffix_length, suffix) == 0;
}

static char* __find_first_file(const char* root, const char* const* exact_names, const char* const* prefixes, const char* suffix)
{
    struct list files;
    struct list_item* item;
    char* result = NULL;

    if (!__path_is_directory(root)) {
        return NULL;
    }

    list_init(&files);
    if (platform_getfiles(root, 1, &files) != 0) {
        return NULL;
    }

    list_foreach(&files, item) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;

        if (entry->type != PLATFORM_FILETYPE_FILE) {
            continue;
        }

        if (__matches_exact(entry->name, exact_names) ||
            __matches_prefix(entry->name, prefixes) ||
            __matches_suffix(entry->name, suffix)) {
            result = platform_strdup(entry->path);
            break;
        }
    }

    platform_getfiles_destroy(&files);
    return result;
}

static int __copy_canonical_file(const char* source_path, const char* target_dir, const char* target_name)
{
    char* destination;
    int   status;

    destination = strpathcombine(target_dir, target_name);
    if (destination == NULL) {
        return -1;
    }

    status = platform_copyfile(source_path, destination);
    free(destination);
    return status;
}

static int __archive_bundle(const char* source_dir, const char* archive_path, int force)
{
    char   arguments[4096] = { 0 };
    size_t index = 0;

    if (__prepare_archive_path(archive_path, force) != 0) {
        return -1;
    }

    if (__append_token(arguments, sizeof(arguments), &index, "-a") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "-cf") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, archive_path) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "-C") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, source_dir) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, ".") != 0) {
        return -1;
    }

    return __run_command("tar", arguments);
}

static int __download_file(const char* url, const char* destination)
{
    char   arguments[4096] = { 0 };
    size_t index = 0;

    if (__prepare_archive_path(destination, 1) != 0) {
        return -1;
    }

    if (__append_token(arguments, sizeof(arguments), &index, "-L") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--fail") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--output") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, destination) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, url) != 0) {
        return -1;
    }

    return __run_command("curl", arguments);
}

static int __extract_archive(const char* archive_path, const char* destination)
{
    char   arguments[4096] = { 0 };
    size_t index = 0;

    if (__prepare_output_directory(destination, 1) != 0) {
        return -1;
    }

    if (__append_token(arguments, sizeof(arguments), &index, "-xf") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, archive_path) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "-C") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, destination) != 0) {
        return -1;
    }

    return __run_command("tar", arguments);
}

static int __write_boot_parameters(const char* target_dir, const char* boot_parameters)
{
    char* path;
    FILE* stream;

    path = strpathcombine(target_dir, "boot_parameters");
    if (path == NULL) {
        return -1;
    }

    stream = fopen(path, "wb");
    free(path);
    if (stream == NULL) {
        return -1;
    }

    fputs(boot_parameters, stream);
    return fclose(stream);
}

static int __write_bundle_manifest(
    const char*                    target_dir,
    const char*                    architecture,
    const struct bundle_source_info* source_info,
    int                            has_kernel,
    int                            has_initrd,
    int                            has_boot_parameters)
{
    char*   manifest_path;
    json_t* root = NULL;
    json_t* source = NULL;
    json_t* files = NULL;
    int     status = -1;

    manifest_path = strpathcombine(target_dir, "bundle.json");
    if (manifest_path == NULL) {
        return -1;
    }

    root = json_object();
    source = json_object();
    files = json_object();
    if (root == NULL || source == NULL || files == NULL) {
        goto cleanup;
    }

    if (source_info != NULL) {
        if (source_info->mode != NULL && json_object_set_new(source, "mode", json_string(source_info->mode)) != 0) {
            goto cleanup;
        }
        if (source_info->url != NULL && json_object_set_new(source, "url", json_string(source_info->url)) != 0) {
            goto cleanup;
        }
        if (source_info->source_dir != NULL && json_object_set_new(source, "source_dir", json_string(source_info->source_dir)) != 0) {
            goto cleanup;
        }
        if (source_info->hcsshim != NULL && json_object_set_new(source, "hcsshim", json_string(source_info->hcsshim)) != 0) {
            goto cleanup;
        }
        if (source_info->linuxkit != NULL && json_object_set_new(source, "linuxkit", json_string(source_info->linuxkit)) != 0) {
            goto cleanup;
        }
    }

    if (json_object_set_new(root, "kind", json_string("lcow-uvm")) != 0 ||
        json_object_set_new(root, "format_version", json_integer(1)) != 0 ||
        json_object_set_new(root, "architecture", json_string(architecture)) != 0 ||
        json_object_set_new(files, "uvm_image", json_string("uvm.vhdx")) != 0 ||
        json_object_set_new(files, "kernel", has_kernel ? json_string("kernel") : json_null()) != 0 ||
        json_object_set_new(files, "initrd", has_initrd ? json_string("initrd") : json_null()) != 0 ||
        json_object_set_new(files, "boot_parameters", has_boot_parameters ? json_string("boot_parameters") : json_null()) != 0 ||
        json_object_set_new(root, "source", source) != 0 ||
        json_object_set_new(root, "files", files) != 0) {
        goto cleanup;
    }

    source = NULL;
    files = NULL;
    status = json_dump_file(root, manifest_path, JSON_INDENT(2)) == 0 ? 0 : -1;

cleanup:
    free(manifest_path);
    if (root != NULL) {
        json_decref(root);
    }
    if (source != NULL) {
        json_decref(source);
    }
    if (files != NULL) {
        json_decref(files);
    }
    return status;
}

static int __normalize_bundle(
    const char*                      source_dir,
    const char*                      output_dir,
    const char*                      architecture,
    const char*                      boot_parameters_override,
    const struct bundle_source_info* source_info,
    const char*                      archive_path,
    int                              force)
{
    static const char* const uvm_exact[] = { "uvm.vhdx", NULL };
    static const char* const kernel_exact[] = { "kernel", NULL };
    static const char* const kernel_prefix[] = { "kernel", NULL };
    static const char* const initrd_exact[] = { "initrd", "initrd.img", NULL };
    static const char* const initrd_prefix[] = { "initrd", NULL };
    static const char* const boot_exact[] = { "boot_parameters", NULL };
    static const char* const boot_prefix[] = { "boot", NULL };
    char* uvm_file = NULL;
    char* kernel_file = NULL;
    char* initrd_file = NULL;
    char* boot_file = NULL;
    char* boot_text = NULL;
    int   has_kernel = 0;
    int   has_initrd = 0;
    int   has_boot_parameters = 0;
    int   status = -1;

    if (!__path_is_directory(source_dir)) {
        fprintf(stderr, "mkuvm: source directory not found: %s\n", source_dir);
        return -1;
    }

    if (__prepare_output_directory(output_dir, force) != 0) {
        return -1;
    }

    uvm_file = __find_first_file(source_dir, uvm_exact, NULL, NULL);
    if (uvm_file == NULL) {
        uvm_file = __find_first_file(source_dir, NULL, NULL, ".vhdx");
    }
    if (uvm_file == NULL) {
        fprintf(stderr, "mkuvm: no uvm.vhdx or VHDX image found under %s\n", source_dir);
        goto cleanup;
    }

    if (__copy_canonical_file(uvm_file, output_dir, "uvm.vhdx") != 0) {
        fprintf(stderr, "mkuvm: failed to copy %s\n", uvm_file);
        goto cleanup;
    }

    kernel_file = __find_first_file(source_dir, kernel_exact, kernel_prefix, NULL);
    if (kernel_file != NULL) {
        if (__copy_canonical_file(kernel_file, output_dir, "kernel") != 0) {
            goto cleanup;
        }
        has_kernel = 1;
    }

    initrd_file = __find_first_file(source_dir, initrd_exact, initrd_prefix, NULL);
    if (initrd_file != NULL) {
        if (__copy_canonical_file(initrd_file, output_dir, "initrd") != 0) {
            goto cleanup;
        }
        has_initrd = 1;
    }

    if (boot_parameters_override != NULL && boot_parameters_override[0] != '\0') {
        boot_text = platform_strdup(boot_parameters_override);
    } else {
        boot_file = __find_first_file(source_dir, boot_exact, boot_prefix, NULL);
        if (boot_file != NULL && __read_text_file(boot_file, &boot_text) != 0) {
            goto cleanup;
        }
    }

    if (boot_text != NULL && boot_text[0] != '\0') {
        if (__write_boot_parameters(output_dir, boot_text) != 0) {
            goto cleanup;
        }
        has_boot_parameters = 1;
    }

    if (__write_bundle_manifest(output_dir, architecture, source_info, has_kernel, has_initrd, has_boot_parameters) != 0) {
        fprintf(stderr, "mkuvm: failed to write bundle manifest\n");
        goto cleanup;
    }

    if (containerv_disk_lcow_validate_uvm(output_dir) != 0) {
        fprintf(stderr, "mkuvm: normalized bundle is invalid at %s\n", output_dir);
        goto cleanup;
    }

    if (archive_path != NULL && archive_path[0] != '\0') {
        if (__archive_bundle(output_dir, archive_path, force) != 0) {
            fprintf(stderr, "mkuvm: failed to archive bundle to %s\n", archive_path);
            goto cleanup;
        }
    }

    printf("LCOW bundle prepared at %s\n", output_dir);
    status = 0;

cleanup:
    free(uvm_file);
    free(kernel_file);
    free(initrd_file);
    free(boot_file);
    free(boot_text);
    return status;
}

static int __fetch_bundle(const struct mkuvm_options* options)
{
    struct bundle_source_info source_info = {
        .mode = "fetch",
        .url = options->source_url
    };
    char* temp_root = NULL;
    char* zip_path = NULL;
    char* expanded_dir = NULL;
    int   status = -1;

    temp_root = __temp_directory_new("chef-lcow-uvm-fetch-");
    if (temp_root == NULL) {
        return -1;
    }

    zip_path = strpathcombine(temp_root, "bundle.zip");
    expanded_dir = strpathcombine(temp_root, "expanded");
    if (zip_path == NULL || expanded_dir == NULL) {
        goto cleanup;
    }

    printf("Downloading %s\n", options->source_url);
    if (__download_file(options->source_url, zip_path) != 0) {
        fprintf(stderr, "mkuvm: failed to download %s\n", options->source_url);
        goto cleanup;
    }

    if (__extract_archive(zip_path, expanded_dir) != 0) {
        fprintf(stderr, "mkuvm: failed to extract %s\n", zip_path);
        goto cleanup;
    }

    status = __normalize_bundle(
        expanded_dir,
        options->output_dir,
        options->architecture,
        options->boot_parameters,
        &source_info,
        options->archive_path,
        options->force);

cleanup:
    if (temp_root != NULL) {
        (void)platform_rmdir(temp_root);
    }
    free(temp_root);
    free(zip_path);
    free(expanded_dir);
    return status;
}

static int __construct_bundle(const struct mkuvm_options* options)
{
    struct bundle_source_info source_info = {
        .mode = "construct"
    };
    char* temp_root = NULL;
    char* working_dir = NULL;
    char* hcsshim_dir = NULL;
    char* raw_output = NULL;
    char* build_script = NULL;
    int   owns_working_dir = 0;
    int   status = -1;
    char  arguments[4096] = { 0 };
    size_t index = 0;

    if (options->working_directory != NULL) {
        working_dir = platform_abspath(options->working_directory);
        if (working_dir == NULL) {
            fprintf(stderr, "mkuvm: invalid working directory %s\n", options->working_directory);
            goto cleanup;
        }
        if (platform_mkdir(working_dir) != 0) {
            fprintf(stderr, "mkuvm: failed to create working directory %s\n", working_dir);
            goto cleanup;
        }
    } else {
        temp_root = __temp_directory_new("chef-lcow-uvm-");
        if (temp_root == NULL) {
            goto cleanup;
        }
        working_dir = platform_strdup(temp_root);
        if (working_dir == NULL) {
            goto cleanup;
        }
        owns_working_dir = 1;
    }

    if (options->hcsshim_dir != NULL) {
        hcsshim_dir = platform_abspath(options->hcsshim_dir);
    } else {
        hcsshim_dir = strpathcombine(working_dir, "hcsshim");
    }
    if (hcsshim_dir == NULL) {
        goto cleanup;
    }

    if (!__path_is_directory(hcsshim_dir)) {
        memset(arguments, 0, sizeof(arguments));
        index = 0;
        if (__append_token(arguments, sizeof(arguments), &index, "clone") != 0 ||
            __append_token(arguments, sizeof(arguments), &index, "--depth") != 0 ||
            __append_token(arguments, sizeof(arguments), &index, "1") != 0 ||
            __append_token(arguments, sizeof(arguments), &index, "https://github.com/microsoft/hcsshim.git") != 0 ||
            __append_token(arguments, sizeof(arguments), &index, hcsshim_dir) != 0) {
            goto cleanup;
        }

        printf("Cloning hcsshim into %s\n", hcsshim_dir);
        if (__run_command("git", arguments) != 0) {
            fprintf(stderr, "mkuvm: failed to clone hcsshim\n");
            goto cleanup;
        }
    }

    build_script = strpathjoin(hcsshim_dir, "scripts", "build-lcow-uvm.sh", NULL);
    raw_output = strpathcombine(working_dir, "output");
    if (build_script == NULL || raw_output == NULL) {
        goto cleanup;
    }

    if (!__path_exists(build_script)) {
        fprintf(stderr, "mkuvm: expected build script not found: %s\n", build_script);
        goto cleanup;
    }

    if (__prepare_output_directory(raw_output, 1) != 0) {
        goto cleanup;
    }

    memset(arguments, 0, sizeof(arguments));
    index = 0;
    if (__append_token(arguments, sizeof(arguments), &index, build_script) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--linuxkit") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, options->linuxkit_bin) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--output") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, raw_output) != 0 ||
        __append_token(arguments, sizeof(arguments), &index, "--arch") != 0 ||
        __append_token(arguments, sizeof(arguments), &index, options->architecture) != 0) {
        goto cleanup;
    }

    printf("Building LCOW UVM bundle into %s\n", raw_output);
    if (__run_command(options->bash_bin, arguments) != 0) {
        fprintf(stderr, "mkuvm: LCOW UVM build failed\n");
        goto cleanup;
    }

    source_info.hcsshim = hcsshim_dir;
    source_info.linuxkit = options->linuxkit_bin;
    status = __normalize_bundle(
        raw_output,
        options->output_dir,
        options->architecture,
        options->boot_parameters,
        &source_info,
        options->archive_path,
        options->force);

cleanup:
    if (owns_working_dir && temp_root != NULL) {
        (void)platform_rmdir(temp_root);
    }
    free(temp_root);
    free(working_dir);
    free(hcsshim_dir);
    free(raw_output);
    free(build_script);
    return status;
}

static int __parse_options(int argc, char** argv, struct mkuvm_options* options)
{
    options->linuxkit_bin = "linuxkit";
    options->bash_bin = "bash";
    options->architecture = "arm64";

    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "-o") || !strcmp(argv[i], "--output")) {
            if (++i >= argc) {
                return -1;
            }
            options->output_dir = argv[i];
        } else if (!strcmp(argv[i], "-s") || !strcmp(argv[i], "--source")) {
            if (++i >= argc) {
                return -1;
            }
            options->source_dir = argv[i];
        } else if (!strcmp(argv[i], "-u") || !strcmp(argv[i], "--url")) {
            if (++i >= argc) {
                return -1;
            }
            options->source_url = argv[i];
        } else if (!strcmp(argv[i], "-a") || !strcmp(argv[i], "--archive")) {
            if (++i >= argc) {
                return -1;
            }
            options->archive_path = argv[i];
        } else if (!strcmp(argv[i], "-w") || !strcmp(argv[i], "--working-directory")) {
            if (++i >= argc) {
                return -1;
            }
            options->working_directory = argv[i];
        } else if (!strcmp(argv[i], "--hcsshim-dir")) {
            if (++i >= argc) {
                return -1;
            }
            options->hcsshim_dir = argv[i];
        } else if (!strcmp(argv[i], "--linuxkit-bin")) {
            if (++i >= argc) {
                return -1;
            }
            options->linuxkit_bin = argv[i];
        } else if (!strcmp(argv[i], "--bash-bin")) {
            if (++i >= argc) {
                return -1;
            }
            options->bash_bin = argv[i];
        } else if (!strcmp(argv[i], "--arch")) {
            if (++i >= argc) {
                return -1;
            }
            options->architecture = argv[i];
        } else if (!strcmp(argv[i], "-p") || !strcmp(argv[i], "--boot-parameters")) {
            if (++i >= argc) {
                return -1;
            }
            options->boot_parameters = argv[i];
        } else if (!strcmp(argv[i], "-f") || !strcmp(argv[i], "--force")) {
            options->force = 1;
        } else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            __print_help();
            return 1;
        } else {
            fprintf(stderr, "mkuvm: unknown option %s\n", argv[i]);
            return -1;
        }
    }
    return 0;
}

int main(int argc, char** argv)
{
    const char*               command;
    struct mkuvm_options      options = { 0 };
    struct bundle_source_info source_info = { 0 };
    int                       result;

    if (argc < 2) {
        __print_help();
        return -1;
    }

    if (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")) {
        __print_help();
        return 0;
    }
    if (!strcmp(argv[1], "-v") || !strcmp(argv[1], "--version")) {
        printf("mkuvm: version " PROJECT_VER "\n");
        return 0;
    }
    if (argv[1][0] == '-') {
        fprintf(stderr, "mkuvm: invalid global option %s\n", argv[1]);
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

    if (!strcmp(command, "normalize")) {
        if (options.source_dir == NULL || options.output_dir == NULL) {
            __print_help();
            return -1;
        }

        source_info.mode = "normalize";
        source_info.source_dir = options.source_dir;
        return __normalize_bundle(
            options.source_dir,
            options.output_dir,
            options.architecture,
            options.boot_parameters,
            &source_info,
            options.archive_path,
            options.force);
    }

    if (!strcmp(command, "fetch")) {
        if (options.source_url == NULL || options.output_dir == NULL) {
            __print_help();
            return -1;
        }
        return __fetch_bundle(&options);
    }

    if (!strcmp(command, "archive")) {
        if (options.source_dir == NULL || options.archive_path == NULL) {
            __print_help();
            return -1;
        }
        return __archive_bundle(options.source_dir, options.archive_path, options.force);
    }

    if (!strcmp(command, "construct")) {
        if (options.output_dir == NULL) {
            __print_help();
            return -1;
        }
        return __construct_bundle(&options);
    }

    fprintf(stderr, "mkuvm: unknown command %s\n", command);
    __print_help();
    return -1;
}