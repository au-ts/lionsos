#!/bin/bash

# Copyright 2026, LionsOS Contributors
# SPDX-License-Identifier: BSD-2-Clause

#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: desktop.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

build() {
    MICROKIT_BOARD=$1
    MICROKIT_CONFIG=$2

    echo "CI|INFO: building desktop for board ${MICROKIT_BOARD} config ${MICROKIT_CONFIG}"

    BUILD_DIR=$LIONSOS/ci_build/desktop/${MICROKIT_BOARD}/${MICROKIT_CONFIG}
    rm -rf $BUILD_DIR

    export BUILD_DIR=$BUILD_DIR
    export MICROKIT_SDK=$MICROKIT_SDK
    export MICROKIT_CONFIG=$MICROKIT_CONFIG
    export MICROKIT_BOARD=$MICROKIT_BOARD
    export LIONSOS=$LIONSOS

    cd $LIONSOS/examples/desktop
    make
}

build "qemu_virt_aarch64" "debug"
build "qemu_virt_aarch64" "release"
