#!/usr/bin/env python3
"""bs_trace.py -- helpers for the Battle Squadron oracle captures in re/trace/.

Register trace (BS_STATELOG): 18 x u32 little-endian per executed instruction,
recorded BEFORE the instruction at `pc` runs: pc, d0..d7, a0..a7, sr.
RAM dumps (--dump-file): raw chip RAM images ($0..$80000); A5 = $8000 always.

  python3 tools/bs_trace.py hits  PC [N]         first N records at PC (regs)
  python3 tools/bs_trace.py calls FROM-TO        PCs executed in a range, with counts
  python3 tools/bs_trace.py mem   DUMP ADDR LEN  hex dump of a RAM image
  python3 tools/bs_trace.py seq   PC N           N records following the first hit of PC
As a module: recs(path) yields (pc, d[8], a[8], sr); chip(path) -> bytes; W/L/B readers.
"""
import struct, sys, os, collections
HERE = os.path.dirname(os.path.abspath(__file__))
TRACE = os.path.join(HERE, '..', 're', 'trace')
STATE = os.path.join(TRACE, 'state_2000.bin')
A5 = 0x8000
REC = struct.Struct('<18I')

def recs(path=STATE, start=0, count=None):
    with open(path, 'rb') as f:
        f.seek(start * REC.size)
        n = 0
        while count is None or n < count:
            b = f.read(REC.size)
            if len(b) < REC.size: return
            r = REC.unpack(b)
            yield r[0], r[1:9], r[9:17], r[17]
            n += 1

def fmt(r):
    pc, d, a, sr = r
    return 'pc=%06x ' % pc + ' '.join('d%d=%08x' % (i, v) for i, v in enumerate(d)) + ' ' + \
        ' '.join('a%d=%08x' % (i, v) for i, v in enumerate(a)) + ' sr=%04x' % sr

def chip(path=os.path.join(TRACE, 'chip_invuln_20000.bin')):
    return open(path, 'rb').read()
def B(m, a): return m[a]
def W(m, a): return struct.unpack('>H', m[a:a+2])[0]
def SW(m, a): return struct.unpack('>h', m[a:a+2])[0]
def L(m, a): return struct.unpack('>I', m[a:a+4])[0]

def main():
    cmd = sys.argv[1]
    if cmd == 'hits':
        pc = int(sys.argv[2], 16); n = int(sys.argv[3]) if len(sys.argv) > 3 else 5
        for i, r in enumerate(recs()):
            if r[0] == pc:
                print(i, fmt(r)); n -= 1
                if n == 0: break
    elif cmd == 'calls':
        lo, hi = (int(x, 16) for x in sys.argv[2].split('-'))
        c = collections.Counter(r[0] for r in recs() if lo <= r[0] < hi)
        for pc in sorted(c): print('%06x %d' % (pc, c[pc]))
    elif cmd == 'mem':
        m = chip(sys.argv[2]); a = int(sys.argv[3], 16); n = int(sys.argv[4], 16)
        for o in range(0, n, 16):
            print('%06x: %s' % (a + o, ' '.join('%02x' % x for x in m[a+o:a+o+16])))
    elif cmd == 'seq':
        pc = int(sys.argv[2], 16); n = int(sys.argv[3])
        it = recs()
        for i, r in enumerate(it):
            if r[0] == pc:
                print(i, fmt(r))
                for j, r2 in zip(range(n), it): print(i + 1 + j, fmt(r2))
                break
if __name__ == '__main__': main()
