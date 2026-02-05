#!/usr/bin/env bash

set -euo pipefail

# use non-interactive sudo
SUDO="sudo -n"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
LOG_DIR="${RUNNER_TEMP:-/tmp}"

CMD_BAKE="$ROOT_DIR/build/bin/bake"
CMD_CVD="$ROOT_DIR/build/daemons/cvd/cvd"

CVD_LOG="$LOG_DIR/cvd-ci.log"

# track build status
BUILD_FAILED=0

if ! command -v sudo >/dev/null 2>&1; then
    echo "ERROR: sudo is not available" >&2
    exit 1
fi

if [[ ! -x "$CMD_BAKE" ]]; then
    echo "ERROR: bake binary not found/executable at $CMD_BAKE" >&2
    exit 1
fi

if [[ ! -x "$CMD_CVD" ]]; then
    echo "ERROR: cvd binary not found/executable at $CMD_CVD" >&2
    exit 1
fi

# Enable seccomp logging if requested for debugging
# This can be set to "1" when investigating seccomp-related build failures
# export CONTAINERV_SECCOMP_LOG=1
if [[ "${CONTAINERV_SECCOMP_LOG:-0}" == "1" ]]; then
    echo "NOTE: Seccomp logging is enabled (CONTAINERV_SECCOMP_LOG=1)"
    echo "      Denied syscalls will be logged to audit/kernel logs instead of just returning EPERM"
fi

# Helper to dump seccomp denial logs
dump_seccomp_logs() {    
    # Check if seccomp logging was enabled
    if [[ "${CONTAINERV_SECCOMP_LOG:-0}" != "1" ]]; then
        # Only show this message if build failed
        if [[ "$BUILD_FAILED" -eq 1 ]]; then
            echo ""
            echo "=== Seccomp logging disabled ==="
            echo "NOTE: Seccomp logging is disabled. Set CONTAINERV_SECCOMP_LOG=1 to enable."
            echo "      Without logging enabled, seccomp violations return EPERM silently."
            echo ""
        fi
        return 0
    fi

    echo ""
    echo "=== Checking for seccomp denials since build start ==="
    
    # Always log seccomp logging status first
    echo "Seccomp logging status:"
    if command -v sysctl >/dev/null 2>&1; then
        sysctl kernel.seccomp.actions_logged 2>/dev/null || echo "  kernel.seccomp.actions_logged: not available"
    else
        echo "  sysctl not available"
    fi
    
    if command -v systemctl >/dev/null 2>&1; then
        if systemctl is-active auditd >/dev/null 2>&1; then
            echo "  Audit daemon: running"
        else
            echo "  Audit daemon: not running"
        fi
    fi
    echo ""

    local FOUND_LOGS=0

    # Try ausearch first (most detailed, structured output for audit events)
    if command -v ausearch >/dev/null 2>&1; then
        local AUDIT_LOGS
        AUDIT_LOGS="$($SUDO ausearch -m SECCOMP 2>/dev/null || true)"

        if [[ -n "$AUDIT_LOGS" ]]; then
            echo "Seccomp denials found in audit log:"
            echo "$AUDIT_LOGS"
            FOUND_LOGS=1
        fi
    fi
    
    if [[ "$FOUND_LOGS" -eq 0 ]] && command -v journalctl >/dev/null 2>&1; then
        local JOURNAL_LOGS
        JOURNAL_LOGS="$($SUDO journalctl -b -k 2>/dev/null | grep -i seccomp || true)"

        if [[ -n "$JOURNAL_LOGS" ]]; then
            echo "Seccomp denials found in kernel log:"
            echo "$JOURNAL_LOGS"
            FOUND_LOGS=1
        fi
    fi
    
    if [[ "$FOUND_LOGS" -eq 0 ]] && [[ -f /var/log/syslog ]]; then
        local SYSLOG_LOGS
        SYSLOG_LOGS="$($SUDO grep -i audit /var/log/syslog 2>/dev/null | grep -i seccomp || true)"

        if [[ -n "$SYSLOG_LOGS" ]]; then
            echo "Seccomp denials found in syslog:"
            echo "$SYSLOG_LOGS"
            FOUND_LOGS=1
        fi
    fi
    
    if [[ "$FOUND_LOGS" -eq 0 ]] && command -v dmesg >/dev/null 2>&1; then
        local DMESG_LOGS
        DMESG_LOGS="$($SUDO dmesg 2>/dev/null | grep -i seccomp || true)"

        if [[ -n "$DMESG_LOGS" ]]; then
            echo "Seccomp denials found in dmesg:"
            echo "$DMESG_LOGS"
            FOUND_LOGS=1
        fi
    fi

    if [[ "$FOUND_LOGS" -eq 0 ]]; then
        echo "No seccomp denials found in any available logs"
    fi

    echo "=== End of seccomp log check ==="
}

