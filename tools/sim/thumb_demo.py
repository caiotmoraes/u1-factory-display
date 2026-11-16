#!/usr/bin/env python3
"""Miniatura de demonstração (suporte em L, cinza) em RGB565 132x132 para o simulador."""
import sys, struct
from PIL import Image, ImageDraw
W = H = 132
BG = (0xEE, 0xF0, 0xF3)
im = Image.new('RGB', (W * 3, H * 3), BG)
d = ImageDraw.Draw(im)
s = 3
def P(pts): return [(x * s, y * s) for x, y in pts]
top, side, front, dark = (214, 217, 222), (168, 172, 180), (190, 194, 201), (140, 145, 153)
d.polygon(P([(20, 92), (70, 118), (112, 96), (62, 70)]), fill=top)       # base
d.polygon(P([(20, 92), (70, 118), (70, 126), (20, 100)]), fill=front)
d.polygon(P([(70, 118), (112, 96), (112, 104), (70, 126)]), fill=side)
d.polygon(P([(20, 92), (20, 24), (34, 17), (34, 85)]), fill=front)      # parede
d.polygon(P([(34, 17), (84, 43), (84, 111), (34, 85)]), fill=top)
d.polygon(P([(20, 24), (34, 17), (84, 43), (70, 50)]), fill=(226, 228, 232))
for cx, cy in [(48, 40), (68, 50), (52, 70)]:
    d.ellipse(P([(cx - 5, cy - 5), (cx + 5, cy + 5)]), fill=dark)
d.polygon(P([(40, 84), (40, 56), (46, 53), (66, 100), (60, 103)]), fill=side)  # nervura
for cx, cy in [(78, 100), (94, 92)]:
    d.ellipse(P([(cx - 5, cy - 3), (cx + 5, cy + 3)]), fill=dark)
im = im.resize((W, H), Image.LANCZOS)
out = bytearray()
for r, g, b in im.getdata():
    out += struct.pack('<H', ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
open(sys.argv[1], 'wb').write(out)
