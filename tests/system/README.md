# System Test Suite

This directory contains the system-test harness for `bake`.  The tests
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
bash tests/system/run.sh dummy-store-roundtrip
bash tests/system/run.sh order-fetch-from-store
bash tests/system/run.sh served-install-from-store
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
| `python3` | Running the dummy store HTTP server |
| `curl` | Seeding the dummy store and negative-path checks |

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
      dummy-store.py   ← Python HTTP server implementing the fake store
      store.sh         ← helpers for starting/seeding the dummy store
    cases/
      smoke-cvd.sh                  ← smoke test: cvd starts and stays alive
      hello-build.sh                ← build test: hello-world produces a .pack artifact
      hello-runtime.sh              ← runtime test: build → install → run hello-world
      dummy-store-roundtrip.sh      ← dummy store publish/download/find/info
      order-fetch-from-store.sh     ← order find/info against dummy store
      served-install-from-store.sh  ← served downloading from dummy store
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

### `dummy-store-roundtrip.sh`

Validates the dummy store itself at a basic workflow level.

- Starts an isolated dummy store instance (`tests/system/lib/dummy-store.py`)
- Seeds a synthetic package via the three-step publish API
- Downloads the package back via `GET /package/download`
- Queries via `GET /package/find` and `GET /package/info`
- Verifies downloaded content matches the seeded content
- Negative-path: requests a missing package — asserts error response

Exit codes: `0` = pass, `1` = fail.

---

### `order-fetch-from-store.sh`

Validates that `tools/order` can interact with the dummy store through the
real chefclient code paths.

- Starts an isolated dummy store instance
- Seeds a test package
- Sets `CHEF_STORE_URL` so `chefclient_api_base_url()` returns the dummy store URL
- Runs `order find hello-world` — asserts publisher and name appear
- Runs `order info testpub/hello-world` — asserts metadata is returned
- Negative-path: `order info` for a missing package — asserts non-zero exit

Exit codes: `0` = pass, `1` = fail.

---

### `served-install-from-store.sh`

Validates that `daemons/served` can download a package from the dummy store.

| Step | Description | Status |
|------|-------------|--------|
| 1–2  | Prerequisites and build hello-world | ✅ hard assertion |
| 3    | Start dummy store | ✅ hard assertion |
| 4    | Seed store with hello-world.pack | ✅ hard assertion |
| 5    | Start served with `CHEF_STORE_URL` pointing at dummy store | ✅ hard assertion |
| 6    | Verify served is responsive (`serve list`) | ✅ hard assertion |
| 7    | Request install of `testpub/hello-world` | ✅ hard assertion |
| 8    | Assert package blob downloaded into local cache | ✅ hard assertion |
| 9    | Package appears in `serve list` | ⚠️ aspirational (requires crypto proof) |
| 10   | Installed application is runnable | ⚠️ aspirational |

Exit codes: `0` = full pass, `1` = infrastructure failure, `2` = aspirational
steps not yet fully implemented.

---

## Dummy store (`lib/dummy-store.py`)

The dummy store is a lightweight Python HTTP server that implements the subset
of the Chef Store API consumed by `libs/chefclient` and `daemons/served`.

### Supported endpoints

| Endpoint | Notes |
|---|---|
| `GET /package/find` | Substring search over seeded packages |
| `GET /package/info` | Returns full metadata including revision list |
| `GET /package/revision` | Resolves latest revision for a platform/arch/channel |
| `GET /package/download` | Streams binary package blob |
| `GET /package/proof` | Returns placeholder proof blob |
| `POST /package/publish/initiate` | Allocates revision and upload token |
| `POST /package/publish/upload` | Accepts raw binary or multipart upload |
| `POST /package/publish/complete` | Finalises publish and assigns channel |
| `GET /account/publisher` | Stub response (for proof resolution path) |
| `GET /account/me` | Stub response (requires any Bearer token) |

### Authentication

All `Auth required` endpoints accept **any non-empty Bearer token**, e.g.:

```
Authorization: Bearer test-token
```

This allows tests to exercise authenticated endpoints without real credentials.

### Filesystem layout under `--root`

```
<root>/
  packages/
    <publisher>/<name>/
      meta.json          — metadata and revision list
      rev/<revision>/
        package.pack     — binary blob
        proof.bin        — placeholder proof
  uploads/
    <token>.json         — pending upload metadata
    <token>.pack         — pending upload blob
```

### Running the dummy store manually

