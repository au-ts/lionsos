# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

IMAGE_FILE=${1}
QEMU=${2}

source /mnt/lionsOS/examples/firewall/docker/scripts/firewall_configuration.sh

for ((idx = 0; idx < "${FW_INTERFACE_COUNT}"; idx++)); do
    device_args+="-netdev tap,id=net${idx},ifname=tap${idx},script=no,downscript=no -device virtio-net-device,netdev=net${idx},mac=${FW_MAC[${idx}]} "
done

${QEMU:-qemu-system-aarch64} -machine virt,virtualization=on \
    -cpu cortex-a53 \
    -serial mon:stdio \
    -device loader,file=${IMAGE_FILE:-/mnt/lionsOS/examples/firewall/build/firewall.img},addr=0x70000000,cpu-num=0 \
    -m size=2G \
    -nographic \
    ${device_args} \
    -global virtio-mmio.force-legacy=false
