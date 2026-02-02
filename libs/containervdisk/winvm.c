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
 * Windows VM disk preparation for containerv.
 *
 * This is a Windows-host-only helper. On non-Windows hosts the functions are
 * no-ops and return success.
 */

#include <chef/dirs.h>
#include <chef/package.h>
#include <chef/platform.h>
#include <chef/ingredient.h>

#include <chef/containerv/disk/winvm.h>
#include <chef/containerv.h>
#include <chef/package.h>

#include <errno.h>
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

static char* __ps_quote_single(const char* s)
{
    // Wrap in single quotes; escape single quotes by doubling.
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

static int __spawn_powershell(const char* script)
{
    if (script == NULL) {
        errno = EINVAL;
        return -1;
    }

    char args[8192];
    int rc = snprintf(args, sizeof(args), "-NoProfile -NonInteractive -Command \"%s\"", script);
    if (rc < 0 || (size_t)rc >= sizeof(args)) {
        errno = EINVAL;
        return -1;
    }

    return platform_spawn("powershell", args, NULL, &(struct platform_spawn_options) {0});
}

static char* __exec_powershell(const char* script)
{
    if (script == NULL) {
        errno = EINVAL;
        return NULL;
    }

    char cmd[8192];
    int rc = snprintf(cmd, sizeof(cmd), "powershell -NoProfile -NonInteractive -Command \"%s\"", script);
    if (rc < 0 || (size_t)rc >= sizeof(cmd)) {
        errno = EINVAL;
        return NULL;
    }
    return platform_exec(cmd);
}

static void __trim_inplace(char* s)
{
    if (s == NULL) {
        return;
    }
    size_t len = strlen(s);
    while (len && (s[len - 1] == '\n' || s[len - 1] == '\r' || s[len - 1] == ' ' || s[len - 1] == '\t')) {
        s[len - 1] = '\0';
        len--;
    }
    char* p = s;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        p++;
    }
    if (p != s) {
        memmove(s, p, strlen(p) + 1);
    }
}

static int __ensure_dir(const char* path)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    return platform_mkdir(path);
}

static char* __join_winpath2(const char* a, const char* b)
{
    if (a == NULL || b == NULL) {
        return NULL;
    }
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t need = la + 1 + lb + 1;
    char* out = calloc(need, 1);
    if (!out) {
        return NULL;
    }
    strcpy(out, a);
    if (la > 0 && out[la - 1] != '\\') {
        out[la] = '\\';
        out[la + 1] = '\0';
    }
    strcat(out, b);
    return out;
}

static char* __cache_dir(void)
{
    const char* base = chef_dirs_cache();
    if (base == NULL) {
        return NULL;
    }
    // Under cache/cvd/winvm
    char* c1 = strpathjoin(base, "containerv", "winvm");
    if (c1 == NULL) {
        return NULL;
    }
    if (__ensure_dir(c1) != 0) {
        free(c1);
        return NULL;
    }
    return c1;
}

static int __create_differencing_vhdx(const char* child, const char* parent)
{
    char* child_q = __ps_quote_single(child);
    char* parent_q = __ps_quote_single(parent);
    if (child_q == NULL || parent_q == NULL) {
        free(child_q);
        free(parent_q);
        return -1;
    }

    char script[8192];
    int rc = snprintf(
        script,
        sizeof(script),
        "New-VHD -Path %s -ParentPath %s -Differencing | Out-Null",
        child_q,
        parent_q
    );
    free(child_q);
    free(parent_q);
    if (rc < 0 || (size_t)rc >= sizeof(script)) {
        errno = EINVAL;
        return -1;
    }
    return __spawn_powershell(script);
}

static char* __mount_vhd_get_drive_root(const char* vhd_path)
{
    // Returns something like "E:\\".
    char* p_q = __ps_quote_single(vhd_path);
    if (p_q == NULL) {
        return NULL;
    }

    // Ensure a drive letter is assigned; pick the largest partition if needed.
    const char* tmpl =
        "$ErrorActionPreference='Stop';"
        "$p=%s;"
        "$disk=(Mount-VHD -Path $p -Passthru | Get-Disk);"
        "$parts=($disk | Get-Partition | Sort-Object Size -Descending);"
        "foreach($pt in $parts){ if(-not $pt.DriveLetter){ try{ $pt | Add-PartitionAccessPath -AssignDriveLetter | Out-Null } catch{} } }"
        "$vol=($disk | Get-Partition | Get-Volume | Where-Object { $_.DriveLetter -and $_.FileSystem } | Sort-Object Size -Descending | Select-Object -First 1);"
        "if(-not $vol){ throw 'no volume'; }"
        "Write-Output ($vol.DriveLetter + ':\\\\');";

    char script[8192];
    int rc = snprintf(script, sizeof(script), tmpl, p_q);
    free(p_q);
    if (rc < 0 || (size_t)rc >= sizeof(script)) {
        errno = EINVAL;
        return NULL;
    }

    char* out = __exec_powershell(script);
    if (out == NULL) {
        return NULL;
    }
    __trim_inplace(out);
    if (out[0] == '\0') {
        free(out);
        errno = EIO;
        return NULL;
    }
    return out;
}

static int __dismount_vhd(const char* vhd_path)
{
    char* p_q = __ps_quote_single(vhd_path);
    if (p_q == NULL) {
        return -1;
    }
    char script[4096];
    int rc = snprintf(script, sizeof(script), "Dismount-VHD -Path %s", p_q);
    free(p_q);
    if (rc < 0 || (size_t)rc >= sizeof(script)) {
        errno = EINVAL;
        return -1;
    }
    return __spawn_powershell(script);
}

static int __unmkvafs_to_path(const char* pack, const char* out_dir)
{
    char args[8192];
    int rc;
    if (pack == NULL || out_dir == NULL) {
        errno = EINVAL;
        return -1;
    }

    rc = snprintf(args, sizeof(args), "--no-progress --out \"%s\" \"%s\"", out_dir, pack);
    if (rc < 0 || (size_t)rc >= sizeof(args)) {
        errno = EINVAL;
        return -1;
    }
    return platform_spawn("unmkvafs", args, NULL, &(struct platform_spawn_options) {0});
}

static int __mklink_junction(const char* link_path, const char* target_path)
{
    // Use cmd.exe to create a junction on the mounted volume.
    char args[8192];
    int rc = snprintf(
        args,
        sizeof(args),
        "/c if exist \"%s\" rmdir /S /Q \"%s\" & mklink /J \"%s\" \"%s\"",
        link_path,
        link_path,
        link_path,
        target_path
    );
    if (rc < 0 || (size_t)rc >= sizeof(args)) {
        errno = EINVAL;
        return -1;
    }
    return platform_spawn("cmd.exe", args, NULL, &(struct platform_spawn_options){0});
}

