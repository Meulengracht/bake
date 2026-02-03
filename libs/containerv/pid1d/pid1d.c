#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !defined(WIN32) && !defined(_WIN32) && !defined(__WIN32__) && !defined(__NT__)
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

#include <jansson.h>

#include <pid1_common.h>
#include <logging.h>

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
#include <windows.h>
#endif

typedef struct proc_entry {
    uint64_t id;
    pid1_process_handle_t handle;
    struct proc_entry* next;
} proc_entry_t;

static proc_entry_t* g_procs = NULL;
static uint64_t      g_next_id = 1;

static const unsigned char g_b64_enc_table[65] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static int __b64_dec_val(unsigned char c)
{
    if (c >= 'A' && c <= 'Z') return (int)(c - 'A');
    if (c >= 'a' && c <= 'z') return (int)(c - 'a') + 26;
    if (c >= '0' && c <= '9') return (int)(c - '0') + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return -2; // padding
    return -1;
}

static char* __base64_encode_alloc(const unsigned char* data, size_t len, size_t* out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (data == NULL && len != 0) {
        errno = EINVAL;
        return NULL;
    }

    size_t enc_len = ((len + 2) / 3) * 4;
    char* out = calloc(enc_len + 1, 1);
    if (out == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        unsigned char in0 = data[i];
        unsigned char in1 = (i + 1 < len) ? data[i + 1] : 0;
        unsigned char in2 = (i + 2 < len) ? data[i + 2] : 0;

        out[o++] = (char)g_b64_enc_table[in0 >> 2];
        out[o++] = (char)g_b64_enc_table[((in0 & 0x03) << 4) | (in1 >> 4)];
        if (i + 1 < len) {
            out[o++] = (char)g_b64_enc_table[((in1 & 0x0f) << 2) | (in2 >> 6)];
        } else {
            out[o++] = '=';
        }
        if (i + 2 < len) {
            out[o++] = (char)g_b64_enc_table[in2 & 0x3f];
        } else {
            out[o++] = '=';
        }
    }

    out[o] = '\0';
    if (out_len != NULL) {
        *out_len = o;
    }
    return out;
}

static unsigned char* __base64_decode_alloc(const char* b64, size_t b64_len, size_t* out_len)
{
    if (out_len != NULL) {
        *out_len = 0;
    }
    if (b64 == NULL) {
        errno = EINVAL;
        return NULL;
    }

    // Ignore whitespace; estimate max output.
    size_t max_out = (b64_len / 4) * 3 + 3;
    unsigned char* out = malloc(max_out);
    if (out == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    int quartet[4];
    int qn = 0;
    size_t o = 0;

    for (size_t i = 0; i < b64_len; ++i) {
        unsigned char c = (unsigned char)b64[i];
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            continue;
        }

        int v = __b64_dec_val(c);
        if (v == -1) {
            free(out);
            errno = EINVAL;
            return NULL;
        }

        quartet[qn++] = v;
        if (qn != 4) {
            continue;
        }
        qn = 0;

        // '=' padding represented as -2
        int v0 = quartet[0];
        int v1 = quartet[1];
        int v2 = quartet[2];
        int v3 = quartet[3];
        if (v0 < 0 || v1 < 0) {
            free(out);
            errno = EINVAL;
            return NULL;
        }

        out[o++] = (unsigned char)((v0 << 2) | (v1 >> 4));
        if (v2 != -2) {
            if (v2 < 0) {
                free(out);
                errno = EINVAL;
                return NULL;
            }
            out[o++] = (unsigned char)(((v1 & 0x0f) << 4) | (v2 >> 2));
            if (v3 != -2) {
                if (v3 < 0) {
                    free(out);
                    errno = EINVAL;
                    return NULL;
                }
                out[o++] = (unsigned char)(((v2 & 0x03) << 6) | v3);
            }
        }
    }

    if (qn != 0) {
        free(out);
        errno = EINVAL;
        return NULL;
    }

    if (out_len != NULL) {
        *out_len = o;
    }
    return out;
}

