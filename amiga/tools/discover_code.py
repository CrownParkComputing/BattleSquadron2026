#!/usr/bin/env python3
"""Conservatively discover reachable MC68000 code in a raw overlay.

This is intentionally a recursive traversal rather than a linear sweep: game
overlays mix routines, graphics, lookup tables, and music in the same file.
Only direct branch/call targets are followed. Indirect jump-table entries can
be supplied as additional --entry arguments once they are proven.
"""

from __future__ import annotations

import argparse
import re
from collections import deque
from pathlib import Path

from capstone import CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000, Cs


# Capstone formats an absolute control-flow destination as ``$1234`` or
# ``$1234.l``. Register-relative calls use superficially similar text such as
# ``$34(a2)``; accepting the first hexadecimal token from those operands would
# incorrectly turn a library-vector displacement into an entry in this image.
TARGET_RE = re.compile(r"^\$([0-9a-fA-F]+)(?:\.[bwl])?$")
CONDITIONAL_BRANCHES = {
    "bhi", "bls", "bcc", "bcs", "bne", "beq", "bvc", "bvs",
    "bpl", "bmi", "bge", "blt", "bgt", "ble",
}
TERMINATORS = {"rts", "rte", "rtr", "stop", "illegal", "trapv"}


def direct_target(op_str: str) -> int | None:
    match = TARGET_RE.fullmatch(op_str.strip())
    return int(match.group(1), 16) if match else None


def discover(data: bytes, base: int, entries: list[int]) -> dict[int, int]:
    end = base + len(data)
    decoder = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)
    decoder.detail = False
    pending = deque(entries)
    visited: dict[int, int] = {}

    def enqueue(address: int | None) -> None:
        if address is not None and base <= address < end and not address & 1:
            pending.append(address)

    while pending:
        pc = pending.popleft()
        while base <= pc < end and pc not in visited:
            decoded = list(decoder.disasm(data[pc - base :], pc, count=1))
            if not decoded:
                break
            insn = decoded[0]
            if insn.address != pc or insn.size <= 0 or pc + insn.size > end:
                break
            visited[pc] = insn.size
            mnemonic = insn.mnemonic.split(".", 1)[0]
            target = direct_target(insn.op_str)

            if mnemonic in {"bsr", "jsr"}:
                enqueue(target)
            elif mnemonic in CONDITIONAL_BRANCHES or mnemonic.startswith("db"):
                enqueue(target)
            elif mnemonic in {"bra", "jmp"}:
                enqueue(target)
                break

            pc += insn.size
            if mnemonic in TERMINATORS:
                break
    return visited


def ranges(instructions: dict[int, int]) -> list[tuple[int, int]]:
    result: list[tuple[int, int]] = []
    for address in sorted(instructions):
        end = address + instructions[address]
        if result and result[-1][1] == address:
            result[-1] = (result[-1][0], end)
        else:
            result.append((address, end))
    return result


def number(value: str) -> int:
    return int(value, 0)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("binary", type=Path)
    parser.add_argument("--base", required=True, type=number)
    parser.add_argument("--entry", required=True, action="append", type=number)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    data = args.binary.read_bytes()
    found = discover(data, args.base, args.entry)
    code_ranges = ranges(found)
    lines = ["MACHINE 68000", f"OFFSET ${args.base:08X}"]
    lines.extend(f"ENTRY ${entry:08X}" for entry in args.entry)
    lines.extend(f"CODE ${start:08X} - ${end:08X}" for start, end in code_ranges)
    lines.append("END")
    args.output.write_text("\n".join(lines) + "\n")
    covered = sum(size for size in found.values())
    print(
        f"{args.binary.name}: {len(found)} instructions, {covered} code bytes, "
        f"{len(code_ranges)} ranges ({covered / len(data):.1%})"
    )


if __name__ == "__main__":
    main()
