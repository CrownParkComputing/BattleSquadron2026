/* engine.h -- native Battle Squadron engine API (FROZEN, session 2; see re/ENGINE.md).
 *
 * Battle Squadron is a fixed-pool, type-switch state machine run at 25 Hz (one
 * game frame = two PAL display frames).  The native engine keeps the ORIGINAL
 * record layouts as C structs with the original field offsets in their names
 * where no role is proven, so the per-frame objlog diffs 1:1 against the
 * oracle's `--objlog` (same G/P/S/H/O/E lines).  Every per-type branch of the
 * two update routines (LAB_79E2 hostiles, LAB_5F34 objects) becomes one C
 * handler; re/PORTING_GUIDE.md says how.
 *
 * Coordinates: one "game space": X = 256 + map column (0..384 playfield; screen
 * x = X - g.cam7204, cam 256..352), Y = 256 + screen row (visible $100..$1FF).
 * Hostiles/effects: 16.16 (`x` long, integer part = the original word at +0,
 * i.e. x_word = x >> 16).  Objects/shots/players: integer.
 * g.dframe = DISPLAY frame counter (-28552, +2 per game frame); "alternate
 * frame" = g.dframe & 2 (toggles per game frame), & 1 per display frame.
 *
 * DATA: the engine reads every original table (descriptors, templates, wave
 * lists, scripts, weapon tables, RNG table) from the 512 KiB chip image built
 * by src/bsdata.c; `bs_chip` + cb/cw/cl below give handlers the same view.
 * Pointers the original stored in records (script ptr +12, gfx ptrs +32/+36,
 * weapon table +24) are kept as NUMERIC chip addresses so the objlog matches.
 */
#ifndef BS_ENGINE_H
#define BS_ENGINE_H
#include <stdint.h>

/* ---------- chip image (bsdata.c owns it; engine + handlers read tables) ---------- */
extern uint8_t *bs_chip;                              /* 0x80000 bytes */
static inline uint8_t  cb(uint32_t a) { return bs_chip[a]; }
static inline uint16_t cw(uint32_t a) { return (uint16_t)((bs_chip[a] << 8) | bs_chip[a + 1]); }
static inline uint32_t cl(uint32_t a) { return ((uint32_t)cw(a) << 16) | cw(a + 2); }
static inline void cwrw(uint32_t a, uint16_t v) { bs_chip[a] = (uint8_t)(v >> 8); bs_chip[a + 1] = (uint8_t)v; }

/* ---------- hostile pool: $2DC80, 12 x 80 bytes (ENGINE_hostiles.md §0) ---------- */
enum { HF_FIRED_A = 1, HF_FIRED_B = 2, HF_BIT2 = 4, HF_HARMLESS = 8, HF_OFFSCREEN = 16,
       HF_FIRE_REQUEST = 32, HF_DONE = 64, HF_IMMUNE = 128 };            /* +30 */
typedef struct Hostile {
    int32_t x, y;                 /* +0/+4 16.16: word part = x >> 16 (live iff != 0) */
    int32_t p8;                   /* +8  per type (type 0: {dur,dir,turn,ttimer} bytes; 4/8: vx; 5: drift accel; 6: vx) */
    int32_t p12;                  /* +12 per type: script CHIP ADDRESS (types 0/3/D), vx/vy 16.16 (1/4/8/C/D), rel-x (5) */
    const uint8_t *script;        /* native view of p12 when it is a script pointer (bs_chip + p12; keep p12 in sync) */
    int16_t box[4];               /* +16 x0,x1,y0,y1 (refreshed by hostile_draw / LAB_981C) */
    int8_t  hp;                   /* +24 (signed; < 0 after the killing hit) */
    uint8_t hitsnd;               /* +25 */
    uint8_t t26, t27, t28;        /* +26..+28 per-type timers/phases (27/28 preloaded from descriptor +26/+27) */
    uint8_t explode;              /* +29 explosion countdown 8..0 on alternate frames */
    uint8_t flags;                /* +30 HF_* */
    uint8_t type;                 /* +31 $00..$0D */
    uint32_t gfxmask, gfx;        /* +32/+36 chip addresses (frame 0 mask / planes) */
    int16_t g40, g42, frame_bytes, frame_stride, modulo, h, w_words, score_bcd;  /* +40..+54 */
    uint8_t flash;                /* +57 */
    int16_t fire_x, fire_y;       /* +58/+60 */
    uint8_t damage;               /* +62 accumulated by player shots (consumed by the handler) */
    uint8_t frame;                /* +63 */
    int16_t box_dx, box_w, clip_w;/* +64/+66/+68 */
    int slot;                     /* native */
} Hostile;
static inline int16_t hxw(const Hostile *h) { return (int16_t)(h->x >> 16); }
static inline int16_t hyw(const Hostile *h) { return (int16_t)(h->y >> 16); }
static inline void set_hxw(Hostile *h, int16_t v) { h->x = (h->x & 0xFFFF) | ((int32_t)v << 16); }
static inline void set_hyw(Hostile *h, int16_t v) { h->y = (h->y & 0xFFFF) | ((int32_t)v << 16); }

