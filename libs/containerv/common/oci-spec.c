#include "oci-spec.h"

#include <ctype.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define CV_STRDUP _strdup
#else
#define CV_STRDUP strdup
#endif

static int __appendf(char** buf, size_t* cap, size_t* len, const char* fmt, ...)
{
    if (buf == NULL || cap == NULL || len == NULL || fmt == NULL) {
        return -1;
    }

    va_list args;
    va_start(args, fmt);

    for (;;) {
        if (*buf == NULL) {
            *cap = 4096;
            *len = 0;
            *buf = (char*)calloc(*cap, 1);
            if (*buf == NULL) {
                va_end(args);
                return -1;
            }
        }

        va_list args2;
        va_copy(args2, args);
        int n = vsnprintf(*buf + *len, *cap - *len, fmt, args2);
        va_end(args2);

        if (n < 0) {
            va_end(args);
            return -1;
        }

        if (*len + (size_t)n < *cap) {
            *len += (size_t)n;
            va_end(args);
            return 0;
        }

        size_t new_cap = (*cap) * 2;
        while (*len + (size_t)n >= new_cap) {
            new_cap *= 2;
        }

        char* tmp = (char*)realloc(*buf, new_cap);
        if (tmp == NULL) {
            va_end(args);
            return -1;
        }

        memset(tmp + *cap, 0, new_cap - *cap);
        *buf = tmp;
        *cap = new_cap;
    }
}

