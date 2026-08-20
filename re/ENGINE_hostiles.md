# Battle Squadron — HOSTILE pool engine spec (C pseudocode)

Pool: `$2DC80`, 12 records x 80 bytes (slot n at `$2DC80 + n*$50`; slot 8 = `$2DF00`, 9 = `$2DF50`, 10 = `$2DFA0`,
11 = `$2DFF0` — the boss code addresses these four slots by absolute address). A record is live iff `rec->x0 != 0`
(word at +0). Everything below is from `~/BattleSquadron-Amiga/asm/loader.asm` (addresses are runtime addresses)
cross-checked against the captures in `re/trace/` and `runtime.c`. Marks: **(V: how)** verified, **(L)** listing only.

Companion files: `re/handlers_hostiles.txt` (one line per type), `re/waves_level1.txt` (stage-0 wave table + scripts),
`re/stats/hostile_stats.txt` (objlog statistics, produced by `tools/hostile_stats.py`).

## 0. Units, coordinates, record layout

* `x` (+0 word, +2 frac) is a *playfield* x: screen_x = `x - g7204` (scroll 7204 = left edge of the 320-px window,
  range 256..352 in the captures — the playfield bitmap is 384 px wide, bitmap column = x - 256). `y` (+4, +6 frac) :
  screen_y = `y - 256`; `y >= $200` = below the screen, `y < $100` = above it. Both are 16.16 in the long view
  (`rec->x32 += vx`). **(V: LAB_7608 adds g7204, LAB_55D8 subtracts $100; objlog type-1 y 224+3n)**
* `g7222` = vertical scroll speed per game frame (1 in play, 0 = stopped: wave scheduler and ground objects freeze).
* One game frame = 2 display frames (25 Hz). `g-28552` (word) = DISPLAY frame counter (+2 per game frame, LAB_5502
  runs twice), `g-28551` = its low byte: every "alternate frame" test below is `g-28551 & 2` (toggles every GAME
  frame) or `& 1` (toggles per display frame). [corrected by the merge: see ENGINE_frame_player.md §1]

```c
struct Hostile {                      /* 80 bytes */
  int16 x, xfrac;  int16 y, yfrac;    /* +0..+7  16.16 position, playfield coords (see above) */
  int32 p8, p12;                      /* +8, +12 per type: type 0: {dur,dir,turn,turntimer} + script ptr;
                                         type 1: p12 = vx; type 3/D: {dur,step,phase,phasestep} + script ptr;
                                         type 4/8: vx, vy; type 5: p8 = drift accel, p12 = rel-x 16.16;
                                         type 6: p8 = vx; type 9: p8 = vx or path index; */
  int16 box[4];                       /* +16 x0,x1,y0,y1 — recomputed every draw (LAB_981C) */
  int8  hp;  uint8 hitsnd;            /* +24 armour (signed; <0 after the killing hit), +25 = sound number played on hit */
  uint8 t26, t27, t28;                /* per type timers/phases (+26, +27, +28; +27/+28 preloaded from descriptor +26/+27) */
  uint8 explode;                      /* +29 explosion countdown (8 -> 0, steps on alternate frames) */
  uint8 flags;                        /* +30: bit0/1/2 per type (type 1: fired@256/fired@416; 8: heading-left;
                                         9: started), bit3 = does NOT hurt the player on contact (LAB_35F0 skips),
                                         bit4 = off-screen/not drawn this frame (LAB_55D8), bit5 = FIRE REQUEST
                                         (LAB_4704 spawns a bullet at (+58,+60) and clears it), bit6 = processed
                                         this frame, bit7 = immune to player shots (LAB_3544 skips) */
  uint8 type;                         /* +31 */
  uint32 gfxmask, gfx;                /* +32 mask-plane base, +36 bitplane base */
  int16 g40,g42, frame_bytes, frame_stride, modulo, h, w_words, score; /* +44 = (2w-2)*h, +46 = 6*that (5 planes+mask),
                                         +48 = $30-2w, +50 h (+51 low byte), +52 width in words, +54 score (BCD, see 7) */
  uint8 flash;                        /* +57 hit-flash timer */
  int16 fire_x, fire_y;               /* +58/+60 bullet origin for the fire request */
  uint8 damage;                       /* +62 damage accumulated by player shots this frame (LAB_3544 adds) */
  uint8 frame;                        /* +63 animation frame */
  int16 box_dx, box_w;                /* +64/+66 from descriptor +28 long */
  int16 clip_w;                       /* +68 = (w-1)*16 */
};
```

### 0.1 Descriptor table `$CD7A + type*$20` (V: RAM, identical in all images)
| type | h | w(words) | hp (desc) | hp used* | hit snd (+25) | gfx (+36) | mask (+32) | score (+54 BCD) | +26→rec+27 | +27→rec+28 | +28 → box dx,w |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 00 plane | 32 | 3 | 0 | 0 | 0 | 017780 | 017500 | $0001 = 100 | 0 | 0 | 0,32 |
| 01 aimed drone | 32 | 3 | 3 | 2 | $3B | 020780 | 020500 | $5000 = 50 | 0 | 0 | 2,28 |
| 02 mothership part | 32 | 6 | 89 | 67 | $3B | 0511E0 | 050BA0 | 0 | $FF | 0 | 2,76 |
| 03 ground pop-up | 32 | 3 | 7 | 5 | $32 | 036280 | 036000 | $0005 = 500 | 9 | 0 | 2,28 |
| 04 swooping fighter | 32 | 3 | 3 | 2 | $35 | 01A780 | 01A500 | $0001 = 100 | 0 | 0 | 0,32 |
| 05 pickup | 16 | 2 | 1 | 1 | 0 | 012930 | 012890 | 0 | 0 | 0 | 0,16 |
| 06 bomber | 44 | 3 | 31 | 23 | $3B | 0147E8 | 014450 | $0010 = 1000 | 0 | 0 | 0,32 |
| 07 rising ground gun | 32 | 3 | 7 | 5 | $3B | 043980 | 043700 | $0005 = 500 | 0 | 0 | 2,28 |
| 08 homing missile | 32 | 3 | 3 | 2 | $33 | 017780 | 017500 | $5007 = 750 | 0 | $C8 | 2,28 |
| 09 boss body/pod | 32 | 7 | 127 | 95 | $34 | 02FCE0 | 02F560 | $0050 = 5000 | 0 | 0 | 4,86 |
| 0A boss puff | 32 | 3 | 0 | 0 | 0 | 011310 | 011090 | 0 | 0 | 0 | 32,-32 |
| 0B flypast | 32 | 3 | 0 | 0 | 0 | 043980 | 043700 | 0 | 0 | 0 | 32,-32 |
| 0C tank | 50 | 5 | 47 | 35 | $33 | 050F90 | 0507C0 | $0025 = 2500 | $14 | 0 | 8,48 |
| 0D pop-up drone | 16 | 2 | 1 | 1 | $32 | 0373E0 | 037340 | 0 | 0 | 0 | 0,16 |

