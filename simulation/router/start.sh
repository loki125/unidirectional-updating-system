#!/bin/bash
set -e

setup_iface() {
    local ip="$1"
    local iface_name="$2"

    # Detect interface from IP using an exact match (ignoring the /24 CIDR mask)
    local iface_val
    iface_val=$(ip -o -4 addr show | awk -v ip="$ip" '{split($4, a, "/"); if(a[1] == ip) print $2}')

    if [ -z "$iface_val" ]; then
        echo "ERROR: Could not detect interface: $iface_name ($ip)"
        exit 1
    fi

    echo "[INFO] Configuring $iface_name with IP $ip on interface $iface_val"

    # Export the global variable so the rest of the script can use it
    export "${iface_name}_IFACE=$iface_val"

    ethtool -K "$iface_val" tx off rx off \
        >/dev/null 2>&1 || true
}

enable_forwarding() {
    sysctl -q -w net.ipv4.ip_forward=1

    sysctl -q -w net.ipv4.conf.all.rp_filter=0

    sysctl -q -w net.ipv4.conf."$OUTSIDE_IFACE".rp_filter=0
    sysctl -q -w net.ipv4.conf."$MCAST_IFACE".rp_filter=0
    sysctl -q -w net.ipv4.conf."$VIEW_IFACE".rp_filter=0
}

reset_firewall() {
    iptables -F
    iptables -t mangle -F

    iptables -P FORWARD DROP
}

allow_multicast_flow() {
    echo "[INFO] Allowing multicast forwarding..."

    iptables -A FORWARD \
        -i "$OUTSIDE_IFACE" \
        -o "$MCAST_IFACE" \
        -d "$MULTICAST_IP" \
        -j ACCEPT
}

allow_view_telemetry() {
    echo "[INFO] Allowing telemetry forwarding to sys-view..."

    iptables -A FORWARD \
        -i "$MCAST_IFACE" \
        -o "$VIEW_IFACE" \
        -p udp \
        -s "$STORE_MANAGER_MCAST_IP" \
        -d "$VIEW_IP" \
        --dport "$VIEW_UDP_PORT" \
        -j ACCEPT
}

configure_ttl() {
    echo "[INFO] Configuring multicast TTL..."

    iptables -t mangle -A PREROUTING \
        -i "$OUTSIDE_IFACE" \
        -d "$MULTICAST_IP" \
        -j TTL --ttl-inc 1 || true
}

configure_smcroute() {
    echo "[INFO] Configuring SMCRoute..."

    cat <<EOF > /etc/smcroute.conf
phyint $OUTSIDE_IFACE enable
phyint $MCAST_IFACE enable

mgroup from $OUTSIDE_IFACE group $MULTICAST_IP
mroute from $OUTSIDE_IFACE group $MULTICAST_IP to $MCAST_IFACE
EOF
}

main() {
    # Ensure env variables are available
    if [ -z "$MULTICAST_IP" ] || [ -z "$STORE_MANAGER_MCAST_IP" ]; then
        echo "ERROR: Missing required environment variables (MULTICAST_IP or STORE_MANAGER_MCAST_IP)"
        exit 1
    fi

    setup_iface "$ROUTER_OUTSIDE_IP" "OUTSIDE"
    setup_iface "$ROUTER_MCAST_IP" "MCAST"
    setup_iface "$ROUTER_VIEW_IP" "VIEW"

    enable_forwarding
    reset_firewall
    allow_multicast_flow
    allow_view_telemetry
    configure_ttl
    configure_smcroute

    echo "[INFO] Starting SMCRoute daemon..."
    exec smcrouted -n
}

main "$@"