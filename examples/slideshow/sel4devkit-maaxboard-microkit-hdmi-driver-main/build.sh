#!/bin/bash

# This work is Crown Copyright NCSC, 2024.

export MICROKIT_BOARD="maaxboard"
export MICROKIT_CONFIG="debug"
export BUILD_DIR="./build"
export SOURCE_LOCATION=$(pwd)
export MICROKIT_SDK="dep/microkit/out/sel4devkit-maaxboard-microkit/out/microkit-sdk-1.4.1"
export BOARD_DIR="$MICROKIT_SDK/board/maaxboard/debug/"
export PYTHONPATH="$MICROKIT_SDK/bin"
export MICROKIT_TOOL="$MICROKIT_SDK/bin/microkit"

example_list=("empty_client" "moving_square" "resolution_change" "rotating_bars" "static_image") 

if [ -z "$1" ]; then
    echo "Please use one of the following examples as the first argument to this script. E.g ./build.sh empty_client"
    echo "${example_list[*]}"
    exit 1
fi

EXAMPLE=$1

if [[ " ${example_list[*]} " != *"$EXAMPLE"* ]];
then
    echo "Example doesn't exist Please use one of the following examples as the first argument to this script. E.g ./build.sh empty_client"
    echo "${example_list[*]}"
    exit 1
fi


if [ $1 = "empty_client" ] ; then
    export CURRENT_EXAMPLE="$1"
else
    export CURRENT_EXAMPLE="examples/$1"
fi 


# Make clean 
rm -r $BUILD_DIR/*
mkdir build
make -C $SOURCE_LOCATION