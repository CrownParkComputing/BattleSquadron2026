/* render.c -- Battle Squadron frame renderer (no blitter, no copper: the same
 * model drawn directly from the engine state + chip image).
 *
 * Sources of truth: re/ENGINE_frame_player.md §1/§3 (scroll, ring, sprite
 * space), re/ASSETS.md §1-§5 (formats), engine.c render_list (bobs emitted in
 * the original blit order).  Screen mapping (verified against re/trace/shots
 * with tools/framecmp): screen x = X - g.cam7204, screen y = Y - 0x100; the
 * 352x288 oracle PPMs carry the visible window at offset (32,12), 288 x 255.
 *
 * Sprite colours: the in-game copper list (chip_2700.bin $B29E/$BB1A..$BC02)
 * holds the values the HUD/loader code writes mid-frame; the constants below
 * are those measured values (documented in PROJECT.md).
 */
#include <stdio.h>
#include <string.h>
#include "engine/engine.h"
#include "render.h"
#include <stdlib.h>

/* empirical sprite-position calibration vs the oracle shots (framecmp); env
 * BS_FX_DX.. override while calibrating */
static int cloak_adj_x = 1, cloak_adj_y;   /* +1: the capture blit's word-align shift lands one pixel right (measured: d13500 332->146, i_18000 147->76) */
static int fx_dx = -2, fx_dy = 1, ship_dx = -1, ship_dy = -2, shot_dx = -1, shot_dy = -2;
static void calib(void)
{
    static int done;
    const char *e;
    if (done) return;
    done = 1;
    if ((e = getenv("BS_FX_DX"))) fx_dx = atoi(e);
    if ((e = getenv("BS_FX_DY"))) fx_dy = atoi(e);
    if ((e = getenv("BS_SHIP_DX"))) ship_dx = atoi(e);
    if ((e = getenv("BS_SHIP_DY"))) ship_dy = atoi(e);
    if ((e = getenv("BS_SHOT_DX"))) shot_dx = atoi(e);
    if ((e = getenv("BS_CLOAK_DX"))) cloak_adj_x = atoi(e);
    if ((e = getenv("BS_CLOAK_DY"))) cloak_adj_y = atoi(e);
    if ((e = getenv("BS_SHOT_DY"))) shot_dy = atoi(e);
}

int render_hiscore = 1000000;

static uint32_t pal_rgba[32];            /* current frame palette */
static int cur_stage = -1;

static uint32_t rgb12(uint16_t v)
{
    uint8_t r = (uint8_t)(((v >> 8) & 15) * 17);
    uint8_t g8 = (uint8_t)(((v >> 4) & 15) * 17);
    uint8_t b = (uint8_t)((v & 15) * 17);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)g8 << 8) | r;
}

/* stage-0 replacement palettes after n inner stages (LAB_7336) */
static uint32_t stage0_pal_addr(void)
{
    /* LAB_7336 runs only when the game copper is rebuilt (game start, stage-clear
     * return).  The demo patches hangars4099=$0E WITHOUT a rebuild, so the
     * attract runs on the base palette. */
    if (g.demo) return 0x14F6;
    int n = 0;
    for (int b = 0; b <= 3; b++) if (g.hangars4099 & (1 << b)) n++;
    static const uint32_t tab[4] = { 0x14F6, 0x183A, 0x187A, 0x18BA };
    return tab[n];
}

void render_stage(const BsData *d)
{
    (void)d;
    cur_stage = g.stage7228;
}

static void latch_palette(void)
{
    uint32_t base = (g.stage7228 == 0) ? stage0_pal_addr()
                                       : g.stage_desc7224 + 12;
    if (g.stage7228 == 3 && !g_27618 &&
        g.progress7206 >= 0x79E && g.progress7206 < 0xFA0)
        base = 0x171A;                   /* LAB_1420 stage-3 mid-section palette */
    uint16_t pal[32];
    for (int i = 0; i < 32; i++) pal[i] = cw(base + (uint32_t)i * 2);
    if (g_27618 & 1) {                   /* LAB_1454: alt/base strobe while the flash counts down */
        uint32_t alt = g.stage_desc7224 + 0x4C;
        for (int i = 0; i <= 16; i++) pal[i] = cw(alt + (uint32_t)i * 2);
        pal[20] = cw(alt + 40); pal[24] = cw(alt + 48); pal[28] = cw(alt + 56);
    }
    if (g.msg8514)                       /* LAB_A30E head: colour-3 of all four sprite banks = $0600 --
                                            entries 19/23/27/31 are shared with the playfield */
        pal[19] = pal[23] = pal[27] = pal[31] = 0x600;
    for (int i = 0; i < 32; i++) pal_rgba[i] = rgb12(pal[i]);
}

