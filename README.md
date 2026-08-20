# Battle Squadron 2026

**Battle Squadron** (Innerprise / Cope-com, 1989) rebuilt as a native program — no
emulator, no 68000 core, no disk image. The game's logic, wave scheduler, collision
and sound drivers were read out of the original and rewritten in C; the picture is
drawn by a native renderer and the audio synthesised on a native Paula.

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
| `re/` | the write-up: engine notes, porting guide, asset formats |

## Building

```sh
make            # needs raylib and a C compiler
./build/bsview
```

The original game files (`LOADER`, `LODGAM`, `LODST1`, …) are **not** included and
are not distributed here. Point the loader at your own copy.

## Controls

| | Player one | Player two | Pad |
| --- | --- | --- | --- |
| Move | arrows | WASD | stick / d-pad |
| Fire | Space / Ctrl / Enter | Alt / C | A |
| Smart bomb | X / Shift | V / Tab | B |
| Pause | P / Esc | — | START |

Player two joins at any time by pressing fire.

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
affiliated with or endorsed by the rights holders, and contains none of the game's
data or code. If you are a rights holder and want it taken down, get in touch.
