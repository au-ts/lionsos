# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

# -- Commands for building, running and executing in the container -- #

# Set configuration variables
source scripts/firewall_configuration.sh

# Build the docker image
docker build -f Dockerfile . -t ${DOCKER_IMAGE}

# Run the image interactively
docker run -it \
    --privileged --cap-add=NET_ADMIN --cap-add=NET_RAW \
    -v ${LIONSOS_REPO}:/mnt/lionsOS \
    -p ${HOST_SSH_PORT}:22 \
    --name ${DOCKER_CONTAINER} \
    ${DOCKER_IMAGE}

# Open a terminal to image without using ssh
docker exec -it ${DOCKER_CONTAINER} /bin/bash

# Open a terminal to image with ssh
ssh -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -i ${HOST_KEY_PATH} \
    root@localhost -p ${HOST_SSH_PORT}

# Open an ssh port forwarding connection to container to access firewall web GUI
# at http://localhost:${HOST_HTTP_PORT}/
ssh -o StrictHostKeyChecking=no \
    -o UserKnownHostsFile=/dev/null \
    -L ${HOST_HTTP_PORT}:${FW_INT_IP}:80 \
    -i ${HOST_KEY_PATH} \
    root@localhost -p ${HOST_SSH_PORT}
