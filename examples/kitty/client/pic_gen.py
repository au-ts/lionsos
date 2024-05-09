import png

data = list(png.Reader("catwithfish.png").read()[2])

with open("catwithfish.data", "wb") as f:
    print(data)
    for d in data:
        f.write(d)

w, h = len(data[0]) // 4, len(data)
assert w == 400
assert h == 430
print("reading")
with open("catwithfish.data", "rb") as f:
    pic = list(f.read())
    w = 400
    h = 430
    for y in range(h):
        for x in range(w):
            pic_pixel = pic[(y * w * 4 + (x * 4)):(y * w * 4 + (x * 4))+4]
            data_pixel = data[y][(x * 4):(x * 4)+4]
            if bytearray(pic_pixel) != data_pixel:
                print(bytearray(pic_pixel), data_pixel, x, y)
            assert bytearray(pic_pixel) == data_pixel

