# System Test Suite — Phase 1

This directory contains the Phase 1 system-test harness for `bake`.  The tests
exercise the real binaries as subprocesses, assert on filesystem artifacts and
command output, and preserve logs for CI debugging.

---

## Quick start

```bash
# Build the project first
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j$(nproc)

# Run all system tests (requires passwordless sudo for cvd/served)
bash tests/system/run.sh

# Run a single test case
bash tests/system/run.sh smoke-cvd
bash tests/system/run.sh hello-build
bash tests/system/run.sh hello-runtime
```

### Required system dependencies

The tests run on Linux.  The following are needed in addition to the project's
own build dependencies:

| Dependency | Purpose |
|---|---|
| `sudo` (passwordless) | Starting `cvd` and `served` as root |
| `libfuse3` / FUSE kernel module | VaFS mounting inside `served` |
| `libsqlite3` | `served` state database |
| `libssl` / `libcrypto` | Package proof verification |

---

## Directory layout

```text
tests/
  system/
    README.md          ← this file
    run.sh             ← orchestrator; run this to execute the suite
    lib/
      env.sh           ← binary paths and environment setup
      process.sh       ← command execution and socket-readiness helpers
      daemon.sh        ← daemon start / stop / readiness checks
      assert.sh        ← lightweight assertion functions
      cleanup.sh       ← teardown, log capture, and temp-dir removal
    cases/
      smoke-cvd.sh     ← smoke test: cvd starts and stays alive
      hello-build.sh   ← build test: hello-world produces a .pack artifact
      hello-runtime.sh ← runtime test: build → install → run hello-world
```

---

## Separation of concerns

| Layer | Responsibility |
|---|---|
| `lib/` | Reusable helpers shared by all test cases |
| `cases/` | One workflow per file; self-contained, sources `lib/` |
| `run.sh` | Discovery, orchestration, result summary |

---

## Test cases

### `smoke-cvd.sh`

Verifies that the `cvd` container daemon starts and remains healthy.

- Starts `cvd` via `sudo`
- Polls with `kill -0` until the process is alive (bounded retry loop)
- Holds for 2 seconds to confirm stability
- Tears down cleanly

Exit codes: `0` = pass, `1` = fail.

---

### `hello-build.sh`

Verifies that `examples/recipes/hello-world` can be built end-to-end.

- Starts `cvd` (required by `bake`)
- Copies `hello.yaml` and `hello-world/` to an isolated temp directory
- Runs `bake build hello.yaml` with a 20-minute timeout
- Asserts at least one non-empty `.pack` file was produced

Exit codes: `0` = pass, `1` = fail.

**Expected artifact:**

```
<work-dir>/hello-world-1.0.0-<arch>.<revision>.pack
```

The `.pack` file is a VaFS-based binary package containing the compiled
`hello-world` executable, metadata, and command descriptors.

---

### `hello-runtime.sh`

End-to-end runtime workflow: build → install → run.

| Step | Description | Status |
|------|-------------|--------|
| 1 | Create isolated temp environment | ✅ implemented |
| 2 | Start `cvd` | ✅ implemented |
| 3 | Build `hello-world` | ✅ implemented |
| 4 | Start `served` with `--root <tmpdir>` | ✅ implemented |
| 5 | Wait for `/tmp/served` socket | ✅ implemented |
| 6 | Verify `serve list` succeeds | ✅ implemented |
| 7 | `serve install <pack>` | ⚠️ aspirational — see Known Limitations |
| 8 | Verify package in `serve list` | ⚠️ aspirational |
| 9 | Run wrapper script | ⚠️ aspirational |
| 10 | Assert exit 0 and output `"hello world"` | ⚠️ aspirational |

Exit codes: `0` = full pass, `1` = infrastructure failure, `2` = aspirational
steps not yet implemented.

---

## Repo-specific implementation findings

The following questions were investigated during harness development.

### 1. What artifact does `bake build` produce for hello-world?

`bake build hello.yaml` (run from the directory containing `hello.yaml`)
produces one or more `.pack` files in the current working directory:

```
hello-world-1.0.0-linux-amd64.<revision>.pack
```

The filename follows the pattern `<name>-<version>-<platform>-<arch>.<rev>.pack`.
Discovery: use `"$WORK_DIR"/*.pack` glob after the build.

### 2. How should `served` be configured for test-local state?

`served` accepts a `--root <path>` argument that relocates **all** filesystem
state under the given directory:

