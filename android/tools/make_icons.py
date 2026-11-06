#!/usr/bin/env python3
"""
Генератор иконки приложения (без внешних зависимостей).

Рисует джойстик на скруглённом тёмном квадрате и раскладывает PNG по
mipmap-* каталогам. Запуск из каталога android/:

    python3 tools/make_icons.py
"""

import os
import struct
import zlib

SS = 4  # суперсэмплинг: 4x4 подпикселя на точку — сглаженные края

SIZES = {
    "mipmap-mdpi": 48,
    "mipmap-hdpi": 72,
    "mipmap-xhdpi": 96,
    "mipmap-xxhdpi": 144,
    "mipmap-xxxhdpi": 192,
}

BG_TOP = (15, 20, 32)
BG_BOTTOM = (21, 27, 38)
BASE = (27, 34, 48)
RING = (56, 189, 248)
HANDLE = (125, 211, 252)
OUTLINE = (53, 64, 90)

RES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "app", "src", "main", "res")


def in_rounded_rect(x, y, size, radius):
    r = radius
    if x < 0 or y < 0 or x > size or y > size:
        return False
    cx = min(max(x, r), size - r)
    cy = min(max(y, r), size - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def dist(x, y, cx, cy):
    return ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5


def sample(x, y, size):
    """Цвет в точке (x, y) для иконки размером size или None (прозрачно)."""
    radius = size * 0.22
    if not in_rounded_rect(x, y, size, radius):
        return None

    t = y / size
    bg = tuple(int(BG_TOP[i] + (BG_BOTTOM[i] - BG_TOP[i]) * t) for i in range(3))
    color = bg

    cx, cy = size * 0.5, size * 0.56
    r_base = size * 0.30
    r_handle = size * 0.115
    hx, hy = cx + size * 0.13, cy - size * 0.12

    d_base = dist(x, y, cx, cy)
    if d_base <= r_base:
        color = BASE
    if r_base - size * 0.045 <= d_base <= r_base:
        color = RING
    if d_base > r_base and d_base <= r_base + size * 0.012:
        color = OUTLINE
    if dist(x, y, hx, hy) <= r_handle:
        color = HANDLE

    return color


def render(size):
    pixels = bytearray()
    step = 1.0 / SS
    for py in range(size):
        for px in range(size):
            r = g = b = a = 0
            for sy in range(SS):
                for sx in range(SS):
                    c = sample(px + (sx + 0.5) * step, py + (sy + 0.5) * step, size)
                    if c is not None:
                        r += c[0]
                        g += c[1]
                        b += c[2]
                        a += 255
            total = SS * SS
            if a == 0:
                pixels += bytes((0, 0, 0, 0))
            else:
                covered = a // 255
                pixels += bytes((r // covered, g // covered, b // covered, a // total))
    return pixels


def write_png(path, size, pixels):
    raw = bytearray()
    stride = size * 4
    for y in range(size):
        raw.append(0)  # filter type 0
        raw += pixels[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        crc = zlib.crc32(tag + data) & 0xFFFFFFFF
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", crc)

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", header)
        + chunk(b"IDAT", zlib.compress(bytes(raw), 9))
        + chunk(b"IEND", b"")
    )
    with open(path, "wb") as f:
        f.write(png)


def main():
    for folder, size in SIZES.items():
        target_dir = os.path.normpath(os.path.join(RES_DIR, folder))
        os.makedirs(target_dir, exist_ok=True)
        path = os.path.join(target_dir, "ic_launcher.png")
        write_png(path, size, render(size))
        print("wrote", os.path.relpath(path))


if __name__ == "__main__":
    main()
