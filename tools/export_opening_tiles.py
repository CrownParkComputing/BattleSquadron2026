#!/usr/bin/env python3
"""Export the 25 existing terrain tiles used by the opening 512 world rows.
The tile IDs and map remain authoritative; the atlas is only an art reference.
"""
import json
import struct
from pathlib import Path
from PIL import Image
ROOT = Path(__file__).resolve().parents[1]
data = (ROOT / 'amiga/original/modules/LODS0T.bin').read_bytes()
loader = (ROOT / 'amiga/original/whdload/BattleSquadron/data/LOADER').read_bytes()
word = lambda offset: struct.unpack_from('>H', data, offset)[0]
lword = lambda address: struct.unpack_from('>H', loader, address - 0x100)[0]
palette = [tuple(((lword(0x14F6 + i*2) >> s) & 15)*17 for s in (8,4,0)) for i in range(32)]
ids = sorted({word(row*48 + col*2) for row in range(480,512) for col in range(24)})
assert len(ids) == 25
atlas = Image.new('RGB',(1280,1280))
for i, tile_id in enumerate(ids):
    tile = Image.new('RGB',(16,16))
    for y in range(16):
        planes = [word(0x6000 + tile_id*2 + y*2 + p*32) for p in range(5)]
        for x in range(16):
            index = sum(((v >> (15-x)) & 1) << p for p,v in enumerate(planes))
            tile.putpixel((x,y),palette[index])
    atlas.paste(tile.resize((256,256),Image.Resampling.NEAREST),((i%5)*256,(i//5)*256))
atlas.save(ROOT/'build/opening-tiles-reference.png')
manifest = {'stage':0,'first_map_row':480,'last_map_row':511,'columns':5,'rows':5,'tile_ids':ids,
            'note':'Map cells, geometry, triggers and collision data stay unchanged. First 512 world pixels only.'}
(ROOT/'assets/remaster/opening-tiles.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(manifest)
