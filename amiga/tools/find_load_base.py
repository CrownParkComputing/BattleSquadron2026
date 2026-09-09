#!/usr/bin/env python3
"""Recover the load address of a WHDLoad game module.

The discover stage needs to know where a module sits in memory.  Guessing a
round number is not good enough and, worse, the guess is not self-announcing:
running Hybris's data/04 at $3C000 reported 14.0% coverage while the correct
$3C084 reported 5.5%.  Coverage measures how far a walk got before hitting an
invalid opcode, not whether it was ever decoding real instructions.

What does discriminate is CROSS-REFERENCE CONSISTENCY.  A module's own absolute
calls have to land on the instruction boundaries of the same module, and its
routines call each other, so at the true base the entry points knit together
and at a wrong one they decode into noise (on 68000, characteristically a run
of ``ori.b #imm,d3`` as $00xx byte pairs are misread as opcodes).

    python3 tools/find_load_base.py MODULE
    python3 tools/find_load_base.py MODULE --entry 0x3c0c6 --entry 0x3ea96
    python3 tools/find_load_base.py MODULE --top 5

With no --entry the module is assumed to open with a `JMP abs.l` table, which
is how Amiga overlays usually publish their entry points.
"""

from __future__ import annotations

import argparse
import struct
import sys
from pathlib import Path

try:
    from capstone import Cs, CS_ARCH_M68K, CS_MODE_BIG_ENDIAN, CS_MODE_M68K_000
except ImportError:
    print("capstone is required: pip install capstone", file=sys.stderr)
    raise SystemExit(2)

JMP_ABS = 0x4EF9
JSR_ABS = 0x4EB9
MAX_INSNS = 32
# A hit on another known entry point is worth far more than a few extra
# decoded instructions: it is two independent references agreeing.
CROSSREF_BONUS = 25


def leading_jump_table(data: bytes) -> list[int]:
    """Read a run of `JMP abs.l` from the start of the module."""
    entries = []
    offset = 0
    while offset + 6 <= len(data):
        opcode, target = struct.unpack_from(">HI", data, offset)
        if opcode != JMP_ABS:
            break
        entries.append(target)
        offset += 6
    return entries


def absolute_calls(data: bytes) -> list[int]:
    """Every `JSR abs.l` / `JMP abs.l` operand anywhere in the module.

    A linear scan over unaligned data finds false positives; that is fine,
    they are only used to bound and score candidate bases.
    """
    found = []
    for offset in range(0, len(data) - 6, 2):
        opcode, target = struct.unpack_from(">HI", data, offset)
        if opcode in (JMP_ABS, JSR_ABS):
            found.append(target)
    return found


def operand_addresses(op_str: str):
    """Absolute/branch targets capstone printed for one instruction."""
    for token in op_str.replace(",", " ").split():
        token = token.strip("()").lstrip("$")
        if token.endswith(".l") or token.endswith(".w"):
            token = token[:-2]
        if token.startswith("0x"):
            token = token[2:]
        if not token or any(c not in "0123456789abcdefABCDEF" for c in token):
            continue
        try:
            yield int(token, 16)
        except ValueError:
            continue


CONTROL_FLOW = ("bra", "bsr", "jmp", "jsr", "beq", "bne", "bcc", "bcs",
                "bge", "bgt", "ble", "blt", "bhi", "bls", "bmi", "bpl",
                "bvc", "bvs", "dbra", "dbf")
# $00xx decodes as ORI.B #imm,Dn.  Real code almost never opens a routine with
# it; a misaligned walk produces runs of them, because address longwords like
# $0003C0C6 supply the $00 bytes.  Four of Hybris's seven entries began with
# one at the wrong base and none did at the right one.
MISALIGN_SIGNATURE = ("ori", "andi", "eori", "cmpi")
BOUNDARY_BONUS = 40
# A published entry point that decodes to ORI/ANDI/EORI/CMPI with a byte
# immediate is near-proof of a wrong base -- those come from the $00 bytes of
# address longwords.  Weighting this like a cross-reference was not enough:
# Hybris's data/07 elected a base $5C too high until this outvoted it, while
# the slave named $3AFB8 outright.
MISALIGN_PENALTY = 90
BOUNDARY_PENALTY = 60
NEAR_ENTRY = 512


def boundary_score(md, data: bytes, base: int, entries: list[int]) -> int:
    """Do the entry points sit on each other's instruction boundaries?

    Disassembling from one entry towards the next must arrive exactly on it.
    At a wrong base the walk sails straight through, landing mid-instruction.
    This is the test that does not care how far a walk got, only whether two
    independent facts about the module agree.
    """
    score = 0
    ordered = sorted(entries)
    for first, second in zip(ordered, ordered[1:]):
        span = second - first
        if not 0 < span <= NEAR_ENTRY:
            continue
        offset = first - base
        if not 0 <= offset < len(data):
            continue
        landed = False
        overshot = False
        for insn in md.disasm(data[offset:offset + span + 16], first):
            if insn.address == second:
                landed = True
                break
            if insn.address > second:
                overshot = True
                break
        if landed:
            score += BOUNDARY_BONUS
        elif overshot:
            score -= BOUNDARY_PENALTY
    return score


