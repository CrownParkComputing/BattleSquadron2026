#!/usr/bin/env python3
"""Rip Hybris' artwork out of chip RAM, using the blitter as the index.

Guessing at the layout of a packed data file is hopeless; watching what the
blitter actually reads is not.  `BS_DUMP_BLITS=file build/hybris` logs every
distinct (source, width, height) the blitter fetched, which is exactly the
address and size of every piece of art the game drew.  This turns each of
those into a PNG, using the palette captured alongside the memory dump.

Hybris' own format, found by reading the blit DESTINATIONS rather than
guessing: 5 planes, each plane PADDED to 128 bytes regardless of the frame's
real size, rows 4 bytes apart.  Decoding at the frame size instead of 128
gives convincing noise, which is what made this take a while.

    BS_DUMP_BLITS=build/blits.txt ./build/hybris --frames 3000 \\
        --fire-from 700 --dump build/mem_gfx.bin
    tools/extract_hybris_gfx.py build/mem_gfx.bin build/blits.txt build/gfx
"""
import argparse
import os
import struct
import sys

CHIP_SIZE = 0x80000


def load_palette(path):
    """32 Amiga $0RGB words -> RGB triples.  Falls back to a grey ramp."""
    try:
        with open(path, "rb") as handle:
            raw = handle.read(64)
    except OSError:
        return [(i * 8, i * 8, i * 8) for i in range(32)]
    palette = []
    for i in range(32):
        word = struct.unpack(">H", raw[i * 2:i * 2 + 2])[0]
        r = (word >> 8) & 15
        g = (word >> 4) & 15
        b = word & 15
        palette.append((r * 17, g * 17, b * 17))
    return palette


def planar_pixels(memory, base, words, rows, planes, interleaved, stride=None,
                  plane_stride=None):
    """Decode a planar block into a list of palette indices per row.

    `stride` is the bytes between rows: a frame cut out of a sprite SHEET is
    blitted with a modulo, and without it every row is read from the wrong
    place and the result is noise."""
    out = []
    if stride is None:
        stride = words * 2
    if plane_stride is None:
        plane_stride = stride * rows         # sequential, planes packed
    for y in range(rows):
        line = []
        for x in range(words * 16):
            index = 0
            for p in range(planes):
                if interleaved:
                    at = base + ((y * planes) + p) * stride + (x // 16) * 2
                else:
                    at = base + p * plane_stride + y * stride + (x // 16) * 2
                if at + 1 >= len(memory):
                    continue
                word = (memory[at] << 8) | memory[at + 1]
                bit = (word >> (15 - (x % 16))) & 1
                index |= bit << p
            line.append(index)
        out.append(line)
    return out


def write_png(path, pixels, palette, scale=3):
    """Minimal PNG writer: no dependencies, transparent index 0."""
    import zlib
    height = len(pixels) * scale
    width = len(pixels[0]) * scale
    raw = bytearray()
    for row in pixels:
        line = bytearray([0])
        for index in row:
            r, g, b = palette[index % len(palette)]
            alpha = 0 if index == 0 else 255
            line += bytes([r, g, b, alpha]) * scale
        raw += line * scale
    def chunk(tag, data):
        payload = tag + data
        return (struct.pack(">I", len(data)) + payload +
                struct.pack(">I", zlib.crc32(payload) & 0xffffffff))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("memory")
    parser.add_argument("blits")
    parser.add_argument("out")
    parser.add_argument("--shape", default="2x24",
                        help="blit shape to rip, WORDSxROWS (default 2x24)")
    parser.add_argument("--planes", type=int, default=5)
    parser.add_argument("--plane-stride", type=int, default=128,
                        help="bytes between planes.  NOT the frame size: "
                             "Hybris pads each 32-wide plane to 128 bytes, "
                             "which is why decoding at the frame size gives "
                             "noise.  The real value comes from the blit "
                             "DESTINATIONS: consecutive plane blits land "
                             "$4000 apart (one 256x512 bitplane) from sources "
                             "128 apart.")
    parser.add_argument("--limit", type=int, default=120)
    args = parser.parse_args()

    with open(args.memory, "rb") as handle:
        memory = handle.read()
    palette = load_palette(args.memory + ".pal")
    want_words, want_rows = (int(v) for v in args.shape.split("x"))

    sources = []
    with open(args.blits) as handle:
        for line in handle:
            parts = line.split()
            if len(parts) < 3:
                continue
            source, words, rows = int(parts[0], 16), int(parts[1]), int(parts[2])
            modulo = int(parts[4]) if len(parts) > 4 else 0
            if words == want_words and rows == want_rows:
                sources.append((source, modulo))
    sources = sorted(set(sources))
    if not sources:
        sys.exit(f"no {args.shape} blits in {args.blits}")

    os.makedirs(args.out, exist_ok=True)
    written = 0
    for source, modulo in sources[:args.limit]:
        for interleaved in (False, True):
            rows = want_rows // args.planes if interleaved else want_rows
            if rows < 1:
                continue
            pixels = planar_pixels(memory, source, want_words, rows,
                                   args.planes, interleaved,
                                   want_words * 2 + modulo,
                                   args.plane_stride)
            if not any(any(row) for row in pixels):
                continue                      # nothing but background
            kind = "interleaved" if interleaved else "sequential"
            write_png(os.path.join(args.out, f"{source:06x}_m{modulo}_{kind}.png"),
                      pixels, palette)
            written += 1
    print(f"{len(sources)} sources of {args.shape}, wrote {written} PNGs "
          f"to {args.out}/ ({args.planes} planes, both layouts)")


if __name__ == "__main__":
    main()