static int __mkdir_cmd(const char* path)
{
    if (path == NULL) {
        errno = EINVAL;
        return -1;
    }
    char args[8192];
    int rc = snprintf(args, sizeof(args), "/c mkdir \"%s\" 2>nul", path);
    if (rc < 0 || (size_t)rc >= sizeof(args)) {
        errno = EINVAL;
        return -1;
    }
    return platform_spawn("cmd.exe", args, NULL, &(struct platform_spawn_options){0});
}

static int __path_exists_file(const char* path)
{
    struct platform_stat st;
    if (platform_stat(path, &st) != 0) {
        return 0;
    }
    return st.type == PLATFORM_FILETYPE_FILE;
}

static char* __find_base_vhdx(const struct containerv_layer* layers, int layer_count)
{
    for (int i = 0; i < layer_count; i++) {
        if (layers[i].type != CONTAINERV_LAYER_BASE_ROOTFS) {
            continue;
        }
        if (layers[i].source == NULL || layers[i].source[0] == '\0') {
            continue;
        }

        // If source is a vhdx file, use it directly.
        if (strendswith(layers[i].source, ".vhdx") == 0) {
            char* p = platform_strdup(layers[i].source);
            if (p != NULL && __path_exists_file(p)) {
                return p;
            }
            free(p);
        }

        // Otherwise, try source\\container.vhdx
        char* p = __join_winpath2(layers[i].source, "container.vhdx");
        if (p != NULL && __path_exists_file(p)) {
            return p;
        }
        free(p);
    }
    return NULL;
}

static int __pick_single_application_pack(const struct containerv_layer* layers, int layer_count, const char** packPathOut, struct chef_package** pkgOut, struct chef_version** verOut)
{
    const char* pick = NULL;
    struct chef_package* pick_pkg = NULL;
    struct chef_version* pick_ver = NULL;

    for (int i = 0; i < layer_count; i++) {
        if (layers[i].type != CONTAINERV_LAYER_VAFS_PACKAGE) {
            continue;
        }
        if (layers[i].source == NULL || layers[i].source[0] == '\0') {
            continue;
        }

        struct chef_package* pkg = NULL;
        struct chef_version* ver = NULL;
        if (chef_package_load(layers[i].source, &pkg, &ver, NULL, NULL) != 0 || pkg == NULL) {
            continue;
        }

        if (pkg->type == CHEF_PACKAGE_TYPE_APPLICATION) {
            if (pick != NULL) {
                // Multiple application packs; not cacheable in current model.
                chef_package_free(pkg);
                chef_version_free(ver);
                if (pick_pkg) chef_package_free(pick_pkg);
                if (pick_ver) chef_version_free(pick_ver);
                *packPathOut = NULL;
                *pkgOut = NULL;
                *verOut = NULL;
                return 0;
            }
            pick = layers[i].source;
            pick_pkg = pkg;
            pick_ver = ver;
        } else {
            chef_package_free(pkg);
            chef_version_free(ver);
        }
    }

    *packPathOut = pick;
    *pkgOut = pick_pkg;
    *verOut = pick_ver;
    return 0;
}

static void __derive_pub_name(const struct chef_package* pkg, const char** pubOut, const char** nameOut)
{
    const char* publisher = pkg && pkg->publisher ? pkg->publisher : NULL;
    const char* name = pkg && pkg->package ? pkg->package : "unknown";

    if (publisher == NULL) {
        const char* slash = strchr(name, '/');
        if (slash != NULL && slash != name && slash[1] != '\0') {
            // Use best-effort, but don't slice here; caller should sanitize.
            *pubOut = name;
            *nameOut = slash + 1;
            return;
        }
        publisher = "default";
    }
    *pubOut = publisher;
    *nameOut = name;
}

static char* __format_version_tag(const struct chef_version* v)
{
    if (v == NULL) {
        return platform_strdup("0.0.0.0");
    }
    char buf[128];
    if (v->tag && v->tag[0]) {
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d%s", v->major, v->minor, v->patch, v->revision, v->tag);
    } else {
        snprintf(buf, sizeof(buf), "%d.%d.%d.%d", v->major, v->minor, v->patch, v->revision);
    }
    return platform_strdup(buf);
}

static int __has_vafs_layers(const struct containerv_layer* layers, int layer_count)
{
    for (int i = 0; i < layer_count; i++) {
        if (layers[i].type == CONTAINERV_LAYER_VAFS_PACKAGE && layers[i].source && layers[i].source[0]) {
            return 1;
        }
    }
    return 0;
}

static char* __path_join_host_root(const char* drive_root, const char* rel)
{
    // drive_root is expected to be like "E:\\".
    if (drive_root == NULL || rel == NULL) {
        return NULL;
    }
    size_t ld = strlen(drive_root);
    size_t lr = strlen(rel);
    size_t need = ld + lr + 1;
    char* out = calloc(need, 1);
    if (out == NULL) {
        return NULL;
    }
    strcpy(out, drive_root);
    // drive_root already ends with backslash.
    strcat(out, rel);
    return out;
}

static int __append_activate_header(
    char**      script_inout,
    uint32_t    index,
    const char* publisher,
    const char* name,
    const char* guest_root)
{
    if (script_inout == NULL || *script_inout == NULL || guest_root == NULL) {
        errno = EINVAL;
        return -1;
    }

    const char* tmpl =
        "\r\n"
        "rem %s/%s\r\n"
        "set \"CHEF_PKG_%u=%s\"\r\n";

    char block[1024];
    int rc = snprintf(block, sizeof(block), tmpl,
        publisher ? publisher : "default",
        name ? name : "unknown",
        (unsigned)index,
        guest_root);
    if (rc < 0 || (size_t)rc >= sizeof(block)) {
        errno = EINVAL;
        return -1;
    }

    size_t old_len = strlen(*script_inout);
    size_t add_len = strlen(block);
    char* next = realloc(*script_inout, old_len + add_len + 1);
    if (next == NULL) {
        return -1;
    }
    *script_inout = next;
    memcpy(*script_inout + old_len, block, add_len + 1);
    return 0;
}

static int __is_abs_guest_path(const char* p)
{
    if (p == NULL || p[0] == '\0') {
        return 0;
    }
    // Drive root, UNC, or leading slash/backslash.
    if ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) {
        if (p[1] == ':' && (p[2] == '\\' || p[2] == '/')) {
            return 1;
        }
    }
    if (p[0] == '\\' || p[0] == '/') {
        return 1;
    }
    return 0;
}

