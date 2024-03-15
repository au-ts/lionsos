import time
import png
import sys
import socket
import asyncio
from multiprocessing import shared_memory
from bdfparser import Font

width = 800
height = 600

fb = shared_memory.SharedMemory("fb0")

font = Font('unifont-13.0.04.bdf')
pic=list(png.Reader("catwithfish.png").read()[2])

loop = asyncio.new_event_loop()

def set_pixel(x, y, rgba):
    idx = 4 * (width * y + x) 
    fb.buf[idx:(idx + 4)] = bytearray([
        rgba & 0xFF,
        (rgba >> 8) & 0xFF,
        (rgba >> 16) & 0xFF,
        (rgba >> 24) & 0xFF,
    ])

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
        if shadowed: glyph = glyph.shadow()
        if glowing: glyph = glyph.glow()
        glyph *= scale
        data = glyph.todata(2)
        draw_bitmap(x + i * x_off, y, data, color1, color2)

def draw_image(x0, y0, data):
    w, h = len(data[0]) // 4, len(data)
    for y in range(h):
        for x in range(w):
            pixel = data[y][(x * 4):(x * 4)+4]
            rgba = pixel[3] & 0xFF
            rgba |= (pixel[2] & 0xFF) << 8
            rgba |= (pixel[1] & 0xFF) << 16
            rgba |= (pixel[0] & 0xFF) << 24
            if rgba == 0x000000FF: rgba = 0x303030FF
            set_pixel(x0 + x, y0 + y, rgba)

def fill_rectangle(x0, y0, w, h, rgba):
    for x in range(x0, x0 + w):
        for y in range(y0, y0 + h):
            set_pixel(x, y, rgba)

def reset_status():
    fill_rectangle(400, 250, 300, 300, 0x000000FF)
    draw_string(400, 250, "Tap to pay $1", 2, 0xFFFFFFFF, 0x0, False, False)

reset_cb = None

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
token = 0

def on_tap():
    global jarcher_bal
    global token
    sys.stdin.readline()
    card_id = hex(123456789012)[2:] 
    sock.send(bytes(f'100 {token} {card_id} 1.0' + '\n', 'utf-8'))
    token = (token + 1) % 1000000

def heartbeat():
    global sock
    sock.send(b'200 0 0\n')
    loop.call_later(4, heartbeat)

def on_message():
    global reset_cb
    global sock
    global token
    messages = sock.recv(1000).split(b'\n')
    for message in messages:
        print(message)
        words = message.decode('utf-8').split()
        if len(words) == 0: continue
        if words[0] == '101':
            token = int(words[1])
            print(f'TOKEN = {token}')
        elif words[0] == '200':
            name, balance = words[2], words[4]
            draw_string(400, 250, f'Thanks {name}', 2, 0xFFFFFFFF, 0x0, False, False)
            draw_string(400, 300, f'Balance: {balance}', 2, 0xFFFFFFFF, 0x0, False, False)
            if reset_cb is not None:
                reset_cb.cancel()
                reset_cb = None
            reset_cb = loop.call_later(2, reset_status)

sock.connect(("localhost", 3737))
sock.setblocking(0)
heartbeat()

draw_image(0, 40, pic)
draw_string(400, 100, "Kitty v5", 5, 0xFFFFFFFF, 0x008800FF, True, False)
reset_status()

loop.add_reader(sys.stdin, on_tap)
loop.add_reader(sock, on_message)
loop.run_forever() 
