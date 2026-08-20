/* hostiles.c -- the 14 HOSTILE type handlers of Battle Squadron (LAB_79E2 CMPI chain, loader.asm $7A9A..$9752),
 * translated literally from ~/BattleSquadron-Amiga/asm/loader.asm against src/engine/engine.h.
 *
 * Conventions (re/PORTING_GUIDE.md, re/ENGINE_hostiles.md):
 *  - one function per type, called once per game frame for each live record AFTER the engine's shared prologue
 *    (done flag, explosion countdown +29, death -> explosion); the handler ends in one of
 *        draw_frame(h)             LAB_97F8: plane = gfx + frame*frame_stride, mask = plane + stride - frame_bytes
 *        draw_ptr(h, plane, mask)  LAB_981C with explicit pointers (LAB_9814 = draw_ptr(h, h->gfx, h->gfxmask))
 *        skip(h)                   LAB_98D0 without a draw (record stays, nothing drawn this frame)
 *        hostile_free(h)           LAB_98E2: x word := 0
 *        hostile_explode(h)        LAB_98E6 (engine)
 *  - x/y are 16.16: `ADD.W d,(A4)` on the original word == h->x += d << 16 (fraction kept).
 *  - "alternate frame" = g.dframe & 2; the listing reads the low byte of the DISPLAY counter (-28551) everywhere.
 *  - fire requests: h->flags |= HF_FIRE_REQUEST, h->fire_x/fire_y; explicit directions go to hg.fire_dx/fire_dy
 *    (g-14390/-14388) and are consumed by effects_from_requests().
 *  - Amiga bitplane addresses are kept verbatim in h->gfx/h->gfxmask/draw_ptr(): the native renderer maps them.
 *
 * Verbs the engine.h draft does not have yet are declared below as WEAK defaults (override in engine.c).
 */
#include <stdint.h>
#include <stddef.h>
#include "engine/engine.h"

#define WEAK __attribute__((weak))

/* ------------------------------------------------------------------------------------------------------------ */
/* globals used by the hostile code that the engine.h draft (session 1) does not carry                           */
typedef struct { int16_t x, y, count; } BossPuff;              /* $790A / $7910: {x.w, y.w, count.w} */
#define PUFF ((BossPuff *)g_790A)      /* shared with objects.c (engine.h g_790A) */
typedef struct {
    int16_t fire_dx, fire_dy;     /* g-14390 / g-14388: explicit bullet direction for the next fire request (0,0 = aim) */
    int32_t missile_vmax;         /* g-1790 (2.5 px = $28000) */
    int32_t missile_acc;          /* g-1786 ($2000) */
    int8_t  wander_x, wander_y;   /* g-1568 / g-1567: mothership wander step counters (LAB_9542) */
    uint8_t boss_mode;            /* g-26242: end-of-game mothership active (type 2 runs only then) */
    uint8_t boss_destroyed;       /* g-4096: set $FF by the mothership death timer */
    uint8_t screen_flash;         /* g-27618: = 8 by LAB_85E4 */
    uint16_t pop_fire_period, gun_fire_period, bomber_fire_period, missile_fire_period; /* g-2200/-2199/-2198/-2197 */
    BossPuff puff[2];             /* [0] = $790A, [1] = $7910 */
    uint16_t reveal_mask16;       /* type $0D horizontal reveal: the 16 mask words are ANDed with this ($76E8 buffer) */
    uint32_t reveal_mask32;       /* type $03 horizontal reveal: the 32 mask longs are ANDed with this */
} HostileGlobals;
HostileGlobals hg = { 0, 0, 0x28000, 0x2000, 0, 0, 0, 0, 0, 50, 50, 50, 75, {{0,0,0},{0,0,0}}, 0xFFFF, 0xFFFFFFFFu };

/* ------------------------------------------------------------------------------------------------------------ */
/* verbs missing from engine.h: weak defaults so this file links standalone; engine.c may define the real ones   */

/* LAB_981C: collision box from x/box_dx/box_w/y/h, cookie-cut blit of (plane,mask), restore-list entry. */
WEAK void hostile_draw(Hostile *h, uint32_t plane, uint32_t mask)
{
    int16_t x = (int16_t)(h->x >> 16), y = (int16_t)(h->y >> 16);
    h->box[0] = x + h->box_dx; h->box[1] = x + h->box_dx + h->box_w;
    h->box[2] = y;             h->box[3] = y + h->h;
    (void)plane; (void)mask;                                  /* renderer: h->flags |= HF_OFFSCREEN if fully clipped */
}
/* LAB_491C: D7 bits (bit0 = p1 left of (x,y), bit1 = p1 above, bit2/3 = same for p2), d5/d6 = Manhattan distances. */
WEAK int aim_bits(int16_t x, int16_t y, int16_t *d5, int16_t *d6)
{
    int d7 = 0; int16_t d1, d2, d3, d4;
    d1 = g.players[0].sx + 12 - x; if (d1 < 0) { d1 = -d1; d7 |= 1; }
    d2 = g.players[0].sy + 16 - y; if (d2 < 0) { d2 = -d2; d7 |= 2; }
    *d5 = d1 + d2;
    d3 = g.players[1].sx + 12 - x; if (d3 < 0) { d3 = -d3; d7 |= 4; }
    d4 = g.players[1].sy + 16 - y; if (d4 < 0) { d4 = -d4; d7 |= 8; }
    *d6 = d3 + d4;
    return d7;
}
/* LAB_5B3E: paint a 32x32 smoke puff (mask plane `puff`, gfx $577E) INTO the playfield bitmap at (x,y). */
WEAK void paint_smoke(int16_t x, int16_t y, uint32_t puff_mask) { (void)x; (void)y; (void)puff_mask; }
/* LAB_883C probe: true when the playfield byte under (x-$FE, y-$101) is 0 or $FF in all 5 planes (bitmap g7208). */
WEAK int playfield_blank(int16_t x, int16_t y) { (void)x; (void)y; return 0; }
/* type-0 script chain: $FF,$00,ptr.l -> native view of that Amiga address (NULL = unknown -> treated as END). */
WEAK const uint8_t *script_resolve(uint32_t amiga_addr) { (void)amiga_addr; return NULL; }
/* LAB_9986 tail `BRA LAB_79F2`: the re-initialised record is processed AGAIN in the same pass. */
WEAK void hostile_reprocess(Hostile *h)
{
    if (h->flags & HF_DONE) return;
    h->flags |= HF_DONE;
    hostile_handlers[h->type](h);
}

/* ------------------------------------------------------------------------------------------------------------ */
/* small helpers */
static inline int16_t XI(const Hostile *h) { return (int16_t)(h->x >> 16); }
static inline int16_t YI(const Hostile *h) { return (int16_t)(h->y >> 16); }
static inline void set_xi(Hostile *h, int16_t v) { h->x = (int32_t)(((uint32_t)v << 16) | ((uint32_t)h->x & 0xFFFF)); }
static inline void set_yi(Hostile *h, int16_t v) { h->y = (int32_t)(((uint32_t)v << 16) | ((uint32_t)h->y & 0xFFFF)); }
static inline void add_xw(Hostile *h, int16_t d) { h->x += (int32_t)d << 16; }      /* ADD.W d,(A4) */
static inline void add_yw(Hostile *h, int16_t d) { h->y += (int32_t)d << 16; }      /* ADD.W d,4(A4) */
static inline uint8_t gframe(void) { return (uint8_t)g.dframe; }                    /* -28551 */
static inline uint8_t B(int32_t v, int i) { return (uint8_t)((uint32_t)v >> (24 - 8 * i)); } /* big-endian byte i of a long */
static inline void SETB(int32_t *v, int i, uint8_t b)
{ uint32_t sh = 24 - 8 * i; *v = (int32_t)(((uint32_t)*v & ~(0xFFu << sh)) | ((uint32_t)b << sh)); }
static inline int32_t be32(const uint8_t *p) { return (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]); }

static inline void draw_frame(Hostile *h)                   /* LAB_97F8 */
{
    uint32_t plane = h->gfx + (uint32_t)h->frame * (uint16_t)h->frame_stride;
    hostile_draw(h, plane, plane + (uint16_t)h->frame_stride - (uint16_t)h->frame_bytes);
}
static inline void draw_ptr(Hostile *h, uint32_t plane, uint32_t mask) { hostile_draw(h, plane, mask); }  /* LAB_981C */
static inline void skip(Hostile *h) { (void)h; }                                                          /* LAB_98D0 */
static inline void hostile_free(Hostile *h) { h->x = 0; }                                                 /* LAB_98E2 */
static inline void fire_at(Hostile *h, int16_t dx, int16_t dy)
{ h->fire_x = XI(h) + dx; h->fire_y = YI(h) + dy; h->flags |= HF_FIRE_REQUEST; }
static inline void fire_dir(int16_t dx, int16_t dy) { g_14390 = dx; g_14388 = dy; }   /* g-14390/-14388 */

