import os
import asyncio
import fs_async
import time
from microdot import Microdot, Response
from config import base_dir

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
    def __init__(self, f):
        self.f = f

    def __aiter__(self):
        return self

    async def __anext__(self):
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


async def send_file(path, request_headers):
    if '..' in path:
        # directory traversal is not allowed
        return Response(status_code=404, reason='Not Found')
    path = f'{base_dir}/{path}'

    response_headers = {
        'Content-Type': 'application/octet-stream',
        'Cache-Control': 'max-age=31536000'
    }

    try:
        stat = await fs_async.stat(path)
    except:
        return Response(status_code=404, reason='Not Found')

    if stat[0] & 0o170000 == 0o40000: # directory
        path = f'{path}/index.html'
        try:
            stat = await fs_async.stat(path)
        except:
            return Response(status_code=404, reason='Not Found')

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

    response_headers['Last-Modified'] = format_http_date(mtime)

    f = await fs_async.open(path)
    return Response(body=FileStream(f), headers=response_headers)


app = Microdot()

@app.route('/')
async def index(request):
    return await send_file('index.html', request.headers)

@app.route('/<path:path>')
async def static(request, path):
    return await send_file(path, request.headers)

app.run(debug=True, port=80)
