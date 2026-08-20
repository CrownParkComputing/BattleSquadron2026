#!/usr/bin/env python3
"""parity.py HOST_OBJLOG NATIVE_OBJLOG [--pool H|O|E|P|S] [--type NN] [--frames N] [--verbose]

Align the two Battle Squadron objlogs by scroll progress (g7206, third G field)
and report, per pool and per type:
  * player/scroll parity: first game frame where any P0/G field diverges
  * spawn parity: every H record birth (slot,type,x0,y0) in time order,
    diffed host vs native by progress
  * per-type live-count divergence (first progress where the counts differ)
Progress wraps at ~8192; alignment uses the first occurrence of each value
after the run start, walked monotonically."""
import sys, argparse, collections

def load(path, limit=None):
    frames = []          # list of dicts per game frame
    cur = None
    for line in open(path):
        p = line.split()
        if len(p) < 3: continue
        F = int(p[0]); kind = p[1]
        if kind == 'G':
            if cur is not None:
                frames.append(cur)
                if limit and len(frames) >= limit: break
            cur = {'F': F, 'G': p[2:], 'P': {}, 'S': [], 'H': {}, 'O': {}, 'E': []}
        elif cur is None:
            continue
        elif kind == 'P': cur['P'][int(p[2])] = p[3:]
        elif kind == 'S': cur['S'].append(p[2:])
        elif kind == 'H': cur['H'][int(p[2])] = p[3:]
        elif kind == 'O': cur['O'][int(p[2])] = p[3:]
        elif kind == 'E': cur['E'].append(p[3:])
    if cur is not None and (not limit or len(frames) < limit):
        frames.append(cur)
    return frames

def align(host, native):
    """pair frames by (progress sequence).  native starts later or earlier than
    host; find the host frame with the native's first progress, then walk both."""
    n0 = int(native[0]['G'][2])
    hi = next((i for i, f in enumerate(host) if int(f['G'][2]) == n0), None)
    if hi is None:
        print("no common progress start (native %d)" % n0); sys.exit(1)
    pairs = []
    ni = 0
    while hi < len(host) and ni < len(native):
        pairs.append((host[hi], native[ni]))
        hi += 1; ni += 1
    return pairs

PFIELDS = ['x', 'y', 'b38', 'b39', 'b48', 'b49', 'w52', 'b56', 'w60', 'w68']
HFIELDS = ['x', 'y', 'type', 'b24', 'b27', 'b28', 'b29', 'b30', 'b57', 'b62', 'b63', 'l12', 'l36']
OFIELDS = ['x', 'y', 'type', 'b19', 'b25', 'b28', 'b30', 'b31', 'b33', 'b42', 'b43', 'l12']

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('host'); ap.add_argument('native')
    ap.add_argument('--pool'); ap.add_argument('--type')
    ap.add_argument('--frames', type=int)
    ap.add_argument('--verbose', action='store_true')
    a = ap.parse_args()
    host = load(a.host)
    native = load(a.native, a.frames)
    if a.frames: native = native[:a.frames]
    pairs = align(host, native)
    if a.frames: pairs = pairs[:a.frames]
    print("aligned %d frames (host F %d.., native F %d.., progress %s..)" %
          (len(pairs), pairs[0][0]['F'], pairs[0][1]['F'], pairs[0][0]['G'][2]))

    # 1. scroll + player parity
    first_g = first_p = None
    g_ok = p_ok = 0
    for i, (h, n) in enumerate(pairs):
        # G: dframe(0) cam(1) prog(2) stage(3) half(4) 7222(5) 7212(6) msg(7) demo(8)
        if h['G'][1:4] + h['G'][5:7] == n['G'][1:4] + n['G'][5:7]: g_ok += 1
        elif first_g is None: first_g = (i, h['G'], n['G'])
        hp, np_ = h['P'].get(0), n['P'].get(0)
        if hp == np_: p_ok += 1
        elif first_p is None:
            diff = [(PFIELDS[j], hp[j], np_[j]) for j in range(min(len(hp), len(np_))) if hp[j] != np_[j]] \
                   if hp and np_ else [('missing', hp, np_)]
            first_p = (i, diff)
    print("G  (cam/progress/7222/7212): %d/%d match; first divergence: %s" %
          (g_ok, len(pairs), "frame %d host %s native %s" % first_g if first_g else "none"))
    print("P0 (%s): %d/%d match; first divergence: %s" %
          (','.join(PFIELDS), p_ok, len(pairs), "frame %d %s" % first_p if first_p else "none"))

    # 2. hostile spawns (new slot appearances) by progress
    def spawns(frames):
        out = []
        prev = {}
        for f in frames:
            prog = int(f['G'][2])
            for slot, rec in f['H'].items():
                if slot not in prev:
                    out.append((prog, slot, rec[2], rec[0]))    # progress, slot, type, x
            prev = f['H']
        return out
    hs = spawns([h for h, _ in pairs]); ns = spawns([n for _, n in pairs])
    n_match = sum(1 for x, y in zip(hs, ns) if x == y)
    print("H spawns: host %d native %d, first %d in lockstep" % (len(hs), len(ns), n_match))
    for i in range(min(len(hs), len(ns))):
        if hs[i] != ns[i]:
            print("  first spawn divergence: host (prog,slot,type,x)=%s native %s" % (hs[i], ns[i]))
            break
    if a.verbose:
        for i in range(min(len(hs), len(ns), 40)):
            mark = ' ' if hs[i] == ns[i] else '*'
            print("  %s host %s | native %s" % (mark, hs[i], ns[i]))

    # 3. per-type live counts (H and O pools)
    for pool, fields in (('H', HFIELDS), ('O', OFIELDS)):
        first_div = {}
        for i, (h, n) in enumerate(pairs):
            hc = collections.Counter(r[2] for r in h[pool].values())
            nc = collections.Counter(r[2] for r in n[pool].values())
            for t in set(hc) | set(nc):
                if hc[t] != nc[t] and t not in first_div:
                    first_div[t] = (i, int(h['G'][2]), hc[t], nc[t])
        if first_div:
            for t, (i, prog, hcnt, ncnt) in sorted(first_div.items()):
                print("%s type %s: first count divergence frame %d (prog %d): host %d native %d" %
                      (pool, t, i, prog, hcnt, ncnt))
        else:
            print("%s pool: live counts per type match on every aligned frame" % pool)

    # 4. optional per-type trajectory dump
    if a.pool and a.type:
        t = a.type.lower()
        for i, (h, n) in enumerate(pairs):
            hr = [(s, r) for s, r in h[a.pool].items() if r[2].lower() == t]
            nr = [(s, r) for s, r in n[a.pool].items() if r[2].lower() == t]
            if hr or nr:
                print("f%04d prog %s H:%s" % (i, h['G'][2], hr))
                print("            N:%s" % (nr,))

if __name__ == '__main__':
    main()
