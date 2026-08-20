/* objects.c -- Battle Squadron OBJECT pool handlers (LAB_5F34 per-type branches), native C.
 *
 * Literal translations of ~/BattleSquadron-Amiga/asm/loader.asm $5F68..$6FEC against re/ENGINE_objects.md.
 * One function per type byte (+17), registered in object_handlers[].  The engine (object_update_all,
 * LAB_5F34/$5F44) does the per-slot prologue -- `flags31 &= ~0x20; if (x == 0) skip; flags31 ^= 0x08` --
 * then calls object_handlers[type](o).  EVERYTHING ELSE of the original branch is in here, including
 * `y += g.scrolled7222` and, for types >= $20, the shared damage/flash/death/pickup part LAB_686C (it is
 * part of the branch: the per-type code at LAB_6A40 only runs when that part falls through).
 *
 * Each handler ends the way the original did:
 *   tail  = LAB_6EE4  -> object_tail(o)  (engine: off-screen free test, hit box +48..+54, render record)
 *   skip  = LAB_6FEC  -> plain return    (no box, no render this frame)
 *   free  = LAB_6EF8  -> o->x = 0
 *
 * Field names are the original offsets (engine.h).  Engine verbs/globals that engine.h does not yet
 * declare are listed in the "engine verbs missing" block below and given WEAK fallbacks so this file
 * links with or without the real engine.
 */
#include <stdint.h>
#include <string.h>
#include "engine/engine.h"

/* ------------------------------------------------------------------------------------------------
 * engine verbs / globals MISSING from engine.h (weak fallbacks; the engine's definitions win)
 * ------------------------------------------------------------------------------------------------ */
#define BS_WEAK __attribute__((weak))

/* LAB_6EE4: free if y >= $200 or y + h < $100, write the hit box, emit the render record(s).  The
 * fallback does everything but the render list (the renderer owns LAB_6EFE..$6FE8). */
BS_WEAK void object_tail(Object *o)
{
    int16_t y = o->y;
    if (y >= 0x200 || (int16_t)(y + o->h6) < 0x100) { o->x = 0; return; }
    /* f48..f54 collision box: x+f20 .. x+f20+f22, y .. y+h  (engine.h has no f48..f55 fields yet) */
}
BS_WEAK uint8_t g_8414 = 10;            /* turret turn delay   (-8414: 10 at start, -2 per inner stage cleared) */
BS_WEAK uint8_t g_8413 = 50;            /* turret reload       (-8413: 50 at start, -10 per inner stage cleared) */
BS_WEAK uint8_t g_8397;                 /* gate "all players inside" consecutive-frame counter (-8397) */
BS_WEAK int16_t g_14390, g_14388;       /* forced bullet direction vx,vy for the next fire request (-14390/-14388) */
BS_WEAK uint8_t g_27618;                /* palette flash countdown (-27618, LAB_1420) */
BS_WEAK int16_t g_790A[2][3];           /* two explosion-puff slots {x, y, timer} at $790A/$7910 (consumed by hostile type $0A) */

/* --------------------------------------------------------------------------------------------- */
/* field access helpers: engine.h packs +36/+37 as one int16 and +38/+40 as bytes; keep the
 * original BYTE f36/f37 and WORD f38/f40 semantics here.  (Ask for `uint8_t f36, f37; int16_t
 * fire_x38, fire_y40;` in the frozen header -- then only these four lines change.) */
#define F36(o)  (((uint8_t *)&(o)->f36)[0])
#define F37(o)  (((uint8_t *)&(o)->f36)[1])
static inline void set_fire_pos(Object *o, int16_t px, int16_t py)
{ memcpy(&o->f38, &px, 2); memcpy(&o->f40, &py, 2); }
int16_t object_fire_x(const Object *o) { int16_t v; memcpy(&v, &o->f38, 2); return v; }
int16_t object_fire_y(const Object *o) { int16_t v; memcpy(&v, &o->f40, 2); return v; }

#define DLO()       ((uint8_t)g.dframe)         /* g-28551: low byte of the display-frame counter */
#define SCROLL()    (g.scrolled7222)            /* g7222 */
#define bs_sfx(id, x) (sfx(id))                 /* EXT_2470E, D0 = id  (x = emitter, unused by the original) */

/* f31 |= $20; f38/f40 = muzzle.  LAB_4704 turns it into an aimed bullet (or a mine for $25/$26). */
static inline void fire_request(Object *o, int16_t px, int16_t py)
{ o->flags31 |= 0x20; set_fire_pos(o, px, py); }

/* hull/core damage: SUB.B D1,hp ; BPL -> returns 1 when the result went negative (killed) */
static inline int apply_damage_s8(int8_t *hp, uint8_t d)
{ *hp = (int8_t)(*hp - d); return *hp < 0; }

/* ------------------------------------------------------------------------------------------------
 * LAB_6836 / LAB_6810 -- explosion puff request (type $05 only)
 * ------------------------------------------------------------------------------------------------ */
