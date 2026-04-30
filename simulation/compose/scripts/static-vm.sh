#!/bin/bash

# The first argument passed from Python is the path
VM_PATH="$1"

# Check if path is empty or file doesn't exist
if [[ -z "$VM_PATH" ]] || [[ ! -f "$VM_PATH" ]]; then
    echo "[ERROR] VM Image not found at: '$VM_PATH'"
    exit 1
fi

echo "[INFO] Starting VM with image: $VM_PATH"
echo "[INFO] Requesting sudo for KVM/Network access..."

# sudo: Required to access /dev/kvm and the tap interface
# Note: $1 is the path provided via the Python entry box
sudo qemu-system-x86_64 \
  -m 2G \
  -smp 2 \
  -cpu host \
  -enable-kvm \
  -drive file="$VM_PATH",if=virtio \
  -netdev tap,id=mynet0,ifname=tap-vm0,script=no,downscript=no \
  -device virtio-net-pci,netdev=mynet0,mac=52:54:00:12:34:56 \
  -netdev user,id=net_internt \
  -device virtio-net-pci,netdev=net_internt,mac=52:54:00:12:34:01

# Return the exit code of QEMU
exit $?