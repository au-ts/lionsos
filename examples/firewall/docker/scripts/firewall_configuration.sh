# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

# -- Variables for configuring docker and the firewall -- #

# Docker image name
export DOCKER_IMAGE="firewall_ubuntu"

# Docker container name
export DOCKER_CONTAINER="firewall_container"

# Path to LionsOS repo
export LIONSOS_REPO=

# Host ssh port to container
export HOST_SSH_PORT=2222

# Host ssh public key
export HOST_SSH_PUB_KEY=

# Path to host identity
export HOST_KEY_PATH=

# Host http port to container
export HOST_HTTP_PORT=8080

# -------------- Firewall Network Config -------------- #

# Number of firewall interfaces configured for QEMU testing.
export INTERFACE_COUNT=3

# Firewall interface0 MAC address
export FW_INTERFACE0_MAC=00:01:c0:39:d5:18

# Firewall interface0 IP address
export FW_INTERFACE0_IP=172.16.2.1

# Firewall interface0 subnet network bits
export FW_INTERFACE0_SUBNET=12

# Interface0 root IP address
export INTERFACE0_ROOT_IP=172.16.2.2

# Interface0 forwarding host IP address
export INTERFACE0_HOST_IP=172.16.2.200

# Firewall interface1 MAC address
export FW_INTERFACE1_MAC=00:01:c0:39:d5:10

# Firewall interface1 IP address
export FW_INTERFACE1_IP=192.168.1.1

# Firewall interface1 subnet network bits
export FW_INTERFACE1_SUBNET=24

# Interface1 root IP address
export INTERFACE1_ROOT_IP=192.168.1.2

# Interface1 forwarding host IP address
export INTERFACE1_HOST_IP=192.168.1.100

# Firewall interface2 MAC address
export FW_INTERFACE2_MAC=00:01:c0:39:d5:12

# Firewall interface2 IP address
export FW_INTERFACE2_IP=10.0.2.1

# Firewall interface2 subnet network bits
export FW_INTERFACE2_SUBNET=24

# Interface2 root IP address
export INTERFACE2_ROOT_IP=10.0.2.2

# Interface2 forwarding host IP address
export INTERFACE2_HOST_IP=10.0.2.100

# Interface0 non-existant host IP address
export INTERFACE0_BAD_HOST_IP=172.16.2.201

# Interface1 non-existant host IP address
export INTERFACE1_BAD_HOST_IP=192.168.1.101

# Interface2 non-existant host IP address
export INTERFACE2_BAD_HOST_IP=10.0.2.101

# Interface0 unknown network host IP address
export INTERFACE0_BAD_NET_IP=173.16.2.201

# Interface1 unknown network host IP address
export INTERFACE1_BAD_NET_IP=193.168.1.101

# Interface2 unknown network host IP address
export INTERFACE2_BAD_NET_IP=10.0.3.101