\* `hp used` = value actually written to +24 when `g-2732 == 0` (all captures): `hp -= (hp+1) >> 2` **(V: objlog hp0 of
types 1/3/6/9/C/D = 2/5/23/95/35/1)**. Descriptor +4..+11 (initial box) is 0,0,32,32 for 0..9 and zero for A..D; it is
overwritten by LAB_981C on the first draw. Score decoding: `points = bcd(lo byte)*100 + bcd(hi byte)` (LAB_40BE adds
the 4 nibbles at the units/tens/hundreds/thousands positions in that order) (L).

## 1. Allocator LAB_75E4 / initialiser LAB_7608

```c
/* LAB_75E4: D1 = x param, D2 = y param, D3 = type, D4 = script/velocity long. Returns A0 = record or nothing. */
Hostile *hostile_alloc(int16 xp, int16 yp, uint8 type, uint32 p12) {
  if (g-16120 /* game-over gate */ && !g-28516 /* not demo */) return NULL;            (L)
  for (slot = 11; slot >= 0; slot--)            /* searches from slot 11 DOWN to 0 */
    if (pool[slot].x0 == 0) return hostile_init(&pool[slot], xp, yp, type, p12);
  return NULL;                                   /* pool full: the spawn is silently lost */
}
/* LAB_7608 */
Hostile *hostile_init(Hostile *r, int16 xp, int16 yp, uint8 type, uint32 p12) {
  Desc *d = $CD7A + type*$20;
  r->type = type;
  r->x = (xp < $320) ? xp + g7204 : xp - 1000 + 256;   r->xfrac = 0;        (V: objlog spawn x = param + scroll)
  r->y = yp + 256;  r->yfrac = 0;                                               (V: objlog y0 = param + 256)
  r->p8 = 0;  r->p12 = p12;
  r->box = d->box;  r->hp16 = d->hp16;           /* +24 = hp, +25 = hit sound */
  if (g-2732 == 0) r->hp -= (r->hp + 1) >> 2;    /* difficulty: fewer hit points */
  r->t26 = 0; r->t27 = d->b26; r->t28 = d->b27; r->explode = 0; r->flags = 0;
  r->gfxmask = d->gfx16; r->gfx = d->gfx20;  r->l40 = 0;
  r->h = d->h;  r->w_words = d->w;  r->clip_w = (d->w - 1) << 4;
  r->modulo = $30 - 2*d->w;  r->frame_bytes = (2*d->w - 2) * d->h;  r->frame_stride = 6 * r->frame_bytes;
  r->score = d->w24;  r->flash = 0; r->damage = 0; r->frame = 0;  r->box_dx/box_w = d->l28;
  return r;
}
```
Default frame addressing (LAB_97F8): `plane = gfx + frame * frame_stride; mask = plane + frame_stride - frame_bytes`
i.e. each frame is 5 bitplanes followed by one mask plane, each `(2w-2)*h` bytes. LAB_9814 uses +36/+32 directly.

## 2. Per-frame driver LAB_79E2 — pass selection, explosion, draw (V: main loop $AE4..$BCE; state_2000 shows $B7A/$B82/$BC2/$BCA each 131x in 131 frames, type-0 records only reached $7A2C from $B82/$BCA = the 'every remaining type' passes, split by y)

The main loop calls LAB_79E2 FOUR times per game frame, racing the beam (after the upper half of the playfield has been
restored/drawn, then after the lower half):
```
  pass A: g-1792 = 0,    g-1791 = 0     -> records with y <= $146, types $0D/$07/$03 only     ($B72)
  pass B: g-1792 = 0,    g-1791 = $FF   -> records with y <= $146, every remaining type        ($B7E)
  pass C: g-1792 = $FF,  g-1791 = 0     -> records with y >  $146, types $0D/$07/$03 only     ($BBA)
  pass D: g-1792 = $FF,  g-1791 = $FF   -> records with y >  $146, every remaining type        ($BC6)
```
(Ground types 3/7/D are updated/drawn first in each half = they end up under the flyers.) During the end-boss
(`g-26242 != 0`, loop variant $ACA) there are only two passes: upper (g-1791=$FF) then lower. Flag bit6 marks a record
as done; LAB_35F0 (player collision, later in the frame) clears bit6 of every record (`BCLR #6,30(A0)` at $3614). A
record that is re-initialised inside the pass (LAB_9986 → $79F2) is processed again immediately.