static void explosion_at(int16_t px, int16_t py)              /* LAB_6836 @ $6836 */
{
    int s = (g_790A[0][2] <= g_790A[1][2]) ? 0 : 1;        /* the slot with the smaller timer */
    g_790A[s][0] = px; g_790A[s][1] = py; g_790A[s][2] = 8;
    g_27618 = rng() & 0x0F;                                 /* palette flash */
    bs_sfx(31, px);
}
static void spark(Object *o)                                  /* LAB_6810 @ $6810 */
{
    int16_t px = o->x - 16, py = o->y - 16;
    px += rng() & 0x1F;
    py += rng() & 0x1F;
    explosion_at(px, py);
}

/* ------------------------------------------------------------------------------------------------
 * Type $00 -- stage-3 opening bunker (LAB_5F68 @ $5F68, template $2B68)
 * hull hp28 63 (47 1p), core f32 31, opens after rnd 1..128 frames, 3 aimed bullets, closes once.
 * f31 bit1 = open, bit4 = has opened / closing, bit3 = half-rate clock (toggled by the prologue)
 * ------------------------------------------------------------------------------------------------ */
void object_type_00(Object *o)
{
    uint8_t d;
    o->y += SCROLL();                                       /* $5F68 */
    if (!(o->flags31 & 0x02)) goto closed;                  /* $5F70 */
    if (o->flags31 & 0x10) {                                /* CLOSING $5F82 */
        o->dmg24 = 0;
        if (o->flags31 & 0x08) { object_tail(o); return; }
        if ((int8_t)--o->f25 > 3) { object_tail(o); return; }
        o->f25 = 0; o->flags31 &= ~0x02;
        goto closed;
    }
    if (o->f18 == 0) {                                      /* OPENING LAB_60AC: frames 4..7 half rate */
        o->dmg24 = 0;
        if (o->flags31 & 0x08) { object_tail(o); return; }
        if ((int8_t)++o->f25 < 7) { object_tail(o); return; }
        o->f18 = 100;                                       /* open for 100 frames */
        object_tail(o); return;
    }
    if ((int8_t)o->f25 >= 11) goto core_dying;              /* $5FB4 -> LAB_5FE4 */
    if (o->f30) goto core_flash;                            /* $5FBC -> LAB_6006 */
    d = o->dmg24;
    if (d) {                                                /* $5FC2: core takes the damage */
        o->dmg24 = 0;
        if (apply_damage_s8((int8_t *)&o->f32, d)) {        /* core destroyed */
            o->f25 = 10; o->flags31 |= 0x04; o->flags31 &= ~0x08;
core_dying:                                                 /* LAB_5FE4: frames 10..15 half rate, stays 15 */
            if (o->flags31 & 0x08) { object_tail(o); return; }
            if ((int8_t)o->f25 >= 15) { object_tail(o); return; }
            o->f25++;
            object_tail(o); return;
        }
        o->f30 = 4;                                         /* LAB_6000 */
core_flash:                                                 /* LAB_6006: frames 9/8 */
        o->f25 = 9;
        o->f30--;
        if (o->f30 & 0x02) { object_tail(o); return; }
        o->f25--;                                           /* = 8 */
        object_tail(o); return;
    }
    /* open & idle LAB_6022 */
    d = o->f18;
    if ((int8_t)d > 0x32) d -= 0x32;
    o->f25 = 7;
    if ((int8_t)d < 0x1E && (int8_t)d >= 0x14) {            /* gun out: frame 8 */
        o->f25++;
        if (o->f18 == 0x19) fire_request(o, o->x + 0x1A, o->y + 0x1E);   /* $604E */
        if (o->f18 == 0x49) fire_request(o, o->x + 0x1E, o->y + 0x10);   /* $6068 */
        if (o->f18 == 0x4D) fire_request(o, o->x + 0x0F, o->y + 0x11);   /* $6082 */
    }
    if (--o->f18 != 0) { object_tail(o); return; }          /* LAB_6094 */
    o->flags31 |= 0x10; o->flags31 &= ~0x08;                /* start closing; never opens again */
    object_tail(o); return;

closed:                                                     /* LAB_60D2 */
    if (o->f29) {                                           /* hull destroyed: 16 -> 1, frames 10/1 */
        if (o->f29 == 1) { object_tail(o); return; }
        o->f29--;
        o->f25 = (o->f25 == 10) ? 1 : 10;
        object_tail(o); return;
    }
    if (!(o->flags31 & 0x10) && o->y >= 0x100 && --o->rnd43 == 0) {   /* LAB_6102: OPEN */
        o->flags31 |= 0x02; o->flags31 &= ~0x08; o->f25 = 4;
        bs_sfx(25, o->x);
        object_tail(o); return;
    }
    if (o->f25 == 0) {                                      /* LAB_6136: hull damage only on frame 0 */
        d = o->dmg24;
        if (d == 0) { object_tail(o); return; }
        o->dmg24 = 0;
        if (apply_damage_s8(&o->hp28, d)) { o->f29 = 0x10; o->flags31 |= 0x04; }
    }
    if ((int8_t)++o->f25 >= 4) o->f25 = 0;                  /* LAB_615A: closed idle frames 0..3 */
    object_tail(o);
}

/* ------------------------------------------------------------------------------------------------
 * Type $01 -- launch pad (LAB_6170 @ $617A, template $2B98): spawns hostile type 07 at (x+14, y+31)
 * ------------------------------------------------------------------------------------------------ */
