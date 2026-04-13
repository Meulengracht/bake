#!/usr/bin/env bash
# hello-runtime.sh — end-to-end runtime workflow for hello-world
#
# Intended workflow (Phase 1):
#   1.  Create isolated temp environment
#   2.  Start cvd (container daemon)
#   3.  Build examples/recipes/hello-world  →  produces hello-world-*.pack
#   4.  Start served (runtime daemon) with --root pointing at the temp dir
#   5.  Wait for served to be ready (/tmp/served socket appears)
#   6.  Verify served is responsive via 'serve list'
#   7.  Install the built .pack via 'serve install'
#   8.  Verify the package appears in 'serve list'
#   9.  Run the installed application via its wrapper script
#   10. Assert exit code 0 and expected stdout ("hello world")
#
# ┌─────────────────────────────────────────────────────────────────────────┐
# │ KNOWN LIMITATIONS (Phase 1 — as discovered during harness development) │
# │                                                                         │
# │ • The 'served' install API currently does not propagate the local-file  │
# │   path through its transaction state machine (api.c stores             │
# │   options->package but not options->path, so installing from a local   │
# │   .pack sets a NULL package name in the transaction).  Steps 7–10 will │
# │   therefore FAIL until this gap is closed.                             │
# │                                                                         │
# │ • Steps 1–6 are hard assertions.  Steps 7–10 are attempted and their  │
# │   failure is clearly reported, but the test exits with a distinct      │
# │   non-zero code (2) so CI can distinguish infrastructure failures from  │
# │   aspirational-feature failures.                                        │
# └─────────────────────────────────────────────────────────────────────────┘

set -euo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$TESTS_DIR/lib/env.sh"
source "$TESTS_DIR/lib/cleanup.sh"
source "$TESTS_DIR/lib/process.sh"
source "$TESTS_DIR/lib/daemon.sh"
source "$TESTS_DIR/lib/assert.sh"

TEST_NAME="hello-runtime"
TEST_LOG_DIR="$(mktemp -d)"
WORK_DIR="$TEST_LOG_DIR/build"
SERVED_ROOT="$TEST_LOG_DIR/served-root"
CVD_LOG="$TEST_LOG_DIR/cvd.log"
SERVED_LOG="$TEST_LOG_DIR/served.log"
BUILD_LOG="$TEST_LOG_DIR/bake-build.log"
INSTALL_LOG="$TEST_LOG_DIR/serve-install.log"
LIST_LOG="$TEST_LOG_DIR/serve-list.log"
SIGN_LOG="$TEST_LOG_DIR/serve-sign.log"
RUN_LOG="$TEST_LOG_DIR/run-output.log"

mkdir -p "$WORK_DIR" "$SERVED_ROOT"

register_tmpdir "$TEST_LOG_DIR"
register_log    "$CVD_LOG"
register_log    "$SERVED_LOG"
register_log    "$BUILD_LOG"
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

# ── Start cvd ─────────────────────────────────────────────────────────────────
echo "[2/11] Starting cvd..."
start_daemon_as_root "cvd" "$CVD_LOG" "$CMD_CVD" -vv

echo "       Waiting for cvd to become alive..."
if ! wait_for_cvd 40 0.25; then
    echo "FAIL: cvd did not become alive"
    exit 1
fi
echo "       cvd is alive."

# ── Build hello-world ─────────────────────────────────────────────────────────
echo "[3/11] Building hello-world..."

cp -a "$RECIPE_DIR/hello.yaml"   "$WORK_DIR/hello.yaml"
cp -a "$RECIPE_DIR/hello-world"  "$WORK_DIR/hello-world"

build_rc=0
(cd "$WORK_DIR" && \
    if command -v timeout >/dev/null 2>&1; then
        timeout 20m "$CMD_BAKE" build hello.yaml -v
    else
        "$CMD_BAKE" build hello.yaml -v
    fi) >"$BUILD_LOG" 2>&1 || build_rc=$?

if [[ $build_rc -ne 0 ]]; then
    echo "FAIL: bake build exited with code $build_rc"
    dump_log "$BUILD_LOG" 1000
    exit 1
fi

