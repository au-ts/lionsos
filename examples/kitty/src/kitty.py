import fb

print("KITTY GRAPHICS starting")

while True:
    print("hello")
    fb.wait()
    print("KITTY GRAPHICS about to write")
    for x in range(100, 300):
        for y in range(100, 300):
            fb.set_pixel(x, y)
            # fb.set_pixel(x, y, 200-(y-100)/5, 15+(x-100)/2, 100, 0)
    fb.flush()
