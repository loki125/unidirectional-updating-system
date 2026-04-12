#!/usr/bin/env bash
set -e

# Change to the directory where the script is located
cd "$(dirname "$(readlink -f "$0" 2>/dev/null || echo "$0")")" || exit 1

# Configuration
BRIDGE="br-isolated"
BRIDGE_IP="192.168.50.1/24"
TAP_IF="tap-vm0"

PORTS_TO_CLEAR=(40085 11080)

# Helper Functions for logs
log_info()    { echo -e "\e[34m[i] $1\e[0m"; }
log_success() { echo -e "\e[32m[+] $1\e[0m"; }
log_error()   { echo -e "\e[31m[!] $1\e[0m"; }

cleanup_environment() {
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

    log_info "Creating TAP interface $TAP_IF..."
    # Delete the interface if it already exists (suppress errors if it doesn't)
    sudo ip link delete "$TAP_IF" &>/dev/null || true
    
    # Create the new TAP interface
    sudo ip tuntap add dev "$TAP_IF" mode tap
    log_success "TAP $TAP_IF created"

    # Attach TAP to Bridge
    sudo ip link set "$TAP_IF" master "$BRIDGE"
    sudo ip link set "$TAP_IF" up
    log_success "TAP $TAP_IF is UP and attached to $BRIDGE"
}

start_static_dhcp_server() {
    local VM_MAC="52:54:00:12:34:56" 
    local VM_STATIC_IP="192.168.50.10"

    log_info "Restarting DHCP server in STATIC mode..."
    # Kill any existing dnsmasq process specifically tied to this bridge
    sudo pkill -f "dnsmasq --interface=$BRIDGE" || true
    sleep 0.5 # Small buffer to ensure the port is released

    sudo dnsmasq --interface="$BRIDGE" \
                 --bind-interfaces \
                 --port=0 \
                 --dhcp-range=192.168.50.0,static \
                 --dhcp-host="$VM_MAC","$VM_STATIC_IP",ignore-id \
                 --conf-file=/dev/null 
    log_success "Static DHCP server started for $VM_STATIC_IP for MAC $VM_MAC"
}

start_dynamic_dhcp_server() {
    local DHCP_START="192.168.50.10"
    local DHCP_END="192.168.50.100"
    local NETMASK="255.255.255.0"

    log_info "Restarting DHCP server in DYNAMIC mode..."
    # Kill any existing dnsmasq process specifically tied to this bridge
    sudo pkill -f "dnsmasq --interface=$BRIDGE" || true
    sleep 0.5

    sudo dnsmasq --interface="$BRIDGE" \
                 --bind-interfaces \
                 --port=0 \
                 --dhcp-range="$DHCP_START","$DHCP_END","$NETMASK",12h \
                 --conf-file=/dev/null 
    log_success "Dynamic DHCP server started (Range: $DHCP_START - $DHCP_END)"
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
    trap 'docker compose -f networks.yml down > /dev/null 2>&1' EXIT

    # Default settings
    local DHCP_MODE="static"
    local SHOULD_BUILD=false

    # Parse Arguments
    for arg in "$@"; do
        case $arg in
            -s|--static)
                DHCP_MODE="static"
                shift
                ;;
            -d|--dynamic)
                DHCP_MODE="dynamic"
                shift
                ;;
            -b|--build)
                SHOULD_BUILD=true
                shift
                ;;
        esac
    done

    # Run Setup
    cleanup_environment
    setup_network
    configure_firewall

    # Choose DHCP Server based on flag
    if [[ "$DHCP_MODE" == "dynamic" ]]; then
        start_dynamic_dhcp_server
    else
        start_static_dhcp_server
    fi

    # Handle Build Flag
    if [ "$SHOULD_BUILD" = true ]; then 
        log_info "Building Docker images..."
        docker compose -f networks.yml build
    fi

    log_success "Setup complete! Mode: $DHCP_MODE. Starting Docker Compose..."
    docker compose -f networks.yml up
}

# Run main with all script arguments
main "$@"
