/* Runs N live-game frames from the recompilation and prints the same reference
 * state lines as the native runner's --dump-state, so tools/parity_diff.py can
 * align the two logs and report the first divergent record.
 *
 *   make build/recomp_dump build/battle_squadron_native
 *   ./build/battle_squadron_native --frames 3000 --fire --dump-state 1 > ref.txt
 *   ./build/recomp_dump 600 > ours.txt
 *   python3 tools/parity_diff.py ref.txt ours.txt
 *
 * The recomp boots through the title exactly like tools/render_frame.c, jumps
 * into a live game via bs_recomp_start_new_game, and holds player-one fire
 * (bs_recomp_set_input(m, 0, BS_INPUT_FIRE), which drives CIA-A port A bit 7
 * exactly like the native runner's --fire flag driving joy_state[1]). */
#include <stdio.h>
#include <stdlib.h>

#include "runtime.h"

/* The dump has to land on the same point of the 25 Hz cycle that the native
 * runner samples at the end of a PAL frame.  BS_DUMP_AT selects which of the
 * two half-frame boundaries that is, so a suspected phase error in the diff
 * can be told apart from a real logic bug.
 *
 * $BCE is the phase that matches: it follows the four LAB_79E2 passes that
 * move every record, and precedes the $CDA wave scheduler that creates new
 * ones, so a record's first dumped state is post-update exactly as in the
 * native log.  Sampling at $AA0 instead made every freshly created record
 * appear one game frame behind and swamped the diff. */
static uint32_t dump_pc = 0xbce;

