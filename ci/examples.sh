#!/bin/sh
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

$LIONSOS/ci/kitty.sh $LIONSOS $MICROKIT_SDK
$LIONSOS/ci/webserver.sh $LIONSOS $MICROKIT_SDK
$LIONSOS/ci/vmm.sh $LIONSOS $MICROKIT_SDK
$LIONSOS/ci/vmm-virtio.sh $LIONSOS $MICROKIT_SDK
