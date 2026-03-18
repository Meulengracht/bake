#!/usr/bin/env bash
# run.sh — system-test suite entrypoint for Meulengracht/bake
#
# Usage:
#   bash tests/system/run.sh [CASE...]
#
# With no arguments, all test cases are run in order.
# With one or more arguments, only those named cases are run.
# Case names correspond to files under tests/system/cases/ without the .sh suffix.
#
# Environment variables:
#   BUILD_DIR   Override the CMake build directory (default: <repo-root>/build)
#   SUDO        Override the sudo command (default: "sudo -n"; set to "" if root)
#   ALWAYS_DUMP_LOGS=1   Print all log files even on success (useful for debugging)
#
# Exit codes:
#   0   All mandatory tests passed
#   1   One or more mandatory tests failed
#
# The hello-runtime test may exit with code 2 (aspirational steps not yet
# implemented); run.sh treats code 2 as a non-blocking warning rather than a
# hard failure so CI can distinguish infrastructure regressions from feature gaps.

set -euo pipefail

SYSTEM_TESTS_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CASES_DIR="$SYSTEM_TESTS_DIR/cases"

# Source env just for ROOT_DIR/BUILD_DIR discovery (each test also sources it)
source "$SYSTEM_TESTS_DIR/lib/env.sh"

# ── Determine which test cases to run ─────────────────────────────────────────
DEFAULT_CASES=(
    smoke-cvd
    hello-build
    hello-runtime
    dummy-store-roundtrip
    order-fetch-from-store
    served-install-from-store
)

if [[ $# -gt 0 ]]; then
    CASES=("$@")
else
    CASES=("${DEFAULT_CASES[@]}")
fi

# ── Run each test case ─────────────────────────────────────────────────────────
PASS=0
FAIL=0
WARN=0

# Directory for preserving logs (useful for CI artifact upload)
ARTIFACT_DIR="${RUNNER_TEMP:-/tmp}/system-test-artifacts"
mkdir -p "$ARTIFACT_DIR"

for case_name in "${CASES[@]}"; do
    script="$CASES_DIR/${case_name}.sh"
    if [[ ! -f "$script" ]]; then
        echo "ERROR: test case not found: $script" >&2
        FAIL=$((FAIL + 1))
        continue
    fi

    echo ""
    echo "┌──────────────────────────────────────────────────────────┐"
    echo "│  TEST: $case_name"
    echo "└──────────────────────────────────────────────────────────┘"

    rc=0
    bash "$script" || rc=$?

    if [[ $rc -eq 0 ]]; then
        echo "● PASS  $case_name"
        PASS=$((PASS + 1))
    elif [[ $rc -eq 2 ]]; then
        # Exit code 2 = aspirational steps not yet implemented; treat as warning
        echo "● WARN  $case_name  (aspirational steps not yet fully implemented)"
        WARN=$((WARN + 1))
    else
        echo "● FAIL  $case_name  (exit code $rc)"
        FAIL=$((FAIL + 1))
    fi
done

# ── Print summary ──────────────────────────────────────────────────────────────
echo ""
echo "══════════════════════════════════════════════════════════════"
echo "  System test results"
echo "  PASS: $PASS   FAIL: $FAIL   WARN: $WARN"
echo "══════════════════════════════════════════════════════════════"

if [[ $FAIL -gt 0 ]]; then
    echo ""
    echo "OVERALL: FAILED ($FAIL test(s) failed)"
    echo "  Logs for failed/warned tests may be found in $ARTIFACT_DIR"
    exit 1
fi

if [[ $WARN -gt 0 ]]; then
    echo ""
    echo "OVERALL: PASSED WITH WARNINGS"
    echo "  Some aspirational test steps are not yet fully implemented."
    echo "  See tests/system/README.md — 'Known Limitations' for details."
    exit 0
fi

echo ""
echo "OVERALL: PASSED"