```c
void hostile_pass(void) {                                            /* LAB_79E2 */
  for (slot = 0; slot < 12; slot++) { Hostile *r = &pool[slot];
  again:
    if (!r->x0) continue;
    bool lower = r->y > $146;
    if (lower != (g-1792 != 0)) continue;                            /* half selection */
    if (!g-1791 && r->type != $0D && r->type != 7 && r->type != 3) continue;
    if (r->flags & 0x40) continue;   r->flags |= 0x40;
    if (r->explode) {                                                /* $7A42 explosion countdown */
      if (!(g-28551 & 2)) {                                          /* steps on alternate frames */
        if (--r->explode) { r->gfx += $300; r->gfxmask += $300; }    /* next 32x32 explosion frame ($11090/$11310 set by LAB_98E6) */
        else if (r->type != 0) { free(r); continue; }                /* LAB_98E2 */
        else {                                                       /* finished type-0 plane: last of its wave? */
          n = count of live records with type 0 (including this one);
          if (n != 1) { free(r); continue; }
          turn_into_pickup(r, /*nova*/ true);  goto again;           /* LAB_9986, see type 5 */
        }
      }
      draw(r, r->gfx, r->gfxmask);  continue;                        /* LAB_9814 */
    }
    switch (r->type) { ... handlers below ... }                      /* each ends in draw (LAB_97F8/981C), skip (LAB_98D0) or free (LAB_98E2) */
  }
  *(int16*)g-1800 = 0;                                               /* terminate the restore list */
}
/* LAB_98E6: start the explosion (called by several handlers when hp < 0) */
void explode(Hostile *r) { r->explode = 8; r->flags = (r->flags & ~0x20) | 0x80; r->gfx = $11090; r->gfxmask = $11310; r->h = 32; goto $7A42; }
/* LAB_981C: collision box + draw + restore-list entry */
void draw(Hostile *r, plane, mask) {
  r->box = { x + box_dx, x + box_dx + box_w, y, y + h };            /* used by LAB_3544 (shots) and LAB_35F0 (player) */
  LAB_55D8(r, plane, mask);                                          /* cookie-cut blit; sets flags bit4 if fully off-screen */
  if (r->flags & 0x10) return;
  push {clamp(x,$100..), clamp(y,$100..$1FF-h), h, w_words} on the restore list g-1800 (split in two at y=$180 = the raster split).
}
```
Explosion length: 8 steps x 2 frames = 16 game frames (V: objlog +29 = 8..1, records vanish after).

## 3. The fire request and the effect pool (LAB_4704 / LAB_47D2 / LAB_491C)

Every handler "fires" by setting `flags |= 0x20` and `fire_x/fire_y` (+58/+60); optionally writing a direction into
`g-14390/g-14388` (dx,dy words; 0 = aim at a player) and `g-14384` (non-zero = mine). LAB_4704 (main loop, after the
hostile passes) walks the pool:
```c
void spawn_requested_bullets(void) {                                 /* LAB_4704 */
  if (g-28516 /*demo*/ && g-28550 >= 1500) return;
  for each hostile r with flags&0x20: r->flags &= ~0x20; if (!r->x0) continue;
    g-14398 = r->fire_x; g-14396 = r->fire_y; g-14384 = 0;
    if (r->type == 9 && r->fire_y - r->y == $26) { g-14384 = ~0; sound(56); }      /* boss drops a MINE */
    LAB_47D2();
  (same for object-pool records with +31 bit5: +38/+40, types $25/$26 drop mines, sound 56)
}
void LAB_47D2(void) {
  if (g-25334) return;  if (both players dead: g-12702 & g-12436 both negative) return;
  if (effect[g10062].x0 != 0) return;          /* bullet cap: slot g10062 (=15) occupied -> no more bullets */
  insert a 20-byte effect record ($4976, 16 x 20, kept sorted by y) at the fire position:
    e->x = g-14398; e->y = g-14396;
    if (g-14384) { e->vx = e->vy = 0; e->kind16 = $0C600001 (or $0C700018 if LAB_79DE) }   /* stationary mine */
    else {
      speed = g-14386 << 8  (= $18000 = 1.5 px/frame, set from g10064 at game start; +$80 per ... at $F98)
      if (g-14390 || g-14388) dir = (g-14390, g-14388), g-14390 = 0        /* explicit direction */
      else { LAB_491C: dir = player centre (px+12, py+16) - fire pos, for the nearer (Manhattan) living player
             (object spawners +42 = 1/2 force player 1/2) }
      the LARGER |component| gets the full speed, the other is scaled: if |dy| >= |dx|: vx = dx*speed/(dy|1), vy = speed ...
      e->vx, e->vy (16.16, signs restored); e->kind16 = $07580000 (plain bullet)
    }
}
```
(L; structure confirmed by objlog E lines `b16 b17 = 07 58` for bullets.) LAB_491C also returns D7 bits
(bit0 = player1 is left of the point, bit1 = above, bit2/3 same for player 2) and D5/D6 = Manhattan distances —
reused by the missile (type 8) and the swooper (type 4).

## 4. Type handlers

### 4.0 Type $00 — scripted plane (LAB_9752) — waves of 4, 100 pts (V: objlog/trace)
```c
/* script stream at r->p12: 4-byte commands {duration, dir, turn, turn_timer}; $FF,$00,ptr.l = chain; $FF,$01 = end */
void type0(Hostile *r) {
  if (r->dur == 0) {                                         /* +8 */
    uint8 *s = r->p12;
    while (s[0] == $FF) { if (s[1] == 1) { free(r); return; }  if (s[1] != 0) break;  s = *(uint32*)(s+2); }
    r->dur = s[0]; r->dir = s[1]; r->turn = s[2]; r->turntimer = s[3];  r->p12 = s + 4;
  }
  v = vec_table[r->dir & 31];  scale = ((r->dir & $E0) >> 2) + 8;                     /* 8..64 */
  r->x32 += v.vx * scale;  r->y32 += v.vy * scale;         /* px/frame = v*scale/65536: idx 8 scale 64 -> 4.0 px */  (V: trace $97B0 d1=fffc0000 for dir $F8)
  if (r->turntimer && --r->turntimer == 0) { r->dir += r->turn;  r->turntimer = r->p12[-1]; }   /* reload from the command's byte 3 */
  r->dur--;
  r->frame = (r->dir & 31) >> 1;  r->gfx = $17500;            /* 16 heading frames, frame = idx/2 */
  if (r->damage) { explode(r); return; }                      /* hp is 0: any hit kills (100 pts via LAB_35CE) */
  draw_frame(r);                                              /* LAB_97F8 */
}
```
Vector table `$CCF2` (32 x {vx.w, vy.w}) — **(V: RAM chip_2700 == pristine build/loader-rebuilt.bin)**:
```
idx: 0:(0,17873) 1:(800,17989) 2:(1568,18315) 3:(2272,18874) 4:(2896,19618) 5:(3408,20526) 6:(3792,21550) 7:(4016,22667)
     8:(4096,0) 9:(4016,1163) 10:(3792,2280) 11:(3408,3304) 12:(2896,4212) 13:(2272,4957) 14:(1568,5515) 15:(800,5841)
    16:(0,5957) 17:(-800,5841) 18:(-1568,5515) 19:(-2272,4957) 20:(-2896,4212) 21:(-3408,3304) 22:(-3792,2280) 23:(-4016,1163)
    24:(-4096,0) 25:(-4016,22667) 26:(-3792,21550) 27:(-3408,20526) 28:(-2896,19618) 29:(-2272,18874) 30:(-1568,18315) 31:(-800,17989)
```
idx 8..24 = half-ellipse vx = 4096 cos θ, vy = 5957 sin θ (θ = (idx-8)*11.25°, downward). idx 0..7/25..31 are NOT the
upward half: vy = 23830 - 5957 cos θ (moves down FAST, 2.2..2.8 px at scale 8). Whether that is a data bug or deliberate is
unknown; the level-1 scripts only use idx 8..24 (waves_level1.txt) and the traces only show idx 22/24 — flag it, do not
"fix" it. Freed by: script END, explosion. No off-screen test (scripts end off-screen). Lifetime 174-204 frames (V: stats).

