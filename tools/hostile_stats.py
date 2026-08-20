#!/usr/bin/env python3
"""hostile_stats.py -- per-type statistics of the HOSTILE pool ($2DC80, 12 x 80) from the
oracle objlogs in re/trace/objlog_*.txt ('H' lines).

H line: F H slot x(+0) y(+4) type(+31) b24 b27 b28 b29 b30 b57 b62 b63 l12(hex) l36(hex)
F = display frame; the game samples once per game frame (F step 2).

A record INSTANCE = run of consecutive game frames in one slot with the same type, broken by
a gap, a type change, an hp (+24) increase, a backwards y jump > 60 or an x jump > 120
(slot reuse). Per type we report: instances, lifetime in game frames, y at creation
(screen y = y-256), typical |vx|,|vy| (median of per-frame deltas, px/game frame), hp at
creation, and how the instance ended (explosion: +29 != 0 in its last frame(s); offscreen:
last y >= 512 / x outside scroll-32..scroll+320; other).

usage: hostile_stats.py [objlog files...]  (default: all re/trace/objlog_*.txt)
"""
import sys, os, glob, statistics, collections
HERE = os.path.dirname(os.path.abspath(__file__))
TRACE = os.path.join(HERE, '..', 're', 'trace')

def med(v): return statistics.median(v) if v else float('nan')

def analyse(path):
    live = {}              # slot -> instance dict
    done = []
    scroll = 0
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 3: continue
            if p[1] == 'G':
                scroll = int(p[3]); continue
            if p[1] != 'H': continue
            F = int(p[0]); slot = int(p[2]); x = int(p[3]); y = int(p[4]); t = int(p[5], 16)
            b24, b27, b28, b29, b30, b57, b62, b63 = (int(v, 16) if not v.isdigit() else int(v) for v in p[6:14])  # b30 may print as hex
            l12 = int(p[14], 16); l36 = int(p[15], 16)
            inst = live.get(slot)
            new = (inst is None or inst['type'] != t or F - inst['lastF'] > 2 or
                   b24 > inst['hp_last'] or inst['y_last'] - y > 60 or abs(x - inst['x_last']) > 120)
            if new:
                if inst: done.append(inst)
                inst = live[slot] = dict(type=t, F0=F, x0=x, y0=y, hp0=b24, b27_0=b27, b28_0=b28,
                                         l12_0=l12, frames=0, dx=[], dy=[], expl=0, hp_last=b24,
                                         y_last=y, x_last=x, lastF=F, maxexpl=0, b30s=set())
            else:
                inst['dx'].append(abs(x - inst['x_last'])); inst['dy'].append(abs(y - inst['y_last']))
            inst['frames'] += 1; inst['lastF'] = F; inst['x_last'] = x; inst['y_last'] = y
            inst['hp_last'] = b24; inst['scroll'] = scroll; inst['b30s'].add(b30)
            if b29: inst['expl'] += 1; inst['maxexpl'] = max(inst['maxexpl'], b29)
            inst['b29_last'] = b29
    done.extend(live.values())
    return done

def end_kind(i):
    if i['b29_last']: return 'explosion'
    if i['expl']: return 'explosion'
    if i['y_last'] >= 500: return 'off-bottom'
    if i['x_last'] <= i['scroll'] - 30 or i['x_last'] >= i['scroll'] + 318: return 'off-side'
    return 'vanished'

def report(path, out):
    inst = analyse(path)
    by = collections.defaultdict(list)
    for i in inst: by[i['type']].append(i)
    out.write('== %s  (%d instances)\n' % (os.path.basename(path), len(inst)))
    out.write('type  n    life(med/max)  y0(med min..max, screen=y-256)  |vx| |vy| (med px/gf)  hp0(med,max)  ends\n')
    for t in sorted(by):
        L = by[t]
        life = [i['frames'] for i in L]
        y0 = [i['y0'] for i in L]
        vx = [d for i in L for d in i['dx']]; vy = [d for i in L for d in i['dy']]
        hp = [i['hp0'] for i in L]
        ends = collections.Counter(end_kind(i) for i in L)
        out.write('$%02x %4d   %5.0f/%-5d      %4.0f (%4d..%4d)                 %4.1f %4.1f            %3.0f,%-3d       %s\n' % (
            t, len(L), med(life), max(life), med(y0), min(y0), max(y0), med(vx), med(vy), med(hp), max(hp),
            ', '.join('%s=%d' % kv for kv in ends.most_common())))
    out.write('\n')
    # per-type detail: creation fields
    for t in sorted(by):
        L = by[t]
        c27 = collections.Counter(i['b27_0'] for i in L).most_common(4)
        c28 = collections.Counter(i['b28_0'] for i in L).most_common(4)
        c12 = collections.Counter('%08x' % i['l12_0'] for i in L).most_common(4)
        b30 = collections.Counter(b for i in L for b in i['b30s']).most_common(6)
        mx = collections.Counter(i['maxexpl'] for i in L if i['maxexpl']).most_common(3)
        out.write('  $%02x at creation: b27=%s b28=%s l12=%s; flags30 seen=%s; max +29=%s\n' % (t, c27, c28, c12, b30, mx))
    out.write('\n')

def main():
    files = sys.argv[1:] or sorted(glob.glob(os.path.join(TRACE, 'objlog_*.txt')))
    out = sys.stdout
    out.write('Hostile pool statistics (tools/hostile_stats.py). life = game frames (25 Hz).\n\n')
    for f in files: report(f, out)

if __name__ == '__main__':
    main()
