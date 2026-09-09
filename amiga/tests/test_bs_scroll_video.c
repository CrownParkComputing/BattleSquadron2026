#include "runtime.h"
#include "ocs_video.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t read_machine(void *user, uint32_t address)
{
    return bs_recomp_read8((const BsRecomp *)user, address);
}

static OcsVideoSource video_source(BsRecomp *machine)
{
    return (OcsVideoSource){
        .user = machine,
        .read8 = read_machine,
        .chip_mask = BS_RECOMP_MEMORY_SIZE - 1,
        .copper_address =
            ((uint32_t)machine->custom[0x080 >> 1] << 16) |
            machine->custom[0x082 >> 1],
    };
}

static int run_to_pc(BsRecomp *machine, uint32_t pc, long guard)
{
    while (machine->cpu.pc != pc && guard-- > 0)
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) return 0;
    return machine->cpu.pc == pc;
}

static int run_game_frame(BsRecomp *machine, uint8_t *clean_playfield)
{
    /* $AA0 is reached after the complete restore/draw loop.  $1078 changes
     * twice per loop and $B54 is a pre-restore midpoint; neither is safe as
     * a host presentation clock. */
    for (int guard = 0; guard < 512; guard++) {
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) return 0;
        if (machine->cpu.pc == 0xb54) break;
        if (guard == 511) return 0;
    }
    {
        uint32_t base = machine->cpu.a[5];
        uint16_t camera = bs_recomp_read16(machine, base + 7204);
        uint32_t expected = bs_recomp_read32(machine, base + 7208) +
            (((uint16_t)(camera - 0x100) >> 3) & 0xfffe);
        uint32_t actual =
            ((uint32_t)bs_recomp_read16(machine, 0xb278) << 16) |
            bs_recomp_read16(machine, 0xb27c);
        if (actual != expected) {
            fprintf(stderr, "scroll video: Copper pointer $%05x != ring "
                    "$%05x at progress $%04x D1=$%08x\n", actual,
                    expected, bs_recomp_read16(machine, base + 7206),
                    machine->cpu.d[1]);
            return 0;
        }
    }
    memcpy(clean_playfield, machine->memory + 0x62000, 0x1e000);
    for (int guard = 0; guard < 512; guard++) {
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) return 0;
        if (machine->cpu.pc == 0xaa0) return 1;
    }
    return 0;
}

static size_t shifted_matches_xy(const uint32_t *before,
                                 const uint32_t *after, int dx, int dy)
{
    size_t matches = 0;
    /* Ignore the upper HUD and side edges where sprites/camera steering can
     * legitimately alter pixels. */
    for (int y = 48; y < OCS_VIDEO_HEIGHT - 16; y++) {
        int old_y = y + dy;
        if (old_y < 0 || old_y >= OCS_VIDEO_HEIGHT) continue;
        for (int x = 16; x < OCS_VIDEO_WIDTH - 16; x++) {
            int old_x = x + dx;
            if (old_x < 0 || old_x >= OCS_VIDEO_WIDTH) continue;
            matches += after[y * OCS_VIDEO_WIDTH + x] ==
                       before[old_y * OCS_VIDEO_WIDTH + old_x];
        }
    }
    return matches;
}

