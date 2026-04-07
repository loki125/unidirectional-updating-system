#!/bin/bash
set -e

# Setup NICs
ethtool -K eth0 tx off rx off 2>/dev/null || true
ethtool -K eth1 tx off rx off 2>/dev/null || true

# Wait for the other side of the bridge (172.30.0.254)
TARGET_IP="172.30.0.254"
echo "Waiting for $TARGET_IP..."
count=0
until ping -c 1 -W 1 $TARGET_IP > /dev/null 2>&1 || [ $count -eq 30 ]; do
    sleep 1
    count=$((count+1))
done

# Force ARP
arping -c 3 -I eth0 172.30.0.254 > /dev/null 2>&1 || true

echo "Applying IPTables Firewall Rules..."
iptables -A FORWARD -i eth0 -o eth1 -j DROP || echo "Warning: IPTables failed"

# Hack to prevent TTL=1 drops (Increases the TTL by 1 before it routes)
iptables -t mangle -A PREROUTING -d 238.1.1.95 -j TTL --ttl-inc 1 || true

echo "Configuring SMCRoute..."
# Create the routing config dynamically
cat <<EOF > /etc/smcroute.conf
# Ensure both interfaces are enabled for multicast
phyint eth0 enable
phyint eth1 enable

# Tell the kernel to join the group on eth1
mgroup from eth1 group 238.1.1.95

# Route the traffic from eth1 to eth0
mroute from eth1 group 238.1.1.95 to eth0
EOF

echo "Starting SMCRoute Daemon..."
# Run smcrouted in the foreground (-n) with debug logs (-l debug)
exec smcrouted -n -l debug