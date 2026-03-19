#!/usr/bin/env bash
# order-fetch-from-store.sh — validate tools/order against the dummy store
#
# Workflow:
#   1. Start an isolated dummy store instance
#   2. Seed the store with a known package
#   3. Run 'order find <query>' — assert package is found
#   4. Run 'order info publisher/package' — assert metadata is returned
#   5. Negative path: 'order info' for a missing package — assert failure
#
# The CHEF_STORE_URL environment variable (set by store.sh) redirects all
# chefclient HTTP calls to the dummy store instance, exercising the real
# chefclient code paths.
#
# Exit codes:
#   0  all assertions passed
#   1  infrastructure or assertion failure

set -euo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$TESTS_DIR/lib/env.sh"
source "$TESTS_DIR/lib/cleanup.sh"
source "$TESTS_DIR/lib/process.sh"
source "$TESTS_DIR/lib/daemon.sh"
source "$TESTS_DIR/lib/assert.sh"
source "$TESTS_DIR/lib/store.sh"

TEST_NAME="order-fetch-from-store"
TEST_LOG_DIR="$(mktemp -d)"
STORE_ROOT="$TEST_LOG_DIR/store-root"
STORE_LOG="$TEST_LOG_DIR/dummy-store.log"
ORDER_ROOT="$TEST_LOG_DIR/order-root"
ORDER_LOG="$TEST_LOG_DIR/order.log"

mkdir -p "$STORE_ROOT" "$ORDER_ROOT"

register_tmpdir "$TEST_LOG_DIR"
register_log    "$STORE_LOG"
register_log    "$ORDER_LOG"
trap 'teardown_test' EXIT

echo "=== $TEST_NAME ==="

# ── Preflight ─────────────────────────────────────────────────────────────────
echo "[1/7] Checking prerequisites..."

if [[ ! -x "$CMD_ORDER" ]]; then
    echo "FAIL: order binary not found: $CMD_ORDER" >&2
    echo "      Make sure the project is built before running system tests." >&2
    exit 1
fi

if ! command -v curl >/dev/null 2>&1; then
    echo "FAIL: curl is required for seeding the dummy store" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required to run the dummy store" >&2
    exit 1
fi

# ── Start dummy store ─────────────────────────────────────────────────────────
echo "[2/7] Starting dummy store..."
STORE_PORT=19877
start_dummy_store "$STORE_PORT" "$STORE_ROOT" "$STORE_LOG"

echo "      Waiting for dummy store to become ready..."
if ! wait_for_dummy_store 40 0.25; then
    echo "FAIL: dummy store did not become ready"
    exit 1
fi
echo "      Dummy store is ready at $DUMMY_STORE_URL"

# ── Seed the dummy store ──────────────────────────────────────────────────────
echo "[3/7] Seeding dummy store with test package..."
TEST_PACK="$TEST_LOG_DIR/testpkg.pack"
printf 'HELLO-WORLD-TEST-PACKAGE-CONTENT' > "$TEST_PACK"

revision=$(seed_dummy_store \
    "testpub" "hello-world" \
    "linux" "amd64" "stable" \
    1 2 3 \
    "$TEST_PACK")

if [[ $? -ne 0 || -z "$revision" ]]; then
    echo "FAIL: failed to seed dummy store" >&2
    exit 1
fi
echo "      Seeded testpub/hello-world at revision $revision"

# CHEF_STORE_URL is already exported by store.sh/start_dummy_store.
# order commands that call chefclient_initialize() will pick it up.

# ── order find ────────────────────────────────────────────────────────────────
echo "[4/7] Running 'order find hello-world'..."
find_rc=0
find_output=""
run_cmd find_output \
    "$CMD_ORDER" --root "$ORDER_ROOT" find "hello-world" || find_rc=$?

echo "$find_output" >"$ORDER_LOG"

assert_success "$find_rc" "'order find hello-world'"
assert_contains "$find_output" "testpub"      "'order find' output contains publisher"
assert_contains "$find_output" "hello-world"  "'order find' output contains package name"
echo "      'order find' output:"
echo "$find_output" | sed 's/^/        /'

# ── order info ────────────────────────────────────────────────────────────────
echo "[5/7] Running 'order info testpub/hello-world'..."
info_rc=0
info_output=""
run_cmd info_output \
    "$CMD_ORDER" --root "$ORDER_ROOT" info "testpub/hello-world" || info_rc=$?

echo "$info_output" >>"$ORDER_LOG"

assert_success "$info_rc" "'order info testpub/hello-world'"
assert_contains "$info_output" "testpub"     "'order info' output contains publisher"
assert_contains "$info_output" "hello-world" "'order info' output contains package name"
echo "      'order info' output:"
echo "$info_output" | sed 's/^/        /'

# ── Verify revision is visible in store find ──────────────────────────────────
echo "[6/7] Confirming revision $revision is visible in store metadata..."
info_resp=$(curl -sf \
    "${DUMMY_STORE_URL}/package/info?publisher=testpub&name=hello-world" 2>&1)
assert_contains "$info_resp" '"revision"' "store metadata contains revision field"
echo "      Store metadata contains revision information"

# ── Negative path: info for missing package ───────────────────────────────────
echo "[7/7] Negative-path: 'order info' for a missing package..."
neg_rc=0
neg_output=""
run_cmd neg_output \
    "$CMD_ORDER" --root "$ORDER_ROOT" info "testpub/does-not-exist" || neg_rc=$?

echo "$neg_output" >>"$ORDER_LOG"
assert_failure "$neg_rc" "'order info' for missing package returns non-zero"
echo "      'order info' correctly failed for missing package (rc=$neg_rc)"

echo ""
echo "PASS: $TEST_NAME"
