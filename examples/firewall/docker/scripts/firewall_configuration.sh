# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

# -- Variables for configuring docker and the firewall -- #

# Docker image name
export DOCKER_IMAGE="firewall_ubuntu"

# Docker container name
export DOCKER_CONTAINER="firewall_container"

# Path to LionsOS repo
export LIONSOS_REPO="/Users/jenny/Desktop/lionsos"

# Host ssh port to container
export HOST_SSH_PORT=2222

# Host ssh public key
export HOST_SSH_PUB_KEY="ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIM7p0tERaIcM2Ag7dLHCGg0TAHlXKUDDLVO9PiVuVtoZ jenny@air"

# Path to host identity
export HOST_KEY_PATH="/.ssh/id_ts.pub"

# Host http port to container
export HOST_HTTP_PORT=8080

# -------------- Firewall Network Config -------------- #

# Firewall internal IP address
export FW_EXT_IP=172.16.2.1

# Firewall external subnet network bits
export FW_EXT_SUBNET=12

# External root IP address
export EXT_ROOT_IP=172.16.2.2

# External forwarding host IP address
export EXT_HOST_IP=172.16.2.200

# Firewall internal IP address
export FW_INT_IP=192.168.1.1

# Firewall internal subnet network bits
export FW_INT_SUBNET=24

# Internal root IP address
export INT_ROOT_IP=192.168.1.2

# Internal forwarding host IP address
export INT_HOST_IP=192.168.1.100

# External non-existant host IP address
export EXT_BAD_HOST_IP=172.16.2.201

# Internal non-existant host IP address
export INT_BAD_HOST_IP=192.168.1.101