| Path (relative to root) | Purpose |
|---|---|
| `var/chef/state.db` | SQLite transaction/package database |
| `var/chef/packs/` | Stored `.pack` files |
| `usr/share/chef/` | Application data directories |
| `chef/bin/` | Generated wrapper scripts |
| `etc/profile.d/chef.sh` | Shell profile script (skipped on snap builds) |

Example:

```bash
sudo served --root /tmp/test-served-root-$$
```

The Unix socket path (`/tmp/served`) is **hardcoded** in both the daemon and
the client — it cannot be changed without modifying the source.  Tests must
therefore run `served` instances sequentially and clean up `/tmp/served`
between runs.

### 3. How should the `serve` CLI be pointed at the test instance of `served`?

The socket path `/tmp/served` is hardcoded in `tools/serve/commands/client.c`.
There is **no environment variable or config flag** to redirect it.

Practical implications:
- Tests must be run sequentially (no parallel `served` instances).
- Each test must remove `/tmp/served` during teardown.

### 4. What is the correct way to run the installed hello-world application?

After a successful `serve install`, `served` generates a wrapper shell script
at:

```
<served-root>/chef/bin/hello
```

The wrapper calls `serve-exec` (a setuid helper located next to the `served`
binary) which launches the application inside its container:

```sh
#!/bin/sh
<serve-exec-path> --container <publisher>.<package> \
    --path /usr/bin/hello-world --wdir / 
```

**Expected output:**  `hello world`  (no trailing newline — the `main.c`
uses `printf("hello world")` without `\n`).

### 5. Known limitations / implementation gaps

#### Local-file install path not implemented in `served`

When `serve install ./file.pack` is invoked:

1. The `serve` CLI sets `installOptions.path = <absolute-path>` and leaves
   `installOptions.package = NULL`.
2. `served`'s API handler (`daemons/served/api.c`,
   `chef_served_install_invocation`) stores `options->package` in the
   transaction state but **ignores `options->path`**.
3. The transaction state machine's DOWNLOAD step calls
   `store_ensure_package()` with a NULL package name, which fails.

**Impact:** Steps 7–10 of `hello-runtime.sh` will fail until this gap is
closed.

**Fix required:** Store `options->path` in `state_transaction` and add a
bypass in the DOWNLOAD state that uses the local path directly instead of
fetching from the store.

#### `serve-exec` requires setuid root

`served` attempts `chmod(wrapperPath, 06755)` when generating wrapper scripts.
Without root this silently fails (the code falls through without propagating
the error).  The wrapper is created but non-setuid, so container execution via
`serve-exec` may not work as expected in some environments.

---

## Log files

Each test case creates a unique temp directory and writes separate log files:

| File | Contents |
|---|---|
| `cvd.log` | stdout+stderr from the `cvd` daemon |
| `served.log` | stdout+stderr from the `served` daemon |
| `bake-build.log` | stdout+stderr from `bake build` |
| `serve-install.log` | stdout+stderr from `serve install` |
| `serve-list.log` | stdout+stderr from `serve list` |
| `run-output.log` | stdout+stderr from the installed application |

On failure the teardown handler prints the relevant log files to stdout so
they appear in CI output.  In GitHub Actions, the workflow also uploads the
log directory as an artifact for further inspection.

---

## CI integration

The system tests are run as part of the Ubuntu build in
`.github/workflows/build.yml`.

- `smoke-cvd` and `hello-build` are **hard gates** — CI fails if they fail.
- `hello-runtime` exits with code 2 for aspirational steps; `run.sh` treats
  code 2 as a **warning** (not a failure) so CI continues while the
  implementation gap is open.

Logs are uploaded as a CI artifact on failure for offline debugging.

---

## Adding new tests

1. Create `tests/system/cases/<name>.sh`.
2. Source the lib scripts at the top:
   ```bash
   source "$TESTS_DIR/lib/env.sh"
   source "$TESTS_DIR/lib/cleanup.sh"
   source "$TESTS_DIR/lib/process.sh"
   source "$TESTS_DIR/lib/daemon.sh"
   source "$TESTS_DIR/lib/assert.sh"
   ```
3. Set `TEST_NAME`, create a `TEST_LOG_DIR`, register it, and install the
   `trap 'teardown_test' EXIT` handler.
4. Add the case name to the `DEFAULT_CASES` array in `run.sh`.
