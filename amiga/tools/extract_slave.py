#!/usr/bin/env python3
"""Extract and describe the single code hunk in a WHDLoad slave."""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


HUNK_HEADER = 0x3F3
HUNK_CODE = 0x3E9
HUNK_END = 0x3F2


def be16(data: bytes, offset: int) -> int:
    return struct.unpack_from(">H", data, offset)[0]


def be32(data: bytes, offset: int) -> int:
    return struct.unpack_from(">I", data, offset)[0]


def extract(source: bytes) -> bytes:
    if len(source) < 36 or be32(source, 0) != HUNK_HEADER:
        raise ValueError("not an Amiga HUNK executable")
    if be32(source, 4) != 0:
        raise ValueError("resident-library names are not supported")
    if (be32(source, 8), be32(source, 12), be32(source, 16)) != (1, 0, 0):
        raise ValueError("expected a single-hunk WHDLoad slave")
    allocation_longs = be32(source, 20) & 0x3FFFFFFF
    if be32(source, 24) != HUNK_CODE:
        raise ValueError("slave's first hunk is not HUNK_CODE")
    code_longs = be32(source, 28)
    if code_longs > allocation_longs:
        raise ValueError("code hunk exceeds its allocation")
    end = 32 + code_longs * 4
    if be32(source, end) != HUNK_END or end + 4 != len(source):
        raise ValueError("unexpected data after slave code hunk")
    return source[32:end]


def describe(code: bytes) -> None:
    if code[4:12] != b"WHDLOADS":
        raise ValueError("code hunk has no WHDLOADS signature")
    fields = {
        "required WHDLoad": be16(code, 12),
        "flags": f"${be16(code, 14):04x}",
        "base memory": f"${be32(code, 16):08x}",
        "entry": f"${be16(code, 24):04x}",
        "current directory": f"${be16(code, 26):04x}",
        "exit key": f"${code[31]:02x}",
        "expansion memory": f"${be32(code, 32):08x}",
    }
    for name, value in fields.items():
        print(f"{name}: {value}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    code = extract(args.source.read_bytes())
    describe(code)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(code)
    print(f"wrote {len(code)} bytes to {args.output}")


if __name__ == "__main__":
    main()
