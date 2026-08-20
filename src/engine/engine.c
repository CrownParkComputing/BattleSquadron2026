/* engine.c -- Battle Squadron native engine core (route B; see re/ENGINE.md).
 *
 * Literal translation of the LOADER main loop LAB_AA0..LAB_CE6 and every
 * engine verb, against ~/BattleSquadron-Amiga/asm/loader.asm (addresses in
 * comments) and the measured specs in re/ENGINE_*.md.  Behaviour handlers live
 * in src/behaviours/{hostiles,objects}.c; this file provides weak stubs so the
 * core links and runs before they land.
 *
 * Everything table-shaped (descriptors $CD7A, templates $2B68, wave lists,
 * scripts, weapon tables $2024/$37B4.., RNG table $17400, thresholds) is read
 * from the chip image `bs_chip` built by bsdata.c, never duplicated here.
 */
#include "engine.h"
#include <string.h>

BsGame g;
uint8_t *bs_chip;
uint8_t g_8414 = 10, g_8413 = 50, g_8397;
int16_t g_14390, g_14388;
uint8_t g_27618;
int16_t g_790A[2][3];
RenderEntry render_list[256];
int render_count;
uint32_t render_reveal_mask = 0xFFFFFFFFu;
void (*eng_sfx_hook)(int n);
void (*eng_display_hook)(void);

void sfx(int n) { if (eng_sfx_hook) eng_sfx_hook(n); }

/* ---------------- RNG $2B1E: byte from the 256-byte table at $17400 (LODDAT), self-incrementing */
uint8_t rng(void)
{
    uint8_t v = cb(0x17400u + g.rng_index);
    g.rng_index++;
    return v;
}

/* ---------------- render list (front-end feed; parity does not read it) */
static void emit(int kind, int x, int y, uint32_t gfx, uint32_t mask, int w_words, int h, int frame)
{
    if (render_count >= 256) return;
    RenderEntry *r = &render_list[render_count++];
    r->kind = kind; r->type = -1; r->x = x; r->y = y; r->gfx = gfx; r->mask = mask;
    r->w_words = w_words; r->h = h; r->frame = frame;
    r->stride = 0; r->reveal = 0xFFFFFFFFu;
}

/* ================= scroll LAB_9C44 ================= */
static void stage_end_9d68(void)                    /* LAB_9D68 */
{
    g.gate4100 = (uint8_t)~g.gate4100;
    g.pending7230 = 0;
    g.done7232 = g.stage7228;
    g.scrolled7222 = 0;
}

void scroll_frame(void)
{
    if (g.hold7234) {                               /* $9C44: stage-3 end delay */
        if (--g.hold7234 == 0) stage_end_9d68();
        return;
    }
    /* camera target from the live ships (byte +38 with bit7 clear = present) */
    int16_t sum = 0; int n = 1;
    if ((int8_t)g.players[0].state38 >= 0) { sum += g.players[0].x - 0x100; n++; }
    if ((int8_t)g.players[1].state38 >= 0) { sum += g.players[1].x - 0x100; n++; }
    int16_t target;
    if (n == 1) target = 0x130;
    else {
        uint16_t t = (uint16_t)sum >> n;            /* LSR.W D3 */
        target = (int16_t)(t + (t >> 1) + 0x100);
    }
    if (target != g.cam7204) g.cam7204 += (target > g.cam7204) ? 1 : -1;    /* 1 px per game frame */
    g.scrolled7222 = 1;
    if (g.hangars4099 == 0x0E && g.progress7206 == 0xF0) {   /* final boss (LODFIN) -- not supported */
        g.final_boss26242 = 0xFF;
        g.scrolled7222 = 0;
        return;
    }
    if (g.hold_a14c ||
        (g.boss_hold1570 < 0 && (g.progress7206 == 0x1FD6 || g.progress7206 == 0x0C1C))) {
        g.scrolled7222 = 0;                          /* boss holds the scroll */
        return;
    }
    g.progress7206++;
    g.rows7218--;
    g.rowphase7212 -= 2;
    if (g.rowphase7212 < 0) {                        /* finished a 16-row tile strip */
        g.rowphase7212 = 30;
        g.maprow7214 -= 0x30;
        if (g.maprow7214 < 0x44000) {                /* map exhausted (512 rows) */
            if (g.stage7228 == 0) {                  /* planet surface LOOPS */
                g.maprow7214 = 0x49FD0;
                g.wave2736 = (g.hangars4099 == 0x0E) ? 0xCF90 : cl(g.stage_desc7224 + 4);
                g.progress7206 = 1;
            } else if (g.stage7228 == 3) {
                g.hold7234 = 0xFA;
                g.scrolled7222 = 0;
                return;
            } else {
                stage_end_9d68();
                return;
            }
        }
    }
    g.ring7208 -= 0x30;
    if (g.ring7208 < 0x62000) { g.ring7208 = 0x64FD0; g.rows7218 = 0xFF; }
    /* the terrain blit itself is the front end's job (bs_tile) */
}

/* ================= object pool ================= */
Object *object_alloc(uint32_t tmpl, int column)     /* $32F4/$332E (see spawner for the pool-full path) */
{
    Object *o = NULL;
    for (int i = 0; i < 18; i++)
        if (g.objects[i].x == 0) { o = &g.objects[i]; break; }
    if (!o) return NULL;
    memset(o, 0, sizeof *o);
    o->slot = (int)(o - g.objects);
    o->tmpl = tmpl;
    o->x = (int16_t)((0x17 - column) * 16 + 0x100);
    o->h6 = (int16_t)cw(tmpl + 6);
    o->w8 = (int16_t)cw(tmpl + 8);
    o->y = (int16_t)(0x100 - o->h6);
    o->f10 = (int16_t)(2 * o->w8 * o->h6);
    o->f26 = (int16_t)(5 * o->f10);
    o->f4 = (int16_t)(0x30 - 2 * o->w8);
    o->gfx12 = cl(tmpl + 12);
    o->f16 = cb(tmpl + 16); o->type = cb(tmpl + 17); o->f18 = cb(tmpl + 18); o->f19 = cb(tmpl + 19);
    o->f20 = (int16_t)cw(tmpl + 20); o->f22 = (int16_t)cw(tmpl + 22);
    o->dmg24 = cb(tmpl + 24); o->f25 = cb(tmpl + 25);
    o->hp28 = (int8_t)cb(tmpl + 28); o->f29 = cb(tmpl + 29); o->f30 = cb(tmpl + 30); o->flags31 = cb(tmpl + 31);
    if (g.two_players2732 == 0)
        o->hp28 = (int8_t)(o->hp28 - (uint8_t)(((uint8_t)(o->hp28 + 1)) >> 2));   /* one player: ~25 % less */
    o->f32 = (int8_t)cb(tmpl + 32); o->f33 = cb(tmpl + 33); o->f34 = cb(tmpl + 34); o->f35 = cb(tmpl + 35);
    o->f36 = cb(tmpl + 36); o->f37 = cb(tmpl + 37);
    o->f42 = 0;
    o->rnd43 = (uint8_t)((rng() & cb(tmpl + 43)) + 1);
    o->score_bcd44 = (int16_t)cw(tmpl + 44); o->sfx46 = cb(tmpl + 46); o->f47 = cb(tmpl + 47);
    return o;
}

void object_spawner(void)                            /* LAB_3078 */
{
    if (g.scrolled7222 == 0) return;
    if (g.rowphase7212 != 0) return;
    if (g.msg8514 >= 0x7530) return;
    if (g.stage7228 == 0 && g.pending7230 == 0) {    /* the 3 hangar gates */
        int col = -1, idx = 0;
        if (g.progress7206 == 0x0F10 && !(g.hangars4099 & 2)) { col = 11; idx = 1; }
        else if (g.progress7206 == 0x1490 && !(g.hangars4099 & 4)) { col = 9; idx = 2; }
        else if (g.progress7206 == 0x1DD0 && !(g.hangars4099 & 8)) { col = 12; idx = 3; }
        if (col >= 0) {
            g.pending7230 = (int16_t)idx;
            object_alloc(0x2FB8, col);
            return;
        }
    }
    uint32_t row = g.maprow7214 - 0x30;
    for (int i = 0; i < 24; i++) {
        int col = 23 - i;                            /* D0 counts down while the address ascends */
        uint32_t wa = row + (uint32_t)i * 2;
        uint16_t tile = cw(wa);
        uint32_t tmpl = 0;
        switch (g.stage7228) {
        case 0:
            if (tile == 0x6180) tmpl = 0x2DA8;
            else if (tile == 0x5640) { if (cw(wa + 2 + 0x30) == 0x5CD0) tmpl = 0x2E68; }   /* $3138: 48(A1), A1 already past the word */
            else if (tile == 0x0280) { if (g.progress7206 >= 0x3E8 && g.progress7206 < 0x1F40) tmpl = 0x2B98; }
            else if (tile == 0x8020) tmpl = 0x2EC8;
            else if (tile == 0x5D20) tmpl = 0x2EF8;
            else if (tile == 0x5D70) tmpl = 0x2F28;
            else if (tile == 0x5910) tmpl = 0x2F58;
            else if (tile == 0x59B0) tmpl = 0x2F88;
            break;
        case 1:
            if (tile == 0x5C80) tmpl = 0x2DD8;
            else if (tile == 0x00A0) tmpl = 0x2FE8;
            else if (tile == 0x0230) tmpl = 0x3018;
            else if (tile == 0x0D70) tmpl = 0x2C28;
            else if (tile == 0x92E0 && g.progress7206 < 0x19C8) tmpl = 0x3048;
            break;
        case 2:
            if (tile == 0x7260) tmpl = 0x2BC8;
            else if (tile == 0x9420) tmpl = 0x2E98;
            else if (tile == 0x0CD0) tmpl = 0x2E08;
            break;
        default:
            if (tile == 0x8C00) tmpl = 0x2B68;
            else if (tile == 0x9420) tmpl = 0x2E38;
            else if (tile == 0x92E0) { if (g.progress7206 < 0x0DDE && rng() < 0x40) tmpl = 0x2BF8; }
            else if (tile == 0x3A70) tmpl = 0x2C58;
            else if (tile == 0x4BF0) tmpl = 0x2C88;
            else if (tile == 0x3ED0) tmpl = 0x2CB8;
            else if (tile == 0x3340) tmpl = 0x2CE8;
            else if (tile == 0x4E70) tmpl = 0x2D18;
            else if (tile == 0x4290) tmpl = 0x2D48;
            else if (tile == 0x3520) tmpl = 0x2D78;
        }
        if (!tmpl) continue;
        Object *o = object_alloc(tmpl, col);
        if (!o) {                                    /* pool full ($3308) */
            if (g.stage7228 == 1) {                  /* stage 1: disarm the hatch tile in the map */
                if (tile == 0x00A0) cwrw(wa, 0x0320);
                else if (tile == 0x0230) cwrw(wa, 0x04B0);
                continue;
            }
            return;                                  /* other modes: stop the scan */
        }
        if (o->type == 0x27) return;                 /* a gate ends the scan */
    }
}

