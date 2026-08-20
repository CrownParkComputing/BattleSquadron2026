# Battle Squadron (Amiga) — asset formats + C decoder plan (2026-08-19)

Companion to `re/BRIEF.md`. Mirrors the SWIV pattern (`~/SWIV-Native/src/swivdata.h`): one `bs_open()` that
materialises the overlays into a 512 KiB "chip image", then typed accessors that decode straight from the
image at the game's own addresses. Every format below is `(V: …)` = proven by rendering from
`re/trace/chip_2700.bin` with `tools/sprite_dump.py` / `tools/map_dump.py` (PNGs in `re/assets_preview/`),
or `(L)` = listing/runtime.c only.

## 0. Containers — overlays (done work, just reuse)
* Descriptor table: LOADER `$1980` (file offset `$1880`), 24 bytes: `load.l, packed.l, staging.l, mode.l,
  name[8]`; `mode == -2` → BOND packed (`bs_bond_depack`, C port of `$AB46` in
  `~/BattleSquadron-Amiga/src/recomp/bond.c`), `mode == 2` raw. 23 entries ending at LODSAV.
  `src/recomp/overlay.c` already parses/loads (copy both files verbatim into `src/`).
* Load map (address = where the overlay lives in the chip image; overlays sharing an address replace each other):

| file | addr | size | content |
|---|---|---|---|
| LOADER | `$100` | 67584 | code + all static tables (stage descriptors `$14EA`, hostile descriptors `$CD7A`, object templates `$2B68`, wave lists `$CF90/$D00A/..`, sprite ptr table `$C6B6`, HUD copper `$2176/$B256..`) |
| LODDAT | `$10000` | 83696 | resident gfx bank: hw-sprite ship `$10000`, font `$10550`, explosion `$11090`, hostile types 0/4/5/6/8/A gfx, …  |
| LODGAM | `$246F0` | 38108 | game audio: jump table `$246F0`, sequencer, 23 instruments `$25504`, sfx table `$2539C`, samples `$257E4..$286B0+`, songs 1..5 |
| LODS0F | `$2E508` | 62200 | stage 0 object/hostile gfx (`$2E840..$3D800`) |
| LODS0S | `$3D800` | 26624 | stage 0 more gfx (`$3D800..$44000`: objects `$3D800,$3E6A0,$3EE80,$3F6E0,$40500`, hostile 7/B `$43700`) |
| LODS0T | `$44000` | 106496 | stage 0 terrain: map `$44000-$49FFF`, tile strip `$4A000-$5E000` |
| LODST1/2/3 | `$2E89A/$2E8C0/$2E840` | ~194 K | stages 1-3: gfx + map + tiles in ONE file, same addresses as above |
| LODCOM / LODMUS | `$3D800` | 20218 / 22470 | menu audio engine / menu music (replace each other and LODS0S) |
| LODSPE / LODHIS | `$246F0` | 12186 / 21916 | replace LODGAM: "special" (intro speech?) / high-score screen |
| LODSTO | `$62000` | 115200 | title screen(s) (picture data; 115200 = 2 × 320x288x5 planes? (L)) |
| LODINT | `$62000` | 40000 | intro picture (L) |
| LODTEM | `$62000` | 64000 | = 320x320x5/8: picture (L) |
| LODEND | `$62000` | 76800 | ending picture (L) |
| LODFIN | `$44000` | 98208 | ending code/gfx (L) |
| LODLOD | `$30000` | 41600 | = 320x260x4/8 loading screen (L) |
| LODJOY | `$78000` | 18240 | option/joystick screen (L) |
| LODTXT | `$7F000` | 3584 | text screens (L) |
| LODSCO | `$FD0E` | 240 | high-score table (inside LOADER image) |
| LODSAV | `$62000` | 8192 | saved config |

Stage → files: stage descriptor `+8` = pointer to the descriptor-table entry: stage0 `$19E0`(LODS0F) + also
`$19F8`(LODS0S) `$1A10`(LODS0T); stage1 `$1A28` LODST1; stage2 `$1A40` LODST2; stage3 `$1A58` LODST3
(V: run_stage_clear in runtime.c + table arithmetic).