const uint32_t *render_palette(void) { return pal_rgba; }

/* ---------------- terrain ---------------- */
/* Row exposure model (same maths as engine.c playfield_blank / LAB_9C44):
 * screen row sy shows the map row whose exposure progress is q = g7206 - sy;
 * map row address = $4A000 - (((q-1)>>4)+1)*$30, pixel row inside the 16-px
 * tile = 15 - ((q-1)&15).  Stage 0 loops: q wraps modulo 8192. */
/* single-pixel sample of the terrain (no nova shake: blitter reads the real
 * playfield): fx = field x (cam-space, 0..384), sy = screen row */
static uint32_t terrain_px(int fx, int sy)
{
    int32_t q = (int32_t)g.progress7206 - sy;
    while (q < 1) q += 8192;
    uint32_t maprow = 0x4A000 - (uint32_t)(((q - 1) >> 4) + 1) * 0x30;
    while (maprow < 0x44000) maprow += 0x6000;
    int tile_row = 15 - ((q - 1) & 15);
    int col = fx >> 4;
    if (col < 0 || col >= 24) return pal_rgba[0];
    uint16_t word = cw(maprow + (uint32_t)col * 2);
    uint32_t tb = 0x4A000 + (uint32_t)word * 2 + (uint32_t)tile_row * 2;
    int bit = 15 - (fx & 15);
    int c = 0;
    for (int p = 0; p < 5; p++)
        c |= ((cw(tb + (uint32_t)p * 0x20) >> bit) & 1) << p;
    return pal_rgba[c];
}

/* LAB_5B3E is NOT a smoke paint: it is a background CAPTURE blit (BLTCON0
 * $09F0, minterm F0 = D := A) -- it copies the playfield under (x,y) into the
 * $577E buffer, and LAB_9814 then draws the missile mask filled with that
 * capture: the type-8 homing missile is CLOAKED (a refraction of the terrain,
 * offset by however far the capture point is from the draw point -- the demo
 * parade captures at (x-2,y-2), so the parade missile shows as a 2px-shifted
 * ghost of the map).  We record the capture positions per engine frame and
 * sample the terrain at render time. */
static struct { int16_t x, y; } cloak_cap[16];
static int cloak_n;
static uint32_t cloak_frame;
void paint_smoke(int16_t x, int16_t y, uint32_t puff_mask)
{
    (void)puff_mask;
    if (cloak_frame != g.frame_no) { cloak_frame = g.frame_no; cloak_n = 0; }
    if (cloak_n < 16) { cloak_cap[cloak_n].x = x; cloak_cap[cloak_n].y = y; cloak_n++; }
}

/* draw a $577E entry: missile mask cookie-cut, filled with the terrain sampled
 * at the capture point (nearest queued capture to this record) */
static void draw_cloak(uint32_t *rgba, const RenderEntry *r)
{
    int capx = r->x, capy = r->y;
    int best = -1;
    for (int i = 0; i < cloak_n; i++) {
        int d = abs(cloak_cap[i].x - r->x) + abs(cloak_cap[i].y - r->y);
        if (best < 0 || d < best) { best = d; capx = cloak_cap[i].x; capy = cloak_cap[i].y; }
    }
    int sx = r->x - g.cam7204, sy = r->y - 0x100;
    int cfx = capx - 0x100, csy = capy - 0x100;      /* field x / screen row of the capture */
    int words = r->w_words - 1;
    if (words <= 0 || r->h <= 0) return;
    for (int y = 0; y < r->h; y++) {
        int dy = sy + y;
        if (dy < 0 || dy >= BS_VIEW_H) continue;
        uint32_t *out = rgba + (size_t)dy * BS_VIEW_W;
        for (int wx = 0; wx < words; wx++) {
            uint16_t mk = cw(r->mask + (uint32_t)(y * 2 * words + wx * 2));
            for (int bit = 15; bit >= 0; bit--) {
                if (!((mk >> bit) & 1)) continue;
                int px = wx * 16 + (15 - bit);
                int dx = sx + px;
                if (dx < 0 || dx >= BS_VIEW_W) continue;
                out[dx] = terrain_px(cfx + px + cloak_adj_x, csy + y + cloak_adj_y);
            }
        }
    }
}

