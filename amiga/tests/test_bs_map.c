#include "bs_map.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "Battle Squadron map test: %s\n", message); failed = 1; \
    goto done; \
} } while (0)

int main(void)
{
    int failed = 0;
    BsRecomp *machine = malloc(sizeof *machine);
    ScrollMapTrace trace;
    scroll_map_init(&trace, NULL, 0, 0, 0, 0);
    CHECK(machine != NULL, "machine allocation failed");
    CHECK(bs_recomp_init(machine, "original/whdload/BattleSquadron/data") ==
          BS_RECOMP_OK, "native runtime init failed");
    for (unsigned guard = 0; machine->cpu.pc != 0x7d0 && guard < 1000;
         guard++)
        CHECK(bs_recomp_run(machine, 1) == BS_RECOMP_OK,
              "native runtime failed before map stream");
    CHECK(machine->cpu.pc == 0x7d0, "first terrain update was not reached");
    bs_map_configure_trace(&trace, machine);

    size_t pending_object_frame = SIZE_MAX;
    size_t object_pixels = 0;
    for (unsigned guard = 0;
         (trace.frame_count < 300 || pending_object_frame != SIZE_MAX) &&
         guard < 20000;
         guard++) {
        uint32_t pc = machine->cpu.pc;
        BsMapState before, after;
        bs_map_get_state(machine, &before);
        CHECK(bs_recomp_run(machine, 1) == BS_RECOMP_OK,
              "native runtime failed during map capture");
        if (pc == 0xb6e && pending_object_frame != SIZE_MAX) {
            int changed = bs_map_capture_object_line(
                &trace, pending_object_frame, machine);
            CHECK(changed >= 0, "object-layer capture failed");
            object_pixels += (size_t)changed;
            pending_object_frame = SIZE_MAX;
        }
        if (pc != 0x7d0 && pc != 0xb34) continue;
        bs_map_get_state(machine, &after);
        if (after.progress == before.progress) continue;
        if (trace.frame_count >= 300) continue;
        CHECK(bs_map_append_frame(&trace, machine,
                                  machine->translated_steps) == 1,
              "map source differs from circular playfield row");
        if (pc == 0xb34) pending_object_frame = trace.frame_count - 1;
    }
    CHECK(trace.frame_count == 300, "did not capture 300 terrain rows");
    bs_map_update_palette(&trace, machine);
    CHECK(trace.palette[0] == UINT32_C(0xff000000) &&
          trace.palette[1] != UINT32_C(0xff000055) &&
          trace.palette[2] != UINT32_C(0xff0000aa),
          "terrain palette fell back instead of using live Copper RGB4");
    ScrollMapValidation validation = scroll_map_validate(&trace);
    CHECK(!validation.primary_discontinuities &&
          !validation.ring_discontinuities &&
          !validation.cross_discontinuities &&
          !validation.phase_discontinuities &&
          !validation.source_mismatches,
          "terrain stream is not pixel-smooth or ring-exact");
    CHECK(object_pixels > 0,
          "native scenery renderer produced no separate object pixels");
    CHECK(trace.records[256].ring_address == trace.records[0].ring_address,
          "256-line circular playfield did not wrap exactly");
    CHECK(scroll_map_save(&trace, "build/test_battle_squadron_map.rmap"),
          "real terrain trace could not be saved");
    printf("Battle Squadron map: PASS (%zu exact rows, %zu object pixels, "
           "ring=$%05x, map=$%05x, x=%d/%d)\n", trace.frame_count,
           object_pixels,
           trace.records[trace.frame_count - 1].ring_address,
           trace.records[trace.frame_count - 1].source_address,
           trace.records[trace.frame_count - 1].cross_position,
           trace.records[trace.frame_count - 1].cross_coarse_position);

done:
    scroll_map_free(&trace);
    free(machine);
    return failed;
}