void object_type_01(Object *o)
{
    o->y += SCROLL();
    if (!(DLO() & 0x02)) { object_tail(o); return; }        /* every other game frame */
    if (o->f25 == 0) {                                      /* wait: on screen, then rnd 1..64 half-frames */
        if (o->y < 0x100) { object_tail(o); return; }
        if (--o->rnd43 != 0) { object_tail(o); return; }
        o->f25++;
        object_tail(o); return;
    }
    if ((int8_t)o->f25 < 9) { o->f25++; object_tail(o); return; }   /* opens: frames 1..9 */
    if (F36(o)) { object_tail(o); return; }                 /* LAB_61BC: once */
    F36(o) = 0xFF;
    if (g.demo) { object_tail(o); return; }                 /* -28516 attract mode: no launch */
    /* $61D2: LAB_75E4(D1 = x - g7204 + 14, D2 = y - $E1, D3 = 7, D4 = garbage) */
    hostile_alloc((int16_t)(o->x - g.cam7204 + 0x0E), (int16_t)(o->y - 0xE1), 7, 0);
    object_tail(o);
}

/* ------------------------------------------------------------------------------------------------
 * Type $02 -- stage-3 rising turret (LAB_61F0 @ $61FA, template $2BF8): hp 31/23, 1500 pts,
 * fire period g-2194 (= g.armour[6]), rnd 1..32, death frames 6..10
 * ------------------------------------------------------------------------------------------------ */
void object_type_02(Object *o)
{
    uint8_t d;
    o->flags31 |= 0x04;
    o->y += SCROLL();
    if (o->y >= 0x200) { o->x = 0; return; }                /* free */
    if (o->y < 0x100) return;                               /* skip: nothing while above the screen */
    if (o->rnd43) {                                         /* $621C: random delay, then emerge sound */
        if (--o->rnd43 != 0) return;
        bs_sfx(30, o->x);
        return;
    }
    if ((int8_t)F36(o) < 0x17) {                            /* LAB_6236: rises over 24 frames, frames 0..2 */
        F36(o)++;
        o->f25 = F36(o) >> 3;
        object_tail(o); return;
    }
    if ((int8_t)o->f25 >= 6) {                              /* LAB_6250: dying 6..10 every 4th frame */
        if ((int8_t)o->f25 < 11 && !(DLO() & 0x06)) o->f25++;
        object_tail(o); return;
    }
    o->flags31 &= ~0x04;                                    /* LAB_6276: shootable */
    if (!F37(o)) F37(o) = g.armour[6];                      /* -2194 */
    F37(o)--;
    o->f25 = 2;
    if ((int8_t)F37(o) < 0x18) {                            /* last 24 frames: frames 3,4,3 + 2 bullets */
        d = F37(o) >> 3;
        if (d == 2) d = 0;
        o->f25 = 3 + d;
        if (F37(o) == 0x09) fire_request(o, o->x + 0x1C, o->y + 4);   /* $62B6 */
        if (F37(o) == 0x0E) fire_request(o, o->x + 0x1C, o->y + 4);   /* $62D0 */
    }
    d = o->dmg24;                                           /* LAB_62E2 */
    if (d) {
        o->dmg24 = 0;
        if (apply_damage_s8(&o->hp28, d)) { o->flags31 |= 0x04; o->f25 = 6; object_tail(o); return; }
        o->f30 = 6;
    }
    if (o->f30) {                                           /* LAB_6308: hit flash 5/2 */
        o->f30--;
        o->f25 = 5;
        if (!(o->f30 & 1)) o->f25 = 2;
    }
    object_tail(o);
}

/* ------------------------------------------------------------------------------------------------
 * Type $03 -- stage-2 emerging turret (LAB_632E @ $6338, template $2BC8): hp 47/35, 2500 pts,
 * appears at y = $F0 + rnd(1..32), 1/32 per frame burst of 2 aimed bullets, recoil 8..12 on hit,
 * death 13..19
 * ------------------------------------------------------------------------------------------------ */
