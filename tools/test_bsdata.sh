#!/bin/sh
# bsdata identity test: native renders == python renders (re/ASSETS.md §9).
# 1. build/bsdata_test depacks every module and byte-compares with the
#    reference extractions, then dumps decoded assets as PGM.
# 2. The python below re-decodes the same assets from the chip image the
#    proven way (tools/sprite_dump.py / map_dump.py formulas) and diffs
#    pixel-for-pixel.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/assets_check
./build/bsdata_test /home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data \
                    /home/jon/BattleSquadron-Amiga/original/modules build/assets_check
python3 - <<'EOF'
import struct, sys, os

mods = '/home/jon/BattleSquadron-Amiga/original/modules'
loader = open('/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data/LOADER','rb').read()

# rebuild the chip image the reference way: LOADER@$100 + reference module bins
chip = bytearray(0x80000)
chip[0x100:0x100+len(loader)] = loader
for name, addr in (('LODDAT',0x10000), ('LODGAM',0x246F0), ('LODS0F',0x2E508),
                   ('LODS0S',0x3D800), ('LODS0T',0x44000)):
    d = open(os.path.join(mods, name + '.bin'),'rb').read()
    chip[addr:addr+len(d)] = d
W = lambda a: struct.unpack('>H', chip[a:a+2])[0]
L = lambda a: struct.unpack('>I', chip[a:a+4])[0]

def pgm(path):
    f = open(path,'rb'); assert f.readline().strip()==b'P5'
    w,h = map(int, f.readline().split()); f.readline()
    return w,h,f.read()

fails = 0
def check(name, ok):
    global fails
    print(('ok   ' if ok else 'FAIL ') + name)
    if not ok: fails += 1

# tiles
def tile_idx(word):
    base = 0x4A000 + word*2
    out = bytearray()
    for y in range(16):
        ws = [W(base + p*0x20 + y*2) for p in range(5)]
        for x in range(16):
            c = 0
            for p in range(5): c |= ((ws[p] >> (15-x)) & 1) << p
            out.append(c)
    return bytes(out)
w,h,px = pgm('build/assets_check/tiles_idx.pgm')
ref = bytearray(256*256)
for t in range(256):
    buf = tile_idx(t*80)
    for y in range(16):
        ref[((t//16)*16+y)*256+(t%16)*16 : ((t//16)*16+y)*256+(t%16)*16+16] = buf[y*16:y*16+16]
check('tiles (256 tiles, indices)', (w,h)==(256,256) and px==bytes(ref))

# map (spot rows: full compare is 3 MB but cheap enough)
w,h,px = pgm('build/assets_check/map0_idx.pgm')
ok = (w,h)==(384,8192)
if ok:
    import random
    random.seed(1)
    for r in random.sample(range(512), 48):
        row = 0x44000 + r*0x30
        for cx in range(24):
            buf = tile_idx(W(row+cx*2))
            for y in range(16):
                if px[(r*16+y)*384+cx*16:(r*16+y)*384+cx*16+16] != buf[y*16:y*16+16]:
                    ok = False; break
            if not ok: break
        if not ok: break
check('map stage 0 (48 random rows vs python tiles)', ok)

# hostile bobs (sprite_dump.py decode_frame formulas)
def hostile_frame(t):
    a = 0xCD7A + t*0x20
    hh, ww = W(a), W(a+2)
    base = L(a+20)
    rb = ww*2-2; ps = rb*hh
    out = bytearray()
    for y in range(hh):
        for x in range(rb*8):
            byte = y*rb + (x>>3); bit = 0x80 >> (x&7)
            c = 0
            for p in range(5):
                if chip[base+p*ps+byte] & bit: c |= 1<<p
            if not (chip[base+5*ps+byte] & bit): c = 255
            out.append(c)
    return rb*8, hh, bytes(out)
for t in (0,1,3,4,5,6,7,8,0xA,0xB):
    w2,h2,ref = hostile_frame(t)
    w,h,px = pgm('build/assets_check/hostile%02x_f0.pgm' % t)
    check('hostile type %02x frame 0 (%dx%d, cookie mask)' % (t,w2,h2), (w,h)==(w2,h2) and px==ref)

# object templates (obj_strip formulas)
def obj_frame(t):
    a = 0x2B68 + t*48
    hh, ww, base = W(a+6), W(a+8), L(a+12)
    ps = ww*2*hh
    out = bytearray()
    for y in range(hh):
        for x in range(ww*16):
            byte = y*ww*2 + (x>>3); bit = 0x80 >> (x&7)
            c = 0
            for p in range(5):
                if chip[base+p*ps+byte] & bit: c |= 1<<p
            out.append(c)
    return ww*16, hh, bytes(out)
for t in (1,12,16,17,21,22):
    w2,h2,ref = obj_frame(t)
    w,h,px = pgm('build/assets_check/objtmpl%02d_f0.pgm' % t)
    check('object template %d frame 0 (%dx%d)' % (t,w2,h2), (w,h)==(w2,h2) and px==ref)

# font
w,h,px = pgm('build/assets_check/font.pgm')
ok = (w,h)==(43*8,9)
if ok:
    for c in range(43):
        base = 0x10550 + (0x30+c)*10
        for y in range(9):
            for x in range(8):
                want = 255 if (chip[base+y] >> (7-x)) & 1 else 0
                if px[y*43*8+c*8+x] != want: ok = False
check('font glyphs 0..Z (8x9)', ok)

# hw sprite 0 (2 planes interleaved per row)
w,h,px = pgm('build/assets_check/hwsprite0.pgm')
ok = (w,h)==(16,30)
if ok:
    for y in range(30):
        a = W(0x10000+y*4); b = W(0x10000+y*4+2)
        for x in range(16):
            want = ((a >> (15-x)) & 1) | (((b >> (15-x)) & 1) << 1)
            if px[y*16+x] != want: ok = False
check('hw sprite 0 (ship half)', ok)

# palette
want = ['%03x' % W(0x14EA+12+i*2) for i in range(32)]
got = [l.strip() for l in open('build/assets_check/palette0.txt')]
check('stage 0 palette (32 x RGB12)', want == got)

print('test_bsdata: %s' % ('%d FAILURES' % fails if fails else 'all identical'))
sys.exit(1 if fails else 0)
EOF
echo "test_bsdata.sh: PASS"
