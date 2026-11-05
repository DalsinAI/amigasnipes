#!/usr/bin/env python3
"""Writes amiga/icons/art/snipes.png, the icons' badge: a corner of the
maze drawn with the game's own characters and colours (the walls, a snipe
and the player, as Snipes.cpp's sprite tables have them) in the port's
8x8 font (amiga/cp437_font.h), on black.

    python3 amiga/tools/make_icon_art.py
"""
import re
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CGA = [(0x00, 0x00, 0x00), (0x00, 0x00, 0xAA), (0x00, 0xAA, 0x00), (0x00, 0xAA, 0xAA),
       (0xAA, 0x00, 0x00), (0xAA, 0x00, 0xAA), (0xAA, 0x55, 0x00), (0xAA, 0xAA, 0xAA),
       (0x55, 0x55, 0x55), (0x55, 0x55, 0xFF), (0x55, 0xFF, 0x55), (0x55, 0xFF, 0xFF),
       (0xFF, 0x55, 0x55), (0xFF, 0x55, 0xFF), (0xFF, 0xFF, 0x55), (0xFF, 0xFF, 0xFF)]
W, S = 0x9, 0x2   # maze walls; a snipe (data_1112: 0x201, 0x21A)
SCENE = [  # (character, colour) a cell
    [(0xC9, W), (0xCD, W), (0xCD, W), (0xCD, W), (0xBB, W)],
    [(0xBA, W), (0x01, S), (0x1A, S), (0x20, 0), (0xBA, W)],
    [(0xBA, W), (0x93, 0xF), (0x93, 0xF), (0x09, 0xE), (0xBA, W)],   # the player (data_10E2) and its bullet
    [(0xBA, W), (0x11, 0xF), (0x10, 0xF), (0x20, 0), (0xBA, W)],
    [(0xC8, W), (0xCD, W), (0xCD, W), (0xCD, W), (0xBC, W)],
]


def main():
    src = (ROOT / 'amiga/cp437_font.h').read_text()
    font = [[int(x, 16) for x in m.split(', ')] for m in re.findall(r'\{(0x[^}]*)\}', src)]
    h, w = len(SCENE) * 8, len(SCENE[0]) * 8
    rows = []
    for y in range(h):
        row = bytearray([0])
        for x in range(w):
            ch, col = SCENE[y // 8][x // 8]
            on = font[ch][y % 8] >> (7 - x % 8) & 1
            row += bytes(CGA[col]) + b'\xff' if on else b'\0\0\0\xff'
        rows.append(bytes(row))

    def chunk(t, d):
        return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))
    png = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)) \
        + chunk(b'IDAT', zlib.compress(b''.join(rows), 9)) + chunk(b'IEND', b'')
    (ROOT / 'amiga/icons/art/snipes.png').write_bytes(png)


if __name__ == '__main__':
    main()
