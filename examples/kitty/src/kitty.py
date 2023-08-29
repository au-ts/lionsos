import fb

print("KITTY GRAPHICS starting")

while True:
    fb.wait()
    print("KITTY GRAPHICS about to write")
    for x in range(100, 300):
        for y in range(100, 300):
            r = (int(200-(y-100)/5) << 24) & 0xff000000
            b = (int(15+(x-100)/2) << 16)  & 0x00ff0000
            g = (int(100) << 8) & 0x0000ff00
            fb.set_pixel(x, y, r | b | g | a)
    print("flushing!")
    fb.flush()
    print("flushed")
