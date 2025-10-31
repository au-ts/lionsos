#!/bin/bash

# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: posix.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

build() {
    MICROKIT_BOARD=$1
    MICROKIT_CONFIG=debug

    echo "CI|INFO: building posix for board: ${MICROKIT_BOARD}"

    BUILD_DIR=$LIONSOS/ci_build/posix/${MICROKIT_BOARD}/${MICROKIT_CONFIG}
    rm -rf $BUILD_DIR

    export BUILD_DIR=$BUILD_DIR
    export MICROKIT_SDK=$MICROKIT_SDK
    export MICROKIT_CONFIG=$MICROKIT_CONFIG
    export MICROKIT_BOARD=$MICROKIT_BOARD
    export LIONSOS=$LIONSOS

    cd $LIONSOS/examples/posix
    make
}

BOARDS=("qemu_virt_aarch64")
for BOARD in "${BOARDS[@]}"
do
    build ${BOARD}
done

echo ""
echo "CI|INFO: Passed all posix build tests"