**OPEN ISSUE — LODST1.bin** (`~/BattleSquadron-Amiga/original/modules/LODST1.bin`): its `$44000-$49FFF`
map reads as LITTLE-endian tile offsets (`0x6270,0x6220,0x61d0,…` = multiples of 80) and the last ~70 rows
(`$48F80-$49FFF`, the level START) are noise; LODST2/LODST3/LODS0T are clean (all 12288 map words %80==0, max
≤ 511). Either the extraction of LODST1 is wrong or that stream needs re-depacking with the 68k `$AB46`
path. `BS_STAGE=frame:1` on the oracle did NOT reach stage 1 (pending stays 1, game restarts ~frame 8000),
so no stage-1 RAM image exists yet. Decoder must depack LODST1 itself from the WHDLoad `data/LODST1` with
`bs_bond_depack` and check the map invariant; if it still fails, fall back to the oracle once the
stage transition works.

## 1. Terrain (map + tiles) — (V: map_stage0/2/3.png, tiles_4a000.png; oracle's bs_map.c agrees)
* **Map**: `$44000..$49FFF` = 512 rows × 24 words ($30/row). Row at HIGH address = level start: the
  game sets `7214(A5) = $49FD0` at progress 1 and walks `-$30` per 16 px scrolled (wraps to `$49FD0` when
  `< $44000`). Progress `7206` counts PIXELS (7206++ per scrolled line, phase `7212` = 0x1E→0 step 2 =
  tile row*2, then next map row), so it wraps at 512*16 = 8192 (the "~8000" in BRIEF). Image
  row `r` (0=top of the rendered strip) = address `$44000 + r*$30`; the level plays bottom→top of that image.
  Level width 24 tiles = 384 px, visible 320 px: `7204(A5)` = camera x (`$100`-based; pixel x =
  `7204-$100`, coarse word = `((7204-$100)>>3)&~1`, fine = `(15-7204)&15`).
* **Map word** = byte offset / 2 into the tile strip at `$4A000`. Tile pixel row `r` (0..15), plane `p`:
  `word16 at $4A000 + map_word*2 + r*2 + p*$20`. I.e. a tile is 5 planes × 32 bytes = 160 bytes, planes
  non-interleaved within the tile, and tiles are referenced by byte offset, so a map word need NOT be a
  multiple of 80 (stage 0/2/3 happen to use 80-aligned tiles 0..511 = `$4A000..$5E000` exactly).
* Playfield ring the game builds: 5 planes, `$6000` per plane at `$62000`, 384 px wide, 256 visible rows,
  two `$3000` halves (clean twin) — engine detail, not an asset.
* **Palette**: stage descriptor `$14EA + stage*$8C`: `+0.l` wave list, `+4.l` wave list after loop (replay),
  `+8.l` module descriptor ptr, `+12` 32 × RGB12 (COLOR00..31; copied into the COLOR copper list at `$B2A0`,
  value words at 4-byte step, by LAB_1C2C with a fade), `+76 ($4C)` 32 more words used by LAB_1454
  (flash palette toggled by `-27618(A5)`; copies entries 0..7 → COLOR00..07, then +24→COLOR12, +26→13,
  +28→14, +30→15, +32→16, +40→20, +48→24, +56→28 (L)). Stage 0 palette (V): 000 331 eee 9c4 470 240 110
  b10 a9c 869 647 425 213 aa5 03a 220 | 662 000 000 000 552 9f6 fa0 f40 67f eed ddc ccb 883 bba aa9 998.
  Colours 16-31 are shared with hardware sprites (the HUD copper rewrites 17-19/21-23.. per player:
  update_hud_palette `$0889/$0225`, `$0C94/$0521`) — in the live copper at frame 2700, 21/25/26/27 differ
  from the descriptor (af8 fb0 f60 b40): those are the in-game sprite colours set elsewhere (L: find writer).
* Decoder: `bs_map(stage)` → `{uint16_t words[512][24]; uint8_t tiles[...]; }`, `bs_map_render(stage,
  canvas)` paints 384 × 8192 indexed pixels; identity test: for frame F take `7214/7212/7204/7208` from the
  state trace, decode the 24-word row as bs_map.c does, compare with the ring `$62000` (already proven by
  `tests/test_bs_map.c` in the oracle: zero mismatches), and compare a full screenshot `shots/a_02000.ppm`
  against `render(map rows from progress) + objects`.

## 2. Hostile sprites (enemy bobs, blitted, cookie-cut) — (V: hostile_type00/01/03/04/05/06/07/08/09.png)
* Descriptor `$CD7A + type*$20` (14 types 0..$D): `+0.w h`, `+2.w w_words` (INCLUDING one extra blitter
  shift word), `+4..+11` collision box, `+12.b armour`, `+13.b hit-sfx`, `+16.l mask ptr (frame 0)`,
  `+20.l planes ptr (frame 0)`, `+24.w score/points`, `+26.b`, `+27.b`, `+28.l`. (V: dump in sprite_dump.py)
