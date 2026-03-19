#!/usr/bin/env bash
# store.sh — helpers for starting, seeding, and stopping the dummy store
#
# Source this file after env.sh, process.sh, daemon.sh, and cleanup.sh.
#
# Exported variables (set after start_dummy_store):
#   DUMMY_STORE_PORT  — TCP port the server is listening on
#   DUMMY_STORE_URL   — http://127.0.0.1:<PORT>  (also exported as CHEF_STORE_URL)
#   DUMMY_STORE_PID   — process ID of the Python server
#   DUMMY_STORE_ROOT  — filesystem root used by the server for package data

# Locate the Python script relative to this file
STORE_LIB_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DUMMY_STORE_SCRIPT="$STORE_LIB_DIR/dummy-store.py"

# Start the dummy store in the background.
# Usage: start_dummy_store PORT ROOT_DIR LOG_FILE
start_dummy_store() {
    local port="$1"
    local root_dir="$2"
    local log_file="$3"

    if [[ ! -f "$DUMMY_STORE_SCRIPT" ]]; then
        echo "ERROR: dummy-store.py not found at $DUMMY_STORE_SCRIPT" >&2
        return 1
    fi

    if ! command -v python3 >/dev/null 2>&1; then
        echo "ERROR: python3 is required to run the dummy store" >&2
        return 1
    fi

    mkdir -p "$root_dir"

    python3 "$DUMMY_STORE_SCRIPT" --port "$port" --root "$root_dir" >"$log_file" 2>&1 &
    local pid=$!

    _DUMMY_STORE_PID="$pid"
    DUMMY_STORE_PORT="$port"
    DUMMY_STORE_ROOT="$root_dir"
    DUMMY_STORE_URL="http://127.0.0.1:${port}"
    export CHEF_STORE_URL="$DUMMY_STORE_URL"

    # Register with daemon.sh tracking so stop_all_daemons cleans it up
    _DAEMON_PIDS["dummy-store"]="$pid"
    _DAEMON_VIA_SUDO["dummy-store"]=""

    echo "  started dummy store (PID $pid, port $port, root $root_dir)"
    echo "  log: $log_file"
}

# Wait for the dummy store HTTP server to become ready.
# Usage: wait_for_dummy_store [MAX_RETRIES] [SLEEP_SECS]
wait_for_dummy_store() {
    local max_retries="${1:-40}"
    local sleep_secs="${2:-0.25}"
    local i=0

    while [[ $i -lt $max_retries ]]; do
        if curl -sf --max-time 1 "${DUMMY_STORE_URL}/package/find?search=_probe_" \
                >/dev/null 2>&1; then
            return 0
        fi
        sleep "$sleep_secs"
        i=$((i + 1))
    done
    echo "TIMEOUT: dummy store did not become ready after $max_retries attempts" >&2
    return 1
}

# Seed the dummy store with a package blob using the three-step publish API.
#
# Usage: seed_dummy_store PUBLISHER NAME PLATFORM ARCH CHANNEL MAJOR MINOR PATCH PACK_FILE
#
# Returns 0 on success.  The revision assigned is printed to stdout.
seed_dummy_store() {
    local publisher="$1"
    local name="$2"
    local platform="$3"
    local arch="$4"
    local channel="$5"
    local major="$6"
    local minor="$7"
    local patch="$8"
    local pack_file="$9"

    if [[ ! -f "$pack_file" ]]; then
        echo "ERROR: seed_dummy_store: pack file not found: $pack_file" >&2
        return 1
    fi

    local size
    size=$(wc -c < "$pack_file")

    # Step 1: initiate
    local initiate_body
    initiate_body=$(cat <<EOF
{
  "PublisherName": "$publisher",
  "PackageName":   "$name",
  "Platform":      "$platform",
  "Architecture":  "$arch",
  "Major":         $major,
  "Minor":         $minor,
  "Patch":         $patch,
  "Size":          $size
}
EOF
)
    local initiate_response
    initiate_response=$(curl -sf \
        -X POST \
        -H "Authorization: Bearer test-token" \
        -H "Content-Type: application/json" \
        -d "$initiate_body" \
        "${DUMMY_STORE_URL}/package/publish/initiate" 2>&1)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: seed_dummy_store: initiate failed: $initiate_response" >&2
        return 1
    fi

    local token revision
    token=$(echo "$initiate_response"    | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['upload-token'])" 2>/dev/null) || {
        echo "ERROR: seed_dummy_store: could not extract upload token from response: $initiate_response" >&2
        return 1
    }
    revision=$(echo "$initiate_response" | python3 -c "import sys,json; d=json.load(sys.stdin); print(d['revision'])" 2>/dev/null) || {
        echo "ERROR: seed_dummy_store: could not extract revision from response: $initiate_response" >&2
        return 1
    }

    if [[ -z "$token" ]]; then
        echo "ERROR: seed_dummy_store: upload token is empty" >&2
        return 1
    fi

    # Step 2: upload (send as raw binary — the dummy store accepts both forms)
    local upload_response
    upload_response=$(curl -sf \
        -X POST \
        -H "Authorization: Bearer test-token" \
        -H "Content-Type: application/octet-stream" \
        --data-binary "@${pack_file}" \
        "${DUMMY_STORE_URL}/package/publish/upload?key=${token}" 2>&1)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: seed_dummy_store: upload failed: $upload_response" >&2
        return 1
    fi

    # Step 3: complete
    local complete_response
    complete_response=$(curl -sf \
        -X POST \
        -H "Authorization: Bearer test-token" \
        -H "Content-Length: 0" \
        "${DUMMY_STORE_URL}/package/publish/complete?key=${token}&channel=${channel}" 2>&1)
    if [[ $? -ne 0 ]]; then
        echo "ERROR: seed_dummy_store: complete failed: $complete_response" >&2
        return 1
    fi

    echo "$revision"
    return 0
}
