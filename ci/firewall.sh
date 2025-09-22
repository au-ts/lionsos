#!/bin/bash

# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: firewall.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

build() {
    MICROKIT_BOARD=$1
    MICROKIT_CONFIG=$2

    echo "CI|INFO: building firewall for board ${MICROKIT_BOARD} config ${MICROKIT_CONFIG}"

    BUILD_DIR=$LIONSOS/ci_build/firewall/${MICROKIT_BOARD}/${MICROKIT_CONFIG}
    rm -rf $BUILD_DIR

    export BUILD_DIR=$BUILD_DIR
    export MICROKIT_SDK=$MICROKIT_SDK
    export MICROKIT_CONFIG=$MICROKIT_CONFIG
    export MICROKIT_BOARD=$MICROKIT_BOARD
    export LIONSOS=$LIONSOS

    cd $LIONSOS/examples/firewall
    make
}

build "imx8mp_iotgate" "debug"
build "imx8mp_iotgate" "release"