static int __mkdirs_for_file(const char* path)
{
    if (path == NULL || path[0] == 0) {
        errno = EINVAL;
        return -1;
    }

    const char* last_slash = strrchr(path, '/');
    const char* last_backslash = strrchr(path, '\\');
    const char* sep = last_slash;
    if (last_backslash != NULL && (sep == NULL || last_backslash > sep)) {
        sep = last_backslash;
    }

    if (sep == NULL) {
        return 0;
    }

    size_t dir_len = (size_t)(sep - path);
    if (dir_len == 0) {
        return 0;
    }

    char* dir = calloc(dir_len + 1, 1);
    if (dir == NULL) {
        errno = ENOMEM;
        return -1;
    }
    memcpy(dir, path, dir_len);
    dir[dir_len] = 0;

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    // CreateDirectoryA is not recursive; build up component-by-component.
    for (size_t i = 0; i < dir_len; ++i) {
        if (dir[i] == '/' || dir[i] == '\\') {
            char save = dir[i];
            dir[i] = 0;
            if (dir[0] != 0) {
                (void)CreateDirectoryA(dir, NULL);
            }
            dir[i] = save;
        }
    }
    if (dir[0] != 0) {
        (void)CreateDirectoryA(dir, NULL);
    }
    free(dir);
    return 0;
#else
    for (size_t i = 0; i < dir_len; ++i) {
        if (dir[i] == '/') {
            dir[i] = 0;
            if (dir[0] != 0) {
                (void)mkdir(dir, 0755);
            }
            dir[i] = '/';
        }
    }
    (void)mkdir(dir, 0755);
    free(dir);
    return 0;
#endif
}

static void __procs_free_all(void)
{
    proc_entry_t* it = g_procs;
    while (it != NULL) {
        proc_entry_t* next = it->next;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        if (it->handle != NULL) {
            CloseHandle(it->handle);
        }
#endif
        free(it);
        it = next;
    }
    g_procs = NULL;
}

static proc_entry_t* __procs_find(uint64_t id)
{
    for (proc_entry_t* it = g_procs; it != NULL; it = it->next) {
        if (it->id == id) {
            return it;
        }
    }
    return NULL;
}

static void __procs_remove(uint64_t id)
{
    proc_entry_t** cur = &g_procs;
    while (*cur != NULL) {
        if ((*cur)->id == id) {
            proc_entry_t* to_free = *cur;
            *cur = (*cur)->next;
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
            if (to_free->handle != NULL) {
                CloseHandle(to_free->handle);
            }
#endif
            free(to_free);
            return;
        }
        cur = &(*cur)->next;
    }
}

static int __write_json_line(json_t* obj)
{
    char* dumped = json_dumps(obj, JSON_COMPACT);
    if (dumped == NULL) {
        return -1;
    }

    fputs(dumped, stdout);
    fputc('\n', stdout);
    fflush(stdout);
    free(dumped);
    return 0;
}

static int __respond_ok(json_t* extra)
{
    json_t* resp = json_object();
    if (resp == NULL) {
        return -1;
    }
    json_object_set_new(resp, "ok", json_true());

    if (extra != NULL) {
        const char* key;
        json_t* value;
        json_object_foreach(extra, key, value)
        {
            json_object_set(resp, key, value);
        }
    }

    int rc = __write_json_line(resp);
    json_decref(resp);
    return rc;
}

static int __respond_err(const char* msg)
{
    json_t* resp = json_object();
    if (resp == NULL) {
        return -1;
    }
    json_object_set_new(resp, "ok", json_false());
    json_object_set_new(resp, "errno", json_integer(errno));
    json_object_set_new(resp, "err", json_string(msg ? msg : "error"));

    int rc = __write_json_line(resp);
    json_decref(resp);
    return rc;
}

static const char* __json_get_string(json_t* obj, const char* key)
{
    json_t* v = json_object_get(obj, key);
    if (!json_is_string(v)) {
        return NULL;
    }
    return json_string_value(v);
}

static int __json_get_bool(json_t* obj, const char* key, int default_value)
{
    json_t* v = json_object_get(obj, key);
    if (v == NULL) {
        return default_value;
    }
    if (json_is_true(v)) {
        return 1;
    }
    if (json_is_false(v)) {
        return 0;
    }
    return default_value;
}

static int __json_to_string_array(json_t* arr, const char*** out)
{
    if (arr == NULL || json_is_null(arr)) {
        *out = NULL;
        return 0;
    }

    if (!json_is_array(arr)) {
        errno = EINVAL;
        return -1;
    }

    size_t n = json_array_size(arr);
    const char** v = calloc(n + 1, sizeof(char*));
    if (v == NULL) {
        errno = ENOMEM;
        return -1;
    }

    for (size_t i = 0; i < n; ++i) {
        json_t* item = json_array_get(arr, i);
        if (!json_is_string(item)) {
            free(v);
            errno = EINVAL;
            return -1;
        }
        v[i] = json_string_value(item);
    }
    v[n] = NULL;
    *out = v;
    return 0;
}

static void __free_string_array(const char** arr)
{
    free((void*)arr);
}

static int __handle_ping(void)
{
    json_t* extra = json_object();
    if (extra == NULL) {
        return __respond_ok(NULL);
    }
    json_object_set_new(extra, "service", json_string("pid1d"));
    json_object_set_new(extra, "version", json_integer(1));
    int rc = __respond_ok(extra);
    json_decref(extra);
    return rc;
}

