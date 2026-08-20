# Battle Squadron (Cope-com / Innerprise 1989) — fully native port (route B), status log

Reference trees: ~/BattleSquadron-Amiga (Musashi oracle host `build/battle_squadron_native`, module extraction,
docs/MAP.md, docs/module-map.json, docs/porting-pipeline.md, tools/parity_diff.py, tools/recomp_dump.c,
src/recomp/runtime.c = partial instruction-level recomp — NOT the route here). ~/SWIV-Native = the finished
route-B example (engine.h coroutine object model, behaviours/, frontend.c, audio/sfx_bank, tools/parity.py).
Goal: no Musashi, no 68k semantics — C engine + behaviour scripts + raylib front end, parity-checked against
the oracle's per-frame record log.

## Status log
### 2026-08-19 — session 1: oracle instrumentation + engine decode
* Host (`~/BattleSquadron-Amiga/src/host`, commits 7367a06, +BS_STAGE): `BS_STATELOG/_FROM/_MAX` (18 x u32 LE
  register trace), `BS_PCSET`, `BS_WATCH`, `--objlog PATH` (per-game-frame record log of all four pools +
  players, sampled at PC $BE8), `--dump-file BASE LEN PATH`, `BS_AUTOPILOT=1`, `BS_INVULN=1`,
  `BS_STAGE=frame:stage` (forces the stage-clear transition so the underground stages 1..3 can be captured).
  `make unit-test`: the one video self-test failure ("early-fetch fine-scroll lead") pre-exists my changes.
* Captures: `re/trace/` (see re/BRIEF.md): surface level (stage 0) autofire / invulnerable / fire-held runs,
  stages 1-3 forced at frame 3000, register traces from frames 2000/8000 and per stage, PC set, RAM images,
  screenshots (`re/trace/shots`, ignored by git together with the .bin files).
* Decode (done): re/ENGINE.md (top-level) + re/ENGINE_frame_player.md, re/ENGINE_hostiles.md, re/ENGINE_objects.md
  (C pseudocode per routine, (V)/(L) marks), re/handlers.txt (14 hostile + 16 object types → addresses),
  re/waves_level1.txt, re/stats/{hostile,object,effect}_stats.txt, re/ASSETS.md + re/sfx_triggers.txt +
  tools/{sprite,map}_dump.py (formats proven by rendering), re/PORTING_GUIDE.md, src/engine/engine.h draft.
* Next (session 2): src/engine/engine.c core (pools, frame order, allocators, scheduler, spawner, collision,
  player, effects) + tools/simrun.c (native objlog in host format) + tools/parity.py (align by g7206);
  fan out hostile types and object types to agents per PORTING_GUIDE; src/bsdata.c per ASSETS.md §10.

