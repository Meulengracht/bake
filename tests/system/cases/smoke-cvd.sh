#!/usr/bin/env bash
# smoke-cvd.sh — verify that the cvd container daemon starts and stays healthy
#
# What this test checks:
#   1. cvd binary is present and executable
#   2. cvd starts without immediately exiting
#   3. cvd remains alive for a liveness check interval
#   4. cvd is cleanly shut down during teardown
#
# This test preserves and formalises the existing CI smoke check for cvd.

set -euo pipefail

TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
source "$TESTS_DIR/lib/env.sh"
source "$TESTS_DIR/lib/cleanup.sh"
source "$TESTS_DIR/lib/process.sh"
source "$TESTS_DIR/lib/daemon.sh"
source "$TESTS_DIR/lib/assert.sh"

TEST_NAME="smoke-cvd"
TEST_LOG_DIR="$(mktemp -d)"
CVD_LOG="$TEST_LOG_DIR/cvd.log"

register_tmpdir "$TEST_LOG_DIR"
register_log    "$CVD_LOG"
trap 'teardown_test' EXIT

echo "=== $TEST_NAME ==="

# ── Preflight ─────────────────────────────────────────────────────────────────
echo "[1/3] Checking prerequisites..."
env_check_binaries
env_check_sudo

# ── Start cvd ─────────────────────────────────────────────────────────────────
echo "[2/3] Starting cvd..."
start_daemon_as_root "cvd" "$CVD_LOG" "$CMD_CVD" -vv

echo "      Waiting for cvd to become alive..."
if ! wait_for_cvd 40 0.25; then
    echo "FAIL: cvd did not become alive"
    dump_log "$CVD_LOG" 200
    exit 1
fi

echo "      cvd is alive."

# ── Liveness hold ─────────────────────────────────────────────────────────────
echo "[3/3] Confirming cvd remains healthy for 2 seconds..."
sleep 2

if ! daemon_is_alive "cvd"; then
    echo "FAIL: cvd exited unexpectedly after startup"
    dump_log "$CVD_LOG" 200
    exit 1
fi

echo "      cvd is still running — smoke check passed."
echo ""
echo "PASS: $TEST_NAME"
