#!/usr/bin/env python3
"""Unit tests for the loader-owned overlay descriptor table."""

from __future__ import annotations

import struct
import unittest

from extract_modules import TABLE_FILE_OFFSET, descriptors, s32


def descriptor(name: str, address: int, size: int, staging: int, mode: int) -> bytes:
    return (
        struct.pack(">IIII", address, size, staging, mode & 0xFFFFFFFF)
        + name.encode("ascii").ljust(8, b"\0")
    )


class DescriptorTests(unittest.TestCase):
    def test_signed_mode_conversion(self) -> None:
        self.assertEqual(s32(0xFFFFFFFE), -2)
        self.assertEqual(s32(2), 2)

    def test_table_stops_at_lodsav(self) -> None:
        loader = bytearray(TABLE_FILE_OFFSET)
        loader.extend(descriptor("LODGAM", 0x246F0, 29200, 0x3BA00, -2))
        loader.extend(descriptor("LODSAV", 0x62000, 8192, 0x3F200, 2))
        loader.extend(descriptor("IGNORED", 1, 2, 3, 4))
        self.assertEqual(
            list(descriptors(bytes(loader))),
            [
                ("LODGAM", 0x246F0, 29200, 0x3BA00, -2),
                ("LODSAV", 0x62000, 8192, 0x3F200, 2),
            ],
        )

    def test_missing_terminal_descriptor_is_rejected(self) -> None:
        loader = bytearray(TABLE_FILE_OFFSET)
        loader.extend(descriptor("LODGAM", 0x246F0, 29200, 0x3BA00, -2))
        with self.assertRaisesRegex(ValueError, "did not end"):
            list(descriptors(bytes(loader)))


if __name__ == "__main__":
    unittest.main()
