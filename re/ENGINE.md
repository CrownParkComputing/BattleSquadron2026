# Battle Squadron (Amiga, Cope-com 1989) — engine architecture (measured, 2026-08-19)

This is the top-level spec; the per-area specs carry the C pseudocode, every (V)/(L) mark and the field tables:
* `ENGINE_frame_player.md` — main loop & timing, globals table, scroll/map consumption, stage descriptors, player,
  shots, nova, effect pool, score/lives/game-over/initials, messages. (the ARCH + player part)
* `ENGINE_hostiles.md` — hostile pool ($2DC80): allocator, descriptors, 4-pass driver, all 14 type handlers, fire
  requests, heading16, sound. (the VERBS/handlers part for enemies) + `waves_level1.txt` (wave lists + script language).
* `ENGINE_objects.md` — object pool ($2E040): tile-triggered spawner, 27 templates, all object types, collision,
  damage, score, pickups. (the OBJECT.md part for ground objects + collision verbs)
* `ASSETS.md`, `sfx_triggers.txt` — data formats and the decoder plan; `handlers.txt` — type → handler table;
  `stats/*.txt` — measured activation/lifetime per type; `BRIEF.md` — how the captures were made (NB: the BRIEF has
  LAB_3424/34FA swapped and calls -28552 a game-frame counter; the ENGINE_* files are right: 3424 = shots vs objects,
  34FA = shots vs hostiles, -28552 = DISPLAY frames, +2 per game frame).

## 1. Shape of the engine (what the port has to be)
Battle Squadron is a **fixed-pool, type-switch state machine**, not a task kernel (unlike SWIV):
* Four record pools, all statically placed, scanned linearly every game frame, free slot = x word == 0:
  **hostiles** 12 × 80 B ($2DC80: planes, drones, bullets-drones, bosses, pickups), **objects** 18 × 64 B ($2E040:
  ground scenery, turrets, hangars, the stage gate), **effects** 16 × 20 B ($4976: enemy bullets, homing missiles,
  nova sparks; compacted, y-sorted), **player shots** 12 × 12 B per player (inside the 266-byte player records
  $4E3C/$4F46). Records hold x/y in one "game space": X = 256 + map column (0..384), Y = 256 + screen row; screen x
  = X − g7204 (camera, 256..352), screen y = Y − 256. Hostiles/effects carry 16.16 fractions; objects/shots integers.
* Behaviour is selected by a TYPE byte: hostile `+31` ($00..$0D, CMPI chain in LAB_79E2), object `+17` ($00..$05,
  $20..$29 in LAB_5F34). Each handler is a per-frame step over the record's own counters (no stacks, no coroutines).
  The type-0 plane follows a **4-byte velocity-command script** ({dur, dir|scale, turn, turn-timer}, $FF,0,ptr chain,
  $FF,1 end; vector table $CCF2 32 × (vx,vy)); types 3/$0D use a rise/sit/sink script; type 9 (boss) has a path table.
* Creation: (a) the **wave scheduler** LAB_7556 walks a per-stage list of 12-byte entries {trigger progress, x, y,
  type, pad, script} and allocates a hostile when progress ≥ trigger (types 6/8/1 get randomised x/y; $FFFF = wipe
  the pool); (b) the **tile-triggered spawner** LAB_3078 scans the 24 words of each newly exposed 16-px map row and
  allocates an object template for magic tile words (table per stage mode 7228); plus the 3 hangar gates at progress
  $F10/$1490/$1DD0; (c) hostiles/objects spawn further hostiles via LAB_75E4 (launch pad → type 7, silo → $0D,
  invisible spawner → 8, bomber death → pickup 5, last plane of a wave → nova pickup); (d) enemy fire = a FIRE REQUEST
  (flags bit5 + origin +58/+60) that LAB_4704 turns into an effect-pool bullet later in the same frame.
* Death: hostile `hp +24` goes negative → `+29` explosion countdown (8 steps on alternate frames, explosion gfx), score
  BCD at `+54` → LAB_40BE into the selected player's 8 ASCII digits; objects: `+28` health → death frames f19..f33,
  type-$20 wrecks become bonus pickups (+97 BCD). Pickups (hostile type 5, `+28` subtype: $0A nova charge, 0/2/4/6
  weapon) are collected by LAB_35F0; weapon level `+60` 0..5 (type `+58` fixed by the title option).

