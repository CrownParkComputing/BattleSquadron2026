# Battle Squadron (Amiga) — shared facts for the engine-decode agents (2026-08-19)

## Sources (read-only unless told otherwise)
* Listing: `~/BattleSquadron-Amiga/asm/loader.asm` (IRA, LOADER at $100..$10900; every line carries
  `;xxxxxx:` = runtime address), `asm/lodgam.asm` (LODGAM overlay at $246F0: sound/music), `asm/lodcom.asm`.
  Labels are `LAB_xxxx` = address. `A5 = $8000` is the global base for the whole game (`N(A5)`, N may be
  negative: -28552(A5) = $90F8). `A6 = $DFF000` (custom chips). Player 1 record = $4E3C, player 2 = $4F46.
* Hand translation with comments (partial, PINNED BY PARITY against the oracle — trust it, but the route here
  is NOT instruction-level recomp): `~/BattleSquadron-Amiga/src/recomp/runtime.c` (7.7k lines; grep for the
  LAB you need; comments name the LAB and explain semantics), `tests/test_recomp_boot.c`.
* Existing docs: `~/BattleSquadron-Amiga/docs/MAP.md`, `docs/module-map.json` (overlay files, load addresses,
  BOND-packed), `docs/porting-pipeline.md`. Extracted modules: `~/BattleSquadron-Amiga/original/modules/*.bin`.
* Oracle host: `~/BattleSquadron-Amiga/build/battle_squadron_native` (Musashi). Options: `--data DIR --frames N
  --video --dump-frame F --dump-frame-seq PREFIX --dump-every K --dump-state N --autofire --fire --objlog PATH
  --dump-file BASE LEN PATH`; env `BS_STATELOG=path BS_STATELOG_FROM=frame BS_STATELOG_MAX=n BS_PCSET=path
  BS_WATCH=lo-hi[,..] BS_HOLD=hex BS_ONE_PLAYER=1 BS_AUTOPILOT=1 BS_INVULN=1`. It runs ~1000 display
  frames/second headless, so RE-RUNNING WITH A DIFFERENT WATCH/TRACE IS CHEAP — do it whenever a claim needs
  checking. Source: `~/BattleSquadron-Amiga/src/host/{main.c,amiga.c}`.

## Captures in `/home/jon/BattleSquadron-Native/re/trace/` (all: level 1, one player, autopilot sweep)
* `objlog_auto_12000.txt` (honest autofire, player dies and restarts), `objlog_invuln_20000.txt`
  (BS_INVULN, 9260 game frames), `objlog_fire_30000.txt` (invuln + fire held: many kills, score 33875).
  Format (one line per live record, sampled once per GAME frame at PC $BE8 = right after the $BCE raster wait,
  i.e. after the four LAB_79E2 passes and before the $CDA wave scheduler; F = DISPLAY frame = bs_frame_no;
  the game loop runs every 2 display frames = 25 Hz):
  - `F G gframe(-28552) scroll(7204) progress(7206) stage(7228) half(-28551 byte) g7222 g7212 msg8514 demo(-28516)`
  - `F P n x(+4) y(+6) b38 b39 b48 b49(death timer) w52(invuln) b56(lives) w60(weapon level) w68`
  - `F S n.slot x y vx vy dmg` (player shots, 12 x 12 bytes at player+122)
  - `F H slot x(+0) y(+4) type(+31) b24 b27 b28 b29 b30 b57 b62 b63 l12(hex) l36(hex)` (hostile pool $2DC80, 12 x 80)
  - `F O slot x(+0) y(+2) type(+17) b19 b25 b28 b30 b31 b33 b42 b43 l12(hex)` (object pool $2E040, 18 x 64)
  - `F E slot x(+0) y(+4) vx(l8) vy(l12) b16 b17 b18 b19` (effect pool $4976, 16 x 20)
* `state_2000.bin` register trace from display frame 2000 (3M records ≈ 130 game frames, gameplay),
  `state_8000.bin` from display frame 8000. 18 x u32 LE per record: pc, d0-d7, a0-a7, sr (state BEFORE the
  instruction at pc). `pcset.txt` = every PC executed in 20000 frames (title + gameplay). Helpers:
  `python3 /home/jon/BattleSquadron-Native/tools/bs_trace.py hits PC [N] | calls LO-HI | seq PC N | mem DUMP ADDR LEN`
  (import it for ad-hoc scripts: recs(), chip(), W/SW/L/B).
* RAM images (whole 512K chip, A5=$8000 so global N(A5) is at byte $8000+N): `chip_2700.bin` (early play),
  `chip_invuln_20000.bin`, `chip_auto_12000.bin`. Screenshots `shots/{a,i,f}_NNNNN.ppm` (352x288 P6).
* Stage 7228 stays 0 in every capture: the level loops (progress wraps at ~8000) because the stage-end gate
  (object type $27) needs the ship to sit in its box. Hostile types seen: 0,1,3,4,6,7,8; object types seen:
  01,20,21,22,25,26,27. Types not in the captures must be decoded from the listing alone (say so).

