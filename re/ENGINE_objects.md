# Battle Squadron — OBJECT pool ($2E040), spawner, collision/score/pickup — C pseudocode spec

Companion to `BRIEF.md`. Field names: `o->f<off>` = byte/word/long at object record + off; `g<off>` =
`off(A5)` (A5 = $8000, so g7222 lives at $8000+7222). Hostile record = `h->f<off>` (pool $2DC80, 12 x 80),
effect/enemy-bullet record = `e->f<off>` (pool $4976, 16 x 20), player = `p->f<off>` ($4E3C / $4F46).
`(V: ...)` = verified against a trace/objlog/RAM image, `(L)` = listing only. Trace files: `state_8000.bin`
(stage 0 play), `state_stage{1,2,3}.bin`, objlogs `objlog_*.txt` (stats in `re/stats/object_stats.txt`,
script `tools/object_stats.py`).

## 0. Units and coordinate space (V: objlog + listing)

* One "game frame" = one pass of the main loop = 2 display frames. `g-28552` (word) is the display-frame
  counter, incremented by 2 per game frame; `g-28551` is its LOW BYTE, so `g-28551 & 2` toggles every game
  frame, `& 6 == 0` is true every 4th game frame, `& $0e == 0` every 8th, `& $1f` cycles every 16.
  (V: G lines: gframe 0,2,4,... per logged frame.)
* All object/player/shot coordinates are in ONE integer "game space": X = 256 + map pixel column (map row
  = 24 words = 384 px, x in $100..$280), Y = 256 + screen row (visible rows y = $100..$1FF; y < $100 is above
  the screen, y >= $200 below). `g7204` = X of the screen's left edge (256..352 in the captures); `g7206` =
  scroll progress (pixels, +1 per game frame while `g7222` = 1); `g7222` = vertical scroll step per game frame
  (1 while scrolling, 0 when the scroll is stopped — the objects move with the ground by adding it to y);
  `g7212` = pixel-row countdown 30,28,..,0 inside the current 16-px map row (a new row of the map is exposed
  when it wraps, and that is the ONLY frame LAB_3078 scans a row); `g7214` = pointer to the map row words.
  (V: G lines; x ranges of O/P/S lines overlap; objects x = $100 + 16*col.)
* Hostiles (LAB_75E4) take SCREEN-relative x in D1 (added to g7204 unless >= $320) and y-$100 in D2 — the
  object code passes `x - g7204 + dx`, `y - $100 + dy`, so the spawned hostile lands at `(x+dx, y+dy)` in game
  space. (V: $61E6 records: D1 = x-g7204+14.)
* Objects have no sub-pixel part; hostiles/effects have 16.16 fractions (not this document).

## 1. LAB_3078 ($3078..$33D0) — tile-triggered object spawner

Called once per game frame from the main loop ($CDE) and 256 times from the stage-clear pre-roll LAB_7002
($72FC: the new stage scrolls 256 rows of map with LAB_9C44/LAB_5F34/LAB_3078 before play resumes) (L).
`runtime.c spawn_wave_objects()/allocate_wave_object()` are exact translations (V: parity-pinned).

```c
void spawn_objects_from_map(void)                       /* LAB_3078 */
{
    if (g7222 == 0) return;                             /* scroll stopped                       */
    if (g7212 != 0) return;                             /* not on a 16-px row boundary (V: G)   */
    if ((s16)g8514 >= 0x7530) return;                   /* message/cut-scene timer (L)          */

    /* 3 fixed boss-gate spawns on the surface stage: only while stage 7228 == 0 and no gate is live */
    if (g7228 == 0 && g7230 == 0) {
        if (g7206 == 0x0F10 && !(g-4099 & 2)) { col = 11; g7230 = 1; alloc(T_2FB8); return; }
        if (g7206 == 0x1490 && !(g-4099 & 4)) { col =  9; g7230 = 2; alloc(T_2FB8); return; }
        if (g7206 == 0x1DD0 && !(g-4099 & 8)) { col = 12; g7230 = 3; alloc(T_2FB8); return; }
        /* g-4099 bit n = underground stage n already cleared; the gate spawns in column 11/9/12
           -> x = $1C0/$1E0/$1B0 (V: pcset shows $30AE/$30C6/$30DE executed; gate objects in all objlogs) */
    }

    u16 *row = g7214 - 0x30;                            /* the row just exposed: 24 words       */
    for (col = 23; col >= 0; col--) {                   /* D0 = col; word 0 is the LEFT edge    */
        u16 tile = *row++;  tmpl = 0;
        switch (g7228) {
        case 0:   /* surface / hub */
            if (tile == 0x6180) tmpl = T_2DA8;                                 /* turret          */
            else if (tile == 0x5640) { if (row[0x18] == 0x5CD0) tmpl = T_2E68; } /* needs the word one row below (+$30 bytes) */
            else if (tile == 0x0280) { if (g7206 >= 0x3E8 && g7206 < 0x1F40) tmpl = T_2B98; }
            else if (tile == 0x8020) tmpl = T_2EC8;
            else if (tile == 0x5D20) tmpl = T_2EF8;
            else if (tile == 0x5D70) tmpl = T_2F28;
            else if (tile == 0x5910) tmpl = T_2F58;
            else if (tile == 0x59B0) tmpl = T_2F88;
            break;
        case 1:   /* underground 1 */
            if (tile == 0x5C80) tmpl = T_2DD8;
            else if (tile == 0x00A0) tmpl = T_2FE8;
            else if (tile == 0x0230) tmpl = T_3018;
            else if (tile == 0x0D70) tmpl = T_2C28;
            else if (tile == 0x92E0 && g7206 < 0x19C8) tmpl = T_3048;
            break;
        case 2:   /* underground 2 */
            if (tile == 0x7260) tmpl = T_2BC8;
            else if (tile == 0x9420) tmpl = T_2E98;
            else if (tile == 0x0CD0) tmpl = T_2E08;
            break;
        default:  /* 3: underground 3 */
            if (tile == 0x8C00) tmpl = T_2B68;
            else if (tile == 0x9420) tmpl = T_2E38;
            else if (tile == 0x92E0) { if (g7206 < 0x0DDE && rnd() < 0x40) tmpl = T_2BF8; }   /* 25 % */
            else if (tile == 0x3A70) tmpl = T_2C58;
            else if (tile == 0x4BF0) tmpl = T_2C88;
            else if (tile == 0x3ED0) tmpl = T_2CB8;
            else if (tile == 0x3340) tmpl = T_2CE8;
            else if (tile == 0x4E70) tmpl = T_2D18;
            else if (tile == 0x4290) tmpl = T_2D48;
            else if (tile == 0x3520) tmpl = T_2D78;
        }
        if (tmpl && alloc(tmpl, col, tile, row) == GATE) return;   /* a gate ends the scan */
    }
}
```
(V: mode 0 mapping — objlog stage 0 shows exactly types 01,20,21,22,25,26,27 with gfx 02EA80/03C180/03EE80/
03D800/03A100/037400/038A80/035B00/040500 = the 9 mode-0 templates; mode 1: 03F6E0 ($20) + 000000 ($29);
mode 2: 03A280 (03), 04EB00 ($20), 037D00 ($22); mode 3: 03A000 (00), 032800 (02), 030640 ($20). Mode-1
$0D70/$00A0/$0230 and the mode-3 type-5 words never appeared in the 16000-frame stage runs: (L).)

