#!/bin/sh
#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: kitty.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

BUILD_DIR=$LIONSOS/ci_build/kitty
rm -rf $BUILD_DIR

export NFS_SERVER=0.0.0.0
export NFS_DIRECTORY=test
export BUILD_DIR=$BUILD_DIR
export MICROKIT_SDK=$MICROKIT_SDK
export LIONSOS=$LIONSOS

cd $LIONSOS/examples/kitty
make submodules
make images
make

