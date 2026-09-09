# Reusable Amiga recompilation kit

Battle Squadron is the first consumer, but the platform seams are deliberately
title-neutral. A future game should reuse these pieces and keep its addresses,
file formats, translated routines, and state machines in a separate adapter.

## Reusable components

| Component | Contract | Game-specific knowledge |
| --- | --- | --- |
| `src/platform/recomp_68k.h` | D0-D7, A0-A7, PC, SR/CCR and partial-register helpers | None |
| `src/platform/ocs_video.h` | Memory callback + chip mask + Copper address -> 320x256 RGBA | None |
| `src/platform/ocs_video.c` | PAL Copper MOVE/WAIT, six bitplanes, dual playfields, fine scroll, eight sprites | None |
| `src/platform/paula_audio.c` | Four Paula DMA channels, descriptor one-shots, authentic pan, PAL producer and callback ring | None |
| `src/platform/ocs_palette.h` | Memory callback + Copper address + raster line -> live 32-colour RGB4 palette | None |
| `src/platform/scroll_map.h` | Indexed scanline trace, ring/fine-scroll validation, save/load, playback and PPM export | None |
| `tests/test_ocs_video.c` | Synthetic Copper, palette WAIT, bitplane and sprite acceptance test | None |
| `tests/test_scroll_map.c` | Synthetic one-pixel playback, coarse-only negative view, ring wrap and trace round-trip | None |
| `src/recomp/preview.c` | Battle Squadron runtime-to-raylib adapter | Battle Squadron frame counter and boot checkpoint only |
| `src/recomp/bs_map.c` | Adapter from the title's map words/tile bank/ring state into `ScrollMapTrace` | Battle Squadron addresses and five-plane tile layout |

`overlay.c`, `bond.c`, and `runtime.c` remain Battle Squadron code. BOND is a
game format, not an Amiga platform service, and should only be reused by a
title that proves it has the same stream format.

## New-title sequence

1. Lock one owned binary variant with size and SHA-256. Never let codegen run
   against an unrecognised input.
2. Keep an emulator-backed build only as a development oracle. Give the
   shipping/native target a separate link graph from day one.
3. Create a title context containing `Recomp68kContext`, owned RAM, custom/CIA
   state, and title services. Preserve big-endian memory until parity is solid.
4. Translate the boot path into direct C basic blocks. Every unknown PC,
   indirect target, object type, or service must return `UNTRANSLATED`; there
   is no interpreter fallback.
5. At each new boundary compare PC, registers, CCR, touched memory, service
   order, and custom-register writes with the oracle.
6. Connect `OcsVideoSource` using the title's byte-read callback, chip-memory
   mask, and current Copper-list address. The renderer does not need access to
   the title context or to a 68000 core.
7. Add live input and audio through similarly narrow platform contracts. Do
   not expose title memory offsets to the frontend.
8. Before connecting gameplay rendering, capture the title's raw incoming map
   rows with a small adapter. Verify every decoded source row against the row
   written into its playfield ring, then validate one-pixel progress, ring
   wrap, tile phase, and coarse/fine cross-scroll independently.
9. Warm a circular terrain buffer completely before presenting it. During a
   title-to-game transition the unfilled rows still contain the previous
   bitmap and will decode as corrupt terrain under the new palette.
10. Identify the unique post-scroll scheduler edge. A title's general frame
    counter can advance more than once per displayed terrain update; using it
    blindly produces duplicate frames and half-rate motion.
11. Preserve the architectural results of removed raster waits and interrupt
    seams, including register upper words. Assert each generated Copper
    bitplane pointer against the expected ring address and camera offset over
    a sustained run; a correct ring can still look corrupt when scanout is
    redirected into unrelated chip memory.
12. Gate release artifacts with `nm`/`readelf`: no Musashi, UAE, `m68k_execute`,
   opcode decoder, or interpreter fallback may be present.

## Map extraction/playback contract

`ScrollMapTrace` records one newly generated indexed scanline per scroll
update. A record carries the source and ring addresses, world position, tile
phase, and both the complete cross-scroll position and its deliberately
coarse-only counterpart. An optional second indexed line uses `$FF` as
transparent and holds scenery BOB/object pixels without modifying the verified
terrain. The reusable player reconstructs the current viewport from the
latest rows and can toggle that layer independently.

Two details are acceptance rules rather than presentation choices:

- The current cross-scroll value applies to the whole reconstructed viewport.
  Applying each historical row's old camera value diagonally shears the map.
- The coarse address and fine 0-15 pixel phase must combine into a position
  whose frame-to-frame delta is at most one pixel. Dropping the fine phase
  makes the display freeze and then jump by a complete 16-pixel word.
- Do not clip fine-scroll carry to the nominal DDF fetch span. A display can
  be wider than that span and consume adjacent row words through the shifter;
  hard clipping exposes or removes a black word-wide strip at coarse rollover.

Palette extraction is separate. `ocs_palette_at_line` replays the live Copper
list through a requested raster line, so map tools do not guess colours from
a fade workspace or hard-coded table.

For a new vertically scrolling title, its adapter only needs to decode the
incoming source row, decode the matching row in the playfield ring, and fill
the generic record fields. Horizontal titles can use the same trace format;
horizontal viewport reconstruction is the next platform extension when a
consumer needs it.

## Minimum acceptance ladder

- **Boot:** exact service order and first stable display checkpoint.
- **First frame:** exact registers, CCR, focused RAM, and screen-plane hashes.
- **Playable slice:** title -> join/start -> controls -> fire -> enemies ->
  collision -> death/respawn, with unknown edges fatal.
- **Sustained run:** deterministic multi-thousand-frame profiles, both players,
  dense combat, save/load, and negative controls that prove gates can fail.
- **Release:** the frontend and packaged binary link only translated title code
  and reusable platform modules. The oracle remains a developer-only target.

## Battle Squadron example

```c
static uint8_t read_title_memory(void *user, uint32_t address)
{
    return bs_recomp_read8(user, address);
}

OcsVideoSource source = {
    .user = machine,
    .read8 = read_title_memory,
    .chip_mask = BS_RECOMP_MEMORY_SIZE - 1,
    .copper_address = current_copper_address
};
const uint32_t *rgba = ocs_video_render(video, &source);
```

That callback is the complete dependency from the reusable renderer back into
Battle Squadron.

Battle Squadron's map adapter is similarly small: 24 map words select 16-pixel
patterns from a five-plane bank, producing one 384-pixel row. The runtime
inserts that row at a 48-byte stride into a 256-row circular playfield. Run
the exact extractor/player with:

```sh
make map-test
make map-extract
make show-map
```

Its object layer is sampled after the upper scenery render list and before
projectile/player rendering. Comparing that completed playfield row with the
raw source row captures map-anchored turrets and guns while avoiding moving
bullet and player trails. `O` switches between composited and raw terrain.
