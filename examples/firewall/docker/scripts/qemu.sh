# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

IMAGE_FILE=${1}
QEMU=${2}

${QEMU:-qemu-system-aarch64} -machine virt,virtualization=on \
        -cpu cortex-a53 \
        -serial mon:stdio \
        -device loader,file=${IMAGE_FILE:-/mnt/lionsOS/examples/firewall/build/firewall.img},addr=0x70000000,cpu-num=0 \
        -m size=2G \
        -nographic \
        -netdev tap,id=net0,ifname=tap0,script=no,downscript=no \
        -device virtio-net-device,netdev=net0,mac=00:01:c0:39:d5:18 \
        -netdev tap,id=net1,ifname=tap1,script=no,downscript=no \
        -device virtio-net-device,netdev=net1,mac=00:01:c0:39:d5:10 \
        -global virtio-mmio.force-legacy=false