void object_type_03(Object *o)
{
    uint8_t d;
    int16_t appear;
    o->flags31 |= 0x04;
    o->y += SCROLL();
    appear = (int16_t)(o->rnd43 + 0xF0);
    if (appear > o->y) return;                              /* skip: hidden */
    if (appear == o->y) bs_sfx(30, o->x);
    if ((int8_t)o->f25 < 5) {                               /* emerges: frames 0..4 every 8th frame */
        F36(o) = 0x10;
        if (DLO() & 0x0E) { object_tail(o); return; }
        o->f25++;
        object_tail(o); return;
    }
    o->flags31 &= ~0x04;                                    /* LAB_6384 */
    if ((int8_t)o->f25 >= 13) {                             /* LAB_646E: death 13..19 every 4th frame */
        o->flags31 |= 0x04;
        if ((int8_t)o->f25 >= 0x14) { object_tail(o); return; }
        if (DLO() & 0x06) { object_tail(o); return; }
        o->f25++;
        object_tail(o); return;
    }
    d = o->dmg24;
    if (d) {                                                /* $6394 */
        o->dmg24 = 0;
        if (apply_damage_s8(&o->hp28, d)) { o->f25 = 13; object_tail(o); return; }
        if ((int8_t)o->f25 >= 8) goto recoil;              /* LAB_63AE */
        o->f25 = 8;
        object_tail(o); return;
    }
    if ((int8_t)o->f25 >= 8) goto recoil;                   /* LAB_63C0 */
    if (!F36(o)) {                                          /* LAB_63EE: 1/32 per frame start a burst */
        o->f25 = 7;
        if (rng() & 0x1F) { object_tail(o); return; }
        F36(o) = 0x18;
    }
    F36(o)--;                                               /* LAB_6410 */
    d = F36(o);
    if ((int8_t)d < 8 || (int8_t)d >= 16) { o->f25 = 5; object_tail(o); return; }   /* LAB_6424 */
    o->f25 = 6;                                             /* LAB_642E */
    if (d == 13) { fire_request(o, o->x + 4, o->y + 8); object_tail(o); return; }
    if (d == 12) { fire_request(o, o->x + 0x23, o->y + 0x10); object_tail(o); return; }
    object_tail(o); return;

recoil:                                                     /* LAB_63C8 */
    if (!(DLO() & 0x02)) { object_tail(o); return; }
    if ((int8_t)++o->f25 < 13) { object_tail(o); return; }
    o->f25 = 7; F36(o) = 0;
    object_tail(o);
}

/* ------------------------------------------------------------------------------------------------
 * Type $04 -- stage-1 two-phase cannon (LAB_6492 @ $649C, template $2C28) (L: never captured)
 * phase 1: 4-bullet fan per g-2195 (= g.armour[5]) period, hull hp28; hull death frames 3..11 ->
 * phase 2: core f32 = 15, one aimed bullet per period, frames 12/13 pulsing, death 15..17
 * ------------------------------------------------------------------------------------------------ */
void object_type_04(Object *o)
{
    uint8_t d;
    o->y += SCROLL();
    if ((int8_t)o->f25 >= 12) {                             /* PHASE 2  LAB_6598 */
        if ((int8_t)o->f25 >= 15) {                         /* death 15..17 every 8th frame */
            if (DLO() & 0x0E) { object_tail(o); return; }
            if ((int8_t)o->f25 >= 0x11) { object_tail(o); return; }
            o->f25++;
            object_tail(o); return;
        }
        o->flags31 &= ~0x04;                                /* LAB_65BE */
        if (--F36(o) == 0) {
            F36(o) = g.armour[5];                           /* -2195 */
            fire_request(o, o->x + 0x1C, o->y + 0x0E);      /* $65D0 */
        }
        d = o->dmg24;                                       /* LAB_65E2 */
        if (d) {
            o->dmg24 = 0;
            if (apply_damage_s8((int8_t *)&o->f32, d)) { o->f25 = 15; o->flags31 |= 0x04; object_tail(o); return; }
            o->f30 = 6;
        }
        o->f25 = 12;                                        /* LAB_6608: pulsing core 12/13 */
        if ((DLO() & 0x1F) < 0x18) o->f25++;
        if (o->f30) {                                       /* LAB_6620 */
            o->f30--;
            if (o->f30 & 1) o->f25 = 14;
        }
        object_tail(o); return;
    }
    if ((int8_t)o->f25 >= 3) {                              /* hull destroyed: 3..11 every 4th frame */
        if (DLO() & 0x06) { object_tail(o); return; }
        o->f25++;
        object_tail(o); return;
    }
    if (!F36(o)) F36(o) = g.armour[5];                      /* LAB_64CA: -2195 */
    o->f25 = 0;
    F36(o)--;
    if ((int8_t)F36(o) < 0x19) {                            /* last 25 frames: frame 1, 4-shot fan */
        o->f25++;
        switch (F36(o)) {
        case 0x14: g_14390 = -50; g_14388 = 50; fire_request(o, o->x + 0x1C, o->y + 0x0B); break;   /* $64F2 */
        case 0x0F: g_14390 = -25; g_14388 = 50; fire_request(o, o->x + 0x1C, o->y + 0x0B); break;   /* $6508 */
        case 0x0A: g_14390 =  15; g_14388 = 50; fire_request(o, o->x + 0x1C, o->y + 0x0B); break;   /* $651E */
        case 0x05: g_14390 =  50; g_14388 = 50; fire_request(o, o->x + 0x1C, o->y + 0x0B); break;   /* $6534 */
        default: break;
        }
    }
    d = o->dmg24;                                           /* LAB_6552 */
    if (d) {
        o->dmg24 = 0;
        if (apply_damage_s8(&o->hp28, d)) { o->f25 = 3; o->flags31 |= 0x04; object_tail(o); return; }
        o->f30 = 6;
    }
    if (o->f30) {                                           /* LAB_6578 */
        o->f30--;
        if (o->f30 & 1) o->f25 = 2;
    }
    object_tail(o);
}

/* ------------------------------------------------------------------------------------------------
 * Type $05 -- stage-3 crates / hidden targets (LAB_6640 @ $664A; $2C58 visible hp 23, $2C88..$2D78
 * hidden f25 = 6..11 hp 7) (L: never captured).  Visible: opens/closes on a 128-frame triangle, sprays
 * random-velocity bullets, only shootable when it is the only live type-5 within +-64 rows; burning
 * (f25 = 5) throws explosion puffs for 75 frames.  Hidden: drawn only on the frame it is hit / once dead.
 * ------------------------------------------------------------------------------------------------ */
