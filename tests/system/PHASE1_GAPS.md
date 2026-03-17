# Phase 1 Runtime Test — Implementation Gaps

This document is a **problem statement for a follow-up implementation task**.
It describes exactly what needs to be implemented in `daemons/served` so that
the `hello-runtime.sh` system test can exercise the full build → install → run
workflow with a locally built `.pack` file.

---

## Context

`tests/system/cases/hello-runtime.sh` exercises:

1. Build `examples/recipes/hello-world` → produces `hello-world-*.pack`
2. Start `served --root <tmpdir>` (isolated state)
3. `serve install ./hello-world-*.pack -P ci-test-proof`
4. `serve list` → assert hello-world appears
5. Run the generated wrapper `<served-root>/chef/bin/hello`
6. Assert exit 0 and stdout contains `"hello world"`

Steps 1–3 (infrastructure) pass.  Steps 4–6 (install + run) fail because of
the gaps described below.

---

## Gap 1 — `served` silently drops the local file path from install requests

### Where

`daemons/served/api.c` → `chef_served_install_invocation()`

### What happens

When `serve install ./file.pack` is called:

1. The `serve` CLI resolves the absolute path and sets
   `installOptions.path = "/abs/path/to/file.pack"`, leaving
   `installOptions.package = NULL`.
2. `served` receives the request.  The debug log line prints the path:
   ```c
   VLOG_DEBUG("api", "chef_served_install_invocation(publisher=%s, path=%s)\n",
              options->package, options->path);
   ```
3. But when the transaction state is created, **only `options->package`,
   `options->channel`, and `options->revision` are stored**; `options->path`
   is discarded:
   ```c
   served_state_transaction_state_new(
       transactionId,
       &(struct state_transaction){
           .name    = options->package,   // NULL for local-file installs
           .channel = options->channel,
           .revision = options->revision,
       }
   );
   ```
4. The result: the transaction's `name` field is `NULL`.

### Cascade failures

With a `NULL` name the state machine fails at every subsequent step:

| State | Failure |
|---|---|
| DOWNLOAD | `package.name = state->name` → `NULL` passed to `store_ensure_package()` → crash/error |
| VERIFY | `utils_split_package_name(name)` → fails on `NULL` |
| INSTALL | `utils_split_package_name(state->name)` → fails on `NULL`; `store_package_path()` also fails |
| DEPENDENCIES | `utils_split_package_name(state->name)` → fails on `NULL` |

### Fix required

#### 1a. Add a `path` field to `struct state_transaction`

File: `daemons/served/include/state.h`

```c
struct state_transaction {
    unsigned int id;

    const char* name;       // publisher/package (NULL for local-file installs)
    const char* channel;
    int         revision;
    const char* path;       // NEW: absolute local file path (may be NULL)

    struct state_transaction_log* logs;
    int                           logs_count;
};
```

#### 1b. Persist `path` to the SQLite `transactions_state` table

File: `daemons/served/state/state.c`

Add a `path TEXT` column to the `transactions_state` table schema and update
all INSERT, UPDATE, SELECT, and load functions to handle the new column.

Key locations to update:
- `g_transactionsStateTableSQL` (schema DDL, ~line 110)
- `__execute_add_tx_state_op()` (INSERT, ~line 1276)
- `__load_transaction_states_from_db()` (SELECT, ~line 703)
- Any UPDATE functions for transaction state

#### 1c. Populate `path` in the API handler

File: `daemons/served/api.c`

```c
served_state_transaction_state_new(
    transactionId,
    &(struct state_transaction){
        .name    = options->package,   // still NULL for local-file installs
        .channel = options->channel,
        .revision = options->revision,
        .path    = options->path,      // NEW: absolute local file path
    }
);
```

Also fix the display name and description buffers which currently format
`options->package` (which can be `NULL`).  When path is set and package is
NULL, derive a display name from the filename:

```c
const char* displayName = options->package;
if (displayName == NULL && options->path != NULL) {
    // Use the filename portion as the display name
    displayName = strrchr(options->path, '/');
    displayName = (displayName != NULL) ? displayName + 1 : options->path;
}
snprintf(nameBuffer, sizeof(nameBuffer), "Install via API (%s)",
         displayName ? displayName : "(unknown)");
```

---

## Gap 2 — DOWNLOAD state has no local-file bypass

### Where

`daemons/served/states/download.c` → `served_handle_state_download()`

### What happens

The DOWNLOAD state always calls `store_ensure_package()`, which either:
- looks up the package in the local store inventory, **or**
- downloads it from the remote store via the configured backend

There is no code path for "the caller already has a local file".

### Fix required

File: `daemons/served/states/download.c`

When `state->path` is non-NULL, bypass `store_ensure_package()` and instead
register the local file directly in the store inventory so that the subsequent
VERIFY and INSTALL states can find it via `store_package_path()`.

