# Copyright 2024, UNSW
# SPDX-License-Identifier: BSD-2-Clause
#
# The purpose of this script is to do all the client side
# 'business logic' for the Kitty system. This includes:
# * waiting for and receiving card taps via the I2C card
#   reader.
# * connecting to the server over the network to register
#   card taps.
# * drawing and updating the UI based on card taps from
#   users and responses from the server.
#


import framebuf
import fb
import time
import sys
import asyncio
import errno
import os
from pn532 import PN532
import font_height50
import font_height35
import config
from writer import CWriter

# Expect this to be on the file system by the time we start, if we are
# configured to load from NFS
LOGO_PATH = "lionsos_logo.data"
LOGO_WIDTH = 356
LOGO_HEIGHT = 335

current_uid = []
current_equal_count = 0
current_not_equal_count = 0
TICKS_TO_CONFIRM = 2
TICKS_TO_RESET = 3
TICKS_DELAY_MS = 50
token = 0
reader_stream = None
writer_stream = None
stdin_ready = True

HOST = ""
PORT = 3737

""" Globals for dealing with UI """
# KittyDisplay instance
display = None
# Display text writers. The way we use fonts right now is with
# a fixed display height. We use two heights in the UI so we have
# two separate writers, one for each font height,
wri50 = None
wri35 = None


def info(s):
    print("KITTY|INFO: " + str(s))


def error(s):
    print("KITTY|ERROR: " + str(s))


class BoolPalette(framebuf.FrameBuffer):
    def __init__(self, mode):
        buf = bytearray(4)  # OK for <= 16 bit color
        super().__init__(buf, 2, 1, mode)

    def fg(self, color):  # Set foreground color
        self.pixel(1, 0, color)

    def bg(self, color):
        self.pixel(0, 0, color)


class KittyDisplay(framebuf.FrameBuffer):
    def __init__(self, width, height):
        self.buf = bytearray(width * height * 2)
        self.palette = BoolPalette(framebuf.RGB565)
        self.width = width
        self.height = height
        super().__init__(self.buf, self.width, self.height, framebuf.RGB565)

    def show(self):
        fb.wait()
        fb.machine_fb_send(memoryview(self.buf), self.width, self.height)


# Heartbeat to let the server know we still exist
def heartbeat():
    global writer_stream
    while True:
        try:
            info(f"Sending heartbeat at {time.time()}")
            writer_stream.write(b'200 0 0\n')
            await writer_stream.drain()
        except OSError as e:
            if e.errno == errno.ECONNRESET:
                error("RESETTING CONNECTION")
                reader_stream, writer_stream = await asyncio.open_connection(HOST, PORT)

        await asyncio.sleep(4)


async def on_tap(card_id):
    global writer_stream
    global token
    display.rect(0, 400, 2000, 2000, 0x0, True)
    wri50.set_textpos(display, 430, 370)
    wri50.printstring("Processing...")
    display.show()
    while True:
        try:
            info(f'card id: {card_id}')
            writer_stream.write(
                bytes(f'100 {token} {''.join('{: 02x}'.format(x) for x in card_id)} 1.0' + '\n', 'utf-8')
            )
            await writer_stream.drain()
            break
        except OSError as e:
            if e.errno == errno.ECONNRESET:
                error("RESETTING CONNECTION")
                reader_stream, writer_stream = await asyncio.open_connection(HOST, PORT)

    token = (token + 1) % 1000000


async def read_stdin():
    sreader = asyncio.StreamReader(sys.stdin)
    global token
    global stdin_ready
    while True:
        if token != 0 and stdin_ready:
            stdin_ready = False
            print("\033[34mPlease enter card number: \033[0m")
            uid_str = await sreader.readline()
            info(f"Got card ID: {uid_str.strip()}")
            uid = []
            try:
                uid.append(int(uid_str))
            except ValueError:
                print(f"Card ID '{uid_str}' must be an integer")
                stdin_ready = True
                continue
            await on_tap(uid)

        await asyncio.sleep_ms(100)


