# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import time
import os

path = 'bench.dat'

chunk_size = 0x10000 # 64k
n_chunk = 32 # 32 * 64k chunks
iterations = 8

def bytes_to_mbytes(b):
    return b / 1024**2

def time_to_perf(t_pre, t_post, bytes_read):
    seconds = (t_post - t_pre) / 1000
    mbytes_per_s = bytes_to_mbytes(bytes_read) / seconds
    return mbytes_per_s

def write():
    data = '#' * chunk_size

    tot_bytes = 0
    t_pre = time.time()
    for i in range(iterations):
        with open(path, 'wb') as f:
            for j in range(n_chunk):
                tot_bytes += f.write(data)
                # if n != chunk_size:
                #     print(f'on write chunk #{i}, {n} != {chunk_size}')

    t_post = time.time()

    if tot_bytes != n_chunk * chunk_size * iterations:
        print(f'ERROR: write never completed, expected {n_chunk * chunk_size} got {tot_bytes}')
        return
    else:
        print(f'Written {bytes_to_mbytes(tot_bytes)} MiB at a rate of {time_to_perf(t_pre, t_post, tot_bytes)} MiB/s')

def read():
    buffer = bytearray(chunk_size)

    tot_bytes = 0
    t_pre = time.time()
    for i in range(iterations):
        with open(path, 'rb') as f:
            for j in range(n_chunk):
                tot_bytes += f.readinto(buffer)
    
    t_post = time.time()
    if tot_bytes != n_chunk * chunk_size * iterations:
        print(f'ERROR: read never completed, expected {n_chunk * chunk_size} got {tot_bytes}')
        return
    else:
        print(f'Read {bytes_to_mbytes(tot_bytes)} MiB at a rate of {time_to_perf(t_pre, t_post, tot_bytes)} MiB/s')

def clean():
    os.remove(path)

def bench():
    write()
    read()
    clean()