static void draw_terrain(uint32_t *rgba)
{
    int camx = g.cam7204 - 0x100;        /* 0..96+: window offset into the 384-px field */
    if (g.nova25334 && !(g.dframe & 2))  /* LAB_9E32 screen shake */
        camx += (g.nova25334 >= 0xBA) ? 3 : ((g.nova25334 - 0x8A) >> 4);
    for (int sy = 0; sy < BS_VIEW_H; sy++) {
        int32_t q = (int32_t)g.progress7206 - sy;
        while (q < 1) q += 8192;         /* stage-0 loop; pre-start rows show the wrap */
        uint32_t maprow = 0x4A000 - (uint32_t)(((q - 1) >> 4) + 1) * 0x30;
        while (maprow < 0x44000) maprow += 0x6000;    /* 512-row wrap */
        int tile_row = 15 - ((q - 1) & 15);
        uint32_t *out = rgba + (size_t)sy * BS_VIEW_W;
        int cx = camx;
        int sx = 0;
        while (sx < BS_VIEW_W) {
            int col = cx >> 4;
            if (col < 0 || col >= 24) { out[sx++] = pal_rgba[0]; cx++; continue; }
            uint16_t word = cw(maprow + (uint32_t)col * 2);
            uint32_t tb = 0x4A000 + (uint32_t)word * 2 + (uint32_t)tile_row * 2;
            uint16_t pl[5];
            for (int p = 0; p < 5; p++) pl[p] = cw(tb + (uint32_t)p * 0x20);
            int bit = 15 - (cx & 15);
            while (bit >= 0 && sx < BS_VIEW_W) {
                int c = 0;
                for (int p = 0; p < 5; p++) c |= ((pl[p] >> bit) & 1) << p;
                out[sx++] = pal_rgba[c];
                cx++; bit--;
            }
        }
    }
}

/* ---------------- bobs (render_list) ---------------- */
void render_bob(uint32_t *rgba, const RenderEntry *r)   /* the production bob blit; tools/spritecheck.c diffs the browser decode against it */
{
    int sx = r->x - g.cam7204, sy = r->y - 0x100;
    int hostile = (r->kind == 2);
    int w = hostile ? (r->w_words - 1) * 16 : r->w_words * 16;
    int words = hostile ? r->w_words - 1 : r->w_words;
    int ps = r->stride ? r->stride : 2 * words * r->h;   /* per-plane bytes: the FULL frame's plane size */
    if (w <= 0 || r->h <= 0) return;
    for (int y = 0; y < r->h; y++) {
        int dy = sy + y;
        if (dy < 0 || dy >= BS_VIEW_H) continue;
        uint32_t *out = rgba + (size_t)dy * BS_VIEW_W;
        for (int wx = 0; wx < words; wx++) {
            uint32_t off = (uint32_t)(y * 2 * words + wx * 2);
            uint16_t pl[5], mk;
            for (int p = 0; p < 5; p++) pl[p] = cw(r->gfx + (uint32_t)p * (uint32_t)ps + off);
            mk = hostile ? cw(r->mask + off) : 0xFFFF;
            if (r->reveal != 0xFFFFFFFFu && wx < 2)          /* $76E8 horizontal-reveal AND (32 bits) */
                mk &= (uint16_t)(r->reveal >> (wx ? 0 : 16));
            for (int bit = 15; bit >= 0; bit--) {
                int dx = sx + wx * 16 + (15 - bit);
                if (dx < 0 || dx >= BS_VIEW_W) continue;
                if (!((mk >> bit) & 1)) continue;
                int c = 0;
                for (int p = 0; p < 5; p++) c |= ((pl[p] >> bit) & 1) << p;
                out[dx] = pal_rgba[c];
            }
        }
    }
}

/* ---------------- hardware sprites ---------------- */
/* Measured in-game sprite colour banks (chip_2700 copper, see header). */
/* LAB_1FAE colour cycle: effect sprite colours from $1F1E (or $1F4E in nova),
 * entry = HUD-frame & $E, columns at +0/+16/+32 bytes. */