### 2026-08-19 — session 2: ENGINE CORE + data decoder + parity (commit b5012fc)
* src/bsdata.[ch] (+ overlay.c/bond.c copied verbatim): bs_open loads LOADER/LODDAT/LODGAM from the
  WHDLoad install into a 512 KiB chip image, bs_load_stage the stage files; decoders for map/tiles/
  palettes/hostile+object bobs/hw sprites/font/sfx.  `make test` proves them: every module depack
  byte-equals original/modules/*.bin (LODST1 INCLUDED — the "byte-swapped extraction" worry was wrong),
  and every native render equals an independent python decode (tools/test_bsdata.sh).
* src/engine/engine.c + frozen engine.h: the whole LAB_AA0 game frame (eng_frame_update = up to the $BE8
  objlog point, eng_frame_finish = the rest), scroll incl. the surface loop, wave scheduler, tile spawner
  (+ hangar gates, stage-1 pool-full map rewrite), hostile driver (4 passes, explosion, last-of-wave nova
  pickup), fire-request → effect pool, effects (y-sort, channels, missile steering), player (fire volley,
  movement, shots + weapon-3 wobble, death/respawn, HUD portrait counters), the four collision passes, BCD
  scoring, pickups, extra lives, game over + initials editor + fire-restart, type-7 terrain probe (from the
  map; the real probe reads the composited playfield — objects drawn over terrain are not modelled).
* Original QUIRKS discovered via the oracle and ported bit-exactly: (1) LAB_47D2's insert-shift (LAB_481E)
  overshoots one record — the insert slot inherits record slot-1, or the CODE BYTES at $4962 for slot 0
  (bullet x/y fractions $986D/$6A06); (2) the object fire scan's mine path loads D0=56 for the sound call
  and the DBF then walks 57 fake "records" past the object pool into LODS0F bytes, firing garbage-origin
  bullets (transient junk E records in the captures); (3) missile 32-dir turn parity uses the DBF counter
  (15-slot), not the slot; (4) type-8 vy steering at -vmax falls through into the opposite branch and
  bounces ($871E BGT.S LAB_8726); (5) player bytes 31/63 (not the hostile's) gate the LAB_35CE kill quirk.
* Parity (tools/simrun + tools/parity.py, aligned by progress): FIRE capture, all 14300 game frames
  (~9.5 min): G scroll + P0 player 14300/14300 exact, all 330 hostile spawns in lockstep (time/slot/type/x),
  O/E/S pools byte-identical on every frame, H identical except the type-07 l12 column (the host logs the
  uninitialised D4 register at spawn — cosmetic, unused).  INVULN capture: 9200/9200 exact incl. the
  8192→1 progress wrap.  AUTO capture (honest deaths): exact through death → initials → restart (3922
  frames); the 2nd initials walk diverges — the oracle itself no longer reproduces that capture bit-exactly,
  so its input reconstruction is suspect.
* simrun stage 1-3 run (own init approximation: 11C6 reset + 256-frame pre-roll); the BS_STAGE-forced
  captures cannot be aligned exactly (unknown rng/pool state at the forced transition) — parity for the
  underground stages needs the native stage-clear transition to be driven from a stage-0 run instead.
* Blocked/open: hostile $02 mothership + $0B flypast, nova (ported from the doc, never exercised), full
  stage clear LAB_7002/7180 (stub: gate toggle recorded, no advance), final boss (LODFIN), attract demo.

### 2026-08-20 — session 3: FRONTEND + AUDIO + RAYLIB VIEWER
* src/render.[ch]: full frame renderer from engine state + chip image into a 288x255 canvas (the
  game's real DIW window; the 352x288 oracle PPMs carry it at offset (32,12)).  Terrain per-pixel from
  the map/scroll model (q = progress - row, stage-0 wrap), object/hostile bobs from the engine render
  list (opaque / cookie-cut), hardware sprites (effects, player shots, ships incl. banking + the
  $13190 death animation), font/HUD/message overlays.  Colour machinery decoded and implemented:
  LAB_1420/1454 palette flash = base/alt STROBE by counter bit0 over entries 0..16+20/24/28 (not the
  0..7 subset the assets doc guessed); LAB_1FAE effect colour cycle from $1F1E (or $1F4E in nova),
  entry (frame&$E); LAB_28F0 ship/shot colours = the animated $272E weapon rows (phase (frame&6)) with
  the $27AE/$27BE grey cycle while invulnerable; sprite copper colours confirmed from the live copper.
  New finding: hostile type-8 smoke frames display the exhaust-puff SPRITE (image $58 at +8/+5) — the
  record's $577E/$1A780 pair is the playfield smoke paint, not the visible bob.
* tools/framecmp.c + `make verify`: replays the parity captures (same flags as simrun) and pixel-diffs
  the frame rendered at the new eng_display_hook against re/trace/shots, rows 24..254.  Results:
  i_12000 99.74% / i_18000 99.72% / i_06000 98.8% / f_20000 98.6% / f_10000 96.9% exact.  Terrain,
  palettes and bobs are pixel-exact everywhere; the residue is (a) bobs crossing the 384-px ring seam
  (the original blit shears them; native draws them clean), (b) 1-2 px on a couple of bullets per
  frame (odd-slot sprite-buffer parity not modelled), (c) the approximated HUD row, (d) f_10000's
  forced-invuln ship colour phase.
* src/audio.[ch]: the LODGAM sequencer (music + SFX, one mixer as the original: SFX borrow a channel,
  the song resumes; jingle entries $2471A..$24750 = tracks 1..10 incl. death/extra-life/nova) ported
  verbatim from the parity-pinned runtime.c translation, on a native 4-channel Paula model reading
  samples from the chip image; CIA cadence 709379/$3100 = 56.55 Hz.  tools/audiotest.c renders tracks
  headless to WAV (tonal, moving note peaks; sfx injection audible).  Menu audio (LODCOM/LODMUS — a
  separate engine) not ported: the title is silent (TODO).
* src/viewer.c + `make run` (build/bsview, raylib): title menu (native text title — the real LODSTO
  title picture's plane layout resisted decoding, TODO), 1-2 player start, options screen persisted in
  options.txt (volume, music/sfx, difficulty, weapon type, lives, fullscreen) + hiscore, P/START
  pause, ESC-to-title, 50 Hz via VSync+SetTargetFPS, integer-scaled centred canvas, keyboards per the
  itch doc + auto-detected pads, TRUE 50 Hz sprite motion (both display frames of an engine iteration
  rendered via eng_display_hook).  Engine additions: eng_display_hook only; parity smoke still green.
* `make smoke`: boots straight into play with autofire and screenshots to build/smoke.png (verified:
  terrain + HUD + bobs + pickups on screen).
* Open: title/intro pictures (LODSTO layout), menu music, stage-clear advance (engine stub), ring-seam
  bob shear, HUD pixel-exactness, two-player camera formula unverified.

### 2026-08-20 — session 4: TITLE PICTURE + MENU MUSIC + ATTRACT DEMO (commits 91882bb, db64e90, 8d924f6)
* TITLE PICTURE solved by measurement: dumped the idle oracle at display frame 36000 (title showing)
  plus the whole chip, and read the LIVE copper list ($AFB6).  The title is NOT in LODSTO — it is the
  whole LODINT module verbatim: 5 planes at $62000 + p*8000 (320x200, modulo 0), palette split by
  copper WAITs at picture rows 86 and 184 (entries 6/16/28..31 restored below 184).  LODSTO turns out
  to be the intro/story pictures (2 x 320x288x5); the F-key icons, PRESS BUTTON TO START and the
  INNERPRISE line are BAKED into the LODINT art — the title code only draws two dynamic bands (rows
  120-139 input icons + on/off labels, rows 147-155 ONE/TWO PLAYERS).  render_title() + build/titlecmp
  (in `make verify`): 98.18% of the host title frame byte-exact, 0 mismatches outside the two bands.
  Viewer title = the real picture on a 320x200 canvas, F1/F2 start 1/2P, F3 FX, F4 music, F5 players,
  O = native options (re/trace/shots/title_36000.ppm is the reference).
* LIVE-PLAY FIXES from Jon's desktop run: (1) player bullets were invisible — LAB_5410 emits shot +8
  as the sprite HEIGHT and +9 as the $C6B6 gfx index; render.c had them swapped (weapon templates whose
  +8 hit a null table slot vanished), and height-0 filler template slots must draw nothing; (2) audio
  path verified end-to-end against the PulseAudio monitor (BS_AUDIO_DEBUG=1 traces buffers/rms/Paula).
* MENU MUSIC: LODMUS ($3D800, replacing LODS0S; speech = LODSPE at $246F0, replacing LODGAM) is a
  second Ron Klaren driver — freshly IRA'd to the oracle tree's asm/lodmus.asm (interrupt $3DDA2).
  Differences vs LODGAM kept faithfully: no pause byte, $83 AND $85 stop, 0-duration notes continue the
  command loop, envelope clamps to the master byte $3DF16+1 (stop request fades it at half tick rate).
  Tick = CIA-B latch $2500 = 74.90 Hz.  Track 1 is pre-armed in the module ($3D800 entry = init+play,
  called at boot and title entry); track 2 = second song; 7 speech descriptors at $3E038 point into
  LODSPE.  Validated: audiotest 101/102 render both songs (rms 5003/10503); spectral diff vs host
  --dump-audio at the title matches the bass-note transitions window-for-window over 12 s.  Viewer:
  title/attract swap the audio overlays exactly like the loader (title_enter/start_game), the boot
  "welcome to Battle Squadron" (whole LODSPE, one Paula one-shot on ch 0) plays on first title entry.
* ATTRACT DEMO: recorded input.  The idle title enters the game loop with -28516 set and LAB_9A9E
  replays 2 raw joy bytes/frame (P1+P2) from LODDAT $22F80; entry ($6FA) = both players in (P1 w3 l5,
  P2 w2 l4 — the recording fires novas!), type-3/6 hp patched 3/$17, bullet speed $200, mid-level
  start progress $EA0 / maprow $47420 / waves $D3BE / hangars $0E, 256-iteration pre-roll; LAB_45D2
  counts -28550: 1500 ships leave + hiscore board, 2000/2500/3000/3500 POINTS/CREDITS overlays
  (LODS0F texts), HUD centre cycles DEMO/SCORES/POINTS/CREDITS, 4000 = back to title.  During the
  attract LODMUS keeps playing and the $246F0 sfx jump table is RTS-patched (measured) — demo is
  sfx-silent.  Ported as eng_demo_init() + demo branches; simrun --demo; parity vs the oracle's own
  first attract (re/trace/objlog_demo_30000.txt): G + P0 4000/4000 exact over the entire demo, every
  spawn in lockstep (incl. type-0b flypast + nova bursts), H pool byte-identical 1556/4000 (rest =
  kill-time jitter from frame 788).  Found + fixed a real engine bug: LAB_1D0C reads the nova script
  as WORDS; the byte-wide port ended each nova after ~3 frames.  `make test` now has a 780-frame demo
  leg with TOTAL parity (G/P0/spawns/H/O counts).  Viewer: 12 s idle -> demo, any input -> title,
  no score persistence; BS_DEMO_SMOKE=N screenshots it.
* Open: stage-clear advance (engine stub), final boss (LODFIN), intro/story sequence (LODSTO+LODTXT),
  ring-seam bob shear, HUD pixel-exactness, demo kill-time jitter past frame 788 (dmg -1 penetration
  details), the two title menu-band icons (drawn with the chip font instead of the original title font).

### 2026-08-20 — session 5: DEMO TOTAL PARITY + FAITHFUL LAB_A30E MESSAGE RENDERER
* DEMO PARITY closed: three engine bugs found by chasing the first byte divergences vs the idle
  oracle (re/trace/objlog_demo_30000.txt): (1) LAB_36D6 weapon pickup writes t28>>1 into byte
  59(A4) = the LOW BYTE OF THE WEAPON WORD 58(A4) — a pickup CHANGES THE WEAPON; the port had
  invented a separate "hudcol59" byte, so P2 kept firing the old volley (first S divergence at
  demo frame 697, H kill jitter from 788); (2) type-0b flypast t28 gated on an unwired
  hg.flypast_toggle instead of gframe()&2 ($1079 bit 1); (3) nova ring spawn ($1DB0/$1DB2) and the
  LAB_1D56 nova/demo clear write only the INTEGER x words (+ age byte) — fractions and the rest of
  the record keep pool residue, and LAB_4E1A refills effect slot 15 from the 20 code bytes past the
  pool ($4AB6) instead of zeroing.  Result: ALL SIX POOLS (G/P/S/H/O/E) byte-identical 4000/4000
  over the whole attract demo; test_parity_smoke now runs the full 4000-frame demo leg and requires
  per-pool byte identity (FIRE/INVULN legs unchanged, still green).
* LAB_A30E MESSAGE RENDERER rewritten faithfully from the listing (render.c draw_messages):
  pages = {x.w,y.w,char[16]} records ($2E508/82/E8/$2E676 in LODS0F; hiscore board = the REAL
  $FCE6 page in the loader image, defaults baked in); loader adds $16 to every y after load
  (LAB_1368/boot $554).  8 sprite columns of 2 chars (font $10550, 10 bytes/char; plane1 =
  cur|prev>>1, plane2 = prev>>1&~cur → glyph = cycling colour 1, +1/+1 drop shadow = colour 3
  $0600).  Counter 8514 ticks per display frame: line L appears at tick L+1; ticks 20+12L..71+12L
  add the $A24E per-column deltas (121..258/64ths px) — the 8 columns fan out rightward from the
  stacked off-screen pageX into their ~16px-spaced rest spots (position>>6 = sprite hpos, canvas
  x = hpos-$90, canvas y = y+$16-$26); after tick hold ($2EE) the $A25E deltas slide each line off
  right.  Colours: 4 copper WAITs per line at vpos y..y+6 load COLOR17/21/25/29 (column pairs) from
  the $A26E 64-word cycle, word (dframe>>2 + 4L + w) mod 64 + 4*pair (pair offset unwrapped, past
  $A2EE, as the original reads it); display lags the build by one tick (render uses 8514-1).
  While a message is up the sprite channels belong to the text: effects/shots/ships and the type-8
  exhaust-puff sprite are not drawn, and palette entries 19/23/27/31 = $0600 (the A30E head writes,
  shared playfield/sprite entries — the terrain's orange pipes really do turn dark red).  Demo runs
  on the BASE stage-0 palette: LAB_7336 only latches a palette when the copper is rebuilt, and the
  demo patches hangars=$0E without one.
* VERIFY: framecmp --demo + six new oracle dumps (idle host --dump-frame-seq, fbase 9125) in
  `make verify`: hiscore board d12500 99.91%, SCORES d13500 99.35% (residue = the missile's
  playfield smoke trail, not modelled), POINTS d14500 99.85%, d15500 99.92%, credits d16500 99.75%,
  mid-slide d13200 99.58% exact — the showcased parade ships sit pixel-exactly beside their POINTS
  rows.  Known residue: the last line's rows 6-9 colour band shows a stale cycle value on the
  oracle (mid-frame copper rebuild race, reproduced by the scanline host; ~20-60 px), and the
  smoke playfield paint.  make test / verify / smoke all green.

### 2026-08-20 — session 6: render fixes (parade cloak-missile, pop-up garble, bullet line) + LODST1
* CLOAKED MISSILE decoded: LAB_5B3E is NOT a smoke paint — it is a background-CAPTURE blit
  (BLTCON0 $09F0, minterm F0 = D:=A, source = playfield at the call position, dest = the $577E
  buffer, then a 3-word→2-word compaction), and LAB_9814 draws the missile mask filled with that
  capture: the type-8 homing missile is a REFRACTION of the terrain (in-game capture at (x,y) =
  near-invisible shimmer; the demo parade captures at (x-2,y-2), so the ship beside "750 POINTS"
  shows as a 2px-shifted ghost of the map).  render.c paint_smoke records the capture positions;
  draw_cloak fills the mask with terrain sampled at the capture point +1px x (the capture blit's
  word-align shift, measured).  d13500 432→146 bad px (0.65%→0.22%), i_18000 185→66.
* POP-UP GARBLE fixed: draw_bob computed the per-plane stride from the record's CURRENT height,
  but emerging ground pop-ups (type 3/D, h < full) blit with the FULL frame's plane size 44(A4)
  (LAB_97F8 A3 = A2 + stride − frame_bytes) — RenderEntry now carries stride (hostiles:
  frame_bytes; objects: f10) and draw_bob uses it, so a half-emerged turret no longer reads
  planes 1-4 from the wrong offsets (garbage until fully out).  Also modelled $76E8: type-3
  horizontal emerge ANDs the 32 mask longs with the reveal mask (RenderEntry.reveal, consumed
  per draw like the original temp).  f_10000 2055→945 bad px (3.09%→1.42%).
* ENEMY-BULLET RED LINE fixed: the $C6B6 bullet images (slots $58/$59) are 28 bytes = 7 rows
  apart; drawing 8 rows read the next image's first row as a spurious line under every bullet.
  h=7 for $58..$5F.  f_20000 1147→1092, i_6000 769→745.
* LODST1 DECODE solved (stage-1 map/sprites rendered as noise): LoadModule has a LODST1-only tail
  ($1C08..$1C22) that byte-REVERSES the depacked region $2EF20..$5E000 in place (cursors meet at
  $46790), and the stage-advance path ($71CA..$7242) rebases 8 object gfx pointers
  $F068+n*$12 → $E924+n*$12 over the 12-byte records at $2E8A2..$2EF20.  Both applied in
  bs_load_stage(1); after them the ENTIRE LODST1/2/3 modules are byte-identical to the oracle's
  live RAM (re/trace/chip_stage{1,2,3}_16000.bin, 0 diffs) — bsdata_test now asserts this.
  (The old "byte-swapped extraction" worry was the reversal: modules/LODST1.bin is the raw
  depack, correct but pre-fixup.)  make test / verify / smoke all green.
