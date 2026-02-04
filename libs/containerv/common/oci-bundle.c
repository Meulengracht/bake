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
#include <ctype.h>
#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "standard-mounts.h"
#include "oci-bundle.h"

static int __normalize_linux_relpath_alloc(const char* path, char** rel_out)
{
    if (rel_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    *rel_out = NULL;

    if (path == NULL || path[0] == '\0') {
        *rel_out = platform_strdup("");
        return (*rel_out != NULL) ? 0 : -1;
    }

    size_t n = strlen(path);
    char* tmp = calloc(n + 1, 1);
    if (tmp == NULL) {
        errno = ENOMEM;
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        tmp[i] = (path[i] == '\\') ? '/' : path[i];
    }
    tmp[n] = '\0';

    const char* p = tmp;
    while (*p == '/') {
        p++;
    }

    char* out = calloc(n + 1, 1);
    if (out == NULL) {
        free(tmp);
        errno = ENOMEM;
        return -1;
    }

    size_t out_len = 0;
    while (*p != '\0') {
        const char* seg = p;
        size_t seg_len = 0;
        while (p[seg_len] != '\0' && p[seg_len] != '/') {
            seg_len++;
        }

        if (seg_len == 0) {
            p++;
            continue;
        }

        if (seg_len == 1 && seg[0] == '.') {
            // Skip "."
        } else if (seg_len == 2 && seg[0] == '.' && seg[1] == '.') {
            free(tmp);
            free(out);
            errno = EINVAL;
            return -1;
        } else {
            if (out_len > 0) {
                out[out_len++] = '/';
            }
            memcpy(out + out_len, seg, seg_len);
            out_len += seg_len;
            out[out_len] = '\0';
        }

        p += seg_len;
        while (*p == '/') {
            p++;
        }
    }

    free(tmp);
    *rel_out = out;
    return 0;
}

static int __write_resolv_conf(const char* path, const char* dns_servers)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if (dns_servers == NULL || dns_servers[0] == '\0') {
        // No DNS settings; create an empty file if it doesn't exist.
        return platform_writetextfile(path, "");
    }

    char* copy = platform_strdup(dns_servers);
    if (copy == NULL) {
        errno = ENOMEM;
        return -1;
    }

    size_t out_cap = strlen(dns_servers) * 2 + 64;
    char* out = calloc(out_cap, 1);
    if (out == NULL) {
        free(copy);
        errno = ENOMEM;
        return -1;
    }

    size_t out_len = 0;
    char* token = copy;
    for (char* p = copy; ; ++p) {
        if (*p == ';' || *p == ',' || *p == ' ' || *p == '\t' || *p == '\0') {
            char saved = *p;
            *p = '\0';

            if (token[0] != '\0') {
                size_t need = strlen(token) + 12;
                if (out_len + need + 1 > out_cap) {
                    out_cap = (out_len + need + 64) * 2;
                    char* tmp = realloc(out, out_cap);
                    if (tmp == NULL) {
                        free(out);
                        free(copy);
                        errno = ENOMEM;
                        return -1;
                    }
                    out = tmp;
                }
                out_len += (size_t)snprintf(out + out_len, out_cap - out_len, "nameserver %s\n", token);
            }

            if (saved == '\0') {
                break;
            }
            token = p + 1;
        }
    }

    int rc = platform_writetextfile(path, out);
    free(out);
    free(copy);
    return rc;
}

static int __ensure_parent_dirs(const char* root, const char* sub_path)
{
    if (!root || !sub_path) {
        errno = EINVAL;
        return -1;
    }

    char* full = strpathcombine(root, sub_path);
    if (full == NULL) {
        errno = ENOMEM;
        return -1;
    }

    // Create each directory prefix up to the final path segment.
    size_t root_len = strlen(root);
    for (char* p = full + root_len; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            char saved = *p;
            *p = '\0';
            (void)platform_mkdir(full);
            *p = saved;
        }
    }

    free(full);
    return 0;
}

