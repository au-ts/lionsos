# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

import asyncio
import fs_raw

async def open(path):
    flag = asyncio.ThreadSafeFlag()
    request = fs_raw.request_open(path, flag)
    await flag.wait()
    fd = fs_raw.complete_open(request)
    return AsyncFile(fd)

async def stat(path):
    flag = asyncio.ThreadSafeFlag()
    request = fs_raw.request_stat(path, flag)
    await flag.wait()
    return fs_raw.complete_stat(request)

class AsyncFile:
    def __init__(self, fd):
        self.fd = fd
        self.pos = 0

    async def close(self):
        flag = asyncio.ThreadSafeFlag()
        request = fs_raw.request_close(self.fd, flag)
        await flag.wait()
        return fs_raw.complete_close(request)

    async def read(self, nbyte):
        flag = asyncio.ThreadSafeFlag()
        request = fs_raw.request_pread(self.fd, nbyte, self.pos, flag)
        await flag.wait()
        data = fs_raw.complete_pread(request)
        self.pos += len(data)
        return data

    async def pread(self, nbyte, pos):
        flag = asyncio.ThreadSafeFlag()
        request = fs_raw.request_pread(self.fd, nbyte, pos, flag)
        await flag.wait()
        data = fs_raw.complete_pread(request)
        return data