static void effect_bank(uint16_t bank[3])
{
    int pf = (g.dframe & 1) ? g.dframe - 2 : g.dframe - 1;   /* last LAB_1FAE frame */
    uint32_t t = (g.nova25334 ? 0x1F4E : 0x1F1E) + (uint32_t)(pf & 0xE);
    bank[0] = cw(t); bank[1] = cw(t + 16); bank[2] = cw(t + 32);
}
static const uint16_t BANK_DEATH[3]  = { 0xFFD, 0xF70, 0xB40 };   /* death flash (approx) */

/* LAB_28F0: ship colours = $2178 row (weapon+6), row 10 while the nova runs;
 * shot colours = $272E + hud-colour row, animated by dframe&6 (exact). */
static void ship_bank(const Player *p, uint16_t bank[3])
{
    /* LAB_28F0/29E8 via the copper: the ship band colours are the animated
     * weapon colours ($272E row = weapon, phase = HUD-update frame & 6), or
     * the grey invulnerability cycle ($27AE/$27BE) while +52 runs. */
    int pf = g.dframe - 1;               /* the frame the HUD copper was last built on */
    if (p->invuln52 && ((uint16_t)p->invuln52 >= 0x32 || (pf & 1))) {
        int idx = (pf & 0xE) >> 1;
        bank[0] = 0xFFD;
        bank[1] = cw(0x27AE + (uint32_t)idx * 2);
        bank[2] = cw(0x27BE + (uint32_t)idx * 2);
        return;
    }
    if (p->nova90) {                     /* nova: row 10 of $2178 */
        bank[0] = cw(0x2178 + 10 * 16); bank[1] = cw(0x2178 + 10 * 16 + 4); bank[2] = cw(0x2178 + 10 * 16 + 8);
        return;
    }
    uint32_t a = 0x272E + (uint32_t)(p->weapon58 & 3) * 32 + (uint32_t)(pf & 6) * 4;
    bank[0] = cw(a); bank[1] = cw(a + 2); bank[2] = cw(a + 4);
}
static void shot_bank(const Player *p, uint16_t bank[3])
{
    uint32_t a = 0x272E + (uint32_t)(p->weapon58 & 3) * 32 + (uint32_t)((g.dframe - 1) & 6) * 4;
    bank[0] = cw(a); bank[1] = cw(a + 2); bank[2] = cw(a + 4);
}

/* one 16-px hardware sprite image: 2 planes interleaved, 4 bytes per row */
static void draw_hwsprite(uint32_t *rgba, uint32_t ptr, int h, int sx, int sy,
                          const uint16_t bank[3])
{
    if (!ptr || ptr >= 0x80000) return;
    for (int y = 0; y < h; y++) {
        int dy = sy + y;
        if (dy < 0 || dy >= BS_VIEW_H) continue;
        uint16_t a = cw(ptr + (uint32_t)y * 4);
        uint16_t b = cw(ptr + (uint32_t)y * 4 + 2);
        uint32_t *out = rgba + (size_t)dy * BS_VIEW_W;
        for (int bit = 15; bit >= 0; bit--) {
            int c = ((a >> bit) & 1) | (((b >> bit) & 1) << 1);
            if (!c) continue;
            int dx = sx + (15 - bit);
            if (dx < 0 || dx >= BS_VIEW_W) continue;
            out[dx] = rgb12(bank[c - 1]);
        }
    }
}

static uint32_t sprite_ptr(int idx)      /* table $C6B6 + idx*4 */
{
    if (idx < 0 || idx > 0x7F) return 0;
    return cl(0xC6B6 + (uint32_t)idx * 4);
}

static void draw_effects(uint32_t *rgba)
{
    for (int i = 0; i < 16; i++) {
        const Effect *e = &g.effects[i];
        int16_t xw = (int16_t)(e->x >> 16), yw = (int16_t)(e->y >> 16);
        if (!xw) continue;
        if (yw < 0xF6 || yw >= 0x200) continue;
        int frame = e->frame;
        if (e->age == 0) frame += (g.dframe & 4) ? 0 : 1;    /* bullet flicker */
        int h = (frame >= 0x58 && frame < 0x60) ? 7 : (frame >= 0x60 && frame < 0x80) ? 12 : 16;
        /* bullet images ($C6B6 slots $58/$59) are 28 bytes = 7 rows apart: an
         * 8th row reads the next image's first row = a spurious line under
         * every enemy bullet */
        uint16_t bank[3];
        effect_bank(bank);
        draw_hwsprite(rgba, sprite_ptr(frame), h, xw - g.cam7204 + fx_dx, yw - 0x100 + fx_dy,
                      bank);
    }
}

