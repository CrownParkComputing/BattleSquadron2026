/* bobscan.c -- ground truth for the sprite browser: replay the engine and dump
 * every distinct bob the in-game render list ever carries, per hostile type /
 * object template.  Used to build (and to regression-check) the sprite catalog
 * in src/sprites.c.
 * usage: bobscan STAGE FRAMES [--fire] [--autofire] [--autopilot] [--invuln] [--demo] [--fbase N] [--rng N] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/bsdata.h"
#include "../src/engine/engine.h"

static BsData data;
typedef struct { int kind, type, w, h, stride; uint32_t gfx, mask; } Rec;
static Rec recs[8192]; static int nrec;

static void note(const RenderEntry *r)
{
    for (int i = 0; i < nrec; i++)
        if (recs[i].kind == r->kind && recs[i].type == r->type && recs[i].gfx == r->gfx &&
            recs[i].mask == r->mask && recs[i].w == r->w_words && recs[i].h == r->h &&
            recs[i].stride == r->stride) return;
    if (nrec >= 8192) return;
    recs[nrec++] = (Rec){ r->kind, r->type, r->w_words, r->h, r->stride, r->gfx, r->mask };
}
static int cmp(const void *a, const void *b)
{
    const Rec *x = a, *y = b;
    if (x->kind != y->kind) return x->kind - y->kind;
    if (x->type != y->type) return x->type - y->type;
    if (x->gfx != y->gfx) return x->gfx < y->gfx ? -1 : 1;
    return (int)x->mask - (int)y->mask;
}

int main(int argc, char **argv)
{
    const char *dir = getenv("BS_DATA") ? getenv("BS_DATA") : "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    int stage = 0, fire = 0, autofire = 0, autopilot = 0, invuln = 0, demo = 0, rng0 = 0, pos = 0;
    long frames = 20000, fbase = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--fire")) fire = 1;
        else if (!strcmp(argv[i], "--autofire")) autofire = 1;
        else if (!strcmp(argv[i], "--autopilot")) autopilot = 1;
        else if (!strcmp(argv[i], "--invuln")) invuln = 1;
        else if (!strcmp(argv[i], "--demo")) demo = 1;
        else if (!strcmp(argv[i], "--rng") && i + 1 < argc) rng0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fbase") && i + 1 < argc) fbase = atol(argv[++i]);
        else if (pos == 0) { stage = atoi(argv[i]); pos++; }
        else if (pos == 1) { frames = atol(argv[i]); pos++; }
    }
    if (bs_open(&data, dir)) return 1;
    if (bs_load_stage(&data, stage)) return 1;
    bs_chip = data.chip;
    if (demo) eng_demo_init(); else { eng_init(stage, 1, 3, 3, 1); g.rng_index = (uint8_t)rng0; }
    for (long n = 0; n < frames; n++) {
        if (invuln) for (int p = 0; p < 2; p++)
            if (g.players[p].explode49 == 0 && (uint16_t)g.players[p].invuln52 < 100) g.players[p].invuln52 = 100;
        eng_frame_update();
        for (int i = 0; i < render_count; i++)
            if (render_list[i].kind == 1 || render_list[i].kind == 2) note(&render_list[i]);
        uint8_t joy[2] = { 0, 0 };
        long F = fbase + g.dframe;
        if (fire) joy[0] = 0x10; else if (autofire) joy[0] = (F % 100) < 20 ? 0x10 : 0;
        if (autopilot) {
            long ph = F % 400;
            uint8_t dir = ph < 150 ? 0x08 : ph < 200 ? 0x01 : ph < 350 ? 0x02 : 0x04;
            joy[0] = dir | ((fire || (F % 100) < 20) ? 0x10 : 0);
        }
        if (invuln) for (int p = 0; p < 2; p++)
            if (g.players[p].explode49 == 0 && (uint16_t)g.players[p].invuln52 < 100) g.players[p].invuln52 = 100;
        eng_frame_finish(joy);
    }
    qsort(recs, (size_t)nrec, sizeof recs[0], cmp);
    for (int i = 0; i < nrec; i++)
        printf("%s stage=%d type=%02x gfx=%05x mask=%05x w=%d h=%d stride=%d\n",
               recs[i].kind == 2 ? "H" : "O", stage, recs[i].type, recs[i].gfx, recs[i].mask,
               recs[i].w, recs[i].h, recs[i].stride);
    return 0;
}
