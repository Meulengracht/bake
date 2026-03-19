#!/usr/bin/env bash
# dummy-store-roundtrip.sh — validate the dummy store itself at a basic level
#
# Workflow:
#   1. Start an isolated dummy store instance
#   2. Publish (seed) a synthetic test package via the store HTTP API
#   3. Retrieve the package via GET /package/download
#   4. Query via GET /package/find and GET /package/info
#   5. Attempt to retrieve an absent package — assert 404
#   6. Assert all operations produced expected results
#
# Exit codes:
#   0  all assertions passed
#   1  infrastructure failure or assertion failure

set -euo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$TESTS_DIR/lib/env.sh"
source "$TESTS_DIR/lib/cleanup.sh"
source "$TESTS_DIR/lib/process.sh"
source "$TESTS_DIR/lib/daemon.sh"
source "$TESTS_DIR/lib/assert.sh"
source "$TESTS_DIR/lib/store.sh"

TEST_NAME="dummy-store-roundtrip"
TEST_LOG_DIR="$(mktemp -d)"
STORE_ROOT="$TEST_LOG_DIR/store-root"
STORE_LOG="$TEST_LOG_DIR/dummy-store.log"
DOWNLOAD_DIR="$TEST_LOG_DIR/downloads"

mkdir -p "$STORE_ROOT" "$DOWNLOAD_DIR"

register_tmpdir "$TEST_LOG_DIR"
register_log    "$STORE_LOG"
trap 'teardown_test' EXIT

echo "=== $TEST_NAME ==="

# ── Preflight ─────────────────────────────────────────────────────────────────
echo "[1/8] Checking prerequisites..."

if ! command -v curl >/dev/null 2>&1; then
    echo "FAIL: curl is required for this test" >&2
    exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
    echo "FAIL: python3 is required to run the dummy store" >&2
    exit 1
fi

# ── Start dummy store ─────────────────────────────────────────────────────────
echo "[2/8] Starting dummy store..."
STORE_PORT=19876
start_dummy_store "$STORE_PORT" "$STORE_ROOT" "$STORE_LOG"

echo "      Waiting for dummy store to become ready..."
if ! wait_for_dummy_store 40 0.25; then
    echo "FAIL: dummy store did not become ready"
    exit 1
fi
echo "      Dummy store is ready at $DUMMY_STORE_URL"

# ── Create a synthetic test package ──────────────────────────────────────────
echo "[3/8] Creating synthetic test package..."
TEST_PACK="$TEST_LOG_DIR/testpkg.pack"
echo "SYNTHETIC-PACKAGE-CONTENT-FOR-ROUNDTRIP-TEST" > "$TEST_PACK"
assert_file_nonempty "$TEST_PACK" "synthetic test package"

# ── Publish (seed) the package ────────────────────────────────────────────────
echo "[4/8] Seeding package into dummy store..."
revision=$(seed_dummy_store \
    "testpublisher" "hello-world" \
    "linux" "amd64" "stable" \
    1 0 0 \
    "$TEST_PACK")

if [[ $? -ne 0 || -z "$revision" ]]; then
    echo "FAIL: seed_dummy_store failed" >&2
    exit 1
fi
echo "      Package seeded as revision $revision"

# ── Retrieve via GET /package/revision ───────────────────────────────────────
echo "[5/8] Resolving latest revision..."
rev_resp=$(curl -sf \
    "${DUMMY_STORE_URL}/package/revision?publisher=testpublisher&name=hello-world&platform=linux&arch=amd64&channel=stable" \
    2>&1)
assert_success $? "GET /package/revision"

rev_num=$(echo "$rev_resp" | python3 -c "import sys,json; print(json.load(sys.stdin)['revision'])")
assert_contains "$rev_num" "$revision" "resolved revision matches seeded revision"
echo "      Resolved revision: $rev_num"

# ── Download via GET /package/download ───────────────────────────────────────
echo "[6/8] Downloading package..."
DOWNLOADED_PACK="$DOWNLOAD_DIR/downloaded.pack"
dl_rc=0
curl -sf \
    "${DUMMY_STORE_URL}/package/download?publisher=testpublisher&name=hello-world&revision=${revision}" \
    -o "$DOWNLOADED_PACK" 2>&1 || dl_rc=$?
assert_success "$dl_rc" "GET /package/download"
assert_file_nonempty "$DOWNLOADED_PACK" "downloaded package"

# Verify content matches what was seeded
original_content=$(cat "$TEST_PACK")
downloaded_content=$(cat "$DOWNLOADED_PACK")
assert_contains "$downloaded_content" "SYNTHETIC-PACKAGE-CONTENT-FOR-ROUNDTRIP-TEST" \
    "downloaded content matches seeded content"
echo "      Download verified — content matches original"

# ── Query via GET /package/find ───────────────────────────────────────────────
echo "[7/8] Testing find and info endpoints..."
find_resp=$(curl -sf \
    "${DUMMY_STORE_URL}/package/find?search=hello-world" 2>&1)
assert_success $? "GET /package/find"
assert_contains "$find_resp" "testpublisher" "find response contains publisher"
assert_contains "$find_resp" "hello-world"   "find response contains package name"
echo "      Find returned matching results"

info_resp=$(curl -sf \
    "${DUMMY_STORE_URL}/package/info?publisher=testpublisher&name=hello-world" 2>&1)
assert_success $? "GET /package/info"
assert_contains "$info_resp" "testpublisher" "info response contains publisher"
assert_contains "$info_resp" "hello-world"   "info response contains package name"
assert_contains "$info_resp" '"revision"'    "info response contains revision list"
echo "      Info returned package metadata including revisions"

# ── Negative path: missing package returns 404 ───────────────────────────────
echo "[8/8] Negative-path check: requesting a missing package..."
missing_rc=0
curl -sf \
    "${DUMMY_STORE_URL}/package/download?publisher=testpublisher&name=does-not-exist&revision=1" \
    -o /dev/null 2>&1 || missing_rc=$?
# curl -sf exits with non-zero when HTTP status is 400+
assert_failure "$missing_rc" "GET /package/download for missing package returns error"
echo "      Missing package correctly returned an error (curl rc=$missing_rc)"

echo ""
echo "PASS: $TEST_NAME"
