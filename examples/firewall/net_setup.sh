#!/bin/bash

# Create bridges
ip link add br0 type bridge
ip link add br1 type bridge
ip link set br0 up
ip link set br1 up
ip addr add 172.16.2.2/12 dev br0
ip addr add 192.168.1.2/24 dev br1

# Create taps
ip tuntap add dev tap0 mode tap user $(id -u)
ip tuntap add dev tap1 mode tap user $(id -u)
ip link set tap0 up
ip link set tap1 up
ip link set tap0 master br0
ip link set tap1 master br1

# Create namespaces, 0 = external side, 1 = internal side
ip netns add ext
ip netns add int

# Create veth between namespaces and bridges
ip link add br0-ext type veth peer name ext-br0
ip link add br1-int type veth peer name int-br1

# Attach veth to namespaces and bridges
ip link set ext-br0 netns ext
ip link set br0-ext master br0
ip link set int-br1 netns int
ip link set br1-int master br1

# Assign ip address on the namespace side of the veths
ip -n int addr add 192.168.1.100/24 dev int-br1
ip -n ext addr add 172.16.2.200/12 dev ext-br0

# Set the veth interfaces to up
ip -n int link set int-br1 up
ip -n ext link set ext-br0 up
ip link set br1-int up
ip link set br0-ext up

# Add default routes to namespaces via the firewall
ip -n int route add default via 192.168.1.1
ip -n ext route add default via 172.16.2.1

# Disable bridge/VLAN filtering
ip link set dev br0 type bridge stp_state 0 vlan_filtering 0
ip link set dev br1 type bridge stp_state 0 vlan_filtering 0
sysctl -w net.bridge.bridge-nf-call-iptables=0
