#!/bin/bash
#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: webserver.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

build() {
    MICROKIT_BOARD=$1
    MICROKIT_CONFIG=$2

    echo "CI|INFO: building webserver for board ${MICROKIT_BOARD} config ${MICROKIT_CONFIG}"

    BUILD_DIR=$LIONSOS/ci_build/webserver/${MICROKIT_BOARD}/${MICROKIT_CONFIG}
    rm -rf $BUILD_DIR

    export NFS_SERVER=0.0.0.0
    export NFS_DIRECTORY=test
    export WEBSITE_DIR=www
    export BUILD_DIR=$BUILD_DIR
    export MICROKIT_SDK=$MICROKIT_SDK
    export MICROKIT_CONFIG=$MICROKIT_CONFIG
    export MICROKIT_BOARD=$MICROKIT_BOARD
    export LIONSOS=$LIONSOS

    cd $LIONSOS/examples/webserver
    make
}

build "odroidc4" "debug"
build "odroidc4" "release"
build "qemu_virt_aarch64" "debug"
build "qemu_virt_aarch64" "release"