static void draw_shots(uint32_t *rgba, const Player *p)
{
    uint16_t sb[3], shipb[3];
    shot_bank(p, sb);
    ship_bank(p, shipb);
    for (int i = 0; i < 12; i++) {
        const Shot *s = &p->shots[i];
        if (!s->x || s->b10) continue;   /* free / still relative to the ship */
        /* LAB_28F0 colour banding: COLOR25-27 hold the animated shot colours
         * above the ship's raster row and the ship row from there down */
        /* LAB_5410 emit: D4 = +8 = HEIGHT, D5 = +9 = gfx index (they are in
         * that order in the record; the first draft had them swapped, which
         * hid every player bullet whose $C6B6 slot at +8 happened to be 0) */
        if (!s->b8) continue;            /* height 0: the weapon tables' filler slots */
        const uint16_t *bank = (p->state38 == 0 && s->y >= p->sy) ? shipb : sb;
        draw_hwsprite(rgba, sprite_ptr(s->b9), s->b8,
                      s->x - g.cam7204 + shot_dx, s->y - 0x100 + shot_dy, bank);
    }
}

static void draw_ship(uint32_t *rgba, const Player *p)
{
    if (!p->joined39 || p->state38 >= 0xC8) return;
    int sx = p->x - 0x100 + ship_dx, sy = p->y - 0x100 + ship_dy;
    if (p->state38 == SHIP_EXPLODING) {  /* dying ship $13190 + phase*$1E0 */
        int phase = (0x46 - p->explode49) / 10;
        if (phase < 0) phase = 0;
        if (phase > 6) phase = 6;
        uint32_t base = 0x13190 + (uint32_t)phase * 0x1E0;
        draw_hwsprite(rgba, base, 60, sx, sy, BANK_DEATH);
        draw_hwsprite(rgba, base + 0xF0, 60, sx + 16, sy, BANK_DEATH);
        return;
    }
    /* banking frame 0..12 -> image pair from the $C6B6 table ($10000 + idx*$78) */
    uint16_t bank[3];
    ship_bank(p, bank);
    int idx = p->bank10;
    if (idx < 0) idx = 0;
    if (idx > 12) idx = 12;
    draw_hwsprite(rgba, sprite_ptr(idx), p->height8 ? p->height8 : 30, sx, sy, bank);
    draw_hwsprite(rgba, sprite_ptr(idx + 1), p->height8 ? p->height8 : 30, sx + 16, sy, bank);
}

/* ---------------- font / HUD ---------------- */
static void draw_glyph(uint32_t *rgba, int x, int y, int ch, uint32_t colour)
{
    for (int r = 0; r < 9; r++) {
        int dy = y + r;
        if (dy < 0 || dy >= BS_VIEW_H) continue;
        uint8_t b = cb(0x10550 + (uint32_t)(ch & 0x7F) * 10 + (uint32_t)r);
        for (int bit = 7; bit >= 0; bit--) {
            if (!((b >> bit) & 1)) continue;
            int dx = x + (7 - bit);
            if (dx < 0 || dx >= BS_VIEW_W) continue;
            rgba[(size_t)dy * BS_VIEW_W + dx] = colour;
        }
    }
}

void render_text(uint32_t *rgba, int x, int y, const char *s, uint32_t colour)
{
    for (; *s; s++, x += 8) {
        int ch = *s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch == ' ') continue;
        draw_glyph(rgba, x + 1, y + 1, ch, 0xFF000000u);   /* drop shadow (LAB_43F2) */
        draw_glyph(rgba, x, y, ch, colour);
    }
}

