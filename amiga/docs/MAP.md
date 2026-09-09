# Amiga code and overlay map

## Boot path

The WHDLoad slave requires WHDLoad 19, 512 KiB base memory, and 4 KiB expansion
memory. Its loader entry is `$180` within the slave code hunk. It loads the
original `LOADER` at `$100`, validates either the PAL or NTSC checksum, applies
hardware/loader patches, and transfers control to `$100`.

The original loader then:

1. takes over the exception vectors and custom-chip interrupts;
2. enters its bootstrap at `$400`;
3. loads overlays using the descriptor table at `$1980`;
4. runs BOND decompression for descriptors whose mode is `-2`;
5. calls overlay jump tables at `$246F0` and `$3D800`.

## Verified loader routines

| Address | Symbol | Role |
| ---: | --- | --- |
| `$000100` | `LoaderEntry` | Original executable entry |
| `$000150` | `TakeOverSystem` | Save vectors, install handlers, configure interrupts |
| `$0001E0` | `EnterSupervisor` | Supervisor-mode transition |
| `$000298` | `CopyLongs` | Longword copy helper |
| `$000400` | `GameBootstrap` | Overlay loading and top-level game bootstrap |
| `$001BA8` | `LoadModule` | Load/copy a descriptor and optionally BOND-depack it |
| `$001C9E` | `Clear32Words` | Clear a 64-byte block |
| `$00AB46` | `BondDecompress` | Backward bitstream/LZ depacker with XOR validation |
| `$00EAD0` | `LoadFileByName` | Original named-file loader entry |

## Runtime overlays

The complete machine-readable list is in
[`module-map.json`](module-map.json). Important code overlays currently are:

| File | Load address | Stored | Runtime | Treatment |
| --- | ---: | ---: | ---: | --- |
| `LOADER` | `$000100` | 67,584 | 67,584 | Raw original loader |
| `LODGAM` | `$0246F0` | 29,200 | 38,108 | BOND depacked; game-audio jump table and assets |
| `LODCOM` | `$03D800` | 20,218 | 20,218 | Raw common-audio engine and assets |
| `LODMUS` | `$03D800` | 22,470 | 22,470 | Raw music overlay, replaces `LODCOM` |
| `LODSPE` | `$0246F0` | 12,186 | 12,186 | Raw special overlay, replaces `LODGAM` |

The game is heavily overlaid: several files deliberately share an address and
replace one another between menus, levels, music, and ending sequences.

## Discovery boundary

The disassembly configuration uses recursive control-flow discovery from
verified jump-table and exception entries. It does not linearly reinterpret
graphics or music as instructions. More code is reached through runtime state
tables and copied callback pointers; those spans remain exact data until each
entry is demonstrated. This is why source coverage is intentionally
conservative even though every rebuilt byte already matches.

Current proven coverage is 32,016 bytes (47.4%) of `LOADER`, 2,404 bytes
(6.3%) of the canonical `LODGAM` runtime image, and 1,494 bytes (7.4%) of
`LODCOM`.

`LODCOM` installs `$3DD74` into a low-memory vector in its initialization
routine. Seeding that demonstrated asynchronous entry reaches the four-channel
update routine at `$3D8B4`, expanding the proven common-audio path from 324 to
1,494 bytes. Two adjacent, structurally valid audio helpers at `$3DCF4` and
`$3DD50` remain classified as data because no runtime caller has yet been
demonstrated.

The discovery parser now distinguishes absolute targets such as `$24C6E.l`
from register-relative operands such as `$34(a2)`. The latter are library or
object dispatch offsets, not addresses in the current image. Removing those
false paths reduced the loader figure by 356 bytes and is covered by a focused
regression test.

## Native host boundary

The headless runner loads the byte-exact `LOADER` at `$100` and intercepts only
its external named-file routine at `$EAD0`. The original 68000 `LoadModule`
and `$AB46` BOND decompressor therefore remain on the executed path. Its first
long smoke gate reaches 20 named overlay loads and 2,054,047 blits in 50,000
host frames.

Implemented host facilities are 512 KiB chip RAM, raster position, Amiga
interrupt set/clear semantics, minimal CIA-A defaults, CIA-B timer A, and a
generic ascending/descending OCS blitter with A/B shifts, masks, modulos,
minterms, and BZERO state. Copper/video rendering, joystick events, and Paula
sample presentation are not wired into this runner yet.
