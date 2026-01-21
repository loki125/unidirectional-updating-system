#!/bin/sh
set -e

echo "Applying IPTables Firewall Rules..."

# Explicitly prevent traffic from routing back from eth0 to eth1.
iptables -A FORWARD -i eth0 -o eth1 -j DROP

echo "Firewall rules applied. Starting C++ Router..."

# use 'exec' so the C++ app takes over PID 1 (handles signals correctly)
exec ./mcast_router