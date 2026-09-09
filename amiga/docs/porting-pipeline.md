# Porting a WHDLoad game to native C

`tools/recomp_studio.py` drives a WHDLoad title from its shipped install to a
native port that links no 68000 emulator.  Musashi is a development oracle
only; the shipping target is the translated C in `src/recomp/`.

    python3 tools/recomp_studio.py                                  # GUI
    python3 tools/recomp_studio.py --project GAME.json --list
    python3 tools/recomp_studio.py --project GAME.json --stage discover
    python3 tools/recomp_studio.py --project GAME.json --run-all
    python3 tools/recomp_studio.py --project GAME.json --summary
    python3 tools/recomp_studio.py --new-from /path/to/WHDLoadGame --out GAME.json

Exit codes from `--run-all`: `0` everything ran, `1` a stage failed, `2` nothing
failed but stages were skipped for want of configuration.

## How much of this actually generalises

Battle Squadron was the first title through.  Hybris (Cope-com, the same
authors, the same genre, the same era) was the second, on 2026-08-16, and it is
the measurement that matters — most of the pipeline turned out to be about
*Battle Squadron*, not about WHDLoad.

| Stage | Generic? | Notes |
|---|---|---|
| `ingest` | **yes** | Every WHDLoad slave is an AmigaDOS hunk file. Read Hybris's header and code first try. |
| `modules` | **no** | Needs a `LOADER` descriptor table + BOND-packed overlays. |
| `discover` | **yes** | Capstone recursive descent from entry points you supply. 655 instructions out of Hybris's music module first try. |
| `assemble` | per-title | A script that reassembles *this* title's overlays. |
| `verify` | per-title | A script that cmp's *this* title's rebuilt overlays. |
| `oracle` | per-title | Needs a Musashi host for this game and its own deterministic milestone. |
| `unit-test`, `integration-test`, `recomp-test`, `gate` | per-title | Make targets and a gate binary belonging to this port. |
| `translate` | manual | The 68000 → C work. Records `manual`, never `done`. |
| `parity` | per-title | Needs the oracle plus a state dumper for this game. |

So **two stages of twelve are genuinely portable**.  That is worth knowing up
front: a new title is mostly new work, and the value of the app is the
*discipline* — fail closed, diff against an oracle, gate the emulator out — not
the automation.

### The `overlay_layout` finding

Battle Squadron's `data/` holds a `LOADER` whose descriptor table names
BOND-packed overlays.  **That is a Cope-com title layout, not a WHDLoad
convention.** Hybris — same authors, earlier game — ships `data/03`…`data/18`
plus `main.pal` and has no `LOADER` at all.

`overlay_layout` records this: `loader-bond` for the Battle Squadron shape,
`none` otherwise.  `modules` refuses to run unless the title declares
`loader-bond` *and* a `LOADER` is really present.  Do not assume the next game
looks like either of these two.

## The rule that this document exists for

**A stage that a project does not configure reports `unsupported`.  It never
falls back to another title's script, make target or binary.**

The first Hybris run reported `done` for `assemble`, `verify`, `unit-test` and
`integration-test`.  All four had ignored the project file entirely and run
Battle Squadron's `./regen_asm.sh`, `./verify.sh`, `make unit-test` and
`make integration-test`.  The `verify` stage cheerfully printed

    OK: LOADER byte-exact (67584 bytes)

for a game that was not being ported.  `translate` also reported `done` while
doing nothing at all, and the `--new-from` skeleton filled a brand new project
in with `build/battle_squadron_native` as its oracle and `build/test_recomp_boot`
as its gate — so a new title would have shown a mostly-green pipeline having
achieved nothing.

Every per-title command now lives in the project JSON and is fetched through
`need()`, which raises `StageUnsupported` when it is missing.  `--run-all`
always prints a summary, and `unsupported` is called out in it explicitly.