# We require TICKS_TO_CONFIRM consecutive ticks with the card_id to
# count a tap/ Once a card has been read, we require TICKS_TO_RESET
# consecutive no-card reads to reset to a waiting state. This
# prevents erroneous double taps
async def read_card(p):
    global current_uid
    global current_count
    global current_not_equal_count
    global TICKS_TO_RESET
    global TICKS_TO_CONFIRM
    global token
    global TICKS_DELAY_MS
    while True:
        print("Waiting for card read")
        uid = p.read_uid()
        # Case where:
        #   - We are not waiting on a specific card
        #   - p.read_uid() did not return a card ID
        if uid == [] and current_uid == []:
            await asyncio.sleep_ms(TICKS_DELAY_MS)
            continue

        if current_uid == []:
            # If we are not currently waiting for a specific card
            current_uid = uid
            current_count = 1
        elif current_uid != uid:
            # If we are waiting on a specific card, but the one we just
            # read does not match this.
            current_not_equal_count += 1
            if current_count < TICKS_TO_CONFIRM:
                current_count = 0
            if current_not_equal_count == TICKS_TO_RESET:
                # If we see multiple non-matches in a row,
                # go back to a reset state
                info("Resetting...")
                current_uid = []
                current_count = 0
                current_not_equal_count = 0
        else:
            # current_uid != [] && current_uid == uid ==> MATCH!
            current_count += 1
            current_not_equal_count = 0
            if current_count == TICKS_TO_CONFIRM:
                info("Registering tap")
                await on_tap(current_uid)

        if len(uid) > 4:
            # On MiFare ultralight cards, which have a UID of length greater
            # than 4, we have noticed that the PN532 card reader device can
            # return an empty UID when the physical card is still near the device.
            # To avoid this messing with our logic, we do an extra UID read here.
            uid = p.read_uid()

        await asyncio.sleep_ms(TICKS_DELAY_MS)


def set_pixel(display, x, y, rgba):
    r = (rgba >> 24) & 0xff
    g = (rgba >> 16) & 0xff
    b = (rgba >> 8) & 0xff
    rgb565 = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3)
    display.pixel(x, y, rgb565)


def reset_status():
    global stdin_ready
    display.rect(0, 400, 2000, 2000, 0x0, True)
    wri50.set_textpos(display, 430, 330)
    wri50.printstring("Waiting for taps...")
    display.show()
    # Now we are ready to accept new taps
    stdin_ready = True


def draw_image(display, x0, y0, data):
    w = LOGO_WIDTH
    h = LOGO_HEIGHT
    for y in range(h):
        for x in range(w):
            pixel = data[(y * w * 4 + (x * 4)):(y * w * 4 + (x * 4))+4]
            rgba = pixel[3] & 0xFF
            rgba |= (pixel[2] & 0xFF) << 8
            rgba |= (pixel[1] & 0xFF) << 16
            rgba |= (pixel[0] & 0xFF) << 24
            if rgba == 0x000000FF:
                rgba = 0x303030FF
            set_pixel(display, x0 + x, y0 + y, rgba)


# Helper function for sleeping some number of seconds and
# calling a function
async def wait_seconds_and_call(seconds, fn):
    await asyncio.sleep(seconds)
    fn()


