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
 * LCOW UVM asset retrieval and caching (Windows host).
 */

#include <chef/containerv/disk/lcow.h>
#include <chef/dirs.h>
#include <chef/platform.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vlog.h>

static uint64_t __fnv1a64(const char* s)
{
    uint64_t h = 1469598103934665603ULL;
    if (s == NULL) {
        return h;
    }
    for (const unsigned char* p = (const unsigned char*)s; *p; ++p) {
        h ^= (uint64_t)(*p);
        h *= 1099511628211ULL;
    }
    return h;
}

static int __path_exists(const char* path)
{
    struct platform_stat st;
    return (path && platform_stat(path, &st) == 0) ? 1 : 0;
}

static int __ensure_dir(const char* path)
{
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    return platform_mkdir(path);
}

static char* __ps_quote_single(const char* s)
{
    if (s == NULL) {
        return platform_strdup("''");
    }

    size_t in_len = strlen(s);
    size_t extra = 0;
    for (size_t i = 0; i < in_len; i++) {
        if (s[i] == '\'') {
            extra++;
        }
    }

    char* out = calloc(in_len + extra + 3, 1);
    if (out == NULL) {
        return NULL;
    }

    char* w = out;
    *w++ = '\'';
    for (size_t i = 0; i < in_len; i++) {
        if (s[i] == '\'') {
            *w++ = '\'';
            *w++ = '\'';
        } else {
            *w++ = s[i];
        }
    }
    *w++ = '\'';
    *w = '\0';
    return out;
}

static int __download_and_extract_zip(const char* url, const char* dest_dir, const char* zip_path)
{
    char* url_q = __ps_quote_single(url);
    char* dest_q = __ps_quote_single(dest_dir);
    char* zip_q = __ps_quote_single(zip_path);
    if (url_q == NULL || dest_q == NULL || zip_q == NULL) {
        free(url_q);
        free(dest_q);
        free(zip_q);
        return -1;
    }

    char script[8192];
    int rc = snprintf(
        script,
        sizeof(script),
        "$ProgressPreference='SilentlyContinue'; "
        "$url=%s; $zip=%s; $dest=%s; "
        "if (Test-Path $dest) { Remove-Item -Recurse -Force $dest }; "
        "New-Item -ItemType Directory -Path $dest | Out-Null; "
        "Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $zip; "
        "Expand-Archive -Path $zip -DestinationPath $dest -Force;",
        url_q,
        zip_q,
        dest_q
    );
    free(url_q);
    free(dest_q);
    free(zip_q);
    if (rc < 0 || (size_t)rc >= sizeof(script)) {
        return -1;
    }

    char args[9000];
    rc = snprintf(args, sizeof(args), "-NoProfile -NonInteractive -Command \"%s\"", script);
    if (rc < 0 || (size_t)rc >= sizeof(args)) {
        return -1;
    }

    return platform_spawn("powershell", args, NULL, &(struct platform_spawn_options) {0});
}

static int __write_marker(const char* marker)
{
    FILE* f = fopen(marker, "wb");
    if (f == NULL) {
        return -1;
    }
    fputs("ok", f);
    fclose(f);
    return 0;
}

int containerv_disk_lcow_resolve_uvm(
    const struct containerv_disk_lcow_uvm_config* config,
    char**                                       image_path_out)
{
    if (image_path_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *image_path_out = NULL;

    if (config == NULL || config->uvm_url == NULL || config->uvm_url[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    const char* cache_root = chef_dirs_cache();
    if (cache_root == NULL) {
        errno = ENOENT;
        return -1;
    }

    char* lcow_dir = strpathcombine(cache_root, "lcow");
    char* uvm_dir = lcow_dir ? strpathcombine(lcow_dir, "uvm") : NULL;
    if (lcow_dir == NULL || uvm_dir == NULL) {
        free(lcow_dir);
        free(uvm_dir);
        errno = ENOMEM;
        return -1;
    }

    if (__ensure_dir(lcow_dir) != 0 || __ensure_dir(uvm_dir) != 0) {
        free(lcow_dir);
        free(uvm_dir);
        return -1;
    }

    uint64_t h = __fnv1a64(config->uvm_url);
    char key[32];
    snprintf(key, sizeof(key), "%016llx", (unsigned long long)h);

    char* target_dir = strpathcombine(uvm_dir, key);
    if (target_dir == NULL) {
        free(lcow_dir);
        free(uvm_dir);
        errno = ENOMEM;
        return -1;
    }

    char* marker = strpathcombine(target_dir, "uvm.ready");
    char* zip_path = strpathcombine(uvm_dir, "uvm.zip");
    if (marker == NULL || zip_path == NULL) {
        free(lcow_dir);
        free(uvm_dir);
        free(target_dir);
        free(marker);
        free(zip_path);
        errno = ENOMEM;
        return -1;
    }

    if (!__path_exists(marker)) {
        VLOG_DEBUG("containerv[lcow]", "downloading LCOW UVM assets from %s\n", config->uvm_url);
        if (__download_and_extract_zip(config->uvm_url, target_dir, zip_path) != 0) {
            VLOG_ERROR("containerv[lcow]", "failed to download/extract LCOW UVM assets\n");
            free(lcow_dir);
            free(uvm_dir);
            free(target_dir);
            free(marker);
            free(zip_path);
            return -1;
        }
        (void)__write_marker(marker);
    }

    *image_path_out = target_dir;
    free(lcow_dir);
    free(uvm_dir);
    free(marker);
    free(zip_path);
    return 0;
}
