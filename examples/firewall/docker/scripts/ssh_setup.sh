# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

# -- Container script for setting up the ssh server -- #

mkdir -p ~/.ssh
chmod 700 ~/.ssh
touch ~/.ssh/authorized_keys
chmod 600 ~/.ssh/authorized_keys
echo ${HOST_SSH_PUB_KEY} > ~/.ssh/authorized_keys
systemctl enable ssh
systemctl start ssh

# Check running
service ssh status
