#!/usr/bin/env bash

# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

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

# MAC addresses of the firewall's interfaces
export FW_MAC=("00:01:c0:39:d5:18" "00:01:c0:39:d5:10" "00:01:c0:39:d5:12")

# IP addresses of the firewall's interfaces
export FW_IP=(172.16.2.1 192.168.1.1 10.0.2.1)

# IP address of firewall interface to reach the webserver
export FW_WEBSERVER_IP=192.168.1.1

# Network bits of the firewall's subnets
export FW_SUBNET=(12 24 24)

# Firewall subnet broadcast addresses
export BROADCAST_IP=(172.31.255.255 192.168.1.255 10.0.2.255)

# IP addresses of the root task in each subnet
export ROOT_IP=(172.16.2.2 192.168.1.2 10.0.2.2)

# IP addresses of the testing hosts in each subnet
export HOST_IP=(172.16.2.200 192.168.1.100 10.0.2.100)

# Non-existent host IP addresses
export BAD_HOST_IP=(172.16.2.201 192.168.1.101 10.0.2.101)

# Non-existent net IP addresses
export BAD_NET_IP=(173.16.2.201 193.168.1.101 10.0.3.101)

# Number of interfaces of the firewall
export FW_INTERFACE_COUNT="${#FW_IP[@]}"
