#include "scroll_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const uint8_t file_magic[8] = {'R','M','A','P','L','A','B','2'};

void scroll_map_init(ScrollMapTrace *trace, const char *title,
                     uint16_t line_pixels, uint16_t viewport_pixels,
                     uint16_t viewport_lines, uint8_t axis)
{
    if (!trace) return;
    memset(trace, 0, sizeof *trace);
    if (title) snprintf(trace->title, sizeof trace->title, "%s", title);
    trace->line_pixels = line_pixels;
    trace->viewport_pixels = viewport_pixels;
    trace->viewport_lines = viewport_lines;
    trace->axis = axis;
    trace->primary_step = 1;
}

void scroll_map_free(ScrollMapTrace *trace)
{
    if (!trace) return;
    free(trace->records);
    free(trace->lines);
    free(trace->overlay_lines);
    memset(trace, 0, sizeof *trace);
}

static int reserve_frames(ScrollMapTrace *trace, size_t needed)
{
    if (needed <= trace->capacity) return 1;
    size_t capacity = trace->capacity ? trace->capacity : 256;
    while (capacity < needed) {
        if (capacity > SIZE_MAX / 2) return 0;
        capacity *= 2;
    }
    if (!trace->line_pixels || capacity > SIZE_MAX / trace->line_pixels)
        return 0;
    size_t old_capacity = trace->capacity;
    ScrollMapRecord *records = realloc(trace->records,
                                       capacity * sizeof *records);
    if (!records) return 0;
    trace->records = records;
    uint8_t *lines = realloc(trace->lines,
                             capacity * trace->line_pixels);
    if (!lines) return 0;
    trace->lines = lines;
    uint8_t *overlay = realloc(trace->overlay_lines,
                               capacity * trace->line_pixels);
    if (!overlay) return 0;
    trace->overlay_lines = overlay;
    memset(trace->overlay_lines + old_capacity * trace->line_pixels, 0xff,
           (capacity - old_capacity) * trace->line_pixels);
    trace->capacity = capacity;
    return 1;
}

int scroll_map_append(ScrollMapTrace *trace, const ScrollMapRecord *record,
                      const uint8_t *line)
{
    if (!trace || !record || !line || !trace->line_pixels ||
        !reserve_frames(trace, trace->frame_count + 1))
        return 0;
    trace->records[trace->frame_count] = *record;
    memcpy(trace->lines + trace->frame_count * trace->line_pixels, line,
           trace->line_pixels);
    trace->frame_count++;
    return 1;
}

int scroll_map_set_overlay(ScrollMapTrace *trace, size_t frame,
                           const uint8_t *overlay_line)
{
    if (!trace || !overlay_line || frame >= trace->frame_count ||
        !trace->overlay_lines)
        return 0;
    memcpy(trace->overlay_lines + frame * trace->line_pixels, overlay_line,
           trace->line_pixels);
    return 1;
}

static int32_t wrapped_delta(int32_t value, int32_t step, int32_t count)
{
    int32_t result = value + step;
    if (count <= 0) return result;
    result %= count;
    if (result < 0) result += count;
    return result;
}

ScrollMapValidation scroll_map_validate(const ScrollMapTrace *trace)
{
    ScrollMapValidation result = {0};
    if (!trace) return result;
    for (size_t i = 0; i < trace->frame_count; i++) {
        const ScrollMapRecord *current = &trace->records[i];
        if (!(current->flags & SCROLL_MAP_SOURCE_MATCH))
            result.source_mismatches++;
        if (!i) continue;
        const ScrollMapRecord *previous = &trace->records[i - 1];
        if (current->primary_position !=
            previous->primary_position + trace->primary_step)
            result.primary_discontinuities++;
        int32_t cross_delta = current->cross_position -
                              previous->cross_position;
        if (cross_delta < -1 || cross_delta > 1)
            result.cross_discontinuities++;
        if (trace->ring_span && trace->ring_step) {
            int64_t relative = (int64_t)previous->ring_address -
                               trace->ring_first + trace->ring_step;
            relative %= trace->ring_span;
            if (relative < 0) relative += trace->ring_span;
            uint32_t expected = trace->ring_first + (uint32_t)relative;
            if (current->ring_address != expected)
                result.ring_discontinuities++;
        }
        if (trace->phase_count) {
            int32_t expected = wrapped_delta(previous->phase,
                                             trace->phase_step,
                                             trace->phase_count);
            if (current->phase != (uint16_t)expected)
                result.phase_discontinuities++;
        }
    }
    return result;
}

static size_t wrap_pixel(int64_t value, size_t width)
{
    value %= (int64_t)width;
    if (value < 0) value += width;
    return (size_t)value;
}