### 4.1 Type $01 — aimed bullet-drone (LAB_95D8) — 50 pts, hp 2 (V: objlog trajectory, flags)
Spawned by the wave list (x += rnd + (rnd & $7f) - $40 per LAB_7556, y = -32 → 224) with `p12` = initial vx.
```c
void type1(Hostile *r) {
  T = g7228 ? $792E : $7916;  /* stage 0: thresholds {-2,-1,+1,+2 px}, vmax = 3.0 px (word at +16 = 3 = vy!), accel $3000
                                 stage>0: {-2.5,-1.5,+1.5,+2.5}, vmax 4.0 (vy 4 px), accel $4000 */
  if (!(flags&1) && y >= $100) { flags |= 1|0x20; fire at (x+11, y+32); }            /* first shot entering the screen */
  else if (!(flags&2) && y >= $1A0) { flags |= 2|0x20; fire at (x+11, y+32); }       /* second shot at screen y 160 */
  vx = r->p12;
  if (y - 30 >= p1.y && y - 30 >= p2.y) { if (vx) vx -= sign(vx)*$800; }             /* below both players: brake */
  else { pick the player nearer in |dx| (ties -> player 2; if that player is ABOVE the other by more than 30 px, take the other);
         if (target.x >= x) { if (vx != T.vmax) vx += T.accel; } else { if (vx != -T.vmax) vx -= T.accel; } }
  r->p12 = vx;
  if (r->damage) { r->damage = 0; r->hp -= dmg; if (hp < 0) { explode(r); return; }  r->flash = 6; }
  r->x32 += vx;
  frame = vx < T[0] ? 0 : vx < T[1] ? 1 : vx < T[2] ? 2 : vx < T[3] ? 3 : 4;  if (flash && !(--flash & 2)) frame += 5;  /* 5 bank frames + 5 lit */
  r->y += T.vmax.hi (3 or 4 px);  if (y >= $200) free;
  draw_frame(r);
}
```
(V: objlog slot 11 F1821..: y +3/frame, vx +$3000/frame up to $30000, frame 2→4, flags 40→61→41→43.)

### 4.2 Type $02 — end-of-game mothership (LAB_8F92) — (L only, never in a capture)
Runs only while `g-26242 != 0` (boss mode; the main loop switches to the 2-pass variant). Four records at fixed slots:
* slot 8 `$2DF00` = hull: `flags|=0x80` (immune), h=$1C w=7, gfx $44690/$45500 (flash $44D20/$45650, dead $44000/$453B0).
  Its "health" is `t27` (starts $FF): each frame it sums the damage bytes (+62) of slots 9/10/11 (142/222/302(A4)) and
  subtracts them; when it goes negative: all four records get `flags |= $88`, `flash = $66`, `t26 = $64` death timer
  (counts down; at 1 sets g-4096 = $FF = "boss destroyed" → stage end). Movement LAB_9542: slow random wander, 1 px steps,
  using the global counters g-1567 (y, bounds $F0..$110) and g-1568 (x, bounds $150..$1D0), with the gating byte 268(A4)
  (= slot 11 +28 phase) choosing wander speed. Fires when 268(A4) >= $FA: every 8 frames from (x + rnd(<$E0)-$44, y+$38) aimed.
* slots 9/10 (LAB_90FC path, `A4 != $2DFF0`): side turrets positioned relative to the hull (slot 9: hull-80 → (hull.x-$50,
  hull.y+$26); slot 10: hull.x+$60, hull.y+$28, gfx $565A0). frame 0/1 idle (alternating on g-28551 bit5), fire every 16
  frames (slot 10 offset by 2) from (x+20|38, y+20) with random direction (dx = rnd&$f.., dy = 50); hit → flash, hp<0 →
  frame 3, `flags|=0x80`; frames 3..7 = dying (advance every 8 frames), 8..11 = dead/smoking, steered by EXT_2E00C
  (= slot 11 +28) and EXT_2DF39/2DF1A (slot 8 bytes).
* slot 11 (LAB_92CC): central cannon relative to the hull (hull-240 → (hull.x, hull.y+$1C)), h=$30 w=7, gfx $457A0 +
  frame*$B40; frames 0/1 alive, 2 = destroyed, 3..6 dying, 7 = dead + t28 death phases (0..$32 slow flash, ..$C8, ..$FA,
  then $4D360/$4DEA0 big-explosion frames). Damage allowed only when slots 9 AND 10 are dead (`-17(A4) >= 3 && -97(A4) >= 3`
  = their frames >= 3); hp<0 → frame 2, immune, hp=$7F. Fire LAB_94E8: every 8 frames from (x + (rnd&$3f)+12, y+$24),
  aimed, or random direction (dx = rnd signed 7-bit, dy = 100). Kill sound 29 (20 if EXT_2E02F >= 7), LAB_35CE.
