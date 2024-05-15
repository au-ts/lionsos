#!/bin/sh
#
# This script aims to build a already checked out version of LionsOS.
#

set -e

if [ "$#" -ne 2 ]; then
    echo "usage: webserver.sh /path/to/lionsos /path/to/microkit/sdk"
    exit 1
fi

BUILD_DIR=$(pwd)/ci_build

rm -rf $BUILD_DIR

LIONSOS=$1
MICROKIT_SDK=$2

export NFS_SERVER=0.0.0.0
export NFS_DIRECTORY=test
export WEBSITE_DIR=www

cd $LIONSOS/examples/webserver
make BUILD_DIR=$BUILD_DIR MICROKIT_SDK=$MICROKIT_SDK LIONSOS=$LIONSOS
