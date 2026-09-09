# Remastered terrain and animation

Select **Options → Graphics → REMASTERED** in native or Android.
Original and Enhanced remain available. `BS_AI_PREVIEW=1 ./build/bsview`
selects the preview on desktop.

The opening uses procedural stars at three drift speeds, twinkling and flowing
purple clouds. Original tile identities and the reference atlas define the
space/cloud boundary. No static star/cloud artwork is drawn. The coastline
through world pixel 640 replaces all original surf colours, including white,
and blends clouds into the ground using neighbouring reference samples.

The land extension covers world pixels 513–1280 (map rows 432–479), including
the first two volcanic craters and rocky ridge. The final 32 pixels blend into the shared material renderer.

`opening-land-ai-v1.png` is a full map-strip edit made with the built-in image
generation tool; see [its prompt](LAND-PROMPT.md). The source is unmodified.
The shader preserves mechanical cut-outs and adds slowly flowing lava to dark
olive fissures, rock-bordered black cracks and the two crater mouths, with
restrained glow. The old generated tile atlas is retained only as art history;
see [its prompt](PROMPT.md).

Ships, enemies and mechanical scenery draw above the replacement terrain.
Map data, triggers, collisions and damage are unchanged. The artwork follows
the native camera and shake. Painted rock detail approximates the reference;
it is not pixel-identical geometry. Captured game-frame time drives stars,
clouds and lava, so pausing freezes all three.

`tools/build_opening_shader.py` writes equivalent desktop GLSL 330 and Android
GLSL ES 100 shaders. `tools/export_opening_tiles.py` exports the original
opening tile reference. Regression checks cover renderer state preservation,
original/demo behaviour, terrain alpha and removal of original bright surf.

## All-stage material pass

Remastered now applies across all four complete maps and surface returns.
The GPU receives a separate terrain/material snapshot for each displayed frame;
original objects and sprites render over it. Original and Enhanced are unchanged,
and recorded demos keep their original rendering.

The shared pass adds material detail to rock using a clean patch of the approved
AI artwork, smooths metal interiors, animates cloud floor materials and existing
molten surfaces, and adds restrained fissure glow in volcanic environments.
It retains original shading to keep ridges and structures in place. Material
classes use the original tile identity and palette index, with tile histograms
protecting machinery from cloud replacement. The green environment does not
receive volcanic fissure effects. Colour comes from the live stage palette,
including surface returns and in-game palette changes.

The opening still has its dedicated AI-redrawn land strip. Elsewhere this is a
material/shader treatment of the original terrain, not a newly AI-redrawn map.
`src/materials.h` defines classifications; `tools/build_opening_shader.py`
produces both desktop and Android shaders from the same source.

For visual regression checks, `BS_SMOKE_STAGE=0..3` selects a stage only when
`--smoke` is active. Headless regression traverses all maps, checking original
RGB, foreground rendering, map/game immutability, and unchanged demo output.

## Volcanic openings and entrance chasms

`tools/build_lava_mask.py` flood-fills the original surface map's black areas.
Only openings with an entirely olive-rock boundary are selected, excluding
mechanical cut-outs and the starfield. The generated bit mask preserves their
exact shapes: 749 crater/fissure openings, including the three large entrance
chasms. Large pools have darker moving centres and hotter edges to suggest
depth. The original entrance markers and all entry/gameplay logic are retained.
The mask is optional; missing data retains the previous material treatment.

`BS_SMOKE_PROGRESS` can position the visual smoke test at a later map section;
it is ignored in normal play. Tests verify all three gate positions are inside
the lava mask, deep lava is restricted to original black surface terrain, and
renderer state/map/foreground invariants hold across the complete game.
