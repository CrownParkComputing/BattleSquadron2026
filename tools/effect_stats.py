#!/usr/bin/env python3
"""effect_stats.py -- measure the effect pool ($4976, 16 x 20 bytes) from the objlog 'E' lines.

E line: F E slot x(+0) y(+4) vx(+8 16.16) vy(+12 16.16) b16(gfx) b17(frame) b18(sprite chan) b19(age/mode)
Sampled once per GAME frame (2 display frames).  The pool is compacted (LAB_4E1A) and y-sorted
(LAB_4ADA), so slots are not identities: bullets (b19 == 0, type 7) are chained by predicting
x+2*vx, y+2*vy (two display ticks per game frame); missiles (b16 == 12) by their b19 age counter.
Usage: effect_stats.py objlog... > re/stats/effect_stats.txt
"""
import sys, collections, math

def load(path):
    frames = collections.OrderedDict()
    for line in open(path):
        p = line.split()
        if len(p) < 3 or p[1] != 'E': continue
        F = int(p[0])
        frames.setdefault(F, []).append(tuple(int(v) for v in p[2:]))
    return frames

def run(path, out):
    frames = load(path)
    nF = 0
    for line in open(path):
        if ' G ' in line: nF += 1
    out.write('== %s: %d game frames, %d frames with effects\n' % (path, nF, len(frames)))
    bytype = collections.Counter(); maxlive = collections.Counter()
    per_frame_hist = collections.Counter()
    for F, recs in frames.items():
        c = collections.Counter(r[5] for r in recs)
        for t, n in c.items():
            bytype[t] += n; maxlive[t] = max(maxlive[t], n)
        per_frame_hist[len(recs)] += 1
    out.write('records sampled per gfx type (+16): %s\n' % dict(bytype))
    out.write('max live per game frame per type: %s\n' % dict(maxlive))
    out.write('frames with N live effects: %s\n' % sorted(per_frame_hist.items()))
    # ---- bullets: chain by prediction
    Fs = list(frames.keys())
    speeds = collections.Counter(); life = collections.Counter(); spawn_x = collections.Counter()
    prev = {}  # id -> (x,y,vx,vy,born)
    nextid = 0; chains = {}
    for F in Fs:
        cur = {}
        recs = frames[F]
        used = set()
        bullets = [r for r in recs if r[5] == 7 and r[8] == 0]
        for r in bullets:
            x, y, vx, vy = r[1], r[2], r[3], r[4]
            best = None
            for i, (px, py, pvx, pvy, born) in prev.items():
                if i in used: continue
                ex = px + (2 * pvx) / 65536.0; ey = py + (2 * pvy) / 65536.0
                if abs(ex - x) <= 2 and abs(ey - y) <= 2 and pvx == vx and pvy == vy:
                    best = i; break
            if best is None:
                best = nextid; nextid += 1
                sp = math.hypot(vx, vy) / 65536.0
                speeds[round(sp, 2)] += 1
                chains[best] = [F, F]
            used.add(best)
            cur[best] = (x, y, vx, vy, chains[best][0])
            chains[best][1] = F
        prev = cur
    for i, (b, e) in chains.items():
        life[(e - b) // 2 + 1] += 1
    out.write('bullets (gfx 7, b19=0): %d chains\n' % len(chains))
    out.write('  speed |v| px/display-tick (16.16/65536): %s\n' % sorted(speeds.items()))
    out.write('  lifetime in game frames (chain length): %s\n' % sorted(life.items()))
    # vx,vy component ranges
    vxs = [r[3] / 65536.0 for recs in frames.values() for r in recs if r[5] == 7]
    vys = [r[4] / 65536.0 for recs in frames.values() for r in recs if r[5] == 7]
    if vxs:
        out.write('  vx range %.3f..%.3f  vy range %.3f..%.3f (px/tick)\n' % (min(vxs), max(vxs), min(vys), max(vys)))
    # ---- missiles / homing (gfx 12)
    ages = collections.Counter(); frame17 = collections.Counter(); mv = collections.Counter()
    for recs in frames.values():
        for r in recs:
            if r[5] == 12:
                ages[r[8]] += 1; frame17[r[6]] += 1
                mv[(round(r[3] / 65536.0, 2), round(r[4] / 65536.0, 2))] += 1
    if ages:
        out.write('missiles (gfx 12): age b19 histogram %s\n' % sorted(ages.items()))
        out.write('  frame b17 values %s\n' % sorted(frame17.items()))
        out.write('  most common (vx,vy) px/tick: %s\n' % mv.most_common(12))
    # ---- sprite channel / b18 usage and x,y ranges
    b18 = collections.Counter(r[7] for recs in frames.values() for r in recs)
    out.write('b18 sprite-channel values: %s\n' % sorted(b18.items()))
    xs = [r[1] for recs in frames.values() for r in recs]; ys = [r[2] for recs in frames.values() for r in recs]
    if xs: out.write('x range %d..%d (scroll-space, screen x = x - 7204)  y range %d..%d (screen y = y - 256)\n' % (min(xs), max(xs), min(ys), max(ys)))
    # nova ring (gfx 16)?
    other = collections.Counter((r[5], r[6]) for recs in frames.values() for r in recs if r[5] not in (7, 12))
    if other: out.write('other (gfx,frame) records: %s\n' % other.most_common(20))
    out.write('\n')

if __name__ == '__main__':
    paths = sys.argv[1:] or ['re/trace/objlog_auto_12000.txt', 're/trace/objlog_invuln_20000.txt', 're/trace/objlog_fire_30000.txt']
    for p in paths: run(p, sys.stdout)
