/* simrun.c -- headless native Battle Squadron run, writing an objlog in the
 * SAME format as the oracle host's `--objlog` (src/host/main.c): one block of
 * G/P/S/H/O/E lines per game frame, sampled at the $BE8 point (after the four
 * hostile passes, before input/collision/scheduler).
 *
 * usage: simrun STAGE FRAMES OUT.txt [--data DIR] [--fire] [--autofire]
 *               [--autopilot] [--invuln] [--rng N] [--fbase N] [--players N]
 *   --fire       hold fire on player 1 (matches the host's --fire runs)
 *   --autofire   fire 20 of every 100 display frames (host --autofire)
 *   --autopilot  the BS_AUTOPILOT sweep (dir from (fbase + dframe) % 400)
 *   --invuln     BS_INVULN: top player 1's +52 up to 100 while alive
 *   --rng N      initial RNG index ($2B1D) -- calibrate against the capture
 *   --fbase N    display-frame number of the first game frame (host F of the
 *                first G line), used for the F column and the autopilot phase
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/bsdata.h"
#include "../src/engine/engine.h"

static BsData data;
static FILE *out;
static long fbase;

static void log_frame(void)
{
    long F = fbase + g.dframe;
    fprintf(out, "%ld G %u %u %u %u %u %u %u %u %u\n", F,
            g.dframe, (uint16_t)g.cam7204, (uint16_t)g.progress7206, (uint16_t)g.stage7228,
            (uint8_t)g.dframe, (uint16_t)g.scrolled7222, (uint16_t)g.rowphase7212,
            (uint16_t)g.msg8514, g.demo);
    for (int p = 0; p < 2; p++) {
        Player *pl = &g.players[p];
        fprintf(out, "%ld P %u %d %d %u %u %u %u %u %u %u %u\n", F, p,
                pl->sx, pl->sy, pl->state38, pl->joined39, pl->entry48,
                pl->explode49, (uint16_t)pl->invuln52, pl->lives56,
                (uint16_t)pl->level60, (uint16_t)pl->hud68);
        for (int s = 0; s < 12; s++) {
            Shot *sh = &pl->shots[s];
            if (!sh->x) continue;
            fprintf(out, "%ld S %u.%u %d %d %d %d %d\n", F, p, s,
                    sh->x, sh->y, sh->vx, sh->vy, sh->dmg);
        }
    }
    for (int i = 0; i < 12; i++) {
        Hostile *h = &g.hostiles[i];
        if (!hxw(h)) continue;
        fprintf(out, "%ld H %u %d %d %02x %u %u %u %u %02x %u %u %u %08x %06x\n", F, i,
                hxw(h), hyw(h), h->type, (uint8_t)h->hp, h->t27, h->t28,
                h->explode, h->flags, h->flash, h->damage, h->frame,
                (unsigned)h->p12, (unsigned)h->gfx);
    }
    for (int i = 0; i < 18; i++) {
        Object *o = &g.objects[i];
        if (!o->x) continue;
        fprintf(out, "%ld O %u %d %d %02x %u %u %u %02x %02x %u %u %u %06x\n", F, i,
                o->x, o->y, o->type, o->f19, o->f25, (uint8_t)o->hp28,
                o->f30, o->flags31, o->f33, o->f42, o->rnd43, (unsigned)o->gfx12);
    }
    for (int i = 0; i < 16; i++) {
        Effect *e = &g.effects[i];
        if (!(int16_t)(e->x >> 16)) continue;
        fprintf(out, "%ld E %u %d %d %d %d %u %u %u %u\n", F, i,
                (int16_t)(e->x >> 16), (int16_t)(e->y >> 16),
                (int)e->vx, (int)e->vy, e->gfx, e->frame, e->channel, e->age);
    }
}

int main(int argc, char **argv)
{
    const char *dir = "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    int stage = 0, mode_fire = 0, mode_autofire = 0, mode_autopilot = 0, invuln = 0, players = 1;
    int mode_demo = 0;
    long frames = 500;
    int rng0 = 0;
    const char *path = NULL;
    int pos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--fire")) mode_fire = 1;
        else if (!strcmp(argv[i], "--autofire")) mode_autofire = 1;
        else if (!strcmp(argv[i], "--autopilot")) mode_autopilot = 1;
        else if (!strcmp(argv[i], "--invuln")) invuln = 1;
        else if (!strcmp(argv[i], "--demo")) mode_demo = 1;
        else if (!strcmp(argv[i], "--players") && i + 1 < argc) players = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--rng") && i + 1 < argc) rng0 = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--fbase") && i + 1 < argc) fbase = atol(argv[++i]);
        else if (pos == 0) { stage = atoi(argv[i]); pos++; }
        else if (pos == 1) { frames = atol(argv[i]); pos++; }
        else if (pos == 2) { path = argv[i]; pos++; }
        else { fprintf(stderr, "simrun: bad arg %s\n", argv[i]); return 2; }
    }
    if (!path) { fprintf(stderr, "usage: simrun STAGE FRAMES OUT.txt [options]\n"); return 2; }
    if (bs_open(&data, dir)) return 1;
    if (bs_load_stage(&data, stage)) return 1;
    bs_chip = data.chip;
    out = fopen(path, "w");
    if (!out) { perror(path); return 1; }
    /* options match the captures: 3 lives, weapon 3, difficulty 1, 1 player */
    if (mode_demo) eng_demo_init();
    else eng_init(stage, players, 3, 3, 1);
    if (!mode_demo) g.rng_index = (uint8_t)rng0;
    for (long n = 0; n < frames; n++) {
        if (invuln) {                                 /* BS_INVULN (host: per display frame) */
            for (int p = 0; p < 2; p++) {
                Player *pl = &g.players[p];
                if (pl->explode49 == 0 && (uint16_t)pl->invuln52 < 100) pl->invuln52 = 100;
            }
        }
        eng_frame_update();
        log_frame();
        uint8_t joy[2] = { 0, 0 };
        long F = fbase + g.dframe;                    /* host display frame at input time */
        if (mode_fire) joy[0] = 0x10;
        else if (mode_autofire) joy[0] = (F % 100) < 20 ? 0x10 : 0;
        if (mode_autopilot) {
            long ph = F % 400;
            uint8_t dir = ph < 150 ? 0x08 : ph < 200 ? 0x01 : ph < 350 ? 0x02 : 0x04;
            /* host joy_state bits (1 up,2 down,4 left,8 right) map to game bits
             * via the quadrature decode: up->1, right->2, down->4, left->8 */
            joy[0] = dir | ((mode_fire || (F % 100) < 20) ? 0x10 : 0);
        }
        if (invuln) {
            for (int p = 0; p < 2; p++) {
                Player *pl = &g.players[p];
                if (pl->explode49 == 0 && (uint16_t)pl->invuln52 < 100) pl->invuln52 = 100;
            }
        }
        eng_frame_finish(joy);
    }
    fclose(out);
    fprintf(stderr, "simrun: %ld game frames, progress %u, score P1 %.8s\n",
            frames, (uint16_t)g.progress7206, g.players[0].score);
    return 0;
}
