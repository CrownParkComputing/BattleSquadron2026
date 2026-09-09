# itch.io page — Hybris

## Title
Hybris — Native

## Tagline (short description, 140 chars max)
Discovery Software's 1988 Amiga shooter, running natively. No emulator to set up, no disk image, no WHDLoad — it just runs.

## Classification
Game · Action / Shoot 'em up

## Release status
Prototype

## Pricing
No payments (free download)

---

## Description (paste into the page body)

**Hybris**, Discovery Software's 1988 Amiga vertical shooter, running as a native program.

There is no emulator to configure and no disk image to mount. Download, unzip, run. It boots from ordinary files, with a controller picked up automatically.

### What this actually is

Not a wrapper around an emulator front-end. The game's own 68000 code runs against a purpose-built OCS chipset — copper, blitter, bitplanes, sprites, Paula, CIA — written against what Hybris genuinely does rather than against a spec sheet. Several things had to be implemented because the game visibly breaks without them:

- The score panel is drawn by the **copper writing sprite registers directly**, repositioning a single sprite for every glyph across one scanline. None of it goes through a sprite list.
- The commander selection highlights one name with a **mid-scanline colour change** — a copper WAIT with a horizontal target as well as a vertical one.
- The end-of-attract credit scroller is a **hires** screen.
- The music runs off a **CIA-B timer interrupt at level 6**.

The display is a full PAL raster at 352×288, presented at 4:3 because an Amiga pixel is not square.

### Features

- Boots from files — no ADF, no WHDLoad slave, nothing to configure
- Full controller support, hot-plugged, plus keyboard
- Pause, and an alternative soundtrack you can switch to mid-game
- Screenshot-and-state capture on F12
- Runs at a locked PAL 50Hz

### Controls

| Action | Controller | Keyboard |
| --- | --- | --- |
| Move | stick or d-pad | arrows or WASD |
| Fire | A or RB | Ctrl or Z |
| Second button | B | Shift |
| Actions | X, Y | Space, Return |
| Pause | START | P |
| Swap soundtrack | L1 | F2 |

### Install

Unzip and run `./run.sh` (or the executable directly). Linux x86-64; needs OpenGL and X11, which any desktop already has. Run it from its own folder so it finds its data.

### Source

github.com/CrownParkComputing/Hybris — the port, the chipset, the disk extractor and the graphics ripper.

---

## Legal note for the page

Hybris is © Discovery Software International. This is a preservation project
and is not affiliated with or endorsed by the rights holders. If you are a
rights holder and want it taken down, get in touch and it goes.
