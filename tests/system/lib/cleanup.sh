#!/usr/bin/env bash
# cleanup.sh — teardown helpers for the system-test harness
#
# Usage pattern in a test case:
#
#   source .../lib/cleanup.sh
#
#   TEST_NAME="my-test"
#   TEST_LOG_DIR="$(mktemp -d)"          # directory for this test's logs
#   register_tmpdir "$TEST_LOG_DIR"
#
#   trap 'teardown_test' EXIT
#
#   ... test body ...
#
# On exit (normal or error) teardown_test will:
#   1. Stop all tracked daemons
#   2. Print the contents of every registered log file
#   3. Remove registered temp directories

# Registered temporary directories to remove on exit
declare -a _CLEANUP_DIRS=()

# Registered log files to print on exit
declare -a _CLEANUP_LOGS=()

# Whether to always dump logs (default: only on failure)
ALWAYS_DUMP_LOGS="${ALWAYS_DUMP_LOGS:-0}"

# Register a temp directory to be removed during teardown.
register_tmpdir() {
    _CLEANUP_DIRS+=("$1")
}

# Register a log file to be printed during teardown.
register_log() {
    _CLEANUP_LOGS+=("$1")
}

# Print the last N lines of a log file with a header.
dump_log() {
    local log_file="$1"
    local tail_lines="${2:-500}"
    if [[ -f "$log_file" ]]; then
        echo ""
        echo "=== Log: $log_file (last $tail_lines lines) ==="
        tail -n "$tail_lines" "$log_file" || true
        echo "=== End of log ==="
    else
        echo "(log file not found: $log_file)"
    fi
}

# Called from the EXIT trap.  Stops daemons, dumps logs, removes temp dirs.
teardown_test() {
    local exit_code=$?
    echo ""
    echo "--- Teardown: ${TEST_NAME:-test} (exit=$exit_code) ---"

    # Stop all daemons tracked by daemon.sh
    if declare -f stop_all_daemons >/dev/null 2>&1; then
        stop_all_daemons
    fi

    # Remove the served socket so subsequent tests start clean
    if [[ -S "/tmp/served" ]]; then
        rm -f "/tmp/served" 2>/dev/null || \
            { [[ -n "${SUDO:-}" ]] && $SUDO rm -f "/tmp/served" 2>/dev/null || true; }
    fi

    # Print logs if test failed or ALWAYS_DUMP_LOGS=1
    if [[ $exit_code -ne 0 || "${ALWAYS_DUMP_LOGS:-0}" == "1" ]]; then
        for log in "${_CLEANUP_LOGS[@]:-}"; do
            dump_log "$log"
        done
    fi

    # On failure, copy logs to the CI artifact directory before cleaning up.
    # RUNNER_TEMP is set by GitHub Actions; fall back to /tmp for local runs.
    if [[ $exit_code -ne 0 ]]; then
        local artifact_dir="${RUNNER_TEMP:-/tmp}/system-test-artifacts/${TEST_NAME:-unknown}"
        preserve_logs "$artifact_dir"
    fi

    # Remove temp directories
    for d in "${_CLEANUP_DIRS[@]:-}"; do
        if [[ -d "$d" ]]; then
            rm -rf "$d" || true
        fi
    done
}

# Copy all registered log files into a target directory (useful for CI artifact upload).
# Usage:  preserve_logs /path/to/artifact/dir
preserve_logs() {
    local dest="$1"
    mkdir -p "$dest"
    for log in "${_CLEANUP_LOGS[@]:-}"; do
        if [[ -f "$log" ]]; then
            cp "$log" "$dest/" 2>/dev/null || true
        fi
    done
    echo "  logs preserved to $dest"
}
