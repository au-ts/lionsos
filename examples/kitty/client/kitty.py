# import time
# import png
# import sys
# import socket
import asyncio
import errno
from pn532 import PN532
# from multiprocessing import shared_memory
# from bdfparser import Font

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

# @alwin: all the vars highlighted are still (especially) dodgy
######################################################
# fb = shared_memory.SharedMemory("fb0")
width = 800
height = 600
# pic = list(png.Reader("catwithfish.png").read()[2])
# font = Font('unifont-13.0.04.bdf')
reset_cb = None
######################################################


# Heartbeat to let the server know we still exist
def heartbeat():
    global writer_stream
    while True:
        # @alwin: is it really necessary to have this in a try-catch?
        try:
            print("Sending heartbeat")
            writer_stream.write(b'200 0 0\n')
            writer_stream.drain()
            await asyncio.sleep(4)
        except OSError as e:
            if (e.errno == errno.ECONNRESET):
                reader_stream, writer_stream = await asyncio.open_connection(IP_ADDRESS, PORT)

async def on_tap(token, card_id):
    global writer_stream
    while True:
        # @alwin: is it really necessary to have this in a try-catch?
        try:
            writer_stream.write(
                bytes(f'100 {token} {''.join('{:02x}'.format(x) for x in card_id)} 1.0' + '\n', 'utf-8'))
            # @alwin: Should this have an await()?
            writer_stream.drain()
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
                print("Resetting...")
                current_uid = []
                current_count = 0
                current_not_equal_count = 0
        else:
            # current_uid != [] && current_uid == uid ==> MATCH!
            current_count += 1
            current_not_equal_count = 0
            if (current_count == TICKS_TO_CONFIRM):
                print("Registering tap")
                await on_tap(token, current_uid)

        # Read the uid again, this one should always fail
        # We do this to consume the empty UID packet so it doesn't
        # mess with our matching. This only applies for MiFare
        # ultralight cards, which have len(uid) > 4.
        if (uid != [] and len(uid) > 4):
            uid = p.read_uid()
            assert uid == []

        await asyncio.sleep_ms(100)


def set_pixel(x, y, rgba):
    idx = 4 * (width * y + x)
    fb.buf[idx:(idx + 4)] = bytearray([
        rgba & 0xFF,
        (rgba >> 8) & 0xFF,
        (rgba >> 16) & 0xFF,
        (rgba >> 24) & 0xFF,
    ])


def reset_status():
    fill_rectangle(400, 250, 300, 300, 0x000000FF)
    draw_string(400, 250, "Tap to pay $1", 2, 0xFFFFFFFF, 0x0, False, False)


def draw_image(x0, y0, data):
    w, h = len(data[0]) // 4, len(data)
    for y in range(h):
        for x in range(w):
            pixel = data[y][(x * 4):(x * 4)+4]
            rgba = pixel[3] & 0xFF
            rgba |= (pixel[2] & 0xFF) << 8
            rgba |= (pixel[1] & 0xFF) << 16
            rgba |= (pixel[0] & 0xFF) << 24
            if rgba == 0x000000FF:
                rgba = 0x303030FF
            set_pixel(x0 + x, y0 + y, rgba)


def draw_bitmap(x, y, bitmap, color1, color2):
    for x_off in range(len(bitmap[0])):
        for y_off in range(len(bitmap)):
            if bitmap[y_off][x_off] == 1:
                set_pixel(x + x_off, y + y_off, color1)
            elif bitmap[y_off][x_off] == 2:
                set_pixel(x + x_off, y + y_off, color2)
            else:
                set_pixel(x + x_off, y + y_off, 0x000000FF)


def draw_string(x, y, string, scale, color1, color2, shadowed, glowing):
    x_off = 8 * scale
    for i in range(len(string)):
        glyph = font.glyph(string[i]).draw()
        if shadowed:
            glyph = glyph.shadow()
        if glowing:
            glyph = glyph.glow()
        glyph *= scale
        data = glyph.todata(2)
        draw_bitmap(x + i * x_off, y, data, color1, color2)


def fill_rectangle(x0, y0, w, h, rgba):
    for x in range(x0, x0 + w):
        for y in range(y0, y0 + h):
            set_pixel(x, y, rgba)


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
    while True:
        try:
            message = await asyncio.wait_for(reader_stream.readline(), 0.5)
        except OSError:
            # This usually happens when the server does not recieve a heartbeat
            # in time and resets the connection
            print("CONNECTION RESET")
            reader_stream.close()
            writer_stream.close()
            reader_stream, writer_stream = await asyncio.open_connection(IP_ADDRESS, PORT)
            continue
        except asyncio.TimeoutError:
            continue

        if len(message) == 0:
            continue

        print(message)
        words = message.decode('utf-8').split()
        if words[0] == '101':
            token = int(words[1])
            print(f'TOKEN = {token}')
        elif words[0] == '200':
            name, balance = words[2], words[4]
            draw_string(400, 250, f'Thanks {name}', 2,
                        0xFFFFFFFF, 0x0, False, False)
            draw_string(400, 300, f'Balance: {balance}', 2,
                        0xFFFFFFFF, 0x0, False, False)
            if reset_cb is not None:
                reset_cb.cancel()
                reset_cb = None
            asyncio.create_task(wait_seconds_and_call(2, reset_status()))


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

    # draw_image(0, 40, pic)
    # draw_string(400, 100, "Kitty v5", 5, 0xFFFFFFFF, 0x008800FF, True, False)

    await asyncio.gather(
        asyncio.create_task(heartbeat()),
        asyncio.create_task(read_from_server()),
        asyncio.create_task(read_card_main())
    )

asyncio.run(main())
