# Battle Squadron (Amiga) — frame loop, player, shots, effects, game flow (engine-level decode)

Companion to `re/BRIEF.md` (sources, captures, record layouts). All addresses are LOADER runtime
addresses (`LAB_xxxx` = `$xxxx`), `gN` = `N(A5)` with A5 = $8000 (so gN lives at byte $8000+N of a chip
image), `p->fN` = byte N of a player record (P1 = $4E3C, P2 = $4F46), `A6 = $DFF000`.
Marks: **(V: how)** = checked against the register trace / objlogs / RAM images, **(L)** = listing only.
Tools used: `tools/bs_trace.py`, `tools/effect_stats.py` (output in `re/stats/effect_stats.txt`).

Units used below:
* **display frame / tick** = one 50 Hz raster frame; **game frame** = one iteration of the main loop =
  2 display frames (V: objlog `F` advances by 2 per `G` line; `-28552` advances by 2 per iteration).
* Player/shot/effect positions are in **sprite space**: `screen_x = x - g7204`, `screen_y = y - 256`
  (V: LAB_5374/LAB_4DDC subtract exactly that before LAB_5534 builds the hardware sprite; LAB_5534 then
  adds $8F/$26 = DIW origin). The ship's own position `p->f0/f2` is in **playfield space**
  (`screen_x = f0 - 256`, `screen_y = f2 - 256`); `p->f4 = f0 + g7204 - 256`, `p->f6 = f2` are the
  sprite-space copies used by everything else (V: LAB_5168 + objlog x(+4) = 608 when f0 = 512, g7204 = 352).

--------------------------------------------------------------------------------------------------
## 1. Main loop & timing  (LAB_AA0..LAB_CE6, loader.asm lines 596-770)

One iteration = one game frame = two display frames. Every raster wait reads `VPOSR/VHPOSR` as a long,
masks `$1FF00` (= raster line << 8) and spins; the numbers below are raster LINES (PAL frame = 312
lines, display ≈ 44..300).

```c
// LAB_AA0 — one GAME frame.  (V: order of every call below = trace, 131 iterations, identical each time)
for (;;) {
    g12988 = 0x0B10;                                   // copper colour restore (L)
    // render-list double buffer: bit1 of the DISPLAY frame counter selects which list is "current"
    A1 = 0x7834; A2 = 0x7770; if (g_28551 & 2) swap(A1, A2);
    g_1800 = A1; g_1796 = A2;                          // object render lists (restore/draw) (L)
    if (g_26242) goto final_boss_variant;              // LAB_ACA, see below (L)

    wait_raster(line >= 0x7E && line < 0x126);         // [126, 294)
    LAB_9C44();                                        // scroll: camera x, progress, one terrain row      (once)
    LAB_5F34();                                        // OBJECT pool update ($2E040, 18 x 64)             (once)
    wait_raster(line >= 0xA6);                         // >= 166
    LAB_5BFE(); LAB_5E72();                            // restore previous draws / draw render list, UPPER half (once)
    g_1792 = 0; g_1791 = 0; LAB_79E2();                // HOSTILE pool pass: upper half (y <= $146), pass A
    g_1791 = ~0;            LAB_79E2();                //                    upper half, pass B
    wait_raster(line >= 0x126 || line < 0xA6);         // beam past the upper half (or already in next frame)
    if (g8514 == 0 && g_28550 < 0x5DC) LAB_27EE();     // HUD copper rebuild (score/lives/nova/weapon bars)
    LAB_5BD2(); LAB_5EA6();                            // restore / draw render list, LOWER half            (once)
    g_1792 = ~0; g_1791 = 0; LAB_79E2();               // hostiles lower half (y > $146), pass A
    g_1791 = ~0;            LAB_79E2();                //                lower half, pass B
    wait_raster(line >= 0xD8 && line < 0xFE);          // [216, 254)   <<< objlog sample point is the PC after this ($BE8)
    LAB_5502();                                        // DISPLAY FRAME ++ (g_28552), sprite double-buffer select, blitter setup
    if (g8514 == 0 && g_28550 < 0x5DC) {               // not while a message overlay runs / demo playback ended
        LAB_9A9E();                                    // input -> p->f44 (both players)                     (once)
        LAB_3F44();                                    // nova trigger + primary fire (both players)         (once)
        LAB_1E02(); LAB_5050(); LAB_51EA(); LAB_1D0C(); LAB_4ADA();   // (1st of 2) see "twice" list
    }
    LAB_55AE();                                        // bitplane pointers -> copper (double-buffered by frame bit0)
    LAB_A30E();                                        // message overlay driver (advances g8514)
    // collision "selected player" alternates every game frame (bit1 of the display counter):
    g_18624 = (g_28551 & 2) ? 0x4F46 : 0x4E3C;  g_18620 = g_18624 + 0x72 /* score digit end */;
    LAB_34FA();                                        // selected player's 12 shots vs OBJECT pool
    LAB_3424();                                        // selected player's shots vs HOSTILE pool
    LAB_3748();                                        // selected player vs EFFECT pool (enemy bullets)
    LAB_35F0();                                        // selected player vs HOSTILE pool (incl. pickups type 5)
    LAB_44D0();                                        // HUD score/hiscore digits; in demo: g_28550++ and attract screens
    LAB_410A();                                        // game over detection / initials entry
    if (g8514 == 0 && g_28550 < 0x5DC) { LAB_27EE(); LAB_1420(); LAB_1FAE(); }   // HUD copper, palette flash, colour cycle
    LAB_4704();                                        // pending enemy-fire requests -> effect pool (bullets/missiles)
    LAB_2088();                                        // keyboard: CASTOR cheat
    wait_raster(line < 0xA0);                          // next display frame (line < 160)
    LAB_5502();                                        // DISPLAY FRAME ++ (2nd)
    if (g8514 == 0 && g_28550 < 0x5DC) { LAB_1E02(); LAB_5050(); LAB_51EA(); LAB_1D0C(); LAB_4ADA(); }   // (2nd)
    if (!(g8514 && g_28516 && g_28550 >= 0x1F4 && g_28550 < 0x3E8))   // demo: no new waves during the 1st overlay
        LAB_7556();                                    // wave scheduler (list at g_2736 vs progress g7206)
    LAB_3078();                                        // tile-triggered spawner (map row words -> object templates) + gates
    LAB_139C();                                        // extra-life check (alternates P1/P2 by frame bit2)
    wait_raster(line >= 0x3E && line < 0xA6);          // [62, 166)
    LAB_55AE(); LAB_A30E();                            // bitplane pointers, message driver (2nd)
    LAB_7002();                                        // stage clear sequence (only when g_4100 set)
    g13182 = 0x2835FFFE;                               // copper wait restore (L)
    if (g_28516) { /* attract demo: fire = start game (LAB_D52); g_28550 >= 4000 -> title (LAB_58A) */ }
    else if (g_4096) { /* game over finished -> LAB_E04 fade-out loop -> title */ }
    else continue;                                     // LAB_DFA: back to LAB_AA0
}
```
**Runs once per game frame:** 9C44, 5F34, 5BFE/5E72, 5BD2/5EA6, 79E2 (4 half/pass variants), 9A9E,
3F44, 34FA, 3424, 3748, 35F0, 44D0, 410A, 1420, 1FAE, 4704, 2088, 7556, 3078, 139C, 7002.
**Runs twice (once per display frame):** 5502, 1E02, 5050 (player movement!), 51EA (ship+shot sprite
lists: shots move here), 1D0C (nova), 4ADA (effects move here), 55AE, A30E.  27EE up to twice.
(V: trace = `c98 cda cde ce2 d00 aa0 b34 ... ` and per-iteration entry list
`9c44 5f34 5bfe 5e72 79e2 79e2 27ee 5bd2 5ea6 79e2 79e2 5502 9a9e 3f44 1e02 5050 51ea 1d0c 4ada 55ae a30e 34fa 3424 3748 35f0 44d0 410a 27ee 1420 1fae 4704 2088 5502 1e02 5050 51ea 1d0c 4ada 7556 3078 139c 55ae a30e 7002`.)

