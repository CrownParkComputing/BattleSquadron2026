#include "engine.h"
#include "bsdata.h"
#include <assert.h>
#include <stdio.h>

static BsData data;
static void finish_player(int player) {
    Player *p = &g.players[player];
    p->state38 = 0xC8;
    p->f91 = 0xFF;
    p->f45 = p->f40 = 0;
    p->joy44 = JOY_FIRE;
    p->hud68 = 0x3E7;
    game_over_check();
}
int main(void) {
    assert(bs_open(&data, "amiga/original/whdload/BattleSquadron/data") == 0);
    bs_chip = data.chip;
    assert(bs_load_stage(&data, 0) == 0);
    const int limits[] = { 0, 3, 5, 7, -1 };
    for (unsigned k = 0; k < sizeof limits / sizeof limits[0]; k++) {
        eng_init(0, 2, 3, 3, 1);
        g.continues_left[0] = g.continues_left[1] = limits[k];
        for (int i = 0; i < 10; i++) {
            int allowed = limits[k] < 0 || i < limits[k];
            finish_player(0);
            assert(g.players[0].state38 == (allowed ? 0x96 : 0xC8));
            assert(g.continues_left[1] == limits[k]);
            if (limits[k] >= 0) assert(g.continues_left[0] == (allowed ? limits[k] - i - 1 : 0));
            else assert(g.continues_left[0] == -1);
        }
        if (limits[k] >= 0) {
            g.continues_left[1] = 0;
            finish_player(1);
            g.msg8514 = g.hold16122 = 0;
            game_over_check();
            assert(g.game_over8524);
        }
    }
    eng_init(0, 1, 3, 3, 1);
    assert(g.continues_left[0] == -1 && g.continues_left[1] == -1);
    puts("PASS: Off / 3 / 5 / 7 / Unlimited, per-player budgets, exhaustion and new-game reset");
}
