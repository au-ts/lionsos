#!/bin/sh

# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: vmm.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

BUILD_DIR=$LIONSOS/ci_build/vmm
rm -rf $BUILD_DIR

export BUILD_DIR=$BUILD_DIR
export MICROKIT_SDK=$MICROKIT_SDK
export LIONSOS=$LIONSOS

cd $LIONSOS/examples/vmm
make -j$(nproc)