```c
int alloc(tmpl, col, tile, row)                         /* LAB_32F4..$33D0 */
{
    o = first of the 18 records at $2E040 (stride 64) with o->x == 0;     /* x==0 = free slot */
    if (none) {
        /* pool full: in stage 1 the two hatch tiles are rewritten in the map so the row is not
           re-armed: $00A0 -> $0320, $0230 -> $04B0; then keep scanning. Other modes: RTS (stop the scan). (L) */
        if (g7228 == 1) { if (tile == 0x00A0) row[-1] = 0x0320; else if (tile == 0x0230) row[-1] = 0x04B0; continue scan; }
        return STOP;
    }
    o->x   = (23 - col) * 16 + 0x100;                  /* V: $333A D1=$1D0 for col 10 */
    o->f6.l = tmpl->f6.l;                               /* h (+6.w) and w in words (+8.w) */
    o->y   = 0x100 - o->h;                              /* bottom edge on the screen top (V: y0 = 256-h+1 at first sample) */
    o->f10 = 2*w*h;                                     /* bytes per bitplane of one frame     */
    o->f26 = 5 * o->f10;                                /* bytes per FRAME (5 planes) -> gfx + state*f26 */
    o->f4  = 0x30 - 2*w;                                /* blit modulo (row = 48 bytes)        */
    o->f12.l = tmpl->f12.l  (gfx);  o->f16.l = tmpl->f16.l (f16, TYPE f17, f18, f19);
    o->f20.l = tmpl->f20.l  (box dx +20.w, box width +22.w);  o->f24.w = tmpl->f24.w (dmg mailbox 0, state f25);
    o->f28.l = tmpl->f28.l  (hp f28, f29, f30, f31 flags);
    if (g-2732 == 0) o->f28 -= (o->f28 + 1) >> 2;       /* ONE-PLAYER game: ~25 % less health.
                                                           g-2732 = (g-12701 & g-12435) = both players active (L: $95A);
                                                           V: every objlog hp0 = tmpl hp - ((hp+1)>>2): 3->2, 7->5, 31->23, 47->35, 63->47 */
    o->f32.l = tmpl->f32.l  (hp2 f32, f33 last frame, f34/f35 flash frames);  o->f36.w = tmpl->f36.w;
    o->f42 = 0;
    o->f43 = (rnd() & tmpl->f43) + 1;                   /* random delay 1..mask+1 (V: $33B6 D3=$AC&7+1=5) */
    o->f44.l = tmpl->f44.l  (score BCD +44.w, hit sound +46);
    /* NOT initialised: f38..f41 (stale), f48..f55 (box, written by the update before any collision) */
    return o->f17 == 0x27 ? GATE : OK;
}
```
`rnd()` = LAB_2B1E: next byte of the 256-byte table at $2B1A (D3).

### 1.1 Template table ($2B68..$3048, 48 bytes, dumped from chip_2700.bin)
Score = BCD word at +44 read as `points = 100*BCD(lo byte) + BCD(hi byte)` (see §3.4; V: $2500 -> 25, $5000 -> 50
in traces). "hp 1p" = health after the one-player reduction. Death/last frame: the object animates states
`f25 = f19 .. f33` when killed (types >= $20). Gfx frame = +12 + f25 * 5*2*w*h.

| tmpl | trigger (7228 mode, map word) | TYPE +17 | h +6 | w +8 (words) | gfx +12 | +18 | +19 deathFrame | box +20/+22 | +24/+25 (dmg/state0) | hp +28 (2p) | hp 1p | +29..31 | +32 hp2 | +33 lastFrame | +34/+35 flash | +36 | +37 | rnd mask +43 | score +44 | hitSnd +46 | role |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| $2B68 | mode3 $8C00 | $00 | 64 | 4 | $03A000 | 0 | 0 | 0/64 | 0/0 | 63 | 47 | 00 00 00 | 31 | 0 | 0/0 | 0 | 0 | $7F (1..128) | $0005 = 500 pts | $34 | stage-3 opening bunker (hull+core) |
| $2B98 | mode0 $0280, $3E8<=prog<$1F40 | $01 | 32 | 4 | $040500 | 0 | 0 | 16/-16 | 0/0 | 0 | 0 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $3F (1..64) | $0002 = 200 pts | $3B | launch pad (spawns hostile 07) |
| $2BC8 | mode2 $7260 | $03 | 64 | 3 | $03A280 | 0 | 12 | 8/32 | 0/0 | 47 | 35 | 00 00 00 | 15 | 19 | 11/8 | 0 | 0 | $1F (1..32) | $0025 = 2500 pts | $33 | stage-2 emerging turret |
| $2BF8 | mode3 $92E0, prog<$DDE, rnd<$40 | $02 | 64 | 4 | $032800 | 0 | 6 | 8/48 | 0/0 | 31 | 23 | 00 00 00 | 0 | 11 | 5/2 | 0 | 26 | $1F (1..32) | $0015 = 1500 pts | $32 | stage-3 rising turret (25%) |
| $2C28 | mode1 $0D70 | $04 | 48 | 4 | $034F60 | 0 | 12 | 8/48 | 0/0 | 23 | 17 | 00 00 00 | 15 | 19 | 11/8 | 0 | 0 | $00 (1..1) | $0010 = 1000 pts | $35 | stage-1 two-phase cannon |
| $2C58 | mode3 $3A70 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/0 | 23 | 17 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $0025 = 2500 pts | $33 | stage-3 crate/target, visible |
| $2C88 | mode3 $4BF0 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/6 | 7 | 5 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $33 | hidden target v6 |
| $2CB8 | mode3 $3ED0 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/7 | 7 | 5 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $33 | hidden target v7 |
| $2CE8 | mode3 $3340 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/8 | 7 | 5 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $33 | hidden target v8 |
| $2D18 | mode3 $4E70 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/9 | 7 | 5 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $33 | hidden target v9 |
| $2D48 | mode3 $4290 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/10 | 7 | 5 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $33 | hidden target v10 |
| $2D78 | mode3 $3520 | $05 | 32 | 2 | $02E840 | 0 | 0 | -4/40 | 0/11 | 7 | 5 | 00 00 00 | 0 | 0 | 0/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $33 | hidden target v11 |
| $2DA8 | mode0 $6180 | $20 | 48 | 3 | $02EA80 | 0 | 9 | 12/24 | 0/4 | 3 | 2 | 00 00 00 | 0 | 18 | 8/0 | 0 | 0 | $00 (1..1) | $5002 = 250 pts | $3B | rotating gun turret (8 dirs, aims) |
| $2DD8 | mode1 $5C80 | $20 | 48 | 3 | $03F6E0 | 0 | 4 | 8/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 11 | 3/0 | 0 | 0 | $00 (1..1) | $0004 = 400 pts | $3B | stage-1 hatch gun (70-frame cycle) |
| $2E08 | mode2 $0CD0 | $20 | 48 | 3 | $04EB00 | 0 | 6 | 0/48 | 0/0 | 31 | 23 | 00 00 00 | 0 | 12 | 3/0 | 1 | 0 | $00 (1..1) | $0020 = 2000 pts | $3B | stage-2 silo: launches hostile 0D x2 |
| $2E38 | mode3 $9420 | $20 | 48 | 2 | $030640 | 0 | 2 | 2/28 | 0/0 | 7 | 5 | 00 00 00 | 0 | 7 | 1/0 | 0 | 0 | $00 (1..1) | $0004 = 400 pts | $35 | stage-3 static block |
| $2E68 | mode0 $5640 (+next row $5CD0) | $21 | 32 | 2 | $03C180 | 0 | 2 | 0/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 8 | 1/0 | 0 | 0 | $00 (1..1) | $5001 = 150 pts | $3B | scenery (hangar/bldg) 16x32 |
| $2E98 | mode2 $9420 | $22 | 48 | 2 | $037D00 | 0 | 3 | 2/28 | 0/0 | 7 | 5 | 00 00 00 | 0 | 9 | 2/0 | 0 | 0 | $07 (1..8) | $5002 = 250 pts | $3B | stage-2 blinking beacon |
| $2EC8 | mode0 $8020 | $22 | 32 | 2 | $03A100 | 0 | 6 | 0/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 12 | 0/0 | 0 | 0 | $07 (1..8) | $0003 = 300 pts | $32 | blinking beacon |
| $2EF8 | mode0 $5D20 | $21 | 32 | 2 | $03EE80 | 0 | 2 | 0/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 8 | 1/0 | 0 | 0 | $00 (1..1) | $0002 = 200 pts | $3B | scenery |
| $2F28 | mode0 $5D70 | $21 | 32 | 2 | $03D800 | 0 | 2 | 0/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 8 | 1/0 | 0 | 0 | $00 (1..1) | $0004 = 400 pts | $3B | scenery |
| $2F58 | mode0 $5910 | $25 | 32 | 2 | $037400 | 0 | 2 | 0/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 8 | 1/0 | 0 | 0 | $00 (1..1) | $0001 = 100 pts | $3B | mine A (drops stationary hazard) |
| $2F88 | mode0 $59B0 | $26 | 32 | 2 | $038A80 | 0 | 2 | 0/32 | 0/0 | 3 | 2 | 00 00 00 | 0 | 8 | 1/0 | 0 | 0 | $00 (1..1) | $0001 = 100 pts | $3B | mine B (drops stationary hazard) |
| $2FB8 | boss gate: progress $F10/$1490/$1DD0 (col 11/9/12 -> x $1C0/$1E0/$1B0) | $27 | 32 | 2 | $035B00 | 0 | 2 | 0/0 | 0/0 | 3 | 2 | 00 00 00 | 0 | 8 | 1/0 | 0 | 0 | $00 (1..1) | $0000 = 0 pts | $00 | STAGE GATE (enter + hold 10 frames) |
| $2FE8 | mode1 $00A0 | $28 | 16 | 1 | $03D660 | 0 | 12 | 0/16 | 0/0 | 3 | 2 | 00 00 00 | 0 | 25 | 11/8 | 0 | 0 | $00 (1..1) | $0001 = 100 pts | $32 | stage-1 hatch A (spinner, fires) |
| $3018 | mode1 $0230 | $28 | 16 | 1 | $03E6A0 | 0 | 12 | 0/16 | 0/0 | 3 | 2 | 00 00 00 | 0 | 25 | 11/8 | 0 | 0 | $00 (1..1) | $0001 = 100 pts | $32 | stage-1 hatch B |
| $3048 | mode1 $92E0, prog<$19C8 | $29 | 16 | 1 | $000000 | 0 | 12 | 16/-16 | 0/0 | 49 | 37 | 00 00 00 | 0 | 25 | 11/8 | 0 | 0 | $00 (1..1) | $0000 = 0 pts | $00 | invisible spawner: hostile 08 |

