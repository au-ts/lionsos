#!/bin/bash

# Execute from ext namespace
ip netns exec ext
ip netns exec ext tcpdump -eX -i ext-br0
ip netns exec ext ping 192.168.1.100

# Execute from int namespace
ip netns exec int
ip netns exec int tcpdump -eX -i int-br1
ip netns exec int ping 172.16.2.200

# TCP: ext --> int
ip netns exec int nc -l 65444
ip netns exec ext nc 192.168.1.100 65444

# TCP: int --> ext
ip netns exec ext nc -l 65444
ip netns exec int nc 172.16.2.200 65444

# UDP: ext --> int
ip netns exec int nc -ul 65444
ip netns exec ext nc -u 192.168.1.100 65444

# UDP: int --> ext
ip netns exec ext nc -ul 65444
ip netns exec int nc -u 172.16.2.200 65444

# TCP dumps (e - include ethernet, x - hexdump packet, -i interface)
tcpdump -ex -i br0-ext
tcpdump -ex -i br0
tcpdump -ex -i br1
tcpdump -ex -i br1-int

# Display routes of a network namespace
ip route
ip netns exec ext ip route
ip netns exec int ip route

# Add a dummy endpoint to bridge. Dummy is in root network namespace, so will
# not route traffic through the firewall, but it will reply to ARP requests
ip link add dummy0 type dummy
ip link set dummy0 master br0
ip addr add 172.16.2.3/12 dev dummy0
ip link set dummy0 up