static int __copytree_best_effort(const char* source_root, const char* dest_root)
{
    struct list_item* item;
    struct list       files;

    if (!source_root || !dest_root) {
        errno = EINVAL;
        return -1;
    }

    list_init(&files);
    if (platform_getfiles(source_root, 1, &files) != 0) {
        // platform_getfiles sets errno.
        platform_getfiles_destroy(&files);
        return -1;
    }

    for (item = files.head; item != NULL; item = item->next) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        if (!entry->sub_path || entry->sub_path[0] == '\0') {
            continue;
        }

        if (__ensure_parent_dirs(dest_root, entry->sub_path) != 0) {
            platform_getfiles_destroy(&files);
            return -1;
        }

        char* dest_path = strpathcombine(dest_root, entry->sub_path);
        if (dest_path == NULL) {
            errno = ENOMEM;
            platform_getfiles_destroy(&files);
            return -1;
        }

        if (entry->type == PLATFORM_FILETYPE_SYMLINK) {
            char* link_target = NULL;
            if (platform_readlink(entry->path, &link_target) == 0 && link_target != NULL) {
                // Best-effort: create a symlink with unknown type (assume file).
                (void)platform_symlink(link_target, dest_path, 0);
                free(link_target);
            }
        } else if (entry->type == PLATFORM_FILETYPE_FILE || entry->type == PLATFORM_FILETYPE_UNKNOWN) {
            if (platform_copyfile(entry->path, dest_path) != 0) {
                free(dest_path);
                platform_getfiles_destroy(&files);
                return -1;
            }
        }

        free(dest_path);
    }

    platform_getfiles_destroy(&files);
    return 0;
}

int containerv_oci_bundle_get_paths(
    const char*                         runtimeRoot,
    struct containerv_oci_bundle_paths* paths)
{
    char* bundleDir = NULL;
    char* rootfsDir = NULL;
    char* configPath = NULL;

    if (runtimeRoot == NULL || paths == NULL) {
        errno = EINVAL;
        return -1;
    }

    bundleDir = strpathcombine(runtimeRoot, "oci-bundle");
    rootfsDir = strpathcombine(bundleDir, "rootfs");
    configPath = strpathcombine(bundleDir, "config.json");
    if (bundleDir == NULL || rootfsDir == NULL || configPath == NULL) {
        goto cleanup;
    }

    paths->bundle_dir = bundleDir;
    paths->rootfs_dir = rootfsDir;
    paths->config_path = configPath;
    return 0;

cleanup:
    free(bundleDir);
    free(rootfsDir);
    free(configPath);
    return -1;
}

void containerv_oci_bundle_paths_delete(struct containerv_oci_bundle_paths* paths)
{
    if (paths == NULL) {
        return;
    }

    free(paths->bundle_dir);
    free(paths->rootfs_dir);
    free(paths->config_path);
}

int containerv_oci_bundle_prepare_rootfs(
    const struct containerv_oci_bundle_paths* paths,
    const char*                               source_rootfs)
{
    if (!paths || !paths->bundle_dir || !paths->rootfs_dir) {
        errno = EINVAL;
        return -1;
    }

    if (platform_mkdir(paths->bundle_dir) != 0) {
        return -1;
    }

    if (!source_rootfs) {
        // Create empty rootfs directory.
        return platform_mkdir(paths->rootfs_dir);
    }

    // Prefer copying for safety (avoids symlink/junction semantics on Windows).
    if (platform_mkdir(paths->rootfs_dir) != 0) {
        return -1;
    }

    return __copytree_best_effort(source_rootfs, paths->rootfs_dir);
}

int containerv_oci_bundle_prepare_rootfs_mountpoints(
    const struct containerv_oci_bundle_paths* paths)
{
    if (!paths || !paths->rootfs_dir) {
        errno = EINVAL;
        return -1;
    }

    // Standard Linux mountpoints (stored as Linux-style absolute paths).
    for (const char* const* mp = containerv_standard_linux_mountpoints(); mp != NULL && *mp != NULL; ++mp) {
        const char* s = *mp;
        if (s == NULL || s[0] == '\0') {
            continue;
        }

        // Convert "/dev/pts" -> "dev/pts" and join under rootfs_dir.
        while (*s == '/') {
            s++;
        }
        if (*s == '\0') {
            continue;
        }

        char* target = strpathcombine(paths->rootfs_dir, s);
        if (target == NULL) {
            errno = ENOMEM;
            return -1;
        }

        if (platform_mkdir(target) == 0) {
            (void)platform_chmod(target, 0755);
        }
        free(target);
    }

    return 0;
}

