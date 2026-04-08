#!/usr/bin/env bash

# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# -- Container script for setting up the network -- #

interface_value() {
    local var_name=$1
    printf '%s' "${!var_name}"
}

idx=0
while [ "${idx}" -lt "${INTERFACE_COUNT}" ]; do
    bridge="br${idx}"
    tap="tap${idx}"
    namespace="namespace${idx}"
    bridge_namespace="${bridge}-${namespace}"
    namespace_bridge="${namespace}-${bridge}"

    root_ip=$(interface_value "INTERFACE${idx}_ROOT_IP")
    host_ip=$(interface_value "INTERFACE${idx}_HOST_IP")
    firewall_ip=$(interface_value "FW_INTERFACE${idx}_IP")
    subnet_bits=$(interface_value "FW_INTERFACE${idx}_SUBNET")

    # Create bridges to connect namespaces to taps
    ip link add "${bridge}" type bridge
    ip link set "${bridge}" up
    ip addr add "${root_ip}/${subnet_bits}" dev "${bridge}"

    # Create taps for the firewall
    ip tuntap add dev "${tap}" mode tap user "$(id -u)"
    ip link set "${tap}" up
    ip link set "${tap}" master "${bridge}"

    # Create namespaces
    ip netns add "${namespace}"

    # Create veths to connect namespaces to bridges
    ip link add "${bridge_namespace}" type veth peer name "${namespace_bridge}"

    # Attach veth to namespaces and bridges
    ip link set "${namespace_bridge}" netns "${namespace}"
    ip link set "${bridge_namespace}" master "${bridge}"

    # Assign ip address on the namespace side of the veths
    ip -n "${namespace}" addr add "${host_ip}/${subnet_bits}" dev "${namespace_bridge}"

    # Set the veth interfaces to up
    ip -n "${namespace}" link set "${namespace_bridge}" up
    ip link set "${bridge_namespace}" up

    # Add default routes to namespaces via the firewall
    ip -n "${namespace}" route add default via "${firewall_ip}"

    idx=$((idx + 1))
done

# Disable bridge/VLAN filtering
idx=0
while [ "${idx}" -lt "${INTERFACE_COUNT}" ]; do
    ip link set dev "br${idx}" type bridge stp_state 0 vlan_filtering 0
    idx=$((idx + 1))
done

sysctl -w net.bridge.bridge-nf-call-iptables=0
