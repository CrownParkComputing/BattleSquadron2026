# Level 1 opening artwork prototype

Select **Options → Graphics → AI PREVIEW** in native or Android. Original and
Enhanced remain available. `BS_AI_PREVIEW=1 ./build/bsview` selects the preview
on desktop.

The cloud atlas covers the first 512 world pixels (map rows 480–511). The
land extension covers world pixels 513–1280 (rows 432–479), including the
first craters and rocky ridge. Each strip fades over 32 pixels at its boundary.
Later scenery, other stages, sublevels and demos use original art.

`opening-land-ai-v1.png` is a full map-strip edit, avoiding repeating terrain
tiles. See [its prompt](LAND-PROMPT.md). Only original olive terrain pixels
reveal this texture; mechanical scenery and black gaps remain the original
pixels, and sprites render on top. The same camera and screen shake align it
with the native map. This preserves gameplay placement; AI-painted rock
detail is an approximation of the reference, not pixel-identical geometry.

`opening-tiles-ai-v1.png` was generated using the built-in image generation tool
from the extracted original tile atlas and a map context image. See [the prompt](PROMPT.md).
The source image is retained unmodified. The runtime shader reduces brightness
for sprite readability, blends narrow tile joins and uses the original reference
atlas to constrain black cloud-edge silhouettes. The repeated tile pattern is
still visible; this is an art-direction preview, not a finished level remaster.

The 25 atlas cells are mapped by original tile identity in `src/remaster.h`.
No map, collision, object, gate or spawn data is rewritten. The frontend captures
the same camera, shake and terrain state as each native render frame. Opaque
original sprites are composited over the high-resolution terrain.

`tools/export_opening_tiles.py` reproduces the source reference and manifest
from original game data; `tools/build_opening_shader.py` writes equivalent
desktop GLSL 330 and Android GLSL ES 100 shaders. Missing preview assets/shaders
fall back to original terrain. Regression tests verify alpha-only renderer
changes, fade-out, state preservation and exclusions.
