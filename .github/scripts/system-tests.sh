#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

bake_bin="$root_dir/build/bin/bake"
cvd_bin="$root_dir/build/daemons/cvd/cvd"

if [[ ! -x "$bake_bin" ]]; then
    echo "ERROR: bake binary not found/executable at $bake_bin" >&2
    exit 1
fi

if [[ ! -x "$cvd_bin" ]]; then
    echo "ERROR: cvd binary not found/executable at $cvd_bin" >&2
    exit 1
fi

log_dir="${RUNNER_TEMP:-/tmp}"
cvd_log="$log_dir/cvd-ci.log"

cvd_pid=""
cvd_via_sudo=0
build_start_time=""
build_failed=0

# Enable seccomp logging if requested for debugging
# This can be set to "1" when investigating seccomp-related build failures
# export CONTAINERV_SECCOMP_LOG=1
if [[ "${CONTAINERV_SECCOMP_LOG:-0}" == "1" ]]; then
    echo "NOTE: Seccomp logging is enabled (CONTAINERV_SECCOMP_LOG=1)"
    echo "      Denied syscalls will be logged to audit/kernel logs instead of just returning EPERM"
fi

# Helper to dump seccomp denial logs on build failure
dump_seccomp_logs() {
    if [[ "$build_failed" -eq 0 || -z "$build_start_time" ]]; then
        return 0
    fi

    echo ""
    echo "=== Checking for seccomp denials since build start ==="
    
    # Check if seccomp logging was enabled
    if [[ "${CONTAINERV_SECCOMP_LOG:-0}" != "1" ]]; then
        echo "NOTE: Seccomp logging is disabled. Set CONTAINERV_SECCOMP_LOG=1 to enable."
        echo "      Without logging enabled, seccomp violations return EPERM silently."
        echo ""
        return 0
    fi
    
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

    local found_any=0

    # Try ausearch first (most detailed, structured output for audit events)
    if command -v ausearch >/dev/null 2>&1; then
        local audit_output
        audit_output="$(sudo -n ausearch -m SECCOMP -ts "$build_start_time" 2>/dev/null || true)"

        if [[ -n "$audit_output" ]]; then
            echo "Seccomp denials found in audit log:"
            echo "$audit_output"
            found_any=1
        fi
    fi

    # Also check journalctl (kernel logs with reliable timestamps)
    if command -v journalctl >/dev/null 2>&1; then
        local journal_output
        journal_output="$(sudo -n journalctl -k --since "$build_start_time" 2>/dev/null | grep -i seccomp || true)"

        if [[ -n "$journal_output" ]]; then
            echo "Seccomp denials found in kernel log:"
            echo "$journal_output"
            found_any=1
        fi
    fi

    # Also check dmesg as additional fallback
    if command -v dmesg >/dev/null 2>&1; then
        local dmesg_output
        dmesg_output="$(sudo -n dmesg -T 2>/dev/null | grep -i seccomp || true)"

        if [[ -n "$dmesg_output" ]]; then
            echo "Seccomp denials found in dmesg:"
            echo "$dmesg_output"
            found_any=1
        fi
    fi

    if [[ "$found_any" -eq 0 ]]; then
        echo "No seccomp denials found in any available logs since '$build_start_time'"
    fi

    echo "=== End of seccomp log check ==="
}

# cvd currently requires root on Linux. Start it via sudo when not root.
if [[ "$(id -u)" -eq 0 ]]; then
    "$cvd_bin" -vv >"$cvd_log" 2>&1 &
    cvd_pid=$!
else
    if ! command -v sudo >/dev/null 2>&1; then
        echo "ERROR: cvd must run as root, but sudo is not available" >&2
        exit 1
    fi

    # Capture the real cvd PID from the privileged shell.
    # Pass through CONTAINERV_SECCOMP_LOG if set for debugging
    cvd_pid="$(sudo -n bash -c "CONTAINERV_SECCOMP_LOG=${CONTAINERV_SECCOMP_LOG:-0} \"$cvd_bin\" -vv >\"$cvd_log\" 2>&1 & echo \$!")" || {
        echo "ERROR: failed to start cvd via sudo (is passwordless sudo available in CI?)" >&2
        exit 1
    }
    cvd_via_sudo=1
fi

cleanup() {
    # Dump seccomp logs if build failed
    dump_seccomp_logs

    if [[ -n "${cvd_pid:-}" ]]; then
        if [[ "$cvd_via_sudo" -eq 1 ]]; then
            sudo -n kill "$cvd_pid" >/dev/null 2>&1 || true
        else
            kill "$cvd_pid" >/dev/null 2>&1 || true
            wait "$cvd_pid" >/dev/null 2>&1 || true
        fi

        echo "cvd log output (last 1000 lines):"
        tail -n 1000 "$cvd_log" || true
    fi

    if [[ -n "${work_dir:-}" && -d "${work_dir:-}" ]]; then
        rm -rf "$work_dir" || true
    fi
}
trap cleanup EXIT

# Give cvd a moment to start listening on @/chef/cvd/api
for _ in {1..20}; do
    if [[ "$cvd_via_sudo" -eq 1 ]]; then
        sudo -n kill -0 "$cvd_pid" >/dev/null 2>&1 || {
            echo "ERROR: cvd exited early. Log:" >&2
            tail -n 200 "$cvd_log" >&2 || true
            exit 1
        }
    elif ! kill -0 "$cvd_pid" >/dev/null 2>&1; then
        echo "ERROR: cvd exited early. Log:" >&2
        tail -n 200 "$cvd_log" >&2 || true
        exit 1
    fi
    sleep 0.25
done

work_dir="$(mktemp -d)"

# Run from a folder that contains hello.yaml so relative paths line up,
# but keep outputs (like *.pack files) isolated from the git checkout.
cp -a "$root_dir/examples/recipes/hello.yaml" "$work_dir/hello.yaml"
cp -a "$root_dir/examples/recipes/hello-world" "$work_dir/hello-world"

pushd "$work_dir" >/dev/null

# Capture timestamp before build for seccomp log filtering
build_start_time="$(date '+%Y-%m-%d %H:%M:%S')"

# Must run from the recipe directory so relative paths resolve.
# Also keep a timeout to avoid wedging CI if a container backend hangs.
if command -v timeout >/dev/null 2>&1; then
    if ! timeout 20m "$bake_bin" build hello.yaml -v; then
        build_failed=1
        exit 1
    fi
else
    if ! "$bake_bin" build hello.yaml -v; then
        build_failed=1
        exit 1
    fi
fi

# Verify artifact was created
shopt -s nullglob
pack_files=("$work_dir"/*.pack)
shopt -u nullglob

if (( ${#pack_files[@]} == 0 )); then
    echo "ERROR: system test succeeded but no .pack file was produced" >&2
    build_failed=1
    exit 1
fi

for f in "${pack_files[@]}"; do
    if [[ ! -s "$f" ]]; then
        echo "ERROR: produced .pack file is empty: $f" >&2
        build_failed=1
        exit 1
    fi
done

popd >/dev/null

echo "System tests completed successfully."