## Known structure (from main.c/runtime.c; verify, don't assume)
* Main loop LAB_AA0..LAB_CE6 (loader.asm lines ~596-770): ONE iteration = one game frame = two raster frames.
  Order: LAB_9C44 scroll; LAB_5F34 (object pool update, $2E040); LAB_5BFE/5BD2 (render list restore/draw),
  LAB_5E72/5EA6; LAB_79E2 x4 (hostile pool passes selected by -1792/-1791(A5) = upper/lower half, pass);
  [$BCE wait] LAB_5502; LAB_9A9E input; LAB_3F44 player (fire LAB_3F54/3FC4/3FD0); LAB_1E02; LAB_5050; LAB_51EA;
  LAB_1D0C; LAB_4ADA (effects $4976); LAB_55AE; LAB_A30E messages; LAB_34FA/3424 (player shots vs objects /
  hostiles), LAB_3748, LAB_35F0 (player vs effects/hostiles), LAB_44D0, LAB_410A (level end), LAB_27EE, LAB_1420,
  LAB_1FAE, LAB_4704, LAB_2088; [wait] LAB_5502, 1E02, 5050, 51EA, 1D0C, 4ADA; LAB_7556 ($CDA wave scheduler:
  list at -2736(A5) of 12-byte entries {trigger.w, x.w, y.w, type.b, pad.b, script.l} vs progress 7206),
  LAB_3078 (tile-triggered object spawner, templates $2B68..$3048, 48 bytes each), LAB_139C (frame end:
  -28552++), [wait] LAB_55AE, LAB_A30E, LAB_7002 (stage clear).
* Hostile record (80 bytes): +0 x.w (+2 frac), +4 y.w (+6 frac), +8.l / +12.l per-type (type 0: +8 duration/dir/turn/
  timer bytes, +12 script ptr; type 1: +12 = velocity 16.16), +16..23 collision box (from descriptor),
  +24 armour/hp, +27/+28 per-type, +29 explosion countdown, +30 flags (bit6 = done this pass, bit5/7 ...),
  +31 TYPE, +32/+36 gfx ptrs (mask/plane), +44/46/48/50/52/54 blit geometry, +57, +62 damage taken, +63 frame.
  Descriptor table $CD7A + type*$20 (14 types $00..$0D): +0 h, +2 w(words), +4..+11 box, +12 hp, +16/+20 gfx,
  +24, +26, +27, +28.
* Object record (64 bytes, template 48 bytes copied from $2B68..): +0 x, +2 y, +4, +6.l, +8, +10, +12.l, +16,
  +17 TYPE, +19, +20.l, +24, +25, +26, +28 health, +30, +31 flags (bit5), +33, +36, +42, +43 (random & mask +1),
  +44.l. Types: 01..05 (LAB_5F34 $6170..$6640), $20..$29 ($5F56, $6894..$6E68).
* Player record ($4E3C / $4F46, 266 bytes): +2, +4 x, +6 y, +12.. shot states(12 bytes), +24.l shot table ptr,
  +28, +30, +32, +38, +39, +44 joystick bits (0 up,1 right,2 down,3 left,4 fire,5 nova), +46 cooldown,
  +48, +49 death timer, +52 invulnerability, +56 lives, +57, +58 weapon index, +59, +60 weapon level (0..5),
  +66 nova charges (<=8), +68, +91, +97 bonus BCD, +106..+113 score as 8 ASCII digits, +122 shots (12 x 12:
  x,y,vx,vy,..,dmg@+11).
* RNG: $2B1E reads a byte from a 256-byte table via a self-modifying pointer at $2B1A (low byte $2B1D ++).
* Sound: `JSR EXT_2470E` with D0 = effect number (LODGAM $24D6E stages a sample descriptor from $2539C).

## Output rules
* Write C PSEUDOCODE per routine (not 68k), with the LAB/address in a comment, field names as `rec->f<offset>`
  / `g<offset>(A5)`; name things when their role is proven. Mark each claim `(V: how)` = verified against
  trace/objlog/RAM, or `(L)` = from the listing only. Do not invent: if unclear, quote the listing and flag.
* Positions: screen-space x/y words with 16.16 fractions for hostiles; say which units each pool uses and
  how scroll 7204 enters.

## ADDED 22:06 — underground stage captures (BS_STAGE=3000:N forces the stage-clear transition at frame 3000)
* `re/trace/objlog_stage{1,2,3}_16000.txt` (invuln + fire held), `chip_stage{1,2,3}_16000.bin`, `state_stage{1,2,3}.bin`
  (register trace from display frame 6000, 1.5M records each), `shots/s{1,2,3}_NNNNN.ppm`.
* Coverage now: hostile types 00,01,03,04,05,06,07,08,09,0a,0c,0d (stage 1 reaches its boss at progress 3100:
  types 09/0a = boss; 0c; stage 2: 0d en masse); NOT seen: 02, 0b. Object types seen: 00,01,02,03,20,21,22,25,26,27,29;
  NOT seen: 04,05,28. Stage 0 = surface (hub with hangars), stages 1-3 = underground; finishing one returns to 0.
