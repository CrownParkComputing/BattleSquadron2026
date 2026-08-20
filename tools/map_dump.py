#!/usr/bin/env python3
"""map_dump.py -- render a stage's terrain straight from the overlay file.
Map: 512 rows x 24 words at $44000..$49FFF (row 0 = file start = END of the
level; the game starts at $49FD0 and walks DOWN by $30 per 16 px).
Map word = (byte offset into the tile strip at $4A000)/2.  A tile row r, plane p
is the word at $4A000 + word*2 + r*2 + p*$20 (so tiles may overlap by rows).
Palette: LOADER $14EA + stage*$8C + 12, 32 x RGB12.
usage: map_dump.py STAGE(0-3) [modules_dir] [loader_path] [out.png]"""
import struct, sys, os
from PIL import Image
here = os.path.dirname(os.path.abspath(__file__))
stage = int(sys.argv[1])
mods = sys.argv[2] if len(sys.argv) > 2 else os.path.expanduser('~/BattleSquadron-Amiga/original/modules')
loader = sys.argv[3] if len(sys.argv) > 3 else os.path.expanduser('~/BattleSquadron-Amiga/original/whdload/BattleSquadron/data/LOADER')
out = sys.argv[4] if len(sys.argv) > 4 else os.path.join(here, '..', 're', 'assets_preview', 'map_stage%d.png' % stage)
files = {0: ('LODS0T', 278528), 1: ('LODST1', 190618), 2: ('LODST2', 189632), 3: ('LODST3', 190528)}
name, la = files[stage]
g = open(os.path.join(mods, name + '.bin'), 'rb').read()
ld = open(loader, 'rb').read()
LW = lambda a: struct.unpack('>H', ld[a - 0x100:a - 0x100 + 2])[0]
W = lambda a: struct.unpack('>H', g[a - la:a - la + 2])[0]
pal = [((LW(0x14EA + stage * 0x8C + 12 + i * 2) >> 8 & 15) * 17, (LW(0x14EA + stage * 0x8C + 12 + i * 2) >> 4 & 15) * 17, (LW(0x14EA + stage * 0x8C + 12 + i * 2) & 15) * 17) for i in range(32)]
cache = {}
def tile(idx):
    if idx in cache: return cache[idx]
    base = 0x4A000 + idx * 2
    img = Image.new('RGB', (16, 16)); px = img.load()
    for y in range(16):
        ws = [W(base + p * 0x20 + y * 2) for p in range(5)]
        for x in range(16):
            c = 0
            for p in range(5): c |= ((ws[p] >> (15 - x)) & 1) << p
            px[x, y] = pal[c]
    cache[idx] = img
    return img
sheet = Image.new('RGB', (384, 512 * 16))
for r in range(512):
    row = 0x44000 + r * 0x30
    for cx in range(24):
        sheet.paste(tile(W(row + cx * 2)), (cx * 16, r * 16))
sheet.save(out)
print('wrote', out, 'distinct tiles', len(cache))
