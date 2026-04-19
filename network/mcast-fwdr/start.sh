#!/bin/bash
set -e

# Auto-Detect Interfaces 
OUTSIDE_IP="172.30.0.254"
ISOLATED_IP="172.30.1.2"

# Extract interface names using the 'ip' command
IN_IFACE=$(ip -4 addr show | grep "$OUTSIDE_IP" | awk '{print $NF}')
OUT_IFACE=$(ip -4 addr show | grep "$ISOLATED_IP" | awk '{print $NF}')

# Failsafe if interfaces aren't ready/detected
if [ -z "$IN_IFACE" ] || [ -z "$OUT_IFACE" ]; then
    echo "ERROR: Could not detect interfaces! IN_IFACE=$IN_IFACE, OUT_IFACE=$OUT_IFACE"
    exit 1
fi

echo "Detected Outside (Inbound) Interface: $IN_IFACE ($OUTSIDE_IP)"
echo "Detected Isolated (Outbound) Interface: $OUT_IFACE ($ISOLATED_IP)"

#  Setup NICs 
ethtool -K "$IN_IFACE" tx off rx off 2>/dev/null || true
ethtool -K "$OUT_IFACE" tx off rx off 2>/dev/null || true

# Enable Kernel Forwarding
sysctl -w net.ipv4.ip_forward=1
# (Note: mc_forwarding sysctl commands removed because they are read-only and cause errors)
sysctl -w net.ipv4.conf.all.rp_filter=0
sysctl -w net.ipv4.conf."$IN_IFACE".rp_filter=0
sysctl -w net.ipv4.conf."$OUT_IFACE".rp_filter=0

#  Firewall 
iptables -F FORWARD
# Accept traffic entering the outside network and leaving the isolated network
iptables -A FORWARD -i "$IN_IFACE" -o "$OUT_IFACE" -j ACCEPT
# Block communication trying to go the other way (isolated -> outside)
iptables -A FORWARD -i "$OUT_IFACE" -o "$IN_IFACE" -j DROP

#  TTL Hack (Applied to the inbound interface)
iptables -t mangle -A PREROUTING -i "$IN_IFACE" -d 238.1.1.95 -j TTL --ttl-inc 1 || true

#  Configure SMCRoute
echo "Configuring SMCRoute..."
cat <<EOF > /etc/smcroute.conf
phyint $IN_IFACE enable
phyint $OUT_IFACE enable

# Join and Route multicast strictly from outside -> isolated
mgroup from $IN_IFACE group 238.1.1.95
mroute from $IN_IFACE group 238.1.1.95 to $OUT_IFACE
EOF

# Start Daemon
echo "Starting SMCRoute Daemon..."
exec smcrouted -n -l debug