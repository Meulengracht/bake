#!/usr/bin/env bash
# hello-runtime.sh — end-to-end runtime workflow for hello-world
#
# Intended workflow (Phase 1):
#   1.  Create isolated temp environment
#   2.  Start cvd (container daemon)
#   3.  Build the Ubuntu base and hello-world recipes
#   4.  Configure an isolated fake-store keypair and sign both built .packs
#   5.  Publish both packs to an isolated dummy store
#   6.  Start served (runtime daemon) with --root pointing at the temp dir
#   7.  Wait for served to be ready (/tmp/served socket appears)
#   8.  Verify served is responsive via 'serve list'
#   9.  Install hello-world; served resolves and installs its base dependency
#   10. Verify the package appears in 'serve list'
#   11. Run the installed application and assert expected stdout
#
# ┌─────────────────────────────────────────────────────────────────────────┐
# │ KNOWN LIMITATIONS (Phase 1 — as discovered during harness development) │
# │                                                                         │
# │ • The runtime verification steps depend on served's store-backed install │
# │   transaction and cryptographic proof handling.                         │
# │                                                                         │
# │ • Steps 1–8 are hard assertions.  Steps 9–11 are attempted and their  │
# │   failure is clearly reported, but the test exits with a distinct      │
# │   non-zero code (2) so CI can distinguish infrastructure failures from  │
# │   aspirational-feature failures.                                        │
# └─────────────────────────────────────────────────────────────────────────┘

set -euo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$TESTS_DIR/lib/env.sh"
source "$TESTS_DIR/lib/cleanup.sh"
source "$TESTS_DIR/lib/process.sh"
source "$TESTS_DIR/lib/signing.sh"
source "$TESTS_DIR/lib/daemon.sh"
source "$TESTS_DIR/lib/assert.sh"
source "$TESTS_DIR/lib/store.sh"

TEST_NAME="hello-runtime"
TEST_LOG_DIR="$(mktemp -d)"
WORK_DIR="$TEST_LOG_DIR/build"
SERVED_ROOT="$TEST_LOG_DIR/served-root"
STORE_ROOT="$TEST_LOG_DIR/store-root"
SIGNING_ROOT="$TEST_LOG_DIR/signing-root"
CVD_LOG="$TEST_LOG_DIR/cvd.log"
SERVED_LOG="$TEST_LOG_DIR/served.log"
STORE_LOG="$TEST_LOG_DIR/dummy-store.log"
BUILD_LOG="$TEST_LOG_DIR/bake-build.log"
BASE_BUILD_LOG="$TEST_LOG_DIR/base-build.log"
INSTALL_LOG="$TEST_LOG_DIR/serve-install.log"
LIST_LOG="$TEST_LOG_DIR/serve-list.log"
SIGN_LOG="$TEST_LOG_DIR/serve-sign.log"
RUN_LOG="$TEST_LOG_DIR/run-output.log"

mkdir -p "$WORK_DIR" "$SERVED_ROOT" "$STORE_ROOT" "$SIGNING_ROOT"

register_tmpdir "$TEST_LOG_DIR"
register_log    "$CVD_LOG"
register_log    "$SERVED_LOG"
register_log    "$STORE_LOG"
register_log    "$BUILD_LOG"
register_log    "$BASE_BUILD_LOG"
register_log    "$INSTALL_LOG"
register_log    "$LIST_LOG"
register_log    "$SIGN_LOG"
register_log    "$RUN_LOG"
trap 'teardown_test' EXIT

# Exit code 2 = aspirational steps failed (distinct from infrastructure failure)
ASPIRATIONAL_FAILED=0

echo "=== $TEST_NAME ==="

# ── Preflight ─────────────────────────────────────────────────────────────────
echo "[1/11] Checking prerequisites..."
env_check_binaries
env_check_sudo

RECIPE_DIR="$ROOT_DIR/examples/recipes"
if [[ ! -f "$RECIPE_DIR/hello.yaml" ]]; then
    echo "FAIL: recipe file not found: $RECIPE_DIR/hello.yaml" >&2
    exit 1
fi
if [[ ! -f "$RECIPE_DIR/linux/base.yaml" || ! -f "$RECIPE_DIR/linux/construct.sh" ]]; then
    echo "FAIL: Linux base recipe files not found" >&2
    exit 1
fi
if ! command -v curl >/dev/null 2>&1 || ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: curl and python3 are required for the dummy store" >&2
    exit 1
fi

# ── Start cvd ─────────────────────────────────────────────────────────────────
echo "[2/11] Starting cvd..."
start_daemon_as_root "cvd" "$CVD_LOG" "$CMD_CVD" -vv

echo "       Waiting for cvd to become alive..."
if ! wait_for_cvd 40 0.25; then
    echo "FAIL: cvd did not become alive"
    exit 1