static int __handle_spawn(json_t* req)
{
    const char* command = __json_get_string(req, "command");
    const char* cwd = __json_get_string(req, "cwd");

    json_t* args_json = json_object_get(req, "args");
    json_t* env_json = json_object_get(req, "env");

    const char** args = NULL;
    const char** env = NULL;

    if (command == NULL) {
        errno = EINVAL;
        return __respond_err("missing command");
    }

    if (__json_to_string_array(args_json, &args) != 0) {
        return __respond_err("invalid args");
    }

    // Default args: [command]
    const char* default_args_buf[2] = {0};
    const char* const* effective_args = args;
    if (effective_args == NULL) {
        default_args_buf[0] = command;
        default_args_buf[1] = NULL;
        effective_args = default_args_buf;
    }

    if (__json_to_string_array(env_json, &env) != 0) {
        __free_string_array(args);
        return __respond_err("invalid env");
    }

    pid1_process_options_t opts;
    memset(&opts, 0, sizeof(opts));
    opts.command = command;
    opts.args = effective_args;
    opts.environment = env;
    opts.working_directory = cwd;
    opts.log_path = NULL;
    opts.wait_for_exit = __json_get_bool(req, "wait", 0);
    opts.forward_signals = 1;

    pid1_process_handle_t handle;
    if (pid1_spawn_process(&opts, &handle) != 0) {
        __free_string_array(args);
        __free_string_array(env);
        return __respond_err("spawn failed");
    }

    proc_entry_t* e = calloc(1, sizeof(proc_entry_t));
    if (e == NULL) {
        // Try best-effort kill to avoid leaking.
        (void)pid1_kill_process(handle);
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
        if (handle != NULL) {
            CloseHandle(handle);
        }
#endif
        __free_string_array(args);
        __free_string_array(env);
        errno = ENOMEM;
        return __respond_err("oom");
    }

    e->id = g_next_id++;
    e->handle = handle;
    e->next = g_procs;
    g_procs = e;

    json_t* extra = json_object();
    if (extra != NULL) {
        json_object_set_new(extra, "id", json_integer((json_int_t)e->id));
    }
    int rc = __respond_ok(extra);
    if (extra != NULL) {
        json_decref(extra);
    }

    __free_string_array(args);
    __free_string_array(env);
    return rc;
}

static int __handle_wait(json_t* req)
{
    json_t* idv = json_object_get(req, "id");
    if (!json_is_integer(idv)) {
        errno = EINVAL;
        return __respond_err("missing id");
    }

    uint64_t id = (uint64_t)json_integer_value(idv);
    proc_entry_t* e = __procs_find(id);
    if (e == NULL) {
        errno = ESRCH;
        return __respond_err("unknown id");
    }

    int exit_code = 0;
    if (pid1_wait_process(e->handle, &exit_code) != 0) {
        return __respond_err("wait failed");
    }

    __procs_remove(id);

    json_t* extra = json_object();
    if (extra != NULL) {
        json_object_set_new(extra, "exit_code", json_integer(exit_code));
    }
    int rc = __respond_ok(extra);
    if (extra != NULL) {
        json_decref(extra);
    }
    return rc;
}

static int __handle_kill(json_t* req)
{
    json_t* idv = json_object_get(req, "id");
    if (!json_is_integer(idv)) {
        errno = EINVAL;
        return __respond_err("missing id");
    }

    int reap = __json_get_bool(req, "reap", 0);

    uint64_t id = (uint64_t)json_integer_value(idv);
    proc_entry_t* e = __procs_find(id);
    if (e == NULL) {
        errno = ESRCH;
        return __respond_err("unknown id");
    }

    if (pid1_kill_process(e->handle) != 0) {
        return __respond_err("kill failed");
    }

    if (reap) {
        (void)pid1_wait_process(e->handle, NULL);
        __procs_remove(id);
    }

    // Caller may still want to wait; keep it tracked unless reaped.
    return __respond_ok(NULL);
}

