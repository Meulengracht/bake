#!/usr/bin/env bash
# hello-build.sh — verify that examples/recipes/hello-world can be built
#
# What this test checks:
#   1. cvd starts (required by bake)
#   2. bake build hello.yaml succeeds
#   3. At least one non-empty .pack file is produced in the work directory
#
# This test preserves and formalises the existing CI check for the hello-world
# build recipe.

set -euo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$TESTS_DIR/lib/env.sh"
source "$TESTS_DIR/lib/cleanup.sh"
source "$TESTS_DIR/lib/process.sh"
source "$TESTS_DIR/lib/daemon.sh"
source "$TESTS_DIR/lib/assert.sh"

TEST_NAME="hello-build"
TEST_LOG_DIR="$(mktemp -d)"
WORK_DIR="$TEST_LOG_DIR/build"
CVD_LOG="$TEST_LOG_DIR/cvd.log"
BUILD_LOG="$TEST_LOG_DIR/bake-build.log"

mkdir -p "$WORK_DIR"

register_tmpdir "$TEST_LOG_DIR"
register_log    "$CVD_LOG"
register_log    "$BUILD_LOG"
trap 'teardown_test' EXIT

echo "=== $TEST_NAME ==="

# ── Preflight ─────────────────────────────────────────────────────────────────
echo "[1/4] Checking prerequisites..."
env_check_binaries
env_check_sudo

RECIPE_DIR="$ROOT_DIR/examples/recipes"
if [[ ! -f "$RECIPE_DIR/hello.yaml" ]]; then
    echo "FAIL: recipe file not found: $RECIPE_DIR/hello.yaml" >&2
    exit 1
fi
if [[ ! -d "$RECIPE_DIR/hello-world" ]]; then
    echo "FAIL: recipe source directory not found: $RECIPE_DIR/hello-world" >&2
    exit 1
fi

# ── Start cvd ─────────────────────────────────────────────────────────────────
echo "[2/4] Starting cvd..."
start_daemon_as_root "cvd" "$CVD_LOG" "$CMD_CVD" -vv

echo "      Waiting for cvd to become alive..."
if ! wait_for_cvd 40 0.25; then
    echo "FAIL: cvd did not become alive"
    exit 1
fi
echo "      cvd is alive."

# ── Build hello-world ─────────────────────────────────────────────────────────
echo "[3/4] Building hello-world..."

# Copy recipe files into work dir so bake's relative paths resolve correctly
# and build artifacts are isolated from the git checkout.
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

# ── Verify artifact ───────────────────────────────────────────────────────────
echo "[4/4] Verifying build artifact..."

shopt -s nullglob
pack_files=( "$WORK_DIR"/*.pack )
shopt -u nullglob

if [[ ${#pack_files[@]} -eq 0 ]]; then
    echo "FAIL: bake build succeeded but no .pack file was produced in $WORK_DIR" >&2
    exit 1
fi

for f in "${pack_files[@]}"; do
    assert_file_nonempty "$f" ".pack artifact"
    echo "      artifact: $(basename "$f") ($(wc -c < "$f") bytes)"
done

echo ""
echo "PASS: $TEST_NAME"
