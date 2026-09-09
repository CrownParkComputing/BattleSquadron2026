# Test strategy

The project separates fast isolated checks from tests that execute original
game code and require the extracted WHDLoad data.

## Unit tests

```sh
make unit-test
```

The Python suite covers:

- absolute versus register-relative 68000 control-flow targets;
- recursive discovery and code-range merging;
- a valid synthetic one-byte BOND stream;
- BOND alignment, length, input, output, match, and checksum failures;
- signed module modes and termination of the loader's descriptor table.

The native `--selftest` suite exercises:

- eight blitter cases: A-to-D copy, cookie-cut minterm, both BZERO states,
  first/last masks, row modulos, ascending shift carry, and descending copy;
- twelve video cases covering bitplane DMA, copper moves, fine scrolling, and
  attached-sprite palette/transparent-pixel behavior;
- nine input cases covering joystick quadrature, active-low fire/Nova, and
  Amiga keyboard serial/PORTS handshaking;
- eight Paula cases covering DMA latching, sample stepping, stereo routing,
  interrupt requests, and ring-buffer output.

## Genuine recompilation and map tests

```sh
make recomp-test
```

This no-Musashi gate covers the native BOND translation, exact bootstrap and
recorded combat checkpoints, the reusable OCS renderer/Copper palette scan,
and the reusable map lab. The synthetic map test proves one-pixel playback,
the deliberately broken coarse-only view, 256-row wrapping, source mismatch
detection, and binary trace save/load.

The Battle Squadron adapter then captures 300 real terrain rows from the
native runtime. Every decoded source row must exactly match all 384 pixels
written into the five-plane circular playfield. Progress, ring address, tile
phase, and combined coarse/fine horizontal scroll must remain continuous. It
also requires non-empty scenery-object pixels captured after the native BOB
pass, stored separately from the exact terrain rows.

`test_bs_scroll_video` enters the real new-game path and requires its exact
`$00A0`/`$49E20` opening starfield state. It renders adjacent live frames
through the reusable OCS scanout and requires an exact one-pixel relationship
across the unobstructed playfield. The reusable scanout test separately locks
the left and right fine-scroll carry pixels for the game's 304-pixel fetch in
its 320-pixel display. It then runs 3,000 gameplay frames, verifies
that transient BOB pixels are restored, and checks on every frame that the
Copper bitplane address equals the terrain-ring address plus the camera's
coarse offset. This locks presentation to the completed `$AA0` edge instead
of the twice-per-loop `$1078` counter or the pre-restore `$B54` midpoint, and
guards against stale register upper words redirecting bitplane scanout.
The sustained run includes the opening type-`$01` launcher and type-`$07`
ground projectile rather than stopping when that map object becomes active.
`test_paula_audio` covers PCM stepping, one-shot completion, DMA looping,
authentic channel pan, and callback-ring accounting. The recomp boot test
also checks the exact `$246F0`/`$17CD` LODMUS speech descriptor and byte-exact
LODSPE payload.

For a longer visible artifact:

```sh
make map-extract   # 2,048 checked rows + PPM map
make show-map      # interactive scanline playback
```

In the player, left/right provides manual pixel panning (`A` restores the
recorded camera), `C` toggles the coarse-word-only failure simulation, `O`
toggles scenery objects, and `G` overlays 16-pixel tile boundaries.

## Integration tests

```sh
make integration-test
```

The integrity stage rebuilds `LOADER`, `LODGAM`, and `LODCOM` byte-for-byte and
decompresses all 15 real BOND streams with checksum validation. The native
stage starts from a fresh emulated machine for each milestone:

| Frames | PC | Loads | Blits | Meaning |
| ---: | ---: | ---: | ---: | --- |
| 10 | `$AB80` | 1 | 0 | Original BOND depacker is executing |
| 500 | `$1CB8` | 7 | 0 | Initial overlay bootstrap is progressing |
| 50,000 | `$C90` | 20 | 2,054,047 | Stable deep game-loop execution |

The suite also proves that unmet milestone expectations produce a failing
process status. Exact PCs and counts intentionally detect timing or chipset
behavior changes; update them only after verifying why execution changed.

An additional 12,000-frame autofire scenario crosses the title-to-game load
and asserts at least 13 file loads, 500,000 blits, 100,000 Paula register
writes, nonzero PCM energy, and 10,000 CIA-B music callbacks. This guards the
interrupt-context bug that previously froze music during the game load.

The integration suite takes roughly seven seconds on the current host after
the native binary has been built. Run everything with:

```sh
make test
```
