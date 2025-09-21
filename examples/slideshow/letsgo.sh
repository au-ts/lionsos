#!/bin/sh

# rm -rfd build

make MICROKIT_BOARD=maaxboard MICROKIT_SDK=/Users/dreamliner787-9/TS/microkit-capdl-dev/release/microkit-sdk-2.0.1-dev && \

scp build/slideshow.img billn@tftp:/tftpboot/maaxboard_billn/loader.img