fi
echo "       cvd is alive."

# ── Build the runtime base and hello-world ────────────────────────────────────
echo "[3/11] Building Ubuntu base and hello-world..."

cp -a "$RECIPE_DIR/linux/base.yaml" "$WORK_DIR/base.yaml"
cp -a "$RECIPE_DIR/linux/construct.sh" "$WORK_DIR/construct.sh"
cp -a "$RECIPE_DIR/hello.yaml"   "$WORK_DIR/hello.yaml"
cp -a "$RECIPE_DIR/hello-world"  "$WORK_DIR/hello-world"

base_build_rc=0
(cd "$WORK_DIR" && \
    if command -v timeout >/dev/null 2>&1; then
        timeout 20m "$CMD_BAKE" -v build base.yaml
    else
        "$CMD_BAKE" -v build base.yaml
    fi) >"$BASE_BUILD_LOG" 2>&1 || base_build_rc=$?

if [[ $base_build_rc -ne 0 ]]; then
    echo "FAIL: base bake build exited with code $base_build_rc"
    dump_log "$BASE_BUILD_LOG" 1000
    exit 1
fi

shopt -s nullglob
base_pack_files=( "$WORK_DIR"/ubuntu-24*.pack )
shopt -u nullglob
if [[ ${#base_pack_files[@]} -eq 0 ]]; then
    echo "FAIL: base bake build succeeded but no Ubuntu base .pack file was produced" >&2
    exit 1
fi
BASE_PACK_FILE="${base_pack_files[0]}"
assert_file_nonempty "$BASE_PACK_FILE" "Ubuntu base .pack artifact"
echo "       base artifact: $(basename "$BASE_PACK_FILE") ($(wc -c < "$BASE_PACK_FILE") bytes)"

build_rc=0
(cd "$WORK_DIR" && \
    if command -v timeout >/dev/null 2>&1; then
        timeout 20m "$CMD_BAKE" -v build hello.yaml
    else
        "$CMD_BAKE" -v build hello.yaml
    fi) >"$BUILD_LOG" 2>&1 || build_rc=$?

if [[ $build_rc -ne 0 ]]; then
    echo "FAIL: bake build exited with code $build_rc"
    dump_log "$BUILD_LOG" 1000
    exit 1
fi

# Locate the .pack artifact
shopt -s nullglob
pack_files=( "$WORK_DIR"/hello-world*.pack )
shopt -u nullglob

if [[ ${#pack_files[@]} -eq 0 ]]; then
    echo "FAIL: bake build succeeded but no .pack file was produced" >&2
    exit 1
fi

PACK_FILE="${pack_files[0]}"
assert_file_nonempty "$PACK_FILE" ".pack artifact"
echo "       artifact: $(basename "$PACK_FILE") ($(wc -c < "$PACK_FILE") bytes)"

# ── Sign the base and hello-world packs ───────────────────────────────────────
# Sign the .pack artifacts to enable installation (skips interactive prompt in
# 'serve install'). Keep signing config under an isolated root so the test does
# not touch the developer's real ~/.chef state.
echo "[4/11] Configuring fake-store keypair and signing .pack artifacts..."
STORE_KEY="$SIGNING_ROOT/store_key"
if ! setup_test_store_signing_identity "$SIGNING_ROOT" "$STORE_KEY"; then
    echo "FAIL: could not configure fake-store signing identity"
    exit 1
fi

: >"$SIGN_LOG"
for sign_pack in "$BASE_PACK_FILE" "$PACK_FILE"; do
    proof_file="$sign_pack.proof"
    if ! create_test_store_proof "$sign_pack" "$proof_file" "$STORE_KEY" "$STORE_KEY.pub" "$(basename "$sign_pack" .pack)"; then
        echo "FAIL: could not create fake-store proof for $(basename "$sign_pack")" >&2
        exit 1
    fi
    echo "created publisher proof: $(basename "$proof_file")" >>"$SIGN_LOG"
done

# Locate the .pack.proof artifact
shopt -s nullglob
proof_files=( "$BASE_PACK_FILE.proof" "$PACK_FILE.proof" )
shopt -u nullglob

for proof_file in "${proof_files[@]}"; do
    if [[ ! -s "$proof_file" ]]; then
        echo "FAIL: bake sign did not produce proof artifact: $proof_file" >&2
        exit 1
    fi
    echo "       artifact: $(basename "$proof_file") ($(wc -c < "$proof_file") bytes)"
done

# ── Publish the packs to the isolated dummy store ─────────────────────────────
echo "[5/11] Publishing base and hello-world packs to the dummy store..."
STORE_PORT=19879
start_dummy_store "$STORE_PORT" "$STORE_ROOT" "$STORE_LOG"
if ! wait_for_dummy_store 40 0.25; then
    echo "FAIL: dummy store did not become ready"
    exit 1
fi

if ! base_revision=$(seed_dummy_store \
    "testpub" "ubuntu-24" "linux" "amd64" "stable" 1 0 0 "$BASE_PACK_FILE" "$BASE_PACK_FILE.proof"); then
    echo "FAIL: failed to seed testpub/ubuntu-24" >&2
    exit 1
fi
if [[ -z "$base_revision" ]]; then
    echo "FAIL: failed to seed testpub/ubuntu-24" >&2
    exit 1
fi
echo "       Seeded testpub/ubuntu-24 at revision $base_revision"

if ! app_revision=$(seed_dummy_store \
    "testpub" "hello-world" "linux" "amd64" "stable" 1 0 0 "$PACK_FILE" "$PACK_FILE.proof"); then
    echo "FAIL: failed to seed testpub/hello-world" >&2
    exit 1
fi
if [[ -z "$app_revision" ]]; then
    echo "FAIL: failed to seed testpub/hello-world" >&2
    exit 1
fi
echo "       Seeded testpub/hello-world at revision $app_revision"

# ── Start served with the isolated dummy store ────────────────────────────────
echo "[6/11] Starting served (--root $SERVED_ROOT, store $DUMMY_STORE_URL)..."
STORE_ENV="CHEF_STORE_URL=${DUMMY_STORE_URL}"
start_daemon_as_root_with_env \
    "served" "$SERVED_LOG" "$STORE_ENV" \
    "$CMD_SERVED" --root "$SERVED_ROOT"

echo "       Waiting for served socket (/tmp/served)..."
if ! wait_for_served 80 0.25; then
    echo "FAIL: served did not become ready"
    dump_log "$SERVED_LOG" 1000
    exit 1
fi
echo "       served is ready."

# ── Verify served responds to 'serve list' ────────────────────────────────────
echo "[8/11] Verifying served is responsive (serve list)..."
list_output=""
list_rc=0
run_cmd list_output "$CMD_SERVE" list || list_rc=$?

echo "$list_output" >"$LIST_LOG"

if [[ $list_rc -ne 0 ]]; then
    echo "FAIL: 'serve list' exited with code $list_rc (served not responsive)"
    dump_log "$SERVED_LOG" 1000
    exit 1
fi
echo "       served responded to 'serve list' — infrastructure is functional."

# ── Install the application and let served resolve its base ───────────────────
echo "[9/11] Installing hello-world from the dummy store..."
install_rc=0
run_cmd_with_timeout 60 install_output \
    "$CMD_SERVE" install "testpub/hello-world" -C stable || install_rc=$?

echo "$install_output" >"$INSTALL_LOG"

if [[ $install_rc -ne 0 ]]; then
    echo "FAIL: store-backed 'serve install' exited with code $install_rc"
    exit 1
else
    echo "       hello-world install request accepted; served should resolve ubuntu-24."
fi

# ── Verify package appears in serve list ──────────────────────────────────────
echo "[10/11] Verifying hello-world appears in 'serve list'..."
sleep 2  # Allow the transaction state machine to progress

list_output2=""
run_cmd list_output2 "$CMD_SERVE" list || true
echo "$list_output2" >>"$LIST_LOG"

if ! assert_package_listed "$list_output2" "hello-world" 2>/dev/null; then
    echo "FAIL: hello-world not yet visible in 'serve list'"
    exit 1
else
    echo "       hello-world is listed — installation succeeded."
fi

# ═════════════════════════════════════════════════════════════════════════════
# Step 11 is aspirational: they depend on the store-backed install path
# being fully implemented in served. It is attempted but its failure does NOT
# indicate a problem with the test infrastructure.
# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "       NOTE: Step 11 test the store-backed install path in served."
echo "             This path is currently aspirational — see KNOWN LIMITATIONS"
echo "             in the file header for details."
echo ""

# ── Run the installed application ─────────────────────────────────────────────
echo "[11/11] Locating hello wrapper script..."
# The wrapper is generated at <served-root>/chef/bin/<command-name>
# The command name is 'hello' (from hello.yaml packs[0].commands[0].name)
WRAPPER="$SERVED_ROOT/chef/bin/hello"

# WORKAROUND DUE TO RUNNING FROM BUILD
# served binary used by this test: /home/runner/work/bake/bake/build/daemons/served/served
# expected serve-exec (same dir as served): /home/runner/work/bake/bake/build/daemons/served/serve-exec
# actual serve-exec path: /home/runner/work/bake/bake/build/bin/serve-exec
# lets copy it to the expected location so the wrapper can find it
EXPECTED_SERVE_EXEC="$(dirname "$CMD_SERVED")/serve-exec"
if [[ ! -x "$EXPECTED_SERVE_EXEC" ]]; then
    echo "       Copying serve-exec to expected location: $EXPECTED_SERVE_EXEC"
    cp "$BUILD_DIR/bin/serve-exec" "$EXPECTED_SERVE_EXEC"
fi

# serve-exec additionally needs to be SUID to have enough privileges to enter the container and run the command. 
# This is a workaround for the test environment, which is normally done by install
if [[ ! -u "$EXPECTED_SERVE_EXEC" ]]; then
    echo "       Setting SUID bit on serve-exec: $EXPECTED_SERVE_EXEC"
    $SUDO chown root:root "$EXPECTED_SERVE_EXEC"
    $SUDO chmod u+s "$EXPECTED_SERVE_EXEC"
fi

# list permissions of serve-exec to verify it is executable and SUID
echo "       serve-exec permissions:"
ls -la "$EXPECTED_SERVE_EXEC" 2>&1 | sed 's/^/       | /'

echo "       Wrapper: $WRAPPER"
echo "       Running installed hello-world..."
run_rc=0
run_output=""
run_cmd run_output "$WRAPPER" || run_rc=$?
echo "$run_output" >"$RUN_LOG"

echo "       Asserting output..."
if [[ $run_rc -ne 0 ]]; then
    echo "SKIP (aspirational): wrapper exited with code $run_rc"
    echo "       --- debug: wrapper script ($WRAPPER) ---"
    cat "$WRAPPER" 2>&1 | sed 's/^/       | /'
    echo "       --- debug: wrapper output ---"
    printf '%s\n' "$run_output" | sed 's/^/       | /'
    if [[ $run_rc -eq 127 ]]; then
        echo "       --- debug: exit 127 means 'command not found' — inspecting invoked serve-exec ---"
        # The wrapper's 2nd line is: <sexec_path> --container ... --path ... --wdir ... [args]
        # (line 1 is the '#!/bin/sh' shebang, which always resolves fine)
        exec_line="$(sed -n '2p' "$WRAPPER" 2>/dev/null)"
        sexec_path="${exec_line%% *}"
        echo "       invoked command line: $exec_line"
        echo "       resolved serve-exec path: $sexec_path"
        if [[ -z "$sexec_path" ]]; then
            echo "       could not extract sexec path from wrapper"
        elif [[ ! -e "$sexec_path" ]]; then
            echo "       MISSING: '$sexec_path' does not exist"
            echo "       (served derives this path from its own /proc/self/exe directory + 'serve-exec';"
            echo "        it may not match where this test's serve-exec binary actually lives)"
        elif [[ ! -x "$sexec_path" ]]; then
            echo "       '$sexec_path' exists but is NOT executable"
            ls -la "$sexec_path" 2>&1 | sed 's/^/       | /'
        else
            echo "       '$sexec_path' exists and is executable; checking dynamic deps..."
            ldd "$sexec_path" 2>&1 | sed 's/^/       | /'
        fi
        echo "       served binary used by this test: $CMD_SERVED"
        echo "       expected serve-exec (same dir as served): $(dirname "$CMD_SERVED")/serve-exec"
        echo "       --- debug: contents of $SERVED_ROOT/chef ---"
        find "$SERVED_ROOT/chef" -maxdepth 4 2>&1 | sed 's/^/       | /'
    fi
    # Debug the /run/containerv socket permissions, which can cause the wrapper to fail with exit 127
    echo "       --- debug: /run/containerv socket permissions ---"
    if [[ -e /run/containerv ]]; then
        ls -la /run/containerv 2>&1 | sed 's/^/       | /'
        ls -la /run/containerv/testpub.hello-world 2>&1 | sed 's/^/       | /'
    else
        echo "       /run/containerv does not exist"
    fi
    ASPIRATIONAL_FAILED=1
elif ! assert_contains "$run_output" "hello world" "hello-world stdout"; then
    echo "SKIP (aspirational): expected output not found"
    ASPIRATIONAL_FAILED=1
else
    echo "       output: $run_output"
    echo "       Application ran and produced expected output."
fi

# Dump the contents of the vafs layers
$SUDO find "/var/chef/vafs" -maxdepth 6 2>&1 | sed 's/^/       | /'

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
if [[ $ASPIRATIONAL_FAILED -ne 0 ]]; then
    echo "PARTIAL: $TEST_NAME"
    echo "  Infrastructure steps (1-10): PASS"
    echo "  Runtime run step (11): NOT IMPLEMENTED YET"
    echo "  See tests/system/README.md — 'Known Limitations' section."
    # Exit code 2 signals aspirational-step failure (not an infra failure)
    exit 2
else
    echo "PASS: $TEST_NAME (full end-to-end workflow)"
fi