/* ---------------- title screen (LODINT picture) ----------------
 * Layout MEASURED from the copper list the idle oracle displays at the title
 * (list at chip $AFB6, host frame 36000): 5 bitplanes at $62000 + p*8000
 * (320 x 200, modulo 0), i.e. the whole LODINT module IS the picture --
 * logo, F-key icons, PRESS BUTTON TO START and the INNERPRISE line included.
 * 32 colours are loaded at the top of the frame (palette A), replaced from
 * picture row 86 (copper WAIT vpos $96, palette B) and entries 6/16/28..31
 * restored to their A values from row 184 (WAIT vpos $F8).  Verified by
 * tools/titlecmp: 98.2% of the host title frame is byte-identical, the rest
 * is the two dynamic menu bands the title code draws at runtime. */
static const uint16_t title_pal_a[32] = {
    0x000, 0x670, 0x560, 0x450, 0x340, 0x230, 0x120, 0x010,
    0xCCC, 0xC66, 0xB55, 0xA44, 0x833, 0x622, 0x411, 0x300,
    0x200, 0x79F, 0x47F, 0x35B, 0x338, 0x226, 0x114, 0x003,
    0x9AF, 0x890, 0x831, 0x621, 0xB80, 0xB40, 0xB20, 0xB52,
};
static const uint16_t title_pal_b[32] = {
    0x000, 0xBB9, 0x997, 0x886, 0x775, 0x553, 0x331, 0x110,
    0xCCC, 0xABA, 0x898, 0x676, 0x565, 0x454, 0x232, 0x121,
    0xDA4, 0x960, 0x999, 0x666, 0x444, 0x333, 0x222, 0x111,
    0xABF, 0x68D, 0x258, 0x124, 0xC88, 0xC33, 0x922, 0x611,
};

void render_title(uint32_t *rgba)                     /* BS_TITLE_W x BS_TITLE_H */
{
    static const uint8_t restore[] = { 6, 16, 28, 29, 30, 31 };
    uint32_t pal[3][32];
    for (int i = 0; i < 32; i++) {
        pal[0][i] = rgb12(title_pal_a[i]);
        pal[1][i] = pal[2][i] = rgb12(title_pal_b[i]);
    }
    for (size_t i = 0; i < sizeof restore; i++)
        pal[2][restore[i]] = pal[0][restore[i]];
    for (int y = 0; y < BS_TITLE_H; y++) {
        const uint32_t *p = pal[y < 86 ? 0 : y < 184 ? 1 : 2];
        for (int x = 0; x < BS_TITLE_W; x++) {
            unsigned v = 0;
            for (int pl = 0; pl < 5; pl++) {
                uint8_t b = cb(0x62000u + (uint32_t)pl * 8000u + (uint32_t)y * 40u + (uint32_t)(x >> 3));
                v |= (unsigned)((b >> (7 - (x & 7))) & 1) << pl;
            }
            rgba[(size_t)y * BS_TITLE_W + x] = p[v];
        }
    }
}

/* title-canvas text: same chip font, 320-wide stride, no uppercasing (the
 * measured "on/off" labels; colours from the host title frame) */
void render_title_text(uint32_t *rgba, int x, int y, const char *s, uint32_t colour, int step)
{
    for (; *s; s++, x += step) {
        int ch = *s;
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch == ' ') continue;
        for (int r = 0; r < 9; r++) {
            uint8_t b = cb(0x10550 + (uint32_t)(ch & 0x7F) * 10 + (uint32_t)r);
            for (int bit = 7; bit >= 0; bit--) {
                if (!((b >> bit) & 1)) continue;
                int dx = x + (7 - bit), dy = y + r;
                if (dx < 0 || dx >= BS_TITLE_W - 1 || dy < 0 || dy >= BS_TITLE_H - 1) continue;
                rgba[(size_t)(dy + 1) * BS_TITLE_W + dx + 1] = 0xFF222222u;
                rgba[(size_t)dy * BS_TITLE_W + dx] = colour;
            }
        }
    }
}

static long score_of(const Player *p)
{
    long v = 0;
    for (int i = 0; i < 8; i++) {
        char c = p->score[i];
        v = v * 10 + ((c >= '0' && c <= '9') ? c - '0' : 0);
    }
    return v;
}

