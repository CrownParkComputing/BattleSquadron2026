#!/usr/bin/env python3
"""Focused regression tests for conservative 68000 code discovery."""

from __future__ import annotations

import unittest

from discover_code import direct_target, discover, ranges


class DirectTargetTests(unittest.TestCase):
    def test_absolute_target(self) -> None:
        self.assertEqual(direct_target("$24c6e.l"), 0x24C6E)

    def test_branch_target(self) -> None:
        self.assertEqual(direct_target("$1684"), 0x1684)

    def test_register_relative_call_is_not_absolute(self) -> None:
        self.assertIsNone(direct_target("$34(a2)"))

    def test_immediate_value_is_not_a_control_flow_target(self) -> None:
        self.assertIsNone(direct_target("#$24c6e"))


class DiscoveryTests(unittest.TestCase):
    def test_absolute_call_target_is_followed(self) -> None:
        data = bytearray(0x20)
        data[0:6] = bytes.fromhex("4eb900000010")  # JSR $10.l
        data[6:8] = bytes.fromhex("4e75")          # RTS
        data[0x10:0x12] = bytes.fromhex("4e75")
        found = discover(bytes(data), 0, [0])
        self.assertEqual(set(found), {0, 6, 0x10})

    def test_register_relative_call_is_not_followed(self) -> None:
        data = bytearray(0x40)
        data[0:4] = bytes.fromhex("4eaa0034")      # JSR $34(A2)
        data[4:6] = bytes.fromhex("4e75")
        data[0x34:0x36] = bytes.fromhex("4e75")
        found = discover(bytes(data), 0, [0])
        self.assertEqual(set(found), {0, 4})

    def test_ranges_merge_only_adjacent_instructions(self) -> None:
        self.assertEqual(ranges({0x100: 2, 0x102: 4, 0x110: 2}),
                         [(0x100, 0x106), (0x110, 0x112)])


if __name__ == "__main__":
    unittest.main()
