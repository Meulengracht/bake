#!/bin/bash
set -euo pipefail

DIR="$1"
ARCH="$2"

OUT_DIR="$DIR/uvm"
MKUVM_BIN="${MKUVM_BIN:-/chef/build/bin/mkuvm}"
BASE_ARCHIVE="${LCOW_BASE_ARCHIVE:-}"
DELTA_ARCHIVE="${LCOW_DELTA_ARCHIVE:-}"
KERNEL_PATH="${LCOW_KERNEL_PATH:-}"
BASH_BIN="${LCOW_BASH_BIN:-bash}"

mkdir -p "$OUT_DIR"

if [[ "$MKUVM_BIN" == */* ]]; then
    if [[ ! -x "$MKUVM_BIN" ]]; then
        echo "mkuvm not found or not executable: $MKUVM_BIN" >&2
        exit 1
    fi
elif ! command -v "$MKUVM_BIN" >/dev/null 2>&1; then
    echo "mkuvm not found on PATH: $MKUVM_BIN" >&2
    exit 1
fi

if [ -z "$BASE_ARCHIVE" ] || [ -z "$KERNEL_PATH" ]; then
    echo "LCOW_BASE_ARCHIVE and LCOW_KERNEL_PATH are required for the explicit mkuvm construct flow" >&2
    exit 1
fi

if [ ! -f "$BASE_ARCHIVE" ]; then
    echo "LCOW base archive not found: $BASE_ARCHIVE" >&2
    exit 1
fi

if [ -n "$DELTA_ARCHIVE" ] && [ ! -f "$DELTA_ARCHIVE" ]; then
    echo "LCOW delta archive not found: $DELTA_ARCHIVE" >&2
    exit 1
fi

if [ ! -f "$KERNEL_PATH" ]; then
    echo "LCOW kernel path not found: $KERNEL_PATH" >&2
    exit 1
fi

args=(
    construct
    --output "$OUT_DIR"
    --base-archive "$BASE_ARCHIVE"
    --kernel "$KERNEL_PATH"
    --arch "$ARCH"
    --bash-bin "$BASH_BIN"
    --force
)

if [ -n "$DELTA_ARCHIVE" ]; then
    args+=(--delta-archive "$DELTA_ARCHIVE")
fi

echo "Running $MKUVM_BIN ${args[*]}"
"$MKUVM_BIN" "${args[@]}"

for expected in "$OUT_DIR/bundle.json" "$OUT_DIR/kernel" "$OUT_DIR/initrd"; do
    if [ ! -e "$expected" ]; then
        echo "Expected construct output is missing: $expected" >&2
        exit 1
    fi
done

echo "LCOW UVM bundle written to $OUT_DIR"