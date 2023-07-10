/**
 * Copyright 2023, Philip Meulengracht
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
#include "../packaging.h"
#include <chef/platform.h>
#include <stdlib.h>
#include <stdio.h>

static int __file_exists(const char* path)
{
    FILE* stream = fopen(path, "r");
    if (stream == NULL) {
        return 0;
    }
    fclose(stream);
    return 1;
}

static int __import_package(const char* packageDirectory, char** paths, struct packaging_import* import)
{
    char* targetPath;
    char  pcName[256];
    int   written;

    written = snprintf(&pcName[0], sizeof(pcName) - 1, "%s.pc", import->name);
    if (written == (sizeof(pcName) - 1)) {
        errno = E2BIG;
        return -1;
    }

    targetPath = strpathcombine(packageDirectory, &pcName[0]);
    if (targetPath == NULL) {
        return -1;
    }

    // if it exists avoid doing anything
    if (__file_exists(targetPath)) {
        free(targetPath);
        return 0;
    }

    for (int i = 0; paths[i]; i++) {
        int   status;
        char* path = strpathcombine(paths[i], &pcName[0]);
        if (path == NULL || targetPath == NULL) {
            free(path);
            free(targetPath);
            return -1;
        }
        if (!__file_exists(path)) {
            free(path);
            continue;
        }
        status = platform_symlink(targetPath, path, 0);
        free(path);
        free(targetPath);
        return status;
    }

    errno = ENOENT;
    return -1;
}

static int __add_environment_variable(struct list* environment, const char* key, const char* value)
{
    struct chef_keypair_item* item = calloc(sizeof(struct chef_keypair_item), 1);
    if (item == NULL) {
        return -1;
    }

    item->key = strdup(key);
    item->value = strdup(value);
    if (item->key == NULL || item->value == NULL) {
        free(item->key);
        free(item->value);
        return -1;
    }
    list_add(environment, item);
    return 0;
}

static int __setup_environment(const char* packageDirectory, struct list* environment)
{
    if (__add_environment_variable(environment, "PKG_CONFIG_PATH", packageDirectory)) {
        return -1;
    }

    if (__add_environment_variable(environment, "PKG_CONFIG_ALLOW_SYSTEM_LIBS", "0")) {
        return -1;
    }

    if (__add_environment_variable(environment, "PKG_CONFIG_ALLOW_SYSTEM_CFLAGS", "0")) {
        return -1;
    }
    return 0;
}

int packaging_load(struct packaging_params* params)
{
    char*             output;
    char**            paths;
    char*             package_path;
    int               status;
    struct list_item* i;

    package_path = strpathcombine(params->prep_path, "pkgconfig");
    if (package_path == NULL) {
        return -1;
    }

    if (platform_mkdir(package_path)) {
        free(package_path);
        return -1;
    }

    output = platform_exec("pkg-config --variable pc_path pkg-config");
    if (output == NULL) {
        free(package_path);
        return -1;
    }

    // parse the paths, and for each package defined we must find the *.pc file
    // for it in those paths.
    paths = strsplit(output, ':');
    free(output);
    if (paths == NULL) {
        free(package_path);
        return -1;
    }
    
    list_foreach(params->imports, i) {
        status = __import_package(package_path, paths, (struct packaging_import*)i);
        if (status) {
            fprintf(stderr, "packaging_load: failed to import host package %s", 
                ((struct packaging_import*)i)->name);
            break;
        }
    }

    if (!status) {
        status = __setup_environment(package_path, params->environment);
        if (status) {
            fprintf(stderr, "packaging_load: failed to setup custom packaging environment");
        }
    }

    strsplit_free(paths);
    free(package_path);
    return status;
}

int packaging_clear(const char* prep_path)
{
    char* package_path;
    int   status;

    package_path = strpathcombine(prep_path, "pkgconfig");
    if (package_path == NULL) {
        return -1;
    }
    
    status = platform_rmdir(package_path);
    free(package_path);
    return status;
}

int packaging_make_available(const char* prep_path, struct package_desc* package)
{
    FILE* file;
    char  pcName[256];
    char* package_path;
    char* pc_path;
    int   written;
    int   status;

    // The package name specified on the pkg-config command line is defined 
    // to be the name of the metadata file, minus the .pc extension. Optionally
    // the version can be appended as name-1.0
    written = snprintf(&pcName[0], sizeof(pcName) - 1, "%s.pc", package->package);
    if (written == (sizeof(pcName) - 1)) {
        errno = E2BIG;
        return -1;
    }
    
    package_path = strpathcombine(prep_path, "pkgconfig");
    if (package_path == NULL) {
        return -1;
    }

    pc_path = strpathcombine(package_path, &pcName[0]);
    free(package_path);
    if (pc_path == NULL) {
        return -1;
    }
    
    file = fopen(pc_path, "w");
    if(!file) {
        fprintf(stderr, "Failed to open %s for writing: %s\n", pc_path, strerror(errno));
        free(pc_path);
        return -1;
    }


}
