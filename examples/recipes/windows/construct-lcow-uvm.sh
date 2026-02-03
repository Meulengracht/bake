#!/bin/bash
set -euo pipefail

DIR="$1"
ARCH="$2"

OUT_DIR="$DIR/uvm"
HCSSHIM_DIR="${HCSSHIM_DIR:-$DIR/hcsshim}"
LINUXKIT_BIN="${LINUXKIT_BIN:-linuxkit}"

mkdir -p "$OUT_DIR"

if ! command -v "$LINUXKIT_BIN" >/dev/null 2>&1; then
    echo "linuxkit not found; install it or set LINUXKIT_BIN" >&2
    exit 1
fi

if [ ! -d "$HCSSHIM_DIR" ]; then
    echo "hcsshim repo not found; cloning..." >&2
    git clone --depth 1 https://github.com/microsoft/hcsshim.git "$HCSSHIM_DIR"
fi

BUILD_SCRIPT="${HCSSHIM_DIR}/scripts/build-lcow-uvm.sh"
if [ ! -f "$BUILD_SCRIPT" ]; then
    echo "Expected hcsshim build script not found: $BUILD_SCRIPT" >&2
    echo "Please ensure you have a recent hcsshim checkout or update HCSSHIM_DIR." >&2
    exit 1
fi

TMP_OUT="$(mktemp -d)"
trap 'rm -rf "$TMP_OUT"' EXIT

# Build the LCOW UVM using hcsshim's LinuxKit pipeline.
# The script is expected to emit a VHDX plus optional kernel/initrd artifacts.
# If your hcsshim version uses different flags, update below accordingly.
"$BUILD_SCRIPT" \
    --linuxkit "$LINUXKIT_BIN" \
    --output "$TMP_OUT" \
    --arch "$ARCH"

# Normalize outputs into a bundle directory.
# Required: a VHDX UVM disk.
UVM_VHDX="$(find "$TMP_OUT" -maxdepth 2 -type f -name '*.vhdx' | head -n 1 || true)"
if [ -z "$UVM_VHDX" ]; then
    echo "No VHDX produced by LCOW UVM build" >&2
    exit 1
fi
install -m 0644 "$UVM_VHDX" "$OUT_DIR/uvm.vhdx"

# Optional: kernel / initrd / boot parameters.
KERNEL="$(find "$TMP_OUT" -maxdepth 2 -type f -name 'kernel*' | head -n 1 || true)"
INITRD="$(find "$TMP_OUT" -maxdepth 2 -type f -name 'initrd*' | head -n 1 || true)"
BOOT="$(find "$TMP_OUT" -maxdepth 2 -type f -name 'boot*' | head -n 1 || true)"

if [ -n "$KERNEL" ]; then
    install -m 0644 "$KERNEL" "$OUT_DIR/kernel"
fi
if [ -n "$INITRD" ]; then
    install -m 0644 "$INITRD" "$OUT_DIR/initrd"
fi
if [ -n "$BOOT" ]; then
    install -m 0644 "$BOOT" "$OUT_DIR/boot_parameters"
fi

echo "LCOW UVM bundle written to $OUT_DIR"