void object_tail(Object *o)                          /* LAB_6EE4 */
{
    if (o->y >= 0x200 || (int16_t)(o->y + o->h6) < 0x100) { o->x = 0; return; }
    o->f48 = (int16_t)(o->x + o->f20);
    o->f50 = (int16_t)(o->f48 + o->f22);
    o->f52 = o->y;
    o->f54 = (int16_t)(o->y + o->h6);
    emit(1, o->x, o->y, o->gfx12 + (uint32_t)o->f25 * (uint32_t)o->f26, 0, o->w8, o->h6, o->f25);
    if (render_count) { render_list[render_count - 1].stride = o->f10;
                        render_list[render_count - 1].type = (int)((o->tmpl - 0x2B68) / 48); }
}

void object_update_all(void)                         /* LAB_5F34 prologue */
{
    render_count = 0;
    for (int i = 0; i < 18; i++) {
        Object *o = &g.objects[i];
        o->flags31 &= (uint8_t)~0x20;
        if (o->x == 0) continue;
        o->flags31 ^= 0x08;
        ObjectHandler fn = (o->type < 0x2a) ? object_handlers[o->type] : 0;
        if (fn) fn(o);
        /* types with no handler: LAB_6FEC -- nothing at all this frame */
    }
}

/* ================= hostile pool ================= */
Hostile *hostile_init(Hostile *h, int16_t xp, int16_t yp, uint8_t type, uint32_t p12)   /* LAB_7608 */
{
    uint32_t d = BS_DESC_BASE + (uint32_t)type * 0x20;
    int slot = h->slot;
    memset(h, 0, sizeof *h);
    h->slot = slot;
    h->type = type;
    if (xp >= 0x320) xp = (int16_t)(xp - 1000 + 0x100);
    else xp = (int16_t)(xp + g.cam7204);
    h->x = (int32_t)xp << 16;
    h->y = (int32_t)(int16_t)(yp + 0x100) << 16;
    h->p8 = 0;
    h->p12 = (int32_t)p12;
    h->script = bs_chip + (p12 & (0x80000 - 1));
    for (int i = 0; i < 4; i++) h->box[i] = (int16_t)cw(d + 4 + (uint32_t)i * 2);
    h->hp = (int8_t)cb(d + 12);
    h->hitsnd = cb(d + 13);
    if (g.two_players2732 == 0)
        h->hp = (int8_t)(h->hp - (uint8_t)(((uint8_t)(h->hp + 1)) >> 2));
    h->t26 = 0; h->t27 = cb(d + 26); h->t28 = cb(d + 27);
    h->explode = 0; h->flags = 0;
    h->gfxmask = cl(d + 16);
    h->gfx = cl(d + 20);
    h->g40 = h->g42 = 0;
    h->h = (int16_t)cw(d);
    h->w_words = (int16_t)cw(d + 2);
    h->clip_w = (int16_t)((h->w_words - 1) << 4);
    h->modulo = (int16_t)(0x30 - 2 * h->w_words);
    h->frame_bytes = (int16_t)((2 * h->w_words - 2) * h->h);
    h->frame_stride = (int16_t)(6 * h->frame_bytes);
    h->score_bcd = (int16_t)cw(d + 24);
    h->flash = 0; h->damage = 0; h->frame = 0;
    h->box_dx = (int16_t)cw(d + 28);
    h->box_w = (int16_t)cw(d + 30);
    return h;
}

Hostile *hostile_alloc(int16_t xp, int16_t yp, uint8_t type, uint32_t p12)   /* LAB_75E4 */
{
    if (g.no_ship16120 && !g.demo) return NULL;
    for (int i = 11; i >= 0; i--)                    /* searches slot 11 DOWN to 0 */
        if (hxw(&g.hostiles[i]) == 0)
            return hostile_init(&g.hostiles[i], xp, yp, type, p12);
    return NULL;
}

static void hostile_free_e(Hostile *h) { h->x = 0; }   /* LAB_98E2 (engine-internal) */

const uint8_t *script_resolve(uint32_t amiga_addr) { return bs_chip + (amiga_addr & (0x80000 - 1)); }

/* LAB_883C terrain probe (type 7): the byte under (x-$FE, y-$101) must be 0 or
 * $FF in every plane of the playfield bitmap.  Native: reconstruct the TERRAIN
 * pixels from the map (the real buffer also contains the object bobs drawn
 * this frame -- approximation, flagged in the report). */
int playfield_blank(int16_t x, int16_t y)
{
    int sy = y - 0x101;                       /* screen row (ring7208 top = row 0) */
    int bx = (x - 0xFE) >> 3;                 /* byte within the 48-byte row */
    if (bx < 0 || bx >= 48) return 0;
    int32_t q = g.progress7206 - sy;          /* the row's exposure progress */
    if (q < 1) return 0;
    uint32_t maprow = 0x4A000 - (uint32_t)(((q - 1) >> 4) + 1) * 0x30;
    if (maprow < 0x44000) return 0;
    int tile_row = 15 - ((q - 1) & 15);
    uint16_t word = cw(maprow + (uint32_t)(bx >> 1) * 2);
    uint32_t tb = 0x4A000 + (uint32_t)word * 2 + (uint32_t)tile_row * 2;
    for (int p = 0; p < 5; p++) {
        uint8_t b = bs_chip[tb + (uint32_t)p * 0x20 + (uint32_t)(bx & 1)];
        if (b != 0 && b != 0xFF) return 0;
    }
    return 1;
}

void hostile_reprocess(Hostile *h) { (void)h; g.reprocess = 1; }   /* BRA LAB_79F2 */

void hostile_draw(Hostile *h, uint32_t planes, uint32_t mask)   /* LAB_9814/LAB_981C */
{
    int16_t xw = hxw(h), yw = hyw(h);
    h->box[0] = (int16_t)(xw + h->box_dx);
    h->box[1] = (int16_t)(h->box[0] + h->box_w);
    h->box[2] = yw;
    h->box[3] = (int16_t)(yw + h->h);
    /* LAB_55D8 clip: bit4 = fully off-screen this frame */
    int16_t right = (int16_t)(((g.cam7204 + 0x11F) | 0x0F) + 1);
    int16_t left = (int16_t)(g.cam7204 & ~0x0F);
    if (xw >= right || (int16_t)(xw + h->clip_w) < left ||
        (int16_t)(yw - 0x100 + h->h - 1) < 0 || yw >= 0x200)
        h->flags |= HF_OFFSCREEN;
    else {
        h->flags &= (uint8_t)~HF_OFFSCREEN;
        emit(2, xw, yw, planes, mask, h->w_words, h->h, h->frame);
        if (render_count) {
            RenderEntry *r = &render_list[render_count - 1];
            /* the blit's per-plane advance is the FULL frame's plane size (44(A4)),
             * not the clipped height: emerging pop-ups draw the top h rows of the
             * full-height image (LAB_97F8 A3 = A2 + stride - frame_bytes) */
            r->stride = (int)(uint16_t)h->frame_bytes;
            r->reveal = render_reveal_mask;
            r->type = h->type;
        }
    }
    render_reveal_mask = 0xFFFFFFFFu;                /* $76E8 is rebuilt per draw */
}

void hostile_explode(Hostile *h)                     /* LAB_98E6 */
{
    h->explode = 8;
    h->flags = (uint8_t)((h->flags & ~HF_FIRE_REQUEST) | HF_IMMUNE);
    h->gfx = 0x11090;
    h->gfxmask = 0x11310;
    h->h = 0x20;
    /* falls into LAB_7A42: the driver's explosion step runs on the NEXT visit;
     * the original jumps straight in -- emulate by running one step now */
    if (!(g.dframe & 2)) {
        if (--h->explode) { h->gfx += 0x300; h->gfxmask += 0x300; }
        else { hostile_free_e(h); return; }          /* (type-0 last-of-wave handled by the driver) */
    }
    hostile_draw(h, h->gfx, h->gfxmask);
}

void turn_into_pickup(Hostile *h, int nova_pickup)   /* LAB_9986 */
{
    int32_t relx = (int32_t)(int16_t)(hxw(h) - g.cam7204 + 8) << 16;
    int16_t y = (int16_t)(hyw(h) - 0xF4);
    hostile_init(h, 0x63, y, 5, (uint32_t)relx);
    h->p12 = relx;
    if (nova_pickup) h->t28 = 0x0A;
    else h->t28 = (uint8_t)(rng() & 6);
    if ((int16_t)(h->p12 >> 16) < 0x88) h->p8 = 0x20000;
    else { h->t27 = (uint8_t)~h->t27; h->p8 = (int32_t)0xFFFE0000; }
    g.reprocess = 1;                                 /* BRA LAB_79F2 */
}

