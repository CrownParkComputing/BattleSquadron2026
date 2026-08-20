#!/usr/bin/env python3
"""sprite_dump.py -- regenerate re/assets_preview/ from the sprite catalog.

There is exactly ONE decoder for the browsable bobs: the catalog in
src/bsdata.c, which tools/spritecheck.c verifies pixel-for-pixel against the
in-game renderer.  This script just drives `build/spritecheck --dump` and
converts its PPM strips to PNG, so the previews can never drift from the code.

(The previous version of this script decoded each $CD7A/$2B68 descriptor out of
a single stage-0 chip dump.  A descriptor's gfx pointer usually lands in a STAGE
overlay, so every sprite belonging to another stage came out as noise -- and the
noise was then checked in as a "reference".)

usage: sprite_dump.py [outdir]
"""
import os, subprocess, sys, glob
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.join(here, '..')
outdir = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, 're', 'assets_preview')
os.makedirs(outdir, exist_ok=True)
tmp = os.path.join(root, 'build', 'preview')
os.makedirs(tmp, exist_ok=True)
for f in glob.glob(os.path.join(tmp, '*.ppm')):
    os.remove(f)

exe = os.path.join(root, 'build', 'spritecheck')
if not os.path.exists(exe):
    subprocess.check_call(['make', 'build/spritecheck'], cwd=root)
subprocess.check_call([exe, '--dump', tmp, '--sheet', os.path.join(tmp, 'sheet.ppm')], cwd=root)

# drop the previous, descriptor-decoded previews
for old in glob.glob(os.path.join(outdir, 'hostile_type*.png')) + \
           glob.glob(os.path.join(outdir, 'object_tmpl*.png')):
    os.remove(old)

n = 0
for ppm in sorted(glob.glob(os.path.join(tmp, '*.ppm'))):
    name = os.path.splitext(os.path.basename(ppm))[0] + '.png'
    Image.open(ppm).save(os.path.join(outdir, name))
    n += 1
print('sprite_dump: wrote %d previews to %s' % (n, outdir))
