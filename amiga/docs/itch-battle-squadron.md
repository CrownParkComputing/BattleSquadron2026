# itch.io page — Battle Squadron

## Title
Battle Squadron — Native

## Tagline (short description, 140 chars max)
Innerprise's 1989 Amiga shooter, running natively. Two players, no emulator to configure, no disk image — download and play.

## Classification
Game · Action / Shoot 'em up

## Release status
Prototype

## Pricing
No payments (free download)

---

## Description (paste into the page body)

**Battle Squadron**, Innerprise's 1989 Amiga vertical shooter, running as a native program.

No emulator to set up, no disk image, no configuration. Download, unzip, run. Two players on one keyboard, or controllers, picked up automatically.

### What this actually is

The game's own 68000 code runs against a purpose-built OCS chipset — copper, blitter, bitplanes, sprites, Paula, CIA — written from what the game genuinely does rather than from a spec sheet, and checked against the original at the pixel level as it was built.

The loader is the game's own: its named-file loads are intercepted and served from the install, which is why there is nothing to mount and nothing to wait for.

### Features

- Two-player co-op, as the original
- Controllers hot-plugged, or share a keyboard
- Full soundtrack and effects through an emulated Paula
- Runs at a locked PAL 50Hz

### Controls

| Action | Player one | Player two | Controller |
| --- | --- | --- | --- |
| Move | arrows | WASD | stick or d-pad |
| Fire | Space, Ctrl or Enter | Alt or C | A / RB |
| Smart bomb | X or Shift | V or Tab | B / LB |
| Pause | P | — | START |

### Install

Unzip and run `./run.sh` (or the executable directly). Linux x86-64; needs OpenGL and X11. Run it from its own folder so it finds its data.

### Source

github.com/CrownParkComputing/BattleSquadron-Amiga — the port and the shared OCS chipset, which also runs Hybris.

---

## Legal note for the page

Battle Squadron is © Innerprise Software. This is a preservation project and
is not affiliated with or endorsed by the rights holders. If you are a rights
holder and want it taken down, get in touch and it goes.