static void draw_hud(uint32_t *rgba)
{
    const uint32_t gold = rgb12(0xDA4), white = rgb12(0xFFF), red = rgb12(0xC33);
    char buf[16];
    /* 1UP block */
    render_text(rgba, 24, 2, "1UP", gold);
    for (int i = 0; i < g.players[0].nova66 && i < 8; i++)      /* nova hearts */
        draw_glyph(rgba, 52 + i * 6, 2, '.', red);
    snprintf(buf, sizeof buf, "%d", g.players[0].lives56);
    render_text(rgba, 52 + 8 * 6, 2, buf, gold);
    memcpy(buf, g.players[0].score, 8); buf[8] = 0;
    render_text(rgba, 24, 12, buf, white);
    /* HIGH block */
    long hi = render_hiscore;
    if (score_of(&g.players[0]) > hi) hi = score_of(&g.players[0]);
    if (score_of(&g.players[1]) > hi) hi = score_of(&g.players[1]);
    if (g.demo) {
        /* LAB_45D2: the attract centre label, 8 chars of "  DEMO   SCORES
         * POINTS  CREDITS" by phase, drawn while (dframe & $1F) < $18 */
        static const char *lab[4] = { "  DEMO  ", " SCORES ", " POINTS ", " CREDITS" };
        int ph = g.demo_frames < 0x5DC ? 0 : g.demo_frames < 0x7D0 ? 1 :
                 g.demo_frames < 0xBB8 ? 2 : 3;
        if ((g.dframe & 0x1F) < 0x18) render_text(rgba, 112, 2, lab[ph], gold);
    } else
    render_text(rgba, 128, 2, "HIGH", gold);
    snprintf(buf, sizeof buf, "%08ld", hi);
    render_text(rgba, 112, 12, buf, white);
    /* 2UP block */
    render_text(rgba, 208, 2, "2UP", gold);
    if (g.players[1].joined39) {
        for (int i = 0; i < g.players[1].nova66 && i < 8; i++)
            draw_glyph(rgba, 236 + i * 6, 2, '.', red);
        snprintf(buf, sizeof buf, "%d", g.players[1].lives56);
        render_text(rgba, 236 + 8 * 6, 2, buf, gold);
    }
    memcpy(buf, g.players[1].score, 8); buf[8] = 0;
    if (!buf[0]) memcpy(buf, "00000000", 9);
    render_text(rgba, 200, 12, buf, white);
}

/* message overlay -- faithful LAB_A30E model.
 *
 * A page ($2E508/$2E582/$2E5E8/$2E676 in LODS0F, $FCE6 in the loader for the
 * hiscore board) is a list of 20-byte records {x.w, y.w, char[16]}, x==0 ends.
 * The loader adds $16 to every y after loading (LAB_1368 / boot $554).  The
 * text is drawn as 8 hardware-sprite columns of 2 chars (font $10550, 8x10,
 * 10 bytes/char); per row r: plane1 = cur | prev>>1, plane2 = prev>>1 & ~cur,
 * so glyph pixels are sprite colour 1 (cycling) and the +1/+1 drop shadow is
 * colour 3 = $0600 (A30E sets the four colour-3 registers each call).
 *
 * Counter 8514 ticks once per display frame (1..hold+$D1):
 *   tick L+1        line L's glyphs are drawn (one line appears per tick)
 *   tick 1          all 8 column positions init to pageX<<6 (6-bit fraction)
 *   ticks > nlines  LAB_A3EC integrates: while 20+12L <= t < 72+12L column k
 *                   adds IN[k] ($A24E, 121..258/tick -- the columns fan out
 *                   rightward from the stacked off-screen pageX to their rest
 *                   spots ~16px apart); after t > hold, while hold+12L <= t <
 *                   hold+60+12L it adds OUT[k] ($A25E) and the line exits right.
 *   displayed x     position>>6 = sprite hpos; canvas x = hpos - $90.
 * Colours: 4 copper WAITs per line at vpos y,y+2,y+4,y+6 load the 4 sprite
 * colour-1 registers (COLOR17/21/25/29 = column pairs) from the $A26E cycle:
 * word at $A26E + (((c>>1) + L*8 + w*2) mod $80) + pair*8 (pair*8 unwrapped,
 * reading past $A2EE like the original). */
