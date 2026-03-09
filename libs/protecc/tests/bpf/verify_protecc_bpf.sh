#!/bin/sh
set -eu

OBJ_LIST="${1:-}"

if ! command -v bpftool >/dev/null 2>&1; then
    echo "SKIP: bpftool not available"
    exit 77
fi

if [ ! -e /sys/fs/bpf ]; then
    echo "SKIP: /sys/fs/bpf is not available"
    exit 77
fi

if [ ! -e /sys/kernel/btf/vmlinux ]; then
    echo "SKIP: /sys/kernel/btf/vmlinux is not available"
    exit 77
fi

if [ "$(id -u)" -ne 0 ]; then
    echo "SKIP: root privileges are required to load eBPF programs"
    exit 77
fi

if [ -z "$OBJ_LIST" ]; then
    echo "SKIP: no BPF smoke objects were provided"
    exit 77
fi

PIN_DIR="/sys/fs/bpf/protecc-verifier-$$"
mkdir -p "$PIN_DIR"
cleanup() {
    rm -rf "$PIN_DIR"
}
trap cleanup EXIT INT TERM

OLD_IFS="$IFS"
IFS=';'
set -- $OBJ_LIST
IFS="$OLD_IFS"

if [ "$#" -eq 0 ]; then
    echo "SKIP: no BPF smoke objects were provided"
    exit 77
fi

for OBJ_PATH in "$@"; do
    if [ ! -f "$OBJ_PATH" ]; then
        echo "SKIP: BPF smoke object was not built: $OBJ_PATH"
        exit 77
    fi

    bpftool -d prog loadall "$OBJ_PATH" "$PIN_DIR"
done

echo "protecc eBPF verifier smoke test passed"