static char* __guest_join(const char* root, const char* rel)
{
    if (root == NULL || rel == NULL) {
        return NULL;
    }
    if (__is_abs_guest_path(rel)) {
        return platform_strdup(rel);
    }
    size_t lr = strlen(root);
    size_t ll = strlen(rel);
    size_t need = lr + 1 + ll + 1;
    char* out = calloc(need, 1);
    if (out == NULL) {
        return NULL;
    }
    strcpy(out, root);
    if (lr > 0 && out[lr - 1] != '\\') {
        out[lr] = '\\';
        out[lr + 1] = '\0';
    }
    strcat(out, rel);
    return out;
}

static char* __join_dirs_for_env(char** dirs, const char* guest_root, const char* fallback_rel)
{
    // Return a semicolon-separated list of absolute guest paths.
    // If dirs is NULL or empty, returns guest_root\fallback_rel if fallback_rel is non-NULL.
    if (dirs == NULL || dirs[0] == NULL) {
        if (fallback_rel == NULL) {
            return NULL;
        }
        char* single = __guest_join(guest_root, fallback_rel);
        return single;
    }

    size_t total = 0;
    int count = 0;
    for (int i = 0; dirs[i] != NULL; i++) {
        if (dirs[i][0] == '\0') {
            continue;
        }
        char* p = __guest_join(guest_root, dirs[i]);
        if (p == NULL) {
            return NULL;
        }
        total += strlen(p);
        free(p);
        count++;
    }
    if (count == 0) {
        if (fallback_rel == NULL) {
            return NULL;
        }
        return __guest_join(guest_root, fallback_rel);
    }

    // separators + NUL
    total += (size_t)(count - 1) + 1;
    char* out = calloc(total, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t off = 0;
    int written = 0;
    for (int i = 0; dirs[i] != NULL; i++) {
        if (dirs[i][0] == '\0') {
            continue;
        }
        char* p = __guest_join(guest_root, dirs[i]);
        if (p == NULL) {
            free(out);
            return NULL;
        }
        size_t lp = strlen(p);
        if (written) {
            out[off++] = ';';
        }
        memcpy(out + off, p, lp);
        off += lp;
        free(p);
        written++;
    }
    out[off] = '\0';
    return out;
}

static char* __join_flags(char** flags)
{
    if (flags == NULL || flags[0] == NULL) {
        return NULL;
    }
    size_t total = 0;
    int count = 0;
    for (int i = 0; flags[i] != NULL; i++) {
        if (flags[i][0] == '\0') {
            continue;
        }
        total += strlen(flags[i]);
        count++;
    }
    if (count == 0) {
        return NULL;
    }
    total += (size_t)(count - 1) + 1;
    char* out = calloc(total, 1);
    if (out == NULL) {
        return NULL;
    }
    size_t off = 0;
    int written = 0;
    for (int i = 0; flags[i] != NULL; i++) {
        if (flags[i][0] == '\0') {
            continue;
        }
        size_t lf = strlen(flags[i]);
        if (written) {
            out[off++] = ' ';
        }
        memcpy(out + off, flags[i], lf);
        off += lf;
        written++;
    }
    out[off] = '\0';
    return out;
}

static int __append_activate_from_options(
    char**                    script_inout,
    uint32_t                  index,
    const char*               publisher,
    const char*               name,
    const char*               guest_root,
    const struct ingredient_options* opts)
{
    // Provide good defaults if opts is missing.
    char* path_list = __join_dirs_for_env(opts ? opts->bin_dirs : NULL, guest_root, "bin");
    char* inc_list  = __join_dirs_for_env(opts ? opts->inc_dirs : NULL, guest_root, "include");
    char* lib_list  = __join_dirs_for_env(opts ? opts->lib_dirs : NULL, guest_root, "lib");
    char* cflags    = __join_flags(opts ? opts->compiler_flags : NULL);
    char* ldflags   = __join_flags(opts ? opts->linker_flags : NULL);

    if (path_list == NULL && inc_list == NULL && lib_list == NULL && cflags == NULL && ldflags == NULL) {
        // Nothing to add.
        free(path_list);
        free(inc_list);
        free(lib_list);
        free(cflags);
        free(ldflags);
        return 0;
    }

    // Write traceability header + CHEF_PKG_%u.
    if (__append_activate_header(script_inout, index, publisher, name, guest_root) != 0) {
        free(path_list);
        free(inc_list);
        free(lib_list);
        free(cflags);
        free(ldflags);
        return -1;
    }

    // Single clean set of env vars per package.
    if (path_list != NULL) {
        char line[4096];
        int rc = snprintf(line, sizeof(line), "set \"PATH=%s;%%PATH%%\"\r\n", path_list);
        if (rc > 0 && (size_t)rc < sizeof(line)) {
            size_t old_len = strlen(*script_inout);
            size_t add_len = strlen(line);
            char* next = realloc(*script_inout, old_len + add_len + 1);
            if (next != NULL) {
                *script_inout = next;
                memcpy(*script_inout + old_len, line, add_len + 1);
            }
        }
    }
    if (inc_list != NULL) {
        char line[4096];
        int rc = snprintf(line, sizeof(line), "set \"INCLUDE=%s;%%INCLUDE%%\"\r\n", inc_list);
        if (rc > 0 && (size_t)rc < sizeof(line)) {
            size_t old_len = strlen(*script_inout);
            size_t add_len = strlen(line);
            char* next = realloc(*script_inout, old_len + add_len + 1);
            if (next != NULL) {
                *script_inout = next;
                memcpy(*script_inout + old_len, line, add_len + 1);
            }
        }
    }
    if (lib_list != NULL) {
        char line[4096];
        int rc = snprintf(line, sizeof(line), "set \"LIB=%s;%%LIB%%\"\r\n", lib_list);
        if (rc > 0 && (size_t)rc < sizeof(line)) {
            size_t old_len = strlen(*script_inout);
            size_t add_len = strlen(line);
            char* next = realloc(*script_inout, old_len + add_len + 1);
            if (next != NULL) {
                *script_inout = next;
                memcpy(*script_inout + old_len, line, add_len + 1);
            }
        }
    }

    // Add flags to both generic env vars and MSVC-friendly ones.
    if (cflags != NULL) {
        char line[4096];
        int rc = snprintf(line, sizeof(line),
            "set \"CHEF_CFLAGS=%%CHEF_CFLAGS%% %s\"\r\n"
            "set \"CFLAGS=%%CFLAGS%% %s\"\r\n"
            "set \"CL=%%CL%% %s\"\r\n",
            cflags, cflags, cflags);
        if (rc > 0 && (size_t)rc < sizeof(line)) {
            size_t old_len = strlen(*script_inout);
            size_t add_len = strlen(line);
            char* next = realloc(*script_inout, old_len + add_len + 1);
            if (next != NULL) {
                *script_inout = next;
                memcpy(*script_inout + old_len, line, add_len + 1);
            }
        }
    }
    if (ldflags != NULL) {
        char line[4096];
        int rc = snprintf(line, sizeof(line),
            "set \"CHEF_LDFLAGS=%%CHEF_LDFLAGS%% %s\"\r\n"
            "set \"LDFLAGS=%%LDFLAGS%% %s\"\r\n"
            "set \"LINK=%%LINK%% %s\"\r\n",
            ldflags, ldflags, ldflags);
        if (rc > 0 && (size_t)rc < sizeof(line)) {
            size_t old_len = strlen(*script_inout);
            size_t add_len = strlen(line);
            char* next = realloc(*script_inout, old_len + add_len + 1);
            if (next != NULL) {
                *script_inout = next;
                memcpy(*script_inout + old_len, line, add_len + 1);
            }
        }
    }

    free(path_list);
    free(inc_list);
    free(lib_list);
    free(cflags);
    free(ldflags);
    return 0;
}

static int __apply_vafs_layers_to_mounted_os_disk(
    const char*                  drive_root,
    const struct containerv_layer* layers,
    int                          layer_count,
    int*                         applied_any_out,
    int*                         wrote_activate_out)
{
    // drive_root is host-side mount root like "E:\\".
    // Activation script must use guest-visible "C:\\..." paths.
    int applied_any = 0;
    int wrote_activate = 0;
    char* activate = NULL;
    uint32_t act_index = 0;

    if (applied_any_out) {
        *applied_any_out = 0;
    }
    if (wrote_activate_out) {
        *wrote_activate_out = 0;
    }
    if (drive_root == NULL || layers == NULL || layer_count <= 0) {
        return 0;
    }

    // Ensure common roots exist.
    {
        char* chef_dir = __path_join_host_root(drive_root, "chef");
        char* app_dir = __path_join_host_root(drive_root, "chef\\app");
        char* pkgs_dir = __path_join_host_root(drive_root, "chef\\pkgs");
        if (chef_dir) (void)__mkdir_cmd(chef_dir);
        if (app_dir)  (void)__mkdir_cmd(app_dir);
        if (pkgs_dir) (void)__mkdir_cmd(pkgs_dir);
        free(chef_dir);
        free(app_dir);
        free(pkgs_dir);
    }

    for (int i = 0; i < layer_count; i++) {
        if (layers[i].type != CONTAINERV_LAYER_VAFS_PACKAGE) {
            continue;
        }
        if (layers[i].source == NULL || layers[i].source[0] == '\0') {
            continue;
        }

        struct chef_package* pkg = NULL;
        struct chef_version* ver = NULL;
        if (chef_package_load(layers[i].source, &pkg, &ver, NULL, NULL) != 0 || pkg == NULL) {
            VLOG_ERROR("containerv", "winvm: failed to load package metadata for %s\n", layers[i].source);
            chef_package_free(pkg);
            chef_version_free(ver);
            return -1;
        }

        const char* pub_ptr;
        const char* name_ptr;
        __derive_pub_name(pkg, &pub_ptr, &name_ptr);

        char pub_buf[CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX + 1];
        if (pub_ptr == pkg->package) {
            const char* slash = strchr(pkg->package, '/');
            size_t n = (slash != NULL) ? (size_t)(slash - pkg->package) : 0;
            if (n == 0 || n > CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX) {
                strcpy(pub_buf, "default");
            } else {
                memcpy(pub_buf, pkg->package, n);
                pub_buf[n] = '\0';
            }
            pub_ptr = pub_buf;
        }

        // Build host and guest install roots.
        // Application installs: C:\chef\app\publisher\package
        // Build/toolchain/ingredient installs: C:\chef\pkgs\publisher\package\<version>
        char* ver_tag = __format_version_tag(ver);
        if (ver_tag == NULL) {
            chef_package_free(pkg);
            chef_version_free(ver);
            return -1;
        }

        char guest_root[1024];
        char guest_current[1024];
        char rel_root[1024];
        char rel_current[1024];

        if (pkg->type == CHEF_PACKAGE_TYPE_APPLICATION) {
            snprintf(rel_root, sizeof(rel_root), "chef\\app\\%s\\%s", pub_ptr, name_ptr);
            snprintf(guest_root, sizeof(guest_root), "C:\\chef\\app\\%s\\%s", pub_ptr, name_ptr);
            snprintf(rel_current, sizeof(rel_current), "chef\\app\\current");
            snprintf(guest_current, sizeof(guest_current), "C:\\chef\\app\\current");
        } else {
            snprintf(rel_root, sizeof(rel_root), "chef\\pkgs\\%s\\%s\\%s", pub_ptr, name_ptr, ver_tag);
            snprintf(guest_root, sizeof(guest_root), "C:\\chef\\pkgs\\%s\\%s\\%s", pub_ptr, name_ptr, ver_tag);
            snprintf(rel_current, sizeof(rel_current), "chef\\pkgs\\%s\\%s\\current", pub_ptr, name_ptr);
            snprintf(guest_current, sizeof(guest_current), "C:\\chef\\pkgs\\%s\\%s\\current", pub_ptr, name_ptr);
        }

        free(ver_tag);

        char* host_root = __path_join_host_root(drive_root, rel_root);
        if (host_root == NULL) {
            chef_package_free(pkg);
            chef_version_free(ver);
            return -1;
        }

        // Ensure directory exists.
        (void)__mkdir_cmd(host_root);

        // Extract pack contents.
        if (__unmkvafs_to_path(layers[i].source, host_root) != 0) {
            VLOG_ERROR("containerv", "winvm: unmkvafs failed for %s\n", layers[i].source);
            free(host_root);
            chef_package_free(pkg);
            chef_version_free(ver);
            return -1;
        }
        applied_any = 1;

        // Set junctions.
        if (pkg->type == CHEF_PACKAGE_TYPE_APPLICATION) {
            char* host_link = __path_join_host_root(drive_root, rel_current);
            if (host_link != NULL) {
                (void)__mklink_junction(host_link, host_root);
                free(host_link);
            }
        } else {
            char* host_link = __path_join_host_root(drive_root, rel_current);
            if (host_link != NULL) {
                (void)__mklink_junction(host_link, host_root);
                free(host_link);
            }
        }

        // Build activation script for non-application packages.
        if (pkg->type != CHEF_PACKAGE_TYPE_APPLICATION) {
            if (activate == NULL) {
                activate = platform_strdup("@echo off\r\nsetlocal\r\n");
                if (activate == NULL) {
                    free(host_root);
                    chef_package_free(pkg);
                    chef_version_free(ver);
                    return -1;
                }
                // Make sure common env vars exist.
                {
                    const char* header = "set \"CHEF_ROOT=C:\\chef\"\r\n";
                    size_t old_len = strlen(activate);
                    size_t add_len = strlen(header);
                    char* next = realloc(activate, old_len + add_len + 1);
                    if (next != NULL) {
                        activate = next;
                        memcpy(activate + old_len, header, add_len + 1);
                    }
                }
            }

            // Prefer ingredient-provided installation metadata when available.
            // Toolchain packs may not carry ingredient opts yet; fall back to conventional bin/include/lib.
            const struct ingredient_options* opts = NULL;
            struct ingredient* ing = NULL;
            if (ingredient_open(layers[i].source, &ing) == 0 && ing != NULL) {
                opts = ing->options;
            }

            if (__append_activate_from_options(&activate, act_index++, pub_ptr, name_ptr, guest_root, opts) != 0) {
                if (ing) {
                    ingredient_close(ing);
                }
                free(host_root);
                chef_package_free(pkg);
                chef_version_free(ver);
                free(activate);
                return -1;
            }

            if (ing) {
                ingredient_close(ing);
            }
        }

        free(host_root);
        chef_package_free(pkg);
        chef_version_free(ver);
    }

    // Write activation script into the disk if we built one.
    if (activate != NULL) {
        char* host_activate = __path_join_host_root(drive_root, "chef\\activate.cmd");
        if (host_activate == NULL) {
            free(activate);
            return -1;
        }
        FILE* f = fopen(host_activate, "wb");
        if (f == NULL) {
            free(host_activate);
            free(activate);
            return -1;
        }
        fwrite(activate, 1, strlen(activate), f);
        fclose(f);
        free(host_activate);
        free(activate);
        wrote_activate = 1;
    }

    if (applied_any_out) {
        *applied_any_out = applied_any;
    }
    if (wrote_activate_out) {
        *wrote_activate_out = wrote_activate;
    }
    return 0;
}

static char* __make_temp_dir_under(const char* base)
{
    char guid[40];
    platform_guid_new_string(guid);
    char* dir = strpathjoin(base, guid);
    if (dir == NULL) {
        return NULL;
    }
    if (platform_mkdir(dir) != 0) {
        free(dir);
        return NULL;
    }
    return dir;
}

static int __filter_out_vafs_layers(struct containerv_layer** layers_inout, int* layer_count_inout)
{
    int in_count = *layer_count_inout;
    struct containerv_layer* in = *layers_inout;
    int out_count = 0;

    for (int i = 0; i < in_count; i++) {
        if (in[i].type == CONTAINERV_LAYER_VAFS_PACKAGE) {
            continue;
        }
        out_count++;
    }
    if (out_count == in_count) {
        return 0;
    }

    struct containerv_layer* out = calloc(out_count, sizeof(*out));
    if (out == NULL) {
        return -1;
    }

    int j = 0;
    for (int i = 0; i < in_count; i++) {
        if (in[i].type == CONTAINERV_LAYER_VAFS_PACKAGE) {
            continue;
        }
        out[j++] = in[i];
    }

    free(in);
    *layers_inout = out;
    *layer_count_inout = out_count;
    return 0;
}

int containerv_disk_winvm_prepare_layers(
    const char*                          container_id,
    struct containerv_layer**            layers_inout,
    int*                                 layer_count_inout,
    struct containerv_disk_winvm_prepare_result* result_out)
{
    if (result_out == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(result_out, 0, sizeof(*result_out));

    if (container_id == NULL || layers_inout == NULL || layer_count_inout == NULL) {
        errno = EINVAL;
        return -1;
    }

    // Determine whether BASE_ROOTFS points at a VHDX (Windows VM) scenario.
    char* base_vhd = __find_base_vhdx(*layers_inout, *layer_count_inout);
    if (base_vhd == NULL) {
        return 0; // Not a disk-based rootfs request.
    }

    // Cache base disk (best-effort copy).
    char* cache = __cache_dir();
    if (cache == NULL) {
        free(base_vhd);
        return -1;
    }

    uint64_t base_key = __fnv1a64(base_vhd);
    char base_name[64];
    snprintf(base_name, sizeof(base_name), "base-%016llx.vhdx", (unsigned long long)base_key);
    char* base_cached = __join_winpath2(cache, base_name);
    if (base_cached == NULL) {
        free(cache);
        free(base_vhd);
        return -1;
    }

    if (!__path_exists_file(base_cached)) {
        VLOG_TRACE("containerv", "winvm: caching base disk to %s\n", base_cached);
        if (platform_copyfile(base_vhd, base_cached) != 0) {
            VLOG_WARNING("containerv", "winvm: failed to cache base disk, using source path directly\n");
            free(base_cached);
            base_cached = platform_strdup(base_vhd);
            if (base_cached == NULL) {
                free(cache);
                free(base_vhd);
                return -1;
            }
        }
    }

    // Optional: build/cache a single-application layer disk.
    const char* app_pack_path = NULL;
    struct chef_package* app_pkg = NULL;
    struct chef_version* app_ver = NULL;
    (void)__pick_single_application_pack(*layers_inout, *layer_count_inout, &app_pack_path, &app_pkg, &app_ver);

    char* parent_for_writable = NULL;
    int applied = 0;

    if (app_pack_path != NULL && app_pkg != NULL) {
        const char* pub_ptr;
        const char* name_ptr;
        __derive_pub_name(app_pkg, &pub_ptr, &name_ptr);

        // Normalize publisher if it was derived from "publisher/name".
        char pub_buf[CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX + 1];
        if (pub_ptr == app_pkg->package) {
            const char* slash = strchr(app_pkg->package, '/');
            size_t n = (slash != NULL) ? (size_t)(slash - app_pkg->package) : 0;
            if (n == 0 || n > CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX) {
                strcpy(pub_buf, "default");
            } else {
                memcpy(pub_buf, app_pkg->package, n);
                pub_buf[n] = '\0';
            }
            pub_ptr = pub_buf;
        }

        char* ver_tag = __format_version_tag(app_ver);
        if (ver_tag == NULL) {
            free(cache);
            free(base_vhd);
            free(base_cached);
            chef_package_free(app_pkg);
            chef_version_free(app_ver);
            return -1;
        }

        char idbuf[512];
        snprintf(idbuf, sizeof(idbuf), "%s/%s@%s|%s", pub_ptr, name_ptr, ver_tag, base_cached);
        uint64_t app_key = __fnv1a64(idbuf);
        free(ver_tag);

        char app_name[64];
        snprintf(app_name, sizeof(app_name), "app-%016llx.vhdx", (unsigned long long)app_key);
        char* app_cached = __join_winpath2(cache, app_name);
        if (app_cached == NULL) {
            free(cache);
            free(base_vhd);
            free(base_cached);
            chef_package_free(app_pkg);
            chef_version_free(app_ver);
            return -1;
        }

        if (!__path_exists_file(app_cached)) {
            VLOG_TRACE("containerv", "winvm: building cached app layer %s\n", app_cached);
            if (__create_differencing_vhdx(app_cached, base_cached) != 0) {
                VLOG_ERROR("containerv", "winvm: failed to create app differencing disk\n");
                free(app_cached);
                free(cache);
                free(base_vhd);
                free(base_cached);
                chef_package_free(app_pkg);
                chef_version_free(app_ver);
                return -1;
            }

            char* drive = __mount_vhd_get_drive_root(app_cached);
            if (drive == NULL) {
                VLOG_ERROR("containerv", "winvm: failed to mount app disk\n");
                (void)__dismount_vhd(app_cached);
                free(app_cached);
                free(cache);
                free(base_vhd);
                free(base_cached);
                chef_package_free(app_pkg);
                chef_version_free(app_ver);
                return -1;
            }

            // Apply only the application pack into the cached app layer disk.
            // (Other packages are applied into the per-container writable disk.)
            char out_dir[4096];
            snprintf(out_dir, sizeof(out_dir), "%schef\\app\\%s\\%s", drive, pub_ptr, name_ptr);
            (void)__mkdir_cmd(out_dir);
            if (__unmkvafs_to_path(app_pack_path, out_dir) != 0) {
                VLOG_ERROR("containerv", "winvm: unmkvafs failed while building app disk\n");
                free(drive);
                (void)__dismount_vhd(app_cached);
                free(app_cached);
                free(cache);
                free(base_vhd);
                free(base_cached);
                chef_package_free(app_pkg);
                chef_version_free(app_ver);
                return -1;
            }

            {
                char link_path[4096];
                char target_path[4096];
                snprintf(link_path, sizeof(link_path), "%schef\\app\\current", drive);
                snprintf(target_path, sizeof(target_path), "%schef\\app\\%s\\%s", drive, pub_ptr, name_ptr);
                (void)__mklink_junction(link_path, target_path);
            }

            free(drive);
            if (__dismount_vhd(app_cached) != 0) {
                VLOG_WARNING("containerv", "winvm: failed to dismount app disk (continuing)\n");
            }
        }

        parent_for_writable = app_cached;
        applied = 1;
    }

    if (parent_for_writable == NULL) {
        parent_for_writable = platform_strdup(base_cached);
        if (parent_for_writable == NULL) {
            free(cache);
            free(base_vhd);
            free(base_cached);
            if (app_pkg) chef_package_free(app_pkg);
            if (app_ver) chef_version_free(app_ver);
            return -1;
        }
    }

    // Create per-container writable differencing disk in a staging rootfs.
    char* staging = __make_temp_dir_under(cache);
    if (staging == NULL) {
        free(parent_for_writable);
        free(cache);
        free(base_vhd);
        free(base_cached);
        if (app_pkg) chef_package_free(app_pkg);
        if (app_ver) chef_version_free(app_ver);
        return -1;
    }
    char* writable = __join_winpath2(staging, "container.vhdx");
    if (writable == NULL) {
        platform_rmdir(staging);
        free(staging);
        free(parent_for_writable);
        free(cache);
        free(base_vhd);
        free(base_cached);
        if (app_pkg) chef_package_free(app_pkg);
        if (app_ver) chef_version_free(app_ver);
        return -1;
    }
    if (__create_differencing_vhdx(writable, parent_for_writable) != 0) {
        VLOG_ERROR("containerv", "winvm: failed to create per-container writable disk\n");
        free(writable);
        platform_rmdir(staging);
        free(staging);
        free(parent_for_writable);
        free(cache);
        free(base_vhd);
        free(base_cached);
        if (app_pkg) chef_package_free(app_pkg);
        if (app_ver) chef_version_free(app_ver);
        return -1;
    }
    free(writable);

    // If there are any VAFS layers beyond the cached application pack, apply them into
    // the per-container writable disk and generate activate.cmd.
    if (__has_vafs_layers(*layers_inout, *layer_count_inout)) {
        char* writable_path = __join_winpath2(staging, "container.vhdx");
        if (writable_path == NULL) {
            platform_rmdir(staging);
            free(staging);
            free(parent_for_writable);
            free(cache);
            free(base_vhd);
            free(base_cached);
            if (app_pkg) chef_package_free(app_pkg);
            if (app_ver) chef_version_free(app_ver);
            return -1;
        }

        char* drive = __mount_vhd_get_drive_root(writable_path);
        if (drive == NULL) {
            VLOG_ERROR("containerv", "winvm: failed to mount per-container writable disk\n");
            free(writable_path);
            platform_rmdir(staging);
            free(staging);
            free(parent_for_writable);
            free(cache);
            free(base_vhd);
            free(base_cached);
            if (app_pkg) chef_package_free(app_pkg);
            if (app_ver) chef_version_free(app_ver);
            return -1;
        }

        int applied_any = 0;
        int wrote_activate = 0;
        if (__apply_vafs_layers_to_mounted_os_disk(drive, *layers_inout, *layer_count_inout, &applied_any, &wrote_activate) != 0) {
            free(drive);
            (void)__dismount_vhd(writable_path);
            free(writable_path);
            platform_rmdir(staging);
            free(staging);
            free(parent_for_writable);
            free(cache);
            free(base_vhd);
            free(base_cached);
            if (app_pkg) chef_package_free(app_pkg);
            if (app_ver) chef_version_free(app_ver);
            return -1;
        }
        if (applied_any) {
            applied = 1;
        }

        free(drive);
        if (__dismount_vhd(writable_path) != 0) {
            VLOG_WARNING("containerv", "winvm: failed to dismount per-container writable disk (continuing)\n");
        }
        free(writable_path);
    }

    // Replace BASE_ROOTFS source with staging directory.
    for (int i = 0; i < *layer_count_inout; i++) {
        if ((*layers_inout)[i].type == CONTAINERV_LAYER_BASE_ROOTFS) {
            (*layers_inout)[i].source = staging;
            result_out->staging_rootfs = staging;
            break;
        }
    }

    // Filter out VAFS layers; they've been applied to the disk (runtime app) or will be handled later.
    if (__filter_out_vafs_layers(layers_inout, layer_count_inout) != 0) {
        platform_rmdir(staging);
        free(staging);
        result_out->staging_rootfs = NULL;
        free(parent_for_writable);
        free(cache);
        free(base_vhd);
        free(base_cached);
        if (app_pkg) chef_package_free(app_pkg);
        if (app_ver) chef_version_free(app_ver);
        return -1;
    }

    result_out->applied_packages = applied;

    free(parent_for_writable);
    free(cache);
    free(base_vhd);
    free(base_cached);
    if (app_pkg) chef_package_free(app_pkg);
    if (app_ver) chef_version_free(app_ver);
    return 0;
}

void containerv_disk_winvm_prepare_result_destroy(struct containerv_disk_winvm_prepare_result* result)
{
    if (result == NULL) {
        return;
    }
    if (result->staging_rootfs != NULL) {
        (void)platform_rmdir(result->staging_rootfs);
        free(result->staging_rootfs);
        result->staging_rootfs = NULL;
    }
    result->applied_packages = 0;
}

int containerv_disk_winvm_prepare_layers(
    const char*                          container_id,
    struct containerv_layer**            layers_inout,
    int*                                 layer_count_inout,
    struct containerv_disk_winvm_prepare_result* result_out)
{
    (void)container_id;
    (void)layers_inout;
    (void)layer_count_inout;
    if (result_out) {
        memset(result_out, 0, sizeof(*result_out));
    }
    return 0;
}

void containerv_disk_winvm_prepare_result_destroy(struct containerv_disk_winvm_prepare_result* result)
{
    if (result) {
        memset(result, 0, sizeof(*result));
    }
}

static char* __path_join3(const char* a, const char* b, const char* c)
{
    if (a == NULL || b == NULL || c == NULL) {
        return NULL;
    }

    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t lc = strlen(c);
    size_t need = la + 1 + lb + 1 + lc + 1;
    char* out = calloc(need, 1);
    if (out == NULL) {
        return NULL;
    }

    // Always join with backslashes; this path is used inside Windows guests.
    // Accept both "C:\\" and "C:\\foo" style prefixes.
    strcpy(out, a);
    if (la > 0 && out[la - 1] != '\\') {
        out[la] = '\\';
        out[la + 1] = '\0';
    }
    strcat(out, b);
    size_t cur = strlen(out);
    if (cur > 0 && out[cur - 1] != '\\') {
        out[cur] = '\\';
        out[cur + 1] = '\0';
    }
    strcat(out, c);
    return out;
}

static void __derive_publisher_and_name(const struct chef_package* package, const char** publisherOut, const char** nameOut)
{
    const char* publisher = NULL;
    const char* name = NULL;

    if (package != NULL) {
        publisher = package->publisher;
        name = package->package;
    }
    if (name == NULL) {
        name = "unknown";
    }

    // If publisher is not explicitly stored in the package metadata yet,
    // allow a future-proof "publisher/name" convention in package->package.
    if (publisher == NULL) {
        const char* slash = strchr(name, '/');
        if (slash != NULL && slash != name && slash[1] != '\0') {
            // Caller expects stable strings; just return pointers into `name`.
            // The derived publisher is only used for path construction.
            // We will treat it as best-effort and fallback to "default" if needed.
            *publisherOut = name;
            *nameOut = slash + 1;
            return;
        }
        publisher = "default";
    }

    *publisherOut = publisher;
    *nameOut = name;
}

static char* __make_temp_dir(void)
{
    char* base = platform_tmpdir();
    char guid[40];
    char* dir;

    if (base == NULL) {
        return NULL;
    }

    platform_guid_new_string(guid);
    dir = strpathjoin(base, "chef-vafs", guid);
    free(base);
    if (dir == NULL) {
        return NULL;
    }
    if (platform_mkdir(dir) != 0) {
        free(dir);
        return NULL;
    }
    return dir;
}

static int __unmkvafs_to_dir(const char* packPath, const char* outDir)
{
    char args[4096];
    int rc;

    if (packPath == NULL || outDir == NULL) {
        errno = EINVAL;
        return -1;
    }

    rc = snprintf(args, sizeof(args), "--no-progress --out \"%s\" \"%s\"", outDir, packPath);
    if (rc < 0 || (size_t)rc >= sizeof(args)) {
        errno = EINVAL;
        return -1;
    }

    return platform_spawn(
        "unmkvafs",
        args,
        NULL,
        &(struct platform_spawn_options) { 0 }
    );
}

static int __guest_cmd(struct containerv_container* container, const char* cmdline)
{
    process_handle_t pid;
    int exit_code = 0;
    int status;

    if (container == NULL || cmdline == NULL) {
        errno = EINVAL;
        return -1;
    }

    status = containerv_spawn(
        container,
        "cmd.exe",
        &(struct containerv_spawn_options) {
            .arguments = cmdline,
            .environment = NULL,
            .flags = 0,
        },
        &pid
    );
    if (status != 0) {
        return -1;
    }

    status = containerv_wait(container, pid, &exit_code);
    if (status != 0) {
        return -1;
    }

    return exit_code == 0 ? 0 : -1;
}

static int __guest_set_current_app_junction(struct containerv_container* container, const char* appRoot)
{
    // Best-effort: delete existing junction/dir then re-create.
    // Use cmd.exe builtins to avoid reliance on PowerShell.
    char cmd[4096];
    int rc;

    if (appRoot == NULL) {
        errno = EINVAL;
        return -1;
    }

    rc = snprintf(
        cmd,
        sizeof(cmd),
        "/c if exist \"C:\\chef\\app\\current\" rmdir /S /Q \"C:\\chef\\app\\current\" & mklink /J \"C:\\chef\\app\\current\" \"%s\"",
        appRoot
    );
    if (rc < 0 || (size_t)rc >= sizeof(cmd)) {
        errno = EINVAL;
        return -1;
    }
    return __guest_cmd(container, cmd);
}

static int __upload_tree_to_guest(struct containerv_container* container, const char* hostDir, const char* guestDir)
{
    struct list files;
    struct list_item* item;
    int status;

    if (container == NULL || hostDir == NULL || guestDir == NULL) {
        errno = EINVAL;
        return -1;
    }

    list_init(&files);
    status = platform_getfiles(hostDir, 1, &files);
    if (status != 0) {
        platform_getfiles_destroy(&files);
        return -1;
    }

    // Batch uploads to reduce overhead.
    const int batch = 64;
    const char* hostPaths[64];
    const char* guestPaths[64];
    char* guestPathAllocs[64];
    int n = 0;

    list_foreach(&files, item) {
        struct platform_file_entry* entry = (struct platform_file_entry*)item;
        if (entry->type != PLATFORM_FILETYPE_FILE) {
            continue;
        }

        // Convert sub_path to Windows separators.
        char* sub = platform_strdup(entry->sub_path != NULL ? entry->sub_path : entry->name);
        if (sub == NULL) {
            status = -1;
            break;
        }
        for (char* p = sub; *p; ++p) {
            if (*p == '/') {
                *p = '\\';
            }
        }

        // Join guestDir + sub.
        size_t need = strlen(guestDir) + 1 + strlen(sub) + 1;
        char* gpath = calloc(need, 1);
        if (gpath == NULL) {
            free(sub);
            status = -1;
            break;
        }
        strcpy(gpath, guestDir);
        if (gpath[strlen(gpath) - 1] != '\\') {
            strcat(gpath, "\\");
        }
        strcat(gpath, sub);
        free(sub);

        hostPaths[n] = entry->path;
        guestPaths[n] = gpath;
        guestPathAllocs[n] = gpath;
        n++;

        if (n == batch) {
            if (containerv_upload(container, hostPaths, guestPaths, n) != 0) {
                status = -1;
                break;
            }
            for (int i = 0; i < n; i++) {
                free(guestPathAllocs[i]);
            }
            n = 0;
        }
    }

    if (status == 0 && n > 0) {
        if (containerv_upload(container, hostPaths, guestPaths, n) != 0) {
            status = -1;
        }
        for (int i = 0; i < n; i++) {
            free(guestPathAllocs[i]);
        }
    } else if (status != 0) {
        for (int i = 0; i < n; i++) {
            free(guestPathAllocs[i]);
        }
    }

    platform_getfiles_destroy(&files);
    return status;
}

static int containerv_disk_winvm_provision(struct containerv_container* container, const struct chef_create_parameters* params)
{
    // Only meaningful for Windows-host Hyper-V VM-backed containers.
    if (!containerv_is_vm(container) || !containerv_guest_is_windows(container)) {
        return 0;
    }

    // Ensure base directories exist.
    // Best-effort: mkdir is idempotent.
    (void)__guest_cmd(container, "/c mkdir \"C:\\chef\" 2>nul");
    (void)__guest_cmd(container, "/c mkdir \"C:\\chef\\app\" 2>nul");
    (void)__guest_cmd(container, "/c mkdir \"C:\\chef\\pkgs\" 2>nul");

    // Build an activation script for non-application packages.
    // This is intentionally simple and convention-based.
    char* activate = platform_strdup("@echo off\r\n");
    if (activate == NULL) {
        return -1;
    }
    int have_activate = 0;

    for (uint32_t i = 0; i < params->layers_count; i++) {
        const struct chef_layer_descriptor* layer = &params->layers[i];
        if (layer->type != CHEF_LAYER_TYPE_VAFS_PACKAGE) {
            continue;
        }
        if (layer->source == NULL || layer->source[0] == '\0') {
            continue;
        }

        struct chef_package* pkg = NULL;
        if (chef_package_load(layer->source, &pkg, NULL, NULL, NULL) != 0 || pkg == NULL) {
            VLOG_ERROR("containerv", "cvd_create: failed to read package metadata for %s\n", layer->source);
            free(activate);
            return -1;
        }

        const char* publisher;
        const char* name;
        __derive_publisher_and_name(pkg, &publisher, &name);

        // If publisher is a prefix of name (publisher/name convention), duplicate the publisher.
        // Otherwise, publisher is a standalone string.
        char publisherBuf[CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX + 1];
        if (publisher == pkg->package) {
            const char* slash = strchr(pkg->package, '/');
            size_t ncopy = (slash != NULL) ? (size_t)(slash - pkg->package) : 0;
            if (ncopy == 0 || ncopy > CHEF_PACKAGE_PUBLISHER_NAME_LENGTH_MAX) {
                strcpy(publisherBuf, "default");
            } else {
                memcpy(publisherBuf, pkg->package, ncopy);
                publisherBuf[ncopy] = '\0';
            }
            publisher = publisherBuf;
        }

        const char* base = (pkg->type == CHEF_PACKAGE_TYPE_APPLICATION) ? "C:\\chef\\app" : "C:\\chef\\pkgs";
        char* guestRoot = __path_join3(base, publisher, name);
        if (guestRoot == NULL) {
            chef_package_free(pkg);
            free(activate);
            return -1;
        }

        char* tmp = __make_temp_dir();
        if (tmp == NULL) {
            free(guestRoot);
            chef_package_free(pkg);
            free(activate);
            return -1;
        }

        if (__unmkvafs_to_dir(layer->source, tmp) != 0) {
            VLOG_ERROR("containerv", "cvd_create: unmkvafs failed for %s\n", layer->source);
            platform_rmdir(tmp);
            free(tmp);
            free(guestRoot);
            chef_package_free(pkg);
            free(activate);
            return -1;
        }

        // Upload extracted content to guestRoot.
        if (__upload_tree_to_guest(container, tmp, guestRoot) != 0) {
            VLOG_ERROR("containerv", "cvd_create: failed to upload VAFS content into guest\n");
            platform_rmdir(tmp);
            free(tmp);
            free(guestRoot);
            chef_package_free(pkg);
            free(activate);
            return -1;
        }

        // If this is an application package, set the `current` junction.
        if (pkg->type == CHEF_PACKAGE_TYPE_APPLICATION) {
            if (__guest_set_current_app_junction(container, guestRoot) != 0) {
                VLOG_WARNING("containerv", "cvd_create: failed to set C:\\chef\\app\\current junction\n");
            }
        } else {
            // Extend activation script.
            const char* tmpl =
                "\r\n"
                "rem %s/%s\r\n"
                "set \"CHEF_PKG_%u=%s\"\r\n"
                "set \"PATH=%%CHEF_PKG_%u%%\\bin;%%PATH%%\"\r\n"
                "set \"INCLUDE=%%CHEF_PKG_%u%%\\include;%%INCLUDE%%\"\r\n"
                "set \"LIB=%%CHEF_PKG_%u%%\\lib;%%LIB%%\"\r\n";

            char block[2048];
            int rc = snprintf(block, sizeof(block), tmpl, publisher, name, (unsigned)i, guestRoot, (unsigned)i, (unsigned)i, (unsigned)i);
            if (rc > 0 && (size_t)rc < sizeof(block)) {
                size_t old_len = strlen(activate);
                size_t add_len = strlen(block);
                char* next = realloc(activate, old_len + add_len + 1);
                if (next != NULL) {
                    activate = next;
                    memcpy(activate + old_len, block, add_len + 1);
                    have_activate = 1;
                }
            }
        }

        platform_rmdir(tmp);
        free(tmp);
        free(guestRoot);
        chef_package_free(pkg);
    }

    if (have_activate) {
        // Upload activation script into guest.
        char* tmp = __make_temp_dir();
        if (tmp == NULL) {
            free(activate);
            return -1;
        }

        char* hostScript = strpathjoin(tmp, "activate.cmd");
        if (hostScript == NULL) {
            platform_rmdir(tmp);
            free(tmp);
            free(activate);
            return -1;
        }

        FILE* f = fopen(hostScript, "wb");
        if (f == NULL) {
            free(hostScript);
            platform_rmdir(tmp);
            free(tmp);
            free(activate);
            return -1;
        }
        fwrite(activate, 1, strlen(activate), f);
        fclose(f);

        const char* hp[1] = { hostScript };
        const char* gp[1] = { "C:\\chef\\activate.cmd" };
        int up = containerv_upload(container, hp, gp, 1);

        free(hostScript);
        platform_rmdir(tmp);
        free(tmp);

        if (up != 0) {
            free(activate);
            return -1;
        }
    }

    free(activate);
    return 0;
}