Quote for the untranslated gate: `$8FD8 TST.B 268(A4); BEQ LAB_90E6 / CMPI.B #$C8,268(A4); BCS LAB_90EC / CMPI.B #$FA ...`.

### 4.3 Type $03 — scripted ground pop-up (LAB_8D6E) — 500 pts, hp 5, does not shoot (V: objlog, L)
`flags |= 0x08` (never hurts the player by contact). Script at p12: 4-byte steps `{dur (signed), step, phase, phase_step}`,
fetched when `dur == 0`; a zero first WORD frees the record. Per frame:
```c
  phase += phase_step;  h = phase & 0x3F;                     /* visible rows: 0 = not drawn (LAB_981C: h==0 -> skip) */
  plane = $36000, mask = $36280;
  if (phase & 0x40) { plane/mask += (dur < 0 ? $900 : $600); flags |= 0x80; damage = 0; }   /* closed: armoured look, immune */
  else { flags &= ~0x80;
         if (damage) { damage=0; flash=4; hp -= dmg; if (hp<0) { t26 = $0F; flags = (flags&~0x20)|0x80; phase -= phase_step; gfx=$11090; mask=$11310; } }
         if (flash && (--flash & 1)) plane/mask += $300; }
  if (dur < 0) x += step; else y += step;   y += g7222;   dur += (dur < 0) ? 1 : -1;
  draw(r, plane, mask)  /* LAB_981C with explicit pointers */
```
Death: there is no explosion countdown; `t26 = $0F` and the gfx pointer become the explosion set — the handler keeps
running the script, so the wreck follows the rest of the path and is freed at the script's end (L). Stage-0 scripts are
listed in waves_level1.txt (typical: rise 32 rows in 32 frames while standing still relative to the ground, sit, sink, END).

### 4.4 Type $04 — swooping fighter (LAB_8B48) — 100 pts, hp 2 (V: objlog slot 11 F3261..)
NOTE: `runtime.c`'s type-4 branch ("armoured steering projectile", Manhattan homing with clamps) does NOT match the
listing or the capture; use this:
```c
void type4(Hostile *r) {
  T = g7228 ? $7952 : $7946;    /* {vmax, accel, divealt.w}: stage 0 {4.0 px, $2000, 64}; stage>0 {5.0 px, $3000, 56} */
  if (damage) { flash = 8; hp -= dmg; damage = 0; if (hp < 0) { explode(r); return; } }
  if (t26 == 0) { t26 = 1; frame = 8; vx = 0; vy = T.vmax;  new_weave(); }   /* LAB_8B96: t27 = ±((rnd&$f)|7) (7 or 15 frames, sign = rnd bit5), t28 = -t27 */
  if (t26 == 1) {                                              /* PHASE 1: dive straight down, weaving */
    frame = 8 (straight down);
    if (!(flags&1) && y >= $100) { flags |= 1|0x20; fire at (x+11, y+32); }
    if (a player is alive (g-12702 < $64 || g-12436 < $64)) {
      pick the player with the smaller |dx|; side bit = that player is LEFT of me;
      if (player.y - T.divealt < y) { t26 = 2; t27 = side; goto phase2; }   /* reached the player's altitude band */
    }
    /* weave: vx += ±$1000 per frame for |t27| frames, then the opposite for the same count, then a new random weave */
    if (t27 < 0) { vx += $1000; if (++t27 == 0) swap_in_t28_or_new_weave(); } else { vx -= $1000; if (--t27 == 0) ... }
  } else {                                                     /* PHASE 2: bank toward the player's side and pull UP */
  phase2:
    if (!(flags&2) && (frame == 4 /*heading right*/ || frame == 12 /*heading left*/)) { flags |= 2|0x20; fire at (x + (frame==4 ? 32 : -8), y+11); }
    if (|vx| < T.vmax) vx += (t27 & 1) ? -T.accel : +T.accel;   /* toward the side remembered in t27 bit0 */
    vy -= T.accel;                                             /* no lower bound: it climbs back off the top */
    frame = heading16(vx, vy);                                 /* LAB_9912, see type 8 */
  }
  if (flash && (--flash & 2)) frame += 16;                     /* lit frames */
  x32 += vx; y32 += vy;
  if (x <= g7204-$20 || x >= g7204+$120 || y >= $200 || y <= $E0) free;   /* leaves the 352x288 band */
  draw_frame(r);
}
```
(V: y +4/frame and t27 14→1→243 weave, then vy $40000 → -$1C000 in $2000 steps, frame 8→13, second shot at frame 12.)

### 4.5 Type $05 — pickup (LAB_8A8A) — created by LAB_9986, collected by LAB_369A (V: stage1 objlog, L)
```c
/* LAB_9986: re-initialise THIS record in place as a pickup (called from the explosion tail of the last type-0 of a wave
   with D7 = 10, and from the type-6 bomber's death with D7 = 0) */
void turn_into_pickup(Hostile *r, bool nova) {
  relx = (r->x - g7204 + 8) << 16;   y = r->y - $F4;          /* -> new y = old y + 12 */
  hostile_init(r, xp = $63 (99 + scroll, irrelevant: x is recomputed from relx), yp = y, type 5, p12 = relx);
  r->t28 = nova ? $0A : (rnd & 6);                            /* $0A = nova charge, 0/2/4/6 = weapon 0..3 */
  if (relx.hi < $88) r->p8 = +2.0 px/frame² else { r->t27 = ~t27 (= $FF = drifting left); r->p8 = -2.0 }
  goto $79F2 (processed again in this pass);
}
void type5(Hostile *r) {
  flags |= 0x80;                                              /* can't be shot */
  if (t27 < 0) { if (relx.hi < $47) t27 = ~t27;  if (p8 > -4.0) p8 -= $2000; }      /* drift accel ±2/16 px/frame² between screen x $47..$C9 */
  else         { if (relx.hi > $C9) t27 = ~t27;  if (p8 < +4.0) p8 += $2000; }
  y32 += g-28516 ? $6000 : $8000;  if (y >= $200) free;       /* sinks 0.5 px/frame (0.375 in demo) */
  relx += p8;  x = g7204 + relx.hi;                           /* screen-locked horizontally */
  if (t28 != $0A && p8 == 0) t28 = (t28 + 2) & 6;             /* weapon pickups cycle colour when the drift reverses */
  frame = t28 + ((g-28551 >> 2) & 1);                         /* 2-frame sparkle per kind (frames 0..7 weapons, 10/11 nova) */
  draw_frame(r);
}
/* LAB_369A (player touches a type 5): record freed; t28 == $0A: nova charges +1 (max 8), sound EXT_24738; else weapon
   index 59 = t28 >> 1, weapon level 60 ++ (max 5), sound EXT_2473E, shots cleared, new shot table (LAB_3722). (L) */
```
(V: stage1 objlog type 5: y0 445, b28 = 10, b27 = 255, |vx| 4.5.)

