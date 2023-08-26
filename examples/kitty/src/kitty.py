import fb

print("KITTY GRAPHICS starting")

while True:
    fb.wait()
    print("KITTY GRAPHICS about to write")
    for x in range(100, 300):
        for y in range(100, 300):
            r = int(200-(y-100)/5)
            b = int(15+(x-100)/2)
            g = 100
            a = 0
            fb.set_pixel(x, y, (r << 24) & (b << 16) & (g << 8) & a)
    fb.flush()