static int display_frame(BsRecomp *m)
{
    uint32_t first = dump_pc == 0xaa0 ? 0xb54 : 0xaa0;
    for (int g = 0; g < 512; g++) {
        if (bs_recomp_run(m, 1) != BS_RECOMP_OK) return 0;
        if (m->cpu.pc == first) break;
    }
    for (int g = 0; g < 512; g++) {
        if (bs_recomp_run(m, 1) != BS_RECOMP_OK) return 0;
        if (m->cpu.pc == dump_pc) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    long frames = argc > 1 ? atol(argv[1]) : 2000;
    const char *at = getenv("BS_DUMP_AT");
    if (at) dump_pc = (uint32_t)strtoul(at, NULL, 16);
    BsRecomp *m = calloc(1, sizeof *m);
    if (!m) return 1;
    if (bs_recomp_init(m, "original/whdload/BattleSquadron/data") !=
        BS_RECOMP_OK) {
        fprintf(stderr, "recomp dump init: %s\n", m->error);
        return 1;
    }
    bs_recomp_set_external_playfield_restore(m, 1);

    struct { long steps; uint32_t pc; const char *name; } edges[] = {
        {38, 0x4ec, "spoken intro"},
        {46, 0x5f6, "title"},
        {16, 0x6fa, "second title"},
        {40, 0x7d0, "game setup"},
    };
    for (unsigned e = 0; e < 4; e++) {
        if (bs_recomp_run(m, edges[e].steps) != BS_RECOMP_OK ||
            m->cpu.pc != edges[e].pc) {
            fprintf(stderr, "%s: pc=$%06x %s\n", edges[e].name,
                    m->cpu.pc, m->error);
            return 1;
        }
    }
    if (bs_recomp_start_new_game(m) != BS_RECOMP_OK) {
        fprintf(stderr, "new game: %s\n", m->error);
        return 1;
    }
    bs_recomp_enable_live_input(m, 1);

    for (long f = 0; f < frames; f++) {
        bs_recomp_set_input(m, 0, BS_INPUT_FIRE);
        if (!display_frame(m)) {
            fprintf(stderr, "stop: %s\n", m->error);
            break;
        }
        /* Mirrors the native --dump-state block in src/host/main.c exactly. */
        printf("scroll=%u\n", bs_recomp_read16(m, 0x8000 + 7204));
        printf("state frame=%ld t1078=%u mode=%u p1 x=%u y=%u "
               "p2x=%u p2y=%u s38=%u c48=%u d49=%u inv52=%u "
               "lives56=%u wpn60=%u\n",
               f,
               bs_recomp_read16(m, 0x1078),
               bs_recomp_read16(m, 0x8000 + 7228),
               bs_recomp_read16(m, 0x4e40), bs_recomp_read16(m, 0x4e42),
               bs_recomp_read16(m, 0x4f4a), bs_recomp_read16(m, 0x4f4c),
               bs_recomp_read8(m, 0x4e3c + 38), bs_recomp_read8(m, 0x4e3c + 48),
               bs_recomp_read8(m, 0x4e3c + 49),
               bs_recomp_read16(m, 0x4e3c + 52),
               bs_recomp_read8(m, 0x4e3c + 56),
               bs_recomp_read16(m, 0x4e3c + 60));
        for (unsigned slot = 0; slot < 18; slot++) {
            unsigned object = 0x2e040 + slot * 0x40;
            unsigned x = bs_recomp_read16(m, object);
            if (!x) continue;
            printf("  obj %2u x=%4u y=%4u type=$%02X limit19=%3u "
                   "state25=%3u health28=%3u status30=%3u final33=%3u\n",
                   slot, x, bs_recomp_read16(m, object + 2),
                   bs_recomp_read8(m, object + 17),
                   bs_recomp_read8(m, object + 19),
                   bs_recomp_read8(m, object + 25),
                   bs_recomp_read8(m, object + 28),
                   bs_recomp_read8(m, object + 30),
                   bs_recomp_read8(m, object + 33));
        }
        if (getenv("BS_DUMP_PLAYER")) {
            printf("  praw");
            for (unsigned b = 0; b < 128; b++)
                printf(" %02X", bs_recomp_read8(m, 0x4e3c + b));
            printf("\n");
        }
        /* The 16-entry effect/debris pool that update_empty_effect_pool draws
         * as Amiga hardware sprites: +16 is the sprite height in lines, +17
         * indexes the graphics pointers at $C6B6, +18 picks the sprite base. */
        for (unsigned slot = 0; slot < 16; slot++) {
            unsigned effect = 0x4976 + slot * 20;
            unsigned x = bs_recomp_read16(m, effect);
            if (!x) continue;
            printf("  eff %2u x=%4u y=%4u vx=%08X vy=%08X h16=%3u g17=%3u "
                   "s18=%3u t19=%3u\n",
                   slot, x, bs_recomp_read16(m, effect + 4),
                   bs_recomp_read32(m, effect + 8),
                   bs_recomp_read32(m, effect + 12),
                   bs_recomp_read8(m, effect + 16),
                   bs_recomp_read8(m, effect + 17),
                   bs_recomp_read8(m, effect + 18),
                   bs_recomp_read8(m, effect + 19));
        }
        for (unsigned p = 0; p < 2; p++) {
            unsigned player = p ? 0x4f46 : 0x4e3c;
            for (unsigned slot = 0; slot < 12; slot++) {
                unsigned shot = player + 122 + slot * 12;
                unsigned x = bs_recomp_read16(m, shot);
                if (!x) continue;
                printf("  sht %u.%-2u x=%4u y=%4u vx=%04X vy=%04X dmg=%3d\n",
                       p, slot, x, bs_recomp_read16(m, shot + 2),
                       bs_recomp_read16(m, shot + 4),
                       bs_recomp_read16(m, shot + 6),
                       (int)(int8_t)bs_recomp_read8(m, shot + 11));
            }
        }
        for (unsigned slot = 0; slot < 12; slot++) {
            unsigned record = 0x2dc80 + slot * 0x50;
            unsigned x = bs_recomp_read16(m, record);
            if (!x) continue;
            char flags_text[20];
            snprintf(flags_text, sizeof flags_text, " fl30=$%02X",
                     bs_recomp_read8(m, record + 30));
            printf("  hos %2u x=%4u y=%4u type=$%02X f29=%3u f63=%3u "
                   "gfx36=$%06X gfx32=$%06X h50=%u w52=%u "
                   "v12=$%08X f57=%u d62=%u%s\n",
                   slot, x, bs_recomp_read16(m, record + 4),
                   bs_recomp_read8(m, record + 31),
                   bs_recomp_read8(m, record + 29),
                   bs_recomp_read8(m, record + 63),
                   bs_recomp_read32(m, record + 36),
                   bs_recomp_read32(m, record + 32),
                   bs_recomp_read16(m, record + 50),
                   bs_recomp_read16(m, record + 52),
                   bs_recomp_read32(m, record + 12),
                   bs_recomp_read8(m, record + 57),
                   bs_recomp_read8(m, record + 62),
                   getenv("BS_DUMP_FLAGS") ? flags_text : "");
        }
        fflush(stdout);
    }
    return 0;
}