### 4.6 Type $06 — bomber (LAB_890C) — 1000 pts, hp 23, 3-shot spread; drops a weapon pickup (V: objlog, L)
Spawned by the wave list at a random x (LAB_7556: x = (rnd & $7F) + $40 + scroll), y = -44 → 212.
```c
void type6(Hostile *r) {
  if (frame >= 5) {                                           /* dying: frames 5..10 every 2 frames */
    if (!(g-28551 & 2) && ++frame >= 11) { turn_into_pickup(r, /*nova*/false); return; }   /* random WEAPON pickup */
    draw_frame(r); return;
  }
  frame = 0;
  if (damage) { damage=0; flash=4; hp -= dmg; if (hp<0) { flags |= 0x80; frame = 5; } }
  if (frame < 5 && flash) { flash--; frame = 4 - flash; }     /* frames 1..4 = hit flash */
  if (t27 == 0) new_weave() (same ±(rnd&$f) / t28 = -t27 scheme as type 4, but here the magnitude is 0..15);
  vx += (t27 < 0) ? +$1000 : -$1000;  t27 toward 0; when 0 swap in t28 (once), else keep 0 -> new random weave next frame;
  x32 += vx;
  if (t26 == 0) t26 = g-2198 (50 normal / 35 easy / 75 hard);  t26--;
  if (t26 == 16) fire at (x+11, y+40) aimed;
  if (t26 == 8)  fire at (x+2,  y+40) dir (-32, (gframe & $7F) | $40);        /* down-left */
  if (t26 == 0)  fire at (x+20, y+40) dir (+32, (gframe & $7F) | $40);        /* down-right */
  y32 += $14000 (1.25 px);  if (y >= $200) free;
  draw_frame(r);
}
```
(V: objlog y +1.25/frame, hp0 23, lifetime 239 frames = (512-213)/1.25. `runtime.c` frees the bomber at frame 11 instead
of turning it into a pickup — listing says LAB_9986 with D7 = 0.)

### 4.7 Type $07 — rising ground gun (LAB_87E4) — 500 pts, hp 5 (V: objlog; L for the terrain probe)
Spawned by the wave list and by object type 1 ($61BC: at (obj.x - scroll + 14, obj.y - 225), once per object).
```c
void type7(Hostile *r) {
  y += g7222; flags |= 0x08;  if (y >= $200) free;
  if (frame >= 3) { if (!(g-28551 & 2) && ++frame >= 11) free;  draw_frame; return; }   /* death frames 3..10 */
  if (t27 < $20) { t27++; h = t27; }                        /* grows one row per frame out of the ground (+51 = low byte of h) */
  frame = 0;
  /* terrain probe: playfield bitmap g7208, 5 planes $6000 apart, row stride $30: if the pixel-byte under (x-$FE, y-$101)
     is 0 or $FF in every plane the gun climbs 1 px (y--) and shows frame 1 on alternate frames */
  if (damage) { damage=0; flash=4; hp -= dmg; if (hp<0) { gfx = $10790; frame = 3; flags = (flags|0x80)&~0x20; } }
  if (frame < 3 && flash && (--flash & 1)) frame = 2;
  flags &= ~0x20;
  if (t27 >= $14) { if (t28 == 0) { t28 = g-2199 (50); fire at (x+12, y+10) aimed; }  t28--; }   /* fires every 50 frames once 20 rows are out */
  draw_frame(r);
}
```
(V: objlog b27 1→32, vy = scroll, lifetime ~200 = until y 512.)

### 4.8 Type $08 — homing missile (LAB_8664) — 750 pts, hp 2 (V: objlog; runtime.c agrees)
Spawned by the wave list (x random as type 1, p12 = vy = 1.0 px) and by objects ($6EA4: missile silo).
```c
void type8(Hostile *r) {
  if (damage) { damage=0; hp -= dmg; if (hp<0) { explode(r); return; } flash = 8; }
  if (t28) {                                                   /* HOMING for t28 = 200 frames (desc +27 = $C8) */
    if (--t28 == 0 && x <= $1B0) flags |= 1;                   /* remember: break off to the LEFT if on the left part */
    D7 = LAB_491C(x, y) direction bits; if (dist1 >= dist2) D7 >>= 2;   /* nearer player's bits: bit0 = it is left, bit1 = above */
  } else {                                                     /* LAUNCHED away: straight heading, leaves the screen */
    if (x <= g7204-$20 || x > g7204+$120) free;
    D7 = flags (bit0 = left);  if (((gframe & $FF) ^ $FF) + $100 < y) D7 |= 2;   /* climb if below a frame-dependent line */
  }
  vmax = g-1790 (2.5 px), acc = g-1786 ($2000):  vx += (D7&1) ? (vx >= -vmax ? -acc : 0) : (vx <= vmax ? +acc : 0);  same for vy with bit1;
  frame = heading16(vx, vy);                                   /* LAB_9912: 16 headings from -vy/(vx>>6) through thresholds $142,$60,$2B,$0D; +8 if vx<0 */
  if (t26 == 0) { t26 = $FF; t27 = 20; }                       /* first shot after 20 frames */
  if (--t27 == 0) { t27 = g-2197 (75); fire at (x+12, y+12) aimed; }
  if (t27 >= 8 || !(t27 & 1)) {                                /* exhaust: on most frames paint a smoke puff INTO the playfield (LAB_5B3E, gfx $1A780+frame*$300 / $577E) */
    if (flash && (--flash & 1) == 0) plain frame else smoke; } else plain frame;
  x32 += vx; y32 += vy;
  draw (smoke: LAB_9814 with +32/+36 = puff; plain: gfx $17500 + frame*stride)
}
```
(V: objlog lifetime 220-315, |v| 2, b28 199→0, flags bit0, ends off-side.)

