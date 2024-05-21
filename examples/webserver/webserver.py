import os
import asyncio
import fs_async
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


async def send_file(path):
    if '..' in path:
        # directory traversal is not allowed
        return Response(status_code=404, reason='Not Found')
    path = f'{base_dir}/{path}'

    headers = {
        'Content-Type': 'application/octet-stream',
        'Cache-Control': 'max-age=3600'
    }

    try:
        stat = await fs_async.stat(path)
    except:
        return Response(status_code=404, reason='Not Found')

    if stat[0] & 0o170000 == 0o40000: # directory
        path = f'{path}/index.html'
        headers['Cache-Control'] = 'max-age=0'

    ext = path.split('.')[-1]
    if ext in content_types_map:
        headers['Content-Type'] = content_types_map[ext]

    f = await fs_async.open(path)
    return Response(body=FileStream(f), headers=headers)


app = Microdot()

@app.route('/')
async def index(request):
    return await send_file('index.html')

@app.route('/<path:path>')
async def static(request, path):
    return await send_file(path)

app.run(debug=True, port=80)