int main(void)
{
    BsRecomp *machine = malloc(sizeof *machine);
    OcsVideo *video = ocs_video_create();
    uint32_t *before = malloc(OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT *
                              sizeof *before);
    uint8_t *clean_playfield = malloc(0x1e000);
    if (!machine || !video || !before || !clean_playfield) return 1;
    if (bs_recomp_init(machine,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK ||
        !run_to_pc(machine, 0x7d0, 200)) {
        fprintf(stderr, "scroll video: setup failed: %s\n", machine->error);
        return 1;
    }
    bs_recomp_set_external_playfield_restore(machine, 1);
    if (bs_recomp_start_new_game(machine) != BS_RECOMP_OK) {
        fprintf(stderr, "scroll video: player level start failed: %s\n",
                machine->error);
        return 1;
    }
    {
        size_t histogram[32] = {0};
        for (unsigned row = 0; row < 256; row++) {
            uint32_t address = 0x62000 + row * 0x30;
            for (unsigned word = 0; word < 24; word++) {
                uint16_t planes[5];
                for (unsigned plane = 0; plane < 5; plane++)
                    planes[plane] = bs_recomp_read16(machine,
                        address + plane * 0x6000 + word * 2);
                for (unsigned bit = 0; bit < 16; bit++) {
                    unsigned colour = 0;
                    for (unsigned plane = 0; plane < 5; plane++)
                        colour |= ((planes[plane] >> (15 - bit)) & 1) << plane;
                    histogram[colour]++;
                }
            }
        }
        uint32_t base = machine->cpu.a[5];
        uint32_t pointer =
            ((uint32_t)bs_recomp_read16(machine, 0xb278) << 16) |
            bs_recomp_read16(machine, 0xb27c);
        uint32_t expected_pointer =
            bs_recomp_read32(machine, base + 7208) +
            (((uint16_t)(bs_recomp_read16(machine, base + 7204) - 0x100)
              >> 3) & 0xfffe);
        if (histogram[0] < 90000 ||
            bs_recomp_read16(machine, base + 7206) != 0x00a0 ||
            bs_recomp_read32(machine, base + 7214) != 0x49e20 ||
            pointer != expected_pointer) {
            fprintf(stderr, "scroll video: invalid new-game starfield state\n");
            return 1;
        }
        printf("scroll video: opening starfield PASS (%zu/98304 black, "
               "map=$49e20)\n", histogram[0]);
    }
    if (!run_game_frame(machine, clean_playfield)) {
        fprintf(stderr, "scroll video: first gameplay frame failed\n");
        return 1;
    }
    OcsVideoSource source = video_source(machine);
    memcpy(before, ocs_video_render(video, &source),
           OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT * sizeof *before);
    memcpy(machine->memory + 0x62000, clean_playfield, 0x1e000);
    if (!run_game_frame(machine, clean_playfield)) {
        fprintf(stderr, "scroll video: second gameplay frame failed\n");
        return 1;
    }
    source = video_source(machine);
    const uint32_t *after = ocs_video_render(video, &source);
    size_t same = shifted_matches_xy(before, after, 0, 0);
    size_t up = shifted_matches_xy(before, after, 0, 1);
    size_t down = shifted_matches_xy(before, after, 0, -1);
    size_t samples = (OCS_VIDEO_HEIGHT - 64) * (OCS_VIDEO_WIDTH - 32);
    size_t best = up > down ? up : down;
    printf("scroll video: same=%zu up=%zu down=%zu of %zu\n",
           same, up, down, samples);
    if (best * 100 < samples * 80 || best <= same) {
        fprintf(stderr,
                "scroll video: adjacent frames are not a one-pixel scroll\n");
        return 1;
    }
    /* Exercise the real live camera across at least one 16-pixel coarse
     * pointer wrap.  Vertical terrain motion continues simultaneously, so
     * compare both horizontal one-pixel candidates with the known Y shift. */
    bs_recomp_enable_live_input(machine, 1);
    bs_recomp_set_input(machine, 0, BS_INPUT_RIGHT);
    unsigned camera_steps = 0;
    unsigned coarse_wraps = 0;
    uint16_t previous_camera = bs_recomp_read16(
        machine, machine->cpu.a[5] + 7204);
    uint16_t previous_coarse =
        ((uint16_t)(previous_camera - 0x100) >> 3) & 0xfffe;
    memcpy(machine->memory + 0x62000, clean_playfield, 0x1e000);
    source = video_source(machine);
    memcpy(before, ocs_video_render(video, &source),
           OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT * sizeof *before);
    for (unsigned frame = 0; frame < 240 && coarse_wraps == 0; frame++) {
        if (!run_game_frame(machine, clean_playfield)) {
            fprintf(stderr, "scroll video: horizontal frame failed: %s\n",
                    machine->error);
            return 1;
        }
        memcpy(machine->memory + 0x62000, clean_playfield, 0x1e000);
        source = video_source(machine);
        after = ocs_video_render(video, &source);
        uint16_t camera = bs_recomp_read16(machine,
                                            machine->cpu.a[5] + 7204);
        uint16_t coarse =
            ((uint16_t)(camera - 0x100) >> 3) & 0xfffe;
        if (camera != previous_camera) {
            size_t no_x = shifted_matches_xy(before, after, 0, -1);
            size_t left = shifted_matches_xy(before, after, 1, -1);
            size_t right = shifted_matches_xy(before, after, -1, -1);
            size_t horizontal = left > right ? left : right;
            if (horizontal <= no_x || horizontal * 100 < samples * 75) {
                fprintf(stderr, "scroll video: camera $%04x->$%04x moved "
                        "by a coarse column\n", previous_camera, camera);
                return 1;
            }
            camera_steps++;
            if (coarse != previous_coarse) coarse_wraps++;
            previous_camera = camera;
            previous_coarse = coarse;
        }
        memcpy(before, after,
               OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT * sizeof *before);
    }
    bs_recomp_set_input(machine, 0, 0);
    if (!camera_steps || !coarse_wraps) {
        fprintf(stderr, "scroll video: live camera did not cross a coarse "
                "word boundary\n");
        return 1;
    }
    printf("scroll video: horizontal fine scroll PASS (%u pixel steps, "
           "%u coarse wrap)\n", camera_steps, coarse_wraps);
    memcpy(machine->memory + 0x62000, clean_playfield, 0x1e000);
    unsigned transient_frames = 0;
    for (unsigned frame = 0; frame < 3000; frame++) {
        if (!run_game_frame(machine, clean_playfield)) {
            fprintf(stderr, "scroll video: long frame %u failed at $%06x: %s\n",
                    frame, machine->cpu.pc, machine->error);
            return 1;
        }
        if (memcmp(machine->memory + 0x62000, clean_playfield, 0x1e000))
            transient_frames++;
        memcpy(machine->memory + 0x62000, clean_playfield, 0x1e000);
        if (memcmp(machine->memory + 0x62000, clean_playfield, 0x1e000)) {
            fprintf(stderr, "scroll video: transient pixels survived frame %u\n",
                    frame);
            return 1;
        }
    }
    if (!transient_frames) {
        fprintf(stderr, "scroll video: long run exercised no BOB draws\n");
        return 1;
    }
    printf("scroll video: PASS (one-pixel continuity, %u/3000 transient "
           "BOB frames restored)\n", transient_frames);
    free(before);
    free(clean_playfield);
    ocs_video_destroy(video);
    free(machine);
    return 0;
}
