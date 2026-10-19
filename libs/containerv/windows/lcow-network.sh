#!/bin/sh
# Executed in the utility VM. A successful DHCP startup leaves one renewing
# client alive until VM teardown; the timeout only bounds initial acquisition.
set -eu
bb=/bin/busybox
address=$1
gateway=$2
dns=$3

if [ -n "$address" ]; then
    family=-4
    case "$address" in *:*) family=-6;; esac
    $bb ip link set dev eth0 up
    $bb ip "$family" addr flush dev eth0
    $bb ip "$family" addr add "$address" dev eth0
    if [ -n "$gateway" ]; then
        $bb ip "$family" route replace default via "$gateway" dev eth0
    fi
else
    # The daemon inherits this override on every bound/renew event. Wrap the
    # image's lease handler so older UVM bundles also retain explicit DNS.
    CHEF_LCOW_DNS=$(printf '%s' "$dns" | $bb tr ',;' '  ')
    export CHEF_LCOW_DNS
    # HCS mounts /run noexec; its writable /var overlay allows executable hooks.
    $bb mkdir -p /run /var/lib/chef
    $bb chmod 700 /var/lib/chef
    $bb cat > /var/lib/chef/udhcpc.script <<'LEASE'
#!/bin/sh
if [ -n "$CHEF_LCOW_DNS" ]; then
    dns=$CHEF_LCOW_DNS
    export dns
fi
exec /usr/share/udhcpc/default.script "$@"
LEASE
    $bb chmod 700 /var/lib/chef/udhcpc.script
    $bb timeout 30 $bb udhcpc -i eth0 -n -t 5 -T 2 -A 1 \
        -p /run/chef-udhcpc.pid -s /var/lib/chef/udhcpc.script
    # The parent exits before the daemon finishes rewriting its pidfile.
    # Wait for a live daemon rather than racing an empty or stale parent PID.
    client_ready=
    attempt=0
    while [ "$attempt" -lt 5 ]; do
        client_pid=$($bb cat /run/chef-udhcpc.pid 2>/dev/null || true)
        case "$client_pid" in
            ''|0|*[!0-9]*) ;;
            *) if kill -0 "$client_pid" 2>/dev/null; then client_ready=1; break; fi ;;
        esac
        attempt=$((attempt + 1))
        $bb sleep 1
    done
    [ "$client_ready" = 1 ] || { echo 'containerv: DHCP daemon did not start' >&2; exit 1; }
fi

if [ -n "$dns" ]; then
    servers=$(printf '%s' "$dns" | $bb tr ',;' '  ')
    set -f
    : > /chef/rootfs/chef/resolv.conf
    for server in $servers; do
        printf 'nameserver %s\n' "$server" >> /chef/rootfs/chef/resolv.conf
    done
fi