static int __handle_file_write_b64(json_t* req)
{
    const char* path = __json_get_string(req, "path");
    const char* data = __json_get_string(req, "data");
    int append = __json_get_bool(req, "append", 0);
    int mkdirs = __json_get_bool(req, "mkdirs", 0);

    if (path == NULL || data == NULL) {
        errno = EINVAL;
        return __respond_err("missing path/data");
    }

    if (mkdirs) {
        (void)__mkdirs_for_file(path);
    }

    size_t data_len = strlen(data);
    size_t decoded_len = 0;
    unsigned char* decoded = __base64_decode_alloc(data, data_len, &decoded_len);
    if (decoded == NULL) {
        return __respond_err("base64 decode failed");
    }

    FILE* f = fopen(path, append ? "ab" : "wb");
    if (f == NULL) {
        free(decoded);
        return __respond_err("open failed");
    }

    size_t written = 0;
    if (decoded_len > 0) {
        written = fwrite(decoded, 1, decoded_len, f);
        if (written != decoded_len) {
            fclose(f);
            free(decoded);
            errno = EIO;
            return __respond_err("write failed");
        }
    }

    fclose(f);
    free(decoded);

    json_t* extra = json_object();
    if (extra != NULL) {
        json_object_set_new(extra, "bytes", json_integer((json_int_t)written));
    }
    int rc = __respond_ok(extra);
    if (extra != NULL) {
        json_decref(extra);
    }
    return rc;
}

static int __handle_file_read_b64(json_t* req)
{
    const char* path = __json_get_string(req, "path");
    json_t* offv = json_object_get(req, "offset");
    json_t* maxv = json_object_get(req, "max_bytes");

    if (path == NULL || !json_is_integer(offv) || !json_is_integer(maxv)) {
        errno = EINVAL;
        return __respond_err("missing path/offset/max_bytes");
    }

    uint64_t offset = (uint64_t)json_integer_value(offv);
    uint64_t max_bytes = (uint64_t)json_integer_value(maxv);
    if (max_bytes == 0 || max_bytes > 64 * 1024) {
        errno = EINVAL;
        return __respond_err("invalid max_bytes");
    }

    FILE* f = fopen(path, "rb");
    if (f == NULL) {
        return __respond_err("open failed");
    }

#if defined(WIN32) || defined(_WIN32) || defined(__WIN32__) || defined(__NT__)
    if (_fseeki64(f, (long long)offset, SEEK_SET) != 0) {
#else
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) {
#endif
        fclose(f);
        return __respond_err("seek failed");
    }

    unsigned char* buf = malloc((size_t)max_bytes);
    if (buf == NULL) {
        fclose(f);
        errno = ENOMEM;
        return __respond_err("oom");
    }

    size_t nread = fread(buf, 1, (size_t)max_bytes, f);
    int eof = feof(f) ? 1 : 0;
    fclose(f);

    size_t b64_len = 0;
    char* b64 = __base64_encode_alloc(buf, nread, &b64_len);
    free(buf);
    if (b64 == NULL) {
        return __respond_err("base64 encode failed");
    }

    json_t* extra = json_object();
    if (extra != NULL) {
        json_object_set_new(extra, "bytes", json_integer((json_int_t)nread));
        json_object_set_new(extra, "eof", eof ? json_true() : json_false());
        json_object_set_new(extra, "data", json_string(b64));
    }
    free(b64);

    int rc = __respond_ok(extra);
    if (extra != NULL) {
        json_decref(extra);
    }
    return rc;
}

static int __dispatch(json_t* req)
{
    const char* op = __json_get_string(req, "op");
    if (op == NULL) {
        errno = EINVAL;
        return __respond_err("missing op");
    }

    if (strcmp(op, "ping") == 0) {
        return __handle_ping();
    }
    if (strcmp(op, "spawn") == 0) {
        return __handle_spawn(req);
    }
    if (strcmp(op, "wait") == 0) {
        return __handle_wait(req);
    }
    if (strcmp(op, "kill") == 0) {
        return __handle_kill(req);
    }
    if (strcmp(op, "file_write_b64") == 0) {
        return __handle_file_write_b64(req);
    }
    if (strcmp(op, "file_read_b64") == 0) {
        return __handle_file_read_b64(req);
    }

    errno = EINVAL;
    return __respond_err("unknown op");
}

int main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    // pid1 logging is optional; keep it on stderr.
    (void)pid1_log_init(NULL, PID1_LOG_INFO);

    if (pid1_init() != 0) {
        (void)__respond_err("pid1_init failed");
        return 1;
    }

    char line[64 * 1024];
    while (fgets(line, (int)sizeof(line), stdin) != NULL) {
        // Trim trailing newline(s)
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[n - 1] = '\0';
            n--;
        }

        if (n == 0) {
            continue;
        }

        json_error_t jerr;
        json_t* req = json_loads(line, 0, &jerr);
        if (req == NULL || !json_is_object(req)) {
            errno = EINVAL;
            (void)__respond_err("invalid json");
            if (req != NULL) {
                json_decref(req);
            }
            continue;
        }

        (void)__dispatch(req);
        json_decref(req);
    }

    __procs_free_all();
    (void)pid1_cleanup();
    (void)pid1_log_close();

    return 0;
}
