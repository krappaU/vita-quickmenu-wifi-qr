# SPDX-License-Identifier: MIT
# Makes camera-like test images: a QR code drawn small, off-centre, slightly rotated, on a grey
# background with noise, at 640x480 and 320x240, as grayscale PGM.
# Usage: make_qr.py <out_dir>  -> prints "<file> <payload>" lines (payload "-" for no code)
import math
import zlib
import random
import sys

import qrcode
from PIL import Image, ImageDraw, ImageFilter

CASES = [
    ("android_wpa", "WIFI:S:Hotel Guest;T:WPA;P:welcome123;H:false;;"),
    ("long_pass", "WIFI:S:Some Long Network Name 5G;T:WPA;P:" + "x9!Y" * 15 + "abc;R:1;;"),
    ("open", "WIFI:S:Airport Free;T:nopass;;"),
]


def render(payload, w, h, module_px, angle_deg, seed):
    rng = random.Random(seed)
    qr = qrcode.QRCode(border=4, error_correction=qrcode.constants.ERROR_CORRECT_M)
    qr.add_data(payload)
    qr.make(fit=True)
    m = qr.get_matrix()
    n = len(m)
    size = n * module_px
    cx, cy = w * 0.5 + 3, h * 0.5 - 2
    a = math.radians(angle_deg)
    ca, sa = math.cos(a), math.sin(a)
    img = bytearray(w * h)
    for y in range(h):
        for x in range(w):
            # inverse-rotate into code space
            dx, dy = x - cx, y - cy
            u = (ca * dx + sa * dy) + size / 2
            v = (-sa * dx + ca * dy) + size / 2
            if 0 <= u < size and 0 <= v < size:
                val = 30 if m[int(v // module_px)][int(u // module_px)] else 225
            else:
                val = 120  # background
            img[y * w + x] = max(0, min(255, val + rng.randint(-18, 18)))
    return img


def render_rounded(payload, radius_modules, seed):
    """Android's Wi-Fi share screen style: modules and corner squares drawn with rounded corners,
    dark on a light card on a dark background, slightly blurred. Strongly rounded corner squares
    defeat quirc's own grouping (no grid); src/quirc_ext.c's fallback handles them."""
    qr = qrcode.QRCode(border=0, error_correction=qrcode.constants.ERROR_CORRECT_Q)
    qr.add_data(payload)
    qr.make(fit=True)
    m = qr.get_matrix()
    n, px, quiet = len(m), 6, 3
    size = (n + 2 * quiet) * px
    im = Image.new("L", (size, size), 240)
    d = ImageDraw.Draw(im)
    eyes = [(0, 0), (n - 7, 0), (0, n - 7)]
    in_eye = lambda x, y: any(ex <= x < ex + 7 and ey <= y < ey + 7 for ex, ey in eyes)
    for y in range(n):
        for x in range(n):
            if m[y][x] and not in_eye(x, y):
                X, Y = (x + quiet) * px, (y + quiet) * px
                d.rounded_rectangle([X + 0.5, Y + 0.5, X + px - 1.5, Y + px - 1.5], radius=px * 0.35, fill=15)
    r = radius_modules
    for ex, ey in eyes:
        X, Y = (ex + quiet) * px, (ey + quiet) * px
        d.rounded_rectangle([X, Y, X + 7 * px - 1, Y + 7 * px - 1], radius=r * px, fill=15)
        d.rounded_rectangle([X + px, Y + px, X + 6 * px - 1, Y + 6 * px - 1], radius=(r - 0.3) * px, fill=240)
        d.rounded_rectangle([X + 2 * px, Y + 2 * px, X + 5 * px - 1, Y + 5 * px - 1], radius=r * 0.6 * px, fill=15)
    bg = Image.new("L", (640, 480), 20)
    bg.paste(im, (320 - size // 2, 240 - size // 2))
    bg = bg.rotate(3, fillcolor=20).filter(ImageFilter.GaussianBlur(1.0))
    return bg.tobytes()


def main():
    out = sys.argv[1]
    for name, payload in CASES:
        # At 320x240 a dense code needs >= 4 pixels per module: at 3, quirc misses the
        # 63-character password code once it is rotated (the reason 640x480 is the default).
        for (w, h, px) in ((640, 480, 6), (320, 240, 4)):
            img = render(payload, w, h, px, 7, zlib.crc32(name.encode()))
            path = f"{out}/{name}_{w}.pgm"
            with open(path, "wb") as f:
                f.write(b"P5 %d %d 255\n" % (w, h))
                f.write(img)
            print(path, payload)
    for r in (1.0, 2.0):
        payload = "WIFI:S:A_SUS_NETWORK;T:WPA;P:ex4mple-pass;H:false;;"
        path = f"{out}/android_rounded_{r}.pgm"
        with open(path, "wb") as f:
            f.write(b"P5 640 480 255\n")
            f.write(render_rounded(payload, r, 0))
        print(path, payload)
    blank = bytes(random.Random(1).randint(100, 140) for _ in range(320 * 240))
    with open(f"{out}/blank_320.pgm", "wb") as f:
        f.write(b"P5 320 240 255\n" + blank)
    print(f"{out}/blank_320.pgm", "-")


main()
