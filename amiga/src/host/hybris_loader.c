#include "hybris_loader.h"

#include "amiga.h"
#include "hybris_files.h"
#include "m68k.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>

/* Intercept the cracked loader's trackloader and serve files instead.
 *
 * The routine at HYBRIS_READ_TRACKS is entered with the start track in D0 and
 * the track count in D1; its caller has already stored the destination in the
 * long at HYBRIS_DEST_PTR, and HYBRIS_FILE_PTR points at the descriptor of
 * the file being loaded, whose length sits at descriptor+2.  Those four facts
 * are the whole contract -- verified against the loader's own dispatch table,
 * which names the same (track, count) pairs the extracted disk map derives.
 *
 * Serving it here means no MFM decode, no DSKLEN DMA and no drive stepping
 * ever runs: the same shape as Battle Squadron's named-file hook. */
#define HYBRIS_READ_TRACKS 0x00eed8
#define HYBRIS_DEST_PTR    0x00f130
#define HYBRIS_FILE_PTR    0x002e28

long hybris_load_count;
long hybris_load_bytes;

static bool trace;

static uint32_t peek32(uint32_t address)
{
    return ((uint32_t)chip[address] << 24) | ((uint32_t)chip[address + 1] << 16)
         | ((uint32_t)chip[address + 2] << 8) | chip[address + 3];
}

/* The intro overlay is a table of eight-character slots, each with its own
 * screen position: "DSI-LOGO" "PRESENTS" "HYBRIS  " "BY      " "QUARTEX!"
 * "MADEBY  " "MARTIN  " "PEDERSEN".  Eight characters per slot is the whole
 * budget, so the replacement is written to read across the eight lines as
 * they appear one after another. */
static const struct { const char *from, *to; } INTRO_TEXT[] = {
    { "DSI-LOGO", "RETRO   " },
    { "PRESENTS", "RECOMPS " },
    { "HYBRIS  ", "PRESENT " },
    { "BY      ", "IN      " },   /* this slot sits furthest right: keep it
                                   * short, the original word here was "BY" */
    { "QUARTEX!", "2026    " },
    { "MADEBY  ", "NATIVE  " },
    { "MARTIN  ", "WIN LNX " },
    { "PEDERSEN", "IOS ANDR" },
};

/* The table is PACKED inside the executable, so it does not exist until the
 * decruncher has run -- which is why this sweeps chip RAM once, on the first
 * file load, rather than patching the file or the image at load time. */
static void rewrite_intro_text(void)
{
    size_t count = sizeof INTRO_TEXT / sizeof INTRO_TEXT[0];
    int replaced = 0;
    for (uint32_t at = 0; at + 8 <= CHIP_SIZE; at++)
        for (size_t i = 0; i < count; i++)
            if (!memcmp(chip + at, INTRO_TEXT[i].from, 8)) {
                memcpy(chip + at, INTRO_TEXT[i].to, 8);
                fprintf(stderr, "hybris: intro slot $%06x \"%s\" -> \"%s\"\n",
                        at, INTRO_TEXT[i].from, INTRO_TEXT[i].to);
                replaced++;
                at += 7;
                break;
            }
    if (!replaced) fprintf(stderr, "hybris: intro text not found to rewrite\n");
}

/* $C65E is the BSET #5 the SPACE key reaches, $C686 the one a POT0DAT
 * change reaches: the two routes to the same action bit. */
long hybris_action_key, hybris_action_pot;

static void hybris_hook(unsigned int pc)
{
    if (pc == 0x00c65e) { hybris_action_key++; return; }
    if (pc == 0x00c686) { hybris_action_pot++; return; }
    if (pc != HYBRIS_READ_TRACKS) return;

    int track = (int)(m68k_get_reg(NULL, M68K_REG_D0) & 0xff);
    uint32_t tracks = m68k_get_reg(NULL, M68K_REG_D1) & 0xffff;
    uint32_t destination = peek32(HYBRIS_DEST_PTR);
    uint32_t descriptor = peek32(HYBRIS_FILE_PTR);
    uint32_t declared = descriptor + 6 <= CHIP_SIZE ? peek32(descriptor + 2) : 0;

    /* The loader would write whole tracks and stop at the file's declared
     * length; serve exactly that many bytes. */
    uint32_t length = tracks * HYBRIS_TRACK_BYTES;
    if (declared && declared < length) length = declared;

    const char *id = hybris_files_id(track);
    if (!id || destination >= CHIP_SIZE || length > CHIP_SIZE - destination) {
        fprintf(stderr, "hybris: unserviceable read, track %d x%u -> $%06x "
                "(%u bytes)\n", track, tracks, destination, length);
        amiga_stop();
        return;
    }

    uint32_t served = hybris_files_read(track, chip + destination, length);
    if (served != length) {
        fprintf(stderr, "hybris: file %s served %u of %u bytes from track %d\n",
                id, served, length, track);
        amiga_stop();
        return;
    }
    if (!hybris_load_count) rewrite_intro_text();
    hybris_load_count++;
    hybris_load_bytes += served;
    if (trace)
        fprintf(stderr, "hybris: load %s (track %d x%u, %u bytes) -> $%06x\n",
                id, track, tracks, served, destination);

    amiga_return_from_hook();
}

