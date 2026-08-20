#!/usr/bin/env bash

# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# -- Container script for setting up the network -- #
source /mnt/lionsOS/examples/firewall/docker/scripts/firewall_configuration.sh

for ((idx = 0; idx < "${FW_INTERFACE_COUNT}"; idx++)); do

    # Create bridges to connect namespaces to taps
    ip link add "br${idx}" type bridge
    ip link set "br${idx}" up
    ip addr add "${ROOT_IP[idx]}/${FW_SUBNET[idx]}" dev "br${idx}"

    # Create taps for the firewall
    ip tuntap add dev "tap${idx}" mode tap user "$(id -u)"
    ip link set "tap${idx}" up
    ip link set "tap${idx}" master "br${idx}"

    # Create namespaces
    ip netns add "namespace${idx}"

    # Create veths to connect namespaces to bridges
    ip link add "br-namespace${idx}" type veth peer name "namespace-br${idx}"

    # Attach veth to namespaces and bridges
    ip link set "namespace-br${idx}" netns "namespace${idx}"
    ip link set "br-namespace${idx}" master "br${idx}"

    # Assign ip address on the namespace side of the veths
    ip -n "namespace${idx}" addr add "${HOST_IP[idx]}/${FW_SUBNET[idx]}" dev "namespace-br${idx}"

    # Set the veth interfaces to up
    ip -n "namespace${idx}" link set "namespace-br${idx}" up
    ip link set "br-namespace${idx}" up

    # Add default routes to namespaces via the firewall
    ip -n "namespace${idx}" route add default via "${FW_IP[idx]}"
done

# Disable bridge/VLAN filtering
for ((idx = 0; idx < "${FW_INTERFACE_COUNT}"; idx++)); do
    ip link set dev "br${idx}" type bridge stp_state 0 vlan_filtering 0
done

sysctl -w net.bridge.bridge-nf-call-iptables=0