int containerv_oci_bundle_prepare_rootfs_dir(
    const struct containerv_oci_bundle_paths* paths,
    const char*                               linux_path,
    uint32_t                                  permissions)
{
    if (!paths || !paths->rootfs_dir) {
        errno = EINVAL;
        return -1;
    }

    char* rel = NULL;
    if (__normalize_linux_relpath_alloc(linux_path, &rel) != 0) {
        return -1;
    }

    if (rel[0] == '\0') {
        // Root path: ensure rootfs exists and apply permissions.
        (void)platform_mkdir(paths->rootfs_dir);
        (void)platform_chmod(paths->rootfs_dir, permissions);
        free(rel);
        return 0;
    }

    if (__ensure_parent_dirs(paths->rootfs_dir, rel) != 0) {
        free(rel);
        return -1;
    }

    char* target = strpathcombine(paths->rootfs_dir, rel);
    if (target == NULL) {
        free(rel);
        errno = ENOMEM;
        return -1;
    }

    if (platform_mkdir(target) == 0) {
        (void)platform_chmod(target, permissions);
    }

    free(target);
    free(rel);
    return 0;
}

int containerv_oci_bundle_prepare_rootfs_standard_files(
    const struct containerv_oci_bundle_paths* paths,
    const char* hostname,
    const char* dns_servers)
{
    if (!paths || !paths->rootfs_dir) {
        errno = EINVAL;
        return -1;
    }

    const char* host = (hostname && hostname[0] != '\0') ? hostname : "localhost";

    char* etc_hosts = strpathcombine(paths->rootfs_dir, "etc/hosts");
    char* etc_hostname = strpathcombine(paths->rootfs_dir, "etc/hostname");
    char* etc_resolv = strpathcombine(paths->rootfs_dir, "etc/resolv.conf");
    if (etc_hosts == NULL || etc_hostname == NULL || etc_resolv == NULL) {
        free(etc_hosts);
        free(etc_hostname);
        free(etc_resolv);
        errno = ENOMEM;
        return -1;
    }

    (void)__ensure_parent_dirs(paths->rootfs_dir, "etc");

    char hosts_buf[512];
    snprintf(hosts_buf, sizeof(hosts_buf), "127.0.0.1\tlocalhost\n127.0.1.1\t%s\n", host);

    int rc = platform_writetextfile(etc_hosts, hosts_buf);
    if (rc == 0) {
        char hostname_buf[256];
        snprintf(hostname_buf, sizeof(hostname_buf), "%s\n", host);
        rc = platform_writetextfile(etc_hostname, hostname_buf);
    }
    if (rc == 0) {
        rc = __write_resolv_conf(etc_resolv, dns_servers);
    }

    if (rc == 0) {
        char* etc_dir = strpathcombine(paths->rootfs_dir, "etc");
        if (etc_dir != NULL) {
            (void)platform_chmod(etc_dir, 0755);
            free(etc_dir);
        }
        (void)platform_chmod(etc_hosts, 0644);
        (void)platform_chmod(etc_hostname, 0644);
        (void)platform_chmod(etc_resolv, 0644);
    }

    free(etc_hosts);
    free(etc_hostname);
    free(etc_resolv);
    return rc;
}

int containerv_oci_bundle_write_config(
    const struct containerv_oci_bundle_paths* paths,
    const char*                               ociConfigJson)
{
    int status;

    if (!paths || !paths->bundle_dir || !paths->config_path || !ociConfigJson) {
        errno = EINVAL;
        return -1;
    }

    status = platform_mkdir(paths->bundle_dir);
    if (status) {
        return status;
    }
    return platform_writetextfile(paths->config_path, ociConfigJson);
}
