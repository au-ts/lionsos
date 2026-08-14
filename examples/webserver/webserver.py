# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

# import interrupt

import os
import asyncio
import fs_async
import time
from microdot import Microdot, Response
from config import base_dir
import io
import errno

import json

# this is used so that I can reload something that uses the network
import asyncio

# Import the module so that we can actually do shared memory
import shared_elf # need to have the early _ for now.

content_types_map = Response.types_map | {
    'pdf': 'application/pdf',
    'svg': 'image/svg+xml',
}

# Microdot requires an object representing a file or string. That
# object can be one of several different types that look like a file,
# string or generator, but if provided any type that is not an async
# iterator then it will default to its own async wrapper. That async
# wrapper uses a fixed buffer size for reading from the file, which is
# suboptimal. Hence we implement our own class which uses a better
# buffer size.
class FileStream:
    def __init__(self, path):
        self.path = path
        self.f = None

    def __aiter__(self):
        return self

    async def __anext__(self):
        if self.f is None:
            self.f = await fs_async.open(self.path)
        buf = await self.f.read(0x8000)
        if len(buf) == 0:
            raise StopAsyncIteration
        return buf

    async def aclose(self):
        await self.f.close()


def parse_http_date(date_str):
    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    parts = date_str.split()

    weekday = parts[0][:-1]
    day = int(parts[1])
    month = months.index(parts[2]) + 1
    year = int(parts[3])
    time_parts = parts[4].split(':')
    hour = int(time_parts[0])
    minute = int(time_parts[1])
    second = int(time_parts[2])

    # yearday and isdst can be set to 0 since they are not used by mktime
    time_tuple = (year, month, day, hour, minute, second, 0, 0, 0)
    timestamp = time.mktime(time_tuple)

    return timestamp


def format_http_date(timestamp):
    days = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"]
    months = ["Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"]

    year, month, mday, hour, minute, second, weekday, yearday = time.gmtime(timestamp)
    formatted_date = "{}, {:02d} {} {:04d} {:02d}:{:02d}:{:02d} GMT".format(
        days[weekday], mday, months[month-1], year, hour, minute, second
    )

    return formatted_date


async def resolve(relative_path):
    # Standard html file extensions to try suffixing to the requested
    # file if the file does not exist.
    html_extensions = ['.html', '.htm', '.xhtml']

    def is_dir(stat):
        return stat[0] & 0o170000 == 0o40000

    async def try_stat(path):
        try:
            return await fs_async.stat(path)
        except:
            return None

    async def try_suffices(path, suffices):
        # TODO: maybe stat these concurrently
        for suffix in suffices:
            suffixed_path = path + suffix
            stat = await try_stat(suffixed_path)
            if stat is not None and not is_dir(stat):
                return 0, suffixed_path, stat
        return 404, None, None

    path = f'{base_dir}/{relative_path}'

    # If the requested path has a trailing slash, then it is intended
    # to refer to a directory. We look for an index file inside the
    # directory with the standard extensions, as well as 'index'
    # itself. The extended forms are prioritised before the
    # non-extended form to save a redundant stat in the common case.
    if relative_path.endswith('/'):
        return await try_suffices(f'{path}index', html_extensions + [''])

    # If 'name' refers to a directory but the URL has no trailing
    # slash, then we want to redirect to the form with the trailing
    # slash, ie 'name/', but we want to do this only if there does not
    # exist a file with the correct name suffixed by a standard
    # extension, eg 'name.html'.
    redirect = False

    stat = await try_stat(path)
    if stat is not None:
        if not is_dir(stat):
            return 0, path, stat
        # 'name' exists but is a directory. Record this for later.
        redirect = True

    err, path, stat = await try_suffices(path, html_extensions)
    if err == 0:
        return err, path, stat

    if redirect:
        # The requested file name referred to a directory which
        # exists, and we did not find any files with the name suffixed
        # by a standard extension, so redirect the client to the form
        # with appropriate trailing slash.
        return 301, f'/{relative_path}/', None

    return 404, None, None


async def send_file(relative_path, request_headers):
    if 'X-Real-IP' in request_headers:
        print(f'GET {relative_path} {request_headers["X-Real-IP"]}')

    if '..' in relative_path.split('/'):
        # directory traversal is not allowed
        return Response(status_code=404, reason='Not Found')

    err, path, stat = await resolve(relative_path)
    if err == 404:
        return Response(status_code=404, reason='Not Found')
    if err == 301:
        return Response.redirect(path, status_code=301)

    response_headers = {
        'Content-Type': 'application/octet-stream',
        'Cache-Control': 'max-age=31536000'
    }

    ext = path.split('.')[-1]
    if ext in content_types_map:
        response_headers['Content-Type'] = content_types_map[ext]

    short_cache_types = [
        'text/html',
        'text/css',
        'application/javascript'
    ]
    if response_headers['Content-Type'] in short_cache_types:
        response_headers['Cache-Control'] = 'max-age=600'

    mtime = stat[8]

    try:
        imstime = parse_http_date(request_headers['If-Modified-Since'])
        if imstime >= mtime:
            return Response(status_code=304, reason='Not Modified', headers=response_headers)
    except:
        pass # malformed If-Modified-Since header should be ignored

    length = stat[6]
    response_headers['Content-Length'] = f'{length}'

    response_headers['Last-Modified'] = format_http_date(mtime)

    return Response(body=FileStream(path), headers=response_headers)

app = Microdot()

@app.route('/')
async def index(request):
    return await send_file('index.html', request.headers)

@app.route('/<path:path>')
async def static(request, path):
    return await send_file(path, request.headers)

OP_FINALIZE_RELOAD = -1

@app.post("/dynamic_loading")
async def dynamic_load(request):
    target_vaddr = int(request.headers["vaddr"])
    size = int(request.headers["seg_size"])
    to_read = size
    buf = bytearray()

    while True:
        chunk = await request.stream.read(min(1400, to_read)) # I think that streams are notified when their thing is ready
        if not chunk:
            break
        to_read -= len(chunk)
        buf.extend(chunk)
        if to_read == 0:
            break

    segment_bytes = bytes(buf)
    shared_elf.machine_shared_elf_send(
        memoryview(segment_bytes),
        len(segment_bytes),
        target_vaddr,
    )

    return "", 204

@app.post("/update_pd")
def reload_handler(request):
    target_pd = int(request.headers["pd_id"])
    elf_entry = int(request.headers["entry"])
    passive_vaddr = int(request.headers["passive_vaddr"])

    async def delayed_reload():
        await asyncio.sleep(0.6)

        print("We are finishing up with reloading")

        # we make our pd negative to signal
        shared_elf.machine_shared_elf_send(
            target_pd,
            passive_vaddr,
            -1 * elf_entry, # signal that this is a reload
        )
        print("reloaded the driver")

    asyncio.create_task(delayed_reload())

    print("return!")

    return "", 204

shared_elf.wait()
app.run(debug=True, port=80)
