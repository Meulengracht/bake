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
#include <chef/dirs.h>
#include <chef/platform.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "commands.h"

static void __print_help(void)
{
    printf("Usage: cvctl uvm <command> [options]\n");
    printf("\n");
    printf("Commands:\n");
    printf("  show\n");
    printf("      Show the current LCOW UVM configuration and detected bundle files\n");
    printf("  import <bundle-dir>\n");
    printf("      Copy a local UVM bundle into Chef's cache and configure cvd to use it\n");
    printf("  fetch <zip-url>\n");
    printf("      Download a zipped UVM bundle into Chef's cache and configure cvd to use it\n");
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

static json_t* __ensure_lcow_object(json_t* root)
{
    json_t* lcow = json_object_get(root, "lcow");

    if (lcow != NULL) {
        return lcow;
    }

    lcow = json_object();
    if (lcow == NULL) {
        return NULL;
    }

    if (json_object_set_new(root, "lcow", lcow) != 0) {
        json_decref(lcow);
        return NULL;
    }
    return lcow;
}

static int __set_or_clear_string(json_t* object, const char* key, const char* value)
{
    if (value == NULL || value[0] == '\0') {
        json_object_del(object, key);
        return 0;
    }

    return json_object_set_new(object, key, json_string(value));
}

static int __save_bundle_configuration(
    const char* config_path,
    const char* image_path,
    const char* uvm_url,
    const char* kernel_file,
    const char* initrd_file,
    const char* boot_parameters)
{
    json_t* root;
    json_t* lcow;

    root = __load_root(config_path);
    if (root == NULL) {
        return -1;
    }

    lcow = __ensure_lcow_object(root);
    if (lcow == NULL) {
        json_decref(root);
        fprintf(stderr, "cvctl: failed to allocate lcow section\n");
        return -1;
    }

    if (__set_or_clear_string(lcow, "uvm-image-path", image_path) != 0 ||
        __set_or_clear_string(lcow, "uvm-url", uvm_url) != 0 ||
        __set_or_clear_string(lcow, "kernel-file", kernel_file) != 0 ||
        __set_or_clear_string(lcow, "initrd-file", initrd_file) != 0 ||
        __set_or_clear_string(lcow, "boot-parameters", boot_parameters) != 0) {
        json_decref(root);
        fprintf(stderr, "cvctl: failed to update lcow configuration\n");
        return -1;
    }

    if (json_dump_file(root, config_path, JSON_INDENT(2)) < 0) {
        json_decref(root);
        fprintf(stderr, "cvctl: failed to save configuration: %s\n", strerror(errno));
        return -1;
    }

    json_decref(root);
    return 0;
}

static int __config_path(char path[PATH_MAX])
{
    if (chef_dirs_initialize(CHEF_DIR_SCOPE_DAEMON) != 0) {
        fprintf(stderr, "cvctl: failed to initialize directory code\n");
        return -1;
    }

    snprintf(path, PATH_MAX, "%s" CHEF_PATH_SEPARATOR_S "cvd.json", chef_dirs_config());
    return 0;
}

static int __print_detected_bundle(const char* image_path)
{
    char* kernel = NULL;
    char* initrd = NULL;
    char* boot = NULL;

    if (image_path == NULL || image_path[0] == '\0') {
        printf("lcow.uvm-image-path = (null)\n");
        return 0;
    }

    printf("lcow.uvm-image-path = %s\n", image_path);
    if (containerv_disk_lcow_validate_uvm(image_path) != 0) {
        printf("lcow.bundle-valid = false\n");
        return 0;
    }

    printf("lcow.bundle-valid = true\n");
    if (containerv_disk_lcow_detect_uvm_files(image_path, &kernel, &initrd, &boot) != 0) {
        return -1;
    }

    printf("lcow.kernel-file = %s\n", kernel != NULL ? kernel : "(auto-none)");
    printf("lcow.initrd-file = %s\n", initrd != NULL ? initrd : "(auto-none)");
    printf("lcow.boot-parameters = %s\n", boot != NULL ? boot : "(auto-none)");
    free(kernel);
    free(initrd);
    free(boot);
    return 0;
}

static int __show_current(const char* config_path)
{
    json_t*      root;
    json_t*      lcow;
    const char*  image_path = NULL;
    const char*  uvm_url = NULL;

    root = __load_root(config_path);
    if (root == NULL) {
        return -1;
    }

    lcow = json_object_get(root, "lcow");
    if (lcow != NULL) {
        image_path = json_string_value(json_object_get(lcow, "uvm-image-path"));
        uvm_url = json_string_value(json_object_get(lcow, "uvm-url"));
        printf("lcow.uvm-url = %s\n", uvm_url != NULL ? uvm_url : "(null)");
    } else {
        printf("lcow.uvm-url = (null)\n");
    }

    if (__print_detected_bundle(image_path) != 0) {
        json_decref(root);
        fprintf(stderr, "cvctl: failed to inspect current bundle\n");
        return -1;
    }

    json_decref(root);
    return 0;
}

static int __configure_bundle(const char* config_path, const char* image_path, const char* uvm_url)
{
    char* kernel = NULL;
    char* initrd = NULL;
    char* boot = NULL;

    if (containerv_disk_lcow_detect_uvm_files(image_path, &kernel, &initrd, &boot) != 0) {
        fprintf(stderr, "cvctl: invalid LCOW UVM bundle at %s\n", image_path);
        return -1;
    }

    if (__save_bundle_configuration(config_path, image_path, uvm_url, kernel, initrd, boot) != 0) {
        free(kernel);
        free(initrd);
        free(boot);
        return -1;
    }

    printf("Configured LCOW UVM bundle:\n");
    printf("  image-path: %s\n", image_path);
    if (uvm_url != NULL) {
        printf("  source-url: %s\n", uvm_url);
    }
    printf("  kernel-file: %s\n", kernel != NULL ? kernel : "(auto-none)");
    printf("  initrd-file: %s\n", initrd != NULL ? initrd : "(auto-none)");
    printf("  boot-parameters: %s\n", boot != NULL ? boot : "(auto-none)");

    free(kernel);
    free(initrd);
    free(boot);
    return 0;
}

static int __handle_import(const char* config_path, const char* source_dir)
{
    char* image_path = NULL;
    int   status;

    status = containerv_disk_lcow_import_uvm(source_dir, &image_path);
    if (status != 0) {
        fprintf(stderr, "cvctl: failed to import LCOW UVM bundle from %s\n", source_dir);
        return -1;
    }

    status = __configure_bundle(config_path, image_path, NULL);
    free(image_path);
    return status;
}

static int __handle_fetch(const char* config_path, const char* url)
{
    struct containerv_disk_lcow_uvm_config cfg = { 0 };
    char*                                  image_path = NULL;
    int                                    status;

    cfg.uvm_url = url;
    status = containerv_disk_lcow_resolve_uvm(&cfg, &image_path);
    if (status != 0) {
        fprintf(stderr, "cvctl: failed to fetch LCOW UVM bundle from %s\n", url);
        return -1;
    }

    status = __configure_bundle(config_path, image_path, url);
    free(image_path);
    return status;
}

int uvm_main(int argc, char** argv, char** envp, struct cvctl_command_options* options)
{
    char config_path[PATH_MAX] = { 0 };

    (void)envp;
    (void)options;

    if (argc < 3 || !strcmp(argv[2], "-h") || !strcmp(argv[2], "--help")) {
        __print_help();
        return argc < 3 ? -1 : 0;
    }

    if (__config_path(config_path) != 0) {
        return -1;
    }

    if (!strcmp(argv[2], "show")) {
        return __show_current(config_path);
    }

    if (!strcmp(argv[2], "import")) {
        if (argc < 4) {
            fprintf(stderr, "cvctl: missing bundle directory\n");
            return -1;
        }
        return __handle_import(config_path, argv[3]);
    }

    if (!strcmp(argv[2], "fetch")) {
        if (argc < 4) {
            fprintf(stderr, "cvctl: missing UVM bundle URL\n");
            return -1;
        }
        return __handle_fetch(config_path, argv[3]);
    }

    fprintf(stderr, "cvctl: unknown uvm command '%s'\n", argv[2]);
    __print_help();
    return -1;
}