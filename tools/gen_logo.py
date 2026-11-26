#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The MONOEATER head (art/monoeater.svg) as two 1-bit pictures for the loading screen: app/logo.h.
   LOGO_INK  the white of the face (drawn in ink), LOGO_MASK  the whole sticker (the fog and the smoke go behind it).
   ./tools/gen_logo.py [size]   (needs rsvg-convert and Pillow)"""
import subprocess, sys, io, pathlib
from PIL import Image
R = pathlib.Path(__file__).resolve().parent.parent
N = int(sys.argv[1]) if len(sys.argv) > 1 else 124
png = subprocess.run(["rsvg-convert", "-w", str(N * 4), "-h", str(N * 4), str(R / "art/monoeater.svg")],
                     check=True, capture_output=True).stdout
im = Image.open(io.BytesIO(png)).convert("RGBA").resize((N, N), Image.LANCZOS)
row = (N + 7) // 8
def bits(f):
    out = []
    for y in range(N):
        for b in range(row):
            v = 0
            for k in range(8):
                x = b * 8 + k
                if x < N and f(*im.getpixel((x, y))):
                    v |= 0x80 >> k
            out.append(v)
    return out
ink = bits(lambda r, g, b, a: a > 128 and r > 128)
solid = [[im.getpixel((x, y))[3] > 128 and im.getpixel((x, y))[0] > 128 for x in range(N)] for y in range(N)]   # (the ink itself: no dark edge)
outside = [[False] * N for _ in range(N)]          # the mask: the sticker with its holes filled (flood from the edge)
todo = [(x, y) for x in range(N) for y in (0, N - 1)] + [(x, y) for y in range(N) for x in (0, N - 1)]
while todo:
    x, y = todo.pop()
    if 0 <= x < N and 0 <= y < N and not outside[y][x] and not solid[y][x]:
        outside[y][x] = True
        todo += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
_px = im.getpixel
im.getpixel = lambda xy: (0, 0, 0, 0 if outside[xy[1]][xy[0]] else 255)
mask = bits(lambda r, g, b, a: a > 100)
im.getpixel = _px
def arr(name, d):
    s = f"static const uint8_t {name}[LOGO_N * LOGO_ROW] = {{\n"
    for i in range(0, len(d), 16):
        s += "    " + ", ".join(f"0x{v:02x}" for v in d[i:i + 16]) + ",\n"
    return s + "};\n"
(R / "app/logo.h").write_text(
    "/* SPDX-License-Identifier: MPL-2.0\n * Copyright (C) 2026 Michael (MONODRONE) */\n"
    "/* The MONOEATER head, 1-bit, for the loading screen (tools/gen_logo.py from art/monoeater.svg: do not edit) */\n"
    "#pragma once\n#include <stdint.h>\n"
    f"#define LOGO_N {N}\n#define LOGO_ROW {row}\n" + arr("LOGO_INK", ink) + arr("LOGO_MASK", mask))
print("app/logo.h", N, "px")