int scroll_map_render_offset(const ScrollMapTrace *trace, size_t frame,
                             int coarse_only, int show_overlay,
                             int32_t cross_offset, uint32_t *rgba)
{
    if (!trace || !rgba || !trace->frame_count ||
        !trace->viewport_pixels || !trace->viewport_lines ||
        frame >= trace->frame_count)
        return 0;
    size_t width = trace->viewport_pixels;
    size_t height = trace->viewport_lines;
    memset(rgba, 0, width * height * sizeof *rgba);
    if (trace->axis != SCROLL_MAP_AXIS_VERTICAL) return 0;
    /* Cross-scroll belongs to the current display state.  Applying every
     * historical row's old camera position shears the map diagonally while
     * the player moves sideways. */
    const ScrollMapRecord *view = &trace->records[frame];
    int32_t cross = coarse_only ? view->cross_coarse_position
                                : view->cross_position;
    cross += cross_offset;
    for (size_t y = 0; y < height && y <= frame; y++) {
        size_t record_index = frame - y;
        const uint8_t *line = trace->lines +
                              record_index * trace->line_pixels;
        const uint8_t *overlay = trace->overlay_lines +
                                 record_index * trace->line_pixels;
        for (size_t x = 0; x < width; x++) {
            size_t source_x = wrap_pixel((int64_t)x + cross,
                                         trace->line_pixels);
            uint8_t index = show_overlay && overlay[source_x] != 0xff
                          ? overlay[source_x] : line[source_x];
            rgba[y * width + x] = trace->palette[index];
        }
    }
    return 1;
}

int scroll_map_render(const ScrollMapTrace *trace, size_t frame,
                      int coarse_only, int show_overlay, uint32_t *rgba)
{
    return scroll_map_render_offset(trace, frame, coarse_only, show_overlay,
                                    0, rgba);
}

static int put_u16(FILE *file, uint16_t value)
{
    return fputc(value & 255, file) != EOF &&
           fputc(value >> 8, file) != EOF;
}

static int put_u32(FILE *file, uint32_t value)
{
    return put_u16(file, (uint16_t)value) &&
           put_u16(file, (uint16_t)(value >> 16));
}

static int put_u64(FILE *file, uint64_t value)
{
    return put_u32(file, (uint32_t)value) &&
           put_u32(file, (uint32_t)(value >> 32));
}

static int get_u16(FILE *file, uint16_t *value)
{
    int low = fgetc(file), high = fgetc(file);
    if (low == EOF || high == EOF) return 0;
    *value = (uint16_t)(low | high << 8);
    return 1;
}

static int get_u32(FILE *file, uint32_t *value)
{
    uint16_t low, high;
    if (!get_u16(file, &low) || !get_u16(file, &high)) return 0;
    *value = (uint32_t)low | (uint32_t)high << 16;
    return 1;
}

static int get_u64(FILE *file, uint64_t *value)
{
    uint32_t low, high;
    if (!get_u32(file, &low) || !get_u32(file, &high)) return 0;
    *value = (uint64_t)low | (uint64_t)high << 32;
    return 1;
}

int scroll_map_save(const ScrollMapTrace *trace, const char *path)
{
    if (!trace || !path || trace->frame_count > UINT32_MAX) return 0;
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int ok = fwrite(file_magic, 1, sizeof file_magic, file) ==
             sizeof file_magic &&
             put_u16(file, trace->line_pixels) &&
             put_u16(file, trace->viewport_pixels) &&
             put_u16(file, trace->viewport_lines) &&
             fputc(trace->axis, file) != EOF &&
             fputc((uint8_t)trace->primary_step, file) != EOF &&
             put_u16(file, (uint16_t)trace->ring_step) &&
             put_u32(file, trace->ring_first) &&
             put_u32(file, trace->ring_span) &&
             put_u16(file, trace->phase_count) &&
             put_u16(file, (uint16_t)trace->phase_step) &&
             put_u32(file, (uint32_t)trace->frame_count) &&
             fwrite(trace->title, 1, sizeof trace->title, file) ==
             sizeof trace->title;
    for (size_t i = 0; ok && i < SCROLL_MAP_PALETTE_SIZE; i++)
        ok = put_u32(file, trace->palette[i]);
    for (size_t i = 0; ok && i < trace->frame_count; i++) {
        const ScrollMapRecord *record = &trace->records[i];
        ok = put_u64(file, record->tick) &&
             put_u32(file, record->source_address) &&
             put_u32(file, record->ring_address) &&
             put_u32(file, (uint32_t)record->primary_position) &&
             put_u32(file, (uint32_t)record->cross_position) &&
             put_u32(file, (uint32_t)record->cross_coarse_position) &&
             put_u16(file, record->phase) && put_u16(file, record->flags) &&
             fwrite(trace->lines + i * trace->line_pixels, 1,
                    trace->line_pixels, file) == trace->line_pixels &&
             fwrite(trace->overlay_lines + i * trace->line_pixels, 1,
                    trace->line_pixels, file) == trace->line_pixels;
    }
    if (fclose(file) != 0) ok = 0;
    return ok;
}