## Project JSON

    {
      "name": "Battle Squadron",
      "root": "/home/jon/BattleSquadron-Amiga",

      "raw_slave":  "...the shipped .slave",
      "slave_out":  "original/slave.bin",
      "data_dir":   "...the WHDLoad data/ directory",

      "overlay_layout": "loader-bond",     // or "none"
      "modules_out": "original/modules",
      "manifest":    "docs/module-map.json",

      "discovery": { "binary": "...", "base": "0x100",
                     "entries": ["0x400", "..."], "output": "build/loader.ira" },

      "scripts":      { "assemble": "./regen_asm.sh", "verify": "./verify.sh" },
      "make_targets": { "unit_test": "unit-test", "integration_test": "...",
                        "recomp_test": "recomp-test",
                        "recomp_dump": "build/recomp_dump" },

      "oracle": { "make_target": "...", "binary": "...", "frames": "50000",
                  "expect_files": "20", "expect_blits": "2054047" },
      "parity": { "native_frames": "6000", "game_frames": "600" },
      "build_prefix": "build/parity",
      "gate_binary":  "build/test_recomp_boot"
    }

Leave a per-title field empty until you have the real thing.  Empty is honest;
a borrowed value is not.

## Starting a new title

1. `--new-from <game dir>` writes a skeleton.  It fills in only what it can see
   on disk and leaves every per-title command empty.
2. Run `ingest` and `discover`.  These work immediately.  For `discover` you
   need a load base and entry points — a module beginning with a `4EF9` jump
   table hands you both (Hybris's `data/04` opens with seven `JMP`s and the
   string `"<P>lay <F>ade <Q>uit"`, i.e. its music replayer at `$3C000`).
3. Everything else is real porting work, in this order: an oracle you can
   diff against, then translation, then parity, then the no-emulator gate.


## Measured against a finished flow

Battle Squadron now runs its whole loop: boot, title, play, stage clear, level
advance, death, game over, initials entry, restart.  Setting Hybris beside it
after both went through this pipeline is the honest measure of what a port
costs.

| | Battle Squadron | Hybris |
|---|---|---|
| Slave ingest | done | **done** (generic) |
| Module extraction | LOADER + BOND overlays | no LOADER; 16 files, format unknown |
| Code discovery | done | only the 2 modules that publish a `JMP` table |
| Load addresses known | all | **2 of 16**, both music replayers |
| Musashi oracle | done | none |
| Byte-exact reassembly | done | none |
| Translated runtime | **7,684 lines, 231 dispatcher PCs** | 0 |
| Parity harness | done | needs an oracle first |
| Renderer | pixel-accurate (0.0–0.5%) | none |
| Sound + music | done | none |
| Game flow | complete loop | none |

The front half of the pipeline is generic and Hybris passes it.  Everything
that makes a playable port is the translation, and that is 7,684 lines of
hand-written C for one title, pinned by 217 assertions and 9 remaining
fail-closed edges.

