#ifndef AMIGA_RECOMP_OCS_PALETTE_H
#define AMIGA_RECOMP_OCS_PALETTE_H

#include "ocs_video.h"

#include <stdint.h>

/* Replays Copper MOVE/WAIT instructions through the requested PAL raster
 * line and returns the live 32-colour RGB4 palette.  The return value is the
 * number of colour-register writes observed. */
int ocs_palette_at_line(const OcsVideoSource *source, int raster_line,
                        uint16_t rgb4[32]);
uint32_t ocs_rgb4_rgba(uint16_t value);

#endif