/* LAB_9912 heading16 (types 4, 8): 16 headings from -vy / (vx >> 6); 0 = up, 4 = right, 8 = down, 12 = left */
static void heading16(Hostile *h)
{
    int32_t vx = h->p8, vy = h->p12;
    int16_t d1 = (int16_t)(((uint32_t)vx >> 6) & 0xFFFF);        /* LSR.L #6 then TST.W */
    int32_t num = -vy; uint8_t d5 = 0; int16_t q;
    if (d1 < 0) d5 = 8; else if (d1 == 0) d1 = 0x40;
    { int32_t qq = num / d1; q = (qq > 32767 || qq < -32768) ? (int16_t)num : (int16_t)qq; }   /* DIVS overflow: D2 kept */
    uint8_t d4;
    if (q >= 0) d4 = q >= 0x142 ? 0 : q >= 0x60 ? 1 : q >= 0x2B ? 2 : q >= 0x0D ? 3 : 4;
    else        d4 = q >= -13 ? 4 : q >= -43 ? 5 : q >= -96 ? 6 : q >= -322 ? 7 : 8;
    h->frame = (uint8_t)((d4 + d5) & 0x0F);
}

/* LAB_9986: re-initialise THIS record in place as a pickup (type 5). nova: D7 != 0 (last type-0 of a wave) else a
 * random WEAPON pickup (bomber death).  Ends in `BRA LAB_79F2` = processed again in this pass. */
void hostile_turn_into_pickup(Hostile *h, int nova)
{
    int32_t relx = (int32_t)(XI(h) - g.cam7204 + 8) << 16;
    int16_t y = YI(h) - 0xF4;
    hostile_init(h, 0x63, y, 5, 0);
    h->p12 = relx;                                              /* D4 = screen-relative x, 16.16 */
    if (nova) h->t28 = 0x0A; else h->t28 = rng() & 6;
    if ((int16_t)(h->p12 >> 16) < 0x88) h->p8 = 0x20000;        /* drift right, +2 px/f² */
    else { h->t27 = (uint8_t)~h->t27; h->p8 = (int32_t)0xFFFE0000; }   /* drift left */
    hostile_reprocess(h);
}

/* ============================================================================================================ */
/* type $00 -- scripted plane (LAB_9752 @ $9752): waves of 4, 100 pts, hp 0                                      */
/* vector table $CCF2: 32 x {vx.w, vy.w}; script at h->script: 4-byte commands {dur, dir|scale, turn, turn_timer},
 * $FF,$00,ptr.l = chain, $FF,$01 = END.  idx 0..7/25..31 are NOT the upward half (kept as in the ROM).            */
static const int16_t vec_table[32][2] = {
    {0,17873},{800,17989},{1568,18315},{2272,18874},{2896,19618},{3408,20526},{3792,21550},{4016,22667},
    {4096,0},{4016,1163},{3792,2280},{3408,3304},{2896,4212},{2272,4957},{1568,5515},{800,5841},
    {0,5957},{-800,5841},{-1568,5515},{-2272,4957},{-2896,4212},{-3408,3304},{-3792,2280},{-4016,1163},
    {-4096,0},{-4016,22667},{-3792,21550},{-3408,20526},{-2896,19618},{-2272,18874},{-1568,18315},{-800,17989} };

void h_type_00(Hostile *h)
{
    if (B(h->p8, 0) == 0) {                                     /* $975A: duration expired -> next command */
        const uint8_t *s = h->script;
        for (;;) {                                              /* LAB_9764 */
            if (s == NULL) { hostile_free(h); return; }
            if (s[0] != 0xFF) break;
            if (s[1] != 0) { if (s[1] == 1) { hostile_free(h); return; } break; }   /* $FF,$01 = END; $FF,other: used as a command */
            s = script_resolve((uint32_t)be32(s + 2));          /* $FF,$00,ptr.l = chain */
        }
        h->p8 = be32(s);                                        /* LAB_977E: {dur, dir, turn, turntimer} */
        h->script = s + 4;
        h->p12 = (int32_t)(uint32_t)(h->script - bs_chip);
    }
    /* LAB_9786: velocity = vec_table[dir & 31] * (((dir & $E0) >> 2) + 8)  (scale 8..64 -> idx 8 scale 64 = 4.0 px) */
    {
        uint8_t dir = B(h->p8, 1);
        int16_t sc = (int16_t)(((dir & 0xE0) >> 2) + 8);
        h->x += (int32_t)vec_table[dir & 31][0] * sc;
        h->y += (int32_t)vec_table[dir & 31][1] * sc;
    }
    if (B(h->p8, 3)) {                                          /* $97B6: turn timer */
        SETB(&h->p8, 3, (uint8_t)(B(h->p8, 3) - 1));
        if (B(h->p8, 3) == 0) {
            SETB(&h->p8, 1, (uint8_t)(B(h->p8, 1) + B(h->p8, 2)));   /* dir += turn */
            SETB(&h->p8, 3, h->script[-1]);                     /* reload from the command's byte 3 */
        }
    }
    SETB(&h->p8, 0, (uint8_t)(B(h->p8, 0) - 1));                /* LAB_97D4: dur-- */
    h->frame = (uint8_t)((B(h->p8, 1) & 0x1F) >> 1);            /* 16 heading frames */
    h->gfx = 0x17500;
    if (h->damage) { hostile_explode(h); return; }               /* hp 0: any hit kills (100 pts via LAB_35CE) */
    draw_frame(h);                                              /* LAB_97F8 */
}

/* ============================================================================================================ */
/* type $01 -- aimed bullet-drone (LAB_95D8 @ $95D8): 50 pts, hp 2                                               */
typedef struct { int32_t thr[4], vmax, acc; } DroneTable;
static const DroneTable drone_tab[2] = {
    { { (int32_t)0xFFFE0000, (int32_t)0xFFFF0000, 0x00010000, 0x00020000 }, 0x00030000, 0x3000 },   /* $7916 stage 0 */
    { { (int32_t)0xFFFD8000, (int32_t)0xFFFE8000, 0x00018000, 0x00028000 }, 0x00040000, 0x4000 } }; /* $792E stage>0 */

void h_type_01(Hostile *h)
{
    const DroneTable *T = g.stage7228 ? &drone_tab[1] : &drone_tab[0];
    int fire = 0;
    if (!(h->flags & HF_FIRED_A)) { if (YI(h) >= 0x100) { h->flags |= HF_FIRED_A; fire = 1; } }      /* $95F4 */
    else if (!(h->flags & HF_FIRED_B) && YI(h) >= 0x1A0) { h->flags |= HF_FIRED_B; fire = 1; }      /* LAB_960C */
    if (fire) fire_at(h, 11, 32);                                                                   /* LAB_9622 */
    /* LAB_963E: steer vx (p12) */
    {
        uint16_t d2 = (uint16_t)(YI(h) - 0x1E);
        if (d2 >= (uint16_t)g.players[0].sy && d2 >= (uint16_t)g.players[1].sy) {   /* below both players: brake */
            if (h->p12 != 0) h->p12 += (h->p12 < 0) ? 0x800 : -0x800;
        } else {                                                                    /* LAB_966A */
            int16_t d1 = g.players[0].sx - XI(h); if (d1 < 0) d1 = -d1;
            int16_t d2b = g.players[1].sx - XI(h); if (d2b < 0) d2b = -d2b;
            int16_t tx;
            if (d1 >= d2b) {                                    /* player 2 nearer in |dx| (ties -> 2) */
                tx = g.players[1].sx;
                if ((uint16_t)g.players[1].sy < (uint16_t)g.players[0].sy &&
                    (uint16_t)(g.players[1].sy + 0x1E) < (uint16_t)YI(h)) tx = g.players[0].sx;
            } else {                                            /* LAB_96A0: player 1 */
                tx = g.players[0].sx;
                if ((uint16_t)g.players[0].sy < (uint16_t)g.players[1].sy &&
                    (uint16_t)(g.players[0].sy + 0x1E) < (uint16_t)YI(h)) tx = g.players[1].sx;
            }
            if (tx >= XI(h)) { if (h->p12 != T->vmax) h->p12 += T->acc; }          /* LAB_96BC */
            else             { if (h->p12 != -T->vmax) h->p12 -= T->acc; }
        }
    }
    if (h->damage) {                                            /* LAB_96E0 */
        uint8_t d = h->damage; h->damage = 0;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) { hostile_explode(h); return; }
        h->flash = 6;
    }
    h->x += h->p12;                                             /* LAB_96F8 */
    {
        int32_t vx = h->p12; uint8_t fr;
        fr = vx < T->thr[0] ? 0 : vx < T->thr[1] ? 1 : vx < T->thr[2] ? 2 : vx < T->thr[3] ? 3 : 4;
        if (h->flash) { h->flash--; if (!(h->flash & 2)) fr += 5; }      /* LAB_9724: lit frames */
        h->frame = fr;
    }
    add_yw(h, (int16_t)(T->vmax >> 16));                        /* $973C: y += 3 or 4 px */
    if ((uint16_t)YI(h) >= 0x200) { hostile_free(h); return; }
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $02 -- end-of-game mothership (LAB_8F92 @ $8F92): 4 records at fixed slots 8..11 (L: never captured)     */
/* LAB_9542(D4): slow random wander of the hull using the global step counters g-1567 (y) / g-1568 (x) */
static void mothership_wander(Hostile *h, int8_t d4)
{
    if (hg.wander_y == 0) {
        uint8_t d3 = (uint8_t)((rng() & 0x0E) | 0x02);
        if (YI(h) < 0xF0) d3 &= ~0x08;
        else if (YI(h) >= 0x110 || (d3 & 0x08)) d3 |= 0xF8;
        hg.wander_y = (int8_t)d3;
    }
    if (hg.wander_y >= 0) { hg.wander_y -= d4; add_yw(h, 1); } else { hg.wander_y += d4; add_yw(h, -1); }
    if (hg.wander_x == 0) {                                     /* LAB_9590 */
        uint8_t d3 = (uint8_t)((rng() & 0x1E) | 0x02);
        if (XI(h) < 0x150) d3 &= ~0x10;
        else if (XI(h) >= 0x1D0 || (d3 & 0x10)) d3 |= 0xF0;
        hg.wander_x = (int8_t)d3;
    }
    if (hg.wander_x >= 0) { hg.wander_x -= d4; add_xw(h, 1); } else { hg.wander_x += d4; add_xw(h, -1); }
}
/* LAB_94E8: cannon fire every 8 frames from (x + (rnd&$3F) + 12, y+$24); D4 != 0: aimed when gframe bit3, else random dir */
static void cannon_fire(Hostile *h, int d4)
{
    if (gframe() & 6) return;
    h->flags |= HF_FIRE_REQUEST;
    h->fire_x = XI(h); h->fire_y = YI(h);
    h->fire_x += (int16_t)((rng() & 0x3F) + 0x0C);
    h->fire_y += 0x24;
    if (d4 && (gframe() & 8)) return;
    { int16_t d3 = rng() & 0x7F; if (d3 & 0x40) d3 = (int16_t)(d3 | 0xFFC0); fire_dir(d3, 100); }
}

