#!/bin/bash
set -e

docker compose down > /dev/null 2>&1

BRIDGE="br-isolated"
BRIDGE_IP="192.168.50.1/24"
TAP_IF="tap-vm0"

VM_MAC="52:54:00:12:34:56" 
VM_STATIC_IP="192.168.50.2"

# Create the Bridge
if ! ip link show "$BRIDGE" &>/dev/null; then
    echo "[+] Creating bridge $BRIDGE..."
    sudo ip link add "$BRIDGE" type bridge
    sudo ip addr add "$BRIDGE_IP" dev "$BRIDGE"
    sudo ip link set "$BRIDGE" up
else
    echo "[i] Bridge $BRIDGE already exists"
fi

# Create the TAP interface and attach to Bridge
if ! ip link show "$TAP_IF" &>/dev/null; then
    echo "[+] Creating TAP interface $TAP_IF..."
    sudo ip tuntap add dev "$TAP_IF" mode tap
    sudo ip link set "$TAP_IF" master "$BRIDGE"
    sudo ip link set "$TAP_IF" up
else
    echo "[i] TAP $TAP_IF already exists"
fi

#start dhcp server
if ! pgrep -f "dnsmasq --interface=$BRIDGE" > /dev/null; then
    echo "[+] Starting DHCP server for $VM_STATIC_IP..."
    sudo dnsmasq --interface="$BRIDGE" \
                 --bind-interfaces \
                 --port=0 \
                 --dhcp-range=192.168.50.0,static \
                 --dhcp-host="$VM_MAC","$VM_STATIC_IP" \
                 --conf-file=/dev/null # Ignore global config
else
    echo "[i] DHCP server already running on $BRIDGE"
fi

# Configure Isolation

# Allow traffic to stay INSIDE the bridge (VM <-> Docker)
if ! sudo iptables -C FORWARD -i "$BRIDGE" -o "$BRIDGE" -j ACCEPT 2>/dev/null; then
    echo "[+] Adding intra-bridge allow rule"
    sudo iptables -I FORWARD -i "$BRIDGE" -o "$BRIDGE" -j ACCEPT
fi

# Block traffic trying to LEAVE the bridge (VM -> Internet/LAN)
if ! sudo iptables -C FORWARD -i "$BRIDGE" ! -o "$BRIDGE" -j DROP 2>/dev/null; then
    echo "[+] Adding isolation drop rule"
    sudo iptables -I FORWARD -i "$BRIDGE" ! -o "$BRIDGE" -j DROP
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

if [[ $1 == -b ]]; then 
    docker compose build
fi

echo "[+] Setup complete!" && docker compose up