Pseudocode:

```c
if (state->path != NULL) {
    // Local-file install: register the file in the store inventory
    // so downstream states can use store_package_path() to find it.
    status = store_add_local_package(
        state->name,      // publisher/package  -- may need to be read from the .pack
        state->path,
        state->revision   // may be 0; read from .pack if needed
    );
    if (status) {
        TXLOG_ERROR(transaction, "Failed to register local package: %s", strerror(errno));
        served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_FAILED);
        return SM_ACTION_CONTINUE;
    }
    TXLOG_INFO(transaction, "Local package registered, skipping download");
    served_sm_post_event(&transaction->sm, SERVED_TX_EVENT_OK);
    return SM_ACTION_CONTINUE;
}
// ... existing remote download path ...
```

Note: if `state->name` is NULL (local-file install where the server does not
yet know the name), the DOWNLOAD state must read the package name from the
`.pack` file itself using `chef_package_load()` and update `state->name`
before registering with the inventory.  The transaction display name should
also be updated at this point.

There is currently no `store_add_local_package()` function; it can be
implemented as a thin wrapper around the existing `inventory_add()` function
in `libs/store/`.

---

## Gap 3 — VERIFY state requires proof from the remote store

### Where

`daemons/served/states/verify.c` → `served_handle_state_verify()`
`daemons/served/utils/proofs.c` → `utils_verify_package()`

### What happens

Verification calls `utils_verify_package()` which:
1. Calls `store_package_path()` to find the local package
2. Calls `store_proof_ensure()` to download the publisher's signing certificate
   from the remote store
3. Verifies the package's SHA-512 hash against the publisher's proof

For a locally built package (from `bake build`), there is no entry in the
remote store's proof database, so `store_proof_ensure()` will fail with a
network/not-found error.

### Fix required

When a package is installed from a local file path (i.e. `state->path` is
non-NULL), the VERIFY state should skip the remote-proof verification and
instead perform only a basic integrity check (e.g. verify the `.pack` file is
readable and well-formed).

The `-P proof` argument sent by `serve install` (`installOptions.proof`) is
already passed through the protocol but is not yet stored or used by served.
A simple approach: if `state->path` is non-NULL (local-file install), skip
full proof verification and emit a warning log entry.

---

## Gap 4 — DEPENDENCIES state requires the base rootfs to be in the store

### Where

`daemons/served/states/dependencies.c` → `served_handle_state_dependencies()`

### What happens

The `hello-world` recipe specifies `base: ubuntu:24`.  The DEPENDENCIES state:
1. Reads the `.pack` file's `base` field
2. Converts `ubuntu:24` → `vali/ubuntu-24` via `utils_base_to_store_id()`
3. Schedules a recursive install transaction for `vali/ubuntu-24` (downloading
   it from the remote store)

In an isolated test environment with no store access, this recursive install
will fail trying to download `vali/ubuntu-24`.

### Fix required

This is the most complex gap and has two viable approaches:

**Option A (recommended for Phase 1):** Provide the base rootfs as a local
`.pack` file that is pre-installed before the test.  If `served --root
<tmpdir>` is running with an already-installed `vali/ubuntu-24` in its state,
the DEPENDENCIES state will find it and skip the download.

The `bake build` process downloads the base rootfs; the CI runner already has
the ubuntu:24 base image cached somewhere during the build.  The test harness
could pre-install it into the test served root before installing hello-world.

**Option B:** Add a mechanism to supply local base rootfs overrides to
`served` (e.g. a `--base-path publisher/name=/local/path.pack` argument) so
that the DEPENDENCIES state can install bases from local files using the same
local-file install path described in Gaps 1–3.

---

## Summary of files to change

| File | Change |
|---|---|
| `daemons/served/include/state.h` | Add `path` field to `struct state_transaction` |
| `daemons/served/state/state.c` | Add `path` column to `transactions_state` schema; update all DB read/write functions |
| `daemons/served/api.c` | Populate `path` from `options->path`; fix NULL display name |
| `daemons/served/states/download.c` | Add local-file bypass when `state->path != NULL` |
| `daemons/served/states/verify.c` | Skip remote proof check for local-file installs |
| `libs/store/store.c` (or `libs/store/inventory.c`) | Add `store_add_local_package()` helper |
| `libs/store/include/chef/store.h` | Declare new `store_add_local_package()` function |

---

## Acceptance criteria

Once these gaps are closed, `tests/system/cases/hello-runtime.sh` should pass
**completely** (all 10 steps, including install → list → run → assert output).

The specific assertion in the test:

```bash
assert_contains "$run_output" "hello world" "hello-world stdout"
```

should succeed, proving the full build → install → run workflow works
end-to-end using only locally built artifacts.