void h_type_02(Hostile *h)
{
    Hostile *s8 = &g.hostiles[8], *s9 = &g.hostiles[9], *s10 = &g.hostiles[10], *s11 = &g.hostiles[11];
    if (!g.final_boss26242) { skip(h); return; }
    if (h->slot == 8) {                                         /* ---- hull ($8FAE) ---- */
        h->flags |= HF_IMMUNE;
        h->frame_bytes = 0x150; h->frame_stride = 0x690; h->modulo = 0x22; h->h = 0x1C; h->w_words = 7;
        h->box_dx = 2; h->box_w = 0x5C; h->clip_w = 0x60;
        uint8_t gate = s11->t28;                                /* 268(A4) = slot 11 +28 */
        if (gate == 0) { mothership_wander(h, 1); draw_ptr(h, 0x44000, 0x453B0); return; }        /* LAB_90E6 */
        if (gate < 0xC8) { draw_ptr(h, 0x44000, 0x453B0); return; }                                /* LAB_90EC */
        if (gate < 0xFA) { if (gate & 1) { draw_ptr(h, 0x44000, 0x453B0); return; } draw_ptr(h, 0x44690, 0x45500); return; }
        /* LAB_9000: gate >= $FA */
        if (h->t26 == 0) {
            hg.wander_x &= (int8_t)0xFE; hg.wander_y &= (int8_t)0xFE;     /* ANDI.W #$FEFE,-1568(A5) */
            mothership_wander(h, 2);
            if ((gframe() & 6) == 0) {                          /* fire every 8 frames, aimed */
                uint8_t r; h->fire_x = XI(h); h->fire_y = YI(h);
                do r = rng(); while (r >= 0xE0);                /* LAB_9028 */
                h->fire_x += (int16_t)(r - 0x44); h->fire_y += 0x38; h->flags |= HF_FIRE_REQUEST;
            }
            uint8_t sum = (uint8_t)(s9->damage + s10->damage + s11->damage);     /* LAB_9046 */
            if (sum) {
                s9->damage = s10->damage = s11->damage = 0;
                int8_t old = (int8_t)h->t27;
                h->t27 = (uint8_t)(h->t27 - sum);
                if ((int8_t)h->t27 < 0 && old >= 0) {           /* health (t27) went negative: the mothership dies */
                    h->flags |= 0x88; s9->flags |= 0x88; s10->flags |= 0x88; s11->flags |= 0x88;
                    h->flash = 0x66; h->t26 = 0x64;
                } else h->flash = 6;
            }
        }
        /* LAB_909A */
        if (h->flash) {
            if (h->t26) {
                if (h->t26 == 1) { g.finished4096 = 0xFF; draw_ptr(h, 0x44690, 0x45500); return; }   /* $90AE */
                h->t26--;
            }
            h->flash--;
            if (h->flash & 1) { draw_ptr(h, 0x44D20, 0x45650); return; }
        }
        draw_ptr(h, 0x44690, 0x45500);                          /* LAB_90D6 */
        return;
    }
    if (h->slot != 11) {                                        /* ---- side turrets, slots 9/10 (LAB_90FC) ---- */
        int16_t d4 = 0, d5 = 0, d6 = 0;
        if (h->frame < 3) {                                     /* LAB_91BC: alive */
            uint8_t d1 = gframe(); int16_t d2 = 20;
            if (h->slot == 10) { d1 += 2; d2 = 38; }
            if ((d1 & 0x0E) == 0) {                             /* fire every 16 frames (slot 10 offset by 2), random dir */
                h->fire_x = XI(h) + d2; h->fire_y = YI(h) + 0x14; h->flags |= HF_FIRE_REQUEST;
                h->fire_x += rng() & 0x0F;
                { int16_t d3 = rng() & 0x3F; if (d3 & 0x20) d3 = (int16_t)(d3 | 0xFFE0); fire_dir(d3, 0x32); }
            }
            h->frame = (gframe() & 0x20) ? 0 : 1;               /* LAB_9216 */
            if (h->damage) {
                uint8_t d = h->damage; h->damage = 0;
                h->hp = (int8_t)(h->hp - d);
                if (h->hp < 0) { h->frame = 3; h->flags |= HF_IMMUNE; goto place; }
                h->flash = 6;
            }
            if (h->flash) { h->frame = 0; h->flash--; if (h->flash & 1) h->frame += 2; }
        } else if (h->frame < 8) {                              /* LAB_91A8: dying frames 3..7 every 8 frames */
            if ((gframe() & 6) == 0) h->frame++;
        } else {                                                /* $9120: dead, steered by the cannon's death phase */
            h->frame = 8;
            uint8_t d1 = s11->t28;                              /* EXT_2E00C */
            if (d1 < 0xC8) goto place;
            if (d1 >= 0xFA) {                                   /* LAB_9144 */
                h->flags &= ~HF_IMMUNE;
                if (!(gframe() & 0x20)) { h->frame++; d6 = (h->slot == 10) ? 2 : 1; }
            } else if (d1 & 1) goto place;
            h->frame++; d5 = -2; d4 = 6;                        /* LAB_9162 */
            d1 = s8->flash;                                     /* EXT_2DF39 */
            if (d1 == 0) goto place;
            d6 = 0; h->frame = 0x0A;
            if (s8->t26) h->flags |= HF_IMMUNE;                 /* EXT_2DF1A */
            if (s8->t26 == 1) goto place;
            if (!(d1 & 1)) goto place;
            h->frame = 0x0B;
        }
    place:                                                      /* LAB_9264 */
        if (h->slot == 10) {
            h->gfx = 0x565A0;
            set_xi(h, (int16_t)(XI(s8) + 0x60 + d5)); set_yi(h, (int16_t)(YI(s8) + 0x28 + d6));
            if (d4) add_yw(h, 2);
            draw_frame(h); return;
        }
        set_xi(h, (int16_t)(XI(s8) - 0x50 + d5)); set_yi(h, (int16_t)(YI(s8) + 0x26 + d4 + d6));    /* LAB_929C */
        if (h->frame >= 9) add_yw(h, -1);
        draw_frame(h); return;
    }
    /* ---- central cannon, slot 11 (LAB_92CC) ---- */
    {
        uint32_t a2, a3 = 0x50060; int16_t d5 = 0;
        h->frame_bytes = 0x240; h->frame_stride = 0xB40; h->modulo = 0x22; h->h = 0x30; h->w_words = 7;
        h->box_dx = 2; h->box_w = 0x5C; h->clip_w = 0x60;
        if (h->frame < 2) {                                     /* LAB_945A: alive */
            h->frame = 0;
            if (s10->frame < 3 || s9->frame < 3) { h->damage = 0; goto frame_gfx; }   /* only damageable once both turrets are dead */
            cannon_fire(h, 0xFF);
            if (h->damage) {
                uint8_t d = h->damage; h->damage = 0;
                h->hp = (int8_t)(h->hp - d);
                if (h->hp < 0) { h->frame = 2; h->flags |= HF_IMMUNE; h->hp = 0x7F; goto frame_gfx; }
                h->flash = 6;
            }
            if (h->flash) { h->flash--; if (h->flash & 1) h->frame++; }
            goto frame_gfx;
        }
        h->hitsnd = 0x35;
        if (h->frame < 7) { if ((gframe() & 6) == 0) h->frame++; goto frame_gfx; }   /* dying 3..6 */
    dead:                                                       /* LAB_9324 */
        h->frame = 7;
        if (h->t28 == 0) {                                      /* LAB_93F8: destroyed but still firing */
            h->flags &= ~HF_IMMUNE;
            cannon_fire(h, 0);
            if (gframe() & 0x10) h->frame++;
            if (h->damage) {
                uint8_t d = h->damage; h->damage = 0;
                h->hp = (int8_t)(h->hp - d);
                if (h->hp < 0) { h->flags |= HF_IMMUNE; h->t28 = 1; goto dead; }
                h->flash = 6;
            }
            if (h->flash) { h->flash--; h->frame = 8; if (h->flash & 1) h->frame++; }
            goto frame_gfx;
        }
        if (h->t28 < 0x32) { h->t28++; if (!(h->t28 & 1)) h->frame += 3; goto frame_gfx; }   /* slow flash */
        h->frame += 3; h->t28++;
        if (h->t28 < 0xC8) goto frame_gfx;
        h->t28--;
        if (h->t28 < 0xFA) {
            h->t28++;
            if (!(h->t28 & 1)) goto frame_gfx;
            a2 = 0x4D360; a3 = 0x502A0; d5 = -2; goto place_c;  /* big explosion frame A */
        }
        /* LAB_938E: final */
        h->flags &= ~HF_IMMUNE;
        a2 = 0x4DEA0; a3 = 0x504E0; d5 = -2;
        {
            uint8_t d1 = s8->flash;                             /* -183(A4) = slot 8 flash */
            if (d1 == 0) { if (!(gframe() & 0x20)) { a2 -= 0xB40; a3 -= 0x240; } goto place_c; }   /* LAB_93E2 */
            if (s8->t26) h->flags |= HF_IMMUNE;                 /* -214(A4) = slot 8 t26 */
            if (s8->t26 == 1) { a2 += 0x1680; a3 += 0x480; goto place_c; }       /* LAB_93CA */
            if (d1 & 1) { a2 += 0xB40; a3 += 0x240; goto place_c; }               /* LAB_93D6 */
            if (s8->t26 == 0) goto place_c;
            a2 += 0x1680; a3 += 0x480; goto place_c;
        }
    frame_gfx:                                                  /* LAB_94BE */
        a2 = 0x457A0 + (uint32_t)(h->frame & 0x0F) * 0xB40;
    place_c:                                                    /* LAB_94D2 */
        set_xi(h, (int16_t)(XI(s8) + d5)); set_yi(h, (int16_t)(YI(s8) + 0x1C));
        draw_ptr(h, a2, a3);
    }
}