### Final-boss variant LAB_ACA (g_26242 != 0)  (L — not in any capture)
`g_26242` is set by LAB_9C44 when `g_4099 == $0E` (all three inner stages cleared) and progress hits
$F0: it loads module LODFIN ($1B18) over the map/tile area $44000 and sets the stage-3 palette pointer
(`g_27622 = $169A`, `g_27618 = 1`). The loop then uses: wait [96,294) → LAB_9C44, LAB_5BFE, LAB_5BD2;
wait ≥160 → g_1792 = 0, g_1791 = $FF, LAB_79E2; g_1792 = $FF, LAB_79E2; LAB_27EE; wait < 160 → BRA LAB_BCE
(the rest is identical). I.e. no OBJECT pool update, only two hostile passes (pass B of each half).

### LAB_5502 ($5502)  `display_frame_begin()`  (V: trace, called at $BE8 and $C98)
```c
g_28552++;                                 // DISPLAY frame counter (word). g_28551 is its LOW byte.
g_11824 = (g_28551 & 1) ? 0x2000 : 0;      // sprite-list double-buffer offset (word; g_11826 = the long view of it)
BLTAFWM/ALWM = -1; BLTCON0 = $09F0; BLTCON1 = 0; BLTAMOD = BLTDMOD = 0;
```
So: **-28552 = display frames** (objlog `gframe`), **-28551 bit0** = odd/even display frame (sprite
buffers, effect sub-steps), **bit1** = alternates per GAME frame (render-list pair, which player is the
"selected" collision player), **bit2** = alternates per 2 game frames (LAB_139C P1/P2).

### Gates
* `g8514` (word) = **message overlay frame counter**; non-zero while a text overlay (stage intro,
  "GOOD WORK", game over, demo captions) is running: input/player/effects/HUD are skipped, scroll and
  pools keep running; the spawner LAB_3078 stops only when g8514 >= $7530 (never in practice).
* `g_28550` (word) = **attract-demo frame counter**: only incremented by LAB_45D2 (inside LAB_44D0, demo
  mode only, once per game frame). In a live game it stays 0, so every `< $5DC` test is true. In the demo:
  at $5DC (1500) play stops (input/player gated off), waves switch to $CF3A, "scores" overlay;
  $7D0/$9C4/$BB8/$DAC = credits text pages ($2E508.. in LODS0F); at $FA0 (4000) back to the title.
* `g_28516` (byte) = **demo / recorded input mode** ($FF in attract). LAB_9A9E then reads two bytes per
  game frame (P1, P2 joystick bytes) from the stream pointer `g6810` straight into `p->f44`.
* `g8524` = -1 once game over has been triggered (LAB_410A).  `g_4096` = game finished flag (LAB_DFA
  leaves the loop).  `g_26241` toggled by the ESC/quit check at LAB_DBE (title return).
* `g_1792` = hostile pass half (0 = records with y <= $146 (upper), $FF = lower), `g_1791` = pass
  A/B: pass A only processes hostile types 3, 7, $0D, pass B the rest (L: LAB_79F2..7A2C). Each record
  is processed once per game frame (bit6 of +30 = "done this frame").

--------------------------------------------------------------------------------------------------
## 2. Globals table (N(A5) met in this part)