## 2. LAB_5F34 ($5F34..$6FFA) — per-frame object update, render-list emission

Runs once per game frame BEFORE the hostile passes and the collision routines (main loop order in BRIEF).
It (a) clears the fire-request bit, (b) moves every object with the ground, (c) runs the per-type state
machine, (d) frees objects that scrolled off, (e) writes the render list at $5C2A (consumed by LAB_5BFE/5BD2).

Object record fields used by the update (all types): `x f0`, `y f2`, `f4` modulo, `f6` h, `f8` w(words),
`f10` plane bytes, `f12` gfx, `f17` type, `f18` per-type timer, `f19` first death frame, `f20/f22` hit box
(x + f20 .. x + f20 + f22), `f24` DAMAGE MAILBOX (added by LAB_3424, consumed here next frame), `f25` STATE =
animation frame index, `f26` frame stride, `f28` hp (signed byte), `f29` per-type, `f30` hit-flash counter /
per-type, `f31` FLAGS, `f32` second hp, `f33` last death frame, `f34/f35` flash frames, `f36/f37` per-type
timers, `f38.w/f40.w` muzzle/effect position for the fire request, `f42` chosen target (1 = P1, 2 = P2),
`f43` random count, `f44` score, `f46` hit sound, `f48..f55` collision box (x1,x2,y1,y2) written every frame.

`f31` flag bits (V: objlog b31 values 00/02/04/08/0a/0c/10/12/18/1a/20/22/28 are exactly these combinations):
bit1 (02) = type-0 "open"; bit2 (04) = NOT SHOOTABLE (dead or hidden; LAB_3424 skips it); bit3 (08) = toggles
every frame (BCHG, used by type 0 as a half-rate clock); bit4 (10) = type-0 "has opened once / closing";
bit5 (20) = FIRE REQUEST for this frame (position in f38/f40; cleared at the top of every update, consumed the
same frame by LAB_4704 -> LAB_47D2, see §2.12).

```c
void update_objects(void)                               /* LAB_5F34 */
{
    render = 0x5C2A;  g-8594 = render;
    for (slot = 17; slot >= 0; slot--, o += 64) {         /* D3 = slot counter (also used by callers of LAB_75E4) */
        o->f31 &= ~0x20;
        if (o->x == 0) continue;                          /* free */
        o->f31 ^= 0x08;
        if (o->f17 >= 0x20) { update_generic_2x(o); continue; }     /* §2.7.. (LAB_686C)           */
        switch (o->f17) { case 0: type0(o); case 1: type1(o); case 2: type2(o); case 3: type3(o);
                          case 4: type4(o); case 5: type5(o); default: continue /* not rendered */ }
    }
    *(u16 *)g-8594 = 0;                                   /* terminate the render list */
}
```
Every handler ends in one of: `tail` (= LAB_6EE4: off-screen test, box, render), `skip` (= LAB_6FEC: next
object, no box/render this frame), `free` (= LAB_6EF8: o->x = 0).

```c
tail:   /* LAB_6EE4 */
    if (o->y >= 0x200 || o->y + o->h < 0x100) { o->x = 0; return; }     /* scrolled off the bottom -> FREE.
            (y + h < $100 never happens on its own: y only grows.)  (V: every objlog life ends with y >= $1F0) */
    o->f48 = o->x + o->f20;  o->f50 = o->f48 + o->f22;  o->f52 = o->y;  o->f54 = o->y + o->h;   /* hit box */
    if (o->y < 0x200 && o->y + o->h > 0x100) {          /* visible: emit render record(s) */
        src = o->f12 + o->f25 * o->f26;                   /* frame = state */
        y = o->y; hgt = o->h;
        if (y + hgt > 0x200) hgt = 0x200 - y;             /* clip bottom */
        if (y < 0x100) { cut = 0x100 - y; hgt = o->h - cut; src += cut * o->f8 * 2; y = 0x100; }   /* clip top */
        if (y < 0x180 && y + hgt > 0x180) {               /* straddles the half-screen line: two records */
            emit(x, y, 0x180 - y, w, src, f10, f4);  src += (0x180 - y) * w * 2;
            emit(x, 0x180, y + hgt - 0x180, w, src, f10, f4);
        } else emit(x, y, hgt, w, src, f10, f4);
        /* record = {x.w, y.w, h.w, w.w, src.l, planebytes.w, modulo.w} = 16 bytes; g-8594 advances */
    }
```
(V: render list layout from the listing $6F9C..$6FE4; the renderer agent owns LAB_5BFE.)

