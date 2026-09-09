#include "engine.h"
#include "bsdata.h"
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static BsData data;
static int loads, fail_load;
static int load_stage(int stage) {
    loads++;
    return fail_load ? -1 : bs_load_stage(&data, stage);
}
extern void object_type_27(Object *o);

static void dwell(Object *gate, int frames) {
    for (int i = 0; i < frames; i++) {
        object_type_27(gate);
        stage_clear();
    }
}

int main(void) {
    assert(bs_open(&data, "amiga/original/whdload/BattleSquadron/data") == 0);
    bs_chip = data.chip;
    eng_stage_load_hook = load_stage;
    for (int players = 1; players <= 2; players++) {
        assert(bs_load_stage(&data, 0) == 0);
        eng_init(0, players, 2, 5, 1);
        for (int stage = 1; stage <= 3; stage++) {
            g.pending7230 = stage;
            g.scrolled7222 = 0;
            for (int i = 0; i < players; i++) {
                Player *p = &g.players[i];
                p->state38 = 0;
                p->explode49 = 0; /* gate fixture is a live, non-exploding ship */
                p->sx = 200; p->sy = 350;
                p->level60 = 3; p->nova66 = 2;
                memcpy(p->score, "00123456", 8);
            }
            Object gate = { .type = 0x27, .x = 200, .y = 350, .h6 = 32, .f19 = 2 };
            int before = loads;
            dwell(&gate, 9);
            assert(g.stage7228 == 0 && loads == before);
            /* Every live ship must stay inside for ten consecutive frames. */
            g.players[players - 1].sx = 230;
            dwell(&gate, 1);
            assert(g_8397 == 0 && loads == before);
            g.players[players - 1].sx = 200;
            dwell(&gate, 9);
            assert(g.stage7228 == 0);
            dwell(&gate, 1);
            assert(loads == before + 1 && g.stage7228 == stage && data.stage == stage);
            assert(g.progress7206 == 256 && !g.gate4100 && !g.pending7230);
            for (int i = 0; i < players; i++) {
                Player *p = &g.players[i];
                assert(p->lives56 == 5 && p->weapon58 == 2 && p->level60 == 3 && p->nova66 == 2);
                assert(memcmp(p->score, "00123456", 8) == 0);
                assert(p->state38 == 0x96 && p->entry48 == 0x91);
            }
            for (int frame = 0; frame < 200; frame++) {
                uint8_t joy[2] = { JOY_FIRE, players == 2 ? JOY_FIRE : 0 };
                eng_frame(joy);
                assert(g.stage7228 == stage);
            }
            if (players == 1) {
                uint32_t pixels[BS_VIEW_W * BS_VIEW_H];
                render_stage(&data);
                render_frame(pixels, 31);
                char path[64];
                snprintf(path, sizeof path, "build/stage%d.ppm", stage);
                FILE *f = fopen(path, "wb");
                assert(f);
                fprintf(f, "P6\n%d %d\n255\n", BS_VIEW_W, BS_VIEW_H);
                for (int j = 0; j < BS_VIEW_W * BS_VIEW_H; j++) {
                    uint8_t *pixel = (uint8_t *)&pixels[j];
                    fwrite(pixel, 1, 3, f);
                }
                fclose(f);
            }
            /* Reach the real scroll-end trigger, including stage 3's delay. */
            g.maprow7214 = 0x44000; g.rowphase7212 = 0;
            g.boss_hold1570 = 0; g.hold_a14c = 0;
            scroll_frame();
            if (stage == 3) {
                assert(g.hold7234 == 250);
                for (int i = 0; i < 250; i++) scroll_frame();
            }
            assert(g.gate4100 && !g.pending7230);
            stage_clear();
            static const int progress[] = { 0, 0xFA0, 0x1520, 0x1E70 };
            assert(g.stage7228 == 0 && data.stage == 0);
            assert(g.progress7206 == progress[stage]);
            assert(g.hangars4099 & (1 << stage));
            assert(!g.gate4100 && !g.pending7230);
        }
        assert(g.hangars4099 == 0x0E);
    }
    g.gate4100 = 0xFF; g.pending7230 = 1; fail_load = 1;
    stage_clear();
    assert(g.stage7228 == 0 && g.gate4100 && g.pending7230 == 1);
    puts("PASS: all three gates, interrupted dwell, one/two players, returns, retained progress, load failure");
}
