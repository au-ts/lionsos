#!/bin/sh

# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

#
# This script aims to build an already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: examples.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

LIONSOS=$1
MICROKIT_SDK=$2

echo "CI|INFO: running for LionsOS '${LIONSOS}' with Microkit '${MICROKIT_SDK}'"

$LIONSOS/ci/kitty.sh $LIONSOS $MICROKIT_SDK
$LIONSOS/ci/webserver.sh $LIONSOS $MICROKIT_SDK
# $LIONSOS/ci/vmm.sh $LIONSOS $MICROKIT_SDK
$LIONSOS/ci/fileio.sh $LIONSOS $MICROKIT_SDK
$LIONSOS/ci/firewall.sh $LIONSOS $MICROKIT_SDK