static void hostile_stub(Hostile *h)                 /* placeholder until hostiles.c lands */
{
    uint32_t planes = h->gfx + (uint32_t)h->frame * (uint32_t)(uint16_t)h->frame_stride;
    hostile_draw(h, planes, planes + (uint16_t)h->frame_stride - (uint16_t)h->frame_bytes);
}
__attribute__((weak)) HostileHandler hostile_handlers[14] = {
    hostile_stub, hostile_stub, hostile_stub, hostile_stub, hostile_stub, hostile_stub, hostile_stub,
    hostile_stub, hostile_stub, hostile_stub, hostile_stub, hostile_stub, hostile_stub, hostile_stub,
};

void hostile_update_all(int lower, int pass_b)       /* LAB_79E2 (one pass) */
{
    for (int i = 0; i < 12; i++) {
        Hostile *h = &g.hostiles[i];
    again:
        if (hxw(h) == 0) continue;
        if ((hyw(h) > 0x146) != (lower != 0)) continue;
        if (!pass_b && h->type != 0x0D && h->type != 0x07 && h->type != 0x03) continue;
        if (h->flags & HF_DONE) continue;
        h->flags |= HF_DONE;
        if (h->explode) {                            /* LAB_7A42 */
            if (!(g.dframe & 2)) {
                if (--h->explode) { h->gfx += 0x300; h->gfxmask += 0x300; }
                else if (h->type != 0) { hostile_free_e(h); continue; }
                else {                               /* last type-0 of its wave -> nova pickup */
                    int n = 0;
                    for (int j = 0; j < 12; j++)
                        if (g.hostiles[j].type == 0 && hxw(&g.hostiles[j]) != 0) n++;
                    if (n != 1) { hostile_free_e(h); continue; }
                    turn_into_pickup(h, 1);
                    g.reprocess = 0;
                    goto again;
                }
            }
            hostile_draw(h, h->gfx, h->gfxmask);
            continue;
        }
        g.reprocess = 0;
        hostile_handlers[h->type](h);
        if (g.reprocess) { g.reprocess = 0; goto again; }
    }
}

uint8_t aim_players(int16_t fx, int16_t fy, int16_t *d5, int16_t *d6)   /* LAB_491C */
{
    uint8_t d7 = 0;
    int16_t dx1 = (int16_t)(g.players[0].sx + 0x0C - fx);
    if (dx1 < 0) { dx1 = (int16_t)-dx1; d7 |= 1; }
    int16_t dy1 = (int16_t)(g.players[0].sy + 0x10 - fy);
    if (dy1 < 0) { dy1 = (int16_t)-dy1; d7 |= 2; }
    if (d5) *d5 = (int16_t)(dx1 + dy1);
    int16_t dx2 = (int16_t)(g.players[1].sx + 0x0C - fx);
    if (dx2 < 0) { dx2 = (int16_t)-dx2; d7 |= 4; }
    int16_t dy2 = (int16_t)(g.players[1].sy + 0x10 - fy);
    if (dy2 < 0) { dy2 = (int16_t)-dy2; d7 |= 8; }
    if (d6) *d6 = (int16_t)(dx2 + dy2);
    return d7;
}

/* ================= wave scheduler LAB_7556 ================= */
void wave_scheduler(void)
{
    for (;;) {
        uint16_t trig = cw(g.wave2736);
        if ((uint16_t)g.progress7206 < trig) return;           /* BCS LAB_75E2 (unsigned) */
        if (g.scrolled7222 == 0) return;
        int16_t x = (int16_t)cw(g.wave2736 + 2);
        int16_t y = (int16_t)cw(g.wave2736 + 4);
        uint8_t type = cb(g.wave2736 + 6);
        uint32_t script = cl(g.wave2736 + 8);
        g.wave2736 += 12;
        if ((uint16_t)x == 0xFFFF) {                 /* wipe the hostile pool */
            for (int i = 0; i < 12; i++) hostile_free_e(&g.hostiles[i]);
            continue;
        }
        if (type == 6) {                             /* $75A0: random x */
            uint8_t r = rng();
            x = (int16_t)((r & 0x7F) + 0x40);
        }
        if (type == 8 || type == 1) {                /* $75C2: x = rnd + (rnd & $7f) - $40 */
            uint8_t r1 = rng();
            uint8_t r2 = rng();
            x = (int16_t)(r1 + (r2 & 0x7F) - 0x40);
        }
        hostile_alloc(x, y, type, script);
    }
}

/* ================= fire requests -> effect pool ================= */
void effect_remove(int slot)                         /* LAB_4E1A: shift down; the LAST record is refilled from
                                                        the 20 bytes PAST the pool ($4AB6.. = code bytes), not zeroed */
{
    memmove(&g.effects[slot], &g.effects[slot + 1], (size_t)(15 - slot) * sizeof(Effect));
    Effect *e = &g.effects[15];
    e->x = (int32_t)cl(0x4AB6); e->y = (int32_t)cl(0x4ABA);
    e->vx = (int32_t)cl(0x4ABE); e->vy = (int32_t)cl(0x4AC2);
    e->gfx = cb(0x4AC6); e->frame = cb(0x4AC7); e->channel = cb(0x4AC8); e->age = cb(0x4AC9);
}

static void spawn_bullet_47d2(const Object *owner)   /* LAB_47D2 (owner = object or NULL for hostiles) */
{
    if (g.nova25334) return;
    if ((g.players[0].state38 & g.players[1].state38) & 0x80) return;   /* both dead */
    if ((int16_t)(g.effects[g.bullet_limit10062].x >> 16) != 0) return; /* pool "full" at the cap slot */
    int16_t fy = g.fire_oy14396;
    int slot = 0;
    for (;;) {
        if ((int16_t)(g.effects[slot].x >> 16) == 0) break;             /* free: insert here, no shift */
        if (fy <= (int16_t)(g.effects[slot].y >> 16)) {                 /* keep ascending y: shift up */
            memmove(&g.effects[slot + 1], &g.effects[slot], (size_t)(15 - slot) * sizeof(Effect));
            /* LAB_481E overshoots one record: the insert slot is filled from the
             * record 20 bytes BELOW it -- slot-1, or the code bytes at $4962 for
             * slot 0 (V: oracle BS_WATCH at $4978, fracs $986D/$6A06). */
            if (slot > 0) g.effects[slot] = g.effects[slot - 1];
            else {
                g.effects[0].x = (int32_t)cl(0x4962);
                g.effects[0].y = (int32_t)cl(0x4966);
                g.effects[0].vx = (int32_t)cl(0x496A);
                g.effects[0].vy = (int32_t)cl(0x496E);
            }
            break;
        }
        slot++;
        if (slot == 16) return;                                          /* cannot happen below the cap */
    }
    Effect *e = &g.effects[slot];
    if (g.mine14384) {                                /* stationary mine */
        e->x = (e->x & 0xFFFF) | ((int32_t)g.fire_ox14398 << 16);
        e->y = (e->y & 0xFFFF) | ((int32_t)fy << 16);
        e->vx = e->vy = 0;
        if (cb(0x79DE)) { e->gfx = 0x0C; e->frame = 0x70; e->channel = 0x00; e->age = 0x18; }
        else            { e->gfx = 0x0C; e->frame = 0x60; e->channel = 0x00; e->age = 0x01; }
        return;
    }
    uint8_t d7 = 0;
    int32_t speed = (int32_t)g.bullet_speed14386 << 8;
    int16_t dx, dy;
    if (g_14390) {                                   /* explicit direction */
        dx = g_14390; if (dx < 0) { dx = (int16_t)-dx; d7 |= 1; }
        g_14390 = 0;
        dy = g_14388; if (dy < 0) { dy = (int16_t)-dy; d7 |= 2; }
    } else {
        int16_t d5, d6;
        uint8_t bits = aim_players(g.fire_ox14398, fy, &d5, &d6);
        int16_t dx1 = (int16_t)(g.players[0].sx + 0x0C - g.fire_ox14398); if (dx1 < 0) dx1 = (int16_t)-dx1;
        int16_t dy1 = (int16_t)(g.players[0].sy + 0x10 - fy); if (dy1 < 0) dy1 = (int16_t)-dy1;
        int16_t dx2 = (int16_t)(g.players[1].sx + 0x0C - g.fire_ox14398); if (dx2 < 0) dx2 = (int16_t)-dx2;
        int16_t dy2 = (int16_t)(g.players[1].sy + 0x10 - fy); if (dy2 < 0) dy2 = (int16_t)-dy2;
        int use_p2;
        if (owner && owner->f42 == 1) use_p2 = 0;
        else if (owner && owner->f42 == 2) use_p2 = 1;
        else if (g.players[0].state38 & 0x80) use_p2 = 1;
        else if (g.players[1].state38 & 0x80) use_p2 = 0;
        else use_p2 = !(d5 < d6);
        d7 = bits;
        if (use_p2) { dx = dx2; dy = dy2; d7 = (uint8_t)(d7 >> 2); }
        else        { dx = dx1; dy = dy1; }
        d7 &= 3;
    }
    int32_t vx, vy;
    if (dy >= dx) {
        uint32_t q = ((uint32_t)(uint16_t)dx * (uint16_t)g.bullet_speed14386) / (uint16_t)(dy | 1);
        vx = (int32_t)((q & 0xFFFF) << 8);
        vy = speed;
    } else {
        uint32_t q = ((uint32_t)(uint16_t)dy * (uint16_t)g.bullet_speed14386) / (uint16_t)(dx | 1);
        vy = (int32_t)((q & 0xFFFF) << 8);
        vx = speed;
    }
    if (d7 & 1) vx = -vx;
    if (d7 & 2) vy = -vy;
    e->x = (e->x & 0xFFFF) | ((int32_t)g.fire_ox14398 << 16);
    e->y = (e->y & 0xFFFF) | ((int32_t)fy << 16);
    e->vx = vx; e->vy = vy;
    e->gfx = 0x07; e->frame = 0x58; e->channel = 0; e->age = 0;
}

