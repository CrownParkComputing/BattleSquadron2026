#!/usr/bin/env python3
"""object_stats.py -- measure the OBJECT pool ($2E040, 'O' lines) and the effect/enemy-bullet
pool ($4976, 'E' lines) from the oracle objlogs in re/trace/.

  python3 tools/object_stats.py [objlog ...]  > re/stats/object_stats.txt

'O' line: F O slot x(+0) y(+2) type(+17) b19 b25 b28 b30 b31 b33 b42 b43 l12
'E' line: F E slot x(+0) y(+4) vx(l8) vy(l12) b16 b17 b18 b19
'G' line: F G gframe scroll(7204) progress(7206) stage(7228) half g7222 g7212 msg8514 demo
A life of an object = consecutive game frames in which the same slot holds the same type and
gfx pointer and y never goes backwards (slots are only reused after the record was freed).
Effects are sorted by y inside their pool, so they are tracked by continuity of position and
velocity instead of by slot.
"""
import sys, os, glob, collections, statistics

HERE = os.path.dirname(os.path.abspath(__file__))
TRACE = os.path.join(HERE, '..', 're', 'trace')

def med(v): return statistics.median(v) if v else 0

def analyse(path):
    objs = {}           # slot -> current life dict
    done = []           # finished object lives
    eff_live = []       # live effects: dict
    eff_done = []
    G = None
    frame_objs = {}
    frame_effs = []
    last_frame = None
    types_per_stage = collections.defaultdict(set)
    def close_frame():
        nonlocal eff_live
        # objects absent this frame are dead
        for slot in list(objs):
            if slot not in frame_objs:
                done.append(objs.pop(slot))
        frame_objs.clear()
        # effects: match
        new_live = []
        for e in frame_effs:
            best = None
            for p in eff_live:
                if p['used']: continue
                if p['b16'] != e['b16']: continue
                dx = e['x'] - p['x']; dy = e['y'] - p['y']
                if e['b16'] == 12:
                    # stationary mine: b19 is its age, it drifts with the scroll then launches
                    if e['b19'] == p['b19'] + 1 and abs(dx) <= 6 and abs(dy) <= 6:
                        best = p; break
                    continue
                if (p['vx'], p['vy']) != (e['vx'], e['vy']): continue
                if abs(dx - p['vx'] * 2 / 65536.0) <= 2 and abs(dy - p['vy'] * 2 / 65536.0) <= 2:
                    best = p; break
            if best:
                best['used'] = True
                best.update(x=e['x'], y=e['y'], vx=e['vx'], vy=e['vy'], frames=best['frames'] + 1, b17=e['b17'], b19=e['b19'])
                best['used'] = False
                new_live.append(best)
            else:
                e.update(frames=1, used=False, x0=e['x'], y0=e['y'], b17_0=e['b17'], b18_0=e['b18'], b19_0=e['b19'], stage=G['stage'] if G else None)
                new_live.append(e)
        for p in eff_live:
            if p not in new_live: eff_done.append(p)
        eff_live = new_live
        frame_effs.clear()
    with open(path) as f:
        for line in f:
            p = line.split()
            if len(p) < 3: continue
            fr = int(p[0])
            if last_frame is not None and fr != last_frame:
                close_frame()
            last_frame = fr
            kind = p[1]
            if kind == 'G':
                G = dict(gframe=int(p[2]), scroll=int(p[3]), progress=int(p[4]), stage=int(p[5]),
                         half=int(p[6]), g7222=int(p[7]), g7212=int(p[8]), msg=int(p[9]))
            elif kind == 'O':
                slot = int(p[2]); x = int(p[3]); y = int(p[4]); typ = int(p[5], 16) if len(p[5]) == 2 and not p[5].isdigit() else int(p[5])
                # type column is printed as hex? detect: values like '20','21' are hex in the log
                typ = int(p[5], 16)
                b19, b25, b28, b30 = int(p[6]), int(p[7]), int(p[8]), int(p[9], 16)
                b31 = int(p[10], 16); b33, b42, b43 = int(p[11]), int(p[12]), int(p[13]); l12 = p[14]
                cur = objs.get(slot)
                if cur and (cur['type'] != typ or cur['l12'] != l12 or y < cur['y'] or x != cur['x']):
                    done.append(objs.pop(slot)); cur = None
                if cur is None:
                    cur = dict(slot=slot, type=typ, x=x, y=y, y0=y, l12=l12, frames=0, hp0=b28, hpmin=b28,
                               b19=b19, b33=b33, b42s=set(), b43_0=b43, b31s=set(), b25max=b25, b25s=set(),
                               stage=G['stage'], progress0=G['progress'], scroll0=G['scroll'], gframe0=G['gframe'],
                               g7222=G['g7222'], frame0=fr, hp_hits=0, last_hp=b28)
                    objs[slot] = cur
                    types_per_stage[G['stage']].add(typ)
                cur['frames'] += 1; cur['y'] = y; cur['b31s'].add(b31); cur['b42s'].add(b42)
                cur['b25max'] = max(cur['b25max'], b25); cur['b25s'].add(b25)
                cur['hpmin'] = min(cur['hpmin'], b28 if b28 < 128 else b28 - 256)
                if b28 != cur['last_hp']: cur['hp_hits'] += 1; cur['last_hp'] = b28
                cur['yend'] = y; cur['frame_end'] = fr
                frame_objs[slot] = cur
            elif kind == 'E':
                frame_effs.append(dict(x=int(p[3]), y=int(p[4]), vx=int(p[5]), vy=int(p[6]),
                                       b16=int(p[7]), b17=int(p[8]), b18=int(p[9]), b19=int(p[10])))
    close_frame()
    done.extend(objs.values()); eff_done.extend(eff_live)
    return done, eff_done, types_per_stage

