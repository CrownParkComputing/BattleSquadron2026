#!/usr/bin/env python3
"""Unit tests for the bounds-checked BOND translation."""

from __future__ import annotations

import struct
import unittest

from depack_bond import BondStream, depack


def footer(bits: int, checksum: int, output_size: int) -> bytes:
    # BOND consumes its three footer longs backwards.
    return struct.pack(">III", bits, checksum, output_size)


def one_byte_literal(value: int) -> bytes:
    # Control 00, literal length 000 (= one byte), then the byte MSB first.
    sequence = [0, 0, 0, 0, 0]
    sequence.extend((value >> bit) & 1 for bit in range(7, -1, -1))
    bits = 1 << 31                    # reservoir sentinel
    for bit, value_bit in enumerate(sequence):
        bits |= value_bit << bit
    return footer(bits, bits, 1)      # footer XOR makes checksum zero


class BondStreamTests(unittest.TestCase):
    def test_single_literal_stream(self) -> None:
        self.assertEqual(depack(one_byte_literal(0xA5)), b"\xA5")

    def test_short_and_odd_streams_are_rejected(self) -> None:
        for packed in (b"", b"\0" * 11, b"\0" * 13):
            with self.subTest(length=len(packed)):
                with self.assertRaisesRegex(ValueError, "too short|aligned"):
                    BondStream(packed)

    def test_zero_output_length_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "output length"):
            BondStream(footer(1, 1, 0))

    def test_input_underflow_is_reported(self) -> None:
        stream = BondStream(footer(0, 0, 1))
        with self.assertRaisesRegex(ValueError, "input underflow"):
            stream.shift_bit()

    def test_literal_output_overflow_is_reported(self) -> None:
        stream = BondStream(footer(1, 1, 1))
        with self.assertRaisesRegex(ValueError, "literal run"):
            stream.literal(2)

    def test_invalid_match_source_is_reported(self) -> None:
        stream = BondStream(footer(1, 1, 1))
        with self.assertRaisesRegex(ValueError, "match source"):
            stream.copy(1, 0)

    def test_checksum_mismatch_is_reported(self) -> None:
        stream = BondStream(footer(1, 0, 1))
        stream.destination = 0
        with self.assertRaisesRegex(ValueError, "checksum mismatch"):
            stream.depack()


if __name__ == "__main__":
    unittest.main()