# cvd currently requires root on Linux. Start it via sudo when not root.
CVD_PID=""
CVD_VIA_SUDO=0
if [[ "$(id -u)" -eq 0 ]]; then
    "$CMD_CVD" -vv >"$CVD_LOG" 2>&1 &
    CVD_PID=$!
else
    # Capture the real cvd PID from the privileged shell.
    # Pass through CONTAINERV_SECCOMP_LOG if set for debugging
    CVD_PID="$($SUDO bash -c "CONTAINERV_SECCOMP_LOG=${CONTAINERV_SECCOMP_LOG:-0} \"$CMD_CVD\" -vv >\"$CVD_LOG\" 2>&1 & echo \$!")" || {
        echo "ERROR: failed to start cvd via sudo (is passwordless sudo available in CI?)" >&2
        exit 1
    }
    CVD_VIA_SUDO=1
fi

cleanup() {
    # Check for seccomp violations (if logging enabled)
    dump_seccomp_logs

    if [[ -n "${CVD_PID:-}" ]]; then
        if [[ "$CVD_VIA_SUDO" -eq 1 ]]; then
            $SUDO kill "$CVD_PID" >/dev/null 2>&1 || true
        else
            kill "$CVD_PID" >/dev/null 2>&1 || true
            wait "$CVD_PID" >/dev/null 2>&1 || true
        fi

        echo "cvd log output (last 1000 lines):"
        tail -n 1000 "$CVD_LOG" || true
    fi

    if [[ -n "${work_dir:-}" && -d "${work_dir:-}" ]]; then
        rm -rf "$work_dir" || true
    fi
}
trap cleanup EXIT

# Give cvd a moment to start listening on @/chef/cvd/api
for _ in {1..20}; do
    if [[ "$CVD_VIA_SUDO" -eq 1 ]]; then
        $SUDO kill -0 "$CVD_PID" >/dev/null 2>&1 || {
            echo "ERROR: cvd exited early. Log:" >&2
            tail -n 200 "$CVD_LOG" >&2 || true
            exit 1
        }
    elif ! kill -0 "$CVD_PID" >/dev/null 2>&1; then
        echo "ERROR: cvd exited early. Log:" >&2
        tail -n 200 "$CVD_LOG" >&2 || true
        exit 1
    fi
    sleep 0.25
done

work_dir="$(mktemp -d)"

# Run from a folder that contains hello.yaml so relative paths line up,
# but keep outputs (like *.pack files) isolated from the git checkout.
cp -a "$ROOT_DIR/examples/recipes/hello.yaml" "$work_dir/hello.yaml"
cp -a "$ROOT_DIR/examples/recipes/hello-world" "$work_dir/hello-world"

pushd "$work_dir" >/dev/null

# Must run from the recipe directory so relative paths resolve.
# Also keep a timeout to avoid wedging CI if a container backend hangs.
if command -v timeout >/dev/null 2>&1; then
    if ! timeout 20m "$CMD_BAKE" build hello.yaml -v; then
        BUILD_FAILED=1
        exit 1
    fi
else
    if ! "$CMD_BAKE" build hello.yaml -v; then
        BUILD_FAILED=1
        exit 1
    fi
fi

# Verify artifact was created
shopt -s nullglob
pack_files=("$work_dir"/*.pack)
shopt -u nullglob

if (( ${#pack_files[@]} == 0 )); then
    echo "ERROR: system test succeeded but no .pack file was produced" >&2
    BUILD_FAILED=1
    exit 1
fi

for f in "${pack_files[@]}"; do
    if [[ ! -s "$f" ]]; then
        echo "ERROR: produced .pack file is empty: $f" >&2
        BUILD_FAILED=1
        exit 1
    fi
done

popd >/dev/null

echo "System tests completed successfully."