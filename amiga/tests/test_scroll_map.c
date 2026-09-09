#include "scroll_map.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { if (!(condition)) { \
    fprintf(stderr, "scroll-map test: %s\n", message); return 1; \
} } while (0)

int main(void)
{
    ScrollMapTrace trace;
    scroll_map_init(&trace, "synthetic wrapping map", 32, 16, 8,
                    SCROLL_MAP_AXIS_VERTICAL);
    trace.ring_first = 0x100;
    trace.ring_span = 0x20;
    trace.ring_step = -4;
    trace.phase_count = 8;
    trace.phase_step = -1;
    for (unsigned index = 0; index < SCROLL_MAP_PALETTE_SIZE; index++)
        trace.palette[index] = UINT32_C(0xff000000) | index;
    for (unsigned frame = 0; frame < 20; frame++) {
        uint8_t line[32];
        for (unsigned x = 0; x < sizeof line; x++)
            line[x] = (uint8_t)((frame + x) & 31);
        ScrollMapRecord record = {
            .tick = frame * 2,
            .source_address = 0x4000 + frame * 32,
            .ring_address = 0x11c - (frame & 7) * 4,
            .primary_position = 100 + (int32_t)frame,
            .cross_position = (int32_t)frame,
            .cross_coarse_position = (int32_t)(frame / 4) * 4,
            .phase = (uint16_t)((7 - frame) & 7),
            .flags = SCROLL_MAP_SOURCE_MATCH
        };
        CHECK(scroll_map_append(&trace, &record, line), "append failed");
    }
    ScrollMapValidation validation = scroll_map_validate(&trace);
    CHECK(!validation.primary_discontinuities &&
          !validation.ring_discontinuities &&
          !validation.cross_discontinuities &&
          !validation.phase_discontinuities &&
          !validation.source_mismatches,
          "valid pixel-stepped trace was rejected");

    uint32_t image[16 * 8];
    uint8_t overlay[32];
    memset(overlay, 0xff, sizeof overlay);
    overlay[10] = 31;
    CHECK(scroll_map_set_overlay(&trace, 10, overlay),
          "overlay append failed");
    CHECK(scroll_map_render(&trace, 10, 0, 0, image),
          "smooth render failed");
    CHECK(image[0] == trace.palette[20] &&
          image[16] == trace.palette[19],
          "smooth reconstruction used the wrong rows or fine scroll");
    CHECK(scroll_map_render_offset(&trace, 10, 0, 0, 3, image) &&
          image[0] == trace.palette[23],
          "manual pixel cross-scroll offset was not applied");
    CHECK(scroll_map_render(&trace, 10, 0, 1, image) &&
          image[0] == trace.palette[31],
          "transparent object layer was not composed");
    CHECK(scroll_map_render(&trace, 10, 1, 1, image),
          "coarse-only render failed");
    CHECK(image[0] == trace.palette[18],
          "coarse-only diagnostic did not expose word stepping");

    CHECK(scroll_map_save(&trace, "build/test_scroll_map.rmap"),
          "trace save failed");
    ScrollMapTrace loaded;
    scroll_map_init(&loaded, NULL, 0, 0, 0, 0);
    CHECK(scroll_map_load(&loaded, "build/test_scroll_map.rmap"),
          "trace load failed");
    CHECK(loaded.frame_count == trace.frame_count &&
          loaded.line_pixels == trace.line_pixels &&
          !strcmp(loaded.title, trace.title) &&
          loaded.records[0].tick == trace.records[0].tick &&
          loaded.records[10].cross_position ==
              trace.records[10].cross_position &&
          loaded.records[19].ring_address ==
              trace.records[19].ring_address &&
          !memcmp(loaded.lines, trace.lines,
                  trace.frame_count * trace.line_pixels) &&
          !memcmp(loaded.overlay_lines, trace.overlay_lines,
                  trace.frame_count * trace.line_pixels),
          "saved trace did not round-trip exactly");

    loaded.records[9].ring_address++;
    loaded.records[12].flags = 0;
    validation = scroll_map_validate(&loaded);
    CHECK(validation.ring_discontinuities &&
          validation.source_mismatches == 1,
          "validator missed a ring seam/source corruption");
    scroll_map_free(&loaded);
    scroll_map_free(&trace);
    puts("reusable scroll-map playback: PASS (fine/coarse, wrap, save/load)");
    return 0;
}
