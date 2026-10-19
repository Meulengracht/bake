#!/bin/sh
# Executed in the utility VM before any workload or host-directory mounts.
# Only a Chef-owned disk with an uncommitted first initialization may be
# formatted. Published disks must mount successfully and are never erased.
set -eu
export PATH=/usr/sbin:/usr/bin:/sbin:/bin

fresh=$1
controller=$2
storage=/run/chef-storage
seed=/chef/rootfs

fail() { echo "containerv: LCOW storage: $*" >&2; exit 1; }
command -v mkfs.ext4 >/dev/null || fail 'utility VM needs e2fsprogs (mkfs.ext4)'

# Resolve controller 0, LUN 0 through sysfs, never guess /dev/sda.
device=
attempt=0
while [ "$attempt" -lt 30 ]; do
    for host in /sys/bus/vmbus/devices/"$controller"/host*; do
        [ -d "$host" ] || continue
        number=${host##*/host}
        for block in /sys/bus/scsi/devices/"$number":0:0:0/block/*; do
            [ -d "$block" ] || continue
            device=/dev/${block##*/}
        done
    done
    [ -n "$device" ] && [ -b "$device" ] && break
    attempt=$((attempt + 1))
    sleep 1
done
[ -n "$device" ] && [ -b "$device" ] || fail 'workspace SCSI disk did not appear'

if [ "$fresh" = 1 ]; then
    mkfs.ext4 -q -F -L chef-lcow "$device"
fi
mkdir -p "$storage"
mount -t ext4 -o noatime "$device" "$storage" || fail 'cannot mount workspace; existing disk preserved'

# Publish only a complete seed. A failed first import is retried on the next
# start; no workloads can run before the marker has been written and synced.
if [ ! -f "$storage/rootfs.ready" ]; then
    mkdir -p "$storage/rootfs"
    for entry in "$seed"/* "$seed"/.[!.]* "$seed"/..?*; do
        [ -e "$entry" ] || [ -L "$entry" ] || continue
        [ "${entry##*/}" != .containerv-lcow ] || continue
        cp -a "$entry" "$storage/rootfs/"
    done
    touch "$storage/rootfs.ready"
    sync
fi

# Prove the semantics bakectl needs on the actual destination filesystem.
probe=$(mktemp -d "$storage/.probe.XXXXXX")
ln -s missing "$probe/link"
[ "$(readlink "$probe/link")" = missing ] || fail 'symlink capability check failed'
rm "$probe/link"
rmdir "$probe"
