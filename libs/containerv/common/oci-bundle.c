#include "oci-bundle.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <chef/platform.h>

#include "standard-mounts.h"

static int __strpathcombine_alloc(const char* a, const char* b, char** out)
{
    if (!a || !b || !out) {
        errno = EINVAL;
        return -1;
    }

    *out = strpathcombine(a, b);
    if (*out == NULL) {
        errno = ENOMEM;
        return -1;
    }

    return 0;
}

static int __mkdir_p_best_effort(const char* path)
{
    if (!path) {
        errno = EINVAL;
        return -1;
    }

    if (platform_mkdir(path) == 0) {
        return 0;
    }

    // If it already exists, treat as success.
    if (errno == EEXIST) {
        return 0;
    }

    return -1;
}

static int __write_text_file(const char* path, const char* text)
{
    FILE* fp;

    if (!path || !text) {
        errno = EINVAL;
        return -1;
    }

    fp = fopen(path, "wb");
    if (!fp) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }

    if (fwrite(text, 1, strlen(text), fp) != strlen(text)) {
        int err = (errno != 0) ? errno : EIO;
        fclose(fp);
        errno = err;
        return -1;
    }

    if (fclose(fp) != 0) {
        if (errno == 0) {
            errno = EIO;
        }
        return -1;
    }

    return 0;
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
            (void)__mkdir_p_best_effort(full);
            *p = saved;
        }
    }

    free(full);
    return 0;
}

static int __copytree_best_effort(const char* source_root, const char* dest_root)
{
    struct list files;
    struct list_item* item;

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
        }
        else if (entry->type == PLATFORM_FILETYPE_FILE || entry->type == PLATFORM_FILETYPE_UNKNOWN) {
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

void containerv_oci_bundle_paths_destroy(struct containerv_oci_bundle_paths* paths)
{
    if (!paths) {
        return;
    }

    free(paths->bundle_dir);
    free(paths->rootfs_dir);
    free(paths->config_path);

    paths->bundle_dir = NULL;
    paths->rootfs_dir = NULL;
    paths->config_path = NULL;
}

int containerv_oci_bundle_get_paths(
    const char* runtime_dir,
    struct containerv_oci_bundle_paths* out)
{
    char* bundle_dir = NULL;
    char* rootfs_dir = NULL;
    char* config_path = NULL;

    if (!runtime_dir || !out) {
        errno = EINVAL;
        return -1;
    }

    memset(out, 0, sizeof(*out));

    if (__strpathcombine_alloc(runtime_dir, "oci-bundle", &bundle_dir) != 0) {
        goto cleanup;
    }

    if (__strpathcombine_alloc(bundle_dir, "rootfs", &rootfs_dir) != 0) {
        goto cleanup;
    }

    if (__strpathcombine_alloc(bundle_dir, "config.json", &config_path) != 0) {
        goto cleanup;
    }

    out->bundle_dir = bundle_dir;
    out->rootfs_dir = rootfs_dir;
    out->config_path = config_path;
    return 0;

cleanup:
    free(bundle_dir);
    free(rootfs_dir);
    free(config_path);
    return -1;
}

int containerv_oci_bundle_prepare_rootfs(
    const struct containerv_oci_bundle_paths* paths,
    const char* source_rootfs)
{
    if (!paths || !paths->bundle_dir || !paths->rootfs_dir) {
        errno = EINVAL;
        return -1;
    }

    if (__mkdir_p_best_effort(paths->bundle_dir) != 0) {
        return -1;
    }

    if (!source_rootfs) {
        // Create empty rootfs directory.
        return __mkdir_p_best_effort(paths->rootfs_dir);
    }

    // Prefer copying for safety (avoids symlink/junction semantics on Windows).
    if (__mkdir_p_best_effort(paths->rootfs_dir) != 0) {
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

        (void)__mkdir_p_best_effort(target);
        free(target);
    }

    return 0;
}

int containerv_oci_bundle_write_config(
    const struct containerv_oci_bundle_paths* paths,
    const char* oci_config_json)
{
    if (!paths || !paths->bundle_dir || !paths->config_path || !oci_config_json) {
        errno = EINVAL;
        return -1;
    }

    if (__mkdir_p_best_effort(paths->bundle_dir) != 0) {
        return -1;
    }

    return __write_text_file(paths->config_path, oci_config_json);
}
