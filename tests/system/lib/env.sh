#!/usr/bin/env bash
# env.sh — environment setup for the system-test harness
#
# Source this file from each test case or from run.sh.
# After sourcing, the following variables are set:
#
#   ROOT_DIR    — absolute path to the repository root
#   BUILD_DIR   — absolute path to the CMake build directory (default: ROOT_DIR/build)
#   SUDO        — sudo command to use (default: "sudo -n")
#   CMD_BAKE    — bake build tool
#   CMD_CVD     — cvd container daemon
#   CMD_SERVED  — served runtime daemon
#   CMD_SERVE   — serve CLI tool
#   SYSTEM_TESTS_DIR — absolute path to tests/system/

SYSTEM_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ROOT_DIR="$(cd "$SYSTEM_TESTS_DIR/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"

# Use non-interactive sudo by default; callers can override by setting SUDO=""
# before sourcing this file if running as root.
if [[ "$(id -u)" -eq 0 ]]; then
    SUDO=""
else
    SUDO="${SUDO:-sudo -n}"
fi

# Binary paths — keep in sync with .github/scripts/system-tests.sh
CMD_BAKE="$BUILD_DIR/bin/bake"
CMD_CVD="$BUILD_DIR/daemons/cvd/cvd"
CMD_SERVED="$BUILD_DIR/daemons/served/served"
CMD_SERVE="$BUILD_DIR/bin/serve"
CMD_ORDER="$BUILD_DIR/bin/order"

# Timeout for long-running build steps (e.g. bake build).
# Can be overridden by the caller before sourcing this file.
BUILD_TIMEOUT_MINS="${BUILD_TIMEOUT_MINS:-20}"

# Verify required binaries exist and are executable
_check_required_binary() {
    local bin="$1"
    if [[ ! -x "$bin" ]]; then
        echo "ERROR: required binary not found or not executable: $bin" >&2
        echo "       Make sure the project is built before running system tests." >&2
        return 1
    fi
}

env_check_binaries() {
    local all_ok=0
    _check_required_binary "$CMD_BAKE"  || all_ok=1
    _check_required_binary "$CMD_CVD"   || all_ok=1
    _check_required_binary "$CMD_SERVED"|| all_ok=1
    _check_required_binary "$CMD_SERVE" || all_ok=1
    return $all_ok
}

# Verify sudo is available when needed
env_check_sudo() {
    if [[ -n "$SUDO" ]]; then
        if ! command -v sudo >/dev/null 2>&1; then
            echo "ERROR: sudo is not available and tests are not running as root" >&2
            return 1
        fi
        # Quick sanity check that passwordless sudo works
        if ! sudo -n true >/dev/null 2>&1; then
            echo "ERROR: passwordless sudo is required but not configured" >&2
            return 1
        fi
    fi
    return 0
}
