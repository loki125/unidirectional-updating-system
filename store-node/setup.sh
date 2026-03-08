#!/bin/bash
set -e

# Configuration
BRIDGE="br-isolated"
BRIDGE_IP="192.168.50.1/24"
TAP_IF="tap-vm0"

VM_MAC="52:54:00:12:34:56" 
VM_STATIC_IP="192.168.50.2"
PORTS_TO_CLEAR=(40085 11080)

# Helper Functions for logs
log_info()    { echo -e "\e[34m[i] $1\e[0m"; }
log_success() { echo -e "\e[32m[+] $1\e[0m"; }
log_error()   { echo -e "\e[31m[!] $1\e[0m"; }

cleanup_environment() {
    log_info "Stopping Docker containers..."
    docker compose down > /dev/null 2>&1 || true

    for PORT in "${PORTS_TO_CLEAR[@]}"; do
        log_info "Checking port $PORT..."
        
        local pids_udp pids_tcp
        pids_udp=$(sudo lsof -ti UDP:"$PORT" 2>/dev/null || true)
        if [[ -n "$pids_udp" ]]; then
            echo "$pids_udp" | xargs sudo kill -9
            log_success "Killed processes on UDP port $PORT"
        fi

        pids_tcp=$(sudo lsof -ti TCP:"$PORT" -sTCP:LISTEN 2>/dev/null || true)
        if [[ -n "$pids_tcp" ]]; then
            echo "$pids_tcp" | xargs sudo kill -9
            log_success "Killed processes on TCP port $PORT"
        fi
    done
}

setup_network() {
    # Create Bridge (with STP disabled so DHCP doesn't time out)
    if ! ip link show "$BRIDGE" &>/dev/null; then
        log_info "Creating bridge $BRIDGE..."
        sudo ip link add "$BRIDGE" type bridge stp_state 0 forward_delay 0
        sudo ip addr add "$BRIDGE_IP" dev "$BRIDGE"
        sudo ip link set "$BRIDGE" up
        log_success "Bridge $BRIDGE created"
    else
        log_info "Bridge $BRIDGE already exists"
    fi

    # Create TAP Interface
    if ! ip link show "$TAP_IF" &>/dev/null; then
        log_info "Creating TAP interface $TAP_IF..."
        sudo ip tuntap add dev "$TAP_IF" mode tap
        log_success "TAP $TAP_IF created"
    fi

    # Attach TAP to Bridge
    sudo ip link set "$TAP_IF" master "$BRIDGE"
    sudo ip link set "$TAP_IF" up
    log_success "TAP $TAP_IF is UP and attached to $BRIDGE"
}

start_dhcp_server() {
    if ! pgrep -f "dnsmasq --interface=$BRIDGE" > /dev/null; then
        log_info "Starting DHCP server for $VM_STATIC_IP..."
        sudo dnsmasq --interface="$BRIDGE" \
                     --bind-interfaces \
                     --port=0 \
                     --dhcp-range=192.168.50.0,static \
                     --dhcp-host="$VM_MAC","$VM_STATIC_IP",ignore-id \
                     --conf-file=/dev/null # Ignore global config
        log_success "DHCP server started"
    else
        log_info "DHCP server already running on $BRIDGE"
    fi
}

configure_firewall() {
    log_info "Configuring firewall & isolation..."

    # Fix UDP Checksum for VirtIO (Crucial for VM DHCP)
    if ! sudo iptables -t mangle -C POSTROUTING -p udp --dport 68 -j CHECKSUM --checksum-fill 2>/dev/null; then
        sudo iptables -t mangle -A POSTROUTING -p udp --dport 68 -j CHECKSUM --checksum-fill
        log_success "Added DHCP checksum-fill rule"
    fi

    # Allow the VM to talk to the DHCP server (Host) on the bridge
    if ! sudo iptables -C INPUT -i "$BRIDGE" -p udp --dport 67 -j ACCEPT 2>/dev/null; then
        sudo iptables -I INPUT -i "$BRIDGE" -p udp --dport 67 -j ACCEPT
        log_success "Allowed DHCP requests on $BRIDGE"
    fi

    # Block traffic trying to LEAVE the bridge (VM -> Internet/LAN)
    if ! sudo iptables -C FORWARD -i "$BRIDGE" ! -o "$BRIDGE" -j DROP 2>/dev/null; then
        sudo iptables -I FORWARD -i "$BRIDGE" ! -o "$BRIDGE" -j DROP
        log_success "Added isolation drop rule (External traffic blocked)"
    fi

    # Allow traffic to stay INSIDE the bridge (VM <-> Docker)
    # (Using -I puts this above the DROP rule so it is evaluated first)
    if ! sudo iptables -C FORWARD -i "$BRIDGE" -o "$BRIDGE" -j ACCEPT 2>/dev/null; then
        sudo iptables -I FORWARD -i "$BRIDGE" -o "$BRIDGE" -j ACCEPT
        log_success "Added intra-bridge allow rule"
    fi
}

# Main Execution Block
main() {
    cleanup_environment
    setup_network
    configure_firewall
    start_dhcp_server

    if [[ "$1" == "-b" || "$1" == "--build" ]]; then 
        log_info "Building Docker images..."
        docker compose build
    fi

    log_success "Setup complete! Starting Docker Compose..."
    docker compose up
}

# Run main with all script arguments
main "$@"