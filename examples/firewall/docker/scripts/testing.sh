# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

# -- Commands for testing the firewall -- #

TEST_PORT=65444

# Execute from ext namespace
ip netns exec ext

# Execute from int namespace
ip netns exec int

# ICMP: ext --> int
ip netns exec ext ping ${INT_HOST_IP}

# ICMP: int --> ext
ip netns exec int ping ${EXT_HOST_IP}

# TCP: ext listens, int initiates
ip netns exec int nc -l ${TEST_PORT}
ip netns exec ext nc ${INT_HOST_IP} ${TEST_PORT}

# TCP: int listens --> ext initiates
ip netns exec ext nc -l ${TEST_PORT}
ip netns exec int nc ${EXT_HOST_IP} ${TEST_PORT}

# UDP: ext listens, int initiates
ip netns exec int nc -ul ${TEST_PORT}
ip netns exec ext nc -u ${INT_HOST_IP} ${TEST_PORT}

# UDP: int listens --> ext initiates
ip netns exec ext nc -ul ${TEST_PORT}
ip netns exec int nc -u ${EXT_HOST_IP} ${TEST_PORT}

# ICMP desination unreachable
ip netns exec ext ping ${INT_BAD_HOST_IP}
ip netns exec int ping ${EXT_BAD_HOST_IP}

# TCP dump interfaces (e - include ethernet, x - hexdump packet, -i interface)

# veth attached to the external namespace
ip netns exec ext tcpdump -ex -i ext-br0
# veth attached to the external bridge
tcpdump -ex -i br0-ext
# external bridge
tcpdump -ex -i br0
# external tap of the firewall
tcpdump -ex -i tap0
# internal tap of the firewall
tcpdump -ex -i tap1
# internal bridge
tcpdump -ex -i br1
# veth attached to the internal bridge
tcpdump -ex -i br1-int
# veth attached to the internal namespace
ip netns exec int tcpdump -eX -i int-br1

# Display routes of a network namespace
ip route
ip netns exec ext ip route
ip netns exec int ip route

# Show all network interface details
ifconfig
ip netns exec ext ifconfig
ip netns exec int ifconfig

# Add a dummy endpoint to bridge. Dummy is in root network namespace, so will
# not route traffic through the firewall, but it will reply to ARP requests
EXT_DUMMY_IP=172.16.2.3
ip link add dummy0 type dummy
ip link set dummy0 master br0
ip addr add ${EXT_DUMMY_IP}/${FW_EXT_SUBNET} dev dummy0
ip link set dummy0 up