# Coroutine responsible for listening to the server
async def read_from_server():
    global reader_stream
    global writer_stream
    global token
    while True:
        try:
            message = await reader_stream.readline()
        except OSError:
            # This usually happens when the server does not recieve a heartbeat
            # in time and resets the connection

            # Error message was originally "connection reset", but there are some cases where
            # this exception can trigger that aren't connection-based

            error("OSERROR, resetting connection")
            reader_stream.close()
            writer_stream.close()
            reader_stream, writer_stream = await asyncio.open_connection(HOST, PORT)
            continue
        except asyncio.TimeoutError:
            continue

        if len(message) == 0:
            continue

        info(message)

        words = message.decode('utf-8').split(" ", 1)
        if words[0] == '101':
            token = int(words[1])
            info(f'TOKEN = {token}')

        # Print server time in smaller font
        elif words[0] == '100':
            display.rect(500, 250, 1000, 100, 0x0, True)
            wri35.set_textpos(display, 250, 500)
            wri35.printstring(words[1])
            display.show()

        else:
            # Print server response
            display.rect(0, 400, 2000, 2000, 0x0, True)
            # Position text in middle of row
            wri50.set_textpos(
                display, 430, (config.TRUE_DISPLAY_WIDTH - wri50.stringlen(words[1]))//2)
            wri50.printstring(f"{words[1]}")

            # Display "new card" message for longer, so person has enough time to see number
            if words[0] == '400':
                message_display_time = 5
            else:
                message_display_time = 3

            # Success, print message that the user's account has been charged
            if words[0] == '200':
                display.rect(0, 500, 2000, 2000, 0x0, True)
                charged_str = "Charged $1"
                wri50.set_textpos(
                    display, 500, (config.TRUE_DISPLAY_WIDTH - wri50.stringlen(charged_str))//2)

                wri50.printstring(charged_str)

            display.show()

            asyncio.create_task(wait_seconds_and_call(
                message_display_time, reset_status))


# Coroutine responsible for reading the card
async def read_card_main():
    global stdin_ready
    # While we are processing a tap, we cannot accept other taps.
    # We consider to be finished processing after the server has
    # responded, and after we have then called reset_status()
    if config.enable_i2c:
        print("pn532: Initialising...")
        p = PN532(1)
        print("pn532: Initialised!")
        p.rf_configure()
        p.sam_configure()
        print("pn532: configured")
        await read_card(p)
    elif stdin_ready is True:
        await read_stdin()
    pass


async def main():
    # Only read the image from NFS if flag is set. Otherwise
    # we will just print strings.
    global reader_stream
    global writer_stream
    info(f"starting at {time.time()}")
    reader_stream, writer_stream = await asyncio.open_connection(HOST, PORT)

    # The logo is stored on the NFS directory, do not load it if we do not
    # have access to it.
    # TODO: gracefully continue instead of crashing out if not found
    if config.enable_nfs:
        logo_stat = os.stat(LOGO_PATH)
        logo_size = logo_stat[6]
        logo_buf = bytearray(logo_size)
        with open(LOGO_PATH, "rb") as f:
            nbytes = f.readinto(logo_buf)
            info(f"read {nbytes} bytes")
            logo = memoryview(logo_buf)
            info("read image, starting to draw")
            draw_image(display, 50, 20, logo)
        display.show()

    wri50.setcolor(0xffff, 0x0000)
    wri50.set_textpos(display, 100, 500)
    wri50.printstring("Welcome to Kitty v5")
    wri50.set_textpos(display, 175, 500)
    wri50.setcolor(0x8e27, 0x0000)
    wri50.printstring("Running on LionsOS!")
    wri50.setcolor(0xffff, 0x0000)
    wri50.set_textpos(display, 430, 330)
    wri50.printstring("Waiting for taps...")
    wri35.setcolor(0xffff, 0x0000)

    display.show()

    await asyncio.gather(
        asyncio.create_task(heartbeat()),
        asyncio.create_task(read_from_server()),
        asyncio.create_task(read_card_main())
    )


def help_prompt():
    print("\nWelcome to Kitty!\nUsage: kitty.run(String HOST).\n\
            HOST: The host name / IP address of the kitty server.")


def run(host: str):
    global display
    global wri50
    global wri35
    global HOST

    if host is None:
        error("host address is None")
        return

    if not isinstance(host, str):
        error("host address should be of type string")
        return

    display = KittyDisplay(config.display_width, config.display_height)
    wri50 = CWriter(display, font_height50)
    wri35 = CWriter(display, font_height35)
    HOST = host
    # TODO: gracefully wait for NFS / tell user to do so.
    # currently just crashes with EHOSTUNREACH otherwise!
    asyncio.run(main())


help_prompt()
