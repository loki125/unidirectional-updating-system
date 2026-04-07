#!/bin/bash
set -e

# Setup NICs
ethtool -K eth0 tx off rx off 2>/dev/null || true
ethtool -K eth1 tx off rx off 2>/dev/null || true

# Enable Kernel Forwarding
sysctl -w net.ipv4.ip_forward=1
sysctl -w net.ipv4.conf.all.mc_forwarding=1
sysctl -w net.ipv4.conf.eth0.mc_forwarding=1
sysctl -w net.ipv4.conf.eth1.mc_forwarding=1
sysctl -w net.ipv4.conf.all.rp_filter=0
sysctl -w net.ipv4.conf.eth0.rp_filter=0
sysctl -w net.ipv4.conf.eth1.rp_filter=0

# Firewall: Accept traffic entering eth1 and leaving eth0
iptables -F FORWARD
iptables -A FORWARD -i eth1 -o eth0 -j ACCEPT

# TTL Hack (Applied to input interface eth1)
iptables -t mangle -A PREROUTING -i eth1 -d 238.1.1.95 -j TTL --ttl-inc 1 || true

echo "Configuring SMCRoute..."
cat <<EOF > /etc/smcroute.conf
phyint eth0 enable
phyint eth1 enable

# Join and Route based on your logs showing data arriving on eth1
mgroup from eth1 group 238.1.1.95
mroute from eth1 group 238.1.1.95 to eth0
EOF

echo "Starting SMCRoute Daemon..."
exec smcrouted -n -l debug