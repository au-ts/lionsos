import os
import asyncio
import fs_async
from microdot import Microdot, send_file
from config import base_dir

app = Microdot()

@app.route('/')
async def index(request):
    path = f'{base_dir}/index.html'
    f = await fs_async.open(path)
    return send_file(path, stream=f) #, max_age=60)

@app.route('/<path:path>')
async def static(request, path):
    if '..' in path:
        # directory traversal is not allowed
        return 'Not found', 404

    path = f'{base_dir}/{path}'

    try:
        stat = await fs_async.stat(path)
    except:
        return 'File not found', 404

    max_age = 3600
    if stat[0] & 0o170000 == 0o40000: # directory
        path = f'{path}/index.html'
        max_age = 60

    f = await fs_async.open(path)
    return send_file(path, stream=f) #, max_age=max_age)

app.run()
