#!/bin/bash

# NOTE: This script must be run from the same directory "sel4-hdmi/picolibc"

# save current directory
cwd=$(pwd)

# cd into that directory
git clone https://github.com/sel4devkit/sel4devkit-maaxboard-microkit-picolibc
cd sel4devkit-maaxboard-microkit-picolibc
./build_script.sh

# Copy necessary files
sudo cp picolibc-microkit/newlib/libc.a $cwd
sudo cp picolibc-microkit/picolibc.specs $cwd