void object_type_05(Object *o)
{
    uint8_t d;
    o->y += SCROLL();
    if (o->y >= 0x200) { o->x = 0; return; }                /* free */
    /* $665C..$667A: hit box written HERE (before the early exits) -- engine: f48..f54 = x+f20, +f22, y, y+h;
     * object_tail() rewrites the same values, the skip paths keep these. */
    if ((int8_t)o->f25 > 5) {                               /* HIDDEN variant  LAB_67E0 */
        if (o->hp28 < 0) { object_tail(o); return; }        /* destroyed: stays visible */
        d = o->dmg24;
        if (!d) return;                                     /* skip: invisible */
        o->dmg24 = 0;
        if (apply_damage_s8(&o->hp28, d)) { o->flags31 |= 0x04; spark(o); object_tail(o); return; }
        spark(o);
        return;                                             /* skip */
    }
    if (o->f25 == 5) {                                      /* BURNING  LAB_6688 */
        if (!F36(o)) { object_tail(o); return; }
        F36(o)--;
        if (rng() & 0x06) { object_tail(o); return; }       /* 1 in 4 */
        {
            int16_t px = o->x - 0x40, py = o->y - 0x20;
            px += rng() & 0x7F;
            py += rng() & 0x3F;
            explosion_at(px, py);
        }
        object_tail(o); return;
    }
    /* LAB_66D4: 0..128 triangle of the frame counter */
    d = (uint8_t)(DLO() << 1);
    if (d & 0x80) d = (uint8_t)-d;
    o->f25 = 3;
    o->flags31 &= ~0x04;
    if (d < 0x18) {                                         /* closed: frames 0..2, not shootable */
        o->f25 = d >> 3;
        o->flags31 |= 0x04;
    } else if (o->y >= 0x108 && o->y < 0x1D8 && ((uint8_t)(d + 4) & 0x07) == 0) {   /* LAB_66FE */
        fire_request(o, o->x + 0x0C, o->y + 0x14);          /* bullet with RANDOM velocity */
        g_14390 = (int8_t)rng();
        g_14388 = (int8_t)rng();
    }
    /* LAB_6740: only an isolated crate is shootable: count live type-5 objects (hp >= 0, hidden ones
     * included, itself included) with y-64 < y' <= y+64 */
    {
        int16_t lo = o->y - 0x40, hi = o->y + 0x40;
        uint8_t n = 0;
        for (int i = 0; i < 18; i++) {
            const Object *p = &g.objects[i];
            if (p->x == 0 || p->type != 0x05 || p->hp28 < 0) continue;
            if (lo >= p->y) continue;
            if (hi < p->y) continue;
            n++;
        }
        if (n != 1) { o->flags31 |= 0x04; object_tail(o); return; }
    }
    d = o->dmg24;                                           /* LAB_6790 */
    if (d) {
        o->dmg24 = 0;
        if (apply_damage_s8(&o->hp28, d)) {                 /* destroyed -> burning 75 frames */
            o->flags31 |= 0x04; o->f25 = 5; F36(o) = 0x4B;
            object_tail(o); return;
        }
        o->f30 = 4;                                         /* LAB_67B6 */
        spark(o);
    }
    if (o->f30) {                                           /* LAB_67C0 */
        o->f30--;
        if (o->f30 & 1) o->f25 = 4;
    }
    object_tail(o);
}

/* ------------------------------------------------------------------------------------------------
 * Types >= $20 -- shared part LAB_686C @ $686C: ground move, damage mailbox, hit flash, death anim,
 * turret-wreck bonus pickup.  Returns 1 when the per-type behaviour (LAB_6A40) must run.
 * ------------------------------------------------------------------------------------------------ */
static const uint8_t tbl_5F24[16] = { 0x00,0x02,0x03,0x04,0x05,0x05,0x05,0x05,0x04,0x03,0x02,0x00,0x01,0x00,0x01,0x00 };

static void wreck_bonus_check(Player *p, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t *got)
{                                                           /* LAB_6A06 */
    if (p->state38 >= 0x4B) return;                         /* not flying */
    if (x1 > p->sx) return;
    if (y1 > p->sy) return;
    if (x2 < p->sx) return;
    if (y2 < p->sy) return;
    if (p->bonus97 == 0x99) return;
    {                                                       /* ABCD #1 */
        uint8_t v = p->bonus97, lo = (v & 0x0F) + 1, hi = v >> 4;
        if (lo > 9) { lo -= 10; hi++; }
        p->bonus97 = (uint8_t)((hi << 4) | lo);
    }
    (*got)++;
}

