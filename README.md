# Battle Squadron 2026

**Battle Squadron** (Innerprise / Cope-com, 1989) rebuilt as a native program — no
emulator, no 68000 core, no disk image. The game's logic, wave scheduler, collision
and sound drivers were read out of the original and rewritten in C; the picture is
drawn by a native renderer and the audio synthesised on a native Paula.

This is the combined repository for the native game and the Amiga project.
The native desktop/Android build stays at the root; the complete former
`BattleSquadron-Amiga` repository and its Git history are retained under
[`amiga/`](amiga/README.md).

## What is here

| Path | |
| --- | --- |
| `src/engine/` | the game: frame loop, object pools, wave scheduler, collision, scoring |
| `src/behaviours/` | per-type behaviour for the 14 hostile and 16 object types |
| `src/bsdata.c` | container/depacker for the original modules (maps, tiles, sprites, fonts) |
| `src/render.c` | 288×255 frame renderer |
| `src/audio.c` | the game's two sequencers on a native Paula |
| `src/viewer.c` | front end: title, options, pause, high scores, and the debug viewers |
| `tools/` | parity harness, decoders, the reference dumps used while building |
| `amiga/` | original Amiga assembly, build verification, reference runner and recovered data |
| `re/` | the write-up: engine notes, porting guide, asset formats |

## Building

```sh
make            # needs raylib and a C compiler
./build/bsview
```

Run from the repository root. The default data path is now
`amiga/original/whdload/BattleSquadron/data`, retained from the imported Amiga
repository. Use `./build/bsview --data /path/to/data` to choose another install.

The Amiga assembly can still be rebuilt and compared against the original:

```sh
make amiga-verify  # requires vasmm68k_mot
make test          # native data checks and audio/sublevel regressions
```

The old capture-based parity checks additionally need the untracked `re/trace/`
reference files. See [the Amiga README](amiga/README.md) for its reference-runner
and Android build targets.

## Controls

| | Player one | Player two | Pad |
| --- | --- | --- | --- |
| Move | arrows | WASD | stick / d-pad |
| Fire | Space / Ctrl / Enter | Alt / C | A |
| Smart bomb | X / Shift | V / Tab | B |
| Pause | P / Esc | — | START |

Player two joins at any time by pressing fire.

Menus use D-pad/stick Up/Down to select, A to activate and B to go back.
In the centred debug viewers, LB/RB scroll the map or sound list; mouse controls
remain available. Options use Left/Right to change a value.

**Continues** in Options offers Off, 3, 5, 7 or Unlimited (the default). Each
player receives their own allowance at the start of a new game. A continue
spends one allowance; ordinary respawns do not. Changes apply to the next game.

Live autofire runs at twice the original cadence for both players. The recorded
attract demo retains its original timing. Graphics in Options switches between
Original and Enhanced: smoother terrain scrolling, subtle surface parallax,
ship shadows, blue engine exhaust and local explosion lighting. Enhanced is
the default; the setting only changes rendering.

## Android

The Android app is built from the same fully native C engine, renderer and
Paula audio implementation as the desktop executable.  It does not include an
emulator or a 68000 core.

Install Android SDK 36, NDK 26.1.10909125 and CMake 3.25 or newer, and keep a
raylib source checkout beside this repository (or set `ANDROID_RAYLIB`).  Then
point `ANDROID_DATA` at a Battle Squadron WHDLoad data directory:

```sh
ANDROID_DATA="$PWD/amiga/original/whdload/BattleSquadron/data" make android-debug
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
```

The original modules are staged directly into the local APK build and remain
local to the APK build.  The resulting app is landscape-only and supports Android
gamepads: d-pad/stick moves, A fires, B uses a smart bomb, and START pauses.

## Soundtrack

MUSIC cycles OFF / ORIGINAL / REMIX. REMIX appears only if you drop an mp3 at
`assets/music/remix.mp3` — the game's own effects keep playing over it. No
track is bundled: the one this was built against is Tony "Fluke73" Wiren's
*Battle Squadron (Genos 2 Edition)* from AmigaRemix, which is the remixer's
work to distribute, not ours.

## How it was verified

Every stage was checked against the original rather than against itself: 14,300 game
frames run in lockstep with all six object pools compared byte for byte, the attract
demo matched over 4,000 frames, and the renderer compared pixel for pixel
(96.9–99.7% exact, the remainder being palette-cycle phase).

## Legal

Battle Squadron is © Innerprise Software. This is a preservation project, not
affiliated with or endorsed by the rights holders, and retains the contents of the original Amiga project under `amiga/`. If you are a rights holder and want it taken down, get in touch.