| global | size | meaning | mark |
|---|---|---|---|
| -28552 | w | display frame counter (2 per game frame); -28551 = its low byte (bits 0/1/2 used as phase bits) | V |
| -28550 | w | attract-demo frame counter (0 in a live game) | V(objlog demo=0 → always 0) |
| -28516 | b | demo / recorded-input mode | V |
| 6810 | l | recorded input stream pointer (2 bytes per game frame) | L |
| 8514 / 8516 / 8518 / 8522 | w w l w | message overlay: frame counter / line count / line list ptr / hold duration | V(objlog msg column) |
| 8524 | w | game-over triggered (-1) | L |
| -18624 / -18620 | l l | "selected" player record / its score-digit end pointer for this game frame's collision passes | V(trace a4 at $3752 alternates) |
| -1800 / -1796 | l l | object render lists (restore / draw) for this frame (swap by frame bit1) | L |
| -1792 / -1791 | b b | hostile pass selectors (half / pass) | L |
| -11824 (-11826.l) | w | sprite-list double buffer offset 0 / $2000 | V |
| 7204 | w | camera x (sprite-space origin). 256 + 3/8·(P.x−256) one player; held at $130 with no ship | V |
| 7206 | w | map progress in pixel rows (+1 per scrolling game frame) | V |
| 7208 | l | terrain ring buffer: address of the TOP visible row ($62000..$64FD0, step −$30, 256 rows x 48 bytes/plane, planes $6000 apart, second copy at +$3000) | L |
| 7212 | w | row phase inside the current 16-row tile strip (30,28,…,0 then reload) | V(objlog g7212 30→0 step 2) |
| 7214 | l | map cursor: address of the 24-word map row being drawn; starts $4A000, −$30 per strip, wraps at < $44000 | L |
| 7218 | w | rows until the ring wraps (256 → … ; reset $FF at wrap) used by the restore logic | L |
| 7222 | w | "terrain scrolled this frame" flag (1/0) — everything scroll-locked adds it | V(objlog g7222) |
| 7224 | l | current stage descriptor ($14EA + stage·$8C) | V(RAM: $14EA, stage 0) |
| 7228 / 7230 / 7232 | w w w | current stage / pending (next) stage / last completed stage | V(7228=0 in captures) |
| 7234 | w | stage-3 end delay (250 frames) before the stage-clear toggle | L |
| -2736 | l | wave list pointer for LAB_7556 | V(RAM $D08E inside list $D00A) |
| -4100 | b | stage-clear toggle (LAB_7002 runs when set) | L |
| -4099 | b | completed-stage bits: bit1/2/3 = inner stage 1/2/3 done; $0E = all → final boss | L |
| -4098 | w | cleared at game init (unused here) | L |
| -1570 | b | boss/“hold scroll” flag ($64 set by hostile code; with progress $1FD6 or $C1C stops the scroll) | L |
| $A14C | w | external "scroll hold" word (TST only; cleared at init; writer not found) | unclear |
| -25334 | b | NOVA timer (255 → 0, one per game frame) | V(objlog: nova not seen; L) |
| -25338 | l | nova ring script pointer (table $171B0 of divisors) | L |
| -27618 / -27622 | b l | palette flash countdown / base stage palette pointer (LAB_1420) | L |
| -26242 | b | final-boss (LODFIN) mode | L |
| -26244 / -26243 | b b | copies of title options 10941 / 10953 (sound/music toggles, used by LODGAM entry) | L |
| -26245 / -26246 | b b | module-loaded flags (LODGAM / LODS0S) | L |
| -2732 | b | TWO-PLAYER flag (set when both +39 active bytes are non-zero at game start, $95A) | L |
| 10059 / 10060 / 10062 / 10064 / 10066 | b w w w w | title options: start lives / start WEAPON TYPE (0..3) / max enemy bullets (15) / enemy bullet speed ($180 = 1.5 px/tick) / difficulty 0..2 | V(RAM: 3,3,15,384,1) |
| -2200..-2194 | 7 b | enemy armour table chosen by difficulty ($23.. / $32.. / $4B..), reduced by 5/10/15 per inner stage cleared (LAB_7336) | L |
| -14386 | w | enemy bullet speed (= 10064) | V(RAM 384) |
| -14398 / -14396 | w w | enemy-fire request origin x/y; -14394 save; -14390/-14388 forced vx/vy (non-zero = fixed direction); -14384 = "stationary/missile" request | L |
| -13962 / -13942 | w w | effect pool y-sort enable (both non-zero → sort) | L |
| -16120 / -16122 | b w | "no ship alive" / game-over hold counter ($FA) | L |
| -24542 / -24541 | b b | CASTOR cheat active / cheat colour tint counter | L |
| -24444 / -24442 | w w | cheat weapon level·4 / weapon type·24 | L |
| -12740.. / -12474.. | | = P1 / P2 record fields via A5 (e.g. -12702 = P1.f38, -12696 = P1.f44, -12672 = P1.f68, -12650 = P1.f90) | V |

--------------------------------------------------------------------------------------------------
## 3. Scroll / map consumption & stage descriptors

### LAB_9C44 ($9C44..$9EA4) `scroll_frame()` — once per game frame (V: progress +1 per G line)
```c
if (g7234) { if (--g7234) goto display; goto stage_end; }          // stage-3 end delay
g_9266 = g7208; g_9268 = g7218;                                     // snapshot for the restore pass (L)
// camera target from the live ships (f38 >= 0 = ship present):
n = 1; sum = 0;
if (P1.f38 >= 0) { sum += P1.f0 - 256; n++; }
if (P2.f38 >= 0) { sum += P2.f0 - 256; n++; }
target = (n == 1) ? 0x130 : ((sum >> n) + (sum >> n >> 1)) + 256;   // one ship: 256 + 3/8·(x−256)   (V: trace tgt column)
if (target != g7204) g7204 += (target > g7204) ? 1 : -1;             // camera pans 1 px per game frame (V)
g7222 = 1;
if (g_4099 == 0x0E && g7206 == 0xF0) { if (!g_26242) load LODFIN, g_26242 = ~0, g_27622 = $169A, g_27618 = 1; g7222 = 0; goto display; }
if (*(word*)0xA14C || (g_1570 < 0 && (g7206 == 0x1FD6 || g7206 == 0x0C1C))) { g7222 = 0; goto display; }   // scroll held (boss)
g7206++; g7218--;
if ((g7212 -= 2) < 0) {                                             // finished a 16-row tile strip
    g7212 = 30; g7214 -= 0x30;                                      // next map row (24 words = 48 bytes) — map is read from the END backwards
    if (g7214 < 0x44000) {                                          // map exhausted (512 rows · 16 = 8192 px)
        if (g7228 == 0) {                                           // planet surface LOOPS:
            g7214 = 0x49FD0; g7206 = 1;                             //   restart at the last map row, progress 1 (V: objlog wrap ~8000→1)
            g_2736 = (g_4099 == 0x0E) ? 0xCF90 : stage->f4;         //   wave list for the repeat ($CFCE for stage 0)
        } else if (g7228 == 3) { g7234 = 0xFA; g7222 = 0; goto display; }   // stage 3: 250 frame delay then end
        else { stage_end: g_4100 = ~g_4100; g7230 = 0; g7232 = g7228; g7222 = 0; goto display; }   // inner stage ends at map end
    }
}
if ((g7208 -= 0x30) < 0x62000) { g7208 = 0x64FD0; g7218 = 0xFF; } // ring buffer wraps (256 rows)
// blit the new top row: 24 tiles, 1 word wide, 5 planes, from tile set $4A000 (tile = 16 rows x 2 bytes x 5 planes, planes $20 apart)
BLTAMOD = 0x1E; BLTDMOD = 0x5FFE; BLTCON0 = 0x09F0; BLTCON1 = 0; masks = -1;
A2 = g7214; A0 = g7208; A4 = A0 + 0x3000; A3 = 0x4A000 + g7212;
for (24 columns) { src = A3 + 2 * *A2++; blit(src -> A0, BLTSIZE $0141); blit(src -> A4, $0141); A0 += 2; A4 += 2; }
display:
x = g7204; if (g_25334 /*nova*/ && !(g_28551 & 2)) x += (g_25334 >= 0xBA) ? 3 : (g_25334 - 0x8A) >> 4;  // screen shake
// bitplane pointers for the 5 terrain planes into the copper ($B278..): plane p = g7208 + ((x-256)>>3 & ~1) + p*$6000
// BPLCON1 ($B26C) = ((15 - x) & 15) replicated in both nibbles (fine scroll)
```
So the visible 320 px window sits `g7204-256` px (0..64+) into the 384-px-wide terrain; the map word is
`tile_index*16` (word*2 = byte offset of a 32-byte-per-plane tile — map format itself: see the assets doc).
"Progress wraps at ~8000" in the captures = the planet-surface loop above (512 rows · 16 px = 8192, minus
the pre-roll; the capture shows 7206 goes 8192 → 1 at F=17545, V: objlog_invuln).

