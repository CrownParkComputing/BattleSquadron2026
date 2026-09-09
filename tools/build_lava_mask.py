#!/usr/bin/env python3
"""Identify black volcanic openings bounded entirely by original olive rock.
Pure black mechanical cut-outs and sky are excluded by their boundary colours.
Output is a bit mask in original 384x8192 map coordinates, never altered map data.
"""
from collections import deque
from pathlib import Path
import json
import subprocess
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
reference=ROOT/'build/lava-map-reference.png'
subprocess.run(['python3',str(ROOT/'tools/map_dump.py'),'0',
 str(ROOT/'amiga/original/modules'),
 str(ROOT/'amiga/original/whdload/BattleSquadron/data/LOADER'),str(reference)],check=True)
im=Image.open(reference);width,height=im.size
pixels=list(im.get_flattened_data());seen=bytearray(width*height)
mask=bytearray(width*height//8);regions=[]
for start,colour in enumerate(pixels):
 if colour!=(0,0,0) or seen[start]:continue
 queue=deque([start]);seen[start]=1;members=[];border=set()
 while queue:
  a=queue.popleft();members.append(a);x=a%width;y=a//width
  for b in ([a-1] if x else [])+([a+1] if x<width-1 else [])+([a-width] if y else [])+([a+width] if y<height-1 else []):
   if pixels[b]==(0,0,0):
    if not seen[b]:seen[b]=1;queue.append(b)
   else:border.add(b)
 if len(members)<12 or not border:continue
 if not all(pixels[a][0]==pixels[a][1] and pixels[a][1]>pixels[a][2] for a in border):continue
 for a in members:mask[a>>3]|=1<<(a&7)
 xs=[a%width for a in members];ys=[a//width for a in members]
 regions.append(dict(pixels=len(members),bounds=[min(xs),min(ys),max(xs),max(ys)]))
large=[r for r in regions if r['pixels']>10000]
assert len(large)==3,large
out=ROOT/'assets/remaster'
(out/'surface-lava-mask.bin').write_bytes(mask)
(out/'surface-lava-regions.json').write_text(json.dumps({'width':width,'height':height,'regions':regions},indent=2)+'\n')
print(f'{len(regions)} volcanic openings, {sum(r["pixels"] for r in regions)} pixels, {len(large)} entrance chasms')