### 2.1 Type $00 — stage-3 opening bunker (template $2B68, mode 3 tile $8C00). 64 px high, 4 words wide,
hull hp 63 (47 in 1p), core hp f32 = 31, score 500, hit sound $34, random 1..128. (V: stage-3 objlog: b31 in
{00,02,08,0a,10,12,18,1a,22}, hp0 47, b43 <= 121, b25 <= 9; state machine (L).)
```c
type0:  y += g7222;
    if (!(f31 & 2)) goto closed;
    if (f31 & 0x10) {                                   /* CLOSING (bit4) */
        f24 = 0;  if (f31 & 8) tail;                      /* half rate */
        if (--f25 > 3) tail;  f25 = 0; f31 &= ~2; goto closed;
    }
    if (f18 == 0) {                                     /* OPENING: LAB_60AC */
        f24 = 0;  if (f31 & 8) tail;  if (++f25 < 7) tail;  f18 = 100; tail;    /* frames 4..7, then open for 100 frames */
    }
    if (f25 >= 11) goto core_dying;                     /* LAB_5FE4 */
    if (f30) goto core_flash;                           /* LAB_6006 */
    if (f24) { d = f24; f24 = 0; f32 -= d;
        if (f32 < 0) { f25 = 10; f31 |= 4; f31 &= ~8;   /* core destroyed -> dead, not shootable */
    core_dying: if (f31 & 8) tail; if (f25 >= 15) tail; f25++; tail; }   /* frames 10..15 half rate, stays 15 */
        f30 = 4;
    core_flash: f25 = 9; f30--; if (f30 & 2) tail; f25 = 8; tail; }      /* hit flash 9/8 */
    /* open & idle: LAB_6022 */
    d = f18; if (d > 50) d -= 50;  f25 = 7;
    if (d >= 20 && d < 30) { f25 = 8;                   /* gun out */
        if (f18 == 25) fire_request(x + 0x1A, y + 0x1E);
        if (f18 == 73) fire_request(x + 0x1E, y + 0x10);
        if (f18 == 77) fire_request(x + 0x0F, y + 0x11);   /* 3 aimed bullets per opening (§2.12) */
    }
    if (--f18 == 0) { f31 |= 0x10; f31 &= ~8; }          /* start closing; never opens again (bit4 stays) */
    tail;
closed:                                                 /* LAB_60D2 */
    if (f29) {                                          /* hull destroyed: f29 16 -> 1 flashing 10/1, then frozen */
        if (f29 == 1) tail;  f29--;  f25 = (f25 == 10) ? 1 : 10;  tail; }
    if (!(f31 & 0x10) && y >= 0x100 && --f43 == 0) {   /* on screen, after the random delay: OPEN */
        f31 |= 2; f31 &= ~8; f25 = 4; sound(25); tail; }
    if (f25 == 0 && f24) { d = f24; f24 = 0; f28 -= d; if (f28 < 0) { f29 = 16; f31 |= 4; } }   /* hull damage only on frame 0 */
    if (++f25 >= 4) f25 = 0;                            /* closed idle: frames 0..3 cycling */
    tail;
```
fire_request(px,py) = `f31 |= 0x20; f38 = px; f40 = py;` (LAB_4704 turns it into an aimed enemy bullet).

### 2.2 Type $01 — launch pad (template $2B98, mode 0 tile $0280 while $3E8 <= progress < $1F40).
Hit box +20=16/+22=-16 is inverted => cannot be hit (V: no hp changes in any objlog). Spawns hostile type 7.
```c
type1:  y += g7222;
    if (!(g-28551 & 2)) tail;                           /* runs every other game frame */
    if (f25 == 0) { if (y < 0x100) tail; if (--f43) tail; f25 = 1; tail; }   /* wait until on screen + random 1..64 half-frames */
    if (f25 < 9) { f25++; tail; }                       /* opens: frames 1..9 */
    if (f36) tail;  f36 = 0xFF;                         /* once */
    if (g-28516 /*attract*/) tail;
    LAB_75E4(D1 = x - g7204 + 14, D2 = y - 0xE1, D3 = 7 /*type*/, D4 = garbage);   /* hostile at (x+14, y+31) */
    tail;
```
(V: $61E6 hit x3 in state_8000: D3=7, D1 = x - g7204 + 14, D2 = y - $E1; objlog type 01 lives: b25 max 9,
b43 <= 64, never damaged.)

### 2.3 Type $02 — stage-3 rising turret (template $2BF8, mode 3 tile $92E0, 25 % chance, progress < $DDE).
hp 31 (23), score 1500, hit sound $32, f19 = 6 (death frames 6..10), fire period g-2194, random 1..32.
(V: stage-3 objlog: 38 lives, hp0 23, b25 <= 5, b43 <= 32, b31 in {00,04,08,0c,20,28}; machine (L).)
```c
type2:  f31 |= 4;  y += g7222;
    if (y >= 0x200) free;  if (y < 0x100) skip;         /* nothing (not even a box) while above the screen */
    if (f43) { if (--f43) skip; sound(30); skip; }      /* random delay, then "emerge" sound; still not drawn */
    if (f36 < 0x17) { f36++; f25 = f36 >> 3; tail; }    /* rises over 24 frames: frames 0,1,2 (not shootable) */
    if (f25 >= 6) { if (f25 < 11 && !(g-28551 & 6)) f25++; tail; }   /* dying 6..10 every 4th frame, stays 10 */
    f31 &= ~4;                                          /* shootable */
    if (!f37) f37 = g-2194;                             /* fire period: 50 / 70 / 100 by difficulty 10066 (L: $1120/$1154/$1180), minus 5/10/15 per cleared stage ($7358) */
    f37--;  f25 = 2;
    if (f37 < 24) { d = f37 >> 3; if (d == 2) d = 0; f25 = 3 + d;          /* frames 3,4,3 over the last 24 */
        if (f37 == 9 || f37 == 14) fire_request(x + 0x1C, y + 4); }        /* 2 aimed bullets per period */
    if (f24) { d = f24; f24 = 0; f28 -= d; if (f28 < 0) { f31 |= 4; f25 = 6; tail; } f30 = 6; }
    if (f30) { f30--; f25 = (f30 & 1) ? 5 : 2; }        /* hit flash */
    tail;
```

### 2.4 Type $03 — stage-2 emerging turret (template $2BC8, mode 2 tile $7260). 64 px, 3 words, hp 47 (35),
score 2500, hit sound $33, f19 = 12 (death 13..19), random 1..32. (V: stage-2 objlog: 35 lives, hp0 35,
b25 <= 12, b43 <= 32, y0 193 (h 64); machine (L).)
```c
type3:  f31 |= 4;  y += g7222;
    if (y < 0xF0 + f43) skip;                           /* hidden until y reaches $F0 + rnd(1..32) */
    if (y == 0xF0 + f43) sound(30);
    if (f25 < 5) { f36 = 0x10; if (g-28551 & 0x0E) tail; f25++; tail; }   /* emerges: frames 0..4 every 8th frame */
    f31 &= ~4;
    if (f25 >= 13) { f31 |= 4; if (f25 < 20 && !(g-28551 & 6)) f25++; tail; }   /* death 13..19 every 4th frame */
    if (f24) { d = f24; f24 = 0; f28 -= d;
        if (f28 < 0) { f25 = 13; tail; }
        if (f25 < 8) { f25 = 8; tail; }                   /* knocked back: recoil animation 8..12 */
        goto recoil; }
    if (f25 >= 8) goto recoil;
    if (!f36) { f25 = 7; if (rnd() & 0x1F) tail; f36 = 0x18; }   /* 1/32 per frame: start a burst */
    f36--; d = f36;
    if (d < 8 || d >= 16) f25 = 5;
    else { f25 = 6; if (d == 13) fire_request(x + 4, y + 8); if (d == 12) fire_request(x + 0x23, y + 0x10); }
    tail;
recoil: if (!(g-28551 & 2)) tail; if (++f25 < 13) tail; f25 = 7; f36 = 0; tail;   /* LAB_63C8 */
```

