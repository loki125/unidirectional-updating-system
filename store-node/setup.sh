#!/bin/bash
set -e

BRIDGE="br-isolated"
BRIDGE_IP="192.168.50.1/24"

# Check if bridge exists

if ! ip link show "$BRIDGE" &>/dev/null; then
    echo "[+] Creating bridge $BRIDGE..."
    sudo ip link add "$BRIDGE" type bridge
    sudo ip addr add "$BRIDGE_IP" dev "$BRIDGE"
    sudo ip link set "$BRIDGE" up
else
    echo "[i] Bridge $BRIDGE already exists"
fi

# Check ports and kill processes

PORTS=(40085 11080)

for PORT in "${PORTS[@]}"; do
    echo "[i] Checking port $PORT..."

    PIDS_UDP=$(sudo lsof -ti UDP:"$PORT" 2>/dev/null || true)
    if [[ -n "$PIDS_UDP" ]]; then
        echo "$PIDS_UDP" | xargs sudo kill -9
    fi

    PIDS_TCP=$(sudo lsof -ti TCP:"$PORT" -sTCP:LISTEN 2>/dev/null || true)
    if [[ -n "$PIDS_TCP" ]]; then
        echo "$PIDS_TCP" | xargs sudo kill -9
    fi

done

docker compose down && docker compose up --build

echo "[+] Setup complete!"
