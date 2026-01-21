#!/bin/bash
set -e

# 1. Define the Router IP (The bridge between Network A and B)
ROUTER_IP="10.10.1.254"
TARGET_SUBNET="10.10.2.0/24"

echo "--- Network Setup ---"
# 2. Add the custom static route
# This tells the container: "To reach 10.10.2.x, go through 10.10.1.254"
ip route add $TARGET_SUBNET via $ROUTER_IP || echo "Route likely already exists"

echo "Route added: $TARGET_SUBNET via $ROUTER_IP"
echo "---------------------"

# 3. Execute the original binary
# This replaces the shell process with your broadcaster process
exec /usr/local/bin/broadcaster "$@"