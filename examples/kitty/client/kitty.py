import framebuf
import fb
import time
from png import png
import sys
# import socket
import asyncio
import errno
from pn532 import PN532
# from multiprocessing import shared_memory
# from bdfparser import Font
import font
from font_writer import CWriter

current_uid = []
current_equal_count = 0
current_not_equal_count = 0
TICKS_TO_CONFIRM = 5
TICKS_TO_RESET = 3
token = 0
reader_stream = None
writer_stream = None

IP_ADDRESS = "172.16.0.2"
PORT = 3738


def info(s):
    print("CLIENT|INFO: " + str(s))


def error(s):
    print("CLIENT|ERROR: " + str(s))


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
        fb.machine_fb_send(self.buf, self.width, self.height)


# @alwin: all the vars highlighted are still (especially) dodgy
######################################################
display_width = 1920
display_height = 1080
display = KittyDisplay(display_width, display_height)
wri = CWriter(display, font)
reset_cb = None
######################################################

# Heartbeat to let the server know we still exist
def heartbeat():
    global writer_stream
    while True:
        # @alwin: is it really necessary to have this in a try-catch?
        try:
            info(f"Sending heartbeat at {time.time()}")
            writer_stream.write(b'200 0 0\n')
            await writer_stream.drain()
        except OSError as e:
            if (e.errno == errno.ECONNRESET):
                reader_stream, writer_stream = await asyncio.open_connection(IP_ADDRESS, PORT)

        await asyncio.sleep(4)


async def on_tap(card_id):
    global writer_stream
    global token
    while True:
        # @alwin: is it really necessary to have this in a try-catch?
        try:
            writer_stream.write(
                bytes(f'100 {token} {''.join('{:02x}'.format(x) for x in card_id)} 1.0' + '\n', 'utf-8'))
            # @alwin: Should this have an await()?
            await writer_stream.drain()
            break
        except OSError as e:
            if (e.errno == errno.ECONNRESET):
                reader_stream, writer_stream = await asyncio.open_connection(IP_ADDRESS, PORT)

    token = (token + 1) % 1000000


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

    while True:
        uid = p.read_uid()
        # Case where:
        #   - We are not waiting on a specific card
        #   - p.read_uid() did not return a card ID
        if uid == [] and current_uid == []:
            await asyncio.sleep_ms(100)
            continue

        if current_uid == []:
            # If we are not currently waiting for a specific card
            current_uid = uid
            current_count = 1
        elif current_uid != uid:
            # If we are waiting on a specific card, but the one we just
            # read does not match this.
            current_not_equal_count += 1
            if (current_count < TICKS_TO_CONFIRM):
                current_count = 0
            if (current_not_equal_count == TICKS_TO_RESET):
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
            if (current_count == TICKS_TO_CONFIRM):
                info("Registering tap")
                await on_tap(current_uid)

        # Read the uid again, this one should always fail
        # We do this to consume the empty UID packet so it doesn't
        # mess with our matching. This only applies for MiFare
        # ultralight cards, which have len(uid) > 4.
        if (uid != [] and len(uid) > 4):
            uid = p.read_uid()
            assert uid == []

        await asyncio.sleep_ms(100)


def set_pixel(display, x, y, rgba):
    r = (rgba >> 24) & 0xff
    g = (rgba >> 16) & 0xff
    b = (rgba >> 8) & 0xff
    rgb565 = ((r & 0b11111000) << 8) | ((g & 0b11111100) << 3) | (b >> 3);
    display.pixel(x, y, rgb565)


def reset_status():
    display.rect(300, 300, 700, 700, 0x0, True)
    wri.set_textpos(display, 300, 540)
    wri.printstring("waiting for taps...")
    display.show()


def draw_image(display, x0, y0, data):
    # w, h = len(data[0]) // 4, len(data)
    w = 400
    h = 430
    for y in range(h):
        for x in range(w):
            pixel = data[(y * w * 4 + (x * 4)):(y * w * 4 + (x * 4))+4]
            # print(f"KITTY|INFO: {pixel}")
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
    global reset_cb
    global reader_stream
    global writer_stream
    global token
    while True:
        try:
            # @alwin: I don't really know if this is any better than just
            # .readline(), but I saw it in the webserver and it seemed to
            # slightly improve reliability (though it could have been some
            # other change). Needs some more thought/experimentation.
            # message = await asyncio.wait_for(reader_stream.readline(), 0.5)
            message = await reader_stream.readline()
        except OSError:
            # This usually happens when the server does not recieve a heartbeat
            # in time and resets the connection
            error("CONNECTION RESET")
            reader_stream.close()
            writer_stream.close()
            reader_stream, writer_stream = await asyncio.open_connection(IP_ADDRESS, PORT)
            continue
        except asyncio.TimeoutError:
            continue

        if len(message) == 0:
            continue

        info(message)
        words = message.decode('utf-8').split()
        if words[0] == '101':
            token = int(words[1])
            info(f'TOKEN = {token}')
        elif words[0] == '200':
            info("got response")
            name, balance = words[2], words[4]
            display.rect(540, 300, 700, 700, 0x0000, True)
            wri.set_textpos(display, 300, 400)
            wri.printstring(f"Thanks {name}")
            wri.set_textpos(display, 400, 400)
            wri.printstring(f"Your balance is now {balance}")
            display.show()
            # draw_string(400, 250, f'Thanks {name}', 2,
                        # 0xFFFFFFFF, 0x0, False, False)
            # draw_string(400, 300, f'Balance: {balance}', 2,
                        # 0xFFFFFFFF, 0x0, False, False)
            if reset_cb is not None:
                reset_cb.cancel()
                reset_cb = None
            asyncio.create_task(wait_seconds_and_call(5, reset_status))


# Coroutine responsible for reading the card
async def read_card_main():
    p = PN532(1)
    p.rf_configure()
    p.sam_configure()
    await read_card(p)

async def main():
    global reader_stream
    global writer_stream
    reader_stream, writer_stream = await asyncio.open_connection(IP_ADDRESS, PORT)

    print(f"KITTY|INFO: starting at {}", time.time())
    size = 688000
    cat_buf = bytearray(size)
    with open("catwithfish.data", "rb") as f:
        nbytes = f.readinto(cat_buf)
        print(f"KITTY|INFO: read {nbytes} bytes")
        pic = memoryview(cat_buf)
        # print(pic[:4])
        print("KITTY|INFO: read image, starting to draw")
        draw_image(display, 0, 40, pic[0:])
    display.show()
    print(time.time())
    print("KITTY|INFO: about to draw string!")
    # draw_string(400, 100, "Kitty v5", 5, 0xFFFFFFFF, 0x008800FF, True, False)
    wri.setcolor(0xffff, 0x0000)
    wri.set_textpos(display, 100, 540)
    wri.printstring("Welcome to Kitty v5!")
    wri.set_textpos(display, 300, 540)
    wri.printstring("waiting for taps...")

    display.show()

    await asyncio.gather(
        asyncio.create_task(heartbeat()),
        asyncio.create_task(read_from_server()),
        asyncio.create_task(read_card_main())
    )

asyncio.run(main())