#define BS_DESC_BASE 0xCD7A       /* descriptor table $CD7A + type*32: read via cw/cb/cl */

/* ---------- object pool: $2E040, 18 x 64 bytes (ENGINE_objects.md §1.1/§2) ---------- */
typedef struct Object {
    int16_t x, y;                 /* +0/+2 game space (live iff x != 0; freed at y >= $200) */
    int16_t f4; int16_t h6, w8, f10; uint32_t gfx12;   /* +4 modulo, +6 height, +8 width words, +10 = 2wh, +12 gfx chip addr */
    uint8_t f16, type, f18, f19;  /* +16, +17 TYPE, +18, +19 first death frame */
    int16_t f20, f22;             /* +20/+22 hit box dx / width */
    uint8_t dmg24, f25, f26h, f27;/* +24 damage mailbox, +25 STATE/frame, (+26.w frame stride: use f26) */
    int16_t f26;                  /* +26 frame stride = 5*f10 (native: separate field; +27 unused) */
    int8_t hp28; uint8_t f29, f30, flags31; /* +28 health, +29, +30 flash, +31 flags (bit2 unshootable, bit5 fire request) */
    int8_t f32; uint8_t f33, f34, f35;      /* +32 second hp, +33 last death frame, +34/+35 flash frames */
    uint8_t f36, f37;             /* +36/+37 per-type timers (copied as one word from template +36) */
    int16_t f38, f40;             /* +38/+40 fire-request muzzle x/y */
    uint8_t f42, rnd43;           /* +42 forced target (1=P1, 2=P2), +43 random count */
    int16_t score_bcd44; uint8_t sfx46, f47; /* +44 score BCD, +46 hit sound */
    int16_t f48, f50, f52, f54;   /* +48..+54 collision box x1,x2,y1,y2 (written by object_tail) */
    uint32_t tmpl; int slot;      /* native: template chip address ($2B68 + t*48) */
} Object;

/* ---------- effect pool: $4976, 16 x 20 bytes, compacted+y-sorted (ENGINE_frame_player.md §7) ---------- */
enum { EF_BULLET = 7, EF_MISSILE = 12, EF_NOVA = 16 };
typedef struct Effect { int32_t x, y, vx, vy; uint8_t gfx, frame, channel, age; } Effect;

/* ---------- player ($4E3C / $4F46, 266 bytes; ENGINE_frame_player.md §4) ---------- */
typedef struct Shot { int16_t x, y, vx, vy; uint8_t b8, b9, b10; int8_t dmg; } Shot;    /* +122.. 12 x 12 */
enum { JOY_UP = 1, JOY_RIGHT = 2, JOY_DOWN = 4, JOY_LEFT = 8, JOY_FIRE = 16, JOY_NOVA = 32 };   /* +44 */
enum { SHIP_FLYING = 0, SHIP_EXPLODING = 0x64, SHIP_ENTERING = 0x96, SHIP_DEAD = 0xC8, SHIP_NONE = 0xFF };   /* +38 */
typedef struct Player {
    int16_t x, y;                 /* +0/+2 playfield (x 256..512, y 258..480) */
    int16_t sx, sy;               /* +4/+6 sprite-space copies (sx = x + cam - 256, sy = y) */
    int16_t height8, bank10;      /* +8 sprite height, +10 banking frame 0..12 */
    int8_t  slot_state[12];       /* +12 (0 launch, 1 in flight, -1 unused) */
    uint32_t weapon_table;        /* +24 chip address (template base = table+24) */
    int16_t fire_period28, roll30, rotate32;    /* +28/+30/+32 */
    uint8_t state38, joined39, f40, free_respawn41; int16_t cursor42;   /* +38/+39/+41/+42 */
    uint8_t joy44, f45; int16_t cooldown46; uint8_t entry48, explode49; int16_t f50, invuln52, spawn_x54;
    uint8_t lives56, repeat57; int16_t weapon58; int16_t level60, shot_hw62, shot_hh64, nova66, hud68, hud70, hud72;
    uint8_t gfxl74, gfxr75; uint32_t f76, f80, f84; int16_t hudx88;
    uint8_t nova90, f91, gesture[4], gesture_t96, bonus97, bonus98, mouse99, f100;
    char initials[4];             /* +34..+37 */
    int16_t f120;                 /* +120 initials timeout */
    char score[8], score_prev[8]; /* +106 / +114 ASCII digits */
    Shot shots[12];               /* +122 */
    int index;
} Player;