static char* __json_escape_utf8(const char* s)
{
    if (s == NULL) {
        return CV_STRDUP("");
    }

    size_t in_len = strlen(s);
    size_t cap = (in_len * 6) + 1;
    char* out = (char*)calloc(cap, 1);
    if (out == NULL) {
        return NULL;
    }

    size_t j = 0;
    for (size_t i = 0; i < in_len; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (c == '"' || c == '\\') {
            out[j++] = '\\';
            out[j++] = (char)c;
        } else if (c == '\b') {
            out[j++] = '\\';
            out[j++] = 'b';
        } else if (c == '\f') {
            out[j++] = '\\';
            out[j++] = 'f';
        } else if (c == '\n') {
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (c == '\r') {
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (c == '\t') {
            out[j++] = '\\';
            out[j++] = 't';
        } else if (c < 0x20) {
            int n = snprintf(out + j, cap - j, "\\u%04x", (unsigned int)c);
            if (n < 0) {
                free(out);
                return NULL;
            }
            j += (size_t)n;
        } else {
            out[j++] = (char)c;
        }
    }

    out[j] = '\0';
    return out;
}

static int __env_has_key_case_insensitive(const char* const* envv, const char* key)
{
    if (envv == NULL || key == NULL || key[0] == '\0') {
        return 0;
    }

    size_t key_len = strlen(key);

    for (int i = 0; envv[i] != NULL; ++i) {
        const char* kv = envv[i];
        if (kv == NULL) {
            continue;
        }

        // Compare KEY + '=' case-insensitively.
        for (size_t j = 0; j < key_len; ++j) {
            unsigned char a = (unsigned char)kv[j];
            unsigned char b = (unsigned char)key[j];
            if (a == '\0') {
                goto next;
            }
            if (tolower(a) != tolower(b)) {
                goto next;
            }
        }

        if (kv[key_len] == '=') {
            return 1;
        }

    next:
        (void)0;
    }

    return 0;
}

int containerv_oci_build_linux_spec_json(
    const struct containerv_oci_linux_spec_params* params,
    char** out_json_utf8)
{
    if (out_json_utf8 == NULL) {
        return -1;
    }
    *out_json_utf8 = NULL;

    if (params == NULL || params->root_path == NULL || params->root_path[0] == '\0') {
        return -1;
    }

    const char* args_json = (params->args_json != NULL && params->args_json[0] != '\0') ? params->args_json : "[]";
    const char* cwd = (params->cwd != NULL && params->cwd[0] != '\0') ? params->cwd : "/";

    char* esc_root = __json_escape_utf8(params->root_path);
    char* esc_cwd = __json_escape_utf8(cwd);
    char* esc_hostname = (params->hostname != NULL && params->hostname[0] != '\0')
        ? __json_escape_utf8(params->hostname)
        : NULL;

    if (esc_root == NULL || esc_cwd == NULL || (params->hostname != NULL && params->hostname[0] != '\0' && esc_hostname == NULL)) {
        free(esc_root);
        free(esc_cwd);
        free(esc_hostname);
        return -1;
    }

    // Build env array
    char* env_arr = NULL;
    size_t env_arr_cap = 0;
    size_t env_arr_len = 0;

    if (__appendf(&env_arr, &env_arr_cap, &env_arr_len, "[") != 0) {
        free(esc_root);
        free(esc_cwd);
        free(esc_hostname);
        free(env_arr);
        return -1;
    }

    int first_env = 1;
    if (!__env_has_key_case_insensitive(params->envv, "PATH")) {
        char* esc = __json_escape_utf8("PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin");
        if (esc == NULL) {
            free(esc_root);
            free(esc_cwd);
            free(esc_hostname);
            free(env_arr);
            return -1;
        }
        if (__appendf(&env_arr, &env_arr_cap, &env_arr_len, "%s\"%s\"", first_env ? "" : ",", esc) != 0) {
            free(esc_root);
            free(esc_cwd);
            free(esc_hostname);
            free(esc);
            free(env_arr);
            return -1;
        }
        free(esc);
        first_env = 0;
    }

    if (params->envv != NULL) {
        for (int i = 0; params->envv[i] != NULL; ++i) {
            char* esc = __json_escape_utf8(params->envv[i]);
            if (esc == NULL) {
                free(esc_root);
                free(esc_cwd);
                free(esc_hostname);
                free(env_arr);
                return -1;
            }
            if (__appendf(&env_arr, &env_arr_cap, &env_arr_len, "%s\"%s\"", first_env ? "" : ",", esc) != 0) {
                free(esc_root);
                free(esc_cwd);
                free(esc_hostname);
                free(esc);
                free(env_arr);
                return -1;
            }
            free(esc);
            first_env = 0;
        }
    }

    if (__appendf(&env_arr, &env_arr_cap, &env_arr_len, "]") != 0) {
        free(esc_root);
        free(esc_cwd);
        free(esc_hostname);
        free(env_arr);
        return -1;
    }

    // Compose spec
    char* spec = NULL;
    size_t spec_cap = 0;
    size_t spec_len = 0;

    if (__appendf(
            &spec,
            &spec_cap,
            &spec_len,
            "{"
            "\"ociVersion\":\"1.0.2\""
            ",\"process\":{\"terminal\":false,\"cwd\":\"%s\",\"args\":%s,\"env\":%s,\"user\":{\"uid\":0,\"gid\":0}}"
            ",\"root\":{\"path\":\"%s\",\"readonly\":false}"
            "%s"
            ",\"mounts\":["
            "{\"destination\":\"/proc\",\"type\":\"proc\",\"source\":\"proc\",\"options\":[\"nosuid\",\"noexec\",\"nodev\"]},"
            "{\"destination\":\"/dev\",\"type\":\"tmpfs\",\"source\":\"tmpfs\",\"options\":[\"nosuid\",\"strictatime\",\"mode=755\",\"size=65536k\"]},"
            "{\"destination\":\"/dev/pts\",\"type\":\"devpts\",\"source\":\"devpts\",\"options\":[\"nosuid\",\"noexec\",\"newinstance\",\"ptmxmode=0666\",\"mode=0620\",\"gid=5\"]},"
            "{\"destination\":\"/dev/shm\",\"type\":\"tmpfs\",\"source\":\"shm\",\"options\":[\"nosuid\",\"noexec\",\"nodev\",\"mode=1777\",\"size=65536k\"]},"
            "{\"destination\":\"/sys\",\"type\":\"sysfs\",\"source\":\"sysfs\",\"options\":[\"nosuid\",\"noexec\",\"nodev\",\"ro\"]}"
            "]"
            ",\"linux\":{\"namespaces\":[{\"type\":\"pid\"},{\"type\":\"ipc\"},{\"type\":\"uts\"},{\"type\":\"mount\"},{\"type\":\"network\"}]}"
            "}",
            esc_cwd,
            args_json,
            env_arr,
            esc_root,
            (esc_hostname != NULL) ? ",\"hostname\":\"" : "",
            (esc_hostname != NULL) ? esc_hostname : "") != 0) {
        free(esc_root);
        free(esc_cwd);
        free(esc_hostname);
        free(env_arr);
        free(spec);
        return -1;
    }

    if (esc_hostname != NULL) {
        if (__appendf(&spec, &spec_cap, &spec_len, "\"") != 0) {
            free(esc_root);
            free(esc_cwd);
            free(esc_hostname);
            free(env_arr);
            free(spec);
            return -1;
        }
    }

    free(esc_root);
    free(esc_cwd);
    free(esc_hostname);
    free(env_arr);

    *out_json_utf8 = spec;
    return 0;
}