### Stage descriptor `$14EA + stage*$8C` (140 bytes)  (V: chip_2700 dump)
| off | field | stage 0 | stage 1 | stage 2 | stage 3 |
|---|---|---|---|---|---|
| +0.l | wave list (LAB_7556) at stage start | $0D00A | $2E89A | $0D7F8 | $0DF86 |
| +4.l | wave list used when the map loops (stage 0: the repeat list) | $0CFCE | $2E89A | $0D7F8 | $0DF86 |
| +8.l | LoadModule descriptor of the stage data | $19E0 LODS0F→$2E508 | $1A28 LODST1→$2E89A | $1A40 LODST2→$2E4C0 | $1A58 LODST3→$2E840 |
| +12 | palette, 32 words (colour 0..31), word 0 = 0 | $14F6: 0000 0331 0EEE 09C4 0470 0240… | $1582 | $160E | $169A |
| +76 | alternate palette, 32 words (flash target, LAB_1420) | $1536: 0000 0430 0EEE 0F60 0A42… | $15C2 | $164E | $16DA |
Extra palette blocks after the table: $171A (stage 3 mid-section palette, used by LAB_1420 while
$079E <= progress < $0FA0), $183A/$187A/$18BA (stage-0 palettes after 1/2/3 inner stages, LAB_7336),
$14F6 again after 0.  Map/tiles for stage 0 = LODS0T ($44000..$54958 incl. tiles at $4A000); inner
stages load their own LODSTn (graphics+map), stage 0 reloads LODS0F+LODS0S+LODS0T on return (LAB_7180).
There is NO length field: the map is always 512 rows read backwards from $4A000; progress 7206 is
the row counter; wave entries carry their own trigger progress.

### Who consumes the map
* LAB_9C44 draws one 16-px tile strip row-by-row (above).
* LAB_3078 (tile-triggered spawner, once per game frame): only when `g7222 && g7212 == 0` (a strip was
  just completed), looks at the 24 words of the row at `g7214 - 0x30` and spawns object templates for
  magic tile values ($6180, $5640, $0280 (only for $3E8 <= progress < $1F40), $8020, $5D20, …; per
  stage different tables) — the objects agent documents the templates. The GATES are here too: in
  stage 0 with `g7230 == 0`, at progress **$F10 / $1490 / $1DD0** (if the matching -4099 bit is clear)
  it spawns template $2FB8 (the hangar entrance, object type $27) and sets **g7230 = 1 / 2 / 3**. The
  type-$27 object toggles `g_4100` when the ship sits in its box (brief), which starts LAB_7002.
* LAB_7556 (wave scheduler, once per game frame): `while (list->trigger <= g7206 && g7222) spawn …`
  (12-byte entries, see BRIEF) — other agent.

### LAB_7002 ($7002) stage clear / LAB_7180 stage advance  (L; autopilot never reaches it)
```c
if (!g_4100) return;
sound (stage 0: EXT_2474A, else EXT_2472C); fade palette (LAB_1CB0); clear playfield;
g_28552 = 0; g8514 = 1;                       // start an overlay
text = stage ? (popcount(g_4099)==2 ? $A064 (11 lines, hold $190) : $9FC2 (8, $140)) : $9F34 (7, $118);
while (g8514) { wait vblank; LAB_A30E(); LAB_55AE(); g_28552 += 2; }
// bonus count-down: text $9EA6 (7 lines, hold $172); per game frame (LAB_73EA, from frame $A0 on, even frames):
//   p->f97 (BCD bonus units, max 99, collected from object hits at $6A38) -> f97--, f98++ (BCD), score += 1000 per unit
//   (LAB_40DE with $73C2: raw nibble digits 00000000 01000000 = +1000), sound 59 on P1's turns.
while (g8514) { ...; LAB_139C(); g_28552++; }
g7232 = g7228; g7228 = g7230; g7224 = $14EA + g7228*$8C;       // LAB_7180: ADVANCE
load stage module (stage 0: LODS0F + LODS0S + LODS0T; stage 1 also patches 8 object pointers $F068.. -> $E924..);
fade; LAB_11C6 (reset frame state: counters, pools, g7204 = $130, g7208 = $65000, g7212 = 0, g7218 = $100, g_2736 = stage->f0, g7222 = 0);
// returning to the surface: resume where the gate was
if (g7232 == 1) { g_4099 |= 2; row = $EA;  g_2736 = $D3BE; }
if (g7232 == 2) { g_4099 |= 4; row = $142; g_2736 = $D562; }
if (g7232 == 3) { g_4099 |= 8; row = $1D7; g_2736 = $D796; }
g7206 = row << 4; g7214 = $4A000 - row*$30;
reset both players (LAB_74E8: respawn unless dead/game over), clear playfield;
for (256 frames) { LAB_9C44(); LAB_5F34(); LAB_3078(); }   // pre-roll: fills the 256-row ring and spawns the first objects
palette = LAB_7336 (stage 0 after n inner stages -> $14F6/$183A/$187A/$18BA; armour table -2200.. -= 5/10/15 by difficulty; -8414 -= 2, -8413 -= 10);
LAB_1C2C fade in; music EXT_24720; goto LAB_AA0;
```

--------------------------------------------------------------------------------------------------
## 4. Player