void effects_from_requests(void)                     /* LAB_4704 */
{
    if (g.demo && g.demo_frames >= 0x5DC) return;
    for (int i = 0; i < 12; i++) {
        Hostile *h = &g.hostiles[i];
        if (!(h->flags & HF_FIRE_REQUEST)) continue;
        h->flags &= (uint8_t)~HF_FIRE_REQUEST;
        if (hxw(h) == 0) continue;
        g.fire_ox14398 = h->fire_x;
        g.fire_oy14396 = h->fire_y;
        g.mine14384 = 0;
        if (h->type == 9 && (int16_t)(h->fire_y - hyw(h)) == 0x26) { g.mine14384 = 0xFF; sfx(56); }
        spawn_bullet_47d2(NULL);
    }
    /* Object loop $4778 with the ORIGINAL's D0 bug: the mine path loads D0=56
     * for the sound call BEFORE the counter is saved, so after a mine fires the
     * DBF scans up to 57 more 64-byte "records" PAST the pool (LODS0F bytes at
     * $2E4C0..), firing bullets from graphics data (V: oracle trace: LAB_47D2
     * called with A0 = $2E540/$2E640/..; the junk E records in the captures). */
    {
        int d0 = 17;
        uint32_t a0 = 0x2E040;
        static Object ghost;                          /* fake owner for the beyond-pool records */
        for (;;) {
            int idx = (int)((a0 - 0x2E040) >> 6);
            if (idx < 18) {
                Object *o = &g.objects[idx];
                if ((o->flags31 & 0x20) && o->x != 0) {
                    g.fire_ox14398 = o->f38;
                    g.fire_oy14396 = o->f40;
                    g.mine14384 = 0;
                    if (o->type == 0x25 || o->type == 0x26) { g.mine14384 = 0xFF; d0 = 56; sfx(56); }
                    spawn_bullet_47d2(o);
                }
            } else {                                  /* beyond the pool: raw chip bytes as a record */
                if ((cb(a0 + 31) & 0x20) && cw(a0) != 0) {
                    g.fire_ox14398 = (int16_t)cw(a0 + 38);
                    g.fire_oy14396 = (int16_t)cw(a0 + 40);
                    g.mine14384 = 0;
                    uint8_t t = cb(a0 + 17);
                    if (t == 0x25 || t == 0x26) { g.mine14384 = 0xFF; d0 = 56; sfx(56); }
                    ghost.f42 = cb(a0 + 42);
                    spawn_bullet_47d2(&ghost);
                }
            }
            a0 += 0x40;
            if (--d0 == -1) break;                    /* DBF D0 */
        }
    }
}

/* ================= effects update LAB_4ADA (once per DISPLAY frame) ================= */
static int missile_dir32(int32_t vx, int32_t vy)     /* LAB_4CC4..4D42 */
{
    int neg = 0;
    if (vx < 0) { neg |= 1; vx = -vx; }
    if (vy < 0) { neg |= 2; vy = -vy; }
    uint16_t dv = (uint16_t)(((uint32_t)vx >> 8) | 1);
    uint16_t q = (uint16_t)(((uint32_t)vy) / dv);
    static const uint16_t thr[8] = { 0x19, 0x4E, 0x89, 0xD2, 0x138, 0x1DF, 0x34C, 0xA27 };
    int d3 = 0;
    for (int i = 0; i < 8; i++) if ((int16_t)q < (int16_t)thr[i]) d3++;
    if (neg == 0) d3 = (d3 ^ 7) + 8;
    else if (neg == 1) d3 += 0x10;
    else if (neg == 3) d3 = (d3 ^ 7) + 0x18;
    return d3 & 0x1F;
}

void effects_update(void)
{
    /* y-sort (gates -13962/-13942 are non-zero in play) */
    for (int again = 1; again;) {
        again = 0;
        for (int i = 0; i < 15; i++) {
            if ((int16_t)(g.effects[i + 1].x >> 16) == 0) break;
            if ((int16_t)(g.effects[i].x >> 16) == 0) break;
            if ((int16_t)(g.effects[i].y >> 16) > (int16_t)(g.effects[i + 1].y >> 16)) {
                Effect t = g.effects[i]; g.effects[i] = g.effects[i + 1]; g.effects[i + 1] = t;
                again = 1;
            }
        }
    }
    uint8_t c = 0;                                    /* sprite channel assignment LAB_4B50 */
    for (int i = 0; i < 16; i++) {
        int16_t yw = (int16_t)(g.effects[i].y >> 16);
        g.effects[i].channel = c;
        if (yw >= 0x116 && yw < 0x1E0) c = (uint8_t)((c + 4) & 15);
        else { g.effects[i].channel |= 8; c = (uint8_t)((c + 4) & 7); }
    }
    int16_t xmin = (int16_t)(g.cam7204 - 0x10), xmax = (int16_t)(g.cam7204 + 0x120);
    for (int i = 0; i < 16; i++) {
        Effect *e = &g.effects[i];
        if (e->x == 0) continue;                      /* tests the LONG (word+frac) */
        if (e->age == 0) {                            /* BULLET (LAB_4D8A) */
            e->x += e->vx;
            e->y += e->vy;
            int16_t yw = (int16_t)(e->y >> 16), xw = (int16_t)(e->x >> 16);
            if (yw >= 0x200 || yw <= 0xF3) { effect_remove(i); continue; }
            if (!g.nova25334 && (xw <= xmin || xw >= xmax)) { effect_remove(i); continue; }
            emit(3, xw, yw, e->gfx, 0, 1, 8, e->frame + ((g.dframe & 4) ? 0 : 1));
            continue;
        }
        /* MISSILE (LAB_4BD6..) */
        uint8_t d7 = 0;
        if (e->age < 0x78) {
            int16_t d5, d6;
            uint8_t bits = aim_players((int16_t)(e->x >> 16), (int16_t)(e->y >> 16), &d5, &d6);
            d7 = bits;
            if (d5 >= d6) d7 = (uint8_t)(d7 >> 2);
        } else {
            uint8_t b1 = (uint8_t)(e->x >> 16);       /* low byte of the x word */
            if (b1 < (uint8_t)g.dframe) d7 = 1;
        }
        if (d7 & 1) { if (e->vx > (int32_t)0xFFFE8000) e->vx -= 0x800; }
        else        { if (e->vx < 0x18000) e->vx += 0x800; }
        if (d7 & 2) { if (e->vy > (int32_t)0xFFFE8000) e->vy -= 0x800; }
        else        { if (e->vy < 0x18000) e->vy += 0x800; }
        if (!(g.dframe & 1)) e->age++;
        if (e->age < 0x19) {
            if (g.dframe & 1) e->y += (int32_t)g.scrolled7222 << 16;    /* rides the scroll */
        } else {
            e->x += e->vx;
            e->y += e->vy;
        }
        if ((int16_t)(e->y >> 16) >= 0x200) { effect_remove(i); continue; }
        if ((g.dframe & 1) == ((15 - i) & 1)) {       /* turn on alternate frames (D5 = the DBF counter = 15-i) */
            int dir = missile_dir32(e->vx, e->vy);
            int d4 = (e->frame - 0x60 - dir);
            if (d4) {
                int d5v = e->frame - 0x60;
                if (d4 < 0) d4 += 0x20;
                if (d4 < 0x10) d5v -= 2;
                d5v = (d5v + 1) & 0x1F;
                e->frame = (uint8_t)(d5v + 0x60);
            }
        }
        if ((int16_t)(e->y >> 16) < 0xF6) continue;
        emit(3, (int16_t)(e->x >> 16), (int16_t)(e->y >> 16), e->gfx, 0, 1, 8, e->frame);
    }
}

/* ================= player ================= */
void set_weapon(Player *p, uint32_t t)               /* LAB_2B32 */
{
    p->fire_period28 = (int16_t)cw(t);
    p->weapon58 = (int16_t)cw(t + 2);
    p->roll30 = (int16_t)cw(t + 4);
    p->rotate32 = (int16_t)cw(t + 6);
    for (int i = 0; i < 12; i++) p->slot_state[i] = (int8_t)cb(t + 8 + (uint32_t)i);
    p->shot_hw62 = (int16_t)cw(t + 20);
    p->shot_hh64 = (int16_t)cw(t + 22);
    p->weapon_table = t;                              /* template base = t + 24 */
}

void reload_weapon(Player *p)                        /* LAB_3722 */
{
    set_weapon(p, cl(0x2024 + (uint32_t)p->weapon58 * 24 + (uint32_t)p->level60 * 4));
}

void read_input(Player *p, uint8_t joy) { p->joy44 = joy; }

static void hud_stub(void) {}