int object_shared_2x(Object *o)                             /* LAB_686C (exported for the engine) */
{
    uint8_t d;
    o->y += SCROLL();                                       /* $6870 */
    if ((int8_t)o->f25 < (int8_t)o->f19) {                  /* ALIVE */
        d = o->dmg24;
        if (d == 0) {
            if (o->f30 == 0) return 1;                      /* -> LAB_6A40 behaviour */
            o->f30--;
            goto flash;
        }
        o->dmg24 = 0;                                       /* LAB_6932 */
        if (o->type == 0x22) o->f30++;                      /* beacon: one flash step per hit */
        else if (o->type == 0x20 && g.stage7228 == 2) { if (!o->f30) o->f30 = 0x0F; }   /* silo: 15-frame hit sequence */
        else o->f30 = 3;
        if (apply_damage_s8(&o->hp28, d)) {                 /* LAB_6968: KILLED */
            o->flags31 |= 0x04; o->f25 = o->f19;
            object_tail(o); return 0;
        }
flash:                                                      /* LAB_6894 */
        if (o->type == 0x22 && g.stage7228 == 0) {
            o->f25 = o->f30 ? (uint8_t)(6 - o->f30) : 0;
            object_tail(o); return 0;
        }
        if (o->type == 0x20 && g.stage7228 == 2) {          /* LAB_68C0: silo hit sequence + 2 bullets */
            o->f25 = tbl_5F24[o->f30 & 0x0F];
            if (o->f30 == 6) fire_request(o, o->x + 0x21, o->y + 0x0E);
            if (o->f30 == 5) fire_request(o, o->x + 0x06, o->y + 0x0E);
            object_tail(o); return 0;
        }
        o->f25 = (o->f30 & 1) ? o->f34 : o->f35;            /* LAB_6918 generic flash */
        object_tail(o); return 0;
    }
    /* DYING / DEAD  LAB_6980 */
    if ((int8_t)o->f25 < (int8_t)o->f33) {
        if (!(DLO() & 0x02)) o->f25++;                      /* every other game frame */
        object_tail(o); return 0;
    }
    if (o->type == 0x20 && o->f25 == o->f33) {              /* LAB_699C: finished wreck = bonus pickup */
        int16_t left = o->x, top = o->y;
        int16_t x1, y1, x2, y2; uint8_t got = 0;
        if (g.stage7228 == 3) left -= 8;
        x1 = left - 8; y1 = top - 0x0C; x2 = left + 0x14; y2 = top + 0x10;
        wreck_bonus_check(&g.players[0], x1, y1, x2, y2, &got);
        wreck_bonus_check(&g.players[1], x1, y1, x2, y2, &got);
        if (got) { o->f25 = o->f33 + 1; bs_sfx(53, o->x); }
    }
    object_tail(o); return 0;
}

/* ------------------------------------------------------------------------------------------------
 * Type $20 -- gun emplacement family (LAB_6A40 @ $6A40), behaviour by stage g7228
 * ------------------------------------------------------------------------------------------------ */
static const int8_t  tbl_5F02[16] = { 0,1,1,1,1,-1,-1,-1, 0,1,1,1,1,-1,-1,-1 };   /* $5EFA..$5F09, index -8..7 */
static const int16_t muzzle_dx[8] = { 0x13,0x1A,0x1E,0x1B,0x14,0x0D,0x09,0x0E };  /* $5EDA */
static const int16_t muzzle_dy[8] = { 0x06,0x0A,0x0F,0x17,0x1C,0x17,0x0F,0x0A };  /* $5EEA */

static void type20_mode0_turret(Object *o)                  /* LAB_6B68: rotating turret ($2DA8) */
{
    int16_t cx, cy, d1, d2, tx, ty, dx, dy; int32_t slope; int quadrant, dir, idx; int8_t delta;
    if (F37(o) >= 2) F37(o)--;                              /* reload countdown, stops at 1 */
    if (F36(o)) { F36(o)--; object_tail(o); return; }       /* turn delay */
    cx = o->x + 0x13; cy = o->y + 0x10;                     /* g-14398/-14396 */
    /* LAB_491C: Manhattan distances to the player centres (sprite-space mirrors +4/+6) */
    d1 = (int16_t)(g.players[0].sx + 12 - cx); if (d1 < 0) d1 = -d1;
    d2 = (int16_t)(g.players[0].sy + 16 - cy); if (d2 < 0) d2 = -d2;
    d1 = (int16_t)(d1 + d2);
    { int16_t a = (int16_t)(g.players[1].sx + 12 - cx), b = (int16_t)(g.players[1].sy + 16 - cy);
      if (a < 0) a = -a;
      if (b < 0) b = -b;
      d2 = (int16_t)(a + b); }
    if ((uint16_t)d1 < (uint16_t)d2) { tx = g.players[0].sx; ty = g.players[0].sy; o->f42 = 1; }   /* BCS */
    else                             { tx = g.players[1].sx; ty = g.players[1].sy; o->f42 = 2; }
    dx = (int16_t)(tx - 6 - o->x); if (dx == 0) dx = 1;     /* LAB_6BC6 */
    quadrant = (dx < 0) ? 4 : 0;
    dy = (int16_t)(-ty + o->y + 6);
    dx |= 1;
    slope = (int32_t)dy * 64;                               /* MULS #$40 */
    {   int32_t q = slope / dx;                             /* DIVS: on overflow the register is unchanged */
        if (q >= -32768 && q <= 32767) slope = (int16_t)q; else slope = (int16_t)slope; }
    dir = slope >= 0x9A ? 0 : slope >= 0x1A ? 1 : slope >= -0x1A ? 2 : slope >= -0x9A ? 3 : 4;
    dir = (dir + quadrant) & 7;
    idx = dir - (int)o->f25;                                /* -7..7 */
    delta = tbl_5F02[idx + 8];
    if (delta) {                                            /* $6C24: rotate one step */
        o->f25 = (uint8_t)((o->f25 + delta) & 7);
        o->f35 = o->f25;
        F36(o) = g_8414;                                    /* turn delay */
        if (F37(o) >= 2) { object_tail(o); return; }
        F37(o) = 1;
        object_tail(o); return;
    }
    if (F37(o) >= 2) { object_tail(o); return; }            /* LAB_6C4E: reloading */
    if (F37(o)) { F37(o)--; object_tail(o); return; }
    F37(o) = g_8413;                                        /* LAB_6C66: FIRE, reload */
    fire_request(o, o->x + muzzle_dx[o->f25 & 7], o->y + muzzle_dy[o->f25 & 7]);
    object_tail(o);
}

