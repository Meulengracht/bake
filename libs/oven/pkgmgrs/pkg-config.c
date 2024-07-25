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

#include <chef/list.h>
#include <chef/platform.h>
#include <errno.h>
#include <libingredient.h>
#include <libpkgmgr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

// Files must be installed under the following dirs:
// /usr/share/pkgconfig/ (default, root=/)
// /chef/ingredients/%s/%s/pkgconfig/ (cc, root=/chef/ingredients/%s/%s/)

struct pkgconfig {
    struct pkgmngr base;
    char*          pcroot;
    char*          ccpcroot;
    char*          root;
    char*          ccroot;
    char*          target_platform;
    char*          target_architecture;
};

static const char* __get_root(struct pkgconfig* pkgconfig, struct ingredient* ingredient)
{
    if (strcmp(ingredient->package->platform, CHEF_PLATFORM_STR) ||
        strcmp(ingredient->package->arch, CHEF_ARCHITECTURE_STR)) {
        return pkgconfig->ccroot;
    }
    return pkgconfig->root;
}

static const char* __get_pcroot(struct pkgconfig* pkgconfig, struct ingredient* ingredient)
{
    if (strcmp(ingredient->package->platform, CHEF_PLATFORM_STR) ||
        strcmp(ingredient->package->arch, CHEF_ARCHITECTURE_STR)) {
        return pkgconfig->ccpcroot;
    }
    return pkgconfig->pcroot;
}

static const char* __get_pcroot2(struct pkgconfig* pkgconfig)
{
    if (strcmp(pkgconfig->target_platform, CHEF_PLATFORM_STR) ||
        strcmp(pkgconfig->target_architecture, CHEF_ARCHITECTURE_STR)) {
        return pkgconfig->ccpcroot;
    }
    return pkgconfig->pcroot;
}

static int __ensure_directories(struct pkgconfig* pkgconfig)
{
    int status;

    status = platform_mkdir(pkgconfig->pcroot);
    if (status && errno != EEXIST) {
        VLOG_ERROR("pkg-config", "failed to ensure that directory %s exists\n", pkgconfig->pcroot);
        return status;
    }

    status = platform_mkdir(pkgconfig->ccpcroot);
    if (status && errno != EEXIST) {
        VLOG_ERROR("pkg-config", "failed to ensure that directory %s exists\n", pkgconfig->ccpcroot);
        return status;
    }
    return 0;
}

static char* __string_array_join(const char* const* items, const char* prefix, const char* separator)
{
    char* buffer;
    
    if (items == NULL || *items == NULL) {
        return NULL;
    }

    buffer = calloc(4096, 1); 
    if (buffer == NULL) {
        return NULL;
    }

    for (int i = 0; items[i]; i++) {
        if (buffer[0] == 0) {
            strcpy(buffer, prefix);
        } else {
            strcat(buffer, separator);
            strcat(buffer, prefix);
        }
        strcat(buffer, items[i]);
    }
    return buffer;
}

static int __make_available(struct pkgmngr* pkgmngr, struct ingredient* ingredient)
{
    struct pkgconfig* pkgconfig = (struct pkgconfig*)pkgmngr;
    FILE* file;
    char  pcName[256];
    char* pcPath;
    int   written;
    int   status = 0;
    char* cflags;
    char* libs;

    if (ingredient->options == NULL) {
        // Can't add a pkg-config file if the ingredient didn't specify any
        // options for consumers.
        return 0;
    }

    // Ensure pkg-config directories exist. We cannot do it earlier (i.e)
    // in _new as that is before the environment is setup, and thus they will
    // be deleted again.
    status = __ensure_directories(pkgconfig);
    if (status) {
        return status;
    }

    // The package name specified on the pkg-config command line is defined 
    // to be the name of the metadata file, minus the .pc extension. Optionally
    // the version can be appended as name-1.0
    written = snprintf(&pcName[0], sizeof(pcName) - 1, "%s.pc", ingredient->package->package);
    if (written == (sizeof(pcName) - 1)) {
        errno = E2BIG;
        return -1;
    }
    
    pcPath = strpathcombine(__get_pcroot(pkgconfig, ingredient), &pcName[0]);
    if (pcPath == NULL) {
        return -1;
    }
    
    file = fopen(pcPath, "w");
    if(!file) {
        VLOG_ERROR("pkg-config", "__make_available: failed to open %s for writing: %s\n", pcPath, strerror(errno));
        free(pcPath);
        return -1;
    }

    cflags = __string_array_join((const char* const*)ingredient->options->inc_dirs, "-I{prefix}", " ");
    libs   = __string_array_join((const char* const*)ingredient->options->lib_dirs, "-L{prefix}", " ");
    if (cflags == NULL && libs == NULL) {
        goto cleanup;
    }

    fprintf(file, "# generated by chef, please do not manually modify this\n");
    fprintf(file, "prefix=%s\n\n", __get_root(pkgconfig, ingredient));

    fprintf(file, "Name: %s\n", ingredient->package->package);
    fprintf(file, "Description: %s by %s\n", ingredient->package->package, ingredient->package->maintainer);
    fprintf(file, "Version: %i.%i.%i\n", ingredient->version->major, ingredient->version->minor, ingredient->version->patch);
    if (cflags != NULL) {
        fprintf(file, "Cflags: %s\n", cflags);
    }
    if (libs != NULL) {
        fprintf(file, "Libs: %s\n", libs);
    }

cleanup:
    free(pcPath);
    free(cflags);
    free(libs);
    fclose(file);
    return status;
}