### 2.5 Type $04 — stage-1 two-phase cannon (template $2C28, mode 1 tile $0D70). NOT SEEN in any capture: (L).
hp 31 (23) phase 1, f32 = 15 phase 2, score 1000, hit sound $35, fire period g-2195 (35/50/75 by difficulty).
```c
type4:  y += g7222;
    if (f25 >= 12) {                                    /* PHASE 2 (core exposed)  LAB_6598 */
        if (f25 >= 15) { if (g-28551 & 0x0E) tail; if (f25 < 17) f25++; tail; }   /* death 15..17 */
        f31 &= ~4;
        if (--f36 == 0) { f36 = g-2195; fire_request(x + 0x1C, y + 0x0E); }      /* one aimed bullet per period */
        if (f24) { d = f24; f24 = 0; f32 -= d; if (f32 < 0) { f25 = 15; f31 |= 4; tail; } f30 = 6; }
        f25 = ((g-28551 & 0x1F) < 24) ? 13 : 12;         /* pulsing core */
        if (f30) { f30--; if (f30 & 1) f25 = 14; }
        tail; }
    if (f25 >= 3) { if (g-28551 & 6) tail; f25++; tail; }   /* hull destroyed: 3..11 every 4th frame, then phase 2 */
    if (!f36) f36 = g-2195;  f25 = 0;  f36--;
    if (f36 < 25) { f25 = 1;                            /* 4-shot fan with explicit velocities (see §2.12) */
        if (f36 == 20) { g-14390 = -50; g-14388 = 50; fire_request(x+0x1C, y+0x0B); }
        if (f36 == 15) { g-14390 = -25; g-14388 = 50; fire_request(...); }
        if (f36 == 10) { g-14390 =  15; g-14388 = 50; fire_request(...); }
        if (f36 ==  5) { g-14390 =  50; g-14388 = 50; fire_request(...); } }
    if (f24) { d = f24; f24 = 0; f28 -= d; if (f28 < 0) { f25 = 3; f31 |= 4; tail; } f30 = 6; }
    if (f30) { f30--; if (f30 & 1) f25 = 2; }
    tail;
```

### 2.6 Type $05 — stage-3 crates / hidden targets (templates $2C58 visible, $2C88..$2D78 hidden variants
with initial state f25 = 6..11). NOT SEEN: (L). Visible one: hp 23 (18), score 2500; hidden: hp 7 (5), 250.
```c
type5:  y += g7222;  if (y >= 0x200) free;
    f48 = x + f20; f50 = f48 + f22; f52 = y; f54 = y + h;     /* box set here (before the early exits) */
    if (f25 > 5) {                                      /* hidden variant: LAB_67E0 */
        if (f28 < 0) tail;                              /* already destroyed: drawn at its frame */
        if (!f24) skip;                                 /* INVISIBLE unless hit this frame (no render) */
        d = f24; f24 = 0; f28 -= d;
        if (f28 < 0) { f31 |= 4; spark(); tail; }       /* destroyed: becomes visible permanently */
        spark(); skip; }
    if (f25 == 5) {                                     /* destroyed, burning: LAB_6688 */
        if (!f36) tail;  f36--;  if (rnd() & 6) tail;   /* 1/4 per frame while 75 frames count down */
        explosion_at(x - 0x40 + (rnd() & 0x7F), y - 0x20 + (rnd() & 0x3F));  tail; }
    d = (s8)(g-28551 << 1); if (d < 0) d = -d;          /* 0..127 triangle over 128 frames */
    f25 = 3; f31 &= ~4;
    if (d < 24) { f25 = d >> 3; f31 |= 4; }             /* frames 0..2: closed, not shootable */
    else if (y >= 0x108 && y < 0x1D8 && ((d + 4) & 7) == 0) {      /* every 8th frame while open */
        f38/f40 = (x + 0x0C, y + 0x14); f31 |= 0x20;
        g-14390 = (s8)rnd(); g-14388 = (s8)rnd(); }     /* bullet with RANDOM velocity */
    /* LAB_6740: only an isolated crate is shootable */
    n = count of live type-5 objects with f28 >= 0 and |y' - y| <= 0x40 (itself included);
    if (n != 1) { f31 |= 4; tail; }
    if (f24) { d = f24; f24 = 0; f28 -= d;
        if (f28 < 0) { f31 |= 4; f25 = 5; f36 = 0x4B; tail; }
        f30 = 4; spark(); }
    if (f30) { f30--; if (f30 & 1) f25 = 4; }
    tail;
spark():        explosion_at(x - 16 + (rnd() & 31), y - 16 + (rnd() & 31));          /* LAB_6810 */
explosion_at(px,py):  /* LAB_6836: two 6-byte slots {x,y,timer} at $790A/$7910; pick the one with the
        smaller timer; {px,py,8}; g-27618 = rnd() & 15 (palette flash); sound(31). Consumed by hostile
        type $0A (explosion sprite) in LAB_79E2 $7F56 (L). */
```

### 2.7 Types >= $20 — shared part (LAB_686C..$6A3E) — **reuse runtime.c update_type20_mode0_pool() here**
```c
update_generic_2x(o):
    y += g7222;                                         /* (V: $6870 D2 = 1 = g7222; objlog y +1/frame) */
    if (f25 < f19) {                                    /* ALIVE (state below the first death frame) */
        if (f24) {                                      /* damage mailbox from LAB_3424 (last frame) */
            d = f24; f24 = 0;
            if (f17 == 0x22) f30++;                     /* beacon: one flash step per hit */
            else if (f17 == 0x20 && g7228 == 2) { if (!f30) f30 = 15; }   /* silo: 15-frame hit sequence */
            else f30 = 3;
            f28 -= d;                                   /* (V: $6968 hits: D1 = 1 per shot) */
            if (f28 < 0) { f31 |= 4; f25 = f19; tail; } /* KILLED: unshootable, start death animation */
            goto flash;
        }
        if (f30 == 0) goto behaviour;                   /* LAB_6A40 */
        f30--;
    flash:                                              /* LAB_6894 */
        if (f17 == 0x22 && g7228 == 0) { f25 = f30 ? 6 - f30 : 0; tail; }
        if (f17 == 0x20 && g7228 == 2) { f25 = tbl_5F24[f30];        /* 00 02 03 04 05 05 05 05 04 03 02 00 01 00 01 00 */
            if (f30 == 6) fire_request(x + 0x21, y + 0x0E);
            if (f30 == 5) fire_request(x + 0x06, y + 0x0E);  tail; }   /* silo fires 2 aimed bullets when hit */
        f25 = (f30 & 1) ? f34 : f35;  tail;             /* generic flash: alternate the two flash frames */
    }
    /* DYING / DEAD */
    if (f25 < f33) { if (!(g-28551 & 2)) f25++; tail; }    /* death frames advance every other game frame */
    if (f17 == 0x20 && f25 == f33) {                    /* LAB_699C: a finished turret wreck is a BONUS pickup */
        left = x (mode 3: x - 8); box = [left-8, left+0x14] x [y-0x0C, y+0x10];
        got = 0;
        for p in (P1, P2): if (p->f38 < 0x4B /*alive*/ && p->x >= box.l && p->x <= box.r && p->y >= box.t && p->y <= box.b
                             && p->f97 != 0x99) { p->f97 = bcd_add(p->f97, 1); got++; }   /* bonus counter BCD (max 99) */
        if (got) { f25 = f33 + 1; sound(53); }           /* collected once */
    }
    tail;
```
(V: $6A06 executed 56x in state_stage1; runtime.c has the same code.)