/* ---------- globals (A5 = $8000; ENGINE_frame_player.md §2) ---------- */
typedef struct {
    uint16_t dframe;              /* -28552 (low byte = -28551, bits 0/1/2 are the phase bits) */
    uint16_t demo_frames;         /* -28550 (0 in a live game; the attract counter, ++ in LAB_45D2) */
    uint8_t  demo;                /* -28516 (attract mode; input comes from the recording) */
    uint32_t demo_stream;         /* 6810(A5): chip address of the demo input recording cursor
                                     ($22F80 in LODDAT; 2 bytes -> P1/P2 joy44 per LIVE game frame) */
    int16_t  cam7204, progress7206, rowphase7212, scrolled7222, stage7228, pending7230, done7232, hold7234;
    uint32_t maprow7214;          /* chip address of the 24-word map row cursor */
    uint32_t ring7208; int16_t rows7218;   /* terrain ring (kept for completeness) */
    uint32_t stage_desc7224;      /* $14EA + stage*$8C */
    uint32_t wave2736;            /* -2736: wave list chip address */
    int16_t  msg8514, msg_lines8516, msg_hold8522, game_over8524, hold16122;
    uint32_t msg_text8518;
    uint8_t  gate4100, hangars4099, two_players2732, no_ship16120, finished4096;
    int8_t   boss_hold1570;
    uint16_t hold_a14c;           /* external scroll-hold word $A14C */
    uint8_t  armour[7];           /* -2200..-2194 by difficulty */
    int16_t  bullet_speed14386, bullet_limit10062, difficulty10066, start_lives10059, start_weapon10060;
    int32_t  t8_vmax1790, t8_acc1786;   /* -1790 / -1786 (type-8 missile) */
    uint8_t  rng_index;           /* low byte of the $2B1A pointer ($2B1D) */
    uint8_t  nova25334;           /* -25334 */
    uint32_t nova_script25338;    /* -25338 */
    uint8_t  flash27618_unused;   /* the real one is the global g_27618 below */
    int16_t  fire_ox14398, fire_oy14396;  uint8_t mine14384;   /* fire-request staging */
    uint8_t  final_boss26242;
    Hostile  hostiles[12];
    Object   objects[18];
    Effect   effects[16];
    Player   players[2];
    Player  *sel;                 /* -18624: the selected collision/score player of this game frame */
    uint8_t  reprocess;           /* native: LAB_79F2 re-entry request (turn_into_pickup) */
    long     frame_no;            /* native: game frames run since eng_init */
    int stat_shots[2], stat_hits[2];   /* native stats only (not part of the original state) */
} BsGame;
extern BsGame g;

/* engine-owned globals the behaviour files share (names = A5 offsets / addresses) */
extern uint8_t g_8414, g_8413;    /* -8414 turret turn delay (10), -8413 reload (50); -2/-10 per stage clear */
extern uint8_t g_8397;            /* -8397 gate consecutive-frame counter */
extern int16_t g_14390, g_14388;  /* forced bullet direction for the next fire request */
extern uint8_t g_27618;           /* -27618 palette-flash countdown */
extern int16_t g_790A[2][3];      /* $790A/$7910 explosion-puff slots {x, y, count} */

/* ---------- verbs (engine core; names = original LABs) ---------- */
void eng_init(int stage, int players, int weapon, int lives, int difficulty);
void eng_join_player2(void);      /* port 2 presses fire during play */
void eng_demo_init(void);                    /* $6FA attract entry: two players, recorded input from
                                                LODDAT $22F80, mid-level start (progress $EA0), ends
                                                when demo_frames reaches $FA0 (the caller watches) */
