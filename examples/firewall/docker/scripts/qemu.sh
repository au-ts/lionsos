# Copyright 2025, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#!/bin/bash

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

IMAGE_FILE=${1}
QEMU=${2}

source "${SCRIPT_DIR}/firewall_configuration.sh"

interface_mac() {
    local idx=$1
    local var_name="FW_INTERFACE${idx}_MAC"
    printf '%s' "${!var_name}"
}

qemu_args=(
    -machine virt,virtualization=on
    -cpu cortex-a53
    -serial mon:stdio
    -device "loader,file=${IMAGE_FILE:-/mnt/lionsOS/examples/firewall/build/firewall.img},addr=0x70000000,cpu-num=0"
    -m size=2G
    -nographic
)

for ((idx = 0; idx < INTERFACE_COUNT; idx++)); do
    qemu_args+=(
        -netdev "tap,id=net${idx},ifname=tap${idx},script=no,downscript=no"
        -device "virtio-net-device,netdev=net${idx},mac=$(interface_mac "${idx}")"
    )
done

qemu_args+=(-global virtio-mmio.force-legacy=false)

"${QEMU:-qemu-system-aarch64}" "${qemu_args[@]}"