### 2.8 Type $20 behaviours (LAB_6A40) — one type, four stage modes
**Mode 0 (g7228 == 0): rotating turret ($2DA8).** 8 directions clockwise from 0 = up: 2 = right, 4 = down,
6 = left. f25 = direction (and frame 0..7), f19 = 9 (death 9..18), score 250. (V: state_8000 $6B68.. executed;
$6C66 fire x2; objlog b25 <= 8 alive, up to 19 dead, b42 in {1} (P1 target), b31 20/28 seen = fire requests.)
```c
    if (f37 >= 2) f37--;                                /* reload countdown, stops at 1 */
    if (f36) { f36--; tail; }                           /* turn delay */
    g-14398 = x + 0x13; g-14396 = y + 0x10;             /* turret centre */
    LAB_491C: d1 = |P1x + 12 - cx| + |P1y + 16 - cy| (D5), d2 = same for P2 (D6)
              (P1 = g-12736/-12734, P2 = g-12470/-12468 = player x,y mirrors)
    if (d1 < d2) { tx,ty = P1; f42 = 1; } else { tx,ty = P2; f42 = 2; }
    dx = tx - 6 - x; if (dx == 0) dx = 1;  quadrant = dx < 0 ? 4 : 0;  dx |= 1;
    dy = y + 6 - ty;  slope = (dy * 64) / dx  (signed);
    dir = slope >= 154 ? 0 : slope >= 26 ? 1 : slope >= -26 ? 2 : slope >= -154 ? 3 : 4;
    dir = (dir + quadrant) & 7;
    delta = tbl_5F02[dir - f25];     /* table at $5EFA..$5F09: index -8..7 -> 00 01 01 01 01 FF FF FF 00 01 01 01 01 FF FF FF
                                        => shortest rotation: +1 if (dir-f25)&7 in 1..4, -1 in 5..7, 0 if aligned */
    if (delta) { f25 = (f25 + delta) & 7; f35 = f25; f36 = g-8414 (10: turn delay); if (f37 < 2) f37 = 1; tail; }
    if (f37 >= 2) tail;                                 /* reloading */
    if (f37) { f37--; tail; }                           /* one frame after alignment */
    f37 = g-8413 (50: reload);  f31 |= 0x20;            /* FIRE: aimed bullet from the muzzle */
    f38 = x + muzzle_dx[f25]; f40 = y + muzzle_dy[f25];   /* $5EDA: dx 13 1A 1E 1B 14 0D 09 0E, dy 06 0A 0F 17 1C 17 0F 0A */
    tail;
```
g-8414 = 10, g-8413 = 50 at game start ($1196), reduced by 2 / 10 at every stage clear ($7370) (L).
Flash frames for mode 0: template f34 = 8 (white frame), f35 = 0 but f35 is overwritten with the direction
on every turn, so a hit shows frame 8 and the current direction alternately (f30 = 3 -> frames dir, 8, dir) (L).

**Mode 1 (g7228 == 1): hatch gun ($2DD8).** 70-frame cycle on f36, fires once per cycle. (V: stage-1 objlog:
19 lives, b25 <= 4 alive / 11 dead, b31 28 seen.)
```c
    if (++f36 >= 70) f36 = 0;
    d = f36 >= 35 ? 70 - f36 : f36;                     /* triangle 0..35 */
    f25 = d < 20 ? 0 : d < 25 ? 1 : 2;                  /* hatch opens at 20, 25 */
    if (d == 35) { f38 = x + 0x14; f40 = y + 0x12; f31 |= 0x20; }   /* one aimed bullet at the peak (f42 = 0 -> nearest player) */
    tail;
```
**Mode 2: silo ($2E08, hp 31/23).** (V: stage-2 objlog 27 lives, hp0 23, b31 20/28 seen (= the hit-fire
above), hp changes 49; the launch itself (L) — $6AA2/$6AC8 not in any register trace.)
```c
    if (!f36) f36 = (rnd() & 0x3F) | 0x17;              /* 23..63 frames */
    if (--f36 == 0) {                                   /* timer started below $20: launch type B */
        h = LAB_75E4(x - g7204 + 16, y - 0xA3, type 13, D4 = 0x10000);   /* hostile at (x+16, y+93), f12.l = 1.0 */
        if (h) { h->f28 = 1; h->f27 = ~h->f27; }          /* f27 inverted (mirror/direction flag, hostile agent) */
    } else if (f36 == 0x20) {                           /* timer started >= $20: launch type A when passing $20 */
        f36 = 0;  h = LAB_75E4(same); if (h) h->f28 = 1; }   /* f36 = 0 re-arms the random timer next frame */
    tail;
```
**Mode 3: static block ($2E38)** — no behaviour (`BRA LAB_6EE4`). (V: stage-3 objlog type $20 gfx 030640,
hp0 5 = 7-2, b31 only 00/08.)

### 2.9 Type $21 — static scenery ($2E68, $2EF8, $2F28): no behaviour; only the shared damage/death/flash
(§2.7) and the tail. Score 150/200/400. (V: objlog 82 lives stage 0, killed 25, b25 <= 8 = f33.)

### 2.10 Type $22 — blinking beacon ($2E98 mode 2, $2EC8 mode 0): f43 counts 8 frames, toggles f25 0/1.
```c
    if (--f43 == 0) { f43 = 8; f25 = f25 ? 0 : 1; }  tail;      /* (V: objlog b43 <= 8, b25 0/1 alive) */
```
(Initial f43 = rnd&7 + 1 desynchronises beacons.) When hit: f30++ per hit and f25 = 6 - f30 (mode 0) /
f34-f35 flash (mode 2).

### 2.11 Types $25/$26 — mines ($2F58/$2F88): drop ONE stationary hazard (or never), 50 %:
```c
    if ((s8)f36 < 0) tail;                              /* done */
    if (f36 == 0) { r = rnd(); if (r & 0x40) { f36 = 0xFF; tail; }  f36 = (r & 0x3F) + 0x40; }   /* 64..127 frames */
    if (--f36) tail;
    f36 = 0xFF; f31 |= 0x20; f38/f40 = (x + 2, y + 7) for $25, (x + 0x14, y + 9) for $26;    /* LAB_4704: stationary effect + sound 56 */
    tail;
```
(V: objlog: 30 x $25 + 32 x $26 in the fire run produced 17..22 b16=12 effects; b31 20/28 seen.)

### 2.12 Type $27 — STAGE GATE ($2FB8; spawned by the progress triggers in §1). Never shootable.
```c
    if (y >= 0x1FC) g7230 = 0;                          /* leaving the screen: allow the next gate */
    f31 |= 4;
    f25 = ((g-28551 & 0x1F) < 8) ? 0 : 1;               /* blink: frame 0 for 4 of every 16 game frames */
    box = [x-16, x+16] x [y-16, y+16);                  /* around the record's TOP-LEFT (x,y); bottom edge exclusive */
    n = 0; out = 0;
    for p in (P1, P2): if (p->f38 < 0xAF && p->f38 != 0x64) { n++; if (!(p->x in box && p->y in box)) out = 1; }   /* alive players */
    if (n && !out) { if (++g-8397 >= 10) g-4100 = ~g-4100; }    /* 10 consecutive frames with every live player inside -> stage clear (LAB_7002) */
    else g-8397 = 0;
    tail;
```
(V: pcset: $6DB0 (counter++) executed, $6DBE (toggle) never — the autopilot never held the box, so the stage
never changed in the stage-0 runs; 7230 shows the gate index; objlog gate b31 = 04/0c only. The 10 count: L.)
Entering the gate: LAB_7002 (-4100 != 0) plays EXT_2472C/2474A, sets g-4099 bit (7230), pre-rolls 256 frames.

