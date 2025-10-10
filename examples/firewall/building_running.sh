#!/bin/bash

# Build the docker image
docker build . -t myubuntu

# Run the image
docker run -it --privileged --cap-add=NET_ADMIN --cap-add=NET_RAW --device /dev/net/tun myubuntu

# Extract image hash
DIMAGE=$(docker inspect --format='{{.ID}}' myubuntu)

# Remove sha256: preamble
DIMAGE=${DIMAGE:7}

# Open other terminals to running image
docker exec -it $DIMAGE /bin/bash

# From inside the container
./net_setup.sh
cd lionsos/examples/firewall/
make qemu

# Now the firewall is running, use commands in testing.sh to test
