#!/bin/sh
# Run as root in an ARM64 Ubuntu 24.04 WSL distribution.
set -eu
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
[ "$#" = 5 ] || { echo 'usage: repack-initrd.sh BASE MODULES DELTA DHCP_SCRIPT OUTPUT' >&2; exit 2; }
base=$1
modules=$2
delta=$3
dhcp=$4
output=$5
root=$(mktemp -d /tmp/chef-lcow-initrd.XXXXXX)
packages=$(mktemp -d /tmp/chef-lcow-packages.XXXXXX)
trap 'rm -rf "$root" "$packages"' EXIT

tar -xzf "$base" -C "$root"
tar -xf "$modules" -C "$root" --keep-directory-symlink
tar -xzf "$delta" -C "$root" --keep-directory-symlink --no-same-owner
# Download and unpack into the image, without installing anything in WSL.
# e2fsprogs supplies libe2p as well as mkfs.ext4; the other libraries are its
# Ubuntu 24.04 runtime dependencies not guaranteed in ubuntu-base.
(cd "$packages" && apt-get download -qq busybox-static e2fsprogs libext2fs2t64 libcom-err2 libss2 libuuid1 libblkid1)
for package in "$packages"/*.deb; do
    dpkg-deb --fsys-tarfile "$package" | tar -xf - -C "$root" --keep-directory-symlink
done
mkdir -p "$root/usr/share/udhcpc"
tr -d '\r' < "$dhcp" > "$root/usr/share/udhcpc/default.script"
chmod 0755 "$root/usr/share/udhcpc/default.script"
for link in bin lib sbin; do
    test -L "$root/$link" || { echo "LCOW rootfs /$link is no longer a usr-merge symlink" >&2; exit 1; }
done
for exe in init bin/gcs bin/gcstools bin/vsockexec bin/runc bin/busybox bin/sh sbin/mkfs.ext4 lib/ld-linux-aarch64.so.1; do
    test -x "$root/$exe" || { echo "LCOW rootfs missing executable /$exe" >&2; exit 1; }
done
for exe in init usr/bin/gcs usr/bin/gcstools usr/bin/runc usr/bin/vsockexec usr/bin/wait-paths usr/bin/busybox; do
    chown 0:0 "$root/$exe"
    chmod 0755 "$root/$exe"
done
# Also catch missing shared libraries before publishing an unusable image.
chroot "$root" /sbin/mkfs.ext4 -V
(cd "$root" && find . -mindepth 1 -print0 | cpio --null -o --format=newc --quiet | gzip -c > "$output/initrd")