static void type20_mode1_hatch(Object *o)                   /* LAB_6B02: hatch gun ($2DD8), 70-frame cycle */
{
    uint8_t d;
    if (++F36(o) >= 0x46) F36(o) = 0;
    d = F36(o);
    if ((int8_t)d >= 0x23) d = (uint8_t)(0x46 - d);         /* triangle 0..35 */
    o->f25 = 0;
    if ((int8_t)d >= 0x14) { o->f25++; if ((int8_t)d >= 0x19) o->f25++; }   /* ($6B3A CMPI #$1E; NOP = dead) */
    if (d != 0x23) { object_tail(o); return; }
    fire_request(o, o->x + 0x14, o->y + 0x12);              /* one aimed bullet at the peak (f42 = 0: nearer player) */
    object_tail(o);
}

static void type20_mode2_silo(Object *o)                    /* LAB_6A68: silo ($2E08) launches hostile $0D */
{
    Hostile *h;
    if (!F36(o)) F36(o) = (rng() & 0x3F) | 0x17;            /* 23..63 */
    if (--F36(o) == 0) {                                    /* LAB_6AC8: launch B */
        F36(o) = 0;
        h = hostile_alloc((int16_t)(o->x - g.cam7204 + 0x10), (int16_t)(o->y - 0xA3), 13,
                          0x10000u);                                  /* D4 = $00010000 -> h->p12 */
        if (h) { h->t28 = 1; h->t27 = (uint8_t)~h->t27; }
        object_tail(o); return;
    }
    if (F36(o) != 0x20) { object_tail(o); return; }
    F36(o) = 0;                                             /* $6A92: launch A, re-arm */
    h = hostile_alloc((int16_t)(o->x - g.cam7204 + 0x10), (int16_t)(o->y - 0xA3), 13, 0x10000u);
    if (h) h->t28 = 1;
    object_tail(o);
}

void object_type_20(Object *o)
{
    if (!object_shared_2x(o)) return;
    switch (g.stage7228) {                                  /* LAB_6A40 */
    case 0:  type20_mode0_turret(o); return;
    case 1:  type20_mode1_hatch(o);  return;
    case 2:  type20_mode2_silo(o);   return;
    default: object_tail(o);         return;                /* mode 3 static block: no behaviour */
    }
}

/* Type $21 -- static scenery (LAB_6C9A): shared part only */
void object_type_21(Object *o)
{
    if (!object_shared_2x(o)) return;
    object_tail(o);
}

/* Type $22 -- blinking beacon (LAB_6CA6 @ $6CAE): frame 0/1 toggles every 8 frames (rnd43 starts 1..8) */
void object_type_22(Object *o)
{
    if (!object_shared_2x(o)) return;
    if (--o->rnd43 == 0) {
        o->rnd43 = 8;
        o->f25 = o->f25 ? 0 : 1;
    }
    object_tail(o);
}

/* Types $25/$26 -- mines (LAB_6CE0 @ $6CE0): 50 % never; else after 64..127 frames ONE stationary
 * hazard (LAB_4704 makes the effect and plays sound 56) at (x+2,y+7) / (x+$14,y+9) */
static void type_mine(Object *o)
{
    uint8_t r;
    if (!object_shared_2x(o)) return;
    if ((int8_t)F36(o) < 0) { object_tail(o); return; }     /* done */
    if (F36(o) == 0) {
        r = rng();
        if (r & 0x40) { F36(o) = 0xFF; object_tail(o); return; }   /* $6CF8: never */
        F36(o) = (uint8_t)((r & 0x3F) + 0x40);
    }
    if (--F36(o) != 0) { object_tail(o); return; }
    F36(o) = 0xFF;                                          /* NOT.B */
    if (o->type == 0x25) fire_request(o, o->x + 0x02, o->y + 0x07);   /* LAB_6D38 */
    else                 fire_request(o, o->x + 0x14, o->y + 0x09);   /* $6D2C */
    object_tail(o);
}
void object_type_25(Object *o) { type_mine(o); }
void object_type_26(Object *o) { type_mine(o); }

