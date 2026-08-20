/* bsdata.c -- Battle Squadron native data decoder (formats: re/ASSETS.md; the
 * bob catalog is proven against the live renderer by tools/spritecheck.c, the
 * previews in re/assets_preview/ are generated from it by tools/sprite_dump.py).
 * Containers: overlay.c (module table at LOADER file offset $1880, terminated
 * by LODSAV) + bond.c (BOND depacker).  The LOADER image itself is raw and
 * loads at chip $100. */
#include "bsdata.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int load_loader(BsData *d)
{
    char path[600];
    snprintf(path, sizeof path, "%s/LOADER", d->dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    size_t got = fread(d->chip + 0x100, 1, BS_CHIP_SIZE - 0x100, f);
    fclose(f);
    if (got != 67584) { fprintf(stderr, "bsdata: unexpected LOADER size %zu\n", got); return -1; }
    return 0;
}

const BsModule *bs_find_module(const BsData *d, const char *name)
{
    for (size_t i = 0; i < d->nmods; i++)
        if (!strcmp(d->mods[i].name, name)) return &d->mods[i];
    return NULL;
}

int bs_load_module(BsData *d, const char *name)
{
    const BsModule *m = bs_find_module(d, name);
    if (!m) { fprintf(stderr, "bsdata: no module %s\n", name); return -1; }
    int err = bs_module_load(d->dir, m, d->chip, BS_CHIP_SIZE, NULL);
    if (err) fprintf(stderr, "bsdata: %s: %s\n", name, bs_overlay_error(err));
    return err;
}

int bs_open(BsData *d, const char *data_dir)
{
    memset(d, 0, sizeof *d);
    snprintf(d->dir, sizeof d->dir, "%s", data_dir);
    d->stage = -1;
    if (load_loader(d)) return -1;
    size_t count = 0;
    int err = bs_modules_parse(d->chip + 0x100, 67584, d->mods, BS_MODULE_COUNT_MAX, &count);
    if (err) { fprintf(stderr, "bsdata: module table: %s\n", bs_overlay_error(err)); return err; }
    d->nmods = count;
    if (bs_load_module(d, "LODDAT")) return -1;
    if (bs_load_module(d, "LODGAM")) return -1;
    return 0;
}

int bs_load_stage(BsData *d, int stage)
{
    int err = 0;
    switch (stage) {
    case 0:
        err = bs_load_module(d, "LODS0F");
        if (!err) err = bs_load_module(d, "LODS0S");
        if (!err) err = bs_load_module(d, "LODS0T");
        break;
    case 1:
        err = bs_load_module(d, "LODST1");
        if (!err) {
            /* LoadModule's LODST1-only tail ($1C08..$1C22): the depacked
             * region $2EF20..$5E000 is stored byte-REVERSED in the file --
             * exchange bytes inward until the cursors meet at $46790 */
            uint8_t *a1 = d->chip + 0x2EF20, *a0 = d->chip + 0x5E000;
            while (a1 != d->chip + 0x46790) {
                uint8_t t = *a1;
                *a1++ = a0[-1];
                *--a0 = t;
            }
            /* LAB_71CA..$7242 (stage-advance, descriptor $1A28 only): 8 object
             * gfx pointers rebased $F068+n*$12 -> $E924+n*$12 over the 12-byte
             * records at $2E8A2..$2EF20 */
            for (uint32_t rec = 0x2E8A2; rec < 0x2EF20; rec += 12) {
                uint32_t v = bs_l(d, rec);
                if (v >= 0xF068 && v <= 0xF0E6 && (v - 0xF068) % 0x12 == 0) {
                    uint32_t nv = 0xE924 + (v - 0xF068);
                    d->chip[rec] = (uint8_t)(nv >> 24); d->chip[rec + 1] = (uint8_t)(nv >> 16);
                    d->chip[rec + 2] = (uint8_t)(nv >> 8); d->chip[rec + 3] = (uint8_t)nv;
                }
            }
        }
        break;
    case 2: err = bs_load_module(d, "LODST2"); break;
    case 3: err = bs_load_module(d, "LODST3"); break;
    default: return -1;
    }
    if (!err) d->stage = stage;
    return err;
}

/* ---------- terrain ---------- */
void bs_tile(const BsData *d, uint16_t map_word, uint8_t out[16 * 16])
{
    uint32_t base = 0x4A000 + (uint32_t)map_word * 2;
    for (int y = 0; y < 16; y++) {
        uint16_t pw[5];
        for (int p = 0; p < 5; p++) pw[p] = bs_w(d, base + (uint32_t)p * 0x20 + (uint32_t)y * 2);
        for (int x = 0; x < 16; x++) {
            uint8_t c = 0;
            for (int p = 0; p < 5; p++) c |= (uint8_t)(((pw[p] >> (15 - x)) & 1) << p);
            out[y * 16 + x] = c;
        }
    }
}

void bs_map_render(const BsData *d, uint8_t *idx, int stride)
{
    uint8_t tile[256];
    for (int r = 0; r < 512; r++) {
        uint32_t row = 0x44000 + (uint32_t)r * 0x30;
        for (int cx = 0; cx < 24; cx++) {
            bs_tile(d, bs_w(d, row + (uint32_t)cx * 2), tile);
            for (int y = 0; y < 16; y++)
                memcpy(idx + (size_t)(r * 16 + y) * stride + cx * 16, tile + y * 16, 16);
        }
    }
}

void bs_palette(const BsData *d, int stage, uint16_t rgb12[32])
{
    uint32_t base = 0x14EA + (uint32_t)stage * 0x8C + 12;
    for (int i = 0; i < 32; i++) rgb12[i] = bs_w(d, base + (uint32_t)i * 2);
}

void bs_palette_alt(const BsData *d, int stage, uint16_t rgb12[32])
{
    uint32_t base = 0x14EA + (uint32_t)stage * 0x8C + 76;
    for (int i = 0; i < 32; i++) rgb12[i] = bs_w(d, base + (uint32_t)i * 2);
}

/* ---------- bobs ---------- */
static void bob_fill(BsBob *b, uint32_t gfx, int w_words, int h, int row_bytes, int planes_per_frame)
{
    b->planes = gfx;
    b->w_words = w_words;
    b->h = h;
    b->row_bytes = row_bytes;
    b->plane_stride = row_bytes * h;
    b->frame_stride = planes_per_frame * b->plane_stride;
    b->mask = gfx + (uint32_t)(b->frame_stride - b->plane_stride);
    b->mask_stride = b->frame_stride;
    b->vis_w = row_bytes * 8;
    b->frames = 1;
    b->cloak = 0;
}

int bs_hostile_gfx(const BsData *d, int type, BsBob *b)
{
    if (type < 0 || type > 13) return -1;
    uint32_t a = 0xCD7A + (uint32_t)type * 0x20;
    int h = (int16_t)bs_w(d, a), w = (int16_t)bs_w(d, a + 2);
    if (w < 2 || h < 1) return -1;
    bob_fill(b, bs_l(d, a + 20), w, h, 2 * w - 2, 6);
    /* NB the descriptor's +16 "mask" is only the record's initial gfxmask for the
     * draw_ptr paths -- it is not always gfx + 5*plane_stride (type $06 is $28
     * off), and LAB_97F8's draw_frame derives the mask, so bob_fill wins. */
    return 0;
}

int bs_object_gfx(const BsData *d, int tmpl, BsBob *b)
{
    if (tmpl < 0 || tmpl > 25) return -1;
    uint32_t a = 0x2B68 + (uint32_t)tmpl * 48;
    int h = (int16_t)bs_w(d, a + 6), w = (int16_t)bs_w(d, a + 8);
    if (w < 1 || h < 1) return -1;
    bob_fill(b, bs_l(d, a + 12), w, h, 2 * w, 5);
    b->mask = 0; b->mask_stride = 0;            /* objects blit opaque (no cookie cut) */
    return 0;
}

void bs_bob_frame(const BsData *d, const BsBob *b, int frame, uint8_t *idx, uint8_t *alpha)
{
    uint32_t base = b->planes + (uint32_t)frame * (uint32_t)b->frame_stride;
    uint32_t mask = b->mask ? b->mask + (uint32_t)frame * (uint32_t)b->mask_stride : 0;
    for (int y = 0; y < b->h; y++)
        for (int x = 0; x < b->vis_w; x++) {
            uint32_t byte = (uint32_t)(y * b->row_bytes + (x >> 3));
            uint8_t bit = (uint8_t)(0x80 >> (x & 7)), c = 0;
            if (!b->cloak)
                for (int p = 0; p < 5; p++)
                    if (d->chip[(base + (uint32_t)p * (uint32_t)b->plane_stride + byte) & (BS_CHIP_SIZE - 1)] & bit)
                        c |= (uint8_t)(1 << p);
            idx[y * b->vis_w + x] = c;
            if (alpha)
                alpha[y * b->vis_w + x] =
                    mask ? ((d->chip[(mask + byte) & (BS_CHIP_SIZE - 1)] & bit) ? 255 : 0) : 255;
        }
}

double bs_bob_silhouette(const BsData *d, const BsBob *b, int frame)
{
    uint32_t mask = b->mask ? b->mask + (uint32_t)frame * (uint32_t)b->mask_stride : 0;
    if (!mask) return 0.0;
    long runs = 0, rows = 0;
    for (int y = 0; y < b->h; y++) {
        int prev = 0, r = 0, any = 0;
        for (int x = 0; x < b->vis_w; x++) {
            uint32_t byte = (uint32_t)(y * b->row_bytes + (x >> 3));
            int bit = (d->chip[(mask + byte) & (BS_CHIP_SIZE - 1)] >> (7 - (x & 7))) & 1;
            if (bit && !prev) r++;
            if (bit) any = 1;
            prev = bit;
        }
        if (any) { runs += r; rows++; }
    }
    return rows ? (double)runs / (double)rows : 0.0;
}

double bs_bob_flatness(const BsData *d, const BsBob *b, int frame)
{
    static uint8_t idx[512 * 256];
    if ((size_t)b->vis_w * b->h > sizeof idx) return 1.0;
    bs_bob_frame(d, b, frame, idx, NULL);
    long hist[32] = { 0 }, n = (long)b->vis_w * b->h, top = 0;
    for (long i = 0; i < n; i++) hist[idx[i] & 31]++;
    for (int i = 0; i < 32; i++) if (hist[i] > top) top = hist[i];
    return n ? (double)top / (double)n : 1.0;
}

/* ---------- sprite catalog ----------
 * Ground truth: tools/bobscan.c replays the engine and prints every distinct
 * (type, gfx, mask, w, h, plane stride) the render list carries; the frame
 * counts below are the spans that scan observed, and the never-spawned banks
 * (mothership, flypast, stage-3 boss) come from the literal set-ups in
 * src/behaviours/hostiles.c.  `stages` is the overlay residency: e.g. the
 * type-$03 pop-up lives at $36000 which is LODS0F, so it is only that sprite
 * while stage 0 is loaded -- with any other stage the same address holds that
 * stage's terrain and decodes to noise.  That residency, not the strides, is
 * what made the browser show noise for most entries. */
#define S0 1u
#define S1 2u
#define S2 4u
#define S3 8u
#define SALL 15u
/* NOT in the catalog: the end-of-game mothership (type $02) and the flypast
 * decoration (type $0B).  The mothership's banks ($44000/$44690/$50060/$565A0,
 * LAB_8FAE..LAB_92CC) live in LODEND/LODFIN, which the stage browser never has
 * resident; the flypast borrows the bomber/drone/pop-up banks already listed. */
static const BsSprite SPRITES[] = {
/* --- hostiles: banks in LODDAT, resident in every stage --- */
{ "SHIP BANK (TYPE 00/08)",  BS_SPR_HOSTILE, 0x00, SALL, 0x17500, 0, 3, 32, 6, 16, 0 },
{ "AIMED DRONE",             BS_SPR_HOSTILE, 0x01, SALL, 0x20500, 0, 3, 32, 6, 10, 0 },
{ "SWOOP FIGHTER",           BS_SPR_HOSTILE, 0x04, SALL, 0x1A500, 0, 3, 32, 6, 32, 0 },
{ "PICKUP",                  BS_SPR_HOSTILE, 0x05, SALL, 0x12890, 0, 2, 16, 6, 12, 0 },
{ "BOMBER",                  BS_SPR_HOSTILE, 0x06, SALL, 0x14450, 0, 3, 44, 6, 11, 0 },
{ "EXPLOSION / BOSS PUFF",   BS_SPR_HOSTILE, 0x0A, SALL, 0x11090, 0, 3, 32, 6,  8, 0 },
/* type $08 has no colour planes: LAB_5B3E captures the terrain and cookie-cuts
 * it through the SWOOP FIGHTER masks at $1A780 + frame*$300 (src/render.c draw_cloak) */
{ "HOMING MISSILE (CLOAK)",  BS_SPR_HOSTILE, 0x08, SALL, 0x1A780, 0, 3, 32, 6, 16, 1 },
/* --- hostiles living in a stage overlay --- */
{ "GROUND POP-UP",           BS_SPR_HOSTILE, 0x03, S0,   0x36000, 0, 3, 32, 6,  5, 0 },
{ "RISING GUN",              BS_SPR_HOSTILE, 0x07, S0,   0x43700, 0, 3, 32, 6,  3, 0 },
{ "STAGE 1 BOSS HULL",       BS_SPR_HOSTILE, 0x09, S1,   0x2F560, 0, 7, 32, 6,  1, 0 },
{ "STAGE 1 BOSS TURRETS",    BS_SPR_HOSTILE, 0x09, S1,   0x31960, 0, 7, 48, 6,  2, 0 },
{ "TANK",                    BS_SPR_HOSTILE, 0x0C, S1,   0x507C0, 0, 5, 50, 6,  3, 0 },
/* stage 2/3 boss: 5 colour planes per frame and ONE cookie mask for the lot
 * (LAB_82E0 / $7FE2 hand LAB_981C a fixed A3) -- frame 7 is the dead hull */
{ "STAGE BOSS HULL",         BS_SPR_HOSTILE, 0x09, S2|S3, 0x2E4C0, 0x35CC0, 9, 48, 5,  8, 0 },
{ "STAGE BOSS POD",          BS_SPR_HOSTILE, 0x09, S2|S3, 0x35FC0, 0x37208, 5, 39, 5,  3, 0 },
{ "POP-UP DRONE",            BS_SPR_HOSTILE, 0x0D, S2,   0x37340, 0, 2, 16, 6, 13, 0 },
/* --- objects: one bank per template, in that stage's overlay --- */
{ "LAUNCH PAD",              BS_SPR_OBJECT,  0x01, S0,   0x40500, 0, 4, 32, 5, 10, 0 },
{ "TURRET",                  BS_SPR_OBJECT,  0x0C, S0,   0x2EA80, 0, 3, 48, 5,  9, 0 },
{ "BUILDING A",              BS_SPR_OBJECT,  0x10, S0,   0x3C180, 0, 2, 32, 5,  2, 0 },
{ "BEACON",                  BS_SPR_OBJECT,  0x12, S0,   0x3A100, 0, 2, 32, 5,  6, 0 },
{ "BUILDING B",              BS_SPR_OBJECT,  0x13, S0,   0x3EE80, 0, 2, 32, 5,  2, 0 },
{ "BUILDING C",              BS_SPR_OBJECT,  0x14, S0,   0x3D800, 0, 2, 32, 5,  2, 0 },
{ "MINE A",                  BS_SPR_OBJECT,  0x15, S0,   0x37400, 0, 2, 32, 5,  2, 0 },
{ "MINE B",                  BS_SPR_OBJECT,  0x16, S0,   0x38A80, 0, 2, 32, 5,  2, 0 },
{ "STAGE GATE",              BS_SPR_OBJECT,  0x17, S0,   0x35B00, 0, 2, 32, 5,  2, 0 },
{ "2-PHASE CANNON",          BS_SPR_OBJECT,  0x04, S1,   0x34F60, 0, 4, 48, 5, 13, 0 },
{ "HATCH GUN",               BS_SPR_OBJECT,  0x0D, S1,   0x3F6E0, 0, 3, 48, 5,  4, 0 },
{ "HATCH SPIN A",            BS_SPR_OBJECT,  0x18, S1,   0x3D660, 0, 1, 16, 5, 13, 0 },
{ "HATCH SPIN B",            BS_SPR_OBJECT,  0x19, S1,   0x3E6A0, 0, 1, 16, 5, 13, 0 },
{ "EMERG TURRET",            BS_SPR_OBJECT,  0x02, S2,   0x3A280, 0, 3, 64, 5, 13, 0 },
{ "SILO",                    BS_SPR_OBJECT,  0x0E, S2,   0x4EB00, 0, 3, 48, 5,  6, 0 },
{ "BEACON S2",               BS_SPR_OBJECT,  0x11, S2,   0x37D00, 0, 2, 48, 5,  3, 0 },
{ "BUNKER",                  BS_SPR_OBJECT,  0x00, S3,   0x3A000, 0, 4, 64, 5, 10, 0 },
{ "RISING TURRET",           BS_SPR_OBJECT,  0x03, S3,   0x32800, 0, 4, 64, 5,  6, 0 },
{ "CRATE",                   BS_SPR_OBJECT,  0x05, S3,   0x2E840, 0, 2, 32, 5,  4, 0 },
{ "BLOCK",                   BS_SPR_OBJECT,  0x0F, S3,   0x30640, 0, 2, 48, 5,  2, 0 },
};
#undef S0
#undef S1
#undef S2
#undef S3
#undef SALL

int bs_sprite_count(void) { return (int)(sizeof SPRITES / sizeof SPRITES[0]); }
const BsSprite *bs_sprite(int i)
{ return (i < 0 || i >= bs_sprite_count()) ? NULL : &SPRITES[i]; }

int bs_sprite_bob(const BsData *d, int i, BsBob *b)
{
    const BsSprite *s = bs_sprite(i);
    (void)d;
    if (!s) return -1;
    int row_bytes = (s->kind == BS_SPR_HOSTILE) ? 2 * s->w_words - 2 : 2 * s->w_words;
    bob_fill(b, s->gfx, s->w_words, s->h, row_bytes, s->planes_per_frame);
    if (s->kind == BS_SPR_OBJECT) { b->mask = 0; b->mask_stride = 0; }
    else if (s->mask) { b->mask = s->mask; b->mask_stride = 0; }   /* one mask shared by every frame */
    b->frames = s->frames;
    b->cloak = s->cloak;
    if (s->cloak) { b->mask = s->gfx; b->planes = s->gfx; b->mask_stride = b->frame_stride; }
    return 0;
}

/* ---------- hardware sprites ---------- */
uint32_t bs_hwsprite_ptr(const BsData *d, int index)
{
    return bs_l(d, 0xC6B6 + (uint32_t)index * 4);
}

void bs_hwsprite(const BsData *d, uint32_t ptr, int h, uint8_t *idx)
{
    for (int y = 0; y < h; y++) {
        uint16_t a = bs_w(d, ptr + (uint32_t)y * 4);
        uint16_t b = bs_w(d, ptr + (uint32_t)y * 4 + 2);
        for (int x = 0; x < 16; x++) {
            uint8_t c = (uint8_t)(((a >> (15 - x)) & 1) | (((b >> (15 - x)) & 1) << 1));
            idx[y * 16 + x] = c;
        }
    }
}

/* ---------- font ---------- */
const uint8_t *bs_glyph(const BsData *d, int ascii)
{
    return d->chip + 0x10550 + (uint32_t)ascii * 10;
}

/* ---------- sfx ---------- */
int bs_sfx_desc(const BsData *d, int n, BsSfx *s, int *channel)
{
    if (channel) *channel = (n >> 4) & 3;
    uint32_t a = 0x2539C + (uint32_t)(n & 15) * 12;
    s->ptr = bs_l(d, a);
    s->len_words = bs_w(d, a + 4);
    s->period = bs_w(d, a + 6);
    s->volume = (uint8_t)bs_w(d, a + 8);
    s->dur = d->chip[a + 11];
    return s->ptr && s->len_words ? 0 : -1;
}
