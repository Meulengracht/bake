#!/usr/bin/env bash
# process.sh — command execution and readiness helpers
#
# Source this file after env.sh.

# Run a command, capturing combined stdout+stderr into a variable.
# Usage:  run_cmd OUTPUT_VAR cmd [args...]
# Returns the command's exit code; output is stored in OUTPUT_VAR.
run_cmd() {
    local _outvar="$1"; shift
    local _output
    _output="$("$@" 2>&1)"
    local _rc=$?
    # Use printf to avoid eval, safe for all content
    printf -v "$_outvar" '%s' "$_output"
    return $_rc
}

# Run a command with a wall-clock timeout (in seconds).
# Wraps the system `timeout` command if available; otherwise runs without limit.
# Usage:  run_cmd_with_timeout SECONDS OUTPUT_VAR cmd [args...]
run_cmd_with_timeout() {
    local _timeout="$1"; shift
    local _outvar="$1"; shift
    local _output _rc

    if command -v timeout >/dev/null 2>&1; then
        _output="$(timeout "$_timeout" "$@" 2>&1)"
        _rc=$?
    else
        _output="$("$@" 2>&1)"
        _rc=$?
    fi
    printf -v "$_outvar" '%s' "$_output"
    return $_rc
}

# Poll for a Unix-domain socket to appear.
# Usage:  wait_for_socket PATH [MAX_RETRIES] [SLEEP_SECS]
# Returns 0 when found, 1 after timeout.
wait_for_socket() {
    local socket_path="$1"
    local max_retries="${2:-40}"
    local sleep_secs="${3:-0.25}"
    local i=0

    while [[ $i -lt $max_retries ]]; do
        if [[ -S "$socket_path" ]]; then
            return 0
        fi
        sleep "$sleep_secs"
        i=$((i + 1))
    done
    echo "TIMEOUT: socket $socket_path did not appear after $max_retries attempts" >&2
    return 1
}

# Poll for a regular file to appear and be non-empty.
# Usage:  wait_for_file PATH [MAX_RETRIES] [SLEEP_SECS]
wait_for_file() {
    local file_path="$1"
    local max_retries="${2:-40}"
    local sleep_secs="${3:-0.25}"
    local i=0

    while [[ $i -lt $max_retries ]]; do
        if [[ -s "$file_path" ]]; then
            return 0
        fi
        sleep "$sleep_secs"
        i=$((i + 1))
    done
    echo "TIMEOUT: file $file_path did not appear after $max_retries attempts" >&2
    return 1
}

# Poll until a glob pattern matches at least one non-empty file.
# Usage:  wait_for_glob GLOB_PATTERN [MAX_RETRIES] [SLEEP_SECS]
wait_for_glob() {
    local pattern="$1"
    local max_retries="${2:-40}"
    local sleep_secs="${3:-0.5}"
    local i=0

    while [[ $i -lt $max_retries ]]; do
        # shellcheck disable=SC2206
        local matches=( $pattern )
        if [[ ${#matches[@]} -gt 0 && -s "${matches[0]}" ]]; then
            return 0
        fi
        sleep "$sleep_secs"
        i=$((i + 1))
    done
    echo "TIMEOUT: no non-empty file matched '$pattern' after $max_retries attempts" >&2
    return 1
}