void player_fire(Player *p)                          /* LAB_3F54 */
{
    if (p->state38) return;
    uint8_t joy = p->joy44;
    if ((joy & 0x20) && !g.nova25334 && p->nova66) { /* NOVA trigger LAB_3F64 */
        g_27618 = 0x32;
        sfx(57);
        p->nova66--;
        p->nova90 = 0xFF;
        g.nova25334 = 0xFF;
        hud_stub();
    }
    if (p->nova90) return;
    if (p->cooldown46) {
        p->cooldown46--;
        if (p->repeat57) p->repeat57--;
        if (!(joy & 0x10)) p->repeat57 = 0;
        return;
    }
    if (!(joy & 0x10)) { p->repeat57 = 0; return; }
    if (p->repeat57) { p->repeat57--; return; }
    /* LAB_3FE2: volley bookkeeping */
    int launch_live = 0, flight_live = 0;
    for (int i = 0; i < 12; i++) {
        int8_t st = p->slot_state[i];
        if (st < 0) continue;
        if (st == 0) { if (p->shots[i].x) launch_live++; }
        else { if (p->shots[i].x) flight_live++; }
    }
    if (launch_live) {
        if (flight_live) return;                     /* both banks busy */
        int n = p->roll30;                           /* copy shots[n..2n) -> shots[0..n) (blitter A->D) */
        for (int i = 0; i < n && n + i < 12; i++) p->shots[i] = p->shots[n + i];
        if (p->rotate32) {                           /* $403C: rotate the last f32 records up */
            int r = p->rotate32;
            for (int i = 0; i < r; i++) {
                int src = 12 - r - 1 - i;            /* A1 walks down from base+0x90-r*12 */
                int dst = src + r;
                if (src >= 0 && dst < 12) p->shots[dst] = p->shots[src];
            }
        }
    }
    uint32_t t = p->weapon_table + 24;               /* refill launch slots in order */
    for (int i = 0; i < 12; i++) {
        if (p->slot_state[i] != 0) continue;
        Shot *s = &p->shots[i];
        s->x = (int16_t)cw(t); s->y = (int16_t)cw(t + 2);
        s->vx = (int16_t)cw(t + 4); s->vy = (int16_t)cw(t + 6);
        s->b8 = cb(t + 8); s->b9 = cb(t + 9); s->b10 = cb(t + 10); s->dmg = (int8_t)cb(t + 11);
        t += 12;
        g.stat_shots[p == &g.players[1]]++;          /* native stat */
    }
    p->cooldown46 = p->fire_period28;
    p->repeat57 = 0x0F;
    sfx(cb(0x3F40 + (uint32_t)p->weapon58));
}

void move_ship(Player *p)                            /* LAB_5070 (per display frame) */
{
    if (p->invuln52) p->invuln52--;
    if (p->state38 == 0xFF) return;
    uint8_t joy;
    if (p->entry48) {                                /* entry animation */
        if (--p->entry48 == 0) {
            p->state38 = 0;
            if (p->free_respawn41) p->free_respawn41 = 0;
            else p->lives56--;                       /* the life is taken when the new ship arrives */
            hud_stub();
        }
        joy = (p->entry48 < 0x3C) ? 1 : 0;           /* forced up below 60 */
    } else if (p->state38) {
        return;                                      /* exploding / dead: no movement, no mirrors */
    } else joy = p->joy44;
    if ((joy & 1) && p->y > 0x102) p->y -= 2;
    if (joy & 2) {
        if (!(g.dframe & 3) && p->bank10 < 12) p->bank10 += 2;
        if (p->x < 0x200) p->x += 2;
    }
    if ((joy & 4) && p->y < 0x1E0) p->y += 2;
    if (joy & 8) {
        if (!(g.dframe & 3) && p->bank10) p->bank10 -= 2;
        if (p->x > 0x100) p->x -= 2;
    }
    if (!(g.dframe & 3) && !(joy & 10) && p->bank10 != 6)
        p->bank10 += (p->bank10 > 6) ? -2 : 2;
    /* LAB_5168: sprite-space mirrors for BOTH players */
    for (int i = 0; i < 2; i++) {
        g.players[i].sx = (int16_t)(g.players[i].x + g.cam7204 - 0x100);
        g.players[i].sy = g.players[i].y;
    }
}

static void ship_death_step(Player *p)               /* LAB_5234 tail ($5290..$5372) */
{
    p->height8 = 0x3C;
    if (--p->explode49) return;
    p->height8 = 0x1E;
    p->hud68 = p->hud70 = 0x12C;
    p->hud72 = 0;
    p->f76 = p->f84;
    if (p->lives56 == 0) {                           /* game over for this ship ($52BA) */
        p->state38 = 0xC8;
        p->cursor42 = 0;
        memcpy(p->initials, "AAA]", 4);              /* $52C4 */
        p->f40 = 0;
        p->x = p->y = 0x3E7;
        p->f120 = 0x2EE;
        {                                            /* $52E2: score >= hiscore -> long name entry */
            uint32_t s1 = ((uint32_t)(uint8_t)p->score[0] << 24) | ((uint32_t)(uint8_t)p->score[1] << 16) |
                          ((uint32_t)(uint8_t)p->score[2] << 8) | (uint8_t)p->score[3];
            uint32_t s2 = ((uint32_t)(uint8_t)p->score[4] << 24) | ((uint32_t)(uint8_t)p->score[5] << 16) |
                          ((uint32_t)(uint8_t)p->score[6] << 8) | (uint8_t)p->score[7];
            uint32_t h1 = cl(0x8000 + 32246), h2 = cl(0x8000 + 32250);
            if (s1 > h1 || (s1 == h1 && s2 >= h2)) { p->f100 = 0xFF; p->f120 = 0x7D; }
        }
    } else {                                         /* respawn */
        p->entry48 = 0x91;
        p->nova66 = 3;
        p->hud68 = p->hud70 = 0x80;
        p->hud72 = 0;
        p->f76 = p->f80;
        p->state38 = 0x96;
        p->x = p->spawn_x54;
        p->y = 0x200;
        p->bank10 = 6;
        p->invuln52 = 0x12C;
        p->level60 >>= 1;                            /* THE LEVEL HALVES */
        set_weapon(p, cl(0x2024 + (uint32_t)p->weapon58 * 24 + (uint32_t)p->level60 * 4));
    }
}

void ship_and_shots(Player *p)                       /* LAB_5234/5374/5490 (per display frame) */
{
    if (p->explode49) ship_death_step(p);
    if (p->state38 >= 0xC8) return;                  /* dead: shots not processed */
    int16_t d3 = p->y;
    int16_t xmin = (int16_t)(g.cam7204 - 0x10), xmax = (int16_t)(g.cam7204 + 0x120);
    (void)d3;
    for (int i = 0; i < 12; i++) {
        Shot *s = &p->shots[i];
        if (s->x == 0) continue;
        int16_t nx, ny;
        if (s->b10) {                                /* staggered launch */
            if (--s->b10) continue;
            nx = (int16_t)(s->x + p->sx);            /* becomes absolute (sprite space) */
            ny = (int16_t)(s->y + p->sy);
        } else {
            nx = (int16_t)(s->x + s->vx);
            ny = (int16_t)(s->y + s->vy);
        }
        if (ny <= 0xF3 || ny >= 0x200 || nx <= xmin || nx >= xmax) { s->x = 0; continue; }
        s->x = nx; s->y = ny;
    }
    /* LAB_5490: weapon-3 sine wobble on the outer shot pairs */
    if (p->weapon58 == 3) {
        uint32_t t = p->weapon_table;
        int off = 0;
        if (t >= 0x3DF0) off = 6;
        else if (t >= 0x3D60) off = 4;
        else if (t == 0x3D30) off = 2;
        if (off) {
            int16_t w0 = (int16_t)cw(0x51D2 + (uint32_t)(g.dframe & 7) * 2);
            int16_t w1 = (int16_t)cw(0x51D2 + (uint32_t)(g.dframe & 7) * 2 + 8);
            if (p->shots[0].x) p->shots[0].x += w0;
            if (p->shots[off].x) p->shots[off].x += w0;
            if (p->shots[1].x) p->shots[1].x += w1;
            if (off + 1 < 12 && p->shots[off + 1].x) p->shots[off + 1].x += w1;
        }
    }
}

void portrait_1e02(Player *p)                        /* LAB_1E02/LAB_1E34 (state only, per display frame) */
{
    if (p->hud68 == 0) return;
    int16_t d1 = p->hud68;
    if (d1 < 0x384) {
        p->hud68--;
        if (d1 <= 0x20) {                            /* pixel dissolve phase */
            if (d1 == 0x20) p->hud72 = 0;
            if (p->hud72 != 0x100) p->hud72 += 8;
            return;
        }
    }
    if (p->hud72 == 0x100) return;                   /* LAB_1EA0/1F1C */
    int16_t v = p->hud68;
    if (v == 0x3E7 || (int16_t)(v + 1) == p->hud70) { p->hud68--; p->hud70--; }
}

void nova(void)                                      /* LAB_1D0C (state only; (L), never in a capture) */
{
    if (!g.nova25334) return;
    if (g.nova25334 == 0xFF) { g.nova_script25338 = 0x171B0; sfx(-0x750); }
    Player *p = g.players[0].nova90 ? &g.players[0] : &g.players[1];
    g.nova25334--;
    p->nova90--;
    int16_t d = (int16_t)cw(g.nova_script25338);      /* MOVE.W (A2)+,D3: WORD read */
    g.nova_script25338 += 2;
    if (d < 0) {
        g.nova25334 = 0;
        p->nova90 = 0;
        for (int i = 0; i < 16; i++) {               /* $1D56: CLR.L (A0) + CLR.B 19(A0) ONLY --
                                                        y/vx/vy/gfx/frame/channel keep their residue */
            g.effects[i].x = 0;
            g.effects[i].age = 0;
        }
        return;
    }
    for (int i = 0; i < 8; i++) {                    /* 8 ring records over slots 0..7 */
        Effect *e = &g.effects[i];
        uint32_t tab = 0x1727A + (uint32_t)(g.dframe & 7) * 4 + (uint32_t)i * 0x20;
        /* $1DB0/$1DB2: MOVE.W writes only the INTEGER word -- the slot's old
         * fraction word survives into the new ring record */
        e->x = ((int32_t)(int16_t)(p->sx + 8 + (int16_t)cw(tab) / (d | 1)) << 16) | (e->x & 0xFFFF);
        e->y = ((int32_t)(int16_t)(p->sy - 16 - (int16_t)cw(tab + 2) / (d | 1)) << 16) | (e->y & 0xFFFF);
        e->gfx = 0x10;
        e->frame = (uint8_t)(0x54 + ((g.dframe + i) & 3));
    }
    if (g.nova25334 >= 0xB0 && (g.nova25334 & 15) == 14) {   /* nova shot burst */
        uint32_t src = 0x3EB0;
        for (int i = 0; i < 12; i++) {
            Shot *s = &p->shots[i];
            s->x = (int16_t)cw(src); s->y = (int16_t)cw(src + 2);
            s->vx = (int16_t)cw(src + 4); s->vy = (int16_t)cw(src + 6);
            s->b8 = cb(src + 8); s->b9 = cb(src + 9); s->b10 = cb(src + 10); s->dmg = (int8_t)cb(src + 11);
            src += 12;
        }
    }
}

