#!/bin/sh
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

BUILD_DIR=$LIONSOS/ci_build/webserver
rm -rf $BUILD_DIR

export NFS_SERVER=0.0.0.0
export NFS_DIRECTORY=test
export WEBSITE_DIR=www

cd $LIONSOS/examples/webserver
make BUILD_DIR=$BUILD_DIR MICROKIT_SDK=$MICROKIT_SDK LIONSOS=$LIONSOS