* Record fields built by LAB_7608: `+32 = desc+16 (mask)`, `+36 = desc+20 (planes)`, `+50 h`, `+52 w`,
  `+68 = (w-1)*16` visible px, `+48 = $30 - 2w` dest modulo, `+44 plane_stride = (2w-2)*h`,
  `+46 frame_stride = 6*plane_stride`, `+63 frame`.
* **Bitmap**: frame f at `planes + f*frame_stride`: 5 colour planes then ONE cookie mask plane, each
  `plane_stride` bytes = `(w-1)` words × `h` rows, rows contiguous, MSB-first. Visible width `(w-1)*16`.
  (desc+16 == desc+20 + 5*plane_stride for every type except 6 — type 6's descriptor mask is +920 while
  the blit uses +880 = 5*176. SETTLED: LAB_97F8 DERIVES the mask (`A3 = A2 + 46(A4) - 44(A4)`), so
  desc+16 is only the record's initial `gfxmask` for the `draw_ptr`/LAB_9814 paths and must NOT be used
  to decode a frame. `bs_hostile_gfx` derives it.)
* **5-plane banks**: `frame_stride` is not always `6*plane_stride`. The stage-2/3 boss stores 5 colour
  planes per frame and ONE cookie mask for the whole bank at a fixed address (`$7FE2`: hull frames
  `$2E4C0` step `$F00 = 5*$300`, mask `$35CC0`; LAB_82E0 pod `$35FC0` step `5*312`, mask `$37208`), and
  the end-of-game mothership does the same (`$8FAE`: `frame_bytes $150`, `frame_stride $690 = 5*$150`).
* **Run-time overrides**: several handlers rewrite `+44/+46/+50/+52/+36` after LAB_7608 has filled them
  from the descriptor, so the descriptor is NOT the drawn geometry for types $02 (mothership, 4 slots),
  $09 (per-stage boss: stage 1 hull 7×32 `$2F560` + turrets 7×48 `$31960`; stages 2/3 hull 9×48 and pod
  5×39) and $0B (flypast, which borrows the bomber/drone/pop-up banks). Type $08 has no colour planes at
  all: LAB_5B3E captures the terrain and cookie-cuts it through the swoop-fighter masks at `$1A780`.
* Frame counts are per type/behaviour (type 0 = 16 rotation frames `+63 = (dir&0x1f)>>1`; type 6 = 11
  (5 idle + explosion)); beyond that the strip runs into the next asset — decoder takes `nframes` from
  the caller (engine knows) or from a small table we fill by inspection.
* Explosion: planes `$11090`, mask `$11310`, 32x32, 8 frames (V: explosion_11090.png); set by LAB_98E6
  into +36/+32 with h=32.
* Gfx pointers in the descriptor are static LOADER data; types whose gfx live in a stage file are only
  valid when that stage is resident (type 2 `$50BA0`, 9 `$2F560`, C `$507C0`, D `$37340` are garbage
  in stage 0 — stage 1-3 assets).
* Decoder: `bs_bob_frame()` in `src/bsdata.c`. Because of the two points above, anything that wants to
  BROWSE the bobs (rather than draw a live record) must use the **sprite catalog** `bs_sprite()` /
  `bs_sprite_bob()`: one entry per real bank, carrying the run-time geometry, the frame count and the
  stage mask. `tools/bobscan.c` derives it from the live render list; `tools/spritecheck.c` (wired into
  `make verify`) re-derives it every run and diffs the browser decode against `render.c`'s production
  blit pixel-for-pixel, then checksums a sheet of the whole catalog.
* Identity test: blit a frame with the cookie mask over the map at a position from `objlog H` lines and
  compare against the matching `shots/*.ppm` pixel region (frame index = `b63`).

## 3. Scenery objects (ground installations, opaque bobs) — (V: object_tmpl12_type20.png etc.)
* Templates `$2B68 + t*48`, 26 templates (t 0..25): `+4.w dest modulo ($30-2*ww, filled at spawn)`, `+6.w h`,
  `+8.w ww` (width words, NO shift word), `+10.w plane stride (= 2*ww*h, filled at spawn)`, `+12.l gfx`,
  `+17.b type` (01..05, $20..$28), `+19.b live frames`, `+25.b state/frame`, `+26.w frame stride
  (= 5*plane_stride, filled at spawn)`, `+33.b last frame`, `+44.l`, `+46.b hit sfx`. Record = 64 bytes
  at `$2E040 + slot*64`, template copied in by LAB_3078 (tile-triggered spawner).
