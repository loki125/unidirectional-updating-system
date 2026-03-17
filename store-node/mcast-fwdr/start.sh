#!/bin/sh
set -e

# Setup NICs
# Identify which eth interface is on which network. 
ethtool -K eth0 tx off rx off 2>/dev/null || true
ethtool -K eth1 tx off rx off 2>/dev/null || true

# Wait for the other side of the bridge (172.30.0.254)
TARGET_IP="172.30.0.254"
echo "Waiting for $TARGET_IP..."
# Add a timeout so the container doesn't hang forever if the network is truly dead
count=0
until ping -c 1 -W 1 $TARGET_IP > /dev/null 2>&1 || [ $count -eq 30 ]; do
    echo "Still waiting for $TARGET_IP..."
    sleep 1
    count=$((count+1))
done

# orce ARP (Use the specific IP for the relevant interface)
arping -c 3 -I eth0 172.30.0.254 > /dev/null 2>&1 || true

echo "Applying IPTables Firewall Rules..."
iptables -A FORWARD -i eth0 -o eth1 -j DROP || echo "Warning: IPTables failed"

echo "Starting C++ Router..."
exec ./mcast_router