### Record fields (266 bytes; P1 $4E3C, P2 $4F46; initial image in the listing at $4E3C/$4F46)
| off | meaning | mark |
|---|---|---|
| +0.w / +2.w | ship x / y in playfield space (x 256..512, y 258..480); init x = f54 (P1 $140, P2 $1C0), y = $200 (off the bottom) | V |
| +4.w / +6.w | sprite-space copies: f4 = f0 + g7204 − 256, f6 = f2 (written by LAB_5168 each display frame) | V |
| +8.w | sprite height of the ship (30; $3C while exploding) | L |
| +10.w | banking frame 0..12 (6 = level), steps of 2 every 4th display frame | V(objlog via trace d5) |
| +12..+23 | 12 shot-slot state bytes from the weapon table: 0 = launch slot (refilled on fire), 1 = in-flight copy slot, −1 = unused | V(RAM) |
| +24.l | weapon table pointer (template base = table+24) | V |
| +28.w | fire period (cooldown reload) | V(3 for weapon 3 L0) |
| +30.w | number of records rolled from the launch slots into slots 0.. on a new volley | V |
| +32.w | extra rotate count for staggered banks (0 for most levels; 2/3 for weapon 1 L3..5) | L |
| +34.l | 4 initials chars (game over) | L |
| +38.b | ship state: 0 = flying, $96 (150) = entering, $64 (100) = exploding, $C8 (200) = dead/initials, $FF = no ship (not joined) | V(objlog b38) |
| +39.b | player joined/active ($FF) | V |
| +40.b | initials auto-repeat | L |
| +41.b | "free respawn" flag (no life lost) | L |
| +42.w | initials cursor | L |
| +44.b | input bits: 0 up, 1 right, 2 down, 3 left, 4 fire, 5 nova | V(trace: joy $18 = left+fire) |
| +45.b | fire-release latch for initials | L |
| +46.w | fire cooldown (counts down 1/game frame) | V |
| +48.b | entry animation counter ($91 = 145 → 0; ship controllable/forced-up below 60) | V(objlog b48) |
| +49.b | explosion timer ($46 = 70 → 0) | V(objlog b49) |
| +50.w | explosion sprite gfx index (f10 copy) | L |
| +52.w | invulnerability frames (300 after respawn; $270F while exploding) | V |
| +54.w | spawn x | V |
| +56.b | lives (init from option 10059 = 3, max 4) | V |
| +57.b | auto-repeat counter (15 after a shot) | V(runtime.c parity note) |
| +58.w | WEAPON TYPE 0..3 = title option 10060 (never changed by pickups) | V(RAM 3) |
| +59.b | pickup subtype>>1 of the last pod — only used for the HUD weapon colour (LAB_28F0) | L |
| +60.w | weapon level 0..5 (+1 per pod, halved on respawn) | V |
| +62.w / +64.w | shot collision half-size w/h from the weapon table (+20) | L |
| +66.w | nova charges (init 3, max 8) | V(P1 init) |
| +68 / +70 / +72.w | HUD ship-portrait dissolve: phase (128 at respawn counting down; $384+ reveal mode), twin, pixel cursor | V(objlog w68) |
| +74/+75.b | gfx indices of the two 16-px ship sprites | L |
| +76.l / +80.l / +84.l | portrait image pointers ($10910 live, $10C10 dead) | L |
| +88.w | HUD portrait x | L |
| +90.b | nova in progress ($FF: blocks firing) | L |
| +91.b | initials done | L |
| +92..+95 | last 4 single joystick directions (nova gesture), +96 gesture timeout | L |
| +97.b / +98.b | BCD bonus units collected / counted | L |
| +99.b | mouse mode (read JOYxDAT deltas, second button = nova) | L |
| +100.b | cleared at init | L |
| +102.l | pointer to the explosion sprite record | L |
| +106..+113 | score as 8 ASCII digits; +114..+121 previous-frame copy (extra-life detection) | V |
| +118/+119 | mouse previous x/y; +120.w = initials timeout ($2EE) | L |
| +122.. | 12 shot records × 12 bytes (below) | V |