/* ============================================================================================================ */
/* type $03 -- scripted ground pop-up (LAB_8D6E @ $8D6E): 500 pts, hp 5; script {dur, step, phase, phase_step}  */
/* Does FIRE (LAB_8F00): t27 (preloaded 9) counts down, reloads from g-2200, fires aimed at (x+10, y+17) when it
 * reaches 5 while fully out (h == 32), open (phase bit6 clear) and alive; frames $36C00/$36E80 while t27 <= 9.  */
void h_type_03(Hostile *h)
{
    uint32_t a2 = 0x36000, a3 = 0x36280;
    h->flags |= HF_HARMLESS;
    if (B(h->p8, 0) == 0) {                                     /* fetch next 4-byte step */
        h->p8 = be32(h->script); h->script += 4;
        h->p12 = (int32_t)(uint32_t)(h->script - bs_chip);
        if ((uint16_t)((uint32_t)h->p8 >> 16) == 0) { hostile_free(h); return; }   /* zero first WORD = end */
    }
    uint8_t d4 = B(h->p8, 2);                                   /* phase before the step */
    SETB(&h->p8, 2, (uint8_t)(B(h->p8, 2) + B(h->p8, 3)));      /* phase += phase_step */
    int16_t d2 = B(h->p8, 2) & 0x3F;                            /* visible rows */
    h->h = d2;
    int8_t dur = (int8_t)B(h->p8, 0);
    if (B(h->p8, 2) & 0x40) {                                   /* $8DBC: closed / armoured look, immune */
        if (dur < 0) { a2 += 0x900; a3 += 0x900; } else { a2 += 0x600; a3 += 0x600; }
        h->damage = 0; h->flags |= HF_IMMUNE;
    } else if (h->t26) {                                        /* LAB_8DF0: dying (15 frames of explosion gfx) */
    dying:
        SETB(&h->p8, 1, 0); SETB(&h->p8, 3, 0);                 /* step = phase_step = 0 */
        if (!(h->t26 & 1)) { h->gfx += 0x300; h->gfxmask += 0x300; }
        a2 = h->gfx; a3 = h->gfxmask;
        h->t26--;
        if (h->t26 == 0) { hostile_free(h); return; }
    } else {                                                    /* LAB_8E22: open */
        h->flags &= ~HF_IMMUNE;
        if (h->damage) {
            uint8_t d = h->damage; h->damage = 0; h->flash = 4;
            h->hp = (int8_t)(h->hp - d);
            if (h->hp < 0) {
                h->t26 = 0x0F; h->flags = (uint8_t)((h->flags & ~HF_FIRE_REQUEST) | HF_IMMUNE);
                SETB(&h->p8, 2, d4);                            /* phase -= phase_step */
                h->gfx = 0x11090; h->gfxmask = 0x11310;
                goto dying;
            }
        }
        if (h->flash) { h->flash--; if (h->flash & 1) { a2 += 0x300; a3 += 0x300; } }
    }
    /* LAB_8E84: phase bit7 while moving in y: show only the bottom rows (gfx += (32-h)*4) */
    if ((B(h->p8, 2) & 0x80) && dur >= 0) { uint16_t o = (uint16_t)((0x20 - d2) << 2); a2 += o; a3 += o; }
    {   int16_t step = (int8_t)B(h->p8, 1);                     /* LAB_8E9E */
        if (dur < 0) add_xw(h, step); else add_yw(h, step); }
    add_yw(h, g.scrolled7222);
    if (h->t26 == 0) {
        SETB(&h->p8, 0, (uint8_t)(dur < 0 ? dur + 1 : dur - 1));   /* dur toward 0 */
        h->flags &= ~HF_FIRE_REQUEST;                           /* LAB_8ED0 */
        if (!(B(h->p8, 2) & 0x40) && h->h == 0x20) {            /* open and fully out */
            if ((int8_t)h->t27 <= 9 && h->flash == 0) { a2 = 0x36C00; a3 = 0x36E80; }   /* firing look */
            h->t27--;
            if (h->t27 == 0) h->t27 = g.armour[0];   /* g-2200 */
            if (h->t27 == 5) fire_at(h, 0x0A, 0x11);
        }
    }
    if ((uint16_t)YI(h) >= 0x200) { hostile_free(h); return; }   /* LAB_8F30 */
    if (h->h == 0) { skip(h); return; }
    if (dur >= 0) { draw_ptr(h, a2, a3); return; }
    /* moving in x: horizontal reveal -- the 32 mask longs are ANDed with a (32-h)-bit shifted mask ($76E8) */
    if (B(h->p8, 2) != 0x20) {
        uint8_t d3 = (uint8_t)(0x20 - (B(h->p8, 2) & 0x3F));
        hg.reveal_mask32 = (B(h->p8, 2) & 0x80) ? (0xFFFFFFFFu << (d3 & 31)) : (0xFFFFFFFFu >> (d3 & 31));
        render_reveal_mask = hg.reveal_mask32;
        h->h = 0x20;
    }
    draw_ptr(h, a2, a3);
}

/* ============================================================================================================ */
/* type $04 -- swooping fighter (LAB_8B48 @ $8B48): 100 pts, hp 2                                                */
typedef struct { int32_t vmax, acc; int16_t divealt; } SwoopTable;
static const SwoopTable swoop_tab[2] = { { 0x40000, 0x2000, 64 }, { 0x50000, 0x3000, 56 } };  /* $7946 / $7952 */

static void new_weave(Hostile *h)                               /* LAB_8B96 (= LAB_8986 of the bomber) */
{
    uint8_t d3 = (uint8_t)(rng() | 7);
    if (d3 & 0x20) d3 = (uint8_t)(-(int8_t)(d3 & 0x0F)); else d3 &= 0x0F;   /* +-7 or +-15 */
    h->t27 = d3; h->t28 = (uint8_t)(-(int8_t)d3);
}

void h_type_04(Hostile *h)
{
    const SwoopTable *T = g.stage7228 ? &swoop_tab[1] : &swoop_tab[0];
    if (h->damage) {                                            /* $8B62 */
        uint8_t d = h->damage; h->flash = 8; h->damage = 0;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) { hostile_explode(h); return; }
    }
    if (h->t26 == 0) {                                          /* $8B7E: first frame */
        h->t26++; h->frame = 8; h->p8 = 0; h->p12 = T->vmax;
        new_weave(h);
    }
