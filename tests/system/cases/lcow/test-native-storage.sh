#!/bin/bash
# Run under: sudo unshare --mount --propagation private bash SCRIPT INITRD STORAGE_SCRIPT
# Exercises the shipped guest script with a real ext4 disk, without HCS.
set -euo pipefail
export PATH=/usr/sbin:/usr/bin:/sbin:/bin
initrd=$(realpath "$1")
script=$(realpath "$2")
work=$(mktemp -d /tmp/chef-lcow-storage-test.XXXXXX)
root=$work/uvm
device=
cleanup() {
    umount "$root/run/chef-storage" 2>/dev/null || true
    [ -z "$device" ] || losetup -d "$device"
    rm -rf "$work"
}
trap cleanup EXIT
mkdir "$root"
(cd "$root" && gzip -dc "$initrd" | cpio -idm --quiet)
truncate -s 256M "$work/disk.img"
device=$(losetup --find --show "$work/disk.img")
controller=df6d0690-79e5-55b6-a5ec-c1e2f77f580a
mkdir -p "$root/sys/bus/vmbus/devices/$controller/host0" \
    "$root/sys/bus/scsi/devices/0:0:0:0/block/${device##*/}" "$root/dev" "$root/tmp"
mknod "$root$device" b 7 "${device##*/loop}"
cp "$script" "$root/tmp/storage.sh"
seed=$root/chef/rootfs
native=$root/run/chef-storage/rootfs
mkdir -p "$seed/.containerv-lcow" "$seed/usr/bin"
echo excluded > "$seed/.containerv-lcow/secret"
printf '#!/bin/sh\nexit 0\n' > "$seed/usr/bin/tool"
chmod 751 "$seed/usr/bin/tool"
ln -s usr/bin/tool "$seed/relative"
ln -s missing "$seed/dangling"
echo original > "$seed/.hidden"

chroot "$root" /bin/sh /tmp/storage.sh 1 "$controller"
test "$(stat -f -c %t "$native")" = ef53
test "$(stat -c %a "$native/usr/bin/tool")" = 751
test "$(readlink "$native/relative")" = usr/bin/tool
test "$(readlink "$native/dangling")" = missing
test ! -e "$native/.containerv-lcow"
grep -q original "$native/.hidden"
echo persisted > "$native/state"
echo changed > "$seed/.hidden"
sync
umount "$root/run/chef-storage"
chroot "$root" /bin/sh /tmp/storage.sh 0 "$controller"
grep -q persisted "$native/state"
grep -q original "$native/.hidden"
umount "$root/run/chef-storage"
losetup -d "$device"
device=

# Existing invalid disks must fail without being formatted.
truncate -s 256M "$work/invalid.img"
device=$(losetup --find --show "$work/invalid.img")
# The loop allocator may choose a different free device on this pass.
find "$root/sys/bus/scsi/devices/0:0:0:0/block" -mindepth 1 -maxdepth 1 -type d -empty -delete
mkdir "$root/sys/bus/scsi/devices/0:0:0:0/block/${device##*/}"
[ -b "$root$device" ] || mknod "$root$device" b 7 "${device##*/loop}"
if chroot "$root" /bin/sh /tmp/storage.sh 0 "$controller"; then
    echo 'ERROR: an invalid existing disk was accepted' >&2
    exit 1
fi
if blkid -p "$device"; then
    echo 'ERROR: invalid disk was modified' >&2
    exit 1
fi
echo 'PASS: native ext4 seed, modes, links, persistence, and invalid-disk preservation'
