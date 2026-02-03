#!/bin/bash

export GOROOT=/usr/local/go
export PATH=$PATH:$GOROOT/bin
# GOPATH is automatically set to $HOME/go, /chef/go for chef

GO_VERSION=go1.25.1
UBUNTU_VERSION=24.04

DIR="$1"
ARCH="$2"

ROOTFS_DIR="$DIR/rootfs"
RAW_IMG="$DIR/ext4.raw"
VHDX_IMG="$DIR/ext4.vhdx"

# By default, keep the rootfs directory for LCOW rootfs mapping.
# Set BUILD_VHDX=1 to also generate an ext4.vhdx (legacy/compat use).
# Set KEEP_ROOTFS=0 to remove the rootfs directory after packaging.
BUILD_VHDX=${BUILD_VHDX:-0}
KEEP_ROOTFS=${KEEP_ROOTFS:-1}

# add libraries we may need here
PACKAGES=(
    bash_bins
    passwd_config
    libc6_config
    libc6_gconv
    libgcc-s1_libs
    libc-bin_locale
    libc-bin_nsswitch
    libpam-runtime_config
    netbase_default-hosts
    netbase_default-networks
)

# ensure go is available
wget "https://go.dev/dl/${GO_VERSION}.linux-amd64.tar.gz"
tar -C /usr/local -xzf "${GO_VERSION}.linux-amd64.tar.gz"

# ensure chisel is available
if ! command -v chisel >/dev/null 2>&1; then
    go install github.com/canonical/chisel/cmd/chisel@latest
fi

# build a whitespace separated list
delim=""
JOINED=""
for item in "${PACKAGES[@]}"; do
    JOINED="$JOINED$delim$item"
    delim=" "
done

/chef/go/bin/chisel cut --release "ubuntu-${UBUNTU_VERSION}" --root "$ROOTFS_DIR" --arch "$ARCH" $JOINED

# Install pid1d into the rootfs if the binary is available.
# This makes pid1d always present for Windows-hosted Linux guests.
if [ -f /chef/build/bin/pid1d ]; then
    install -D -m 0755 /chef/build/bin/pid1d "$ROOTFS_DIR/usr/bin/pid1d"
fi

if [ "$BUILD_VHDX" = "1" ]; then
    if ! command -v mkfs.ext4 >/dev/null 2>&1; then
        echo "mkfs.ext4 not found; install e2fsprogs in the build environment" >&2
        exit 1
    fi

    if ! command -v qemu-img >/dev/null 2>&1; then
        echo "qemu-img not found; install qemu-utils in the build environment" >&2
        exit 1
    fi
fi

if [ "$BUILD_VHDX" = "1" ]; then
    bytes_used=""
    if du -sb "$ROOTFS_DIR" >/dev/null 2>&1; then
        bytes_used=$(du -sb "$ROOTFS_DIR" | awk '{print $1}')
    else
        bytes_used=$(du -s -B1 "$ROOTFS_DIR" | awk '{print $1}')
    fi

    gb=$((1024*1024*1024))
    overhead=$((512*1024*1024))
    min_bytes=$((2*gb))
    size_bytes=$((bytes_used + overhead))
    if [ "$size_bytes" -lt "$min_bytes" ]; then
        size_bytes=$min_bytes
    fi

    size_gb=$(((size_bytes + gb - 1) / gb))

    rm -f "$RAW_IMG" "$VHDX_IMG"
    truncate -s "${size_gb}G" "$RAW_IMG"
    mkfs.ext4 -F -L rootfs -d "$ROOTFS_DIR" "$RAW_IMG" >/dev/null
    qemu-img convert -f raw -O vhdx "$RAW_IMG" "$VHDX_IMG"

    rm -f "$RAW_IMG"
fi

if [ "$KEEP_ROOTFS" != "1" ]; then
    rm -rf "$ROOTFS_DIR"
fi