phase1:                                                         /* LAB_8BBA */
    if (h->t26 == 1) {                                          /* PHASE 1: dive straight down, weaving */
        h->frame = 8;
        if (!(h->flags & HF_FIRED_A) && YI(h) >= 0x100) { h->flags |= HF_FIRED_A; fire_at(h, 0x0B, 0x20); }
        if (g.players[0].state38 < 0x64 || g.players[1].state38 < 0x64) {   /* LAB_8BFC: a player is alive */
            int16_t d1 = g.players[0].sx - XI(h), d3 = g.players[1].sx - XI(h), d2; uint8_t d4 = 0;
            if (d1 < 0) { d1 = -d1; d4 |= 1; }
            if (d3 < 0) { d3 = -d3; d4 |= 2; }
            if (d1 < d3) d2 = g.players[0].sy; else { d2 = g.players[1].sy; d4 >>= 1; }
            d2 -= T->divealt;
            if (d2 < YI(h)) {                                   /* reached the player's altitude band */
                h->g42 = (int16_t)((h->g42 & 0xFF00) | (uint8_t)d2);   /* MOVE.B D2,42(A4) */
                h->t26++; h->t27 = d4;
                goto phase2;
            }
        }
        /* LAB_8C56: weave vx +-$1000 per frame for |t27| frames, then the opposite, then a new random weave */
        if ((int8_t)h->t27 < 0) {
            h->p8 += 0x1000; h->t27++;
            if (h->t27 == 0) { if (h->t28 == 0) { new_weave(h); goto phase1; } h->t27 = h->t28; h->t28 = 0; }
        } else {
            h->p8 -= 0x1000; h->t27--;
            if (h->t27 == 0) { if (h->t28 == 0) { new_weave(h); goto phase1; } h->t27 = h->t28; h->t28 = 0; }
        }
        goto flash;
    }
    if (h->t26 == 2) {                                          /* PHASE 2: bank toward the remembered side, pull UP */
    phase2:
        if (!(h->flags & HF_FIRED_B)) {                         /* LAB_8CAC: second shot when heading right (4) / left (12) */
            if (h->frame == 4) { fire_at(h, 32, 11); h->flags |= HF_FIRED_B; }
            else if (h->frame == 12) { fire_at(h, -8, 11); h->flags |= HF_FIRED_B; }
        }
        {   int32_t a = h->p8 < 0 ? -h->p8 : h->p8;
            if (a < T->vmax) { if (h->t27 & 1) h->p8 -= T->acc; else h->p8 += T->acc; }
            h->p12 -= T->acc; }                                 /* climbs without lower bound */
    }
    heading16(h);                                               /* LAB_8D14 */