* **Bitmap**: frame f at `gfx + f*5*plane_stride`: 5 planes, `ww` words × `h` rows, no mask (opaque,
  blitted into the ring with A→D copy). Frames 0..`+33` (e.g. tmpl 12 type $20: 19 frames = 9 live
  (rotating turret) + flash + 8 explosion/wreck).
* Decoder: `bs_object_frame(tmpl, frame, out)`; identity test: objlog `O` lines (x,y,type,b25=frame)
  vs screenshot region.

## 4. Player ship, shots, effects — hardware sprites (L + partial V: hwsprites_10000.png)
* Sprite lists: four `$400` lists at `$5E000/$5E400/$5E800/$5EC00` (+display offset), each record =
  2 control words + `h` rows × 2 words (plane A, plane B), terminated by 0.l (V: list words at frame 2700).
* Source images: pointer table `$C6B6 + idx*4`: idx 0..13 = `$10000 + idx*$78` (30 rows × 4 bytes = 16 px
  wide, 2 planes interleaved per row, ship halves: player ship = 2 sprites, left idx `player+74`, right
  `player+75`, x from `player+88`, heights 24..36 (L: build_respawning_ship_records)); idx 14/15 and
  dying ship: `$13190 + frame*$1E0` (left) / `+$F0` (right), 60 rows, 4+ frames (V: png); idx 32..39 =
  `$400..$470` (tiny 16-byte sprites: shots/effects (L)).
* Sprite colours: COLOR17-19/21-23/25-27/29-31 (4-colour pairs) — the ship renders white/grey with 29-31;
  whether pairs are attached (15 colours) must be read from the control word bit 7 at runtime (L).
* Decoder: `bs_hwsprite(ptr, h) → 16×h indices 0..3`; identity: player P line (x,y) vs screenshot.

## 5. Font / HUD
* Font `$10550 + ascii*10`: 8 px wide × 9 rows (byte 9 = pad), ASCII-indexed (V: font_10550.png shows
  `0-9 : . & ( ) ? A-Z`). Drawn with a 1-px drop shadow (LAB_43F2).
* HUD: copper-driven status bars at `$2176` (16-byte chunks per row, selected by `$2696/$26CE` lists),
  score digits/lives/capacity via draw_life_icons / expand_capacity_bars into `$B394/$B3D8` (112-byte
  row stride, 2 planes) (L). Low priority: re-create the HUD natively from the numbers; verify by
  screenshot crop comparison.

## 6. Sound — LODGAM (V: tables from chip_2700; sequencer translated in runtime.c ~983-1470)
* SFX: `EXT_2470E` with D0 = n: channel `(n>>4)&3`, sample `n&15` → 16 × 12-byte descriptors at
  `$2539C` (see `re/sfx_triggers.txt` for the table and EVERY trigger site). Only 6 distinct PCM samples
  (signed 8-bit, in LODGAM `$28DD2 $2902A $293FC $29CA4 $2B2D0 $2BFD2 $2C24E`), played at period
  214/428/856 (=3 octaves) with a duration in CIA ticks; `$251FD != 0` mutes; a channel busy-flag at +61
  drops the request.
* Music: synth sequencer, 4 channels, 62-byte channel state at `$252A4/$252E2/$25320/$2535E`, 23
  instruments × 32 bytes at `$25504` (waveform ptr, loop, arpeggio/vibrato/envelope params), songs =
  4 pattern-list pointers per track at `$251F8+12+(track-1)*16`, tracks 1..5 (1 = in-game, 2 = restart,
  3 = game over, 4 = stage clear (stage≠0)); driver entries `$2471A..` select, `$246F6` shutdown,
  `$24702/$24708` channel reset/flip, `$24F34` CIA-B tick.
* Recommendation: PORT the translated sequencer (runtime.c `music_channel_update`/`music_interrupt`,
  ~500 lines, already parity-pinned) onto a small Paula model (4 channels, period→rate, volume, loop) —
  do NOT synthesize; the instruments are tiny wavetables so a Paula mixer is the whole job. SFX: the
  same mixer plays the descriptor. Identity test: the oracle's `paula_audio.c` output vs ours for the
  first N ticks of track 1 (compare channel period/volume/ptr sequences, not audio).
* Menu audio (LODCOM/LODMUS at `$3D800`): separate engine; low priority.

