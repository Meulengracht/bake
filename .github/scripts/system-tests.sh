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
  cvd_pid="$(sudo -n bash -c "\"$cvd_bin\" -vv >\"$cvd_log\" 2>&1 & echo \$!")" || {
    echo "ERROR: failed to start cvd via sudo (is passwordless sudo available in CI?)" >&2
    exit 1
  }
  cvd_via_sudo=1
fi

cleanup() {
  if [[ -n "${cvd_pid:-}" ]]; then
    if [[ "$cvd_via_sudo" -eq 1 ]]; then
      sudo -n kill "$cvd_pid" >/dev/null 2>&1 || true
    else
      kill "$cvd_pid" >/dev/null 2>&1 || true
      wait "$cvd_pid" >/dev/null 2>&1 || true
    fi

    echo "cvd log output (last 200 lines):"
    tail -n 200 "$cvd_log" || true
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

# Must run from the recipe directory so relative paths resolve.
# Also keep a timeout to avoid wedging CI if a container backend hangs.
if command -v timeout >/dev/null 2>&1; then
  timeout 20m "$bake_bin" build hello.yaml -v
else
  "$bake_bin" build hello.yaml -v
fi

# Verify artifact was created
shopt -s nullglob
pack_files=("$work_dir"/*.pack)
shopt -u nullglob

if (( ${#pack_files[@]} == 0 )); then
  echo "ERROR: system test succeeded but no .pack file was produced" >&2
  exit 1
fi

for f in "${pack_files[@]}"; do
  if [[ ! -s "$f" ]]; then
    echo "ERROR: produced .pack file is empty: $f" >&2
    exit 1
  fi
done

popd >/dev/null

echo "System tests completed successfully."