def score_base(md, data: bytes, base: int, entries: list[int],
               table_bytes: int) -> tuple[int, int, int]:
    """Return (score, crossrefs, decoded) for placing the module at `base`.

    The instruction count is deliberately NOT part of the score.  Rewarding a
    long walk is what makes a wrong base look good: on Hybris's data/04 the
    wrong $3C000 decoded MORE instructions than the correct $3C084, because
    68000 happily decodes noise.  Only agreement between two independent
    references counts, and matches inside the leading JMP table are ignored --
    the table trivially references every entry, so scoring it would just
    elect the first entry point as the base.
    """
    entry_set = set(entries)
    crossrefs = 0
    decoded = 0
    dead = 0
    misaligned = 0
    for entry in entries:
        offset = entry - base
        if not 0 <= offset < len(data):
            return -1, 0, 0
        window = data[offset:offset + MAX_INSNS * 8]
        count = 0
        for insn in md.disasm(window, entry):
            count += 1
            if count == 1 and insn.mnemonic.split(".")[0] in MISALIGN_SIGNATURE:
                misaligned += 1
            # Only control flow counts.  Scanning every operand of every
            # instruction lets noise match by coincidence, which is how an
            # earlier version of this elected a base 1946 bytes too low.
            if (insn.mnemonic.split(".")[0] in CONTROL_FLOW
                    and offset + (insn.address - entry) >= table_bytes):
                for value in operand_addresses(insn.op_str):
                    if value in entry_set and value != entry:
                        crossrefs += 1
            if count >= MAX_INSNS:
                break
        if count == 0:
            dead += 1
        decoded += count
    score = (boundary_score(md, data, base, entries)
             + crossrefs * CROSSREF_BONUS
             - dead * CROSSREF_BONUS
             - misaligned * MISALIGN_PENALTY)
    return score, crossrefs, decoded


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("module", help="the module file as loaded from disk")
    parser.add_argument("--entry", action="append", default=[],
                        help="a known absolute entry point (repeatable)")
    parser.add_argument("--top", type=int, default=3,
                        help="how many candidates to report (default 3)")
    parser.add_argument("--step", type=int, default=2,
                        help="candidate base alignment (default 2)")
    args = parser.parse_args(argv)

    data = Path(args.module).read_bytes()
    entries = [int(e, 0) for e in args.entry] or leading_jump_table(data)
    if not entries:
        print("no entry points: pass --entry, the module has no JMP table",
              file=sys.stderr)
        return 2
    print(f"{Path(args.module).name}: {len(data)} bytes (${len(data):x}), "
          f"{len(entries)} entry points")
    for entry in entries:
        print(f"  entry ${entry:06x}")

    # Every entry has to fall inside the module, which bounds the search.
    low = max(0, max(entries) - len(data) + 1)
    high = min(entries)
    low -= low % args.step
    table_bytes = len(leading_jump_table(data)) * 6
    md = Cs(CS_ARCH_M68K, CS_MODE_BIG_ENDIAN | CS_MODE_M68K_000)

    results = []
    for base in range(low, high + 1, args.step):
        score, crossrefs, decoded = score_base(md, data, base, entries,
                                               table_bytes)
        if score > -CROSSREF_BONUS * len(entries):
            results.append((score, crossrefs, base, decoded))
    if not results:
        print("no base places every entry point inside the module",
              file=sys.stderr)
        return 1
    # Prefer the higher score; break ties toward the LOWEST base, which is the
    # earliest placement consistent with the evidence.
    results.sort(key=lambda r: (-r[0], r[2]))

    print(f"\nsearched ${low:06x}..${high:06x} "
          f"({len(results)} candidates)\n")
    for score, crossrefs, base, decoded in results[:args.top]:
        print(f"  base ${base:06x}  score {score:5d}  "
              f"{crossrefs} cross-references  "
              f"({decoded} instructions decoded)")
    best = results[0]
    runner = results[1] if len(results) > 1 else None
    print(f"\nbest: ${best[2]:06x}")
    if best[1] == 0:
        print("  WARNING: no entry point referenced another.  This module may "
              "not call itself; confirm the base against the WHDLoad slave.")
    elif runner and best[0] - runner[0] < BOUNDARY_BONUS:
        print(f"  WARNING: only {best[0] - runner[0]} ahead of "
              f"${runner[2]:06x}; confirm against the slave.  A WHDLoad slave "
              f"usually names its load addresses outright, e.g. Hybris's has "
              f"`cmpa.l #$3c084,a3` before applying a patch list.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
