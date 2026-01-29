#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

    uint64_t id = (uint64_t)json_integer_value(idv);
    proc_entry_t* e = __procs_find(id);
    if (e == NULL) {
        errno = ESRCH;
        return __respond_err("unknown id");
    }

    if (pid1_kill_process(e->handle) != 0) {
        return __respond_err("kill failed");
    }

    // Caller may still want to wait; keep it tracked.
    return __respond_ok(NULL);
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
