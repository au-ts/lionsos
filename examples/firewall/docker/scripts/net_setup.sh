# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

# -- Container script for setting up the network -- #

# Create bridges to connect namespaces to taps
ip link add br0 type bridge
ip link set br0 up
ip addr add ${EXT_ROOT_IP}/${FW_EXT_SUBNET} dev br0

ip link add br1 type bridge
ip link set br1 up
ip addr add ${INT_ROOT_IP}/${FW_INT_SUBNET} dev br1

# Create taps for the firewall
ip tuntap add dev tap0 mode tap user $(id -u)
ip link set tap0 up
ip link set tap0 master br0

ip tuntap add dev tap1 mode tap user $(id -u)
ip link set tap1 up
ip link set tap1 master br1

# Create namespaces, 0 = external side, 1 = internal side
ip netns add ext
ip netns add int

# Create veths to connect namespaces to bridges
ip link add br0-ext type veth peer name ext-br0
ip link add br1-int type veth peer name int-br1

# Attach veth to namespaces and bridges
ip link set ext-br0 netns ext
ip link set br0-ext master br0
ip link set int-br1 netns int
ip link set br1-int master br1

# Assign ip address on the namespace side of the veths
ip -n ext addr add ${EXT_HOST_IP}/${FW_EXT_SUBNET} dev ext-br0
ip -n int addr add ${INT_HOST_IP}/${FW_INT_SUBNET} dev int-br1

# Set the veth interfaces to up
ip -n ext link set ext-br0 up
ip link set br0-ext up
ip -n int link set int-br1 up
ip link set br1-int up

# Add default routes to namespaces via the firewall
ip -n ext route add default via ${FW_EXT_IP}
ip -n int route add default via ${FW_INT_IP}

# Disable bridge/VLAN filtering
ip link set dev br0 type bridge stp_state 0 vlan_filtering 0
ip link set dev br1 type bridge stp_state 0 vlan_filtering 0
sysctl -w net.bridge.bridge-nf-call-iptables=0