flash:                                                          /* LAB_8D18 */
    if (h->flash) { h->flash--; if (h->flash & 2) h->frame += 0x10; }
    h->x += h->p8; h->y += h->p12;
    if (XI(h) <= g.cam7204 - 0x20 || XI(h) >= g.cam7204 + 0x120 || YI(h) >= 0x200 || YI(h) <= 0xE0) { hostile_free(h); return; }
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $05 -- pickup (LAB_8A8A @ $8A8A): t28 $0A nova / 0,2,4,6 weapon; screen-locked drift, sinks 0.5 px       */
void h_type_05(Hostile *h)
{
    h->flags |= HF_IMMUNE;
    if ((int8_t)h->t27 < 0) {                                   /* drifting left */
        if ((int16_t)(h->p12 >> 16) < 0x47) h->t27 = (uint8_t)~h->t27;
        if (h->p8 > (int32_t)0xFFFC0000) h->p8 -= 0x2000;
    } else {                                                    /* LAB_8AC0: drifting right */
        if ((int16_t)(h->p12 >> 16) > 0xC9) h->t27 = (uint8_t)~h->t27;
        if (h->p8 < 0x40000) h->p8 += 0x2000;
    }
    h->y += g.demo ? 0x6000 : 0x8000;                           /* LAB_8ADE */
    if (YI(h) >= 0x200) { hostile_free(h); return; }
    h->p12 += h->p8;                                            /* rel-x += drift */
    set_xi(h, (int16_t)(g.cam7204 + (int16_t)(h->p12 >> 16)));
    if (h->t28 == 0x0A) h->frame = 0x0A;
    else if (h->p8 == 0) h->t28 = (uint8_t)((h->t28 + 2) & 6); /* weapon pickups cycle kind when the drift reverses */
    h->frame = (uint8_t)(((gframe() >> 2) & 1) + h->t28);      /* LAB_8B32: 2-frame sparkle */
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $06 -- bomber (LAB_890C @ $890C): 1000 pts, hp 23, 3-shot spread every g-2198 frames, dies into a pickup */
void h_type_06(Hostile *h)
{
    if (h->frame >= 5) {                                        /* dying: frames 5..10 on alternate frames */
        if (gframe() & 2) { draw_frame(h); return; }
        h->frame++;
        if (h->frame >= 11) { hostile_turn_into_pickup(h, 0); return; }   /* LAB_9986, D7 = 0: random weapon pickup */
        draw_frame(h); return;
    }
    h->frame = 0;
    if (h->damage) {                                            /* $8942 */
        uint8_t d = h->damage; h->damage = 0; h->flash = 4;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) { h->flags |= HF_IMMUNE; h->frame = 5; draw_frame(h); return; }
    }
    if (h->flash) { h->flash--; h->frame = (uint8_t)(4 - h->flash); }   /* LAB_8968: hit frames 1..4 */
    if (h->t27 == 0) new_weave(h);                              /* LAB_8986 */
    for (;;) {                                                  /* LAB_89AA (BEQ.S LAB_8986 falls back into the step) */
        if ((int8_t)h->t27 < 0) { h->p8 += 0x1000; h->t27++; if (h->t27) break; }
        else                    { h->p8 -= 0x1000; h->t27--; if (h->t27) break; }
        if (h->t28) { h->t27 = h->t28; h->t28 = 0; break; }
        new_weave(h);
    }
    h->x += h->p8;                                              /* LAB_89EA */
    if (h->t26 == 0) h->t26 = g.armour[2];  /* g-2198 (50) */
    h->t26--;
    h->fire_x = XI(h); h->fire_y = YI(h) + 0x28;
    if (h->t26 == 16) { h->fire_x += 0x0B; h->flags |= HF_FIRE_REQUEST; }                        /* aimed */
    if (h->t26 == 8)  { fire_dir(-0x20, (int16_t)((g.dframe & 0x7F) | 0x40)); h->fire_x += 2; h->flags |= HF_FIRE_REQUEST; }
    if (h->t26 == 0)  { fire_dir(0x20, (int16_t)((g.dframe & 0x7F) | 0x40)); h->fire_x += 0x14; h->flags |= HF_FIRE_REQUEST; }
    h->y += 0x14000;                                            /* 1.25 px */
    if (YI(h) >= 0x200) { hostile_free(h); return; }
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $07 -- rising ground gun (LAB_87E4 @ $87E4): 500 pts, hp 5                                               */
void h_type_07(Hostile *h)
{
    add_yw(h, g.scrolled7222);
    h->flags |= HF_HARMLESS;
    if (YI(h) >= 0x200) { hostile_free(h); return; }
    if (h->frame >= 3) {                                        /* death frames 3..10 on alternate frames */
        if (gframe() & 2) { draw_frame(h); return; }
        h->frame++;
        if (h->frame >= 11) { hostile_free(h); return; }
        draw_frame(h); return;
    }
    if (h->t27 < 0x20) { h->t27++; h->h = h->t27; }             /* grows one row per frame out of the ground */
    h->frame = 0;
    if (playfield_blank(XI(h), YI(h))) {                        /* LAB_883C terrain probe: climbs 1 px on blank ground */
        add_yw(h, -1);
        if (!(gframe() & 2)) h->frame++;
    }
    if (h->damage) {                                            /* LAB_8880 */
        uint8_t d = h->damage; h->damage = 0; h->flash = 4;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) {
            h->gfx = 0x10790; h->frame = 3;
            h->flags = (uint8_t)((h->flags | HF_IMMUNE) & ~HF_FIRE_REQUEST);
            draw_frame(h); return;
        }
        goto flash;                                             /* LAB_88BA (flash was just set to 4) */
    }
    if (h->flash) {
    flash:
        h->flash--; if (h->flash & 1) h->frame = 2;
    }
    h->flags &= ~HF_FIRE_REQUEST;                               /* LAB_88CC */
    if (h->t27 >= 0x14) {                                       /* fires every g-2199 frames once 20 rows are out */
        if (h->t28 == 0) { h->t28 = g.armour[1]; fire_at(h, 0x0C, 0x0A); }
        h->t28--;
    }
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $08 -- homing missile (LAB_8664 @ $8664): 750 pts, hp 2                                                  */
void h_type_08(Hostile *h)
{
    int d7;
    if (h->damage) {
        uint8_t d = h->damage; h->damage = 0;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) { hostile_explode(h); return; }
        h->flash = 8;
    }
    if (h->t28 == 0) {                                          /* $868C: launched away, straight heading */
        if (g.cam7204 - 0x20 >= XI(h)) { hostile_free(h); return; }
        if (g.cam7204 + 0x120 < XI(h)) { hostile_free(h); return; }
        d7 = h->flags;
        { int16_t d2 = (int16_t)((((g.dframe & 0xFF) ^ 0xFF) + 0x100)); if (d2 < YI(h)) d7 |= 2; }
    } else {                                                    /* LAB_86C4: homing for t28 (200) frames */
        h->t28--;
        if (h->t28 == 0 && XI(h) <= 0x1B0) h->flags |= HF_FIRED_A;   /* break off to the LEFT when on the left part */
        int16_t d5, d6;
        d7 = aim_bits(XI(h), YI(h), &d5, &d6);                  /* LAB_491C */
        if (d5 >= d6) d7 >>= 2;                                 /* nearer player's bits */
    }
    {   int32_t vmax = g.t8_vmax1790, acc = g.t8_acc1786;   /* LAB_86EC: g-1790 / g-1786 */
        if (d7 & 1) { if (-vmax <= h->p8) h->p8 -= acc; } else { if (vmax >= h->p8) h->p8 += acc; }
        /* vy quirk ($871E BGT.S LAB_8726): an overshoot past -vmax falls into the ADD branch and bounces */
        if ((d7 & 2) && -vmax <= h->p12) h->p12 -= acc;
        else if (vmax >= h->p12) h->p12 += acc; }
    heading16(h);                                               /* LAB_9912 */
    if (h->t26 == 0) { h->t26 = 0xFF; h->t27 = 0x14; }          /* first shot after 20 frames */
    h->t27--;
    if (h->t27 == 0) { h->t27 = g.armour[3]; fire_at(h, 0x0C, 0x0C); }   /* g-2197 (75) */
    {   int smoke;                                              /* LAB_876C */
        if (h->t27 >= 8 || !(h->t27 & 1)) {
            if (h->flash) { h->flash--; smoke = (h->flash & 1) != 0; } else smoke = 1;
        } else smoke = 0;
        if (smoke) {                                            /* LAB_878E: exhaust puff painted into the playfield */
            h->gfxmask = 0x1A780 + (uint32_t)(h->frame & 0x0F) * 0x300;
            h->gfx = 0x577E;
            paint_smoke(XI(h), YI(h), h->gfxmask);              /* LAB_5B3E */
            h->x += h->p8; h->y += h->p12;
            draw_ptr(h, h->gfx, h->gfxmask);                    /* LAB_9814 */
            /* Displayed: no bob -- the renderer turns this $577E entry into
             * the exhaust-puff sprite (proven vs shots/i_18000). */
            return;
        }
    }
    h->x += h->p8; h->y += h->p12;                              /* LAB_87CA */
    h->gfx = 0x17500;
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $09 -- stage boss (LAB_7FC4 @ $7FC4): body slot 8 + gun pod slot 9; 5000 pts, hp 95                      */
static const uint8_t boss_frame_tab[32] = {                     /* $797E[gframe >> 3] */
    0,0,0,0,0,0,1,2,3,4,5,5,5,5,5,5,5,5,5,5,5,5,4,3,2,1,0,0,0,0,0,0 };
static const uint8_t boss_bob_tab[64] = {                       /* $799E[(gframe >> 1) & $3F], mirrored above $40 */
    1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,9,9,10,11,11,12,13,14,15,16,16,16,16,16,16,16,16,
    16,16,15,15,14,14,13,13,12,12,11,11,10,10,9,9,8,8,7,7,6,6,5,5,4,4,3,3,2,2,1,1 };
/* stage-1 boss path $2EF20: 400 steps x {dx.w, dy.w}; the native asset layer fills this in (weak). */
WEAK const int16_t *boss_path_2EF20(void) { return NULL; }

/* LAB_85E4(D4,D5,D6): boss explosion puffs. hp_before = D3 = hp before this frame's damage. */
static void boss_puff(Hostile *h, int8_t hp_before, int16_t d4, int16_t d5, int16_t d6)
{
    BossPuff *p;
    if (h->hp < 0x10) {                                         /* dying: on gframe bit3 use slot 0, else slot 1 -- only if free */
        p = (gframe() & 8) ? &PUFF[0] : &PUFF[1];
        if (p->count != 0) return;
    } else {
        if ((h->hp & 7) <= (hp_before & 7)) return;             /* puff only when hp crossed an 8 boundary downward */
        p = (PUFF[0].count < PUFF[1].count) ? &PUFF[0] : &PUFF[1];
    }
    int16_t d3 = (int16_t)((rng() & 0x3F) + d6);                /* LAB_8632 */
    p->x = d3; p->y = d4;
    if (d3 >= 0x10 && d3 < 0x30) p->x -= d5;
    p->count = 8;
    g_27618 = 8;
    sfx(31);
}

static void boss_damage_halved(Hostile *h, uint8_t d)           /* LSR.B #1,D1; min 1; SUB.B D1,24(A4) */
{ d >>= 1; if (d == 0) d = 1; h->hp = (int8_t)(h->hp - d); }

void h_type_09(Hostile *h)
{
    Hostile *s8 = &g.hostiles[8], *pod = &g.hostiles[9];
    if (g.stage7228 == 1) {                                     /* ======== stage-1 boss (LAB_8398) ======== */
        if (h->slot == 9) {                                     /* ---- pod (LAB_8526) ---- */
            if (g.boss_hold1570 == 0) g.boss_hold1570 = 0xFF;
            h->frame_bytes = 0x240; h->frame_stride = 0xD80; h->h = 0x30; h->gfx = 0x31960;
            set_xi(h, XI(s8)); set_yi(h, (int16_t)(YI(s8) + 0x20));
            if (h->flags & HF_IMMUNE) { draw_frame(h); return; }
            int8_t hp0 = h->hp;
            if (h->damage) {
                uint8_t d = h->damage; h->damage = 0;
                boss_damage_halved(h, d);
                if (h->hp < 0) { h->flags |= HF_IMMUNE; h->frame = 3; draw_frame(h); return; }
                h->flash = 6;
            }
            boss_puff(h, hp0, 40, 8, 0);                        /* LAB_85E4(40,8,0) */
            if (h->hp < 0x10) { h->frame = 3; draw_frame(h); return; }
            if (h->flash) { h->flash--; if (h->flash & 1) { h->frame = 2; draw_frame(h); return; } }
            h->frame = (gframe() & 0x10) ? 0 : 1;               /* LAB_85CE */
            draw_frame(h); return;
        }
        /* ---- body ---- */
        if (h->flags & HF_IMMUNE) {                             /* dead: scrolls off, then frees 9/10/11 and itself */
            g.boss_hold1570 = 0x64;
            add_yw(h, g.scrolled7222);
            if (YI(h) < 0x200) { draw_frame(h); return; }
            g.boss_hold1570 = 0;
            g.hostiles[11].x = 0; g.hostiles[10].x = 0; g.hostiles[9].x = 0;
            hostile_free(h); return;
        }
        {   /* LAB_83D6: path table $2EF20, 4 bytes/step, index = high word of p12, wraps at $640 */
            uint16_t idx = (uint16_t)((uint32_t)h->p12 >> 16);
            const int16_t *path = boss_path_2EF20();
            int16_t dx = 0, dy = 0;
            if (path) { dx = path[idx / 2]; dy = path[idx / 2 + 1]; }
            idx += 4; if (idx >= 0x640) idx = 0;
            h->p12 = (int32_t)(((uint32_t)idx << 16) | ((uint32_t)h->p12 & 0xFFFF));
            if (g.progress7206 >= 0xFA0) dx = (int16_t)-dx;
            h->x += (int32_t)dx << 4; h->y += (int32_t)dy << 4;
            if (h->t27 < 0xFA) { h->t27++; h->y += 0x4E20; }    /* descends for the first 250 frames */
        }
        int8_t hp0 = h->hp;
        if (pod->frame != 3) h->damage = 0;                     /* LAB_8422: only damageable once the pod is destroyed */
        else {
            if (h->damage) {
                uint8_t d = h->damage; h->damage = 0;
                boss_damage_halved(h, d);
                if (h->hp < 0) { h->frame = 3; h->flags |= 0x88; pod->flags |= 0x88; goto fire1; }
                h->flash = 6;
            }
            if (pod->hp < 0) boss_puff(h, hp0, 0, 0, 0);         /* LAB_8464: LAB_85E4(0,0,0) while the pod is dead */
            if (h->hp < 0x10) { h->frame = 3; goto fire1; }
            if (h->flash) { h->flash--; if (h->flash & 1) { h->frame = 2; goto fire1; } }
            h->frame = (gframe() & 0x20) ? 0 : 1;               /* LAB_849E */
        }
    fire1:                                                      /* LAB_84AE: 48-frame cycle (40 after progress 6000) */
        {
            uint16_t d2 = (g.progress7206 >= 0x1770) ? 40 : 48;
            uint16_t ph = (uint16_t)(((g.dframe | 1) % d2) & ~1u);
            int16_t fx;
            if (ph == 2) fx = 53;
            else if (ph == 20) fx = 33;
            else if (ph == 8 || ph == 14) {
                fx = 43;                                        /* LAB_84EC: random direction */
                int16_t d3 = (int8_t)rng(); fire_dir(d3, 0);
                d3 = (int16_t)(((int16_t)rng() << 2) | 0x3F); g_14388 = d3;
            } else { draw_frame(h); return; }
            h->fire_x = XI(h) + fx; h->fire_y = YI(h) + 0x38; h->flags |= HF_FIRE_REQUEST;   /* LAB_8508 */
            draw_frame(h); return;
        }
    }
    if (h->slot == 9) {                                         /* ---- other stages: pod (LAB_82E0) ---- */
        uint32_t a2 = 0x36BF0, a3 = 0x37208;
        if (g.boss_hold1570 == 0) g.boss_hold1570 = 0xFF;
        h->frame_bytes = 0x138; h->frame_stride = 0x750; h->modulo = 0x26; h->h = 0x27; h->w_words = 5;
        h->box_dx = 2; h->box_w = 0x3C; h->clip_w = 0x40;
        set_xi(h, (int16_t)(XI(s8) + 0x1D)); set_yi(h, (int16_t)(YI(s8) + 0x30));
        if (h->flags & HF_IMMUNE) { draw_ptr(h, a2, a3); return; }
        int8_t hp0 = h->hp;
        if (h->damage) {
            uint8_t d = h->damage; h->damage = 0;
            h->hp = (int8_t)(h->hp - d);
            if (h->hp < 0) { h->flags |= HF_IMMUNE; draw_ptr(h, a2, a3); return; }
            h->flash = 6;
        }
        boss_puff(h, hp0, 56, 8, 12);                           /* LAB_85E4(56,8,12) */
        if (h->hp < 0x10) { draw_ptr(h, a2, a3); return; }
        a2 = 0x35FC0;
        if (h->flash) { h->flash--; if (h->flash & 1) a2 = 0x365D8; }
        draw_ptr(h, a2, a3); return;
    }
    /* ---- other stages: body ($7FE2) ---- */
    {
        uint32_t a2 = 0, a3 = 0x35CC0;
        h->frame_bytes = 0x300; h->frame_stride = 0xF00; h->modulo = 0x1E; h->h = 0x30; h->w_words = 9;
        h->box_dx = 2; h->box_w = 0x78; h->clip_w = 0x80;
        h->frame = 0;
        if (h->flags & HF_IMMUNE) {                             /* dead */
            g.boss_hold1570 = 0x64;
            a2 = 0x34DC0;
            add_yw(h, g.scrolled7222);
            if (YI(h) < 0x200) { draw_ptr(h, a2, a3); return; }
            g.boss_hold1570 = 0;
            g.hostiles[11].x = 0; g.hostiles[10].x = 0; g.hostiles[9].x = 0;
            hostile_free(h); return;
        }
        if (!(h->flags & HF_BIT2)) {                            /* LAB_804A: starts moving when the low byte == 0 */
            if (gframe() != 0) goto fire2;
            h->flags |= HF_BIT2;
        }
        if (g.dframe & 0x100) {                                 /* $8060: BTST #0,-28552 = bit 8 of the display counter */
            if (gframe() == 0) {                                /* pick +-0.5 px horizontal speed toward a living player */
                int16_t d3 = g.players[0].sx, d4 = g.players[1].sx; uint8_t d5 = g.players[0].state38, d6 = g.players[1].state38;
                int32_t d7 = 0;
                if (g.dframe & 0x200) { int16_t t = d3; d3 = d4; d4 = t; uint8_t u = d5; d5 = d6; d6 = u; }
                int ok = 1;
                if (d5 != 0) { int16_t t = d3; d3 = d4; d4 = t; uint8_t u = d5; d5 = d6; d6 = u; if (d5 != 0) ok = 0; }
                if (ok) { d7 = 0x8000; if ((int16_t)(d3 - 0x30) < XI(h)) d7 = -d7; }
                h->p8 = d7;
            }
            {   /* LAB_80AE: vertical bob */
                uint8_t i = (uint8_t)(gframe() >> 1); int32_t d1;
                if (i < 0x40) d1 = boss_bob_tab[i]; else d1 = -(int32_t)boss_bob_tab[(uint8_t)(0x7F - i)];
                h->x += h->p8;
                h->y += d1 << 13;
            }
        } else {                                                /* LAB_80EA: random wander */
            if (h->t27 == 0) {
                uint8_t d3 = (uint8_t)((rng() & 0x8F) | 3);
                if (XI(h) < 0x114) d3 &= 0x7F;
                if (XI(h) >= 0x1EC) d3 |= 0x80;
                if (d3 & 0x80) d3 |= 0x70;
                h->t27 = d3;
            }
            if ((int8_t)h->t27 < 0) { h->x -= 0xC000; h->t27++; } else { h->x += 0xC000; h->t27--; }
            if (h->t28 == 0) {
                uint8_t d3 = (uint8_t)((rng() & 0x8F) | 3);
                if (YI(h) < 0xF8) d3 &= 0x7F;
                if (YI(h) >= 0x128) d3 |= 0x80;
                if (d3 & 0x80) d3 |= 0x70;
                h->t28 = d3;
            }
            if ((int8_t)h->t28 < 0) { h->y -= 0xC000; h->t28++; } else { h->y += 0xC000; h->t28--; }
        }
    fire2:                                                      /* LAB_818E */
        if (h->hp < 0x10) goto damage2;                         /* dying: no fire, no pod gate */
        h->fire_x = XI(h); h->fire_y = YI(h);
        if (((uint8_t)(gframe() + 8) & 0x7F) == 0) {            /* gframe & $7F == $78: drop a MINE (fire_y - y == $26 -> LAB_4704 sound 56) */
            h->flags |= HF_FIRE_REQUEST; h->fire_y += 0x26; h->fire_x += 2;
            if (gframe() & 0x80) h->fire_x += 0x6C;
        }
        if (gframe() < 0x50) goto no_damage;                    /* LAB_81D0 */
        if (gframe() == 0x50) sfx(26);                          /* burst starts */
        if (gframe() >= 0xB0) goto no_damage;
        {   uint8_t d1 = gframe() & 0x0F;
            if (d1 == 0) { h->flags |= HF_FIRE_REQUEST; h->fire_x += 0x2C; h->fire_y += 0x20; }
            else if (d1 == 2) { h->flags |= HF_FIRE_REQUEST; h->fire_x += 0x48; h->fire_y += 0x20; }
            if (d1 == 0 || d1 == 2) {                           /* LAB_8224: random direction */
                int16_t d3 = (int8_t)rng(); g_14390 = d3;
                uint8_t r = rng(); if (r < 0x40) r += 0x40; g_14388 = r;
            } }
        if (pod->flags & HF_IMMUNE) goto damage2;               /* LAB_8240: damage only once the pod is dead */
    no_damage:                                                  /* LAB_8248 */
        h->damage = 0;
        goto look;
    damage2:                                                    /* LAB_824E */
        {
            int8_t hp0 = h->hp;
            if (h->damage) {
                uint8_t d = h->damage; h->damage = 0;
                boss_damage_halved(h, d);
                if (h->hp < 0) { h->flags |= 0x88; pod->flags |= 0x88; goto look; }
                h->flash = 6;
            }
            boss_puff(h, hp0, 20, 8, 10);                       /* LAB_85E4(20,8,10) */
            if (h->flash) { h->flash--; if (h->flash & 1) a2 = 0x33EC0; }
        }
    look:                                                       /* LAB_82A0 */
        if (h->hp < 0x10) { draw_ptr(h, 0x34DC0, a3); return; } /* wreck look */
        if (a2 == 0) {
            h->frame = boss_frame_tab[gframe() >> 3];
            a2 = 0x2E4C0 + (uint32_t)h->frame * 0xF00;
        }
        draw_ptr(h, a2, a3);
    }
}

/* ============================================================================================================ */
/* type $0A -- boss explosion puff (LAB_7F56 @ $7F56): slot 11 reads puff slot $790A, any other slot $7910        */
void h_type_0A(Hostile *h)
{
    BossPuff *p = (h->slot == 11) ? &PUFF[0] : &PUFF[1];
    h->flags |= 0x88;
    if (p->count == 0) { skip(h); return; }
    h->frame++;
    if (p->count == 8) h->frame = 0;
    p->count--;
    if (g.stage7228 == 3) {                                     /* stage 3: the slot's own position, scrolling */
        p->y += g.scrolled7222;
        set_xi(h, p->x); set_yi(h, p->y);
        draw_frame(h); return;
    }
    set_xi(h, (int16_t)(XI(&g.hostiles[8]) + p->x));            /* relative to the boss body (EXT_2DF00/2DF04) */
    set_yi(h, (int16_t)(YI(&g.hostiles[8]) + p->y));
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $0B -- flypast decoration (LAB_7E12 @ $7E12): 4 records in slots 8..11 (D2 = 3,2,1,0), fly right 4 px    */
static const uint8_t flypast_tab[32] = {                        /* $795E[(gframe >> 1) & $1F] */
    0,0,0,0,0,0,1,1,1,1,2,2,3,3,3,3,4,4,4,4,4,4,3,3,3,3,2,2,1,1,1,1 };
void h_type_0B(Hostile *h)
{
    int d2 = 11 - h->slot;                                      /* (A7) = remaining loop count: slot 8 -> 3 ... slot 11 -> 0 */
    if (h->t28 != 0xFF && (gframe() & 2)) h->t28++;
    if (h->t28 >= 0xB4 || g.cam7204 + 0x3C > XI(h)) {           /* LAB_7E46 */
        if (g.cam7204 + 0x120 < XI(h)) { hostile_free(h); return; }
        add_xw(h, 4);
    }
    if ((int16_t)g.demo_frames < 0x9C4) {                       /* first 2500 demo frames */
        h->frame = (uint8_t)((gframe() >> 2) & 0x0F); h->gfx = 0x1A500;      /* plane */
        if (d2 == 2) {                                          /* missile + smoke puff */
            h->frame = (uint8_t)(((gframe() >> 2) + 5) & 0x0F); h->gfx = 0x17500;
            h->gfxmask = 0x1A780 + (uint32_t)h->frame * 0x300; h->gfx = 0x577E;
            paint_smoke((int16_t)(XI(h) - 2), (int16_t)(YI(h) - 2), h->gfxmask);   /* LAB_5B3E */
            draw_ptr(h, h->gfx, h->gfxmask); return;            /* LAB_9814 */
        }
        if (d2 == 1) { h->frame = flypast_tab[(gframe() >> 1) & 0x1F]; h->gfx = 0x20500; }
        if (d2 == 0) { h->frame = (uint8_t)(((gframe() >> 2) + 0x0A) & 0x0F); h->gfx = 0x17500; }
        draw_frame(h); return;
    }
    if (d2 == 0) { h->frame_bytes = 0xB0; h->frame_stride = 0x420; h->gfx = 0x14450; h->h = 0x2C; }   /* bomber */
    if (d2 == 1) { h->frame = 0; if (!(gframe() & 2)) { h->frame++; h->gfx = 0x43700; } }
    if (d2 == 2) h->gfx = 0x36000;
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $0C -- tank (LAB_7CF0 @ $7CF0): 2500 pts, hp 35, 80x50                                                  */
void h_type_0C(Hostile *h)
{
    if (YI(h) >= 0x200) { hostile_free(h); return; }
    add_yw(h, g.scrolled7222);
    {   uint8_t d1 = (gframe() >> 3) & 3; if (d1 == 3) d1 = 1; h->frame = d1; }   /* 0,1,2,1 track animation */
dead:                                                           /* LAB_7D22 */
    if (h->t28) {                                               /* dead: 11 frames of frame 4, then wreck frame 5 */
        h->frame = 5;
        if (h->t28 >= 0x0C) { draw_frame(h); return; }
        h->t28++; h->frame = 4; draw_frame(h); return;
    }
    if (h->damage) {                                            /* LAB_7D46 */
        uint8_t d = h->damage; h->damage = 0;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) { h->flags |= 0x88; h->t28 = 1; goto dead; }
        add_yw(h, -1);
        if (h->flash == 0) h->flash = 8;
    }
    if (h->flash) { h->flash--; h->frame = 2; if (h->flash & 2) h->frame++; }
    if (!(gframe() & 2)) add_yw(h, 1);                          /* creeps down 1 px every other frame */
    {   /* LAB_7D9C: steer toward the player with the smaller y (p1 on tie) */
        int16_t tx = g.players[0].sx;
        if (g.players[0].sy >= g.players[1].sy) tx = g.players[1].sx;
        if ((int16_t)(tx - 0x10) < XI(h)) { if (h->p12 != (int32_t)0xFFFE8000) h->p12 -= 0x1000; }
        else                              { if (h->p12 != 0x18000) h->p12 += 0x1000; }
        h->x += h->p12; }
    h->t27--;
    if (h->t27 == 0) { h->t27 = 0x14; fire_at(h, 0x1D, 0x28); }  /* every 20 frames, aimed */
    draw_frame(h);
}

/* ============================================================================================================ */
/* type $0D -- pop-up drone (LAB_7A9A @ $7A9A): hp 1; t28 == 0 emerging script (LAB_7BF6) / launched              */
static void drone_fire(Hostile *h) { fire_at(h, 4, 8); }         /* LAB_7BDC */

void h_type_0D(Hostile *h)
{
    if (h->t28 == 0) {                                          /* ---- EMERGING (LAB_7BF6): same 4-byte script as type 3 ---- */
        h->flags |= 0x88;
        if (B(h->p8, 0) == 0) {
            h->p8 = be32(h->script); h->script += 4;
            h->p12 = (int32_t)(uint32_t)(h->script - bs_chip);
            if ((uint16_t)((uint32_t)h->p8 >> 16) == 0) { hostile_free(h); return; }
        }
        SETB(&h->p8, 2, (uint8_t)(B(h->p8, 2) + B(h->p8, 3)));  /* phase += phase_step */
        uint8_t phase = B(h->p8, 2); int16_t d2 = phase & 0x1F;
        int8_t dur = (int8_t)B(h->p8, 0);
        h->h = d2;
        uint32_t a2 = 0x37340, a3 = 0x373E0;
        if (dur < 0) { a2 += 0xC0; a3 += 0xC0; }
        if ((phase & 0x80) && dur >= 0) { uint16_t o = (uint16_t)((0x10 - d2) << 1); a2 += o; a3 += o; }   /* bottom rows only */
        {   int16_t step = (int8_t)B(h->p8, 1);
            if (dur < 0) add_xw(h, step); else add_yw(h, step); }
        add_yw(h, g.scrolled7222);
        SETB(&h->p8, 0, (uint8_t)(dur < 0 ? dur + 1 : dur - 1));
        if (YI(h) >= 0x200) { hostile_free(h); return; }
        if (h->h == 0) { skip(h); return; }
        if (dur >= 0) { draw_ptr(h, a2, a3); return; }
        if (phase != 0x10) {                                    /* horizontal reveal: 16 mask words ANDed with a (16-h)-bit shift ($76E8) */
            uint8_t d3 = (uint8_t)(0x10 - (phase & 0x1F));
            hg.reveal_mask16 = (phase & 0x80) ? (uint16_t)(0xFFFFu << (d3 & 15)) : (uint16_t)(0xFFFFu >> (d3 & 15));
            h->h = 0x10;
        }
        draw_ptr(h, a2, a3); return;
    }
    /* ---- LAUNCHED ---- */
    if (YI(h) <= 0xF0) { hostile_free(h); return; }
    add_yw(h, g.scrolled7222);
    if (h->t28 < 0x10) {                                        /* rising out of the launcher: bottom rows, +-0.25 px sideways */
        h->t28++; h->h = h->t28;
        uint16_t o = (uint16_t)(((0x10 - h->t28) << 1) + 0x180);
        h->x += 0x4000;
        if ((int8_t)h->t27 < 0) h->x -= 0x8000;
        draw_ptr(h, h->gfx + o, h->gfxmask + o); return;
    }
    h->h = 0x10;                                                /* LAB_7B00 */
    h->t28++;
    if (h->t28 < 0x19) h->p12 += 0x1000;                        /* sinks a little */
    else if (h->p12 > (int32_t)0xFFFC0000) h->p12 -= 0x2000;    /* then climbs away at up to 4 px/frame */
    if (h->frame >= 7) {                                        /* death frames 7..12 */
        if (!(gframe() & 2)) { draw_frame(h); return; }
        h->frame++;
        if (h->frame < 0x0D) { draw_frame(h); return; }
        hostile_free(h); return;
    }
    if (h->p12 == 0) drone_fire(h);                             /* LAB_7B52: twice per life */
    if (h->p12 == (int32_t)0xFFFC2000) drone_fire(h);
    {   uint8_t d1 = (gframe() >> 1) & 7; if (d1 >= 4) d1 = (uint8_t)(7 - d1); h->frame = (uint8_t)(d1 + 2); }   /* 2..5 */
    if (h->damage) {                                            /* $7B86 */
        uint8_t d = h->damage; h->damage = 0;
        h->hp = (int8_t)(h->hp - d);
        if (h->hp < 0) { h->frame = 7; draw_frame(h); return; }
        h->flash = 4;
    }
    if (h->flash) { h->flash--; if (h->flash & 1) h->frame = 6; }
    h->y += h->p12;                                             /* LAB_7BBE */
    h->x += 0x8000;
    if ((int8_t)h->t27 < 0) h->x -= 0x10000;                    /* SUBI.W #1,(A4): net -0.5 px */
    draw_frame(h);
}

/* ============================================================================================================ */
HostileHandler hostile_handlers[14] = {
    h_type_00, h_type_01, h_type_02, h_type_03, h_type_04, h_type_05, h_type_06,
    h_type_07, h_type_08, h_type_09, h_type_0A, h_type_0B, h_type_0C, h_type_0D };