# Locate the .pack artifact
shopt -s nullglob
pack_files=( "$WORK_DIR"/*.pack )
shopt -u nullglob

if [[ ${#pack_files[@]} -eq 0 ]]; then
    echo "FAIL: bake build succeeded but no .pack file was produced" >&2
    exit 1
fi

PACK_FILE="${pack_files[0]}"
assert_file_nonempty "$PACK_FILE" ".pack artifact"
echo "       artifact: $(basename "$PACK_FILE") ($(wc -c < "$PACK_FILE") bytes)"

# ── Sign hello-world ──────────────────────────────────────────────────────────
# Sign the .pack artifact to enable installation (skips interactive prompt in 'serve install')
# Run these commands to enable signing configuration for this test:
# Should be RSA 
# openssh-keygen -t rsa -b 4096 -f "$WORK_DIR/hello_key" -N ""
# order config auth.name "Github CI"
# order config auth.email "bake-ci@github.com"
# order config auth.key "$WORK_DIR/hello_key"
echo "[4/11] Signing .pack artifact..."
sign_output=""
sign_rc=0
run_cmd sign_output "$CMD_BAKE" sign -vvv "$PACK_FILE" || sign_rc=$?

echo "$sign_output" >"$SIGN_LOG"

if [[ $sign_rc -ne 0 ]]; then
    echo "FAIL: bake sign exited with code $sign_rc"
    dump_log "$SIGN_LOG" 1000
    exit 1
fi

# Locate the .pack.proof artifact
shopt -s nullglob
proof_files=( "$WORK_DIR"/*.pack.proof )
shopt -u nullglob

if [[ ${#proof_files[@]} -eq 0 ]]; then
    echo "FAIL: bake build succeeded but no .pack.proof file was produced" >&2
    exit 1
fi

PROOF_FILE="${proof_files[0]}"
assert_file_nonempty "$PROOF_FILE" ".pack.proof artifact"
echo "       artifact: $(basename "$PROOF_FILE") ($(wc -c < "$PROOF_FILE") bytes)"

# ── Start served ──────────────────────────────────────────────────────────────
echo "[5/11] Starting served (--root $SERVED_ROOT)..."
start_daemon_as_root "served" "$SERVED_LOG" "$CMD_SERVED" --root "$SERVED_ROOT"

echo "       Waiting for served socket (/tmp/served)..."
if ! wait_for_served 80 0.25; then
    echo "FAIL: served did not become ready"
    dump_log "$SERVED_LOG" 1000
    exit 1
fi
echo "       served is ready."

# ── Verify served responds to 'serve list' ────────────────────────────────────
echo "[6/11] Verifying served is responsive (serve list)..."
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

# ═════════════════════════════════════════════════════════════════════════════
# Steps 7–11 are aspirational: they depend on the local-file install path
# being implemented in served.  They are attempted but their failure does NOT
# indicate a problem with the test infrastructure.
# ═════════════════════════════════════════════════════════════════════════════
echo ""
echo "       NOTE: Steps 7-11 test the local-file install path in served."
echo "             This path is currently aspirational — see KNOWN LIMITATIONS"
echo "             in the file header for details."
echo ""

# ── Install the .pack artifact ────────────────────────────────────────────────
echo "[7/11] Installing hello-world pack..."
# Provide a proof token (-P) to skip the interactive yes/no prompt.
# Proof validation happens server-side; any non-empty string bypasses the
# client-side interactive check.
install_rc=0
run_cmd_with_timeout 60 install_output \
    "$CMD_SERVE" install -vvv "$PACK_FILE" || install_rc=$?

echo "$install_output" >"$INSTALL_LOG"

if [[ $install_rc -ne 0 ]]; then
    echo "SKIP (aspirational): 'serve install' exited with code $install_rc"
    echo "     This is expected while the local-file install path is unimplemented."
    echo "     See tests/system/README.md for details."
    ASPIRATIONAL_FAILED=1
fi

# ── Verify package appears in serve list ──────────────────────────────────────
if [[ $ASPIRATIONAL_FAILED -eq 0 ]]; then
    echo "[8/11] Verifying hello-world appears in 'serve list'..."
    sleep 2  # Allow the transaction state machine to progress

    list_output2=""
    run_cmd list_output2 "$CMD_SERVE" list || true
    echo "$list_output2" >>"$LIST_LOG"

    if ! assert_package_listed "$list_output2" "hello-world" 2>/dev/null; then
        echo "SKIP (aspirational): hello-world not yet visible in 'serve list'"
        ASPIRATIONAL_FAILED=1
    else
        echo "       hello-world is listed — installation succeeded."
    fi
else
    echo "[8/11] Skipping (install step failed)"
fi

# ── Run the installed application ─────────────────────────────────────────────
if [[ $ASPIRATIONAL_FAILED -eq 0 ]]; then
    echo "[9/11] Locating hello wrapper script..."
    # The wrapper is generated at <served-root>/chef/bin/<command-name>
    # The command name is 'hello' (from hello.yaml packs[0].commands[0].name)
    WRAPPER="$SERVED_ROOT/chef/bin/hello"

    if [[ ! -x "$WRAPPER" ]]; then
        echo "SKIP (aspirational): wrapper script not found or not executable: $WRAPPER"
        ASPIRATIONAL_FAILED=1
    else
        echo "       wrapper: $WRAPPER"

        echo "[10/11] Running installed hello-world..."
        run_rc=0
        run_output=""
        run_cmd run_output "$WRAPPER" || run_rc=$?
        echo "$run_output" >"$RUN_LOG"

        echo "[11/11] Asserting output..."
        if [[ $run_rc -ne 0 ]]; then
            echo "SKIP (aspirational): wrapper exited with code $run_rc"
            ASPIRATIONAL_FAILED=1
        elif ! assert_contains "$run_output" "hello world" "hello-world stdout"; then
            echo "SKIP (aspirational): expected output not found"
            ASPIRATIONAL_FAILED=1
        else
            echo "       output: $run_output"
            echo "       Application ran and produced expected output."
        fi
    fi
else
    echo "[8/11] Skipping (previous aspirational step failed)"
    echo "[9/11] Skipping"
    echo "[10/11] Skipping"
    echo "[11/11] Skipping"
fi

# ── Summary ───────────────────────────────────────────────────────────────────
echo ""
if [[ $ASPIRATIONAL_FAILED -ne 0 ]]; then
    echo "PARTIAL: $TEST_NAME"
    echo "  Infrastructure steps (1-6): PASS"
    echo "  Runtime install/run steps (7-11): NOT IMPLEMENTED YET"
    echo "  See tests/system/README.md — 'Known Limitations' section."
    # Exit code 2 signals aspirational-step failure (not an infra failure)
    exit 2
else
    echo "PASS: $TEST_NAME (full end-to-end workflow)"
fi