/* ================= collision / scoring ================= */
void award(uint16_t bcd)                             /* LAB_40BE/40DE to the selected player */
{
    uint8_t digit[8] = { 0, 0, 0, 0,
        (uint8_t)((bcd & 0xFF) >> 4), (uint8_t)(bcd & 0x0F),
        (uint8_t)((bcd >> 8) >> 4), (uint8_t)((bcd >> 8) & 0x0F) };
    char *sc = g.sel->score;
    int carry = 0;
    for (int i = 7; i >= 0; i--) {
        int c = sc[i] + digit[i] + carry;
        carry = 0;
        if (c >= '9' + 1) { c -= 10; carry = 1; }
        sc[i] = (char)c;
    }
}

static void award_10000(void)                        /* LAB_3712 tail: digits $40A4.. = +10000 */
{
    char *sc = g.sel->score;
    int carry = 1;                                   /* +1 at the 10^4 digit */
    for (int i = 3; i >= 0 && carry; i--) {
        int c = sc[i] + carry;
        carry = 0;
        if (c >= '9' + 1) { c -= 10; carry = 1; }
        sc[i] = (char)c;
    }
}

void hit_player(Player *p)                           /* $3662/$378A common part */
{
    p->state38 = 0x64;
    p->explode49 = 0x46;
    p->invuln52 = 0x270F;
    p->y -= 12;
    sfx(-0x732);                                     /* EXT_24732 */
}

typedef struct { int16_t x1, y1, x2, y2; } ShotBox;   /* D1,D2 = max, D3,D4 = min */
static ShotBox shot_box(const Player *p, const Shot *s)
{
    ShotBox b;
    if (p->nova90) {
        b.x1 = (int16_t)(s->x + 0x30); b.y1 = (int16_t)(s->y + 0x30);
        b.x2 = (int16_t)(s->x - 0x20); b.y2 = (int16_t)(s->y - 0x20);
    } else {
        b.x1 = (int16_t)(s->x + p->shot_hw62);
        b.y1 = (int16_t)(s->y + p->shot_hh64 + 10);
        b.x2 = s->x;
        b.y2 = (int16_t)(s->y - 10);
    }
    return b;
}

void kill_hostile_sound(Hostile *h)                  /* LAB_35CE */
{
    int snd = 28;
    if (h->type == 2) {
        snd = 29;
        if ((int8_t)g.hostiles[11].frame >= 7) snd = 20;     /* $2E02F = slot 11 byte +63 */
    }
    sfx(snd);
}

void collide_shots_hostiles(void)                    /* LAB_34FA */
{
    Player *p = g.sel;
    for (int si = 0; si < 12; si++) {
        Shot *s = &p->shots[si];
        if (s->x == 0) continue;
        ShotBox b = shot_box(p, s);
        for (int i = 0; i < 12; i++) {
            Hostile *h = &g.hostiles[i];
            if (hxw(h) == 0) continue;
            if (b.y2 >= h->box[3]) continue;
            if (b.y1 < h->box[2]) continue;
            if (b.x2 >= h->box[1]) continue;
            if (b.x1 < h->box[0]) continue;
            if (h->explode || (h->flags & HF_IMMUNE)) continue;
            g.stat_hits[p == &g.players[1]]++;       /* native stat */
            if (s->dmg < 0) h->damage += 2;          /* penetrating shot survives */
            else { s->x = 0; h->damage = (uint8_t)(h->damage + (uint8_t)s->dmg); }
            uint16_t points;
            /* $358E quirk (V runtime.c): the exemption tests PLAYER bytes 31/63 */
            if ((int8_t)(uint8_t)(h->hp - h->damage) < 0 &&
                !((uint8_t)p->roll30 == 9 && (uint8_t)p->shot_hw62 != 5)) {
                points = (uint16_t)h->score_bcd;
                kill_hostile_sound(h);
            } else {
                points = cw(0x8000 - 19208);          /* $5000 = 50 pts per hit */
                sfx(h->hitsnd);
            }
            award(points);
            break;
        }
    }
}

void collide_shots_objects(void)                     /* LAB_3424 */
{
    Player *p = g.sel;
    for (int si = 0; si < 12; si++) {
        Shot *s = &p->shots[si];
        if (s->x == 0) continue;
        ShotBox b = shot_box(p, s);
        for (int i = 0; i < 18; i++) {
            Object *o = &g.objects[i];
            if (o->x == 0) continue;
            if (b.y2 >= o->f54) continue;
            if (b.y1 < o->f52) continue;
            if (b.x2 >= o->f50) continue;
            if (b.x1 < o->f48) continue;
            if (o->flags31 & 0x04) continue;
            if (s->dmg < 0) o->dmg24 += 2;
            else { s->x = 0; o->dmg24 = (uint8_t)(o->dmg24 + (uint8_t)s->dmg); }
            uint16_t points;
            if ((int8_t)(uint8_t)(o->hp28 - o->dmg24) < 0) {
                points = (uint16_t)o->score_bcd44;
                sfx(29);
                if (o->type == 0x20) g_27618 = 0x0C;
            } else {
                points = cw(0x8000 - 19422);          /* $2500 = 25 pts per hit */
                sfx(o->sfx46);
            }
            award(points);
            break;
        }
    }
}

void collide_player_effects(void)                    /* LAB_3748 */
{
    if (g.nova25334) return;
    Player *p = g.sel;
    if (p->invuln52) return;
    int16_t x1 = (int16_t)(p->sx + 0x19), y1 = (int16_t)(p->sy + 0x17);
    for (int i = 0; i < 16; i++) {
        Effect *e = &g.effects[i];
        if ((int16_t)(e->x >> 16) == 0) break;        /* compacted pool: stop at the first free slot */
        int16_t ex = (int16_t)(e->x >> 16), ey = (int16_t)(e->y >> 16);
        if (p->sy >= ey) continue;
        if (y1 < ey) continue;
        if (p->sx >= ex) continue;
        if (x1 < ex) continue;
        hit_player(p);
        effect_remove(i);                             /* the shifted-in record is skipped this frame */
    }
}

static void pickup_369a(Hostile *h)                  /* LAB_369A */
{
    Player *p = g.sel;
    hostile_free_e(h);
    if (h->t28 == 0x0A) {                             /* nova charge */
        sfx(-0x738);
        if (p->nova66 >= 8) { award_10000(); return; }
        p->nova66++;
        hud_stub();
        return;
    }
    sfx(-0x73E);
    for (int i = 0; i < 12; i++) p->shots[i].x = 0;
    /* $36EC: t28>>1 lands in byte 59(A4) = the LOW BYTE of the weapon word 58(A4):
     * a weapon pickup CHANGES THE WEAPON (colour 0..3 == weapon number) */
    p->weapon58 = (int16_t)((h->t28 >> 1) & 0xFF);
    if (p->level60 < 5) {
        p->level60++;
        reload_weapon(p);
        return;
    }
    reload_weapon(p);
    award_10000();
}

void collide_player_hostiles(void)                   /* LAB_35F0 */
{
    Player *p = g.sel;
    int16_t x1 = (int16_t)(p->sx + 0x19), y1 = (int16_t)(p->sy + 0x17);
    int16_t x2 = (int16_t)(p->sx + 7), y2 = (int16_t)(p->sy + 7);
    for (int i = 0; i < 12; i++) {
        Hostile *h = &g.hostiles[i];
        h->flags &= (uint8_t)~HF_DONE;                /* $3614: re-arm for the next game frame */
        if (hxw(h) == 0) continue;
        if (y2 >= h->box[3]) continue;
        if (y1 < h->box[2]) continue;
        if (x2 >= h->box[1]) continue;
        if (x1 < h->box[0]) continue;
        if (h->explode) continue;
        if (h->flags & HF_HARMLESS) continue;
        if (h->type == 5) { pickup_369a(h); continue; }
        if (h->type == 6 && h->frame >= 5) continue;
        if (p->invuln52) continue;
        h->damage += 10;
        hit_player(p);
    }
}

/* ================= game flow ================= */
void extra_life_check(void)                          /* LAB_139C */
{
    Player *p = (g.dframe & 4) ? &g.players[1] : &g.players[0];
    const char *sc = p->score, *pr = p->score_prev;
    int give = 0;
    if (!(sc[0] == '0' && sc[1] == '0')) {
        give = (sc[1] != pr[1]);                      /* $13BA CMP.B 115(A4),D1: 10-million digit changed */
    } else {
        /* both top digits '0': the 100k / 300k / 600k thresholds on digit 2 */
        if (sc[2] == '1' && pr[2] == '0') give = 1;
        else if (sc[2] == '3' && pr[2] == '2') give = 1;
        else if (sc[2] == '6' && pr[2] == '5') give = 1;
    }
    if (give && p->lives56 < 4) {
        p->lives56++;
        sfx(-0x744);
        hud_stub();
    }
    memcpy(p->score_prev, p->score, 4);               /* $1412 MOVE.L 106(A4),114(A4) */
}

