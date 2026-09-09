# Level 1 animated artwork preview

Select **Options → Graphics → AI PREVIEW** in native or Android.
Original and Enhanced remain available. `BS_AI_PREVIEW=1 ./build/bsview`
selects the preview on desktop.

The opening uses procedural stars at three drift speeds, twinkling and flowing
purple clouds. Original tile identities and the reference atlas define the
space/cloud boundary. No static star/cloud artwork is drawn. The coastline
through world pixel 640 replaces all original surf colours, including white,
and blends clouds into the ground using neighbouring reference samples.

The land extension covers world pixels 513–1280 (map rows 432–479), including
the first two volcanic craters and rocky ridge. The final 32 pixels fade back
to original terrain. Later scenery, other stages, sublevels and demos use
original art.

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
