/* framecmp.c -- pixel-compare the native renderer against an oracle screenshot.
 *
 * Drives the engine exactly like simrun (same option flags, so the runs that
 * are parity-proven against the objlogs can be replayed), renders the frame
 * that matches an oracle PPM from re/trace/shots (352x288 P6, visible window
 * at offset (32,12), 288 x 255), and reports per-layer mismatch counts.
 *
 * usage: framecmp SHOT.ppm SHOT_F [--fire] [--autofire] [--autopilot]
 *                 [--invuln] [--fbase N] [--stage N] [--layers MASK]
 *                 [--out PREFIX] [--search]
 *   SHOT_F   display-frame number the shot was dumped at (i_06000 -> 6000)
 *   --search try +-2 px x/y offsets and +-2 frames, report the best match
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/bsdata.h"
#include "../src/engine/engine.h"
#include "../src/render.h"

static BsData data;
static long hook_target = -1, hook_fbase;
static int hook_layers;
static uint32_t hook_rgba[BS_VIEW_W * BS_VIEW_H];
static int hook_fired;
static void display_hook(void)
{
    if (hook_fired || hook_fbase + g.dframe != hook_target) return;
    render_frame(hook_rgba, hook_layers);
    hook_fired = 1;
    if (getenv("BS_DUMPLIST")) { fprintf(stderr, "cam=%d progress=%d rowphase=%d maprow=%05x\n", g.cam7204, g.progress7206, g.rowphase7212, g.maprow7214); }
    if (getenv("BS_DUMPLIST"))
        for (int i = 0; i < render_count; i++) {
            const RenderEntry *r = &render_list[i];
            fprintf(stderr, "rl[%d] kind=%d x=%d y=%d gfx=%05x mask=%05x w=%d h=%d fr=%d\n",
                    i, r->kind, r->x, r->y, r->gfx, r->mask, r->w_words, r->h, r->frame);
        }
    if (getenv("BS_DEBUG"))
        fprintf(stderr, "dbg: P0 x=%d y=%d st=%u bank=%d w58=%d nova90=%u inv=%d h8=%d\n",
                g.players[0].x, g.players[0].y, g.players[0].state38, g.players[0].bank10,
                g.players[0].weapon58, g.players[0].nova90, g.players[0].invuln52,
                g.players[0].height8);
    if (getenv("BS_DEBUG"))
        for (int i = 0; i < 12; i++) {
            const Shot *s = &g.players[0].shots[i];
            if (s->x)
                fprintf(stderr, "dbg: shot %d st=%d x=%d y=%d vx=%d vy=%d h=%d gfx=%02x dly=%d\n",
                        i, g.players[0].slot_state[i], s->x, s->y, s->vx, s->vy, s->b8, s->b9, s->b10);
        }
}

static uint8_t *load_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    char magic[3] = {0};
    int maxv;
    if (fscanf(f, "%2s %d %d %d", magic, w, h, &maxv) != 4 || strcmp(magic, "P6")) {
        fprintf(stderr, "%s: not a P6 ppm\n", path); fclose(f); return NULL;
    }
    fgetc(f);
    uint8_t *px = malloc((size_t)*w * *h * 3);
    if (fread(px, 3, (size_t)*w * *h, f) != (size_t)*w * *h) { fprintf(stderr, "short read\n"); }
    fclose(f);
    return px;
}

static void save_ppm(const char *path, const uint32_t *rgba, int w, int h)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t p[3] = { (uint8_t)rgba[i], (uint8_t)(rgba[i] >> 8), (uint8_t)(rgba[i] >> 16) };
        fwrite(p, 1, 3, f);
    }
    fclose(f);
}

/* compare native canvas vs shot at canvas offset (ox,oy) in the ppm */
static long compare(const uint32_t *rgba, const uint8_t *px, int pw, int ph,
                    int ox, int oy, int y0, int y1, long *total)
{
    long bad = 0, tot = 0;
    for (int y = y0; y < y1; y++) {
        int sy = y + oy;
        if (sy < 0 || sy >= ph) continue;
        for (int x = 0; x < BS_VIEW_W; x++) {
            int sx = x + ox;
            if (sx < 0 || sx >= pw) continue;
            const uint8_t *p = px + ((size_t)sy * pw + sx) * 3;
            uint32_t v = rgba[(size_t)y * BS_VIEW_W + x];
            tot++;
            if (p[0] != (uint8_t)v || p[1] != (uint8_t)(v >> 8) || p[2] != (uint8_t)(v >> 16))
                bad++;
        }
    }
    if (total) *total = tot;
    return bad;
}