static void hiscore_insert_4380(Player *p)           /* LAB_4380: table at $FD1A, 12 x 20 bytes (LODSCO) */
{
    uint32_t s1 = ((uint32_t)(uint8_t)p->score[0] << 24) | ((uint32_t)(uint8_t)p->score[1] << 16) |
                  ((uint32_t)(uint8_t)p->score[2] << 8) | (uint8_t)p->score[3];
    uint32_t s2 = ((uint32_t)(uint8_t)p->score[4] << 24) | ((uint32_t)(uint8_t)p->score[5] << 16) |
                  ((uint32_t)(uint8_t)p->score[6] << 8) | (uint8_t)p->score[7];
    uint32_t a = 0xFD1A;
    int d0 = 11;
    for (; d0 >= 0; d0--, a += 20) {
        uint32_t e1 = cl(a);
        if (s1 > e1 || (s1 == e1 && s2 >= cl(a + 4))) break;
    }
    if (d0 < 0) return;
    if (d0 > 0) {                                     /* shift the tail down */
        for (uint32_t b = 0xFDF2; b > a; b -= 20)
            for (int i = 0; i < 12; i++) bs_chip[b + i] = bs_chip[b - 20 + i];
    }
    for (int i = 0; i < 4; i++) { bs_chip[a + i] = (uint8_t)(s1 >> (24 - 8 * i)); bs_chip[a + 4 + i] = (uint8_t)(s2 >> (24 - 8 * i)); }
    for (int i = 0; i < 4; i++) bs_chip[a - 4 + i] = (uint8_t)p->initials[i];
}

static void player_restart_127e(Player *p)           /* LAB_127E/1288: fire on finished initials */
{
    p->weapon58 = g.start_weapon10060;
    p->level60 = 0;
    reload_weapon(p);
    p->nova90 = 0; p->f91 = 0; p->bonus97 = 0; p->free_respawn41 = 0; p->f100 = 0;
    p->lives56 = (uint8_t)g.start_lives10059;
    p->nova66 = 3;
    p->state38 = 0x96;
    p->explode49 = 0;
    p->repeat57 = 0;
    p->height8 = 0x1E;
    p->invuln52 = 0x12C;
    p->hud68 = p->hud70 = 0x80;
    p->hud72 = 0;
    p->joy44 = 0;
    p->x = p->spawn_x54;
    p->y = 0x200;
    p->entry48 = 0x91;
    p->bank10 = 6;
    p->cooldown46 = 0;
    memset(p->score, '0', 8);
    memcpy(p->score_prev, "0000", 4);
    p->gesture_t96 = 0;
    memset(p->gesture, 0, 4);
    memset(p->shots, 0, sizeof p->shots);
    p->f120 = 0;
    p->f45 = 0;
}

static void initials_4232(Player *p)                 /* LAB_4232 (the on-screen editor; state only) */
{
    if (p->joy44) {                                  /* auto-repeat gate */
        if (p->f40) {
            if (--p->f40) { /* CLR.B 44: swallow this frame's input */ p->joy44 = 0; goto edit; }
            p->f40 = 3;
        } else p->f40 = 0x19;
    } else p->f40 = 0;
edit:
    if (p->f91) {                                    /* LAB_436C: done -> fire restarts the ship */
        if ((p->joy44 & 0x10) && p->f45 == 0) player_restart_127e(p);
        return;
    }
    if (--p->f120 == 0) goto complete;               /* $4266 timeout */
    {
        char *ch = &p->initials[p->cursor42];
        if ((p->joy44 & 0x02) && p->cursor42 != 3) p->cursor42++;
        else if (0) {}
        if ((p->joy44 & 0x08) && p->cursor42 != 0) p->cursor42--;
        if ((p->joy44 & 0x01) && *ch != 'Z') (*ch)++;
        if ((p->joy44 & 0x04) && *ch != '0') (*ch)--;
        p->initials[3] = ' ';
        if (p->cursor42 == 3) p->initials[3] = ']';
        if (p->joy44 & 0x10) {
            if (p->cursor42 != 3) { p->cursor42++; return; }
complete:                                            /* LAB_42F8 */
            p->initials[3] = ' ';
            p->f91 = 0xFF;
            if (p->f45 == 0) {                       /* $4312 */
                p->hud68 = p->hud70 = 0x3E7;
                p->hud72 = 0;
                g.hold16122 = 0xFA;
            }
            hiscore_insert_4380(p);                  /* LAB_4330/4380 */
        }
    }
}

void game_over_check(void)                           /* LAB_410A */
{
    g.no_ship16120 = 0;
    if (!((g.players[0].joined39 && g.players[0].state38 < 0xC8) ||
          (g.players[1].joined39 && g.players[1].state38 < 0xC8)))
        g.no_ship16120 = 0xFF;
    int all_dissolved = 1;                           /* $412E..: every joined+present ship at f68 == $3E6 */
    for (int i = 0; i < 2; i++) {
        Player *p = &g.players[i];
        if (!p->joined39) continue;
        if (p->state38 == 0xFF) continue;
        if (p->hud68 != 0x3E6) { all_dissolved = 0; break; }
    }
    if (all_dissolved) {
        if (g.hold16122) g.hold16122--;              /* $415E: $FA hold */
        else if (g.msg8514 == 0 && g.demo_frames == 0 && g.game_over8524 == 0) {
            g.game_over8524 = -1;                    /* $417C: GAME OVER overlay + LODHIS (audio skipped) */
            g.msg8514 = 1;
            g.msg_text8518 = 0xFCE6;
            g.msg_lines8516 = 14;
            g.msg_hold8522 = 0x40D8;
        }
    }
    for (int i = 0; i < 2; i++) {                    /* LAB_41E0: initials editor per dead ship */
        Player *p = &g.players[i];
        if (p->state38 != 0xC8) continue;
        if (p->f100) memcpy(p->score, "    ", 4);    /* hiscore entry blanks the top digits ($41EE) */
        initials_4232(p);
    }
}

void stage_clear(void)                               /* LAB_7002 (stub: records that the gate fired) */
{
    if (!g.gate4100) return;
    g.done7232 = g.pending7230 ? g.stage7228 : g.done7232;
    /* the full overlay/bonus/advance flow is (L); simrun stops or re-inits per stage instead */
}

/* ================= init + frame ================= */
static void player_reset_1288(Player *p);

static void player_reset_1276(Player *p)             /* LAB_1276 */
{
    p->f45 = 0;
    p->f120 = 0;
    p->weapon58 = g.start_weapon10060;
    p->level60 = 0;
    player_reset_1288(p);
}

static void player_reset_1288(Player *p)             /* LAB_1288 (the demo enters here with
                                                        weapon58/level60 pre-set) */
{
    reload_weapon(p);
    p->nova90 = 0; p->f91 = 0; p->bonus97 = 0; p->free_respawn41 = 0; p->f100 = 0;
    p->lives56 = (uint8_t)g.start_lives10059;
    p->nova66 = 3;
    p->state38 = 0x96;
    p->explode49 = 0;
    p->repeat57 = 0;
    p->height8 = 0x1E;
    p->invuln52 = 0x12C;
    p->hud68 = p->hud70 = 0x80;
    p->hud72 = 0;
    p->joy44 = 0;
    p->x = p->spawn_x54;
    p->y = 0x200;
    p->entry48 = 0x91;
    p->bank10 = 6;
    p->cooldown46 = 0;
    memset(p->score, '0', 8);
    memset(p->score_prev, '0', 8);                    /* the original only writes prev[0..3]; both start '0' */
    p->gesture_t96 = 0;
    memset(p->gesture, 0, 4);
    memset(p->shots, 0, sizeof p->shots);
    if (!p->joined39) {                               /* $1330: not joined */
        p->state38 = 0xFF;
        p->hud68 = 0;
        p->x = p->y = p->sx = p->sy = 0x3E7;
    }
}

/* native: let port 2 join a game in progress (the original does this from the
 * credit/insert path; the port offers it whenever player 2 presses fire) */
void eng_join_player2(void)
{
    Player *p = &g.players[1];
    if (p->joined39) return;
    p->joined39 = 0xFF;
    g.two_players2732 = 0xFF;
    player_reset_1276(p);
}