static char* __compose_keypair(const char* key, const char* value)
{
    size_t size = strlen(key) + strlen(value) + 2;
    char*  envItem = calloc(size, 1);
    if (envItem == NULL) {
        return NULL;
    }
    snprintf(&envItem[0], size, "%s=%s", key, value);
    return envItem;
}

static int __add_pkgconfig_paths(struct pkgmngr* pkgmngr, char** environment)
{
    struct pkgconfig* pkgconfig = (struct pkgconfig*)pkgmngr;
    int               index = 0;
    struct {
        const char* ident;
    } idents[] = {
        { "PKG_CONFIG_PATH" },
        { "PKG_CONFIG_LIBDIR" },
        { NULL }
    };

    // scroll to end
    while (environment[index]) index++;

    // add variables
    for (int i = 0; idents[i].ident != NULL; i++) {
        environment[index++] = __compose_keypair(idents[i].ident, __get_pcroot2(pkgconfig));
    }
    return 0;
}

static void __destroy(struct pkgmngr* pkgmngr)
{
    struct pkgconfig* pkgconfig = (struct pkgconfig*)pkgmngr;
    if (pkgconfig == NULL) {
        return;
    }
    free(pkgconfig->root);
    free(pkgconfig->ccroot);
    free(pkgconfig->pcroot);
    free(pkgconfig->ccpcroot);
    free(pkgconfig->target_platform);
    free(pkgconfig->target_architecture);
    free(pkgconfig);
}

struct pkgmngr* pkgmngr_pkgconfig_new(struct pkgmngr_options* options)
{
    struct pkgconfig* pkgconfig;
    char              tmp[2048];
    VLOG_DEBUG("pkg-config", "pkgmngr_pkgconfig_new(root=%s)\n", options->root);

    pkgconfig = malloc(sizeof(struct pkgconfig));
    if (pkgconfig == NULL) {
        return NULL;
    }

    pkgconfig->base.make_available = __make_available;
    pkgconfig->base.add_overrides  = __add_pkgconfig_paths;
    pkgconfig->base.destroy        = __destroy;

    // build roots
    pkgconfig->root = platform_strdup("/");
    snprintf(&tmp[0], sizeof(tmp), "/chef/ingredients/%s/%s/", options->target_platform, options->target_architecture);
    pkgconfig->ccroot = platform_strdup(&tmp[0]);

    // build pcroots
    snprintf(&tmp[0], sizeof(tmp), "%s/usr/share/pkgconfig/", options->root);
    pkgconfig->pcroot = platform_strdup(&tmp[0]);
    snprintf(&tmp[0], sizeof(tmp), "%s/chef/ingredients/%s/%s/pkgconfig/", options->root, options->target_platform, options->target_architecture);
    pkgconfig->ccpcroot = platform_strdup(&tmp[0]);

    // copy system info
    pkgconfig->target_platform = platform_strdup(options->target_platform);
    pkgconfig->target_architecture = platform_strdup(options->target_architecture);

    return &pkgconfig->base;
}
