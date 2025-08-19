# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause

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

def index_page():
    html = """
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta http-equiv="Content-Type" content="text/html; charset=UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <title>The seL4 Microkernel | seL4</title>
    <meta name="author" content="seL4 Foundation">
    <link rel="canonical" href="https://sel4.systems/">
    <link href="/css/sel4.css?v=1754463370" rel="stylesheet">
    <link rel="preconnect" href="https://fonts.googleapis.com">
    <link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>
    <link href="https://fonts.googleapis.com/css2?family=Roboto:ital,wght@0,100;0,300;0,400;0,500;0,700;0,900;1,100;1,300;1,400;1,500;1,700;1,900&display=swap" rel="stylesheet">
    <link rel=alternate title="Atom Feed" type=application/atom+xml href="/feed.xml">
    <link rel="apple-touch-icon" sizes="57x57" href="/images/icons/apple-touch-icon-57x57.png">
    <link rel="apple-touch-icon" sizes="114x114" href="/images/icons/apple-touch-icon-114x114.png">
    <link rel="apple-touch-icon" sizes="72x72" href="/images/icons/apple-touch-icon-72x72.png">
    <link rel="apple-touch-icon" sizes="144x144" href="/images/icons/apple-touch-icon-144x144.png">
    <link rel="apple-touch-icon" sizes="60x60" href="/images/icons/apple-touch-icon-60x60.png">
    <link rel="apple-touch-icon" sizes="120x120" href="/images/icons/apple-touch-icon-120x120.png">
    <link rel="apple-touch-icon" sizes="76x76" href="/images/icons/apple-touch-icon-76x76.png">
    <link rel="apple-touch-icon" sizes="152x152" href="/images/icons/apple-touch-icon-152x152.png">
    <link rel="icon" type="image/png" href="/images/icons/favicon-96x96.png" sizes="96x96">
    <link rel="icon" type="image/png" href="/images/icons/favicon-32x32.png" sizes="32x32">
    <link rel="icon" type="image/png" href="/images/icons/favicon-16x16.png" sizes="16x16">
    <meta name="application-name" content="&nbsp;">
    <meta name="msapplication-TileColor" content="#FFFFFF">
    <meta name="msapplication-TileImage" content="/images/icons/mstile-144x144.png">
    <meta name="msapplication-square70x70logo" content="/images/icons/mstile-70x70.png">
    <meta name="msapplication-square150x150logo" content="/images/icons/mstile-150x150.png">
    <meta name="msapplication-wide310x150logo" content="/images/icons/mstile-310x150.png">
    <meta name="msapplication-square310x310logo" content="/images/icons/mstile-310x310.png">
    <link rel="shortcut icon" href="/images/icons/favicon.ico">
    <script defer src="/js/menu.js"></script>

    <script defer data-domain="sel4.systems" src="https://analytics.sel4.systems/js/script.js"></script>
    <script defer src="js/3d-force-graph@1.73.6.js" ></script>
  </head>
  <body class="flex flex-col h-screen dark:bg-[#030020] bg-white"><header class="bg-white dark:bg-darkblue">
  <nav class="mx-auto flex items-center justify-between px-6 py-2 lg:pt-4 lg:pb-3 mb-2 lg:px-8
              shadow-md relative z-10 bg-white/90 dark:bg-darkblue/90 backdrop-blur-md" aria-label="Global">
    <div class="flex lg:flex-none">
      <a href="/" class="-m-1.5 p-1.5">
        <span class="sr-only">seL4</span>
        <img class="h-6 lg:h-8 w-auto" src="/images/seL4.svg" alt="seL4 logo">
      </a>
    </div>
    <div class="flex flex-1"></div>

    <div class="hidden md:flex md:gap-x-3 lg:gap-x-5 xl:gap-x-10"><div class="relative">
  <a class="dropbtn" aria-expanded="false" href="#">
    <span>About</span>
    <span class="hidden lg:inline"> seL4</span>
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="drop-icon">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
  </a><div class="absolute -left-14 top-full z-10 mt-4 flex w-screen max-w-max px-4 transition ease-in-out duration-200 dropmenu">
    <div class="w-screen max-w-md flex-auto overflow-hidden rounded-3xl bg-white dark:bg-darkblue text-sm leading-6 shadow-lg ring-1 ring-gray-900/5 dark:ring-gray-100/20 lg:max-w-3xl">
      <div class="grid grid-cols-1 gap-x-6 gap-y-1 p-4 lg:grid-cols-2">
<div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 18v-5.25m0 0a6.01 6.01 0 0 0 1.5-.189m-1.5.189a6.01 6.01 0 0 1-1.5-.189m3.75 7.478a12.06 12.06 0 0 1-4.5 0m3.75 2.383a14.406 14.406 0 0 1-3 0M14.25 18v-.192c0-.983.658-1.823 1.508-2.316a7.5 7.5 0 1 0-7.517 0c.85.493 1.509 1.333 1.509 2.316V18"/>
</svg></div>
          <div>
            <a href="/About/" class="font-semibold text-dark">
              What is seL4?
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">History, Fact Sheet, White Paper. How to use seL4 in your system.</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M18 18.72a9.094 9.094 0 0 0 3.741-.479 3 3 0 0 0-4.682-2.72m.94 3.198.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0 1 12 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 0 1 6 18.719m12 0a5.971 5.971 0 0 0-.941-3.197m0 0A5.995 5.995 0 0 0 12 12.75a5.995 5.995 0 0 0-5.058 2.772m0 0a3 3 0 0 0-4.681 2.72 8.986 8.986 0 0 0 3.74.477m.94-3.197a5.971 5.971 0 0 0-.94 3.197M15 6.75a3 3 0 1 1-6 0 3 3 0 0 1 6 0Zm6 3a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Zm-13.5 0a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Z"/>
</svg></div>
          <div>
            <a href="/use.html" class="font-semibold text-dark">
              Who is using seL4?
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Where seL4 is used and deployed world wide</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75 11.25 15 15 9.75M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z"/>
</svg></div>
          <div>
            <a href="/Verification/" class="font-semibold text-dark">
              Verification
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">seL4 Proofs; implications for seL4-based systems and certification</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M2.25 18 9 11.25l4.306 4.306a11.95 11.95 0 0 1 5.814-5.518l2.74-1.22m0 0-5.94-2.281m5.94 2.28-2.28 5.941"/>
</svg></div>
          <div>
            <a href="/roadmap.html" class="font-semibold text-dark">
              Roadmap
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Planned development &amp; verification</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="m3.75 13.5 10.5-11.25L12 10.5h8.25L9.75 21.75 12 13.5H3.75Z"/>
</svg></div>
          <div>
            <a href="/performance.html" class="font-semibold text-dark">
              Performance
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">The numbers on the world&#39;s fastest kernel</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 3v17.25m0 0c-1.472 0-2.882.265-4.185.75M12 20.25c1.472 0 2.882.265 4.185.75M18.75 4.97A48.416 48.416 0 0 0 12 4.5c-2.291 0-4.545.16-6.75.47m13.5 0c1.01.143 2.01.317 3 .52m-3-.52 2.62 10.726c.122.499-.106 1.028-.589 1.202a5.988 5.988 0 0 1-2.031.352 5.988 5.988 0 0 1-2.031-.352c-.483-.174-.711-.703-.59-1.202L18.75 4.971Zm-16.5.52c.99-.203 1.99-.377 3-.52m0 0 2.62 10.726c.122.499-.106 1.028-.589 1.202a5.989 5.989 0 0 1-2.031.352 5.989 5.989 0 0 1-2.031-.352c-.483-.174-.711-.703-.59-1.202L5.25 4.971Z"/>
</svg></div>
          <div>
            <a href="/Legal/" class="font-semibold text-dark">
              License, Trademark &amp; Logo
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">seL4 License and Implications; Trademark Compliance Guide</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M8.25 3v1.5M4.5 8.25H3m18 0h-1.5M4.5 12H3m18 0h-1.5m-15 3.75H3m18 0h-1.5M8.25 19.5V21M12 3v1.5m0 15V21m3.75-18v1.5m0 15V21m-9-1.5h10.5a2.25 2.25 0 0 0 2.25-2.25V6.75a2.25 2.25 0 0 0-2.25-2.25H6.75A2.25 2.25 0 0 0 4.5 6.75v10.5a2.25 2.25 0 0 0 2.25 2.25Zm.75-12h9v9h-9v-9Z"/>
</svg></div>
          <div>
            <a href="/platforms.html" class="font-semibold text-dark">
              Platform Support Overview
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Hardware and simulators seL4 runs on</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M4.26 10.147a60.438 60.438 0 0 0-.491 6.347A48.62 48.62 0 0 1 12 20.904a48.62 48.62 0 0 1 8.232-4.41 60.46 60.46 0 0 0-.491-6.347m-15.482 0a50.636 50.636 0 0 0-2.658-.813A59.906 59.906 0 0 1 12 3.493a59.903 59.903 0 0 1 10.399 5.84c-.896.248-1.783.52-2.658.814m-15.482 0A50.717 50.717 0 0 1 12 13.489a50.702 50.702 0 0 1 7.74-3.342M6.75 15a.75.75 0 1 0 0-1.5.75.75 0 0 0 0 1.5Zm0 0v-3.675A55.378 55.378 0 0 1 12 8.443m-7.007 11.55A5.981 5.981 0 0 0 6.75 15.75v-1.5"/>
</svg></div>
          <div>
            <a href="/Research/" class="font-semibold text-dark">
              Research
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Seminal seL4 Publications; Ongoing Research; Courses in the Ecosystem</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M13.5 16.875h3.375m0 0h3.375m-3.375 0V13.5m0 3.375v3.375M6 10.5h2.25a2.25 2.25 0 0 0 2.25-2.25V6a2.25 2.25 0 0 0-2.25-2.25H6A2.25 2.25 0 0 0 3.75 6v2.25A2.25 2.25 0 0 0 6 10.5Zm0 9.75h2.25A2.25 2.25 0 0 0 10.5 18v-2.25a2.25 2.25 0 0 0-2.25-2.25H6a2.25 2.25 0 0 0-2.25 2.25V18A2.25 2.25 0 0 0 6 20.25Zm9.75-9.75H18a2.25 2.25 0 0 0 2.25-2.25V6A2.25 2.25 0 0 0 18 3.75h-2.25A2.25 2.25 0 0 0 13.5 6v2.25a2.25 2.25 0 0 0 2.25 2.25Z"/>
</svg></div>
          <div>
            <a href="/tools.html" class="font-semibold text-dark">
              Frameworks &amp; Languages
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Frameworks, Tools, OS Personalities, Components, Language Support</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 21v-8.25M15.75 21v-8.25M8.25 21v-8.25M3 9l9-6 9 6m-1.5 12V10.332A48.36 48.36 0 0 0 12 9.75c-2.551 0-5.056.2-7.5.582V21M3 21h18M12 6.75h.008v.008H12V6.75Z"/>
</svg></div>
          <div>
            <a href="/Foundation/" class="font-semibold text-dark">
              seL4 Foundation
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Members, Board, and Technical Steering Committee</p>
          </div>
        </div>
      </div><div class="grid grid-cols-1 gap-x-6 gap-y-1 p-4 lg:grid-cols-2 bg-gray-50 dark:bg-slate-900"><div class="group menu-item-group hover:bg-gray-100 hover:dark:bg-slate-800">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 6.042A8.967 8.967 0 0 0 6 3.75c-1.052 0-2.062.18-3 .512v14.25A8.987 8.987 0 0 1 6 18c2.305 0 4.408.867 6 2.292m0-14.25a8.966 8.966 0 0 1 6-2.292c1.052 0 2.062.18 3 .512v14.25A8.987 8.987 0 0 0 18 18a8.967 8.967 0 0 0-6 2.292m0-14.25v14.25"/>
</svg></div>
          <div>
            <a href="/Learn/" class="font-semibold text-dark">
              Manual &amp; Documentation
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Manual, Tutorials, Documentation</p>
          </div>
        </div><div class="group menu-item-group hover:bg-gray-100 hover:dark:bg-slate-800">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M9.879 7.519c1.171-1.025 3.071-1.025 4.242 0 1.172 1.025 1.172 2.687 0 3.712-.203.179-.43.326-.67.442-.745.361-1.45.999-1.45 1.827v.75M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Zm-9 5.25h.008v.008H12v-.008Z"/>
</svg></div>
          <div>
            <a href="/About/FAQ.html" class="font-semibold text-dark">
              FAQ
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Frequently Asked Questions</p>
          </div>
        </div></div></div>
  </div>
</div><div class="relative">
  <a class="dropbtn" aria-expanded="false" href="#">
    <span>Contribute</span>
    <span class="hidden lg:inline"></span>
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="drop-icon">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
  </a><div class="absolute -left-14 top-full z-10 mt-4 flex w-screen max-w-max px-4 transition ease-in-out duration-200 dropmenu">
    <div class="w-screen max-w-md flex-auto overflow-hidden rounded-3xl bg-white dark:bg-darkblue text-sm leading-6 shadow-lg ring-1 ring-gray-900/5 dark:ring-gray-100/20 lg:max-w-3xl">
      <div class="grid grid-cols-1 gap-x-6 gap-y-1 p-4 lg:grid-cols-2">
<div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6A2.25 2.25 0 0 1 6 3.75h2.25A2.25 2.25 0 0 1 10.5 6v2.25a2.25 2.25 0 0 1-2.25 2.25H6a2.25 2.25 0 0 1-2.25-2.25V6ZM3.75 15.75A2.25 2.25 0 0 1 6 13.5h2.25a2.25 2.25 0 0 1 2.25 2.25V18a2.25 2.25 0 0 1-2.25 2.25H6A2.25 2.25 0 0 1 3.75 18v-2.25ZM13.5 6a2.25 2.25 0 0 1 2.25-2.25H18A2.25 2.25 0 0 1 20.25 6v2.25A2.25 2.25 0 0 1 18 10.5h-2.25a2.25 2.25 0 0 1-2.25-2.25V6ZM13.5 15.75a2.25 2.25 0 0 1 2.25-2.25H18a2.25 2.25 0 0 1 2.25 2.25V18A2.25 2.25 0 0 1 18 20.25h-2.25A2.25 2.25 0 0 1 13.5 18v-2.25Z"/>
</svg></div>
          <div>
            <a href="/Contribute/" class="font-semibold text-dark">
              Overview
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Overview of contribution guidelines and processes</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0 3.181 3.183a8.25 8.25 0 0 0 13.803-3.7M4.031 9.865a8.25 8.25 0 0 1 13.803-3.7l3.181 3.182m0-4.991v4.99"/>
</svg></div>
          <div>
            <a href="/Contribute/guidelines.html" class="font-semibold text-dark">
              Contribution Guidelines
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Guidelines on how to contribute to the seL4 ecosystem</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75 11.25 15 15 9.75m-3-7.036A11.959 11.959 0 0 1 3.598 6 11.99 11.99 0 0 0 3 9.749c0 5.592 3.824 10.29 9 11.623 5.176-1.332 9-6.03 9-11.622 0-1.31-.21-2.571-.598-3.751h-.152c-3.196 0-6.1-1.248-8.25-3.285Z"/>
</svg></div>
          <div>
            <a href="/Contribute/conduct.html" class="font-semibold text-dark">
              Code of Conduct
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Rules for interaction</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="m6.75 7.5 3 2.25-3 2.25m4.5 0h3m-9 8.25h13.5A2.25 2.25 0 0 0 21 18V6a2.25 2.25 0 0 0-2.25-2.25H5.25A2.25 2.25 0 0 0 3 6v12a2.25 2.25 0 0 0 2.25 2.25Z"/>
</svg></div>
          <div>
            <a href="/Contribute/conventions.html" class="font-semibold text-dark">
              Coding Conventions &amp; Requirements
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Requirements for C Code, Git Commits, Pull Requests</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M20.25 8.511c.884.284 1.5 1.128 1.5 2.097v4.286c0 1.136-.847 2.1-1.98 2.193-.34.027-.68.052-1.02.072v3.091l-3-3c-1.354 0-2.694-.055-4.02-.163a2.115 2.115 0 0 1-.825-.242m9.345-8.334a2.126 2.126 0 0 0-.476-.095 48.64 48.64 0 0 0-8.048 0c-1.131.094-1.976 1.057-1.976 2.192v4.286c0 .837.46 1.58 1.155 1.951m9.345-8.334V6.637c0-1.621-1.152-3.026-2.76-3.235A48.455 48.455 0 0 0 11.25 3c-2.115 0-4.198.137-6.24.402-1.608.209-2.76 1.614-2.76 3.235v6.226c0 1.621 1.152 3.026 2.76 3.235.577.075 1.157.14 1.74.194V21l4.155-4.155"/>
</svg></div>
          <div>
            <a href="/Contribute/rfc-process.html" class="font-semibold text-dark">
              RFC Process
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">When and how to write an RFC</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M11.42 15.17 17.25 21A2.652 2.652 0 0 0 21 17.25l-5.877-5.877M11.42 15.17l2.496-3.03c.317-.384.74-.626 1.208-.766M11.42 15.17l-4.655 5.653a2.548 2.548 0 1 1-3.586-3.586l6.837-5.63m5.108-.233c.55-.164 1.163-.188 1.743-.14a4.5 4.5 0 0 0 4.486-6.336l-3.276 3.277a3.004 3.004 0 0 1-2.25-2.25l3.276-3.276a4.5 4.5 0 0 0-6.336 4.486c.091 1.076-.071 2.264-.904 2.95l-.102.085m-1.745 1.437L5.909 7.5H4.5L2.25 3.75l1.5-1.5L7.5 4.5v1.409l4.26 4.26m-1.745 1.437 1.745-1.437m6.615 8.206L15.75 15.75M4.867 19.125h.008v.008h-.008v-.008Z"/>
</svg></div>
          <div>
            <a href="/Contribute/platform-ports.html" class="font-semibold text-dark">
              Platform Contributions
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Contributing and maintaining a platform port</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M15 19.128a9.38 9.38 0 0 0 2.625.372 9.337 9.337 0 0 0 4.121-.952 4.125 4.125 0 0 0-7.533-2.493M15 19.128v-.003c0-1.113-.285-2.16-.786-3.07M15 19.128v.106A12.318 12.318 0 0 1 8.624 21c-2.331 0-4.512-.645-6.374-1.766l-.001-.109a6.375 6.375 0 0 1 11.964-3.07M12 6.375a3.375 3.375 0 1 1-6.75 0 3.375 3.375 0 0 1 6.75 0Zm8.25 2.25a2.625 2.625 0 1 1-5.25 0 2.625 2.625 0 0 1 5.25 0Z"/>
</svg></div>
          <div>
            <a href="/Contribute/roles.html" class="font-semibold text-dark">
              Development Roles
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Contributors, Reviewers, Committers, TSC</p>
          </div>
        </div>
      </div></div>
  </div>
</div><div class="relative">
  <a class="dropbtn" aria-expanded="false" href="#">
    <span>News</span>
    <span class="hidden lg:inline"> &amp; Events</span>
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="drop-icon">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
  </a>
  <div class="menu-one-col dropmenu">
    <div class="p-4">
<div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M10.34 15.84c-.688-.06-1.386-.09-2.09-.09H7.5a4.5 4.5 0 1 1 0-9h.75c.704 0 1.402-.03 2.09-.09m0 9.18c.253.962.584 1.892.985 2.783.247.55.06 1.21-.463 1.511l-.657.38c-.551.318-1.26.117-1.527-.461a20.845 20.845 0 0 1-1.44-4.282m3.102.069a18.03 18.03 0 0 1-.59-4.59c0-1.586.205-3.124.59-4.59m0 9.18a23.848 23.848 0 0 1 8.835 2.535M10.34 6.66a23.847 23.847 0 0 0 8.835-2.535m0 0A23.74 23.74 0 0 0 18.795 3m.38 1.125a23.91 23.91 0 0 1 1.014 5.395m-1.014 8.855c-.118.38-.245.754-.38 1.125m.38-1.125a23.91 23.91 0 0 0 1.014-5.395m0-3.46c.495.413.811 1.035.811 1.73 0 .695-.316 1.317-.811 1.73m0-3.46a24.347 24.347 0 0 1 0 3.46"/>
</svg></div>
          <div>
            <a href="/news/" class="font-semibold text-dark">
              News
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">News about seL4 and the seL4 Foundation</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M18 18.72a9.094 9.094 0 0 0 3.741-.479 3 3 0 0 0-4.682-2.72m.94 3.198.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0 1 12 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 0 1 6 18.719m12 0a5.971 5.971 0 0 0-.941-3.197m0 0A5.995 5.995 0 0 0 12 12.75a5.995 5.995 0 0 0-5.058 2.772m0 0a3 3 0 0 0-4.681 2.72 8.986 8.986 0 0 0 3.74.477m.94-3.197a5.971 5.971 0 0 0-.94 3.197M15 6.75a3 3 0 1 1-6 0 3 3 0 0 1 6 0Zm6 3a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Zm-13.5 0a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Z"/>
</svg></div>
          <div>
            <a href="/Summit/2025/" class="font-semibold text-dark">
              seL4 Summit 2025
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">The seL4 event of the year</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M21 16.811c0 .864-.933 1.406-1.683.977l-7.108-4.061a1.125 1.125 0 0 1 0-1.954l7.108-4.061A1.125 1.125 0 0 1 21 8.689v8.122ZM11.25 16.811c0 .864-.933 1.406-1.683.977l-7.108-4.061a1.125 1.125 0 0 1 0-1.954l7.108-4.061a1.125 1.125 0 0 1 1.683.977v8.122Z"/>
</svg></div>
          <div>
            <a href="/Summit/" class="font-semibold text-dark">
              Previous seL4 Summits
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">All seL4 summits since 2022</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M21.75 6.75v10.5a2.25 2.25 0 0 1-2.25 2.25h-15a2.25 2.25 0 0 1-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0 0 19.5 4.5h-15a2.25 2.25 0 0 0-2.25 2.25m19.5 0v.243a2.25 2.25 0 0 1-1.07 1.916l-7.5 4.615a2.25 2.25 0 0 1-2.36 0L3.32 8.91a2.25 2.25 0 0 1-1.07-1.916V6.75"/>
</svg></div>
          <div>
            <a href="/stay-informed.html" class="font-semibold text-dark">
              Stay Informed
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Announcement mailing list and other channels to stay informed</p>
          </div>
        </div></div>
      <div class="bg-gray-50 p-8 dark:bg-slate-900">
        <div class="flex justify-between">
          <h3 class="text-sm font-semibold leading-6 text-gray-500">Recent news</h3>
          <a href="/news/" class="text-sm font-semibold leading-6 text-f_green-600">See all <span aria-hidden="true">&rarr;</span></a>
        </div>
        <ul class="mt-6 space-y-6"><li class="relative">
            <time datetime="2025-08-06" class="block text-xs leading-6 text-light">06 Aug 2025</time>
            <a href="/news/2025.html#sponsor25-riverside" class="block truncate text-sm font-semibold leading-6 text-dark">
              Thank you Riverside Research, sponsor of the seL4 Summit 2025 reception
              <span class="absolute inset-0"></span>
            </a>
          </li><li class="relative">
            <time datetime="2025-08-04" class="block text-xs leading-6 text-light">04 Aug 2025</time>
            <a href="/news/2025.html#member-tccoe" class="block truncate text-sm font-semibold leading-6 text-dark">
              The Trusted Computing Center of Excellence™ joins the seL4 Foundation
              <span class="absolute inset-0"></span>
            </a>
          </li><li class="relative">
            <time datetime="2025-08-01" class="block text-xs leading-6 text-light">01 Aug 2025</time>
            <a href="/news/2025.html#panellists25" class="block truncate text-sm font-semibold leading-6 text-dark">
              Panellists for seL4 Summit announced
              <span class="absolute inset-0"></span>
            </a>
          </li></ul>
      </div>
      <div>
      </div>
  </div>
</div><div class="relative">
  <a class="dropbtn" aria-expanded="false" href="#">
    <span>Contact</span>
    <span class="hidden lg:inline"> &amp; Support</span>
    <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="drop-icon">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
  </a>
  <div class="menu-one-col dropmenu">
    <div class="p-4">
<div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M15.59 14.37a6 6 0 0 1-5.84 7.38v-4.8m5.84-2.58a14.98 14.98 0 0 0 6.16-12.12A14.98 14.98 0 0 0 9.631 8.41m5.96 5.96a14.926 14.926 0 0 1-5.841 2.58m-.119-8.54a6 6 0 0 0-7.381 5.84h4.8m2.581-5.84a14.927 14.927 0 0 0-2.58 5.84m2.699 2.7c-.103.021-.207.041-.311.06a15.09 15.09 0 0 1-2.448-2.448 14.9 14.9 0 0 1 .06-.312m-2.24 2.39a4.493 4.493 0 0 0-1.757 4.306 4.493 4.493 0 0 0 4.306-1.758M16.5 9a1.5 1.5 0 1 1-3 0 1.5 1.5 0 0 1 3 0Z"/>
</svg></div>
          <div>
            <a href="/Services/" class="font-semibold text-dark">
              Commercial Support
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Endorsed Service Providers for seL4</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M16.712 4.33a9.027 9.027 0 0 1 1.652 1.306c.51.51.944 1.064 1.306 1.652M16.712 4.33l-3.448 4.138m3.448-4.138a9.014 9.014 0 0 0-9.424 0M19.67 7.288l-4.138 3.448m4.138-3.448a9.014 9.014 0 0 1 0 9.424m-4.138-5.976a3.736 3.736 0 0 0-.88-1.388 3.737 3.737 0 0 0-1.388-.88m2.268 2.268a3.765 3.765 0 0 1 0 2.528m-2.268-4.796a3.765 3.765 0 0 0-2.528 0m4.796 4.796c-.181.506-.475.982-.88 1.388a3.736 3.736 0 0 1-1.388.88m2.268-2.268 4.138 3.448m0 0a9.027 9.027 0 0 1-1.306 1.652c-.51.51-1.064.944-1.652 1.306m0 0-3.448-4.138m3.448 4.138a9.014 9.014 0 0 1-9.424 0m5.976-4.138a3.765 3.765 0 0 1-2.528 0m0 0a3.736 3.736 0 0 1-1.388-.88 3.737 3.737 0 0 1-.88-1.388m2.268 2.268L7.288 19.67m0 0a9.024 9.024 0 0 1-1.652-1.306 9.027 9.027 0 0 1-1.306-1.652m0 0 4.138-3.448M4.33 16.712a9.014 9.014 0 0 1 0-9.424m4.138 5.976a3.765 3.765 0 0 1 0-2.528m0 0c.181-.506.475-.982.88-1.388a3.736 3.736 0 0 1 1.388-.88m-2.268 2.268L4.33 7.288m6.406 1.18L7.288 4.33m0 0a9.024 9.024 0 0 0-1.652 1.306A9.025 9.025 0 0 0 4.33 7.288"/>
</svg></div>
          <div>
            <a href="/support.html" class="font-semibold text-dark">
              Help &amp; Open Source Support
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">Mailing lists, Forum, Chat, Developer Hangouts</p>
          </div>
        </div><div class="group menu-item-group">
          <div class="menu-icon-div"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="menu-icon">
  <path stroke-linecap="round" stroke-linejoin="round" d="M15.75 6a3.75 3.75 0 1 1-7.5 0 3.75 3.75 0 0 1 7.5 0ZM4.501 20.118a7.5 7.5 0 0 1 14.998 0A17.933 17.933 0 0 1 12 21.75c-2.676 0-5.216-.584-7.499-1.632Z"/>
</svg></div>
          <div>
            <a href="/contact.html" class="font-semibold text-dark">
              Contact a Person
              <span class="absolute inset-0"></span>
            </a>
            <p class="mt-1 text-light">seL4 Foundation, Security Contact, Moderation</p>
          </div>
        </div>
      </div>
  </div>
</div></div>
    <div class="hidden md:flex md:flex-1"></div>

    <div class="hidden min-[470px]:flex md:flex-none justify-end">
      <!-- member button -->
      <a href="/Foundation/Join/" class="button-outline md:h-8 lg:h-9 md:text-xs lg:text-sm mr-6 md:mr-4 lg:mr-8 xl:mr-10">Become a Member</a>

      <!-- docsite button -->
      <a href="https://docs.sel4.systems" class="button-outline md:h-8 lg:h-9 md:text-xs lg:text-sm mr-2 lg:mr-6">Docsite
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" aria-hidden="true" viewBox="0 0 24 24" stroke="currentColor" data-slot="icon" class="inline-icon">
  <path stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" d="M13.5 6H5.25A2.25 2.25 0 0 0 3 8.25v10.5A2.25 2.25 0 0 0 5.25 21h10.5A2.25 2.25 0 0 0 18 18.75V10.5m-10.5 6L21 3m0 0h-5.25M21 3v5.25"/>
</svg>
      </a>

      <!-- github button -->
      <a href="https://github.com/seL4" class="mr-4 md:mr-0 px-3 pt-1 pb-2 block text-gray-400 hover:text-gray-500 dark:hover:text-gray-300">
        <span class="sr-only">seL4 on GitHub</span>
        <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" data-slot="icon" class="w-5 h-5 lg:w-6 lg:h-6">
  <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z"/>
</svg>
      </a>
    </div>

    <!-- mobile menu hamburger button -->
    <div class="flex md:hidden">
      <button type="button" class="-m-2.5 inline-flex items-center justify-center rounded-md p-2.5 text-gray-700 dark:text-gray-300 mobile-drop">
        <span class="sr-only">Open main menu</span>
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-6 w-6">
  <path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6.75h16.5M3.75 12h16.5m-16.5 5.25h16.5"/>
</svg>
      </button>
    </div>
  </nav><div class="lg:hidden hidden" id="mobile-menu" role="dialog" aria-modal="true">
  <div class="fixed inset-0 z-10"></div>
  <div class="fixed inset-y-0 right-0 z-10 w-full overflow-y-auto bg-white dark:bg-darkblue px-6 py-6 sm:max-w-sm sm:ring-1 sm:ring-gray-900/10 dark:sm:ring-gray-100/20">
    <div class="flex items-center justify-between">
      <span class="-m-1.5 p-1.5"> </span>
      <button type="button" class="-m-2.5 rounded-md p-2.5 text-light">
        <span class="sr-only">Close menu</span>
        <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-6 w-6">
  <path stroke-linecap="round" stroke-linejoin="round" d="M6 18 18 6M6 6l12 12"/>
</svg>
      </button>
    </div>
    <div class="mt-6 flow-root">
      <div class="-my-6 divide-y divide-gray-500/10 dark:divide-gray-200/40">
        <div class="space-y-2 py-6">
          <div class="-mx-3"><button type="button" class="dropbtn-mobile" aria-expanded="false">
              About seL4
              <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="h-5 w-5 flex-none">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
            </button>
            <div class="mobile-menu hidden"><a href="/About/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 18v-5.25m0 0a6.01 6.01 0 0 0 1.5-.189m-1.5.189a6.01 6.01 0 0 1-1.5-.189m3.75 7.478a12.06 12.06 0 0 1-4.5 0m3.75 2.383a14.406 14.406 0 0 1-3 0M14.25 18v-.192c0-.983.658-1.823 1.508-2.316a7.5 7.5 0 1 0-7.517 0c.85.493 1.509 1.333 1.509 2.316V18"/>
</svg> What is seL4?</a><a href="/use.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M18 18.72a9.094 9.094 0 0 0 3.741-.479 3 3 0 0 0-4.682-2.72m.94 3.198.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0 1 12 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 0 1 6 18.719m12 0a5.971 5.971 0 0 0-.941-3.197m0 0A5.995 5.995 0 0 0 12 12.75a5.995 5.995 0 0 0-5.058 2.772m0 0a3 3 0 0 0-4.681 2.72 8.986 8.986 0 0 0 3.74.477m.94-3.197a5.971 5.971 0 0 0-.94 3.197M15 6.75a3 3 0 1 1-6 0 3 3 0 0 1 6 0Zm6 3a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Zm-13.5 0a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Z"/>
</svg> Who is using seL4?</a><a href="/Verification/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75 11.25 15 15 9.75M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Z"/>
</svg> Verification</a><a href="/roadmap.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M2.25 18 9 11.25l4.306 4.306a11.95 11.95 0 0 1 5.814-5.518l2.74-1.22m0 0-5.94-2.281m5.94 2.28-2.28 5.941"/>
</svg> Roadmap</a><a href="/performance.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="m3.75 13.5 10.5-11.25L12 10.5h8.25L9.75 21.75 12 13.5H3.75Z"/>
</svg> Performance</a><a href="/Legal/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 3v17.25m0 0c-1.472 0-2.882.265-4.185.75M12 20.25c1.472 0 2.882.265 4.185.75M18.75 4.97A48.416 48.416 0 0 0 12 4.5c-2.291 0-4.545.16-6.75.47m13.5 0c1.01.143 2.01.317 3 .52m-3-.52 2.62 10.726c.122.499-.106 1.028-.589 1.202a5.988 5.988 0 0 1-2.031.352 5.988 5.988 0 0 1-2.031-.352c-.483-.174-.711-.703-.59-1.202L18.75 4.971Zm-16.5.52c.99-.203 1.99-.377 3-.52m0 0 2.62 10.726c.122.499-.106 1.028-.589 1.202a5.989 5.989 0 0 1-2.031.352 5.989 5.989 0 0 1-2.031-.352c-.483-.174-.711-.703-.59-1.202L5.25 4.971Z"/>
</svg> License, Trademark &amp; Logo</a><a href="/platforms.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M8.25 3v1.5M4.5 8.25H3m18 0h-1.5M4.5 12H3m18 0h-1.5m-15 3.75H3m18 0h-1.5M8.25 19.5V21M12 3v1.5m0 15V21m3.75-18v1.5m0 15V21m-9-1.5h10.5a2.25 2.25 0 0 0 2.25-2.25V6.75a2.25 2.25 0 0 0-2.25-2.25H6.75A2.25 2.25 0 0 0 4.5 6.75v10.5a2.25 2.25 0 0 0 2.25 2.25Zm.75-12h9v9h-9v-9Z"/>
</svg> Platform Support Overview</a><a href="/Research/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M4.26 10.147a60.438 60.438 0 0 0-.491 6.347A48.62 48.62 0 0 1 12 20.904a48.62 48.62 0 0 1 8.232-4.41 60.46 60.46 0 0 0-.491-6.347m-15.482 0a50.636 50.636 0 0 0-2.658-.813A59.906 59.906 0 0 1 12 3.493a59.903 59.903 0 0 1 10.399 5.84c-.896.248-1.783.52-2.658.814m-15.482 0A50.717 50.717 0 0 1 12 13.489a50.702 50.702 0 0 1 7.74-3.342M6.75 15a.75.75 0 1 0 0-1.5.75.75 0 0 0 0 1.5Zm0 0v-3.675A55.378 55.378 0 0 1 12 8.443m-7.007 11.55A5.981 5.981 0 0 0 6.75 15.75v-1.5"/>
</svg> Research</a><a href="/tools.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M13.5 16.875h3.375m0 0h3.375m-3.375 0V13.5m0 3.375v3.375M6 10.5h2.25a2.25 2.25 0 0 0 2.25-2.25V6a2.25 2.25 0 0 0-2.25-2.25H6A2.25 2.25 0 0 0 3.75 6v2.25A2.25 2.25 0 0 0 6 10.5Zm0 9.75h2.25A2.25 2.25 0 0 0 10.5 18v-2.25a2.25 2.25 0 0 0-2.25-2.25H6a2.25 2.25 0 0 0-2.25 2.25V18A2.25 2.25 0 0 0 6 20.25Zm9.75-9.75H18a2.25 2.25 0 0 0 2.25-2.25V6A2.25 2.25 0 0 0 18 3.75h-2.25A2.25 2.25 0 0 0 13.5 6v2.25a2.25 2.25 0 0 0 2.25 2.25Z"/>
</svg> Frameworks &amp; Languages</a><a href="/Foundation/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 21v-8.25M15.75 21v-8.25M8.25 21v-8.25M3 9l9-6 9 6m-1.5 12V10.332A48.36 48.36 0 0 0 12 9.75c-2.551 0-5.056.2-7.5.582V21M3 21h18M12 6.75h.008v.008H12V6.75Z"/>
</svg> seL4 Foundation</a><a href="/Learn/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 6.042A8.967 8.967 0 0 0 6 3.75c-1.052 0-2.062.18-3 .512v14.25A8.987 8.987 0 0 1 6 18c2.305 0 4.408.867 6 2.292m0-14.25a8.966 8.966 0 0 1 6-2.292c1.052 0 2.062.18 3 .512v14.25A8.987 8.987 0 0 0 18 18a8.967 8.967 0 0 0-6 2.292m0-14.25v14.25"/>
</svg> Manual &amp; Documentation</a><a href="/About/FAQ.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M9.879 7.519c1.171-1.025 3.071-1.025 4.242 0 1.172 1.025 1.172 2.687 0 3.712-.203.179-.43.326-.67.442-.745.361-1.45.999-1.45 1.827v.75M21 12a9 9 0 1 1-18 0 9 9 0 0 1 18 0Zm-9 5.25h.008v.008H12v-.008Z"/>
</svg> FAQ</a></div><button type="button" class="dropbtn-mobile" aria-expanded="false">
              Contribute
              <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="h-5 w-5 flex-none">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
            </button>
            <div class="mobile-menu hidden"><a href="/Contribute/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M3.75 6A2.25 2.25 0 0 1 6 3.75h2.25A2.25 2.25 0 0 1 10.5 6v2.25a2.25 2.25 0 0 1-2.25 2.25H6a2.25 2.25 0 0 1-2.25-2.25V6ZM3.75 15.75A2.25 2.25 0 0 1 6 13.5h2.25a2.25 2.25 0 0 1 2.25 2.25V18a2.25 2.25 0 0 1-2.25 2.25H6A2.25 2.25 0 0 1 3.75 18v-2.25ZM13.5 6a2.25 2.25 0 0 1 2.25-2.25H18A2.25 2.25 0 0 1 20.25 6v2.25A2.25 2.25 0 0 1 18 10.5h-2.25a2.25 2.25 0 0 1-2.25-2.25V6ZM13.5 15.75a2.25 2.25 0 0 1 2.25-2.25H18a2.25 2.25 0 0 1 2.25 2.25V18A2.25 2.25 0 0 1 18 20.25h-2.25A2.25 2.25 0 0 1 13.5 18v-2.25Z"/>
</svg> Overview</a><a href="/Contribute/guidelines.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M16.023 9.348h4.992v-.001M2.985 19.644v-4.992m0 0h4.992m-4.993 0 3.181 3.183a8.25 8.25 0 0 0 13.803-3.7M4.031 9.865a8.25 8.25 0 0 1 13.803-3.7l3.181 3.182m0-4.991v4.99"/>
</svg> Contribution Guidelines</a><a href="/Contribute/conduct.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M9 12.75 11.25 15 15 9.75m-3-7.036A11.959 11.959 0 0 1 3.598 6 11.99 11.99 0 0 0 3 9.749c0 5.592 3.824 10.29 9 11.623 5.176-1.332 9-6.03 9-11.622 0-1.31-.21-2.571-.598-3.751h-.152c-3.196 0-6.1-1.248-8.25-3.285Z"/>
</svg> Code of Conduct</a><a href="/Contribute/conventions.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="m6.75 7.5 3 2.25-3 2.25m4.5 0h3m-9 8.25h13.5A2.25 2.25 0 0 0 21 18V6a2.25 2.25 0 0 0-2.25-2.25H5.25A2.25 2.25 0 0 0 3 6v12a2.25 2.25 0 0 0 2.25 2.25Z"/>
</svg> Coding Conventions &amp; Requirements</a><a href="/Contribute/rfc-process.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M20.25 8.511c.884.284 1.5 1.128 1.5 2.097v4.286c0 1.136-.847 2.1-1.98 2.193-.34.027-.68.052-1.02.072v3.091l-3-3c-1.354 0-2.694-.055-4.02-.163a2.115 2.115 0 0 1-.825-.242m9.345-8.334a2.126 2.126 0 0 0-.476-.095 48.64 48.64 0 0 0-8.048 0c-1.131.094-1.976 1.057-1.976 2.192v4.286c0 .837.46 1.58 1.155 1.951m9.345-8.334V6.637c0-1.621-1.152-3.026-2.76-3.235A48.455 48.455 0 0 0 11.25 3c-2.115 0-4.198.137-6.24.402-1.608.209-2.76 1.614-2.76 3.235v6.226c0 1.621 1.152 3.026 2.76 3.235.577.075 1.157.14 1.74.194V21l4.155-4.155"/>
</svg> RFC Process</a><a href="/Contribute/platform-ports.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M11.42 15.17 17.25 21A2.652 2.652 0 0 0 21 17.25l-5.877-5.877M11.42 15.17l2.496-3.03c.317-.384.74-.626 1.208-.766M11.42 15.17l-4.655 5.653a2.548 2.548 0 1 1-3.586-3.586l6.837-5.63m5.108-.233c.55-.164 1.163-.188 1.743-.14a4.5 4.5 0 0 0 4.486-6.336l-3.276 3.277a3.004 3.004 0 0 1-2.25-2.25l3.276-3.276a4.5 4.5 0 0 0-6.336 4.486c.091 1.076-.071 2.264-.904 2.95l-.102.085m-1.745 1.437L5.909 7.5H4.5L2.25 3.75l1.5-1.5L7.5 4.5v1.409l4.26 4.26m-1.745 1.437 1.745-1.437m6.615 8.206L15.75 15.75M4.867 19.125h.008v.008h-.008v-.008Z"/>
</svg> Platform Contributions</a><a href="/Contribute/roles.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M15 19.128a9.38 9.38 0 0 0 2.625.372 9.337 9.337 0 0 0 4.121-.952 4.125 4.125 0 0 0-7.533-2.493M15 19.128v-.003c0-1.113-.285-2.16-.786-3.07M15 19.128v.106A12.318 12.318 0 0 1 8.624 21c-2.331 0-4.512-.645-6.374-1.766l-.001-.109a6.375 6.375 0 0 1 11.964-3.07M12 6.375a3.375 3.375 0 1 1-6.75 0 3.375 3.375 0 0 1 6.75 0Zm8.25 2.25a2.625 2.625 0 1 1-5.25 0 2.625 2.625 0 0 1 5.25 0Z"/>
</svg> Development Roles</a></div><button type="button" class="dropbtn-mobile" aria-expanded="false">
              News &amp; Events
              <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="h-5 w-5 flex-none">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
            </button>
            <div class="mobile-menu hidden"><a href="/news/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M10.34 15.84c-.688-.06-1.386-.09-2.09-.09H7.5a4.5 4.5 0 1 1 0-9h.75c.704 0 1.402-.03 2.09-.09m0 9.18c.253.962.584 1.892.985 2.783.247.55.06 1.21-.463 1.511l-.657.38c-.551.318-1.26.117-1.527-.461a20.845 20.845 0 0 1-1.44-4.282m3.102.069a18.03 18.03 0 0 1-.59-4.59c0-1.586.205-3.124.59-4.59m0 9.18a23.848 23.848 0 0 1 8.835 2.535M10.34 6.66a23.847 23.847 0 0 0 8.835-2.535m0 0A23.74 23.74 0 0 0 18.795 3m.38 1.125a23.91 23.91 0 0 1 1.014 5.395m-1.014 8.855c-.118.38-.245.754-.38 1.125m.38-1.125a23.91 23.91 0 0 0 1.014-5.395m0-3.46c.495.413.811 1.035.811 1.73 0 .695-.316 1.317-.811 1.73m0-3.46a24.347 24.347 0 0 1 0 3.46"/>
</svg> News</a><a href="/Summit/2025/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M18 18.72a9.094 9.094 0 0 0 3.741-.479 3 3 0 0 0-4.682-2.72m.94 3.198.001.031c0 .225-.012.447-.037.666A11.944 11.944 0 0 1 12 21c-2.17 0-4.207-.576-5.963-1.584A6.062 6.062 0 0 1 6 18.719m12 0a5.971 5.971 0 0 0-.941-3.197m0 0A5.995 5.995 0 0 0 12 12.75a5.995 5.995 0 0 0-5.058 2.772m0 0a3 3 0 0 0-4.681 2.72 8.986 8.986 0 0 0 3.74.477m.94-3.197a5.971 5.971 0 0 0-.94 3.197M15 6.75a3 3 0 1 1-6 0 3 3 0 0 1 6 0Zm6 3a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Zm-13.5 0a2.25 2.25 0 1 1-4.5 0 2.25 2.25 0 0 1 4.5 0Z"/>
</svg> seL4 Summit 2025</a><a href="/Summit/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M21 16.811c0 .864-.933 1.406-1.683.977l-7.108-4.061a1.125 1.125 0 0 1 0-1.954l7.108-4.061A1.125 1.125 0 0 1 21 8.689v8.122ZM11.25 16.811c0 .864-.933 1.406-1.683.977l-7.108-4.061a1.125 1.125 0 0 1 0-1.954l7.108-4.061a1.125 1.125 0 0 1 1.683.977v8.122Z"/>
</svg> Previous seL4 Summits</a><a href="/stay-informed.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M21.75 6.75v10.5a2.25 2.25 0 0 1-2.25 2.25h-15a2.25 2.25 0 0 1-2.25-2.25V6.75m19.5 0A2.25 2.25 0 0 0 19.5 4.5h-15a2.25 2.25 0 0 0-2.25 2.25m19.5 0v.243a2.25 2.25 0 0 1-1.07 1.916l-7.5 4.615a2.25 2.25 0 0 1-2.36 0L3.32 8.91a2.25 2.25 0 0 1-1.07-1.916V6.75"/>
</svg> Stay Informed</a></div><button type="button" class="dropbtn-mobile" aria-expanded="false">
              Contact &amp; Support
              <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 20 20" fill="currentColor" aria-hidden="true" data-slot="icon" class="h-5 w-5 flex-none">
  <path fill-rule="evenodd" d="M5.22 8.22a.75.75 0 0 1 1.06 0L10 11.94l3.72-3.72a.75.75 0 1 1 1.06 1.06l-4.25 4.25a.75.75 0 0 1-1.06 0L5.22 9.28a.75.75 0 0 1 0-1.06Z" clip-rule="evenodd"/>
</svg>
            </button>
            <div class="mobile-menu hidden"><a href="/Services/" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M15.59 14.37a6 6 0 0 1-5.84 7.38v-4.8m5.84-2.58a14.98 14.98 0 0 0 6.16-12.12A14.98 14.98 0 0 0 9.631 8.41m5.96 5.96a14.926 14.926 0 0 1-5.841 2.58m-.119-8.54a6 6 0 0 0-7.381 5.84h4.8m2.581-5.84a14.927 14.927 0 0 0-2.58 5.84m2.699 2.7c-.103.021-.207.041-.311.06a15.09 15.09 0 0 1-2.448-2.448 14.9 14.9 0 0 1 .06-.312m-2.24 2.39a4.493 4.493 0 0 0-1.757 4.306 4.493 4.493 0 0 0 4.306-1.758M16.5 9a1.5 1.5 0 1 1-3 0 1.5 1.5 0 0 1 3 0Z"/>
</svg> Commercial Support</a><a href="/support.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M16.712 4.33a9.027 9.027 0 0 1 1.652 1.306c.51.51.944 1.064 1.306 1.652M16.712 4.33l-3.448 4.138m3.448-4.138a9.014 9.014 0 0 0-9.424 0M19.67 7.288l-4.138 3.448m4.138-3.448a9.014 9.014 0 0 1 0 9.424m-4.138-5.976a3.736 3.736 0 0 0-.88-1.388 3.737 3.737 0 0 0-1.388-.88m2.268 2.268a3.765 3.765 0 0 1 0 2.528m-2.268-4.796a3.765 3.765 0 0 0-2.528 0m4.796 4.796c-.181.506-.475.982-.88 1.388a3.736 3.736 0 0 1-1.388.88m2.268-2.268 4.138 3.448m0 0a9.027 9.027 0 0 1-1.306 1.652c-.51.51-1.064.944-1.652 1.306m0 0-3.448-4.138m3.448 4.138a9.014 9.014 0 0 1-9.424 0m5.976-4.138a3.765 3.765 0 0 1-2.528 0m0 0a3.736 3.736 0 0 1-1.388-.88 3.737 3.737 0 0 1-.88-1.388m2.268 2.268L7.288 19.67m0 0a9.024 9.024 0 0 1-1.652-1.306 9.027 9.027 0 0 1-1.306-1.652m0 0 4.138-3.448M4.33 16.712a9.014 9.014 0 0 1 0-9.424m4.138 5.976a3.765 3.765 0 0 1 0-2.528m0 0c.181-.506.475-.982.88-1.388a3.736 3.736 0 0 1 1.388-.88m-2.268 2.268L4.33 7.288m6.406 1.18L7.288 4.33m0 0a9.024 9.024 0 0 0-1.652 1.306A9.025 9.025 0 0 0 4.33 7.288"/>
</svg> Help &amp; Open Source Support</a><a href="/contact.html" class="block rounded-lg py-2 pl-6 pr-3 text-sm font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="inline-icon mr-2">
  <path stroke-linecap="round" stroke-linejoin="round" d="M15.75 6a3.75 3.75 0 1 1-7.5 0 3.75 3.75 0 0 1 7.5 0ZM4.501 20.118a7.5 7.5 0 0 1 14.998 0A17.933 17.933 0 0 1 12 21.75c-2.676 0-5.216-.584-7.499-1.632Z"/>
</svg> Contact a Person</a></div></div>
        </div>
        <div class="py-6">
          <a href="/Foundation/Join" class="-mx-3 block rounded-lg px-3 py-2.5 text-base font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800">Become a Member</a>
          <a href="https://docs.sel4.systems" class="-mx-3 block rounded-lg px-3 py-2.5 text-base font-semibold leading-7 text-dark hover:bg-gray-50 dark:hover:bg-slate-800">Docsite <svg xmlns="http://www.w3.org/2000/svg" fill="none" aria-hidden="true" viewBox="0 0 24 24" stroke="currentColor" data-slot="icon" class="ml-1 inline-icon">
  <path stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" d="M13.5 6H5.25A2.25 2.25 0 0 0 3 8.25v10.5A2.25 2.25 0 0 0 5.25 21h10.5A2.25 2.25 0 0 0 18 18.75V10.5m-10.5 6L21 3m0 0h-5.25M21 3v5.25"/>
</svg></a>
        </div>
      </div>
    </div>
  </div>
 </div>
</header>
    <div class="flex-1 overflow-y-auto">
      <main id="page-top">
<div class="w-full bg-darkblue py-6">
  <div class="text-[#74ba6e] mx-4 text-center font-semibold text-base sm:text-xl lg:text-2xl">
    The world's most highly assured and fastest operating system kernel
  </div>
</div>
<div class="bg-darkblue">
  <div id="the_div" class="relative w-full h-[18rem] sm:h-[22rem] overflow-hidden z-0">
    <div id="callgraph" class="absolute h-[35rem] w-full pointer-events-none sm:pointer-events-auto sm:cursor-zoom-in "></div>

    <img src="images/seL4-short.svg" alt="seL4 logo"
         class="absolute w-[300px] sm:w-[400px] top-20 md:top-24 left-[calc(50%-150px)] sm:left-[calc(50%-200px)] opacity-60 cursor-default">
    <img src="images/seL4-short.svg" alt="hidden seL4 logo" aria-hidden="true"
         class="absolute w-[300px] sm:w-[400px] top-20 md:top-24 left-[calc(50%-150px)] sm:left-[calc(50%-200px)] opacity-60 cursor-default [mix-blend-mode:_color-dodge]">
  </div>
</div>

<div class="w-full bg-darkblue py-6">
  <div class="grid grid-cols-1 sm:grid-cols-2 mt-4 mb-3 gap-8 px-4 mx-auto w-2/3 sm:w-[30rem] md:w-[40rem] place-items-center">
    <a href="About/" class="button-on-dark text-sm-base h-10 w-full sm:w-52 md:w-60">Learn more about seL4</a>
    <a href="https://docs.sel4.systems" class="button-on-dark text-sm-base h-10 w-full sm:w-52 md:w-60">Start developing</a>
  </div>
</div>

<div class="lg:px-6">




<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/Research/">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/award.jpeg" alt="ACM SIGOPS award plaque" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-10 lg:grid-cols-[60%_40%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
               ">
      Award-winning technology backed by ground-breaking research
    </h2>
    <div></div>
    <div class="">

    <p>
      seL4 is both the world's most highly assured and the world's fastest
      operating system kernel. Its uniqueness lies in the formal mathematical
      proof that it behaves exactly as specified, enforcing strong security
      boundaries for applications running on top of it while maintaining the
      high performance that deployed systems need.
    </p>
    <p>
      seL4 is grounded in research breakthroughs across multiple science
      disciplines. These breakthroughs have been recognised by international
      acclaimed awards, from the MIT Technology Review Award, to the ACM Hall
      of Fame Award, the ACM Software Systems Award, the DARPA Game changer
      award, and more.
    </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/Research/" class="button">Learn more</a>
        <div class="grow"> </div>

      </div>
    </div>
    <div class="mt-12 lg:mt-0  hidden lg:block">
      <a href="/Research/">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/award.jpeg" alt="ACM SIGOPS award plaque" width="400">
      </a>
    </div>
  </div>
</div>





<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/use.html">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/cpu.jpg" alt="Conceptualised image of a CPU on a motherboard" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-14 lg:grid-cols-[40%_60%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
                order-2 ">
      Protecting critical systems around the globe
    </h2>
    <div></div>
    <div class=" order-4 ">

      <p>
        seL4 protects critical systems from software failures and
        cyber-attacks. It allows non-critical functionality to run securely
        alongside critical payloads by enforcing strong isolation and controlled
        communication.
      </p>
      <p>
        seL4 is used in a wide range of critical sectors, from automotive,
        aerospace and IoT to data distribution, military and intelligence. It
        has been successfully retrofitted into complex critical systems and has
        demonstrably prevented cyber-attacks. Government organisations on
        several continents have funded further development of seL4 and its
        ecosystem.
      </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/use.html" class="button">Learn more</a>
        <div class="grow"> </div>

      </div>
    </div>
    <div class="mt-12 lg:mt-0  order-3  hidden lg:block">
      <a href="/use.html">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/cpu.jpg" alt="Conceptualised image of a CPU on a motherboard" width="400">
      </a>
    </div>
  </div>
</div>





<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/Services/">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/providers.jpg" alt="Image of a team at a table shaking hands" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-10 lg:grid-cols-[60%_40%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
               ">
      Supported by commercial service providers
    </h2>
    <div></div>
    <div class="">

      <p>
        seL4 is the leading choice for building highly reliable
        software. Commercial support is available to help you build or migrate
        your product to run on seL4 and benefit from its unparalleled security.
      </p>
      <p>
        A number of Trusted Service Providers have been endorsed by the seL4
        Foundation for their expertise and experience in systems and/or formal
        verification at various levels: kernel, kernel platform ports,
        user-level Operating Systems components, and applications.
      </p>

      <div class="mt-10 flex items-center gap-x-6">
        <a href="/Services/" class="button">Learn more</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  hidden lg:block">
      <a href="/Services/">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/providers.jpg" alt="Image of a team at a table shaking hands" width="400">
      </a>
    </div>
  </div>
</div>





<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/Foundation/">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/foundation-logo-3d.png" alt="seL4 Foundation logo in 3D" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-14 lg:grid-cols-[40%_60%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
                order-2 ">
      Backed by an Open Source Foundation
    </h2>
    <div></div>
    <div class=" order-4 ">

      <p>
        seL4 is open source, supported by the seL4 Foundation, an open,
        transparent and neutral organisation. The seL4 Foundation&apos;s goal is
        to ensure that seL4 continues to be the most highly-assured
        operating-system technology, readily deployable with a diverse and
        stable ecosystem of supporting services and products.
      </p>
      <p>
        seL4 is free to use; its maintenance and development cost are
	funded by the seL4 Foundation memberships.
      </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/Foundation/" class="button">Learn more</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  order-3  hidden lg:block">
      <a href="/Foundation/">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/foundation-logo-3d.png" alt="seL4 Foundation logo in 3D" width="400">
      </a>
    </div>
  </div>
</div>





<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/Contribute/">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/world.jpg" alt="image representing the earth surrounded with telecommunication connections" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-10 lg:grid-cols-[60%_40%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
               ">
      Contributions from a strong ecosystem
    </h2>
    <div></div>
    <div class="">

      <p>
        seL4 and its related technologies receive contributions from developers
        around the world.
      </p>
      <p>
        The microkernel code itself evolves through a tightly controlled
        process, safeguarded by the Foundation's Technical bodies, to preserve
        its security, high assurance, and mathematical proofs.
      </p>
      <p>
        Frameworks, tools and components that run on top of seL4 can use seL4&apos;s
        formally verified protection mechanisms and are therefore easier to
        assess for correctness. This means they can evolve more rapidly and
        accept community contributions at a higher pace, increasing the ease of
        adoption of seL4.
      </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/Contribute/" class="button">Learn more</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  hidden lg:block">
      <a href="/Contribute/">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/world.jpg" alt="image representing the earth surrounded with telecommunication connections" width="400">
      </a>
    </div>
  </div>
</div>





<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/Summit/2025/index.html">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/summit-talk-gernot-with-audience.jpg" alt="Gernot Heiser presenting at the seL4 Summit 2022" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-14 lg:grid-cols-[40%_60%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
                order-2 ">
      Annual gathering at the seL4 Summit
    </h2>
    <div></div>
    <div class=" order-4 ">

      <p>
        The seL4 Summit is the annual international conference on the seL4
        microkernel and all seL4-related technology, tools, infrastructure,
        products, projects, and people.
      </p>
      <p>
        It brings together the entire seL4 community to learn about the
        seL4 technology, its latest advances, uses, successes, challenges and
        plans. The event showcases exciting seL4 development, research, real
        world applications and experiences, offering an opportunity to connect
        with other seL4 developers, users, providers, customers, supporters,
        potential partners and enthusiasts.
      </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/Summit/2025/index.html" class="button">Learn more</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  order-3  hidden lg:block">
      <a href="/Summit/2025/index.html">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/summit-talk-gernot-with-audience.jpg" alt="Gernot Heiser presenting at the seL4 Summit 2022" width="400">
      </a>
    </div>
  </div>
</div>





<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/roadmap.html">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/roadmap.jpg" alt="Roadmap markers on an abstract path with a blue background" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-10 lg:grid-cols-[60%_40%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
               ">
      Development Roadmap
    </h2>
    <div></div>
    <div class="">

      <p>
        With an active, public development roadmap, seL4 continues to solidify its
        position as the leading secure operating system and the industry
        standard for verified software. Evolution drives every level of the
        seL4 ecosystem:
      </p>
      <p>
        seL4 itself is expanding its support for an increasing range
        of platforms, architectures, configurations and features.
      </p>
      <p>
        The ecosystem is expanding with the development of frameworks,
        tools, components and language support to facilitate the production of
        seL4-based systems.
      </p>
      <p>
        The formal proofs, which make seL4 unique, evolve alongside seL4.
        They are constantly maintained, improved, and kept in
        lock-step with the seL4 code.
      </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/roadmap.html" class="button">Learn more</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  hidden lg:block">
      <a href="/roadmap.html">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/roadmap.jpg" alt="Roadmap markers on an abstract path with a blue background" width="400">
      </a>
    </div>
  </div>
</div>







<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/Learn/index.html">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/documentation.jpg" alt="concept image of learning material on a computer screen" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-14 lg:grid-cols-[40%_60%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
                order-2 ">
      Documentation and learning material
    </h2>
    <div></div>
    <div class=" order-4 ">

      <p>
        Eager to learn how to use and build on seL4 or its related frameworks
	and tools like Microkit, CAmkES, and Rust language support?
      </p>
      <p>
	Explore the wide range of learning material for seL4, from hands-on
	tutorials and comprehensive documentation to research articles and
	university courses.
      </p>
      <div class="mt-10 flex items-center gap-x-6">
        <a href="/Learn/index.html" class="button">Get started</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  order-3  hidden lg:block">
      <a href="/Learn/index.html">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/documentation.jpg" alt="concept image of learning material on a computer screen" width="400">
      </a>
    </div>
  </div>
</div>








<div class="py-16 sm:py-20 mx-auto max-w-7xl px-6 sm:px-10 lg:px-8">
  <div class="mb-12 lg:hidden">
    <a href="/news/">
      <img class="rounded-xl shadow-xl mx-auto "
           src="/images/newspaper.jpg" alt="a stack of newspapers" width="400">
    </a>
  </div>
  <div class="text-lg leading-8 text-light mt-6 lg:mr-10 lg:grid lg:grid-rows-[auto_auto]

              lg:gap-x-10 lg:grid-cols-[60%_40%]
              ">
    <h2 class="mb-8 text-3xl lg:text-4xl font-bold tracking-tight text-dark
               ">
      Latest News
    </h2>
    <div></div>
    <div class="">

<div class="flow-root">
  <ul class="-mb-8">
<li>
      <div class="relative pb-8">

        <span class="absolute left-6 top-5 -ml-px h-full w-0.5 bg-slate-200 dark:bg-slate-600"
          aria-hidden="true"></span>

        <div class="relative flex items-start space-x-3">
          <div class="relative">
            <a href="/news/#sponsor25-riverside">
              <div class="-mt-3 p-2 bg-slate-200 dark:bg-slate-600 rounded-full"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="block h-8 w-8">
  <path stroke-linecap="round" stroke-linejoin="round" d="M10.34 15.84c-.688-.06-1.386-.09-2.09-.09H7.5a4.5 4.5 0 1 1 0-9h.75c.704 0 1.402-.03 2.09-.09m0 9.18c.253.962.584 1.892.985 2.783.247.55.06 1.21-.463 1.511l-.657.38c-.551.318-1.26.117-1.527-.461a20.845 20.845 0 0 1-1.44-4.282m3.102.069a18.03 18.03 0 0 1-.59-4.59c0-1.586.205-3.124.59-4.59m0 9.18a23.848 23.848 0 0 1 8.835 2.535M10.34 6.66a23.847 23.847 0 0 0 8.835-2.535m0 0A23.74 23.74 0 0 0 18.795 3m.38 1.125a23.91 23.91 0 0 1 1.014 5.395m-1.014 8.855c-.118.38-.245.754-.38 1.125m.38-1.125a23.91 23.91 0 0 0 1.014-5.395m0-3.46c.495.413.811 1.035.811 1.73 0 .695-.316 1.317-.811 1.73m0-3.46a24.347 24.347 0 0 1 0 3.46"/>
</svg></div>
            </a>
          </div>
          <div class="min-w-0 flex-1">
            <div>
              <div class="text-sm">
                <a href="/news/#sponsor25-riverside" class="font-medium text-dark hover:underline">Thank you Riverside Research, sponsor of the seL4 Summit 2025 reception&nbsp;&rightarrow;</a>
              </div>
              <p class="mt-0.5 text-sm text-gray-500 dark:text-gray-600"> 6 Aug 2025</p>
            </div>
            <div class="mt-2 text-sm text-gray-700 dark:text-gray-500 line-clamp-2">
              <p>
  The seL4 Foundation thanks <a href="https://www.riversideresearch.org/">Riverside Research</a>
  for sponsoring the <a href="/Summit/2025/">seL4
  Summit 2025</a> reception.
</p>
<p>
  Riverside Research <a href="https://www.riversideresearch.org/insights/riverside-research-acquires-cog-systems-to-strengthen-cyber-resilience-and-secure-virtualization-capabilities">recently acquired</a> Cog Systems, a <a href="/Foundation/Membership">Founding Member</a> of the <a href="/Foundation">seL4 Foundation</a>.
</p>
<p>
  Riverside Research is a national security nonprofit serving the DOD and Intelligence Community. Through the company's Open Innovation Center (OIC), it invests in multidisciplinary research and development and encourages collaboration. Riverside Research's areas of expertise include Object and Activity Detection; Accelerated AI/ML; Zero Trust; Open Architectures; Computational Electromagnetics; Plasma Physics; Precision Timing; Terahertz Imaging; Commercial Intelligence, Surveillance, and Reconnaissance (ISR); Collection Planning; and more. Learn more at <a href="https://www.riversideresearch.org">www.riversideresearch.org</a>.
</p>
<p>
  See <a href="https://events.linuxfoundation.org/sel4-summit/sponsor/">here</a>
  if you are interested in sponsoring the  <a href="/Summit/2025/">seL4 Summit
    2025</a>.
</p>

            </div>
          </div>
        </div>
      </div>
    </li><li>
      <div class="relative pb-8">

        <span class="absolute left-6 top-5 -ml-px h-full w-0.5 bg-slate-200 dark:bg-slate-600"
          aria-hidden="true"></span>

        <div class="relative flex items-start space-x-3">
          <div class="relative">
            <a href="/news/#member-tccoe">
              <div class="-mt-3 p-2 bg-slate-200 dark:bg-slate-600 rounded-full"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="block h-8 w-8">
  <path stroke-linecap="round" stroke-linejoin="round" d="M10.34 15.84c-.688-.06-1.386-.09-2.09-.09H7.5a4.5 4.5 0 1 1 0-9h.75c.704 0 1.402-.03 2.09-.09m0 9.18c.253.962.584 1.892.985 2.783.247.55.06 1.21-.463 1.511l-.657.38c-.551.318-1.26.117-1.527-.461a20.845 20.845 0 0 1-1.44-4.282m3.102.069a18.03 18.03 0 0 1-.59-4.59c0-1.586.205-3.124.59-4.59m0 9.18a23.848 23.848 0 0 1 8.835 2.535M10.34 6.66a23.847 23.847 0 0 0 8.835-2.535m0 0A23.74 23.74 0 0 0 18.795 3m.38 1.125a23.91 23.91 0 0 1 1.014 5.395m-1.014 8.855c-.118.38-.245.754-.38 1.125m.38-1.125a23.91 23.91 0 0 0 1.014-5.395m0-3.46c.495.413.811 1.035.811 1.73 0 .695-.316 1.317-.811 1.73m0-3.46a24.347 24.347 0 0 1 0 3.46"/>
</svg></div>
            </a>
          </div>
          <div class="min-w-0 flex-1">
            <div>
              <div class="text-sm">
                <a href="/news/#member-tccoe" class="font-medium text-dark hover:underline">The Trusted Computing Center of Excellence™ joins the seL4 Foundation&nbsp;&rightarrow;</a>
              </div>
              <p class="mt-0.5 text-sm text-gray-500 dark:text-gray-600"> 4 Aug 2025</p>
            </div>
            <div class="mt-2 text-sm text-gray-700 dark:text-gray-500 line-clamp-2">
              <p>
    The seL4 Foundation is pleased to welcome the <a
    href="https://trustedcomputingcoe.org/">Trusted Computing Center of Excellence (TCCoE)</a> as
    <a href="/Foundation/Membership">Associate
    Member</a>.
</p>
<p>
    The Trusted Computing Center of Excellence™ (TCCoE) not-for-profit organization’s purpose is to lower barriers to adoption and facilitate the principled development and deployment of software and systems for which there is strong evidence of trustworthiness, including use of formal methods. Needs of the U.S. and allied defense, intelligence and security communities are our priorities. A major thrust is seL4®. While our colleagues at the seL4 Foundation focus on the code and formal proofs of the open source seL4® microkernel and closely related artifacts, the TCCoE focuses on curated software distributions (kernel, libraries, drivers, configurations, etc.) as bases of stable supported platforms on which to build trustworthy systems.
</p>

            </div>
          </div>
        </div>
      </div>
    </li><li>
      <div class="relative pb-8">

        <div class="relative flex items-start space-x-3">
          <div class="relative">
            <a href="/news/#panellists25">
              <div class="-mt-3 p-2 bg-slate-200 dark:bg-slate-600 rounded-full"><svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="block h-8 w-8">
  <path stroke-linecap="round" stroke-linejoin="round" d="M10.34 15.84c-.688-.06-1.386-.09-2.09-.09H7.5a4.5 4.5 0 1 1 0-9h.75c.704 0 1.402-.03 2.09-.09m0 9.18c.253.962.584 1.892.985 2.783.247.55.06 1.21-.463 1.511l-.657.38c-.551.318-1.26.117-1.527-.461a20.845 20.845 0 0 1-1.44-4.282m3.102.069a18.03 18.03 0 0 1-.59-4.59c0-1.586.205-3.124.59-4.59m0 9.18a23.848 23.848 0 0 1 8.835 2.535M10.34 6.66a23.847 23.847 0 0 0 8.835-2.535m0 0A23.74 23.74 0 0 0 18.795 3m.38 1.125a23.91 23.91 0 0 1 1.014 5.395m-1.014 8.855c-.118.38-.245.754-.38 1.125m.38-1.125a23.91 23.91 0 0 0 1.014-5.395m0-3.46c.495.413.811 1.035.811 1.73 0 .695-.316 1.317-.811 1.73m0-3.46a24.347 24.347 0 0 1 0 3.46"/>
</svg></div>
            </a>
          </div>
          <div class="min-w-0 flex-1">
            <div>
              <div class="text-sm">
                <a href="/news/#panellists25" class="font-medium text-dark hover:underline">Panellists for seL4 Summit announced&nbsp;&rightarrow;</a>
              </div>
              <p class="mt-0.5 text-sm text-gray-500 dark:text-gray-600"> 1 Aug 2025</p>
            </div>
            <div class="mt-2 text-sm text-gray-700 dark:text-gray-500 line-clamp-2">
              <p>
  We are very fortunate to welcome five industry leaders to
  participate at the <a href="/Summit/2025/">seL4 Summit
  2025</a>, in a session <a href="/Summit/2025/abstracts2025.html#a-panel">Building a business case for using a verified kernel</a>:
  Collins Aerospace, DornerWorks, Kry10, NIO, and MEP. The
  panel will be moderated by Juliana Furgala from MIT Lincoln Laboratory.
</p>

<div class="not-prose">
<ul class="mx-auto mt-20 grid max-w-2xl lg:mx-0 lg:max-w-none
           grid-cols-2 gap-x-8 gap-y-16 text-center
           sm:grid-cols-3 md:grid-cols-4

           ">


<li>

  <a href="/Summit/2025/abstracts2025.html#a-panel-collins" class="text-dark no-underline! hover:underline!">

    <img class="mx-auto h-24 w-24 rounded-full" src="/images/summit/dcofer-2022-2-small.jpg" alt="Picture of Darren Cofer">
    <p class="mt-6 text-base font-semibold leading-7 tracking-tight text-dark">Darren Cofer</p>

  </a>

  <p class="text-sm leading-6 text-light"><a href='https://www.collinsaerospace.com'>Collins Aerospace</a></p>
</li>


<li>

  <a href="/Summit/2025/abstracts2025.html#a-panel-dornerworks" class="text-dark no-underline! hover:underline!">

    <img class="mx-auto h-24 w-24 rounded-full" src="/images/summit/robbie.png" alt="Picture of Robbie VanVossen">
    <p class="mt-6 text-base font-semibold leading-7 tracking-tight text-dark">Robbie VanVossen</p>

  </a>

  <p class="text-sm leading-6 text-light"><a href='https://www.dornerworks.com/'>DornerWorks</a></p>
</li>


<li>

  <a href="/Summit/2025/abstracts2025.html#a-panel-kry10" class="text-dark no-underline! hover:underline!">

    <img class="mx-auto h-24 w-24 rounded-full" src="/images/summit/boyd.jpg" alt="Picture of Boyd Multerer">
    <p class="mt-6 text-base font-semibold leading-7 tracking-tight text-dark">Boyd Multerer</p>

  </a>

  <p class="text-sm leading-6 text-light"><a href='https://www.kry10.com/'>Kry10</a></p>
</li>


<li>

  <a href="/Summit/2025/abstracts2025.html#a-panel-mep" class="text-dark no-underline! hover:underline!">

    <img class="mx-auto h-24 w-24 rounded-full" src="/images/summit/peter-square.jpg" alt="Picture of Peter de Ridder">
    <p class="mt-6 text-base font-semibold leading-7 tracking-tight text-dark">Peter de Ridder</p>

  </a>

  <p class="text-sm leading-6 text-light"><a href='https://www.mep-info.com/'>MEP</a></p>
</li>


<li>

  <a href="/Summit/2025/abstracts2025.html#a-panel-nio" class="text-dark no-underline! hover:underline!">

    <img class="mx-auto h-24 w-24 rounded-full" src="/images/summit/yanyan-square.jpg" alt="Picture of Yanyan Shen">
    <p class="mt-6 text-base font-semibold leading-7 tracking-tight text-dark">Yanyan Shen</p>

  </a>

  <p class="text-sm leading-6 text-light"><a href='https://www.nio.com'>NIO</a></p>
</li>


<li>

  <a href="/Summit/2025/abstracts2025.html#a-panel-mit" class="text-dark no-underline! hover:underline!">

    <img class="mx-auto h-24 w-24 rounded-full" src="/images/summit/juliana-square.jpg" alt="Picture of Juliana Furgala<br>(Moderator)">
    <p class="mt-6 text-base font-semibold leading-7 tracking-tight text-dark">Juliana Furgala<br>(Moderator)</p>

  </a>

  <p class="text-sm leading-6 text-light"><a href='https://www.ll.mit.edu/'>MIT Lincoln Laboratory</a></p>
</li>

</ul>


</div>

            </div>
          </div>
        </div>
      </div>
    </li></ul>
</div>

      <div class="mt-10 flex items-center gap-x-6">
        <a href="/news/" class="button">More news</a>
        <div class="grow"> </div>


<a href="#page-top" class="relative">
  <div class="dark:bg-slate-900 hover:bg-slate-100 dark:hover:bg-slate-800 p-1.5 h-7 w-7 rounded-full shadow-xs left-1">
    <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke-width="1.5" stroke="currentColor" aria-hidden="true" data-slot="icon" class="h-4 w-4 text-slate-400 dark:text-slate-500">
  <path stroke-linecap="round" stroke-linejoin="round" d="M12 19.5v-15m0 0-6.75 6.75M12 4.5l6.75 6.75"/>
</svg>
  </div>
</a>


      </div>
    </div>
    <div class="mt-12 lg:mt-0  hidden lg:block">
      <a href="/news/">
        <img class="rounded-xl shadow-xl mx-auto "
             src="/images/newspaper.jpg" alt="a stack of newspapers" width="400">
      </a>
    </div>
  </div>
</div>

</div>





<div class="max-w-7xl bg-white py-20 sm:pt-24 sm:pb-32 dark:max-w-none mx-auto overflow-hidden
          light:sm:[mask-image:_linear-gradient(to_right,transparent_0,_black_200px,_black_calc(100%-200px),transparent_100%)]">
  <a href="/Foundation/Membership/" class="hover:underline">

    <h2 class="text-center text-3xl font-semibold leading-8 text-darkcol mb-10">Meet our members &rarr;</h2>

    <div class="inline-flex flex-nowrap"><ul class="flex items-center justify-center md:justify-start
                        animate-infinite-scroll motion-reduce:animate-none">
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/apple.svg"
                 alt="Apple" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/autoware.svg"
                 alt="Autoware Foundation" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/Cog.jpg"
                 alt="Cog Systems Inc" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/Cyberagentur.svg"
                 alt="Cyberagentur" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/DornerWorks.svg"
                 alt="DornerWorks Ltd" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/ETH-Zurich.svg"
                 alt="ETH Zurich" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/Jump-Trading.svg"
                 alt="Jump Trading" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/kansas-state.svg"
                 alt="Kansas State University" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/Kry10.svg"
                 alt="Kry10 Limited" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/lewis-clark.svg"
                 alt="Lewis & Clark College" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/mep.svg"
                 alt="MEP" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/NCSC.png"
                 alt="NCSC" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/NIO.svg"
                 alt="NIO" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/Penten.svg"
                 alt="Penten Pty Ltd" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/proofcraft.svg"
                 alt="Proofcraft" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/RISC-V.svg"
                 alt="RISC-V International" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/rtx-vertical.svg"
                 alt="RTX Corporation" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/TU-Munich.svg"
                 alt="TU Munich" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/TII.jpg"
                 alt="Technology Innovation Institute" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/TCCoE.svg"
                 alt="Trusted Computing Center of Excellence™" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/UNSW.svg"
                 alt="UNSW Sydney" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
            <img src="/Foundation/Membership/LOGOS/ku-institute.svg"
                 alt="University of Kansas" class="w-[158px] h-[100px] object-contain max-w-none">
        </li></ul>
      <!-- repeat for animation reset -->
      <ul class="flex items-center justify-center md:justify-start
                        animate-infinite-scroll motion-reduce:animate-none" aria-hidden="true">
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/apple.svg"
              alt="Apple" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/autoware.svg"
              alt="Autoware Foundation" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/Cog.jpg"
              alt="Cog Systems Inc" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/Cyberagentur.svg"
              alt="Cyberagentur" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/DornerWorks.svg"
              alt="DornerWorks Ltd" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/ETH-Zurich.svg"
              alt="ETH Zurich" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/Jump-Trading.svg"
              alt="Jump Trading" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/kansas-state.svg"
              alt="Kansas State University" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/Kry10.svg"
              alt="Kry10 Limited" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/lewis-clark.svg"
              alt="Lewis & Clark College" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/mep.svg"
              alt="MEP" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/NCSC.png"
              alt="NCSC" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/NIO.svg"
              alt="NIO" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/Penten.svg"
              alt="Penten Pty Ltd" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/proofcraft.svg"
              alt="Proofcraft" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/RISC-V.svg"
              alt="RISC-V International" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/rtx-vertical.svg"
              alt="RTX Corporation" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/TU-Munich.svg"
              alt="TU Munich" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/TII.jpg"
              alt="Technology Innovation Institute" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/TCCoE.svg"
              alt="Trusted Computing Center of Excellence™" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/UNSW.svg"
              alt="UNSW Sydney" class="w-[158px] h-[100px] object-contain max-w-none">
        </li>
        <li class="mx-8">
          <img src="/Foundation/Membership/LOGOS/ku-institute.svg"
              alt="University of Kansas" class="w-[158px] h-[100px] object-contain max-w-none">
        </li></ul>
    </div>
  </a>
</div>



<script type="module">
    function remtopx(rem) {
      return rem * parseFloat(getComputedStyle(document.documentElement).fontSize);
    }
    const callgraph = document.getElementById('callgraph');
    const Graph = ForceGraph3D()(callgraph)
        .height(remtopx(35))
        .backgroundColor('#030020')
        .warmupTicks(40)
        .cooldownTicks(500)
        .jsonUrl('js/callgraph.json')
        .nodeLabel('id')
        .nodeRelSize(2)
        .nodeResolution(12)
        .nodeOpacity(1)
        .nodeColor(node => node.name.length < 30 ? '#66FF00' : 'white')
        .linkWidth(0.5)
        .linkColor(_ => '#707080')
        .linkOpacity(0.9)
        .cameraPosition( { z: 850, y: 0, x: 0 }, { z: 0, y: -90, x: 0 } )
        .enableNodeDrag(false)
        .enableNavigationControls(true)
        .showNavInfo(false);

    addEventListener("resize", _ => {
      const el = document.getElementById('the_div');
      Graph.width(el.offsetWidth);
    });

  </script>


      </main><footer class="bg-gray-900" aria-labelledby="footer-heading">
  <h2 id="footer-heading" class="sr-only">Footer</h2>
  <div class="mx-auto max-w-7xl px-6 pb-8 pt-16 sm:pt-24 lg:px-8 lg:pt-32">
    <div class="ml-10 lg:grid lg:grid-cols-2 lg:gap-8">
      <div class="space-y-8">
        <img class="h-12" src="/images/seL4.svg" alt="seL4 logo">
        <p class="text-sm leading-6 text-gray-300">Proof. Performance. Security.</p>
        <!-- socials -->
        <div class="flex space-x-6">
          <a href="https://www.linkedin.com/company/sel4" title="seL4 on LinkedIn"
             class="text-gray-500 hover:text-gray-400">
            <svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" aria-hidden="true" viewBox="0 0 455 455" data-slot="icon" class="h-6 w-6">
  <path style="fill-rule:evenodd;clip-rule:evenodd;" d="M246.4,204.35v-0.665c-0.136,0.223-0.324,0.446-0.442,0.665H246.4z"/>
  <path style="fill-rule:evenodd;clip-rule:evenodd;" d="M0,0v455h455V0H0z M141.522,378.002H74.016V174.906h67.506V378.002z M107.769,147.186h-0.446C84.678,147.186,70,131.585,70,112.085c0-19.928,15.107-35.087,38.211-35.087 c23.109,0,37.31,15.159,37.752,35.087C145.963,131.585,131.32,147.186,107.769,147.186z M385,378.002h-67.524V269.345 c0-27.291-9.756-45.92-34.195-45.92c-18.664,0-29.755,12.543-34.641,24.693c-1.776,4.34-2.24,10.373-2.24,16.459v113.426h-67.537 c0,0,0.905-184.043,0-203.096H246.4v28.779c8.973-13.807,24.986-33.547,60.856-33.547c44.437,0,77.744,29.02,77.744,91.398V378.002 z"/>
</svg>
          </a>
          <a href="https://www.youtube.com/@seL4" title="seL4 on YouTube"
             class="text-gray-500 hover:text-gray-400">
            <svg xmlns="http://www.w3.org/2000/svg" fill="currentColor" aria-hidden="true" viewBox="0 0 24 24" data-slot="icon" class="h-6 w-6">
  <path fill-rule="evenodd" d="M19.812 5.418c.861.23 1.538.907 1.768 1.768C21.998 8.746 22 12 22 12s0 3.255-.418 4.814a2.504 2.504 0 0 1-1.768 1.768c-1.56.419-7.814.419-7.814.419s-6.255 0-7.814-.419a2.505 2.505 0 0 1-1.768-1.768C2 15.255 2 12 2 12s0-3.255.417-4.814a2.507 2.507 0 0 1 1.768-1.768C5.744 5 11.998 5 11.998 5s6.255 0 7.814.418ZM15.194 12 10 15V9l5.194 3Z" clip-rule="evenodd"/>
</svg>
          </a>
          <a href="https://github.com/seL4" title="seL4 on GitHub"
             class="text-gray-500 hover:text-gray-400">
            <svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" fill="currentColor" aria-hidden="true" data-slot="icon" class="h-6 w-6">
  <path d="M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52.28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z"/>
</svg>
          </a>
        </div>
      </div>
      <!-- footer menu -->
      <div class="mt-16 flex flex-wrap gap-8 lg:mt-0"><div class="flex-none">
          <h3 class="text-md font-semibold leading-6 text-logogreen">Useful Links</h3>
          <ul class="mt-6 space-y-4"><li>
              <a href="/About/" class="text-sm leading-6 text-gray-300 hover:text-white">What is seL4?</a>
            </li><li>
              <a href="/Foundation/" class="text-sm leading-6 text-gray-300 hover:text-white">seL4 Foundation</a>
            </li><li>
              <a href="/Foundation/Join/" class="text-sm leading-6 text-gray-300 hover:text-white">Become a Member</a>
            </li><li>
              <a href="/Legal/logo.html" class="text-sm leading-6 text-gray-300 hover:text-white">seL4 Logo</a>
            </li><li>
              <a href="/sitemap.html" class="text-sm leading-6 text-gray-300 hover:text-white">Sitemap</a>
            </li></ul>
        </div><div class="flex-none">
          <h3 class="text-md font-semibold leading-6 text-logogreen">Legal</h3>
          <ul class="mt-6 space-y-4"><li>
              <a href="/Legal/trademark.html" class="text-sm leading-6 text-gray-300 hover:text-white">seL4 Trademark</a>
            </li><li>
              <a href="/Legal/license.html" class="text-sm leading-6 text-gray-300 hover:text-white">seL4 License</a>
            </li><li>
              <a href="/Contribute/conduct.html" class="text-sm leading-6 text-gray-300 hover:text-white">Code of Conduct</a>
            </li><li>
              <a href="http://www.lfprojects.org/" class="text-sm leading-6 text-gray-300 hover:text-white">Site Policies</a>
            </li></ul>
        </div><div class="flex-none">
          <h3 class="text-md font-semibold leading-6 text-logogreen">Using seL4</h3>
          <ul class="mt-6 space-y-4"><li>
              <a href="https://docs.sel4.systems" class="text-sm leading-6 text-gray-300 hover:text-white">Docsite</a>
            </li><li>
              <a href="/Info/Docs/seL4-manual-latest.pdf" class="text-sm leading-6 text-gray-300 hover:text-white">Manual</a>
            </li><li>
              <a href="https://docs.sel4.systems/Tutorials/" class="text-sm leading-6 text-gray-300 hover:text-white">Tutorials</a>
            </li><li>
              <a href="/About/FAQ.html" class="text-sm leading-6 text-gray-300 hover:text-white">FAQ</a>
            </li></ul>
        </div></div>
    </div>
    <div class="flex flex-row flex-wrap gap-x-3 mt-16 border-t border-white/10 pt-8 sm:mt-20 lg:mt-24">
      <p class="flex-none max-w-full text-tiny sm:text-xs leading-5 text-gray-400">&copy; 2025 seL4 Project a Series of LF Projects, LLC. seL4 is a trademark of LF Projects, LLC.</p>
      <div class="flex-1"></div>
      <p class="flex-none text-tiny sm:text-xs leading-5 text-gray-400">
        Served on seL4. <a href="/About/webserver.html" class="underline">About this web server.</a>
      </p>
      </div>
  </div>
</footer>

   </div>
  </body>
</html>
    """
    return Response(body=html, headers={'Content-Type': 'text/html'})

app = Microdot()

@app.route('/')
async def index(request):
    # return await send_file('index.html', request.headers)
    return index_page()
    # return Response(body="<html><body>Hello</body></html>", headers={'Content-Type': 'text/html'})

# @app.route('/<path:path# >')
# async def static(request, path):
#     return await send_file(path, request.headers)

app.run(debug=False, port=80)
