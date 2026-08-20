/* bsdata.h -- native decoder for the Battle Squadron game data (re/ASSETS.md).
 *
 * One bs_open() materialises the WHDLoad install's overlays into a 512 KiB
 * "chip image" at the game's own addresses (LOADER at $100, LODDAT at $10000,
 * LODGAM at $246F0), exactly like the original loader; bs_load_stage() adds a
 * stage's graphics/map/tiles (LODS0F+LODS0S+LODS0T or LODSTn).  All typed
 * accessors decode straight from the image, so chip addresses the engine
 * carries in records stay valid.  Containers: src/overlay.c (module table at
 * LOADER+$1880) + src/bond.c (BOND depacker, C port of $AB46).
 */
#ifndef BS_DATA_H
#define BS_DATA_H
#include <stdint.h>
#include <stddef.h>
#include "overlay.h"

#define BS_CHIP_SIZE 0x80000

typedef struct {
    uint8_t chip[BS_CHIP_SIZE];
    BsModule mods[BS_MODULE_COUNT_MAX];
    size_t nmods;
    char dir[512];
    int stage;                          /* currently resident stage, -1 = none */
} BsData;

int  bs_open(BsData *d, const char *data_dir);      /* LOADER + LODDAT + LODGAM; returns 0 on success */
int  bs_load_stage(BsData *d, int stage);           /* 0..3 */
int  bs_load_module(BsData *d, const char *name);   /* any overlay by name */
const BsModule *bs_find_module(const BsData *d, const char *name);

/* big-endian accessors on the image */
static inline uint8_t  bs_b(const BsData *d, uint32_t a) { return d->chip[a]; }
static inline uint16_t bs_w(const BsData *d, uint32_t a) { return (uint16_t)((d->chip[a] << 8) | d->chip[a + 1]); }
static inline uint32_t bs_l(const BsData *d, uint32_t a) { return ((uint32_t)bs_w(d, a) << 16) | bs_w(d, a + 2); }
static inline void bs_rgb12(uint16_t v, uint8_t *r, uint8_t *gg, uint8_t *b)
{ *r = (uint8_t)(((v >> 8) & 15) * 17); *gg = (uint8_t)(((v >> 4) & 15) * 17); *b = (uint8_t)((v & 15) * 17); }

/* terrain: map = 512 rows x 24 words at $44000 (row 0 = level END; the game
 * starts at $49FD0 and walks -$30/strip); map word = byte offset/2 into the
 * tile strip at $4A000 (tile = 5 planes x 32 bytes, 16x16 px). */
void bs_tile(const BsData *d, uint16_t map_word, uint8_t out[16 * 16]);          /* indices 0..31 */
void bs_map_render(const BsData *d, uint8_t *idx, int stride);                   /* 384 x 8192 indices */
void bs_palette(const BsData *d, int stage, uint16_t rgb12[32]);                 /* $14EA + stage*$8C + 12 */
void bs_palette_alt(const BsData *d, int stage, uint16_t rgb12[32]);             /* +76 (flash) */

/* ---------- bobs ----------
 * A bob is the blitter source the game feeds LAB_981C.  Layout, straight out
 * of LAB_97F8 (`D3 = frame * 46(A4); A2 = 36(A4) + D3; A3 = A2 + 46(A4) - 44(A4)`):
 *
 *   plane_stride  44(A4) "frame_bytes" = row_bytes * h, row_bytes = 2*(w-1)
 *                 for hostiles (the blit's last word is the shift spill) and
 *                 2*w for objects (no shift, no mask)
 *   frame_stride  46(A4): 6 * plane_stride for a 5-plane bob + a separate
 *                 cookie mask, 5 * plane_stride when plane 4 IS the mask (a
 *                 16-colour bob in the top half of the palette -- the stage
 *                 bosses and the mothership do this)
 *   mask          plane + frame_stride - plane_stride, i.e. the LAST chunk
 *   modulo        48(A4) = $30 - 2*w (blitter D modulo; not needed here)
 *
 * The $CD7A / $2B68 descriptors carry h/w/gfx/mask and nothing else -- the
 * strides above are derived, and several handlers OVERRIDE h/w/strides/gfx at
 * run time (type $02 mothership, $09 bosses, $0B flypast).  On top of that a
 * descriptor's gfx pointer usually lands in a STAGE overlay, so it only holds
 * that sprite while the matching stage is resident.  Use the sprite catalog
 * below rather than the raw descriptor: it carries the run-time dimensions and
 * the stage mask, and is regression-checked against the live render list by
 * tools/spritecheck.c. */