### 4.9 Type $09 — stage boss (LAB_7FC4) — 5000 pts, hp 95 (V: stage1 objlog for existence/lifetime; internals L)
Two records: slot 8 (`$2DF00`) = body, slot 9 (`$2DF50`) = gun pod, both type 9; the pod's `flags` byte is `110(A4)`
seen from the body, the pod's hp is `104(A4)`, its frame `143(A4)`. Slot 11 (type $0A) carries the explosion puffs.
Two variants by `g7228`:
* **Stage 1 boss** (LAB_8398): body follows a 400-step path table `$2EF20` (4 bytes/step: dx.w, dy.w in 1/16 px; p12 = step
  index, wraps at $640; dx negated once progress >= 4000), descends 0.3 px/frame for its first 250 frames (t27). Only
  damageable when the pod's frame == 3 (pod destroyed); damage halved (min 1); hp<0 → frame 3, `flags|=$88` on body
  AND pod. Flash frames 2/1; idle frames 0/1 alternating on gframe bit5. Fires on a 48-frame (40 after progress 6000)
  cycle: phase 2 → fire at x+53, 20 → x+33, 8/14 → x+43 with random direction (dx rnd, dy = (rnd<<2)|$3F), all y+56.
  Pod (LAB_8526): at (body.x, body.y+32), gfx $31960, h $30; damage halved; hp<0 → frame 3, immune; puffs LAB_85E4(40,8,0).
* **Other stages** (default path $7FE2): body h=$30 w=9 (144 px), gfx $35CC0 mask / $2E4C0 + frame*$F00 frames by table
  `$797E[gframe>>3]` (0..5), flash gfx $33EC0, wreck $34DC0 (when hp < 16). Starts moving when gframe low byte == 0
  (flags bit2). Odd frames: horizontal speed ±0.5 px toward the living player chosen by gframe bit1, vertical bob from table
  `$799E[(gframe>>1)&$3F]` (1..16, mirrored) * $2000. Even frames: random wander: t27/t28 = signed random step counts
  (rnd & $8F | 3, forced toward the centre outside x $114..$1EC / y $F8..$128), x/y ± 0.75 px per frame.
  Fire (only while hp >= 16): at gframe&$7F == $78 → fire at (x+2 [+$6C if gframe bit7], y+$26) = drops a MINE (LAB_4704
  detects fire_y - y == $26: sound 56); between gframe $50 (sound 26) and $B0: every 16 frames at (x+$2C, y+$20) and
  (x+$48, y+$20) with random direction (dx signed rnd, dy = rnd | $40). Damage only once the pod is dead (`110(A4)` bit7),
  halved; hp<0 → `flags|=$88` on both; flash 6. Pod (LAB_82E0): at (body.x+29, body.y+48), h $27 w 5, gfx $37208 mask,
  $36BF0 / $35FC0 / flash $365D8, wreck at hp<16; LAB_85E4(56,8,12).
* Dead body: `g-1570 = $64` (boss-dead timer used by the stage-clear logic), scrolls down with g7222; at y >= $200 frees
  slots 9/10/11 and itself and clears g-1570.
* **LAB_85E4(D4,D5,D6)**: boss explosion puffs: if hp < 16 (dying) on alternate frames, or whenever `hp & 7` dropped below
  the pre-hit value's `hp & 7` (every 8 hp), write {x = (rnd & $3F) + D6 [- D5 if 16 <= x < 48], y = D4, count = 8} into the
  free one of the two puff slots `$790A` / `$7910`, set g-27618 = 8 (screen flash), sound 31.