void eng_frame(const uint8_t joy[2]);        /* LAB_AA0..LAB_CE6: one game frame (= update + finish) */
void eng_frame_update(void);                 /* scroll + object pool + 4 hostile passes (up to the $BE8 objlog point) */
void eng_frame_finish(const uint8_t joy[2]); /* the rest of the iteration (input .. wave scheduler .. extra life) */
uint8_t rng(void);                           /* $2B1E: table byte at $17400 + idx++, order matters for parity */
void scroll_frame(void);                     /* LAB_9C44 */
void object_spawner(void);                   /* LAB_3078 (+ hangar gates) */
Object *object_alloc(uint32_t tmpl, int column);           /* $32F4 (tmpl = chip address) */
void wave_scheduler(void);                   /* LAB_7556 */
Hostile *hostile_alloc(int16_t relx, int16_t rely, uint8_t type, uint32_t p12);   /* LAB_75E4 -> 7608 */
Hostile *hostile_init(Hostile *h, int16_t relx, int16_t rely, uint8_t type, uint32_t p12); /* LAB_7608 */
void hostile_update_all(int lower, int pass_b);  /* LAB_79E2 (one of the four passes) */
void hostile_explode(Hostile *h);            /* LAB_98E6: start the explosion (handler calls on hp < 0) */
void hostile_draw(Hostile *h, uint32_t planes, uint32_t mask);  /* LAB_9814/981C: box refresh + render entry */
void turn_into_pickup(Hostile *h, int nova_pickup); /* LAB_9986 (record is re-processed by the driver) */
void hostile_reprocess(Hostile *h);          /* BRA LAB_79F2: ask the driver to re-run the record */
const uint8_t *script_resolve(uint32_t amiga_addr);   /* chip address -> native pointer (bs_chip + addr) */
uint8_t aim_players(int16_t fx, int16_t fy, int16_t *d5, int16_t *d6);
    /* LAB_491C: D7 bits (0 = P1 left of point, 1 = P1 above, 2/3 = same for P2), D5/D6 = Manhattan distances */
void object_update_all(void);                /* LAB_5F34: prologue (clear bit5, live test, BCHG bit3) + object_handlers[type] */
void object_tail(Object *o);                 /* LAB_6EE4: off-screen free, hit box f48..f54, render record */
int  object_shared_2x(Object *o);            /* LAB_686C shared damage/flash/death part (objects.c exports it) */
void effects_from_requests(void);            /* LAB_4704/47D2 (aim LAB_491C) */
void effects_update(void);                   /* LAB_4ADA (runs twice per game frame) */
void effect_remove(int slot);                /* LAB_4E1A: compact the pool */
void read_input(Player *p, uint8_t joy);     /* LAB_9A9E tail: joy bits 0..5 -> p->joy44 */
void player_fire(Player *p);                 /* LAB_3F54 */
void move_ship(Player *p);                   /* LAB_5070 (twice per game frame) */
void ship_and_shots(Player *p);              /* LAB_5234/5374/5490 (twice): explosion timer, shot motion, wobble */
void nova(void);                             /* LAB_1D0C (twice) */
void portrait_1e02(Player *p);               /* LAB_1E02/1E34 counters (twice) */
void collide_shots_hostiles(void);           /* LAB_34FA (selected player) */
void collide_shots_objects(void);            /* LAB_3424 (selected player) */
void collide_player_effects(void);           /* LAB_3748 (selected player) */
void collide_player_hostiles(void);          /* LAB_35F0 (+ pickups LAB_369A, selected player) */
void kill_hostile_sound(Hostile *h);         /* LAB_35CE */
void award(uint16_t bcd);                    /* LAB_40BE to the selected player (points = BCD(hi) + 100*BCD(lo)) */
void hit_player(Player *p);                  /* $3662/$378A common part */
void set_weapon(Player *p, uint32_t table);  /* LAB_2B32 (table = chip address) */
void reload_weapon(Player *p);               /* LAB_3722 = set_weapon($2024[f58*24/4 + f60*4]) */
void extra_life_check(void);                 /* LAB_139C */
void game_over_check(void);                  /* LAB_410A (g.no_ship16120 only; initials flow stubbed) */
void stage_clear(void);                      /* LAB_7002 / 7180 (stubbed: sets g.done7232, stops) */
void sfx(int n);                             /* EXT_2470E, D0 = n (re/sfx_triggers.txt) */
extern void (*eng_sfx_hook)(int n);          /* simrun/front end taps the trigger */
extern void (*eng_display_hook)(void);       /* native: called once per DISPLAY frame inside eng_frame_finish */

typedef void (*HostileHandler)(Hostile *h);
typedef void (*ObjectHandler)(Object *o);
extern HostileHandler hostile_handlers[14];      /* re/handlers.txt (weak stubs in engine.c; hostiles.c overrides) */
extern ObjectHandler  object_handlers[0x2a];     /* objects.c */

/* ---------- render list (filled each frame for the front end) ---------- */
typedef struct RenderEntry { int kind; int type; int x, y; uint32_t gfx, mask; int w_words, h, frame;
                 int stride;       /* per-plane byte offset (frame_bytes / f10); 0 = 2*words*h */
                 uint32_t reveal;  /* type-3 horizontal reveal: AND over the first 32 mask bits ($76E8) */
} RenderEntry;
extern RenderEntry render_list[256]; extern int render_count;
extern uint32_t render_reveal_mask;   /* $76E8 model: consumed+reset by the next hostile draw */
#endif
