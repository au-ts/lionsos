# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# -- Container script for setting up the network -- #

# Create bridges to connect namespaces to taps
ip link add br0 type bridge
ip link set br0 up
ip addr add ${INTERFACE0_ROOT_IP}/${FW_INTERFACE0_SUBNET} dev br0

ip link add br1 type bridge
ip link set br1 up
ip addr add ${INTERFACE1_ROOT_IP}/${FW_INTERFACE1_SUBNET} dev br1

# Create taps for the firewall
ip tuntap add dev tap0 mode tap user $(id -u)
ip link set tap0 up
ip link set tap0 master br0

ip tuntap add dev tap1 mode tap user $(id -u)
ip link set tap1 up
ip link set tap1 master br1

# Create namespaces, 0 = interface0 side, 1 = interface1 side
ip netns add namespace0
ip netns add namespace1

# Create veths to connect namespaces to bridges
ip link add br0-namespace0 type veth peer name namespace0-br0
ip link add br1-namespace1 type veth peer name namespace1-br1

# Attach veth to namespaces and bridges
ip link set namespace0-br0 netns namespace0
ip link set br0-namespace0 master br0
ip link set namespace1-br1 netns namespace1
ip link set br1-namespace1 master br1

# Assign ip address on the namespace side of the veths
ip -n namespace0 addr add ${INTERFACE0_HOST_IP}/${FW_INTERFACE0_SUBNET} dev namespace0-br0
ip -n namespace1 addr add ${INTERFACE1_HOST_IP}/${FW_INTERFACE1_SUBNET} dev namespace1-br1

# Set the veth interfaces to up
ip -n namespace0 link set namespace0-br0 up
ip link set br0-namespace0 up
ip -n namespace1 link set namespace1-br1 up
ip link set br1-namespace1 up

# Add default routes to namespaces via the firewall
ip -n namespace0 route add default via ${FW_INTERFACE0_IP}
ip -n namespace1 route add default via ${FW_INTERFACE1_IP}

# Disable bridge/VLAN filtering
ip link set dev br0 type bridge stp_state 0 vlan_filtering 0
ip link set dev br1 type bridge stp_state 0 vlan_filtering 0
sysctl -w net.bridge.bridge-nf-call-iptables=0