def report(path, done, eff_done, tps):
    out = []
    out.append('=' * 100)
    out.append('%s: %d object lives, %d effect lives' % (os.path.basename(path), len(done), len(eff_done)))
    out.append('object types per stage(7228): ' + ', '.join('stage %d: %s' % (s, ' '.join('%02x' % t for t in sorted(v))) for s, v in sorted(tps.items())))
    by = collections.defaultdict(list)
    for d in done: by[(d['stage'], d['type'])].append(d)
    out.append('%-5s %-4s %5s %7s %7s %7s %7s %6s %6s %6s %5s %5s %-10s %-10s %-8s %s' % (
        'stage', 'type', 'count', 'life_md', 'life_mx', 'y0_md', 'hp0', 'hpmin', 'b19', 'b33', 'b43mx', 'b25mx', 'b31 seen', 'b42 seen', 'gfx', 'notes'))
    for (stage, typ), L in sorted(by.items()):
        lives = [d['frames'] for d in L]
        y0 = [d['y0'] for d in L]
        hp0 = collections.Counter(d['hp0'] for d in L).most_common(3)
        b31 = set(); b42 = set(); gfx = collections.Counter()
        for d in L: b31 |= d['b31s']; b42 |= d['b42s']; gfx[d['l12']] += 1
        hpmin = min(d['hpmin'] for d in L)
        died = sum(1 for d in L if d['hpmin'] < 0)
        offscreen = sum(1 for d in L if d['yend'] >= 0x1f0)
        notes = 'hp<0(killed)=%d yend>=0x1f0(scrolled off)=%d hp_changes=%d' % (died, offscreen, sum(d['hp_hits'] for d in L))
        out.append('%-5d $%02x  %5d %7d %7d %7d %7s %6d %6s %6s %5d %5d %-10s %-10s %-8s %s' % (
            stage, typ, len(L), med(lives), max(lives), med(y0), '/'.join('%d' % h for h, _ in hp0), hpmin,
            '/'.join(str(x) for x in sorted(set(d['b19'] for d in L))), '/'.join(str(x) for x in sorted(set(d['b33'] for d in L))),
            max(d['b43_0'] for d in L), max(d['b25max'] for d in L),
            ' '.join('%02x' % v for v in sorted(b31)), ' '.join(str(v) for v in sorted(b42)),
            ' '.join(g for g, _ in gfx.most_common(2)), notes))
    # y at creation vs scroll / progress: objects enter at y0 = 0x100 - h
    h_by_type = collections.defaultdict(set)
    for d in done: h_by_type[d['type']].add(0x100 - d['y0'])
    out.append('implied height (0x100 - y at first sample) per type: ' + ', '.join('$%02x:%s' % (t, sorted(v)) for t, v in sorted(h_by_type.items())))
    # per-frame y step vs g7222
    steps = collections.Counter()
    for d in done:
        if d['frames'] > 1: steps[(d['g7222'], round((d['yend'] - d['y0']) / (d['frames'] - 1), 2))] += 1
    out.append('y step per game frame (g7222 at spawn, step): ' + ', '.join('%s:%d' % (k, v) for k, v in steps.most_common(6)))
    # effects
    eb = collections.defaultdict(list)
    for e in eff_done: eb[(e['b16'], e['b17_0'], e['b19_0'])].append(e)
    out.append('effects by (b16, b17 at first sample, b19 at first sample): count life_md life_mx |v|_md(px per GAME frame = 2*16.16 velocity) stationary')
    for k, L in sorted(eb.items()):
        lives = [e['frames'] for e in L]
        sp = [((e['vx'] / 65536.0) ** 2 + (e['vy'] / 65536.0) ** 2) ** 0.5 * 2 for e in L]
        out.append('  %s: %5d %5d %5d %6.2f stationary=%d' % (k, len(L), med(lives), max(lives), med(sp), sum(1 for e in L if e['vx'] == 0 and e['vy'] == 0)))
    out.append('effects total=%d; max simultaneous not tracked here' % len(eff_done))
    return '\n'.join(out)

def main():
    paths = sys.argv[1:] or sorted(glob.glob(os.path.join(TRACE, 'objlog_*.txt')))
    for p in paths:
        done, eff, tps = analyse(p)
        print(report(p, done, eff, tps))
        print()

if __name__ == '__main__':
    main()
