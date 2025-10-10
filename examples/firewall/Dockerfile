FROM ubuntu:latest

RUN echo 'APT::Install-Suggests "0";' >> /etc/apt/apt.conf.d/00-docker
RUN echo 'APT::Install-Recommends "0";' >> /etc/apt/apt.conf.d/00-docker

RUN DEBIAN_FRONTEND=noninteractive \
  apt-get update \
  && apt install -y bridge-utils \
  && apt-get install -y uml-utilities \
  && apt-get install -y iproute2 \
  && apt-get install -y net-tools \
  && apt install -y isc-dhcp-client \
  && apt-get install -y git \
  && apt-get install -y iputils-ping \
  && apt-get install -y tcpdump \
  && apt-get install -y vim \
  && apt-get install -y gcc \
  && apt-get install -y wget xz-utils \
  && apt-get install -y iptables \
  && apt-get install -y netcat-openbsd \
  && apt-get install -y python3 \
  && apt install -y python3.12-venv \
  && apt install -y make cmake clang lld llvm device-tree-compiler unzip git qemu-system-arm \
  && rm -rf /var/lib/apt/lists/*

ENV ZIG_VERSION 0.12.0 # Replace with the desired Zig version

RUN wget https://ziglang.org/download/0.15.1/zig-aarch64-linux-0.15.1.tar.xz -O /tmp/zig.tar.xz --no-check-certificate && \
  tar -xf /tmp/zig.tar.xz -C /opt/ && \
  mv /opt/zig-aarch64-linux-0.15.1 /opt/zig && \
  rm /tmp/zig.tar.xz

ENV PATH="/opt/zig:$PATH"

RUN zig version

RUN wget https://github.com/seL4/microkit/releases/download/2.0.1/microkit-sdk-2.0.1-linux-aarch64.tar.gz && \
  tar xf microkit-sdk-2.0.1-linux-aarch64.tar.gz

RUN git clone https://github.com/au-ts/microkit_sdf_gen/ && \
  cd microkit_sdf_gen/ && \
  python3 -m venv venv && \
  ./venv/bin/pip install . && \
  cd ../

ENV PYTHON=/microkit_sdf_gen/venv/bin/python3 MICROKIT_SDK=/microkit-sdk-2.0.1 MICROKIT_BOARD=qemu_virt_aarch64

RUN git clone https://github.com/au-ts/lionsos/ && \
  cd lionsos/ && \
  git checkout firewall_qemu && \
  cd examples/firewall && \
  make

RUN apt-get update && apt-get install -y ca-certificates && update-ca-certificates
