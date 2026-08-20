# Porting a Battle Squadron handler to C (route B: semantic rewrite, no 68k)

You translate one TYPE handler (a branch of LAB_79E2 for hostiles or of LAB_5F34 for objects) or one verb
(collision, scheduler, player...) into C against `src/engine/engine.h`. Read in this order: `re/ENGINE.md`
(shape + timing), the per-area spec for your part (`re/ENGINE_hostiles.md` / `re/ENGINE_objects.md` /
`re/ENGINE_frame_player.md` — they already contain C pseudocode per routine with (V)/(L) marks), `re/handlers.txt`
(your type's address), `re/stats/*.txt` (measured lifetimes/activation for your type), `re/waves_level1.txt`
(scripts). Listing: `~/BattleSquadron-Amiga/asm/loader.asm` (`;xxxxxx:` comments = address). Parity-pinned
translation you may crib from (register soup, but correct where it was parity-tested — see ENGINE_hostiles.md §8
for where it is WRONG): `~/BattleSquadron-Amiga/src/recomp/runtime.c`.

## Rules
* One C function per type: `void h_type_NN(Hostile *h)` / `void o_type_NN(Object *o)` in
  `src/behaviours/hostiles.c` / `objects.c`, registered in the tables `hostile_handlers[]` / `object_handlers[]`.
  It is called ONCE per game frame for each live record of that type, in the 4-pass order (engine's job), AFTER
  the engine handled the shared prologue (done-flag, explosion countdown `+29`, per-frame damage `+62`/`+24`
  application, death → explosion) — check ENGINE_hostiles.md §2 / ENGINE_objects.md §2.7 for what the shared part
  does and do not repeat it.
* Keep the ORIGINAL field names/offsets (`h->hp`, `h->t27`, `h->flags`, `o->f25`...): the objlog diff is per field.
  No new state unless the original had it (then it is a field you missed — find it).
* Frame parity: "alternate frame" tests are `g.dframe & 2` (= g-28551 bit1, toggles per game frame) or `& 1`
  (per display frame); never invent timers in seconds.
* Fixed point: hostile/effect x,y,vx,vy are 16.16 (`h->x += h->vx`); `x.w` writes set the integer part and keep the
  fraction unless the original writes the long. Objects/shots are integer.
* Spawning: `hostile_alloc(relx, rely, type, p12)` (p12 = uint32: script chip address or velocity long) takes SCREEN-relative x (added to g.cam7204 unless ≥ $320)
  and y − 256, exactly like LAB_75E4; returns NULL when the pool is full or `g.no_ship && !demo`. Effects via the
  FIRE REQUEST: set `h->flags |= FIRE_REQUEST; h->fire_x/fire_y = ...` (+ the g-14390/-14388 fixed-direction words
  or the "stationary" word when the original does) — do NOT spawn bullets yourself; `effects_from_requests()` runs
  later in the frame (LAB_4704) and caps at the option's bullet limit.
* RNG: `rng()` = the $2B1E table byte; call it exactly as often as the original does (order matters for parity).
* Sounds: `sfx(n)` with the ORIGINAL number (see re/sfx_triggers.txt; descriptor byte +13 / template +46 for hits).
* Score: `award(points_bcd)` goes to the SELECTED player of that frame (the engine knows which).
* Keep a `/* LAB_xxxx @ $addr */` comment per function and per non-obvious block. Do NOT invent behaviour: port
  literally and flag in your report anything you had to guess (especially types marked (L) in handlers.txt).

## Frozen contract (session 2 — engine.h is authoritative)
* Chip data: `bs_chip` + `cb/cw/cl` read the 512 KiB image; record "pointers" (script +12, gfx +32/+36,
  weapon table +24) are numeric chip addresses (objlog l12/l36 parity).  Hostile `script` is the native
  pointer view — keep `p12` in sync when you advance it.
* Hostile draw: `hostile_draw(h, planes, mask)` (LAB_981C: box refresh + render + bit4 clip); LAB_97F8
  frame addressing is done by the handler (see hostiles.c draw_frame()).  `hostile_reprocess(h)` = the
  BRA LAB_79F2 re-entry (driver re-runs the record through half/pass/done checks).
* Objects: engine does the $5F44 prologue (clear bit5, live test, BCHG bit3) then `object_handlers[t](o)`;
  the handler does `y += g.scrolled7222`, shared part `object_shared_2x()` for types >= $20, and ends in
  `object_tail(o)` / plain return / `o->x = 0`.
* Shared globals: g_14390/g_14388 (forced bullet direction), g_27618 (palette flash), g_8414/g_8413
  (turret turn/reload), g_8397 (gate counter), g_790A[2][3] (explosion-puff slots).  Fire periods come
  from g.armour[]: -2200..-2194 = armour[0..6].

## Build & check
`make build/simrun && ./build/simrun STAGE FRAMES OUT.txt [--fire] [--invuln]` writes the native objlog in the host's
`--objlog` format (same line types G/P/S/H/O/E, same columns). `python3 tools/parity.py re/trace/objlog_X.txt OUT.txt
[--pool H|O|E] [--type NN]` aligns by progress (g7206) and diffs per-type live counts, first divergence and
trajectories. Until simrun exists (session 2), compile-check against engine.h and compare your numbers with
`re/stats/*` by hand.