void eng_init(int stage, int players, int weapon, int lives, int difficulty)
{
    memset(&g, 0, sizeof g);
    render_count = 0;
    g.start_lives10059 = (int16_t)lives;
    g.start_weapon10060 = (int16_t)weapon;
    g.bullet_limit10062 = 15;
    g.bullet_speed14386 = 0x180;
    g.difficulty10066 = (int16_t)difficulty;
    static const uint8_t arm[3][7] = {                /* $10FC/$1130/$115C */
        { 0x23, 0x23, 0x23, 0x37, 0x4B, 0x23, 0x32 },
        { 0x32, 0x32, 0x32, 0x4B, 0x64, 0x32, 0x46 },
        { 0x4B, 0x4B, 0x4B, 0x6E, 0x7D, 0x4B, 0x64 },
    };
    memcpy(g.armour, arm[difficulty < 0 ? 0 : difficulty > 2 ? 2 : difficulty], 7);
    g.t8_acc1786 = 0x2000;
    g.t8_vmax1790 = 0x28000;
    g_8414 = 10; g_8413 = 50; g_8397 = 0; g_27618 = 0;
    memset(g_790A, 0, sizeof g_790A);
    g_14390 = g_14388 = 0;
    g.stage7228 = (int16_t)stage;
    g.stage_desc7224 = 0x14EA + (uint32_t)stage * 0x8C;
    g.rng_index = 0;                                  /* $11B2 (caller may override for calibration) */
    bs_chip[0xCDE6] = 0x07;                           /* $94E: type-3 / type-6 hp descriptor bytes */
    bs_chip[0xCE46] = 0x1F;                           /* (the demo patches them; restore game values) */
    /* LAB_11C6 */
    g.cam7204 = 0x130;
    g.ring7208 = 0x65000;
    g.rowphase7212 = 0;
    g.rows7218 = 0x100;
    g.wave2736 = cl(g.stage_desc7224);
    g.scrolled7222 = 0;
    g.progress7206 = 0;
    g.maprow7214 = 0x4A000;
    /* players */
    for (int i = 0; i < 2; i++) g.players[i].index = i;
    g.players[0].spawn_x54 = 0x140;
    g.players[1].spawn_x54 = 0x1C0;
    g.players[0].joined39 = 0xFF;
    g.players[1].joined39 = (players >= 2) ? 0xFF : 0;
    g.two_players2732 = (g.players[0].joined39 && g.players[1].joined39) ? 0xFF : 0;
    player_reset_1276(&g.players[0]);
    player_reset_1276(&g.players[1]);
    for (int i = 0; i < 12; i++) g.hostiles[i].slot = i;
    for (int i = 0; i < 18; i++) g.objects[i].slot = i;
    g.sel = &g.players[0];
    if (stage == 0) {                                 /* game start: after the LAB_998 pre-scroll ($A48) */
        g.progress7206 = 0xA0;
        g.maprow7214 = 0x49E20;
        g.rowphase7212 = 0;
        g.scrolled7222 = 1;
    } else {                                          /* inner-stage entry (LAB_7180 tail, approximated) */
        g_8414 = 8; g_8413 = 40;                      /* $7370: one gate passed */
        for (int i = 0; i < 256; i++) {               /* $72FC pre-roll */
            scroll_frame();
            object_update_all();
            object_spawner();
        }
    }
}

void eng_demo_init(void)                              /* $6FA: the attract-demo entry */
{
    eng_init(0, 2, 3, 3, 1);                          /* defaults; the block below overrides */
    bs_chip[0xCDE6] = 0x03;                           /* $74A/$750: softer type-3/type-6 hp */
    bs_chip[0xCE46] = 0x17;
    g.bullet_speed14386 = 0x200;                      /* $744 */
    /* $756..$780: both players join with preset weapons */
    g.players[0].weapon58 = 3; g.players[0].level60 = 5;
    g.players[1].weapon58 = 2; g.players[1].level60 = 4;
    g.players[0].joined39 = 0xFF; g.players[1].joined39 = 0xFF;
    g.two_players2732 = 0xFF;
    g.players[0].f45 = 0; g.players[0].f120 = 0;
    g.players[1].f45 = 0; g.players[1].f120 = 0;
    player_reset_1288(&g.players[0]);                 /* $784/$78E */
    player_reset_1288(&g.players[1]);
    /* $7A8..$7CC: recording + mid-level start */
    g.demo_stream = 0x22F80;                          /* the recorded input in LODDAT */
    g.hangars4099 = 0x0E;
    g.wave2736 = 0xD3BE;
    g.progress7206 = 0x0EA0;
    g.maprow7214 = 0x47420;
    g.scrolled7222 = 0;
    for (int i = 0; i < 256; i++) {                   /* LAB_7D0 pre-roll */
        scroll_frame();
        object_update_all();
        object_spawner();
    }
    g.demo = 0xFF;                                    /* $7F8 */
    g.demo_frames = 0;                                /* $7FE */
    g.dframe = 0;                                     /* first attract entry: -28552 still 0 (V: objlog) */
    g.frame_no = 0;
}

/* LAB_45D2: the attract scheduler, called from the LAB_44D0 slot each demo
 * game frame.  Advances -28550 and stages the SCORES/POINTS/CREDITS text
 * overlays; at frame $5DC the ships leave and the wave list goes quiet. */
static void demo_attract_45D2(void)
{
    g.demo_frames++;
    if (g.demo_frames == 0x5DC) {
        g.wave2736 = 0xCF3A;                          /* quiet wave list */
        for (int i = 0; i < 2; i++) {
            g.players[i].x = 0x100;
            g.players[i].state38 = SHIP_NONE;
        }
        g.msg8514 = 1;
        g.msg_text8518 = 0xFCE6;                      /* hiscore board (built at runtime; the
                                                         native renderer substitutes its own) */
        g.msg_lines8516 = 0x0E;
        g.msg_hold8522 = 0x2EE;
        for (int i = 0; i < 16; i++) { g.effects[i].x = 0; g.effects[i].age = 0; }   /* LAB_1D56 (x + age only) */
    } else if (g.demo_frames == 0x7D0) {
        g.msg8514 = 1; g.msg_text8518 = 0x2E508; g.msg_lines8516 = 6; g.msg_hold8522 = 0x2EE;
    } else if (g.demo_frames == 0x9C4) {
        g.msg8514 = 1; g.msg_text8518 = 0x2E582; g.msg_lines8516 = 5; g.msg_hold8522 = 0x2EE;
    } else if (g.demo_frames == 0xBB8) {
        g.msg8514 = 1; g.msg_text8518 = 0x2E5E8; g.msg_lines8516 = 7; g.msg_hold8522 = 0x2EE;
    } else if (g.demo_frames == 0xDAC) {
        g.msg8514 = 1; g.msg_text8518 = 0x2E676; g.msg_lines8516 = 12; g.msg_hold8522 = 0x2EE;
    }
}

void eng_frame_update(void)                           /* LAB_AA0 up to the $BE8 objlog point */
{
    scroll_frame();                                   /* LAB_9C44 */
    object_update_all();                              /* LAB_5F34 */
    hostile_update_all(0, 0);                         /* upper half, pass A (types 3/7/D) */
    hostile_update_all(0, 1);                         /* upper half, pass B */
    hostile_update_all(1, 0);                         /* lower half, pass A */
    hostile_update_all(1, 1);                         /* lower half, pass B */
}

void eng_frame_finish(const uint8_t joy[2])
{
    g.dframe++;                                       /* LAB_5502 (1st) */
    int live = (g.msg8514 == 0 && g.demo_frames < 0x5DC);
    if (live) {
        if (g.demo && g.demo_stream) {                /* LAB_9A9E demo head: raw recorded joy bytes */
            g.players[0].joy44 = cb(g.demo_stream);
            g.players[1].joy44 = cb(g.demo_stream + 1);
            g.demo_stream += 2;
        } else {
        read_input(&g.players[0], joy[0]);            /* LAB_9A9E */
        read_input(&g.players[1], joy[1]);
        }
        player_fire(&g.players[0]);                   /* LAB_3F44 */
        player_fire(&g.players[1]);
        portrait_1e02(&g.players[0]);                 /* LAB_1E02 (1st) */
        portrait_1e02(&g.players[1]);
        move_ship(&g.players[0]);                     /* LAB_5050 (1st) */
        move_ship(&g.players[1]);
        ship_and_shots(&g.players[0]);                /* LAB_51EA (1st) */
        ship_and_shots(&g.players[1]);
        nova();                                       /* LAB_1D0C (1st) */
        effects_update();                             /* LAB_4ADA (1st) */
    }
    if (eng_display_hook) eng_display_hook();         /* native: display-frame sample point (sprite lists built) */
    /* LAB_A30E message driver (state only: the counter; runs twice per game frame) */
    for (int a30e = 0; a30e < 1; a30e++)
        if (g.msg8514) { if (++g.msg8514 >= g.msg_hold8522 + 0xD1) g.msg8514 = 0; }
    g.sel = (g.dframe & 2) ? &g.players[1] : &g.players[0];   /* $C22 */
    collide_shots_hostiles();                         /* LAB_34FA */
    collide_shots_objects();                          /* LAB_3424 */
    collide_player_effects();                         /* LAB_3748 */
    collide_player_hostiles();                        /* LAB_35F0 */
    if (g.demo) demo_attract_45D2();                  /* LAB_44D0's demo branch */
    game_over_check();                                /* LAB_410A (44D0 is otherwise HUD-only) */
    if (live) { if (g_27618) g_27618--; }             /* LAB_1420 (state part) */
    effects_from_requests();                          /* LAB_4704 */
    g.dframe++;                                       /* LAB_5502 (2nd) */
    if (live) {
        portrait_1e02(&g.players[0]);                 /* (2nd round) */
        portrait_1e02(&g.players[1]);
        move_ship(&g.players[0]);
        move_ship(&g.players[1]);
        ship_and_shots(&g.players[0]);
        ship_and_shots(&g.players[1]);
        nova();
        effects_update();
    }
    if (eng_display_hook) eng_display_hook();         /* native: 2nd display frame of this iteration */
    if (g.msg8514) { if (++g.msg8514 >= g.msg_hold8522 + 0xD1) g.msg8514 = 0; }   /* A30E (2nd) */
    /* $CBE quirk: with a message up during demo frames $1F4..$3E7 the wave
     * scheduler is skipped */
    if (!(g.msg8514 && g.demo && g.demo_frames >= 0x1F4 && g.demo_frames < 0x3E8))
    wave_scheduler();                                 /* LAB_7556 */
    object_spawner();                                 /* LAB_3078 */
    extra_life_check();                               /* LAB_139C */
    stage_clear();                                    /* LAB_7002 */
    g.frame_no++;
}

void eng_frame(const uint8_t joy[2])
{
    eng_frame_update();
    eng_frame_finish(joy);
}
