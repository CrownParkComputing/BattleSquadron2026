#!/usr/bin/env python3
"""Rip Hybris' whole enemy cast to PNGs.

Format (found from the blit destinations, see extract_hybris_gfx.py): five
planes, each PADDED to 128 bytes whatever the frame's height, rows four bytes
apart.  Every source the blitter fetched from the loaded-data region is a
frame the game actually drew; the 1500-odd sources outside it are background
save/restore, not art.

    BS_DUMP_BLITS=build/blits.txt ./build/hybris --frames 3000 \\
        --fire-from 700 --dump build/mem.bin
    tools/rip_hybris_sprites.py build/mem.bin build/blits.txt build/sprites
"""
import argparse
import os
import struct
import zlib
from collections import Counter

PLANES = 5
PLANE_STRIDE = 128
ART_LOW, ART_HIGH = 0x14000, 0x40000


def load_palette(path):
    try:
        raw = open(path, "rb").read(64)
    except OSError:
        return [(i * 8, i * 8, i * 8) for i in range(32)]
    out = []
    for i in range(32):
        word = struct.unpack(">H", raw[i * 2:i * 2 + 2])[0]
        out.append((((word >> 8) & 15) * 17, ((word >> 4) & 15) * 17,
                    (word & 15) * 17))
    return out


def decode(memory, base, words, rows):
    out = []
    for y in range(rows):
        line = []
        for x in range(words * 16):
            index = 0
            for plane in range(PLANES):
                at = base + plane * PLANE_STRIDE + y * words * 2 + (x // 16) * 2
                if at + 1 >= len(memory):
                    break
                word = (memory[at] << 8) | memory[at + 1]
                index |= ((word >> (15 - (x % 16))) & 1) << plane
            line.append(index)
        out.append(line)
    return out


def coherence(pixels):
    """Real art has neighbouring pixels alike; a wrong decode does not."""
    same = total = 0
    for y, row in enumerate(pixels):
        for x, value in enumerate(row):
            if x + 1 < len(row):
                total += 1
                same += value == row[x + 1]
            if y + 1 < len(pixels):
                total += 1
                same += value == pixels[y + 1][x]
    return same / max(1, total)


def write_png(path, pixels, palette, background, scale=4):
    height, width = len(pixels) * scale, len(pixels[0]) * scale
    raw = bytearray()
    for row in pixels:
        line = bytearray([0])
        for index in row:
            r, g, b = palette[index % 32]
            line += bytes([r, g, b, 0 if index == background else 255]) * scale
        raw += line * scale

    def chunk(tag, data):
        payload = tag + data
        return (struct.pack(">I", len(data)) + payload +
                struct.pack(">I", zlib.crc32(payload) & 0xffffffff))

    with open(path, "wb") as handle:
        handle.write(b"\x89PNG\r\n\x1a\n" +
                     chunk(b"IHDR", struct.pack(">IIBBBBB", width, height,
                                                8, 6, 0, 0, 0)) +
                     chunk(b"IDAT", zlib.compress(bytes(raw), 9)) +
                     chunk(b"IEND", b""))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("memory")
    parser.add_argument("blits")
    parser.add_argument("out")
    parser.add_argument("--min-coherence", type=float, default=0.55)
    args = parser.parse_args()

    memory = open(args.memory, "rb").read()
    palette = load_palette(args.memory + ".pal")

    wanted = set()
    for line in open(args.blits):
        parts = line.split()
        if len(parts) < 3:
            continue
        source, words, rows = int(parts[0], 16), int(parts[1]), int(parts[2])
        if ART_LOW <= source < ART_HIGH and 1 <= words <= 4 and 4 <= rows <= 64:
            wanted.add((source, words, rows))

    os.makedirs(args.out, exist_ok=True)
    seen, written, rejected = set(), 0, 0
    for source, words, rows in sorted(wanted):
        pixels = decode(memory, source, words, rows)
        flat = [v for row in pixels for v in row]
        if len(set(flat)) < 4:
            continue
        if coherence(pixels) < args.min_coherence:
            rejected += 1
            continue
        key = tuple(flat)
        if key in seen:
            continue
        seen.add(key)
        edge = Counter([pixels[0][x] for x in range(len(pixels[0]))] +
                       [pixels[-1][x] for x in range(len(pixels[0]))])
        write_png(os.path.join(args.out,
                               f"{words * 16:02d}x{rows:02d}_{source:06x}.png"),
                  pixels, palette, edge.most_common(1)[0][0])
        written += 1
    print(f"{len(wanted)} art sources, wrote {written} sprites to {args.out}/ "
          f"({rejected} rejected as incoherent)")


if __name__ == "__main__":
    main()