```bash
python3 tests/system/lib/dummy-store.py \
    --port 9876 \
    --root /tmp/my-store-root
```

### Seeding the dummy store from a shell script

Source `tests/system/lib/store.sh` and use the provided helpers:

```bash
source tests/system/lib/store.sh

# Start the server
start_dummy_store 9876 /tmp/store-root /tmp/store.log
wait_for_dummy_store

# Seed a package (three-step publish)
revision=$(seed_dummy_store \
    "mypublisher" "mypackage" \
    "linux" "amd64" "stable" \
    1 0 0 \
    /path/to/package.pack)

echo "Seeded at revision $revision"
```

### Redirecting chefclient to the dummy store

Set `CHEF_STORE_URL` before running any binary that uses `libs/chefclient`:

```bash
export CHEF_STORE_URL=http://127.0.0.1:9876
order find mypackage
```

This works because `chefclient_api_base_url()` checks `CHEF_STORE_URL` first
before returning the compiled-in default URL.

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

### 4. How is the store backend configured for `served`?

`served` uses `libs/store` with the default backend from
`libs/store/include/chef/store-default.h`, which calls `chefclient_pack_download`
and `chefclient_pack_proof`.  These call `chefclient_api_base_url()` to build
request URLs.

`chefclient_api_base_url()` checks the `CHEF_STORE_URL` environment variable
first.  Setting this before starting `served` (or any other process that uses
`libs/chefclient`) redirects all store API calls to the specified URL.

To inject the variable into `served` (which runs as root via sudo):

```bash
sudo bash -c "export CHEF_STORE_URL=http://127.0.0.1:9876; served --root /tmp/root -vv &"
```

The `start_daemon_as_root_with_env` helper in `lib/daemon.sh` handles this.

### 5. How is the store backend configured for `tools/order`?

`order` calls `chefclient_initialize()` inside each subcommand.  Setting
`CHEF_STORE_URL` in the environment before running `order` redirects all API
calls:

```bash
CHEF_STORE_URL=http://127.0.0.1:9876 order find hello-world
```

### 6. What minimal package metadata is required for a stored package?

The minimum a dummy store must return for `chefclient_pack_download` to work:

1. `GET /package/revision` → `{"revision": N}` for the matching platform/arch/channel
2. `GET /package/download?publisher=X&name=Y&revision=N` → binary content (`application/octet-stream`)

For `chefclient_pack_find` and `chefclient_pack_info`, the store also needs:

3. `GET /package/find?search=<query>` → JSON array of package summaries
4. `GET /package/info?publisher=X&name=Y` → JSON object with revision list

### 7. What is the correct way to run the installed hello-world application?

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

### 8. Known limitations / implementation gaps

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

#### Package proof verification requires real RSA keys

The VERIFY state in `served` calls `utils_verify_package` which:
1. Retrieves the publisher's public key from `GET /account/publisher`
2. Verifies the key is signed by a hardcoded certificate authority
3. Downloads the package proof from `GET /package/proof`
4. Verifies the package SHA-512 hash against the proof using the publisher key

The dummy store returns placeholder values for proofs and publisher keys.
These **will not pass** the RSA/X.509 verification in `utils_verify_package`.

**Impact:** The VERIFY step will fail for store-backed installs using the
dummy store.  Steps 9–10 of `served-install-from-store.sh` are therefore
marked aspirational.

**Future fix options:**
- Generate a real test CA, test publisher keypair, and sign the test package
  during store seeding, OR
- Add a test/CI mode flag to `served` that skips cryptographic verification

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
| `dummy-store.log` | stdout+stderr from the dummy store |
| `bake-build.log` | stdout+stderr from `bake build` |
| `serve-install.log` | stdout+stderr from `serve install` |
| `serve-list.log` | stdout+stderr from `serve list` |
| `order.log` | stdout+stderr from `order` commands |
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
- `dummy-store-roundtrip` and `order-fetch-from-store` are **hard gates**
  (no aspirational steps).
- `served-install-from-store` exits with code 2 for aspirational steps
  (verify/install/run pipeline requires real crypto proofs).

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
   # For store-backed tests:
   source "$TESTS_DIR/lib/store.sh"
   ```
3. Set `TEST_NAME`, create a `TEST_LOG_DIR`, register it, and install the
   `trap 'teardown_test' EXIT` handler.
4. Add the case name to the `DEFAULT_CASES` array in `run.sh`.