## 7. Wave lists / scripts (engine data, listed for completeness)
Stage desc `+0` → 12-byte wave entries `{trigger.w, x.w, y.w, type.b, pad.b, script.l}` consumed by
LAB_7556 against progress `7206`; scripts are byte-code interpreted by the hostile update (LAB_79E2..).
Keep these as raw bytes in the image; the engine port interprets them in place.

## 8. C API plan (`src/bsdata.h`, mirroring swivdata.h)
```c
typedef struct { uint8_t chip[0x80000]; BsModule mods[32]; int nmods; int stage; } BsData;
int  bs_open(BsData *d, const char *data_dir);          /* LOADER@$100, LODDAT, LODGAM resident */
int  bs_load_stage(BsData *d, int stage);               /* LODS0F+S+T or LODSTn into chip[] (BOND) */
int  bs_load_module(BsData *d, const char *name);       /* any overlay by name (title screens etc.) */
static inline uint16_t bs_w(const BsData*, uint32_t a); uint32_t bs_l(...); uint8_t bs_b(...);
/* terrain */
typedef struct { uint16_t word[512][24]; } BsMap;         /* row 0 = $44000 = END of level */
void bs_map(const BsData *d, BsMap *m);
void bs_tile(const BsData *d, uint16_t map_word, uint8_t out[16*16]);   /* indices 0..31 */
void bs_map_render(const BsData *d, uint8_t *idx, int stride);           /* 384 x 8192 */
void bs_palette(const BsData *d, int stage, uint16_t rgb12[32]);         /* $14EA+stage*$8C+12 */
void bs_palette_alt(const BsData *d, int stage, uint16_t rgb12[32]);     /* +76 */
/* bobs */
typedef struct { uint32_t planes, mask; int w_words, h, plane_stride, frame_stride; } BsBob;
int  bs_hostile_gfx(const BsData *d, int type, BsBob *b);                /* $CD7A */
int  bs_object_gfx(const BsData *d, int tmpl, BsBob *b);                 /* $2B68 (mask=0) */
void bs_bob_frame(const BsData *d, const BsBob *b, int frame, uint8_t *idx, uint8_t *alpha); /* (w-1)*16 or w*16 wide */
/* hw sprites */
void bs_hwsprite(const BsData *d, uint32_t ptr, int h, uint8_t *idx /*16*h, 0..3*/);
uint32_t bs_hwsprite_ptr(const BsData *d, int idx);                      /* $C6B6 */
/* font */
const uint8_t *bs_glyph(const BsData *d, int ascii);                     /* 9 bytes */
/* audio */
typedef struct { uint32_t ptr; uint16_t len_words, period, volume; uint8_t dur; } BsSfx;
void bs_sfx(const BsData *d, int n, BsSfx *s, int *channel);             /* $2539C */
void bs_song(const BsData *d, int track, uint32_t lists[4]);             /* $251F8+12 */
static inline void bs_rgb12(uint16_t v, uint8_t *r, uint8_t *g, uint8_t *b);
```
Everything decodes lazily from `chip[]` so pointers the engine carries (record +36 etc.) stay valid.

## 9. Identity-test plan
1. `test_bs_open`: depack all 23 modules, byte-compare with `original/modules/*.bin` (LODST1 flagged).
2. `test_bs_map`: stage 0 row at progress P (from `objlog G` / trace) equals the oracle ring row (port
   of tests/test_bs_map.c) — 0 mismatches over 2048 rows.
3. `test_bs_frame`: compose map(progress, camera 7204) + objects (objlog O) + hostiles (objlog H,
   frame b63) + player sprite for display frame 2000/3000 and diff against `shots/a_02000.ppm` etc.
   (352x288 P6: the visible 320x256 window offset must be measured from the copper DIWSTRT — check
   `$B256` list: `008e 2690 0090 25b0` → DIWSTRT $2690 DIWSTOP $25B0).
4. `test_bs_sfx`: every trigger number in sfx_triggers.txt maps to a non-empty sample.

## 10. Recommended order
1. bsdata open/depack + stage load (reuse overlay.c/bond.c) — resolve LODST1.
2. Map + tiles + palette (already oracle-proven; trivial).
3. Bob decoder (hostile + object) + cookie-cut blit into an indexed canvas; screenshot diff test.
4. HW sprites (ship/shots) + font/HUD.
5. Audio: Paula mixer + port of the translated sequencer; SFX from `$2539C`.
6. Title/intro/text overlays last.