static void draw_messages(uint32_t *rgba)
{
    /* the display shows the copper/sprites built by the PREVIOUS A30E call:
     * at the display hook 8514 has already been bumped for this half, so the
     * on-screen state corresponds to counter-1 (verified against mid-slide
     * oracle dumps: bias -1 = 0.4% mismatch, 0 = 2.1%) */
    int c = g.msg8514 - 1;
    if (c <= 0 || !g.msg_text8518) return;
    int nl = g.msg_lines8516, hold = g.msg_hold8522;
    uint32_t page = g.msg_text8518;
    uint32_t shadow = rgb12(0x600);
    for (int L = 0; L < nl && c >= L + 1; L++) {
        uint32_t rec = page + (uint32_t)L * 20;
        uint16_t px0 = cw(rec);
        if (px0 == 0) break;
        int y = (int16_t)cw(rec + 2) + 0x16 - 0x26;           /* +$16 load patch, vpos->canvas */
        for (int k = 0; k < 8; k++) {
            uint16_t v = (uint16_t)(px0 << 6);
            {                                                 /* slide-in ticks applied so far */
                int a = 20 + 12 * L, b = 72 + 12 * L - 1;
                if (a < nl + 1) a = nl + 1;
                if (b > c) b = c;
                if (b > hold) b = hold;
                if (b >= a) v = (uint16_t)(v + (b - a + 1) * (int16_t)cw(0xA24E + (uint32_t)k * 2));
            }
            {                                                 /* slide-out ticks applied so far */
                int a = hold + 12 * L, b = hold + 60 + 12 * L - 1;
                if (a < hold + 1) a = hold + 1;
                if (b > c) b = c;
                if (b >= a) v = (uint16_t)(v + (b - a + 1) * (int16_t)cw(0xA25E + (uint32_t)k * 2));
            }
            int cx = (int)(v >> 6) - 0x90;
            if (cx <= -16 || cx >= BS_VIEW_W) continue;
            uint8_t ch[2] = { cb(rec + 4 + (uint32_t)k * 2), cb(rec + 5 + (uint32_t)k * 2) };
            for (int r = 0; r < 10; r++) {
                int dy = y + r;
                if (dy < 0 || dy >= BS_VIEW_H) continue;
                int w = (r >= 6) ? 3 : (r >> 1);
                uint32_t cbase = 0xA26E + (uint32_t)(((((int)g.dframe >> 1) & 0x7E) + L * 8 + w * 2) & 0x7F)
                               + (uint32_t)(k >> 1) * 8;
                uint32_t col1 = rgb12(cw(cbase));
                uint32_t *out = rgba + (size_t)dy * BS_VIEW_W;
                for (int half = 0; half < 2; half++) {
                    uint32_t fp = 0x10550 + (uint32_t)ch[half] * 10 + (uint32_t)r;
                    uint8_t cur = cb(fp), prev = cb(fp - 1);
                    uint8_t p1 = (uint8_t)(cur | (prev >> 1));
                    uint8_t p2 = (uint8_t)((prev >> 1) & (uint8_t)~cur);
                    for (int bit = 7; bit >= 0; bit--) {
                        if (!((p1 >> bit) & 1)) continue;
                        int dx = cx + half * 8 + (7 - bit);
                        if (dx < 0 || dx >= BS_VIEW_W) continue;
                        out[dx] = ((p2 >> bit) & 1) ? shadow : col1;
                    }
                }
            }
        }
    }
}

void render_frame(uint32_t *rgba, int layers)
{
    calib();
    latch_palette();
    if (layers & BS_L_TERRAIN) draw_terrain(rgba);
    else for (size_t i = 0; i < (size_t)BS_VIEW_W * BS_VIEW_H; i++) rgba[i] = 0xFF000000u;
    if (layers & BS_L_BOBS)
        for (int i = 0; i < render_count; i++) {
            if (render_list[i].kind == 2 && render_list[i].gfx == 0x577E) {
                draw_cloak(rgba, &render_list[i]);
                continue;
            }
            if (render_list[i].kind == 1 || render_list[i].kind == 2)
                render_bob(rgba, &render_list[i]);
        }
    /* while a message page is up LAB_A30E owns ALL 8 sprite channels (streams
     * at $5E000 hold only the text sections), so effects/shots/ships and the
     * type-8 exhaust-puff sprite are not displayable */
    if ((layers & BS_L_SPRITES) && !g.msg8514) {
        draw_effects(rgba);
        for (int p = 1; p >= 0; p--) {
            if (!g.players[p].joined39) continue;
            draw_shots(rgba, &g.players[p]);
            draw_ship(rgba, &g.players[p]);
        }
    }
    if (layers & BS_L_MSG) draw_messages(rgba);
    if (layers & BS_L_HUD) draw_hud(rgba);
}