### 4.10 Type $0A — boss explosion puff (LAB_7F56) (V: stage1 objlog; L)
`flags |= $88`. Slot 11 uses puff slot `$790A`, any other slot `$7910`. If the slot's count (word +4) is 0 → not drawn.
Else frame++ (reset to 0 when count == 8), count--; position = (`$2DF00`.x + slot.x, `$2DF00`.y + slot.y) i.e. relative to
the boss body (stage 3: the slot's own x/y, y += g7222). Gfx = explosion set $11090 (desc), 8 frames. Never freed by
itself (lifetime = boss's, V: 2737-3388 frames in stage 1).

### 4.11 Type $0B — flypast / decoration (LAB_7E12) — (L only, never in a capture)
Four records (slot index from the stack = 3,2,1,0 for slots 8..11) flying RIGHT at 4 px/frame once `t28` (incremented on
alternate frames via the global toggle `LAB_1079` bit1) reaches $B4 or x > scroll+$3C; freed when x > scroll+$120.
While `g-28550 < 2500`: slot 8 (D2=3): plane gfx $1A500 frame (gframe>>2)&$F; slot 9 (D2=2): missile gfx $17500 frame
((gframe>>2)+5)&$F plus a smoke puff via LAB_5B3E; slot 10 (D2=1): frame table `$795E[(gframe>>1)&$1F]`, gfx $20500;
slot 11 (D2=0): gfx $17500 frame ((gframe>>2)+10)&$F. After 2500 frames: slot 11 → gfx $14450 (bomber) h=$2C, slot 10 →
$43700 2-frame, slot 9 → $36000. Looks like the attract/ending "squadron flies home" sequence; who spawns it was not found
(no LAB_75E4 caller with D3 = $0B in loader.asm — probably LODGAM/another overlay).

### 4.12 Type $0C — tank (LAB_7CF0) — 2500 pts, hp 35, 80x50 (V: stage1 objlog; L)
```c
void typeC(Hostile *r) {
  if (y >= $200) free;  y += g7222;
  frame = ((g-28551 >> 3) & 3); if (frame == 3) frame = 1;     /* 0,1,2,1 track animation */
  if (t28) { frame = 5; if (t28 < 12) { t28++; frame = 4; }  draw_frame; return; }   /* dead: 12 frames of frame 4 then wreck frame 5 forever */
  if (damage) { damage=0; hp -= dmg; if (hp<0) { flags |= $88; t28 = 1; goto dead; }  y--; if (!flash) flash = 8; }
  if (flash) { flash--; frame = 2 + ((flash & 2) != 0); }
  if (!(g-28551 & 2)) y++;                                     /* creeps down 1 px every other frame */
  target = the player with the SMALLER y (p1 unless p2.y < p1.y);  if (target.x - 16 < x) { if (vx > -1.5) vx -= $1000; } else { if (vx < 1.5) vx += $1000; }
  x32 += vx;                                                   /* p12 = vx */
  if (--t27 == 0) { t27 = 20; fire at (x+29, y+40) aimed; }    /* every 20 frames */
  draw_frame(r);
}
```
(V: stage1 objlog 8 tanks, hp0 35, |vx| 1, vy 2, end off-bottom.)

### 4.13 Type $0D — pop-up drone (LAB_7A9A) — hp 1, no score (V: stage2 objlog 226 of them; L)
Spawned by objects ($6A82/$6AC8 silo: p12 = $10000, then t28 = 1 and optionally t27 = $FF → "launched" immediately,
alternating sides) and by the wave list (t28 = 0 → "emerging" script mode, script at p12).
```c
void typeD(Hostile *r) {
  if (t28 == 0) {                                              /* EMERGING (LAB_7BF6): flags |= $88, same 4-byte script as type 3 */
    fetch step when dur == 0 (zero word -> free);  phase += phase_step;  h = phase & $1F;
    plane = $37340, mask = $373E0 (+$C0 when dur < 0); if (phase bit7 && dur >= 0) show only the bottom rows (gfx += (16-h)*2);
    if (dur < 0) x += step else y += step;  y += g7222;  dur toward 0;  if (y >= $200) free;  if (h == 0) skip draw;
    if (dur < 0 && h != 16) the 16 mask words are copied to $76E8 ANDed with a (16-h)-bit shift mask (horizontal reveal);
    draw; return;
  }
  if (y <= $F0) free;  y += g7222;
  if (t28 < 16) { t28++; h = t28; gfx offset (16 - t28)*2 + $180 (bottom rows: rising out of the launcher);
                  x32 += $4000 (+0.25) or -$4000 if t27 < 0;  draw(LAB_981C); return; }
  h = 16;  t28++;
  if (t28 < 25) vy += $1000; else if (vy > -4.0) vy -= $2000;  /* p12 = vy: sinks a little, then climbs away at up to 4 px/frame */
  if (frame >= 7) { if (g-28551 & 2 && ++frame >= 13) free; draw_frame; return; }   /* death frames 7..12 */
  if (vy == 0 || vy == -3.875) fire at (x+4, y+8) aimed;       /* LAB_7BDC, twice per life */
  frame = 2 + triangle((g-28551 >> 1) & 7) (2..5);
  if (damage) { damage=0; hp -= dmg; if (hp<0) { frame = 7; draw_frame; return; } flash = 4; }
  if (flash && (--flash & 1)) frame = 6;
  y32 += vy;  x32 += $8000 (+0.5) or -$8000 (t27 < 0);
  draw_frame(r);
}
```
(V: stage2 objlog: l12 $10000 at creation, b28 2, |vy| 3, most end "vanished" = freed at y <= $F0 after climbing.)

## 5. LAB_9912 heading16 (shared by types 4, 8) (V: runtime.c parity; L)
`q = (-vy) / max(vx >> 6 as int16, or $40 if 0)`; frame = q >= $142 ? 0 : >= $60 ? 1 : >= $2B ? 2 : >= $0D ? 3 : >= -13 ? 4
: >= -43 ? 5 : >= -96 ? 6 : >= -322 ? 7 : 8; if vx < 0 frame += 8; & $F. (0 = up, 4 = right, 8 = down, 12 = left.)

## 6. Collision side (what the other routines do with these fields)
* LAB_3544 (player shots vs hostiles): box overlap, `explode == 0`, `!(flags & 0x80)`; adds the shot's damage to +62
  (laser: +2 and the shot survives); if `hp - damage < 0` → LAB_35CE (kill sound 28; type 2: 29/20) else hit sound = +25
  (LAB_35A4, D6 = g-19208 = $5000 → 50 pts per hit) then LAB_40BE adds the points. Type 9 with frame 5 is exempt.
* LAB_35F0 (player vs hostiles): box overlap, `explode == 0`, `!(flags & 0x08)`; type 5 → LAB_369A (collect); type 6 with
  frame >= 5 ignored; otherwise the hostile takes 10 damage (+62 += 10) and the player dies (unless invulnerable +52).
* LAB_4704 consumes flags bit5 (section 3).

## 7. Sound triggers inside the hostile code (JSR EXT_2470E, D0)
26 (boss starts its firing burst, $81DC), 31 (boss explosion puff, LAB_85E4), 56 (mine dropped, LAB_4704), 28/29/20 kill
(LAB_35CE), per-type hit sound = descriptor byte +13 (rec +25) via LAB_35A4 ($3B/$32/$35/$33/$34), pickup: EXT_24738
(nova) / EXT_2473E (weapon) in LAB_369A.

## 8. What runtime.c gets wrong vs the listing (for the porter)
* type 4 (LAB_8B48): runtime.c's branch is a different algorithm (see 4.4); the capture matches the listing.
* type 6 death: listing converts the record into a random weapon pickup (LAB_9986, D7 = 0); runtime.c frees it.
* Types 2, 9, A, B, C, D are not translated in runtime.c at all (it reports "untranslated live projectile type").
