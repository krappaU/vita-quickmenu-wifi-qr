# SPDX-License-Identifier: MIT
# Makes camera-like test images: a QR code drawn small, off-centre, slightly rotated, on a grey
# background with noise, at 640x480 and 320x240, as grayscale PGM.
# Usage: make_qr.py <out_dir>  -> prints "<file> <payload>" lines (payload "-" for no code)
import math
import zlib
import random
import sys

import qrcode

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
    blank = bytes(random.Random(1).randint(100, 140) for _ in range(320 * 240))
    with open(f"{out}/blank_320.pgm", "wb") as f:
        f.write(b"P5 320 240 255\n" + blank)
    print(f"{out}/blank_320.pgm", "-")


main()
