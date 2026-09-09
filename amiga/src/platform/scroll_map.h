#ifndef AMIGA_RECOMP_SCROLL_MAP_H
#define AMIGA_RECOMP_SCROLL_MAP_H

#include <stddef.h>
#include <stdint.h>

#define SCROLL_MAP_TITLE_SIZE 64
#define SCROLL_MAP_PALETTE_SIZE 256

enum {
    SCROLL_MAP_AXIS_VERTICAL = 0,
    SCROLL_MAP_AXIS_HORIZONTAL = 1,
    SCROLL_MAP_SOURCE_MATCH = 1u << 0
};

typedef struct {
    uint64_t tick;
    uint32_t source_address;
    uint32_t ring_address;
    int32_t primary_position;
    int32_t cross_position;
    int32_t cross_coarse_position;
    uint16_t phase;
    uint16_t flags;
} ScrollMapRecord;

typedef struct {
    char title[SCROLL_MAP_TITLE_SIZE];
    uint16_t line_pixels;
    uint16_t viewport_pixels;
    uint16_t viewport_lines;
    uint8_t axis;
    int8_t primary_step;
    int16_t ring_step;
    uint32_t ring_first;
    uint32_t ring_span;
    uint16_t phase_count;
    int16_t phase_step;
    uint32_t palette[SCROLL_MAP_PALETTE_SIZE];
    ScrollMapRecord *records;
    uint8_t *lines;
    /* Palette index per source pixel, or $FF for transparent. */
    uint8_t *overlay_lines;
    size_t frame_count;
    size_t capacity;
} ScrollMapTrace;

typedef struct {
    size_t primary_discontinuities;
    size_t ring_discontinuities;
    size_t cross_discontinuities;
    size_t phase_discontinuities;
    size_t source_mismatches;
} ScrollMapValidation;

void scroll_map_init(ScrollMapTrace *trace, const char *title,
                     uint16_t line_pixels, uint16_t viewport_pixels,
                     uint16_t viewport_lines, uint8_t axis);
void scroll_map_free(ScrollMapTrace *trace);
int scroll_map_append(ScrollMapTrace *trace, const ScrollMapRecord *record,
                      const uint8_t *line);
int scroll_map_set_overlay(ScrollMapTrace *trace, size_t frame,
                           const uint8_t *overlay_line);
ScrollMapValidation scroll_map_validate(const ScrollMapTrace *trace);

/* Renders the reconstructed map-only viewport at a recorded frame.  When
 * coarse_only is non-zero the deliberately incomplete, word-stepped cross
 * scroll is used.  This makes missing fine scroll immediately visible. */
int scroll_map_render(const ScrollMapTrace *trace, size_t frame,
                      int coarse_only, int show_overlay, uint32_t *rgba);
int scroll_map_render_offset(const ScrollMapTrace *trace, size_t frame,
                             int coarse_only, int show_overlay,
                             int32_t cross_offset, uint32_t *rgba);

int scroll_map_save(const ScrollMapTrace *trace, const char *path);
int scroll_map_load(ScrollMapTrace *trace, const char *path);
int scroll_map_export_ppm(const ScrollMapTrace *trace, const char *path);

#endif