/* Re-sign the loader.  The cracked disk carries a 24-character signature in
 * the payload the decruncher copies down to $100; replacing it in RAM after
 * the hunks load leaves the extracted file byte-identical to the disk, and
 * the replacement is the same length so nothing moves. */
static const char CRACK_SIGNATURE[] = "CRACKED BY QUARTEX (ROB)";
static const char OUR_SIGNATURE[]   = "DECOMPED BY RETRO RECOMP";

static void resign_loader(void)
{
    size_t length = sizeof CRACK_SIGNATURE - 1;
    for (uint32_t at = 0; at + length < CHIP_SIZE; at++) {
        if (memcmp(chip + at, CRACK_SIGNATURE, length)) continue;
        memcpy(chip + at, OUR_SIGNATURE, length);
        fprintf(stderr, "hybris: signature at $%06x replaced with \"%s\"\n",
                at, OUR_SIGNATURE);
        return;
    }
}

/* A FLYING enemy, as opposed to the animated map furniture sitting right
 * next to it in memory.  The three animation frames are 640 bytes apart --
 * five planes of 128 -- and they are identifiable because they move
 * SIDEWAYS: a map element only ever scrolls straight down with the terrain,
 * so tracking each blit source's screen position over time separates the two
 * without having to understand either one's artwork.
 *   $018120, $0183a0, $018620 = frames 1..3
 * Claiming from the first frame's base to the end of the third covers every
 * plane of all three, and deliberately starts ABOVE $0180c0, which is the
 * animated opening in the map. */
#define ALIEN_ART_LOW   0x018120
#define ALIEN_ART_HIGH  0x0188a0
#define ALIEN_ID        0

HybrisSpriteArt hybris_sprite_art[32];
int hybris_sprite_art_count;

/* One 5-plane frame is 640 bytes; a bare address claims the three animation
 * frames that normally follow it. */
#define FRAME_BYTES 640
#define DEFAULT_FRAMES 3

static char sprite_folder[256] = "assets/sprites";

void hybris_set_sprite_folder(const char *folder)
{
    snprintf(sprite_folder, sizeof sprite_folder, "%s", folder);
}

static void load_sprite_folder(const char *folder)
{
    DIR *dir = opendir(folder);
    if (!dir) return;
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        unsigned low = 0, high = 0;
        int matched = sscanf(entry->d_name, "%x-%x", &low, &high);
        if (matched < 1 || !low) continue;
        if (!strstr(entry->d_name, ".png")) continue;
        if (matched == 1) high = low + FRAME_BYTES * DEFAULT_FRAMES;
        if (hybris_sprite_art_count ==
            (int)(sizeof hybris_sprite_art / sizeof *hybris_sprite_art))
            break;
        HybrisSpriteArt *art = &hybris_sprite_art[hybris_sprite_art_count];
        art->id = hybris_sprite_art_count + 1;      /* 0 means "unclaimed" */
        snprintf(art->path, sizeof art->path, "%s/%s", folder, entry->d_name);
        amiga_register_replacement(low, high, art->id);
        fprintf(stderr, "hybris: %s replaces $%06x-$%06x\n", art->path,
                low, high);
        hybris_sprite_art_count++;
    }
    closedir(dir);
}

bool hybris_loader_install(const char *directory)
{
    if (!hybris_files_load(directory)) return false;
    if (getenv("HYBRIS_REMASTER"))
        load_sprite_folder(sprite_folder);
    if (getenv("HYBRIS_IDENTIFY")) {
        /* Claim everything, suppress nothing: every object reports the
         * address that drew it, against a picture that still looks normal. */
        amiga_register_replacement(0x014000, 0x040000, 0);
        amiga_replacements_suppress(false);
    }
    resign_loader();
    trace = getenv("HYBRIS_TRACE_LOAD") != NULL;
    amiga_set_pc_hook(hybris_hook);
    return true;
}
