#!/usr/bin/env python3
"""Depack Battle Squadron's backward BOND streams.

This is a direct, bounds-checked translation of the MC68000 routine at $AB46
in LOADER. Packed streams end with a bit reservoir, XOR checksum, and unpacked
length. Both input and output are consumed backwards, which permits in-place
decompression on the Amiga.
"""

from __future__ import annotations

import argparse
import struct
from pathlib import Path


class BondStream:
    def __init__(self, packed: bytes):
        if len(packed) < 12 or len(packed) & 1:
            raise ValueError("BOND stream is too short or oddly aligned")
        self.packed = packed
        self.input = len(packed)
        self.output_size = self.read_long()
        if not self.output_size or self.output_size > 16 * 1024 * 1024:
            raise ValueError(f"implausible output length {self.output_size}")
        self.output = bytearray(self.output_size)
        self.destination = self.output_size
        self.checksum = self.read_long()
        self.bits = self.read_long()
        self.checksum ^= self.bits

    def read_long(self) -> int:
        self.input -= 4
        if self.input < 0:
            raise ValueError("BOND input underflow")
        return struct.unpack_from(">I", self.packed, self.input)[0]

    def shift_bit(self) -> int:
        carry = self.bits & 1
        self.bits >>= 1
        if self.bits == 0:
            reservoir = self.read_long()
            self.checksum ^= reservoir
            carry = reservoir & 1
            self.bits = 0x80000000 | (reservoir >> 1)
        return carry

    def get_bits(self, count: int) -> int:
        value = 0
        for _ in range(count):
            value = (value << 1) | self.shift_bit()
        return value

    def literal(self, count: int) -> None:
        if count > self.destination:
            raise ValueError("literal run exceeds output buffer")
        for _ in range(count):
            self.destination -= 1
            self.output[self.destination] = self.get_bits(8)

    def copy(self, count: int, offset: int) -> None:
        if count > self.destination:
            raise ValueError("match exceeds output buffer")
        for _ in range(count):
            self.destination -= 1
            source = self.destination + offset
            if not self.destination < source < len(self.output):
                raise ValueError(
                    f"invalid backward match source {source:#x} at "
                    f"destination {self.destination:#x}"
                )
            self.output[self.destination] = self.output[source]

    def depack(self) -> bytes:
        while self.destination:
            if self.shift_bit():
                selector = self.get_bits(2)
                if selector < 2:
                    self.copy(selector + 3, self.get_bits(9 + selector))
                elif selector == 2:
                    count = self.get_bits(8) + 1
                    self.copy(count, self.get_bits(12))
                else:
                    self.literal(self.get_bits(8) + 9)
            elif self.shift_bit():
                self.copy(2, self.get_bits(8))
            else:
                self.literal(self.get_bits(3) + 1)

        if self.checksum:
            raise ValueError(f"BOND checksum mismatch: {self.checksum:08x}")
        return bytes(self.output)


def depack(packed: bytes) -> bytes:
    return BondStream(packed).depack()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    unpacked = depack(args.source.read_bytes())
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(unpacked)
    print(f"{args.source.name}: wrote {len(unpacked)} bytes to {args.output}")


if __name__ == "__main__":
    main()