### LAB_9A9E ($9A9E) `read_input()` — once per game frame
```c
if (g_28516) { P1.f44 = *stream++; P2.f44 = *stream++; g6810 = stream; return; }     // recorded demo
for (p, joydat, fire) in ((P1, JOY1DAT, CIAA PRA bit7), (P2, JOY0DAT, CIAA PRA bit6)) {
    if (p->f99) { /* mouse: dy = (joydat>>8) - f118, dx = joydat - f119; |d| >= 3..4 -> dir bits; POTINP bit -> nova */ }
    else {
        old = p->f44; p->f44 = 0;
        if (joydat & 0x002) f44 |= 2 /*right*/;  if (joydat & 0x200) f44 |= 8 /*left*/;
        t = joydat ^ (joydat << 1); if (t & 0x200) f44 |= 1 /*up*/; if (t & 0x002) f44 |= 4 /*down*/;
        // NOVA GESTURE: while fire is held (old bit4), the last 4 distinct single directions (f92..f95,
        // 12-frame timeout f96) covering all four -> f44 |= 0x20.  Release of fire clears the history.
    }
    if (!fire_button) p->f44 |= 0x10;
}
```
(V: the autopilot's host bits map to the same +44 values seen at $505E in the trace: left+fire = $18.)

### LAB_5050 / LAB_5070 ($5050) `move_ship(p, joy)` — TWICE per game frame (V: x changes 4/game frame)
```c
if (p->f52) p->f52--;                                  // invulnerability (display frames!)  (V: objlog w52 −2 per G line)
if (p->f38 == 0xFF) return;                            // no ship
if (p->f48) {                                          // entry animation
    if (--p->f48 == 0) { p->f38 = 0; if (p->f41) p->f41 = 0; else p->f56--;   // LIFE IS TAKEN when the new ship arrives
                         lives HUD (LAB_446A/447E); nova HUD (LAB_2A5E/2AA2); }
    joy = (p->f48 < 60) ? 1 /*forced up*/ : 0;
} else if (p->f38) return;                             // exploding / dead
if (joy & 1 && p->f2 > 0x102) p->f2 -= 2;              // speed 2 px per display frame, y >= 258
if (joy & 2) { if (!(g_28552 & 3) && p->f10 < 12) p->f10 += 2; if (p->f0 < 0x200) p->f0 += 2; }
if (joy & 4 && p->f2 < 0x1E0) p->f2 += 2;              // y <= 480
if (joy & 8) { if (!(g_28552 & 3) && p->f10) p->f10 -= 2; if (p->f0 > 0x100) p->f0 -= 2; }
if (!(g_28552 & 3) && !(joy & 10) && p->f10 != 6) p->f10 += (p->f10 > 6) ? -2 : +2;   // bank back to level
P1.f4 = P1.f0 + g7204 - 256; P1.f6 = P1.f2; (same for P2)
```
Ship sprite: 2 hardware sprites 16 px wide at (f0−256, f2−256) and +16, gfx f8/f10 → bank frame, built
by LAB_5374 into the per-player sprite lists ($5F000/$5F400 P1, $5F800/$5FC00 P2, + g_11824 buffer offset);
effects use $5E000..$5EC00 (LAB_4ADA), messages $5E000 too (while g8514).

### Death / respawn (LAB_3748, LAB_35F0 → "hit"; LAB_5234 ($5234..$5372))
```c
hit:   p->f38 = 0x64; p->f49 = 0x46 (70); p->f52 = 0x270F; p->f2 -= 12; sound EXT_24732;
// LAB_5234 every display frame while f49 != 0 (explosion, 7 phases of 10 frames, gfx $13190 + phase*$1E0):
if (--p->f49 == 0) {
    p->f8 = 30; p->f68 = p->f70 = 300; p->f72 = 0; p->f76 = p->f84 (dead portrait);
    if (p->f56 == 0) {                         // no lives left -> game over for this ship
        p->f38 = 0xC8; p->f42 = 0; p->f34 = "AAA]"; p->f40 = 0; p->f0 = p->f2 = 0x3E7; p->f120 = 0x2EE;
        if (score >= hiscore g32246) { p->f100 = 0xFF; p->f120 = 0x7D; ... }    // name entry length
    } else {                                   // respawn
        p->f48 = 0x91; p->f66 = 3; p->f68 = p->f70 = 0x80; p->f72 = 0; p->f76 = p->f80; p->f38 = 0x96;
        p->f0 = p->f54; p->f2 = 0x200; p->f10 = 6; p->f52 = 0x12C (300);
        p->f60 >>= 1;  weapon table = $2024[f58*24 + f60*4] -> LAB_2B32(p);     // LEVEL HALVES
    }
}
```
(V: objlog: b38 100 → b49 67,27 → 150 with b48 145, w52 300, w60 0; lives −1 when b48 reaches 0.)
Respawn flight-in: x = spawn x, y = 512 (below the screen), no control for 85 display frames, then
forced "up" for 60 (V: objlog y 512 → 392 over ~145 display frames).

### LAB_2B32 ($2B32) `set_weapon(p, table)` (V: RAM image of P1 vs table $3D18)
`f28 = t[0]; f58 = t[2]; f30/f32 = t[4..7]; f12..f23 = t[8..19]; f62/f64 = t[20..23]; f24 = t + 24`.
Weapon-level table `$2024[weapon*6 + level]` (4 weapons × 6 levels): weapon 0: $37B4 $37E4 $382C $388C
$38EC $394C; weapon 1: $39AC $39DC $3A0C $3A3C $3A84 $3AD8; weapon 2: $3B38 $3B68 $3BA4 $3BEC $3C40 $3CA0;
weapon 3: $3D00 $3D30 $3D60 $3DA8 $3DF0 $3E50 (tables overlap by 48 bytes: level n+1 header starts at
template 2 of level n — only the first "launch" templates are ever copied). Sound per weapon: byte
`$3F40[f58]`. (V: dump in re/stats would be the assets agent's; the header/template split verified
on the live P1 record: state 01 01 00 00 FF…, f30 = 2, templates (6,−25,0,−8,$18,$4D,1,1) (18,−25,…).)

--------------------------------------------------------------------------------------------------
## 5. Shots (12 × 12 bytes at p+122)

Record: `+0 x.w, +2 y.w (sprite space; before launch: offsets from the ship), +4 vx.w, +6 vy.w
(px per DISPLAY frame), +8 gfx.b, +9 height.b, +10 launch delay.b, +11 damage.b (−1 = penetrating, see
runtime.c apply_player_shot_hit)`. `x == 0` = free slot.

### LAB_3F54 ($3F54..$40A2) `fire(p)` — once per game frame  (V: runtime.c parity + RAM)
```c
if (p->f38) return;                                               // only a flying ship
if (p->f44 & 0x20 && !g_25334 && p->f66) NOVA (section 6);
if (p->f90) return;                                               // nova running: no primary fire
if (p->f46) { p->f46--; if (p->f57) p->f57--; if (!(f44 & 0x10)) p->f57 = 0; return; }
if (!(p->f44 & 0x10)) { p->f57 = 0; return; }
if (p->f57) { p->f57--; return; }                                 // held fire: 16 game frames per volley; tapping: every 4
// LAB_3FE2: volley bookkeeping
launch_live = #slots with state 0 and x != 0;  flight_live = #slots with state 1 and x != 0;
if (launch_live) {
    if (flight_live) return;                                      // both banks busy: no shot
    blit-copy f30 records from slot f30.. down to slot 0.. (the launch bank becomes the in-flight bank);
    if (p->f32) rotate the last f32 records (staggered banks);    // untranslated in runtime.c
}
tmpl = p->f24; for (s = 0; s < 12; s++) if (state[s] == 0) { slot[s] = *tmpl++; }   // refill launch slots in order
p->f46 = p->f28; p->f57 = 15; sound $3F40[f58];
```
Max shots in flight = 2 × (number of launch slots) (2..6 per bank → 4..12).

### Shot motion — inside LAB_51EA/LAB_5374 ($5374..$5444), TWICE per game frame (V: S lines: y −8 per display frame at vy = −8)
```c
for each slot with x != 0:
    if (slot.delay) { if (--slot.delay) continue;            // staggered launch
                      slot.x += p->f4; slot.y += p->f6;      // becomes absolute (sprite space)
                      if (slot.y >= p->f6) { slot.delay++; /* emit the ship sprites first, then re-run this slot */ } }
    else { slot.x += slot.vx; slot.y += slot.vy; }
    if (!(0xF3 < slot.y < 0x200) || !(g7204 - 16 < slot.x < g7204 + 288)) { slot.x = 0; continue; }   // off screen → free
    emit sprite (slot.x - g7204, slot.y - 256, gfx slot.gfx, height slot.h) into this player's list, ordered by y against the ship
LAB_5490: weapon 3, levels 1..3: every display frame add a sine wobble ($51D2[(frame&7)]) to the x of slot 0 and slot f30/… (wiggling shots)
```
Collision with enemies: LAB_34FA (objects) / LAB_3424 (hostiles), once per game frame for the
selected player only (so each player's shots are tested every 2nd game frame!) — shot box =
(x, y, x+f62, y+f64) (runtime.c player_shot_box); hit: damage applied, non-penetrating shot freed.

--------------------------------------------------------------------------------------------------
## 6. Nova (smart bomb)

Trigger (LAB_3F64): `f44 bit5 && f38 == 0 && !g_25334 && f66` → `g_27618 = $32` (palette flash),
sound 57, `f66--`, `f90 = $FF`, `g_25334 = $FF`, nova HUD (LAB_2A5E/2AA2). (L — no capture contains a nova.)
LAB_1D0C ($1D0C) twice per game frame:
```c
if (!g_25334) return;
if (g_25334 == 0xFF) { g_25338 = $171B0; sound EXT_24750; }
p = P1.f90 ? P1 : P2;  g_25334--; p->f90--;
d = *g_25338++; if (d < 0) { g_25334 = 0; p->f90 = 0; clear the effect pool (x = 0, f19 = 0 for all 16); return; }
// 8 ring records written into effect slots 0..7 each call (they overwrite whatever is there):
for i in 0..7: e = effect[i]; tab = $1727A + (frame & 7)*4 + i*$20;
    e.x = p->f4 + 8 + tab[0]/(d|1); e.y = p->f6 - 16 - tab[1]/(d|1); e.f16 = $10; e.f17 = $54 + ((frame + i) & 3);
if (g_25334 >= 0xB0 && (g_25334 & 15) == 14) copy 36 longs from $3EB0 over p's 12 shot slots;   // nova shot burst (every 16 frames, 5 bursts)
```
LAB_1FAE swaps the colour-cycle tables ($1F4E instead of $1F1E) and the sprite palette ($2218/$24A8)
while the nova runs; LAB_9E32 adds the screen shake; LAB_3748/LAB_3F54 are suppressed while g_25334.

--------------------------------------------------------------------------------------------------
## 7. Effects pool ($4976, 16 × 20 bytes, end $4AB6; compact, no free-list)

Record: `+0 x.l (16.16, word part in sprite space), +4 y.l (16.16), +8 vx.l, +12 vy.l (16.16 px per
DISPLAY frame), +16 gfx.b, +17 frame.b, +18 sprite channel (0/4/8/12 → list $4ACA[c] = $5E000/$5E400/
$5E800/$5EC00 + g_11826), +19 mode/age.b`. `x.w == 0` = free. Live records are kept contiguous at the
front: LAB_4E1A removes a record by shifting all following ones down (so SLOT NUMBERS ARE NOT IDENTITIES).

### Types seen (V: re/stats/effect_stats.txt)
| gfx +16 | what | +19 | motion |
|---|---|---|---|
| 7 (frame $58) | **enemy bullet** | 0 | ballistic: pos += vel every display frame; speed: major axis 1.5 px/tick (= option 10064/256), minor scaled ⇒ |v| 1.5..2.12 (V: stats) |
| 12 (frames $60..$7F) | **homing missile** (from hostile type 9 "at launch height" and objects $25/$26, sound 56) | 1.. (age, +1 per game frame) | age < 25: rides the scroll (y += g7222 on odd frames); age ≥ 25: accelerates ±$800/tick toward the aim target (cap ±$18000 = 1.5 px/tick per axis), 32-direction sprite frame turns 1 step/frame; age ≥ $78 (120): steering taken from byte +1 vs frame counter (wander) (L) |
| 16 (frames $54..$57) | nova ring sparks (LAB_1D0C) | — | rewritten every frame | 
| (pickups are NOT effects: they are hostile type 5 with subtype +28) | | | |

### Spawning — LAB_4704 ($4704..$491A), once per game frame
```c
if (g_28516 && g_28550 >= 0x5DC) return;                   // demo after play
for each hostile h ($2DC80, 12): if (h.f30 & 0x20 /*fire request*/) { h.f30 &= ~0x20; if (h.x) {
    g_14398 = h.f58; g_14396 = h.f60; g_14384 = 0;          // origin (per-type muzzle, sprite space)
    if (h.type == 9 && h.f60 - h.y == 0x26) { g_14384 = ~0; sound 56; }   // missile launcher at launch height
    LAB_47D2(); } }
for each object o ($2E040, 18): if (o.f31 & 0x20 && o.x) { g_14398 = o.f38; g_14396 = o.f40; g_14384 = 0;
    if (o.type == $25 || o.type == $26) { g_14384 = ~0; sound 56; }  LAB_47D2(); }
// LAB_47D2: allocate into the y-sorted pool
if (g_25334 || both ships dead (f38 < 0)) return;
if (effect[option 10062].x) return;                          // pool "full" at the option's bullet limit (15)
find the first slot whose y > origin y (keep ascending y), shift the tail up by one record (drops slot 15 if full);
if (g_14384) { e = {origin, vel 0, gfx $0C, frame $60, chan 0, age 1}  or {gfx $0C, frame $70, age $18} if *(byte*)$79DE; return; }
aim = nearest ship (LAB_491C: |dx|+|dy| to P1.f4+12/f6+16 vs P2; a ship with f38 < 0 is ignored; object spawners may force P1/P2 via o.f42 = 1/2);
if (g_14390 || g_14388) { (dx,dy) = forced (g_14390, g_14388); g_14390 = 0; }
speed = g_14386 << 8 (16.16);  if (|dy| >= |dx|) { vy = speed; vx = |dx|*g_14386/(|dy||1) << 8; } else { vx = speed; vy = ... }
apply signs toward the target; e = {origin, vx, vy, gfx 7, frame $58, chan 0, age 0};
```

### LAB_4ADA ($4ADA..$4E3A) `update_effects()` — TWICE per game frame
```c
if (g_13962 && g_13942) bubble-sort the live records by y.w (ascending) (swaps in place, restart until clean);
// sprite channel assignment: c = 0; for each record: f18 = c; if (0x116 <= y.w < 0x1E0) c = (c+4) & 15 else { f18 |= 8; c = (c+4) & 7; }
list[0..3] = $5E000/$5E400/$5E800/$5EC00 + g_11826;
xmin = g7204 - 16; xmax = g7204 + 288;
for each live record e (stop at the first x == 0):
  if (e.f19 == 0) {                                                // BULLET
      e.x += e.vx; e.y += e.vy;                                    // 16.16
      if (y.w >= 0x200 || y.w <= 0xF3 || (!g_25334 && (x.w <= xmin || x.w >= xmax))) { remove(e); continue; }
      frame = e.f17 + ((g_28551 & 4) ? 0 : 1);                     // 2-frame flicker, no write-back
  } else {                                                         // MISSILE
      if (e.f19 < 0x78) steer = LAB_491C(e) (bits: 0 = target is left, 1 = target is above), also compare |dx|+|dy| of the two ships (nearest);
      else steer = (e.byte1 < g_28551) ? 1 : 0;
      vx += (steer&1) ? -$800 : +$800 (clamped ±$18000); vy likewise with bit1;
      if (!(g_28551 & 1)) e.f19++;                                 // age: once per game frame
      if (e.f19 < 0x19) { if (g_28551 & 1) e.y.w += g7222; }       // still on the launcher: scrolls with the ground
      else { e.x += vx; e.y += vy; }
      if (y.w >= 0x200) { remove(e); continue; }
      if ((g_28551 & 1) == (slot & 1)) {                           // turn on alternate frames
          dir = 32-way angle of (vx, vy) via the 8-step ratio table ($19,$4E,$89,$D2,$138,$1DF,$34C,$A27 of dy*256/dx);
          e.f17 = turn e.f17 ($60..$7F) one step toward dir; }
      if (y.w < 0xF6) continue;                                    // not drawn yet
  }
  emit sprite (x.w - g7204, y.w - 256, gfx e.f16, frame) into list[e.f18 >> 2]  (LAB_5534; list pointer advanced);
terminate the 4 lists (CLR.L).
```
(V: bullet chains predicted with x+2·vx per game frame match ⇒ two integrations per game frame; bullets
live up to ~120 game frames (V stats); up to 14-16 live effects; missiles reach age 234.)

### How effects hit the player — LAB_3748 ($3748), once per game frame, selected player only
`if (g_25334 || p->f52) return; for each live effect: if (p->f4 < e.x.w <= p->f4+25 && p->f6 < e.y.w <= p->f6+23)
{ hit(p) (section 4); remove(e); }` — every effect type kills (bullets and missiles; nova sparks are
gone before the check since g_25334 gates it). (V: runtime.c collide_player_with_effects, parity-pinned.)

--------------------------------------------------------------------------------------------------
## 8. Score, lives, game flow

* **Score** = 8 ASCII digits at +106 (+114 = copy of the previous frame). `LAB_40BE(D6 = BCD points,
  A2 = digit end)` writes the 4 nibbles of D6 to $40B8..$40BB and `LAB_40DE` adds 8 raw-nibble digits
  (A3 end pointer) into the ASCII digits with carry. Callers: hostile kill ($34DC: points = h.f44 /
  g_19422), object kill ($35B2: g_19208), pickup that cannot level further (LAB_3712: $40AC = "00010000"
  → 10000 points? — $40AC..$40B3 = 00 00 00 01 00 00 00 00 = +10000 (L)), stage bonus (1000/unit),
  credits/hiscore screens. Points are awarded to the SELECTED player of that frame (g_18620).
* **Extra lives** LAB_139C (once per game frame, P1 on even bit2, P2 on odd): compares +106.. with the
  previous copy: while the two top digits are "00": life at 100000 (3rd digit 0→1), 300000 (2→3),
  600000 (5→6); from 1,000,000 on, every change of the millions digit. Max 4 lives, sound EXT_24744,
  lives HUD. (L)
* **Lives** +56: decremented when the REPLACEMENT ship finishes its entry (LAB_5090), not at death;
  +41 set = free respawn. Game over for a ship when f49 expires with f56 == 0 → f38 = $C8.
* **Game over** LAB_410A (once per game frame): `g_16120 = no ship with f38 < $C8 among joined players`;
  if every joined ship is $C8 and its portrait dissolve is finished (f68 == $3E6): count down g_16122
  ($FA hold), then if no overlay and not already: `g8524 = -1; g8514 = 1; text $FCE6 (14 lines, hold
  $40D8); stop music; load LODHIS; g_26245 = 2; LODGAM entry; EXT_24708`. Then LAB_4232 runs the
  initials editor for each ship in state $C8 (joystick l/r = column, u/d = char '0'..'Z', fire/timeout
  f120 ends; $42F8 completion → f91 = $FF; fire again restarts the ship: f58 = option 10060, f60 = 0,
  LAB_1276 re-init). When both are done `g_4096` is set → LAB_DFA leaves the loop → LAB_E04: 100-frame
  fade (palette ramp, hiscore table LAB_40DE into $40B4 for ships with f38 < $AF), load LODCOM, title.
* **Two players**: `g_2732` = both joined; P2 joins with the second fire button (outside this part).
* **Difficulty** 10066 (0..2) picks the armour table (-2200..) and the per-inner-stage reduction.
* **Options copied at game start ($A88)**: `g_26244 = g10941`, `g_26243 = g10953` (title sound toggles).
* **Cheat** LAB_2088: raw key bytes $98 $BE $BC $D6 $CE $D8 (= "CASTOR") → g_24542 set; afterwards
  `P1.f52 = P2.f52 = $36` every frame (permanent invulnerability), keys on the $26..$2F row select
  weapon type (g_24442 = w·24) / level (g_24444 = l·4) and re-run LAB_2B32 for both ships; g_24541 tints
  colour $32A0 (g12960) as a visual cue.

--------------------------------------------------------------------------------------------------
## 9. Messages (LAB_A30E) & stage clear texts

`g8514` counts overlay frames (twice per game frame here). Line list at `g8518`: 20-byte entries
`{x.w (sprite h-offset), y.w, 16 chars}` terminated by a 0 word; `g8516` = line count, `g8522` =
hold time. Frame 1: copies the line x's into $A14E (one word per sprite, 8 sprites per line). Frames
1..g8516: render one text line per frame into the sprite channel memory at $5E000 + line*$2C (LAB_A50E:
2 chars per sprite from the 8×10 font at $10550). Afterwards each line slides in (frames 20+12·i ..
72+12·i, table $A24E) and out (g8522+12·i .. +60, table $A25E) by adding the table to its sprite x;
colour cycling via $A26E into the copper at $BB46; ends at `g8514 >= g8522 + $D1`, then `g8514 = 0`.
Texts: $9EA6 "EXTRA BONUS …", $9F34 "GOOD WORK … NOW ENTERING THE INNER CORE OF PLANET TERRAINIA",
$9FC2 "NICE JOB! … DESTROYED ONE OF THE INNER LEVELS …", $A064 "WOW! WHAT DESTRUCTION … RED ALERT: …
MAJOR SCANNER INTERFERENCE", $FCE6 game over (LODSCO), $2E508/$2E582/$2E5E8/$2E676 demo captions (LODS0F).
The stage-clear flow is in section 3 (LAB_7002).

--------------------------------------------------------------------------------------------------
## 10. Verification log
* Call order and once/twice cadence: (V) trace state_2000.bin, 131 iterations (`tools/bs_trace.py`).
* Input byte: (V) trace D1 at $505E = $18/$08 while the host autopilot held left(+fire); matches bit
  layout 0 up 1 right 2 down 3 left 4 fire.
* Player motion: (V) trace: f0 −4 per game frame (2 per LAB_5050 call), y clamped at 392 in that run;
  camera target 256 + 3/8·(f0−256) (trace D1 at $9C9E) and g7204 −1 per game frame toward it.
* Scroll: (V) g7206 +1 per game frame (trace D1 at $755E and objlog G), g7212 30→0 step 2 (objlog).
* Shot spawn: (V) objlog_invuln F=2003 slots 2,3 at (612,359)/(624,359), vx 0, vy −8 = template
  (6,−25,0,−8)/(18,−25,…) of weapon 3 level 0 at $3D18 (RAM), launched from f4 = 606, f6 = 392, one
  display frame of flight; y −8 per display frame afterwards.
* Effects: (V) effect_stats: bullets 1.5..2.12 px/tick, 2 integrations per game frame, lifetimes ≤ ~120
  game frames, ≤ 16 live; missiles age 1..234, frames $60..$7F, |v| components capped at 1.5.
* Player death/respawn numbers: (V) objlog_auto P lines (b38 100/150/200, b49 70→0, b48 145→0, w52
  300/$270F-ish, w60 halves, lives −1 on arrival).
* Not verifiable from the captures (L): nova, final-boss variant, stage clear/advance, game over/initials,
  mouse input, cheat, two-player camera formula, $A14C writer.
