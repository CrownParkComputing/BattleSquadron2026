#!/usr/bin/env python3
"""Compare two PPM frames, or print one region of them side by side as ASCII.

    python3 tools/ppm_diff.py ref.ppm ours.ppm
    python3 tools/ppm_diff.py ref.ppm ours.ppm --at 79 76 --size 56 40

Records tell you what the game thinks; this tells you what it drew.  Use it
when a record matches the reference but the picture does not -- a wrong sprite
frame, a sheared blit or a layer drawn at the wrong scroll offset are all
invisible to the state dumps.
"""

from __future__ import annotations

import argparse
import sys

RAMP = " .:-=+*#%@"


def read_ppm(path):
    with open(path, "rb") as handle:
        data = handle.read()
    if not data.startswith(b"P6"):
        raise ValueError(f"{path} is not a binary PPM")
    fields, offset = [], 2
    while len(fields) < 3:
        while offset < len(data) and data[offset:offset + 1].isspace():
            offset += 1
        if data[offset:offset + 1] == b"#":
            while data[offset:offset + 1] not in (b"\n", b""):
                offset += 1
            continue
        start = offset
        while offset < len(data) and not data[offset:offset + 1].isspace():
            offset += 1
        fields.append(int(data[start:offset]))
    offset += 1
    width, height, _maximum = fields
    return width, height, data[offset:offset + width * height * 3]


def pixel(pixels, width, x, y):
    index = (y * width + x) * 3
    return pixels[index], pixels[index + 1], pixels[index + 2]


def ascii_block(pixels, width, height, x0, y0, w, h):
    rows = []
    for y in range(y0, min(y0 + h, height)):
        row = ""
        for x in range(x0, min(x0 + w, width)):
            r, g, b = pixel(pixels, width, x, y)
            row += RAMP[min(len(RAMP) - 1, (r + g + b) * len(RAMP) // 766)]
        rows.append(row)
    return rows


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("reference")
    parser.add_argument("ours")
    parser.add_argument("--at", nargs=2, type=int, metavar=("X", "Y"),
                        help="top-left of the region to show")
    parser.add_argument("--size", nargs=2, type=int, default=[64, 40],
                        metavar=("W", "H"))
    args = parser.parse_args(argv)

    rw, rh, ref = read_ppm(args.reference)
    ow, oh, ours = read_ppm(args.ours)
    if (rw, rh) != (ow, oh):
        print(f"size mismatch: reference {rw}x{rh}, ours {ow}x{oh}")
        return 2

    differing = sum(1 for i in range(0, len(ref), 3)
                    if ref[i:i + 3] != ours[i:i + 3])
    total = rw * rh
    print(f"{differing}/{total} pixels differ "
          f"({100.0 * differing / total:.1f}%)")

    if not args.at:
        # Point at the densest patch of difference so the caller has somewhere
        # to look without knowing the answer in advance.
        best, best_at = -1, (0, 0)
        for y in range(0, rh - 16, 8):
            for x in range(0, rw - 16, 8):
                count = 0
                for yy in range(y, y + 16):
                    base = (yy * rw + x) * 3
                    for xx in range(16):
                        o = base + xx * 3
                        if ref[o:o + 3] != ours[o:o + 3]:
                            count += 1
                if count > best:
                    best, best_at = count, (x, y)
        print(f"densest 16x16 difference at {best_at} ({best} pixels)")
        return 0 if differing == 0 else 1

    x0, y0 = args.at
    w, h = args.size
    left = ascii_block(ref, rw, rh, x0, y0, w, h)
    right = ascii_block(ours, ow, oh, x0, y0, w, h)
    print(f"\nreference{' ' * (w - 9)}  |  ours   (at {x0},{y0} {w}x{h})")
    for a, b in zip(left, right):
        print(f"{a}  |  {b}")
    return 0 if differing == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
