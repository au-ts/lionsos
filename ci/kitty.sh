#!/bin/sh
#
# This script aims to build a already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: kitty.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

BUILD_DIR=$(pwd)/ci_build

rm -rf $BUILD_DIR

LIONSOS=$1
MICROKIT_SDK=$2

export NFS_SERVER=0.0.0.0
export NFS_DIRECTORY=test

cd $LIONSOS/dep/micropython
git submodule update --init lib/micropython-lib
cd $LIONSOS/examples/kitty

build_kitty() {
    BOARD=$1
    echo "CI|INFO: building kitty with board: ${BOARD}"
    rm -rf ${BUILD_DIR}
    make BUILD_DIR=$BUILD_DIR MICROKIT_SDK=$MICROKIT_SDK LIONSOS=$LIONSOS MICROKIT_BOARD=$BOARD
}

BOARDS=("odroidc4" "qemu_arm_virt")
for BOARD in "${BOARDS[@]}"
do
    build_kitty ${BOARD}
done

echo ""
echo "CI|INFO: Passed all kitty tests"