/* Type $27 -- STAGE GATE (LAB_6D44 @ $6D4E): unshootable, blinks, every live player inside
 * [x-16,x+16] x [y-16,y+16) for 10 consecutive frames toggles g-4100 (LAB_7002 stage clear) */
static void gate_player_check(const Player *p, int16_t x1, int16_t y1, int16_t x2, int16_t y2, uint8_t *d4)
{                                                           /* LAB_6DCE */
    if (p->state38 >= 0xAF) return;                         /* not joined */
    if (p->state38 == 0x64) return;                         /* exploding */
    (*d4)++;                                                /* a live player */
    if (x1 > p->sx) { *d4 |= 0x80; return; }
    if (x2 < p->sx) { *d4 |= 0x80; return; }
    if (y1 > p->sy) { *d4 |= 0x80; return; }
    if (y2 > p->sy) return;                                 /* inside */
    *d4 |= 0x80;                                            /* outside */
}
void object_type_27(Object *o)
{
    uint8_t d4 = 0;
    if (!object_shared_2x(o)) return;
    if (o->y >= 0x1FC) g.pending7230 = 0;                   /* leaving: allow the next gate */
    o->flags31 |= 0x04;
    o->f25 = ((DLO() & 0x1F) < 8) ? 0 : 1;                  /* blink */
    gate_player_check(&g.players[0], o->x - 0x10, o->y - 0x10, o->x + 0x10, o->y + 0x10, &d4);
    gate_player_check(&g.players[1], o->x - 0x10, o->y - 0x10, o->x + 0x10, o->y + 0x10, &d4);
    if (!(d4 & 0x80) && d4 != 0) {                          /* $6DA6: some player, none outside */
        if ((int8_t)++g_8397 >= 10) g.gate4100 = (uint8_t)~g.gate4100;   /* -> LAB_7002 */
        object_tail(o); return;
    }
    g_8397 = 0;                                             /* LAB_6DC6 */
    object_tail(o);
}

/* Type $28 -- stage-1 hatch spinner (LAB_6DFE @ $6E06) (L: never captured): spins frames 0..7,
 * hatch table for the last 24 frames of the g-2196 (= g.armour[4]) period, one aimed bullet at f18 == 12 */
static const uint8_t tbl_5F0A[24] = { 8,8,8,8, 9,9,9,9, 10,10,10,10, 10,10,10,10, 9,9,9,9, 8,8,8,8 };
void object_type_28(Object *o)
{
    if (!object_shared_2x(o)) return;
    if (o->f18 == 0x0C) fire_request(o, o->x + 4, o->y + 4);          /* $6E0E */
    if (!o->f18) o->f18 = g.armour[4];                      /* -2196 */
    o->f18--;
    if ((int8_t)o->f18 < 0x18) o->f25 = tbl_5F0A[o->f18];
    else {
        if (!(DLO() & 0x02)) o->f25++;                      /* spin every other frame */
        o->f25 &= 7;
    }
    object_tail(o);
}

/* Type $29 -- invisible spawner (LAB_6E68 @ $6E72, $3048): after rnd 64..127 frames (50 %: never)
 * spawns hostile type 08 at (x+6, y-6), warning sound 24 at 20 frames before; freed at y >= $1CE;
 * never rendered, no box (skip) */
void object_type_29(Object *o)
{
    uint8_t r;
    if (!object_shared_2x(o)) return;
    if (o->y >= 0x1CE) { o->x = 0; return; }                /* free */
    o->flags31 |= 0x04;
    if ((int8_t)F36(o) < 0) return;                         /* skip: done / never */
    if (F36(o) == 0) {
        r = rng();
        F36(o) = (r & 0x80) ? r : (uint8_t)((r & 0x3F) + 0x40);   /* negative = never */
    }
    if (--F36(o) == 0) {
        F36(o) = 0xFF;
        /* $6EB2: LAB_75E4(D1 = x - g7204 + 6, D2 = y - $106, D3 = 8, D4 = garbage) */
        hostile_alloc((int16_t)(o->x - g.cam7204 + 6), (int16_t)(o->y - 0x106), 8, 0);
        return;                                             /* skip */
    }
    if (F36(o) == 0x14) bs_sfx(24, o->x);
    /* skip */
}

/* Types $23/$24 (and any other >= $20 not listed): shared part, then LAB_6FEC -- no box, not drawn */
void object_type_2x_other(Object *o)
{
    if (!object_shared_2x(o)) return;
    /* skip */
}

/* ------------------------------------------------------------------------------------------------ */
ObjectHandler object_handlers[0x2a] = {
    [0x00] = object_type_00, [0x01] = object_type_01, [0x02] = object_type_02,
    [0x03] = object_type_03, [0x04] = object_type_04, [0x05] = object_type_05,
    /* $06..$1F: LAB_5F34 falls to LAB_6FEC -- no behaviour, not even y += scroll: NULL = skip */
    [0x20] = object_type_20, [0x21] = object_type_21, [0x22] = object_type_22,
    [0x23] = object_type_2x_other, [0x24] = object_type_2x_other,
    [0x25] = object_type_25, [0x26] = object_type_26, [0x27] = object_type_27,
    [0x28] = object_type_28, [0x29] = object_type_29,
};