### 2.13 Type $28 — stage-1 hatch spinner ($2FE8/$3018). NOT SEEN: (L). fire period g-2196 (75/100/125).
```c
    if (f18 == 12) { f38 = x + 4; f40 = y + 4; f31 |= 0x20; }       /* fires while the hatch is open */
    if (!f18) f18 = g-2196;
    f18--;
    if (f18 < 24) f25 = tbl_5F0A[f18];                  /* 08 08 08 08 09 09 09 09 0A x8 09 09 09 09 08 08 08 08: hatch open/close */
    else { if (!(g-28551 & 2)) f25++; f25 &= 7; }      /* spinning idle frames 0..7, every other frame */
    tail;
```

### 2.14 Type $29 — invisible spawner ($3048, no gfx, hp 49/37 but never shootable). (V: stage-1 objlog 10
lives, 221 frames, y0 = $100-15, b31 04/0c, hp constant 37; spawn (L): $6EBC not in the 65-frame traces.)
```c
    if (y >= 0x1CE) free;                               /* gives up before the bottom */
    f31 |= 4;
    if ((s8)f36 < 0) skip;
    if (f36 == 0) { r = rnd(); f36 = (r & 0x80) ? r /*negative: never*/ : (r & 0x3F) + 0x40; }
    if (--f36 == 0) { f36 = 0xFF; LAB_75E4(x - g7204 + 6, y - 0x106, type 8); skip; }   /* hostile 8 at (x+6, y-6) */
    if (f36 == 20) sound(24);                           /* warning */
    skip;                                               /* never rendered, no box */
```