typedef struct {
    uint32_t planes, mask;      /* frame-0 plane base / cookie-mask base (mask 0 = opaque) */
    int mask_stride;            /* bytes the mask advances per frame (0 = one mask shared by every frame) */
    int w_words, h;             /* blit width in words / rows */
    int row_bytes;              /* bytes per plane row */
    int plane_stride;           /* 44(A4) frame_bytes */
    int frame_stride;           /* 46(A4) */
    int vis_w;                  /* visible pixels = row_bytes * 8 */
    int frames;                 /* frames in the bank */
    int cloak;                  /* 1 = no colour planes, mask only (type $08 refraction) */
} BsBob;
int  bs_hostile_gfx(const BsData *d, int type, BsBob *b);                        /* $CD7A + type*32, descriptor only */
int  bs_object_gfx(const BsData *d, int tmpl, BsBob *b);                         /* $2B68 + tmpl*48 (opaque) */
void bs_bob_frame(const BsData *d, const BsBob *b, int frame,
                  uint8_t *idx, uint8_t *alpha);                                 /* vis_w x h each; alpha NULL ok */
/* mean mask runs per non-empty row: a real cookie-cut silhouette is ~1-2, a
 * bob decoded out of the wrong (or unloaded) overlay is >= 3.5 */
double bs_bob_silhouette(const BsData *d, const BsBob *b, int frame);
/* share of the frame taken by its single commonest colour index -- an opaque
 * object bob is mostly background (>= 0.25); noise is flat across 32 indices */
double bs_bob_flatness(const BsData *d, const BsBob *b, int frame);

/* ---------- sprite catalog ----------
 * every distinct bob bank the engine ever hands the renderer, with the stages
 * whose overlays actually hold it.  Built from the in-game render list (see
 * tools/bobscan.c) plus the literal set-ups in src/behaviours/hostiles.c. */
enum { BS_SPR_HOSTILE, BS_SPR_OBJECT };
typedef struct {
    const char *name;
    int kind;                   /* BS_SPR_HOSTILE / BS_SPR_OBJECT */
    int type;                   /* hostile type / object template index */
    unsigned stages;            /* bit n set = valid with stage n loaded */
    uint32_t gfx;               /* frame-0 plane base */
    uint32_t mask;              /* 0 = the usual gfx + 5*plane_stride, advancing with the frame;
                                   non-zero = one shared cookie mask at a fixed address (the
                                   stage-2 boss stores 5 colour planes per frame and ONE mask) */
    int w_words, h;             /* run-time dimensions (NOT always the descriptor's) */
    int planes_per_frame;       /* 6 = mask inside each frame, 5 = frames are colour-only */
    int frames;
    int cloak;
} BsSprite;
int  bs_sprite_count(void);
const BsSprite *bs_sprite(int i);
int  bs_sprite_bob(const BsData *d, int i, BsBob *b);   /* 0 ok, -1 bad index */

/* hardware sprites: 16 px wide, 2 planes interleaved per row (4 bytes/row) */
void bs_hwsprite(const BsData *d, uint32_t ptr, int h, uint8_t *idx);            /* 16*h indices 0..3 */
uint32_t bs_hwsprite_ptr(const BsData *d, int index);                            /* table $C6B6 + idx*4 */

/* font $10550 + ascii*10: 8x9 px, byte 9 = pad */
const uint8_t *bs_glyph(const BsData *d, int ascii);

/* sfx: 16 x 12-byte descriptors at $2539C (LODGAM resident) */
typedef struct { uint32_t ptr; uint16_t len_words, period; uint8_t volume, dur; } BsSfx;
int  bs_sfx_desc(const BsData *d, int n, BsSfx *s, int *channel);
#endif