int scroll_map_load(ScrollMapTrace *trace, const char *path)
{
    if (!trace || !path) return 0;
    FILE *file = fopen(path, "rb");
    if (!file) return 0;
    uint8_t magic[8];
    ScrollMapTrace loaded;
    memset(&loaded, 0, sizeof loaded);
    uint16_t ring_step, phase_step;
    uint32_t count;
    int axis = -1, primary_step = 0;
    int ok = fread(magic, 1, sizeof magic, file) == sizeof magic &&
             !memcmp(magic, file_magic, sizeof magic) &&
             get_u16(file, &loaded.line_pixels) &&
             get_u16(file, &loaded.viewport_pixels) &&
             get_u16(file, &loaded.viewport_lines) &&
             (axis = fgetc(file)) != EOF &&
             (primary_step = fgetc(file)) != EOF &&
             get_u16(file, &ring_step) &&
             get_u32(file, &loaded.ring_first) &&
             get_u32(file, &loaded.ring_span) &&
             get_u16(file, &loaded.phase_count) &&
             get_u16(file, &phase_step) && get_u32(file, &count) &&
             fread(loaded.title, 1, sizeof loaded.title, file) ==
             sizeof loaded.title;
    loaded.title[sizeof loaded.title - 1] = 0;
    loaded.axis = (uint8_t)axis;
    loaded.primary_step = (int8_t)primary_step;
    loaded.ring_step = (int16_t)ring_step;
    loaded.phase_step = (int16_t)phase_step;
    if (!loaded.line_pixels || !loaded.viewport_pixels ||
        !loaded.viewport_lines || loaded.axis > SCROLL_MAP_AXIS_HORIZONTAL ||
        count > 1000000)
        ok = 0;
    for (size_t i = 0; ok && i < SCROLL_MAP_PALETTE_SIZE; i++)
        ok = get_u32(file, &loaded.palette[i]);
    if (ok) ok = reserve_frames(&loaded, count);
    for (size_t i = 0; ok && i < count; i++) {
        ScrollMapRecord *record = &loaded.records[i];
        uint32_t primary = 0, cross = 0, coarse = 0;
        ok = get_u64(file, &record->tick) &&
             get_u32(file, &record->source_address) &&
             get_u32(file, &record->ring_address) &&
             get_u32(file, &primary) && get_u32(file, &cross) &&
             get_u32(file, &coarse) && get_u16(file, &record->phase) &&
             get_u16(file, &record->flags) &&
             fread(loaded.lines + i * loaded.line_pixels, 1,
                   loaded.line_pixels, file) == loaded.line_pixels &&
             fread(loaded.overlay_lines + i * loaded.line_pixels, 1,
                   loaded.line_pixels, file) == loaded.line_pixels;
        record->primary_position = (int32_t)primary;
        record->cross_position = (int32_t)cross;
        record->cross_coarse_position = (int32_t)coarse;
    }
    if (ok) loaded.frame_count = count;
    if (fclose(file) != 0) ok = 0;
    if (!ok) {
        scroll_map_free(&loaded);
        return 0;
    }
    scroll_map_free(trace);
    *trace = loaded;
    return 1;
}

int scroll_map_export_ppm(const ScrollMapTrace *trace, const char *path)
{
    if (!trace || !path || !trace->frame_count || !trace->line_pixels)
        return 0;
    FILE *file = fopen(path, "wb");
    if (!file) return 0;
    int ok = fprintf(file, "P6\n%u %zu\n255\n", trace->line_pixels,
                     trace->frame_count) > 0;
    for (size_t y = 0; ok && y < trace->frame_count; y++) {
        size_t frame = trace->frame_count - 1 - y;
        const uint8_t *line = trace->lines + frame * trace->line_pixels;
        const uint8_t *overlay = trace->overlay_lines +
                                 frame * trace->line_pixels;
        for (size_t x = 0; ok && x < trace->line_pixels; x++) {
            uint8_t index = overlay[x] == 0xff ? line[x] : overlay[x];
            uint32_t color = trace->palette[index];
            uint8_t rgb[3] = {(uint8_t)color, (uint8_t)(color >> 8),
                              (uint8_t)(color >> 16)};
            ok = fwrite(rgb, 1, sizeof rgb, file) == sizeof rgb;
        }
    }
    if (fclose(file) != 0) ok = 0;
    return ok;
}
