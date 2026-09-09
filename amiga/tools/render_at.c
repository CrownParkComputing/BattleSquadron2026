/* Render one chosen live-game frame from the recompilation to a PPM, so it can
 * be compared pixel for pixel against the native runner's --dump-frame.
 *
 *   make build/render_at
 *   ./build/render_at 204 ours.ppm
 *   ./build/battle_squadron_native --frames 1799 --fire \
 *       --video-from 1700 --dump-frame ref.ppm
 *   python3 tools/ppm_diff.py ref.ppm ours.ppm --at 79 76 --size 64 48
 *
 * Recomp game frame N corresponds to native frame 1390 + 2*N with the usual
 * alignment.  Unlike tools/render_frame.c this stops where it is told rather
 * than hunting for a colour signature, which makes it useful for any sprite
 * question, not just the gold men. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "runtime.h"
#include "ocs_video.h"

static uint8_t rd(void *user, uint32_t address)
{
    return bs_recomp_read8(user, address);
}

static int display_frame(BsRecomp *m, uint8_t *clean)
{
    for (int g = 0; g < 512; g++) {
        if (bs_recomp_run(m, 1) != BS_RECOMP_OK) return 0;
        if (m->cpu.pc == 0xb54) break;
    }
    memcpy(clean, m->memory + 0x62000, 0x1e000);
    for (int g = 0; g < 512; g++) {
        if (bs_recomp_run(m, 1) != BS_RECOMP_OK) return 0;
        if (m->cpu.pc == 0xaa0) return 1;
    }
    return 0;
}

int main(int argc, char **argv)
{
    long target = argc > 1 ? atol(argv[1]) : 204;
    const char *out = argc > 2 ? argv[2] : "ours.ppm";
    BsRecomp *m = calloc(1, sizeof *m);
    uint8_t *clean = malloc(0x1e000);
    if (!m || !clean) return 1;
    if (bs_recomp_init(m, "original/whdload/BattleSquadron/data") !=
        BS_RECOMP_OK) {
        fprintf(stderr, "init: %s\n", m->error);
        return 1;
    }
    bs_recomp_set_external_playfield_restore(m, 1);
    static const struct { long steps; uint32_t pc; const char *name; } edges[] = {
        {38, 0x4ec, "spoken intro"}, {46, 0x5f6, "title"},
        {16, 0x6fa, "second title"}, {40, 0x7d0, "game setup"},
    };
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(m, edges[e].steps) != BS_RECOMP_OK ||
            m->cpu.pc != edges[e].pc) {
            fprintf(stderr, "%s: pc=$%06x %s\n", edges[e].name,
                    m->cpu.pc, m->error);
            return 1;
        }
    if (bs_recomp_start_new_game(m) != BS_RECOMP_OK) {
        fprintf(stderr, "new game: %s\n", m->error);
        return 1;
    }
    bs_recomp_enable_live_input(m, 1);

    OcsVideo *video = ocs_video_create();
    for (long f = 0; f <= target; f++) {
        bs_recomp_set_input(m, 0, BS_INPUT_FIRE);
        if (!display_frame(m, clean)) {
            fprintf(stderr, "stopped at frame %ld: %s\n", f, m->error);
            return 1;
        }
        /* With the host owning the playfield restore, the terrain captured at
         * $B54 has to be put back after the frame is rendered.  Skipping this
         * leaves every BOB smeared along its path -- a diagonal trail that
         * looks exactly like a sheared blit and is purely a tool artefact.
         * The last frame is left un-restored so the caller sees the BOBs. */
        if (f != target)
            memcpy(m->memory + 0x62000, clean, 0x1e000);
    }
    OcsVideoSource source = {
        .user = m, .read8 = rd,
        .chip_mask = BS_RECOMP_MEMORY_SIZE - 1,
        .copper_address = ((uint32_t)m->custom[0x080 >> 1] << 16) |
                          m->custom[0x082 >> 1],
    };
    const uint32_t *pixels = ocs_video_render(video, &source);
    FILE *file = fopen(out, "wb");
    if (!file) { perror(out); return 1; }
    fprintf(file, "P6\n%d %d\n255\n", OCS_VIDEO_WIDTH, OCS_VIDEO_HEIGHT);
    for (int i = 0; i < OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT; i++) {
        uint32_t value = pixels[i];
        fputc(value & 0xff, file);
        fputc((value >> 8) & 0xff, file);
        fputc((value >> 16) & 0xff, file);
    }
    fclose(file);
    fprintf(stderr, "wrote %s at game frame %ld (%dx%d)\n", out, target,
            OCS_VIDEO_WIDTH, OCS_VIDEO_HEIGHT);
    return 0;
}