int main(int argc, char **argv)
{
    const char *dir = "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    const char *shot = NULL, *outpfx = NULL;
    long shot_f = -1, fbase = 0;
    int mode_demo = 0;
    int stage = 0, mode_fire = 0, mode_autofire = 0, mode_autopilot = 0, invuln = 0;
    int layers = BS_L_TERRAIN | BS_L_BOBS | BS_L_SPRITES, search = 0, pos = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--fire")) mode_fire = 1;
        else if (!strcmp(argv[i], "--autofire")) mode_autofire = 1;
        else if (!strcmp(argv[i], "--autopilot")) mode_autopilot = 1;
        else if (!strcmp(argv[i], "--invuln")) invuln = 1;
        else if (!strcmp(argv[i], "--fbase") && i + 1 < argc) fbase = atol(argv[++i]);
        else if (!strcmp(argv[i], "--stage") && i + 1 < argc) stage = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--layers") && i + 1 < argc) layers = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outpfx = argv[++i];
        else if (!strcmp(argv[i], "--search")) search = 1;
        else if (!strcmp(argv[i], "--demo")) mode_demo = 1;
        else if (pos == 0) { shot = argv[i]; pos++; }
        else if (pos == 1) { shot_f = atol(argv[i]); pos++; }
        else { fprintf(stderr, "framecmp: bad arg %s\n", argv[i]); return 2; }
    }
    if (!shot || shot_f < 0) { fprintf(stderr, "usage: framecmp SHOT.ppm SHOT_F [opts]\n"); return 2; }

    int pw, ph;
    uint8_t *px = load_ppm(shot, &pw, &ph);
    if (!px) return 1;

    if (bs_open(&data, dir) || bs_load_stage(&data, stage)) return 1;
    bs_chip = data.chip;
    eng_init(stage, 1, 3, 3, 1);
    if (mode_demo) eng_demo_init();

    long want = (shot_f - fbase) / 2 + 2;    /* run past the shot frame */
    hook_target = shot_f; hook_fbase = fbase; hook_layers = layers;
    eng_display_hook = display_hook;
    long bestbad = -1; int bestdx = 0, bestdy = 0; long bestn = 0;

    for (long n = 0; n < want; n++) {
        if (invuln)
            for (int p = 0; p < 2; p++) {
                Player *pl = &g.players[p];
                if (pl->explode49 == 0 && (uint16_t)pl->invuln52 < 100) pl->invuln52 = 100;
            }
        eng_frame_update();
        long F = fbase + g.dframe;
        uint8_t joy[2] = { 0, 0 };
        if (mode_fire) joy[0] = 0x10;
        else if (mode_autofire) joy[0] = (F % 100) < 20 ? 0x10 : 0;
        if (mode_autopilot) {
            long ph2 = F % 400;
            uint8_t d = ph2 < 150 ? 0x08 : ph2 < 200 ? 0x01 : ph2 < 350 ? 0x02 : 0x04;
            joy[0] = d | ((mode_fire || (F % 100) < 20) ? 0x10 : 0);
        }
        eng_frame_finish(joy);
        if (hook_fired) break;
    }
    if (!hook_fired) { fprintf(stderr, "framecmp: shot frame not reached\n"); return 1; }
    {
        int r = search ? 2 : 0;
        for (int dy = -r; dy <= r; dy++)
            for (int dx = -r; dx <= r; dx++) {
                long tot;
                long bad = compare(hook_rgba, px, pw, ph, 32 + dx, 12 + dy, 24, 255, &tot);
                if (bestbad < 0 || bad < bestbad) { bestbad = bad; bestdx = dx; bestdy = dy; }
                if (!search)
                    printf("F=%ld layers=%d: %ld/%ld mismatched (%.2f%%)\n",
                           shot_f, layers, bad, tot, 100.0 * bad / (tot ? tot : 1));
            }
        if (outpfx) {
            char nm[512];
            snprintf(nm, sizeof nm, "%s_native.ppm", outpfx);
            save_ppm(nm, hook_rgba, BS_VIEW_W, BS_VIEW_H);
        }
    }
    if (search)
        printf("best: dx=%d dy=%d bad=%ld\n", bestdx, bestdy, bestbad);
    if (outpfx) {
        char nm[512];
        snprintf(nm, sizeof nm, "%s_oracle.ppm", outpfx);
        FILE *f = fopen(nm, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", BS_VIEW_W, BS_VIEW_H);
            for (int y = 0; y < BS_VIEW_H; y++)
                for (int x = 0; x < BS_VIEW_W; x++) {
                    int sy = y + 12 + bestdy, sx = x + 32 + bestdx;
                    uint8_t p[3] = {0,0,0};
                    if (sy >= 0 && sy < ph && sx >= 0 && sx < pw)
                        memcpy(p, px + ((size_t)sy * pw + sx) * 3, 3);
                    fwrite(p, 1, 3, f);
                }
            fclose(f);
        }
    }
    free(px);
    return 0;
}
