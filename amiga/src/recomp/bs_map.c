#include "bs_map.h"
#include "ocs_palette.h"

#include <string.h>

static uint8_t read_machine(void *user, uint32_t address)
{
    return bs_recomp_read8((const BsRecomp *)user, address);
}

void bs_map_get_state(const BsRecomp *machine, BsMapState *state)
{
    const uint32_t base = machine->cpu.a[5];
    memset(state, 0, sizeof *state);
    state->camera_x = bs_recomp_read16(machine, base + 7204);
    state->progress = bs_recomp_read16(machine, base + 7206);
    state->playfield = bs_recomp_read32(machine, base + 7208);
    state->tile_phase_bytes = bs_recomp_read16(machine, base + 7212);
    state->map_address = bs_recomp_read32(machine, base + 7214);
    state->ring_row = bs_recomp_read16(machine, base + 7218);
    state->scroll_active = bs_recomp_read16(machine, base + 7222);
    state->mode = bs_recomp_read16(machine, base + 7228);
    uint16_t offset = (uint16_t)(state->camera_x - 0x100);
    state->coarse_bytes = (uint16_t)((offset >> 3) & 0xfffe);
    state->fine_x = (uint16_t)(15 - state->camera_x) & 15;
    state->effective_x = (int32_t)state->coarse_bytes * 8 - state->fine_x;
}

void bs_map_update_palette(ScrollMapTrace *trace,
                           const BsRecomp *machine)
{
    uint32_t copper = ((uint32_t)machine->custom[0x080 >> 1] << 16) |
                      machine->custom[0x082 >> 1];
    OcsVideoSource source = {
        .user = (void *)machine,
        .read8 = read_machine,
        .chip_mask = BS_RECOMP_MEMORY_SIZE - 1,
        .copper_address = copper
    };
    uint16_t colors[32];
    int writes = ocs_palette_at_line(&source, 100, colors);
    if (writes >= 32) {
        for (unsigned index = 0; index < 32; index++)
            trace->palette[index] = ocs_rgb4_rgba(colors[index]);
    } else {
        /* Keep invalid/incomplete Copper lists conspicuous. */
        for (unsigned index = 0; index < 32; index++) {
            uint32_t red = ((index >> 0) & 3) * 85;
            uint32_t green = ((index >> 2) & 3) * 85;
            uint32_t blue = ((index >> 4) & 1) * 255;
            trace->palette[index] = UINT32_C(0xff000000) |
                                    blue << 16 | green << 8 | red;
        }
    }
}

void bs_map_configure_trace(ScrollMapTrace *trace,
                            const BsRecomp *machine)
{
    scroll_map_init(trace, "Battle Squadron terrain stream",
                    BS_MAP_LINE_PIXELS, BS_MAP_VIEW_PIXELS,
                    BS_MAP_VIEW_LINES, SCROLL_MAP_AXIS_VERTICAL);
    trace->primary_step = 1;
    trace->ring_first = 0x62000;
    trace->ring_span = 0x3000;
    trace->ring_step = -0x30;
    trace->phase_count = 16;
    trace->phase_step = -1;
    bs_map_update_palette(trace, machine);
}

void bs_map_decode_source_line(const BsRecomp *machine,
                               const BsMapState *state,
                               uint8_t pixels[BS_MAP_LINE_PIXELS])
{
    for (unsigned tile = 0; tile < 24; tile++) {
        uint16_t tile_index = bs_recomp_read16(machine,
                                               state->map_address + tile * 2);
        uint32_t source = 0x4a000 + state->tile_phase_bytes +
                          (uint32_t)tile_index * 2;
        uint16_t planes[5];
        for (unsigned plane = 0; plane < 5; plane++)
            planes[plane] = bs_recomp_read16(machine,
                                             source + plane * 0x20);
        for (unsigned bit = 0; bit < 16; bit++) {
            uint8_t color = 0;
            for (unsigned plane = 0; plane < 5; plane++)
                color |= ((planes[plane] >> (15 - bit)) & 1) << plane;
            pixels[tile * 16 + bit] = color;
        }
    }
}

void bs_map_decode_ring_line(const BsRecomp *machine,
                             const BsMapState *state,
                             uint8_t pixels[BS_MAP_LINE_PIXELS])
{
    for (unsigned word = 0; word < 24; word++) {
        uint16_t planes[5];
        for (unsigned plane = 0; plane < 5; plane++)
            planes[plane] = bs_recomp_read16(machine,
                state->playfield + plane * 0x6000 + word * 2);
        for (unsigned bit = 0; bit < 16; bit++) {
            uint8_t color = 0;
            for (unsigned plane = 0; plane < 5; plane++)
                color |= ((planes[plane] >> (15 - bit)) & 1) << plane;
            pixels[word * 16 + bit] = color;
        }
    }
}

int bs_map_append_frame(ScrollMapTrace *trace, const BsRecomp *machine,
                        uint64_t tick)
{
    BsMapState state;
    uint8_t source[BS_MAP_LINE_PIXELS], ring[BS_MAP_LINE_PIXELS];
    bs_map_get_state(machine, &state);
    bs_map_decode_source_line(machine, &state, source);
    bs_map_decode_ring_line(machine, &state, ring);
    int match = !memcmp(source, ring, sizeof source);
    ScrollMapRecord record = {
        .tick = tick,
        .source_address = state.map_address,
        .ring_address = state.playfield,
        .primary_position = state.progress,
        .cross_position = state.effective_x,
        .cross_coarse_position = (int32_t)state.coarse_bytes * 8,
        .phase = (uint16_t)(state.tile_phase_bytes >> 1),
        .flags = match ? SCROLL_MAP_SOURCE_MATCH : 0
    };
    if (!scroll_map_append(trace, &record, source)) return -1;
    return match;
}

int bs_map_capture_object_line(ScrollMapTrace *trace, size_t frame,
                               const BsRecomp *machine)
{
    if (!trace || frame >= trace->frame_count ||
        trace->line_pixels != BS_MAP_LINE_PIXELS)
        return -1;
    BsMapState state;
    uint8_t composite[BS_MAP_LINE_PIXELS];
    uint8_t overlay[BS_MAP_LINE_PIXELS];
    bs_map_get_state(machine, &state);
    bs_map_decode_ring_line(machine, &state, composite);
    const uint8_t *terrain = trace->lines + frame * trace->line_pixels;
    int changed = 0;
    for (unsigned x = 0; x < BS_MAP_LINE_PIXELS; x++) {
        if (composite[x] != terrain[x]) {
            overlay[x] = composite[x];
            changed++;
        } else {
            overlay[x] = 0xff;
        }
    }
    if (!scroll_map_set_overlay(trace, frame, overlay)) return -1;
    return changed;
}