Worse for Hybris specifically: **the two modules whose load addresses are known
are both music replayers** (`data/04` at `$3C084`, `data/07` at `$3AFB8`, both
confirmed against the slave's `resload_Patch` calls).  Its main game code is in
one of the other fourteen files, none of which publishes a jump table, and the
slave's file table gives names but not destinations -- the caller supplies
those.  So Hybris does not yet have a starting address for the code that
matters.

The realistic next step for any second title is therefore not more pipeline: it
is an oracle.  Without something to diff against, translation has no ratchet,
and the parity loop that found every real Battle Squadron bug cannot run.

## Read the slave first — it is the map

The WHDLoad slave is not just a launcher.  It contains the title's memory
layout, written down, and reading it is the cheapest hour of a new port.
Hybris's slave (10,884 bytes after `ingest`) gave up all of this:

* **The file table**, at slave offset `$0B1A` — sixteen 4-byte entries of
  `[id byte]["NN"][NUL]` covering `data/03`…`data/18`.  The loader at `$0AD2`
  matches the caller's `D1` against the id byte, then calls
  `resload_LoadFileDecrunch` at `$1C(A2)`.  Note the **destination is passed in
  by the game, not held in the table** — so the table alone does not tell you
  where a module lands.
* **Load addresses**, named outright as comparisons.  Immediately after the
  load:

        $0AF4: lea.l   $b5a(pc),a0        ; patch list
        $0AF8: cmpa.l  #$3c084,a3         ; ...if it went to $3C084
        $0AFE: beq.b   $b12
        $0B00: lea.l   $b86(pc),a0        ; a different patch list
        $0B04: cmpa.l  #$3afb8,a3         ; ...or to $3AFB8
        $0B12: movea.l a3,a1
        $0B14: jsr     $64(a2)            ; resload_Patch

  So `$3C084` and `$3AFB8` are real destinations, for free.
* **The patch lists** themselves, as 6-byte `[cmd][offset][data]` entries —
  e.g. `$8007 $345E $4270`, writing `CLR.W D0` at module offset `$345E`.  Those
  offsets are the hardware accesses the original port had to neutralise, which
  is a ready-made list of the places a native port will also have to handle.
* **Config strings**, which describe the title's own options — Hybris's slave
  advertises `Screen mode:Auto,PAL,NTSC`, a slow-down toggle and a cheat mode.

## Finding a module's load address

`tools/find_load_base.py` recovers where a module sits in memory.  Point it at
the file; if the module opens with a `JMP abs.l` table it takes its entry
points from that, otherwise pass `--entry`.

    python3 tools/find_load_base.py .../data/04
    → best: $03c084

    python3 tools/find_load_base.py original/whdload/BattleSquadron/data/LOADER \
        --entry 0x400 --entry 0x150 ... → best: $000100   (the known answer)

### Coverage is not correctness

This tool exists because of a trap that cost real time.  Running Hybris's
`data/04` through `discover` at a **guessed** `$3C000` reported

    04: 655 instructions, 2454 code bytes, 18 ranges (15.1%)

and at the **correct** `$3C084`, confirmed twice over, reports

    04: 218 instructions, 890 code bytes, 9 ranges (5.5%)

The wrong base scores nearly three times better.  68000 decodes almost any
byte stream, so "how far the walk got" measures nothing; the wrong base was
producing runs of `ori.b #$xx,d3` — the signature of misreading the `$00` bytes
of address longwords as opcodes — and counting them as coverage.

**Never accept a load base because discovery liked it.**  What discriminates is
agreement between two independent facts:

* *Boundary consistency* — disassembling from one entry point towards the next
  must arrive exactly on it, not sail through it mid-instruction.
* *Control-flow cross-references* — at `$3C084` the routine at `$3EB14` opens
  `bsr.w $3f480`, landing precisely on another published entry.  At `$3C000`
  four of the seven entries decoded to nonsense.

`find_load_base.py` scores those and ignores instruction counts entirely.  An
earlier version of it did count them, and duly elected a base 1,946 bytes too
low.  It warns when the margin is thin — confirm against the slave, which as
above usually names the address outright.

## What the parity stage is for

`tools/parity_diff.py` aligns the native oracle's state dump against the
recompilation's at 2:1 (the game logic runs at 25 Hz, the display at 50 Hz) and
reports the first divergent record.  It is the single most productive tool in
the repo — three Battle Squadron bugs derived by reading the disassembly changed
nothing on screen, and the real ones fell out in one diff.

Two traps, both paid for in hours:

* **Sample at the right point in the frame.** Battle Squadron's dumper must
  sample at `$BCE` — after the passes that move every record, before the
  scheduler that creates new ones.  Sampling at the top of the loop made every
  freshly created record look one frame behind and buried the real bugs. If a
  lag looks universal, move the sample point before you believe it.
* **Compare like-for-like windows.** A 6000-frame native log against a
  600-game-frame recomp dump covers 1200 native frames.  Battle Squadron's
  scenery objects do not appear until native frame 2669, so an empty pool in the
  short dump is not evidence of a broken spawner.
