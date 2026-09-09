# Sprites

Artwork that replaces the game's own, drawn by the frontend over the exact
rectangle the blitter would have used.  Nothing here is limited to the Amiga's
32 colours or its resolution: these are ordinary 24-bit PNGs with alpha.

## The folder is the manifest

A file named after the blit source it replaces is picked up automatically —
no code, no list to maintain:

    018620.png            claims $018620 and the three animation frames
                          that follow it (a frame is 640 bytes: five planes
                          of 128)
    018620-0188a0.png     claims exactly that range

Run with `HYBRIS_REMASTER=1` to enable them; without it the game draws its own
art as usual, so it is easy to compare.

## Finding the address of something you want to replace

    HYBRIS_IDENTIFY=1 BS_TRACE_REPLACE=1 ./build/hybris --frames 2600 \
        --fire-from 700 --ppm build/frame.ppm

That claims every art source but suppresses nothing, so the picture looks
normal while every object reports the address that drew it and where it
landed.  Match a position against the rendered frame and you have the address.

It is worth doing rather than guessing: the flying enemy at $018620 sits right
beside an animated opening in the map at $0180c0, and claiming the pair by
eye replaces the scenery instead of the enemy.

## originals/

Reference art ripped out of chip RAM with `tools/rip_hybris_sprites.py`, at
the game's own size and palette.  Useful for seeing what a replacement has to
read as at 32 pixels wide, before it gets drawn at any size you like.