## 2. Frame & timing (see ENGINE_frame_player.md §1 for the exact call order with raster windows)
One main-loop iteration = **one game frame = two PAL display frames (25 Hz)**; `g-28552` counts DISPLAY frames
(+2/iteration, low byte `g-28551`: bit0 = odd display frame, bit1 = toggles per game frame, bit2 = per 2 game frames).
Per game frame, in order: scroll (LAB_9C44) → object pool (LAB_5F34) → render restore/draw upper half → hostile
passes A,B for y ≤ $146 → HUD → restore/draw lower half → hostile passes A,B for y > $146 (pass A = types 3,7,$0D only,
pass B the rest; every record exactly once, `+30` bit6) → [objlog sample point $BE8] → display frame++ → input
(LAB_9A9E) → fire (LAB_3F44) → {ship move LAB_5050, shots+ship sprites LAB_51EA, nova LAB_1D0C, effects LAB_4ADA}
(1st) → messages → collision for the SELECTED player (alternates each game frame): shots vs objects (3424), shots vs
hostiles (34FA), player vs effects (3748), player vs hostiles/pickups (35F0) → HUD/score (44D0), game over (410A),
palette flash (1420), colour cycle (1FAE), fire requests → effects (4704), cheat (2088) → display frame++ →
{5050,51EA,1D0C,4ADA} (2nd) → wave scheduler (7556) → tile spawner (3078) → extra life (139C) → stage clear (7002).
Native: one `eng_frame()` doing exactly that; the "twice" routines are called twice (ship moves 2 px × 2, shots and
effects integrate twice). The raster waits are dropped; the half split only fixes the processing ORDER of the hostile
pool (keep it: slot allocation and fire-request ordering depend on it).

## 3. Record layouts (verified offsets)
* Hostile (80 B): ENGINE_hostiles.md §0 struct + descriptor table §0.1 (`$CD7A + type*$20`: h, w words, box, hp, hit
  sound, gfx mask/planes, score BCD, +26/+27 preload, +28 box dx/w).
* Object (64 B): ENGINE_objects.md §1.1 template table (27 templates, $2B68 + t*48 → record bytes 6..47) and §2.
* Effect (20 B), player (266 B), shot (12 B), stage descriptor ($14EA + stage*$8C: wave list, repeat list, module,
  palette, flash palette): ENGINE_frame_player.md §3–§7.

## 4. Verbs (C names proposed in src/engine/engine.h)
scroll_frame (9C44) · object_spawner (3078) / object_alloc ($32F4) · object_update (5F34 per type) · wave_scheduler
(7556) · hostile_alloc/hostile_init (75E4/7608) · hostile_update (79E2 per type) · hostile_explode (98E6) ·
fire_request → effects_from_requests (4704/47D2, aim 491C) · effects_update (4ADA) · read_input (9A9E) · fire (3F54)
· move_ship (5050) · ship_and_shots (51EA/5374) · nova (1D0C) · collide_* (3424/34FA/3748/35F0) · kill (35CE) ·
award_score (40BE) · pickup (369A) · hit_player · extra_life (139C) · game_over (410A) · stage_clear (7002/7180) ·
palette_flash (1420) · messages (A30E) · rng ($2B1E: table byte, self-incrementing index) · sfx (EXT_2470E, D0).

## 5. Player, weapons, specials (ENGINE_frame_player.md §4–§6)
Ship moves 2 px per display frame (4/game frame), camera x = 256 + 3/8·(ship x − 256) (pans 1 px/frame). Fire: volley
bank of 12 shot slots, weapon table $2024 = 4 weapon types × 6 levels (type = title option `+58`, level `+60` raised
by pickups, HUD colour `+59`); shots move twice per game frame. Death: `+49` = $46 timer, `+52` invulnerability
$270F then 300 on respawn, lives `+56`, extra life at 100k/300k/600k/every 1M. Nova: `+66` charges ≤ 8, 4-direction
joystick gesture or bit5, 255-frame ring of sparks (effect type 16) — (L) only, never captured.

## 6. Scroll / map / palette (ENGINE_frame_player.md §3, ASSETS.md)
Map = 512 rows × 24 words read BACKWARDS from $4A000 (row 0 = level end); each word = offset/2 into the tile strip
(160-byte 16×16 tiles, 5 planes); progress g7206 +1 px per game frame, a new row every 16; stage 0 loops (progress
8192 → 1, repeat wave list); stages 1–3 end at their boss (scroll stops when a boss sets g-1570). Palette 32 × RGB12
per stage (+12 of the descriptor) with a flash palette (+76) and extra blocks for stage 3 / hub after inner stages.

## 7. What is measured vs. listing-only
(V) across the three specs: call order/cadence, input, ship Δx, camera, scroll, progress wrap, shot spawn/motion,
effect speeds, spawner maths (x=(23−col)·16+$100, y=$100−h, hp reduction when one player), template copies, type
$20 turret aim/fire, object hit/kill/score path, vector-scale formula, type 1/4/6/7/8/C/D kinematics, 4-pass selection,
descriptor/hp values, stage descriptors, boss existence (stage 1). (L) only: hostile $02 mothership, $0B flypast,
object $04/$05/$28, nova, stage clear/advance, game over/initials, final-boss loop (LODFIN), two-player camera,
mouse mode, cheat. See each file's verification section.
