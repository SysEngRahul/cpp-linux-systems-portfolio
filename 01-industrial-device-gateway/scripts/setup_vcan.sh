#!/bin/bash

set -e

echo "Loading virtual CAN kernel module..."
sudo modprobe vcan

echo "Creating vcan0 interface..."
sudo ip link add dev vcan0 type vcan 2>/dev/null || true

echo "Bringing vcan0 UP..."
sudo ip link set up vcan0

echo
echo "Virtual CAN interface ready:"
ip -details link show vcan0