### 2.15 LAB_4704 / LAB_47D2 — what a fire request becomes (runs later in the same game frame)
```c
LAB_4704: if (attract && g-28550 >= 1500) return;
    for each hostile with f30 bit5: ... (hostile agent)   /* hostiles use f58/f60 as the muzzle */
    for each live object with f31 bit5 (LAB_4778):
        g-14398 = o->f38; g-14396 = o->f40; g-14384 = 0;
        if (o->f17 == 0x25 || o->f17 == 0x26) { g-14384 = ~0; sound(56); }   /* stationary hazard */
        LAB_47D2(o);
LAB_47D2(A0 = owner): if (g-25334 /*nova active*/) return; if (g-12702 & g-12436 & 0x80 /*both players dead*/) return;
    if (effect slot g10062 (= 15) is occupied) return;  /* pool of 16 x 20 bytes at $4976, kept sorted by y */
    e = insert position: first entry with y >= g-14396 (entries above it are shifted up by 20 bytes);
    if (g-14384) { e = {x, ., y, ., vx = 0, vy = 0, f16.l = 0x0C600001 (or 0x0C700018 if LAB_79DE)}; return; }   /* mine */
    speed = g-14386 (= $180 = 1.5 px per display frame, 8.8; set from g10064 at game start);
    if (g-14390 != 0) { vx,vy = |g-14390|, |g-14388| with signs; g-14390 = 0; }   /* explicit direction (types 4, 5) */
    else { LAB_491C -> (dx1,dy1,dist1) to P1, (dx2,dy2,dist2) to P2;
           use P1 if owner->f42 == 1, P2 if f42 == 2, else the live / nearer player; }
    scale so that the major axis = speed: if (dy >= dx) { vx = dx*speed/(dy|1) << 8; vy = speed << 8; } else { vy = dy*speed/(dx|1) << 8; vx = speed << 8; }
    apply signs; e = {x = g-14398, y = g-14396, vx (16.16), vy (16.16), f16.l = 0x07580000};
```
(V: E lines: b16 = 7, b17 = 88 and |v| = 3.2 px per game frame (= 1.5 * 2 display frames, LAB_4ADA runs
twice per game frame); b16 = 12 entries have v = 0 at birth, drift with the scroll and later accelerate —
that is LAB_4ADA's business.)

## 3. Collision, damage, score, pickups (main loop $C4C.. : LAB_34FA, LAB_3424, LAB_3748, LAB_35F0)

NOTE: BRIEF.md has the two names swapped. In the listing **LAB_3424 = player shots vs the OBJECT pool
($2E040, box f48..f54)** and **LAB_34FA = player shots vs the HOSTILE pool ($2DC80, box f16..f22)**.
(runtime.c: collide_player_shots_with_entities = LAB_34FA = hostiles; ..._with_projectiles = LAB_3424 = objects.)
All four run for the "selected player" `g-18624`: the main loop ($C22) picks P1 ($4E3C) when `g-28551 & 2`
== 0 and P2 ($4F46) otherwise, so EACH PLAYER'S shot/ram collision runs only every OTHER game frame (L: $C22..
$C58; in a one-player game P2's pass finds nothing). `g-18620` = end of that player's 8 ASCII score digits
(p->f106..f113, so g-18620 = p + 114).

### 3.1 Shot rectangle (shared by 3.2 and 3.3)
```c
for each of the 12 shots s at p->f122 + 12*i (x.w, y.w, vx, vy, .., dmg.b at +11), skip if s->x == 0:
    if (p->f90) { x1 = sx + 0x30; y1 = sy + 0x30; x2 = sx - 0x20; y2 = sy - 0x20; }   /* p->f90 != 0: big (nova?) shot: 80x80 box centred -8 */
    else        { x1 = sx + p->f62; y1 = sy + p->f64 + 10; x2 = sx; y2 = sy - 10; }   /* p->f62/f64 = shot w/h from the weapon table ($2024 + idx*24 + level*4 -> LAB_2B32 copies +20.l): 8 x 24 for weapon 0 (V: chip_2700 p1+62 = 8, +64 = 24) */
    /* hit test against a box [bx1,bx2) x [by1,by2):  y2 < by2 && y1 >= by1 && x2 < bx2 && x1 >= bx1  (signed word compares) */
```
i.e. the shot rectangle [sx, sx+w] x [sy-10, sy+h+10] must overlap the target box (strict on the far edges).

### 3.2 LAB_3424 — shots vs OBJECTS
```c
for each shot:  for each object o (18, stop at first hit of this shot):
    if (o->x == 0) continue;  if (!overlap(o->f48, o->f50, o->f52, o->f54)) continue;
    if (o->f31 & 4) continue;                           /* dead / hidden / never-shootable */
    if ((s8)s->dmg < 0) o->f24 += 2;                    /* PENETRATING shot: not consumed, 2 damage, can hit again next frame */
    else { s->x = 0; o->f24 += s->dmg; }                /* normal shot consumed; damage queued in the mailbox (applied by LAB_5F34 NEXT frame) */
    if ((s8)(o->f28 - o->f24) < 0) {                    /* this shot will kill it */
        pts = o->f44; sound(29);
        if (o->f17 == 0x20) g-27618 = 12;               /* turret kill: 12-frame palette flash (LAB_1420) */
    } else { pts = g-19422 (= $2500 -> 25 points); sound(o->f46); }     /* a hit that does not kill */
    award(pts); break;
```
(V: state_8000 $349E x3 (shot consumed, D6 = dmg 1), $34A8, $34E0 -> LAB_40BE with D6 = $2500 x3; objlog
hp0 2 -> hpmin -2 with b31 04 afterwards.) Quirk (L): because f24 is only applied next frame and bit2 is only
set then, several shots hitting the same object in one frame EACH award the kill score and sound.

### 3.3 LAB_34FA — shots vs HOSTILES
```c
for each shot: for each hostile h (12):
    if (h->x == 0) continue;  if (!overlap(h->f16, h->f18, h->f20, h->f22)) continue;
    if (h->f29 /*exploding*/ || (h->f30 & 0x80)) continue;
    if ((s8)s->dmg < 0) h->f62 += 2; else { s->x = 0; h->f62 += s->dmg; }    /* f62 = damage taken, consumed by the hostile update next frame */
    if ((s8)(h->f24 - h->f62) < 0 && !(h->f31 == 9 && h->f63 != 5)) LAB_35CE(h);   /* KILL (a type-9 boss part only dies on frame 5) */
    else { pts = g-19208 (= $5000 -> 50 points); sound(h->f25 /* hit sound from the descriptor +13 */); }
    award(pts); break;
LAB_35CE(h): pts = h->f54 (BCD, from descriptor +24);  snd = 28;
    if (h->f31 == 2) { snd = 29; if (*(s8*)0x2E02F /* hostile slot 11 +63 */ >= 7) snd = 20; }
    sound(snd);
```
(V: state_8000: $35CE x2 then LAB_40BE D6 = $0001 = 100 points (hostile f54 = $0001); state_stage1: D6 =
$5000 = 50-point hits.)

### 3.4 LAB_40BE — score award (BCD word D6 -> ASCII digits)
```c
award(u16 v):  /* V: trace $2500 -> +25, formula from $40BE..$4104 */
    digits[4] = { (v & 0xFF) >> 4, v & 0x0F, (v >> 8) >> 4, (v >> 8) & 0x0F };   /* at $40B8..$40BB; $40B4..$40B7 = 0 */
    carry = 0;
    for (i = 7; i >= 0; i--) {           /* units first; digit source = $40BB, $40BA, $40B9, $40B8, 0,0,0,0 */
        c = score[i] + digits[..] + carry; carry = 0; if (c >= '9'+1) { c -= 10; carry = 1; } score[i] = c; }
    => points = BCD(hi byte) + 100 * BCD(lo byte):  $5002 = 250, $0025 = 2500, $2500 = 25, $5000 = 50, $0001 = 100.
```
Score values: object kill = template +44 (table §1.1: 500/200/2500/1500/1000/2500/250x6/250/400/2000/400/150/
250/300/200/400/100/100/0/100/100/0); object hit = 25; hostile hit = 50; hostile kill = descriptor +24.
(Only p->f106.. of the SELECTED player is credited: g-18620.)

### 3.5 LAB_35F0 — player vs HOSTILES (ram / pickup)
```c
p = g-18624;  box = [p->x + 7, p->x + 0x19] x [p->y + 7, p->y + 0x17];      /* 18 x 16 inside the 32x32 ship */
for each hostile h: h->f30 &= ~0x40;
    if (!h->x) continue; if (!overlap(h->f16..f22 vs box)) continue;
    if (h->f29 || (h->f30 & 8)) continue;
    if (h->f31 == 5) { LAB_369A(h); continue; }         /* PICKUP */
    if (h->f31 == 6 && h->f63 >= 5) continue;           /* type 6 harmless from frame 5 on */
    if (p->f52) continue;                               /* invulnerable */
    h->f62 += 10;                                       /* the hostile takes 10 damage from the ram */
    hit_player(p);
hit_player(p): p->f38 = 0x64 (dying); p->f49 = 0x46 (70-frame death timer); p->f52 = 0x270F (invulnerable
    until respawn); p->f2 -= 12; JSR EXT_24732 (death sound).           /* (L) never fired in the invuln runs */
```
### 3.6 LAB_369A — pickup (hostile type 5; h->f28 = subtype from the wave list)
```c
LAB_369A(h): h->x = 0;                                  /* consumed */
    if (h->f28 == 0x0A) {                               /* NOVA charge */
        EXT_24738 (sound);
        if (p->f66 >= 8) goto bonus;                    /* already full */
        p->f66++; redraw HUD: LAB_2A5E (P1) / LAB_2AA2 (P2); return; }
    EXT_2473E (sound);  clear all 12 shots (p->f122 + 12*i .x = 0);
    p->f59 = h->f28 >> 1;                               /* weapon (colour) index: subtypes 0/1 -> 0, 2/3 -> 1, ... */
    if (p->f60 < 5) { p->f60++; LAB_3722; return; }     /* weapon level up; LAB_3722 = LAB_2B32(*($2024 + f58*24 + f60*4)) reloads the shot descriptor */
    LAB_3722;
bonus: LAB_40DE(A2 = p->f114, A3 = $40AC) -> adds the digits at $40A4..$40AB = "00010000" = 10000 points (V: chip_2700 $40A4.. = 00 00 00 01 00 00 00 00)
```
(L: no pickup happened in any capture — $369A absent from pcset.)

### 3.7 LAB_3748 — player vs ENEMY BULLETS (effect pool $4976)
```c
if (g-25334 /*nova bomb active*/) return;  p = g-18624;  if (p->f52 /*invulnerable*/) return;
for each effect e at $4976 + 20*i, stopping at the first free one (the pool is compacted/sorted):
    if (p->y < e->y && p->y + 0x17 >= e->y && p->x < e->x && p->x + 0x19 >= e->x) {   /* bullet POINT inside (x, x+25] x (y, y+23] */
        hit_player(p);  LAB_4E1A(e) /* delete the entry: shift the rest down 20 bytes */;  EXT_24732;  }
    /* (L) after a delete the loop still advances 20 bytes, so the entry shifted into this slot is skipped this frame */
```
(L: never fired in the captures — all runs were BS_INVULN or the autopilot; hit_player as in 3.5.)

## 4. Measurements (re/stats/object_stats.txt, tools/object_stats.py)
Per file / stage / type: lives, median+max lifetime (game frames), y at first sample (= $100 - h + 1: all
objects are born one step above the screen), hp at birth, min hp, f19/f33, max f43, max f25, f31 and f42
values seen, gfx pointers, kills (hp went negative) vs scrolled off, count of hp changes. Effects: per
(b16, b17, b19) lives and median speed. Highlights:
* Lifetime = (h + 256) / g7222 frames: 287 for h=32, 303 for h=48, 319 for h=64, 221 for the $29 spawner
  (freed at y >= $1CE) (V). y advances exactly g7222 = 1 per game frame (V: all 800+ lives).
* hp at birth = template hp - ((hp+1)>>2) everywhere (one-player) (V).
* Stage 0 (fire run): $20 87 lives / 35 killed, $21 82 / 25, $22 52 / 12, $25 30 / 7, $26 32 / 6, $01 26 / 0
  (unhittable), $27 5 gates; stage 1: $20 19 / 3, $29 10; stage 2: 03 35, $20 27, $22 22; stage 3: 00 37,
  02 38, $20 18.
* Enemy bullets: b16 = 7 lives median 58 game frames, 3.2 px per game frame; 1132 in the 9000-frame fire run.
  b16 = 12 mines: born with v = 0 (V), later accelerate (LAB_4ADA, not here).

## 5. Verification summary
(V) template copy ($332E..$33C6: x = (23-col)*16+$100, y = $100-h, f10/f26/f4, 1p health -1, random & mask +1);
(V) y += g7222 ($6870) and objlog; (V) shot hit -> mailbox ($349E/$34A8) -> hp decrement next frame ($6968 D1=1)
-> kill sets bit2 (objlog b31 04/0c after hpmin < 0); (V) scores: 25 per object hit, 50 per hostile hit, 100 per
hostile f54=$0001, formula; (V) type-01 hostile-7 spawn args; (V) turret aim/fire ($6B68../$6C66 executed,
b42 = 1, b31 bit5); (V) mapping mode 0..3 by gfx pointers / types per stage; (V) gate counter increments
($6DB0 in pcset) but the 10-frame toggle ($6DBE) never ran: (L).
(L) only: types 04, 05, $28 (never spawned in captures), silo launches ($6AA2/$6AC8), type-$29 spawn ($6EBC),
pickups (LAB_369A), hit_player paths, the bullet-speed/velocity maths of LAB_47D2 (E-line speeds agree).
Unclear: meaning of hostile f27 inversion for the second silo launch; what LAB_79DE selects for the mine
effect variant ($0C700018); +16 of the template (always 0, copied, never read in this code).
