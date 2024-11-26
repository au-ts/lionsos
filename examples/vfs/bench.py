# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import time

path = 'bench.dat'

def bench():
    with open(path, 'w') as f:
        for i in range(8):
            f.write('#' * 1024**2)

    bytes_read = 0
    with open(path) as f:
        buffer_size = 0x8000
        _buffer = bytearray(buffer_size)
        _bmview = memoryview(_buffer)
        t_pre = time.time()
        for i in range(16):
            while True:
                n = f.readinto(_buffer)
                if n == 0:
                    break
                bytes_read += n
            f.seek(0)
        t = time.time() - t_pre

    mbytes_read = bytes_read / 1024**2
    seconds = t / 1000
    mbytes_per_s = mbytes_read / seconds
    print(f'Read {mbytes_read} MiB at a rate of {mbytes_per_s} MiB/s')
