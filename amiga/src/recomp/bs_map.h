#ifndef BATTLE_SQUADRON_RECOMP_MAP_H
#define BATTLE_SQUADRON_RECOMP_MAP_H

#include "runtime.h"
#include "scroll_map.h"

#include <stdint.h>

#define BS_MAP_LINE_PIXELS 384
#define BS_MAP_VIEW_PIXELS 320
#define BS_MAP_VIEW_LINES 256

typedef struct {
    uint16_t camera_x;
    uint16_t progress;
    uint32_t playfield;
    uint16_t tile_phase_bytes;
    uint32_t map_address;
    uint16_t ring_row;
    uint16_t scroll_active;
    uint16_t mode;
    uint16_t fine_x;
    uint16_t coarse_bytes;
    int32_t effective_x;
} BsMapState;

void bs_map_get_state(const BsRecomp *machine, BsMapState *state);
void bs_map_configure_trace(ScrollMapTrace *trace,
                            const BsRecomp *machine);
void bs_map_update_palette(ScrollMapTrace *trace,
                           const BsRecomp *machine);
void bs_map_decode_source_line(const BsRecomp *machine,
                               const BsMapState *state,
                               uint8_t pixels[BS_MAP_LINE_PIXELS]);
void bs_map_decode_ring_line(const BsRecomp *machine,
                             const BsMapState *state,
                             uint8_t pixels[BS_MAP_LINE_PIXELS]);

/* Captures the terrain row selected by the real map stream and verifies it
 * against the native runtime's circular-playfield write. */
int bs_map_append_frame(ScrollMapTrace *trace, const BsRecomp *machine,
                        uint64_t tick);
/* Samples the top scanline after the native scenery-object render pass and
 * stores only pixels which differ from the verified terrain layer. */
int bs_map_capture_object_line(ScrollMapTrace *trace, size_t frame,
                               const BsRecomp *machine);

#endif
