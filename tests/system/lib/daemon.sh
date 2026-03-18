#!/usr/bin/env bash
# daemon.sh — daemon start/stop and readiness helpers
#
# Source this file after env.sh and process.sh.
#
# Global tracking arrays (populated by start_daemon_* functions):
#   _DAEMON_PIDS    — associative: name → PID
#   _DAEMON_VIA_SUDO — associative: name → "1" if started via sudo

declare -gA _DAEMON_PIDS=()
declare -gA _DAEMON_VIA_SUDO=()

# Start a daemon in the background as root (via sudo).
# The daemon's stdout+stderr are redirected to LOG_FILE.
# The real PID of the child process is stored in _DAEMON_PIDS[NAME].
#
# Usage:  start_daemon_as_root NAME LOG_FILE CMD [ARGS...]
#
# After calling this, use wait_for_cvd / wait_for_served (or a custom loop)
# to confirm the daemon is ready before proceeding.
start_daemon_as_root() {
    local name="$1"
    local log_file="$2"
    shift 2
    local cmd=("$@")

    local pid
    if [[ -z "$SUDO" ]]; then
        # Already running as root
        "${cmd[@]}" >"$log_file" 2>&1 &
        pid=$!
    else
        # Capture the PID of the privileged child.
        # We pass the log file via bash -c so redirection happens inside sudo.
        local cmd_str
        cmd_str="$(printf '%q ' "${cmd[@]}")"
        pid="$($SUDO bash -c "${cmd_str} >\"${log_file}\" 2>&1 & echo \$!")" || {
            echo "ERROR: failed to start daemon '$name' via sudo" >&2
            return 1
        }
    fi

    _DAEMON_PIDS["$name"]="$pid"
    _DAEMON_VIA_SUDO["$name"]="${SUDO:+1}"
    echo "  started daemon '$name' (PID $pid), log: $log_file"
}

# Start a daemon in the background as root (via sudo), with extra environment
# variables injected into the privileged process.
#
# Usage:  start_daemon_as_root_with_env NAME LOG_FILE "VAR=val ..." CMD [ARGS...]
#
# The EXTRA_ENV argument is a space-separated list of VAR=value assignments.
# Variable values must not contain spaces or shell special characters.
# Only test-controlled values (such as URLs and paths) should be passed here.
start_daemon_as_root_with_env() {
    local name="$1"
    local log_file="$2"
    local extra_env="$3"
    shift 3
    local cmd=("$@")

    local pid
    if [[ -z "$SUDO" ]]; then
        # Already running as root — evaluate each assignment in a subshell
        (
            IFS=' '
            for pair in $extra_env; do
                export "$pair"
            done
            "${cmd[@]}" >"$log_file" 2>&1
        ) &
        pid=$!
    else
        local cmd_str
        cmd_str="$(printf '%q ' "${cmd[@]}")"
        # Build one "export VAR=val" statement per pair.
        # Values are test-controlled (URLs, paths) and must not contain spaces.
        local env_exports=""
        IFS=' '
        for pair in $extra_env; do
            env_exports="export ${pair}; "
        done
        pid="$($SUDO bash -c "${env_exports}${cmd_str} >\"${log_file}\" 2>&1 & echo \$!")" || {
            echo "ERROR: failed to start daemon '$name' via sudo" >&2
            return 1
        }
    fi

    _DAEMON_PIDS["$name"]="$pid"
    _DAEMON_VIA_SUDO["$name"]="${SUDO:+1}"
    echo "  started daemon '$name' (PID $pid, env: $extra_env), log: $log_file"
}


start_daemon() {
    local name="$1"
    local log_file="$2"
    shift 2

    "$@" >"$log_file" 2>&1 &
    local pid=$!

    _DAEMON_PIDS["$name"]="$pid"
    _DAEMON_VIA_SUDO["$name"]=""
    echo "  started daemon '$name' (PID $pid), log: $log_file"
}

# Return 0 if a tracked daemon process is still alive, 1 otherwise.
# Usage:  daemon_is_alive NAME
daemon_is_alive() {
    local name="$1"
    local pid="${_DAEMON_PIDS[$name]:-}"

    if [[ -z "$pid" ]]; then
        return 1
    fi

    if [[ "${_DAEMON_VIA_SUDO[$name]:-}" == "1" ]]; then
        $SUDO kill -0 "$pid" >/dev/null 2>&1
    else
        kill -0 "$pid" >/dev/null 2>&1
    fi
}

# Stop a tracked daemon by name (SIGTERM, then SIGKILL after a grace period).
# Usage:  stop_daemon NAME [GRACE_SECONDS]
stop_daemon() {
    local name="$1"
    local grace="${2:-5}"
    local pid="${_DAEMON_PIDS[$name]:-}"

    if [[ -z "$pid" ]]; then
        return 0
    fi

    echo "  stopping daemon '$name' (PID $pid)"

    if [[ "${_DAEMON_VIA_SUDO[$name]:-}" == "1" ]]; then
        $SUDO kill "$pid" >/dev/null 2>&1 || true
        # Give it a moment to exit
        local i=0
        while [[ $i -lt $((grace * 4)) ]]; do
            if ! $SUDO kill -0 "$pid" >/dev/null 2>&1; then
                break
            fi
            sleep 0.25
            i=$((i + 1))
        done
        # Force-kill if still alive
        $SUDO kill -9 "$pid" >/dev/null 2>&1 || true
    else
        kill "$pid" >/dev/null 2>&1 || true
        wait "$pid" >/dev/null 2>&1 || true
    fi

    unset '_DAEMON_PIDS[$name]'
    unset '_DAEMON_VIA_SUDO[$name]'
}

# Stop all tracked daemons.
stop_all_daemons() {
    local name
    for name in "${!_DAEMON_PIDS[@]}"; do
        stop_daemon "$name"
    done
}

# Wait until cvd is alive (kill -0 succeeds) with a bounded retry loop.
# cvd does not expose an external socket, so liveness via kill -0 is sufficient.
# Usage:  wait_for_cvd [MAX_RETRIES] [SLEEP_SECS]
wait_for_cvd() {
    local max_retries="${1:-40}"
    local sleep_secs="${2:-0.25}"
    local i=0

    while [[ $i -lt $max_retries ]]; do
        if daemon_is_alive "cvd"; then
            return 0
        fi
        sleep "$sleep_secs"
        i=$((i + 1))
    done
    echo "TIMEOUT: cvd did not become alive after $max_retries retries" >&2
    return 1
}

# Wait until served's Unix socket appears, confirming it is ready to accept
# client connections.
# Usage:  wait_for_served [MAX_RETRIES] [SLEEP_SECS]
wait_for_served() {
    local max_retries="${1:-80}"
    local sleep_secs="${2:-0.25}"

    # First wait for the process to be alive
    local i=0
    while [[ $i -lt $max_retries ]]; do
        if daemon_is_alive "served"; then
            break
        fi
        sleep "$sleep_secs"
        i=$((i + 1))
    done

    if ! daemon_is_alive "served"; then
        echo "TIMEOUT: served process never became alive" >&2
        return 1
    fi

    # Then wait for the socket
    wait_for_socket "/tmp/served" "$max_retries" "$sleep_secs"
}
