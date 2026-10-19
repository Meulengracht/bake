#!/bin/bash
# Run as root in Linux/WSL: BUSYBOX is a static binary with udhcpc and udhcpd.
set -euo pipefail
if [ "${1:-}" != --isolated ]; then
    [ "$#" = 3 ] || { echo 'usage: test-network.sh BUSYBOX NETWORK_SCRIPT LEASE_SCRIPT' >&2; exit 2; }
    exec unshare --mount --net --pid --fork --mount-proc bash "$0" --isolated "$@"
fi
shift
busybox=$(realpath "$1")
network=$(realpath "$2")
lease=$(realpath "$3")
work=$(mktemp -d /tmp/chef-network-test.XXXXXX)
server=chef-dhcp-${work##*.}
server_pid=
client_pid=
cleanup() {
    [ -z "$client_pid" ] || kill "$client_pid" 2>/dev/null || true
    [ -z "$server_pid" ] || kill "$server_pid" 2>/dev/null || true
    ip netns del "$server" 2>/dev/null || true
    umount "$work/root/run" 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT
mount --make-rprivate /
ip netns add "$server"
ip link add eth0 type veth peer name server0
ip link set server0 netns "$server"
ip link set lo up
ip netns exec "$server" ip link set lo up
ip netns exec "$server" ip addr add 192.0.2.1/24 dev server0
ip netns exec "$server" ip link set server0 up
cat > "$work/server.conf" <<EOF
start 192.0.2.20
end 192.0.2.30
interface server0
lease_file $work/leases
pidfile $work/server.pid
option subnet 255.255.255.0
option router 192.0.2.1
option dns 192.0.2.53
option lease 12
min_lease 1
EOF
ip netns exec "$server" "$busybox" udhcpd -f "$work/server.conf" > "$work/server.log" 2>&1 &
server_pid=$!
root=$work/root
mkdir -p "$root"/{bin,dev,run,usr/share/udhcpc,chef/rootfs/chef}
# Match HCS init: /run is writable but scripts cannot be executed from it.
mount -t tmpfs -o nodev,nosuid,noexec tmpfs "$root/run"
mknod "$root/dev/null" c 1 3
cp "$busybox" "$root/bin/busybox"
ln -s busybox "$root/bin/sh"
tr -d '\r' < "$network" > "$root/network.sh"
tr -d '\r' < "$lease" > "$root/lease.sh"
cat > "$root/usr/share/udhcpc/default.script" <<'EOF'
#!/bin/sh
echo "$1" >> /events
exec /bin/sh /lease.sh "$@"
EOF
chmod 755 "$root/usr/share/udhcpc/default.script"
sleep 1
chroot "$root" /bin/sh /network.sh '' '' '203.0.113.53;203.0.113.54'
client_pid=$(cat "$root/run/chef-udhcpc.pid")
kill -0 "$client_pid"
# Exercise renewal automatically with a short lease and survive the original
# startup timeout. Force an additional renewal if the server clamps its lease.
for attempt in $(seq 1 35); do
    sleep 1
    kill -0 "$client_pid"
done
kill -USR1 "$client_pid"
for attempt in $(seq 1 10); do
    grep -q '^renew$' "$root/events" && break
    sleep 1
done
grep -q '^bound$' "$root/events"
grep -q '^renew$' "$root/events"
printf 'nameserver 203.0.113.53\nnameserver 203.0.113.54\n' > "$work/expected"
cmp "$work/expected" "$root/chef/rootfs/chef/resolv.conf"
# Dynamic DNS should still work when no explicit override was requested.
CHEF_LCOW_DNS='' interface=eth0 ip=192.0.2.20 mask=24 router=192.0.2.1 dns=192.0.2.53 \
    chroot "$root" /bin/sh /var/lib/chef/udhcpc.script renew
grep -qx 'nameserver 192.0.2.53' "$root/chef/rootfs/chef/resolv.conf"
kill "$client_pid"
client_pid=
kill "$server_pid"
wait "$server_pid" || true
server_pid=
# Failure to acquire a lease must still fail startup instead of daemonizing.
if chroot "$root" /bin/sh /network.sh '' '' ''; then
    echo 'DHCP startup incorrectly succeeded without a server' >&2
    exit 1
fi
echo 'PASS: DHCP renewal, startup timeout, DNS overrides and acquisition failure'
