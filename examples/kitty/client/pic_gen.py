# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# This script's purpose is to take a PNG and convert it
# into a binary format that can be read in MicroPython.
# While not particularly space-efficient, it means that we
# do not have to parse/read a PNG file in a MicroPython environment,

import png
import argparse

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--png", required=True)
    parser.add_argument("--data", required=True)
    args = parser.parse_args()

    data = list(png.Reader(args.png).read()[2])

    with open(args.data, "wb") as f:
        for d in data:
            f.write(d)

    w, h = len(data[0]) // 4, len(data)
    print("INFO: image width: {w}, height: {h}")
    with open(args.data, "rb") as f:
        pic = list(f.read())
        for y in range(h):
            for x in range(w):
                pic_pixel = pic[(y * w * 4 + (x * 4)):(y * w * 4 + (x * 4))+4]
                data_pixel = data[y][(x * 4):(x * 4)+4]
                if bytearray(pic_pixel) != data_pixel:
                    print(bytearray(pic_pixel), data_pixel, x, y)
                assert bytearray(pic_pixel) == data_pixel
