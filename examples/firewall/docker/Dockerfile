# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

FROM ubuntu:24.04

RUN DEBIAN_FRONTEND=noninteractive

# Install relevant network utilities
RUN apt-get update \
  && apt install -y bridge-utils \
  && apt install -y isc-dhcp-client \
  && apt install -y qemu-system-arm \
  && apt-get install -y uml-utilities \
  && apt-get install -y iproute2 \
  && apt-get install -y net-tools \
  && apt-get install -y iputils-ping \
  && apt-get install -y tcpdump \
  && apt-get install -y vim \
  && apt-get install -y wget xz-utils \
  && apt-get install -y iptables \
  && apt-get install -y netcat-openbsd \
  && apt-get install -y shunit2 \
  && apt-get install -y curl

# Update CA certificates
RUN apt-get install -y ca-certificates \
  && update-ca-certificates

# Install SSH server
RUN apt-get install -y openssh-server \
  && mkdir /var/run/sshd

EXPOSE 22

# Make configuration variables visible in container
RUN echo "source /mnt/lionsOS/examples/firewall/docker/scripts/firewall_configuration.sh" >> ~/.bashrc

# Clean up
RUN rm -rf /var/lib/apt/lists/*

CMD ["/bin/bash"]
