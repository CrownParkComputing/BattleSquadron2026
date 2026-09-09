#!/usr/bin/env python3
"""Diff two Battle Squadron state dumps (native --dump-state vs recomp dump).

Usage: python3 tools/parity_diff.py REF.txt OURS.txt

Both files are a sequence of frames; each frame starts with a ``scroll=`` line
followed by a ``state frame=...`` line and up to 18 ``obj`` / 12 ``hos`` lines.
Two differences are normalised:

* ``frame=`` values differ (the native counts from boot, the recomp from the
  first live-game frame), so they are rewritten to ``frame=N``.
* The native runs the 50 Hz display loop while the game logic updates at
  25 Hz, so one recomp frame corresponds to two native frames.  The driver
  auto-detects the step rate (1:1 or 2:1) that gives the longest matching
  prefix and reports the first divergence under that rate.

Exit codes: 0 = aligned and equal, 1 = divergences found (or the comparison
was truncated), 2 = could not align (or a usage error).
"""

import re
import sys

FRAME_RE = re.compile(r"frame=\d+")


def load_frames(path):
    frames = []
    current = []
    with open(path, "r", encoding="utf-8") as fh:
        for line in fh:
            if line.startswith("scroll="):
                if current:
                    frames.append(current)
                current = []
            current.append(FRAME_RE.sub("frame=N", line))
    if current:
        frames.append(current)
    return frames


def align(ref, ours):
    """Return (offset, rate) so ref[offset + rate*i] == ours[i] for the
    longest prefix, or (-1, 1) if no alignment can be found."""
    if not ours:
        return -1, 1
    best_offset, best_rate, best_n = -1, 1, 0
    for rate in (1, 2):
        for offset in range(len(ref)):
            if ref[offset] != ours[0]:
                continue
            n = 0
            while (n < len(ours) and offset + rate * n < len(ref)
                   and ref[offset + rate * n] == ours[n]):
                n += 1
            if n > best_n:
                best_offset, best_rate, best_n = offset, rate, n
    return best_offset, best_rate


RESYNC_WINDOW = 3


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    ref = load_frames(argv[1])
    ours = load_frames(argv[2])
    if not ref or not ours:
        print("FATAL: one or both dumps are empty.")
        return 2
    offset, rate = align(ref, ours)
    if offset < 0:
        print("FATAL: could not align the recomp dump against the native log.")
        return 2
    print(f"Sync: recomp frame 0 == native frame {offset} "
          f"(rate {rate}:1; native had {len(ref)} frames, recomp {len(ours)}).")

    divergences = []
    resyncs = []
    compared = 0
    exhausted = False
    drift = 0
    for i in range(len(ours)):
        idx = offset + rate * i + drift
        if idx >= len(ref):
            print(f"\nNative log exhausted after {i} aligned frames.")
            exhausted = True
            break
        compared = i + 1
        if ours[i] == ref[idx]:
            continue
        # The native runner's 25 Hz logic cycle is normally two PAL frames,
        # but it is raster driven and occasionally takes three -- a real slip
        # was observed at native frame 1856.  A fixed 2:1 walk then reports
        # every later frame as divergent and buries the genuine differences,
        # so look for the walk having slipped before calling it a divergence.
        recovered = None
        for step in range(-RESYNC_WINDOW, RESYNC_WINDOW + 1):
            if step == 0:
                continue
            probe = idx + step
            if 0 <= probe < len(ref) and ours[i] == ref[probe]:
                recovered = step
                break
        if recovered is not None:
            drift += recovered
            resyncs.append((i, idx, recovered))
            continue
        divergences.append((i, idx, ref[idx], ours[i]))

    if resyncs:
        print(f"\nRe-synchronised {len(resyncs)} time(s) where the native log "
              f"slipped a PAL frame against the 25 Hz logic cycle:")
        for i, idx, step in resyncs[:10]:
            print(f"  recomp frame {i} (native {idx}): {step:+d} native frame(s)")
        if len(resyncs) > 10:
            print(f"  ... and {len(resyncs) - 10} more.")
        print("  These are reference timing artefacts, not recomp bugs.")

    if not divergences:
        if exhausted:
            print(f"Parity PASS (truncated): {compared} aligned frames match "
                  f"before the native log ran out (rate {rate}:1).")
            return 1
        print(f"Parity PASS: {compared} aligned frames match (rate {rate}:1).")
        return 0

    limit = 20
    print(f"\n{len(divergences)} divergent frame(s) found:")
    for i, idx, a, b in divergences[:limit]:
        print(f"\nDivergence at recomp frame {i} (native frame {idx}):")
        for j in range(max(len(a), len(b))):
            la = a[j] if j < len(a) else "<missing>"
            lb = b[j] if j < len(b) else "<missing>"
            if la != lb:
                print(f"  REF : {la.strip()}")
                print(f"  OURS: {lb.strip()}")
                break
    if len(divergences) > limit:
        print(f"\n... and {len(divergences) - limit} more.")
    frames = ", ".join(str(i) for i, *_ in divergences[:limit])
    print(f"\nFirst divergent recomp frames: {frames}")
    return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
