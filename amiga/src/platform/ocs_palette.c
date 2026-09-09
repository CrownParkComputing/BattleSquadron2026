#include "ocs_palette.h"

#include <string.h>

typedef struct {
    OcsVideoSource source;
    uint32_t pc;
    int wait_line;
    uint16_t color[32];
    int writes;
} PaletteScan;

static uint8_t rb(const PaletteScan *scan, uint32_t address)
{
    return scan->source.read8(scan->source.user,
                              address & scan->source.chip_mask);
}

static uint16_t rw(const PaletteScan *scan, uint32_t address)
{
    return ((uint16_t)rb(scan, address) << 8) | rb(scan, address + 1);
}

static void run_line(PaletteScan *scan, int line)
{
    if (scan->wait_line < 0 || line < scan->wait_line) return;
    for (int guard = 0; guard < 4096; guard++) {
        uint16_t first = rw(scan, scan->pc);
        uint16_t second = rw(scan, scan->pc + 2);
        if (!(first & 1)) {
            scan->pc = (scan->pc + 4) & scan->source.chip_mask;
            uint16_t reg = first & 0x01fe;
            if (reg >= 0x180 && reg < 0x1c0) {
                scan->color[(reg - 0x180) / 2] = second & 0x0fff;
                scan->writes++;
            }
        } else if (!(second & 1)) {
            if (first == 0xffff && second == 0xfffe) {
                scan->wait_line = -1;
                return;
            }
            int target = (first >> 8) & 0xff;
            int mask = ((second >> 8) & 0x7f) | 0x80;
            if (((line & 0xff) & mask) >= (target & mask)) {
                scan->pc = (scan->pc + 4) & scan->source.chip_mask;
            } else {
                scan->wait_line = (line & 0x100) | target;
                if (scan->wait_line <= line) scan->wait_line += 0x100;
                return;
            }
        } else {
            /* Copper SKIP is not used by either current title.  Advancing it
             * matches the conservative behaviour in the OCS video module. */
            scan->pc = (scan->pc + 4) & scan->source.chip_mask;
        }
    }
    scan->wait_line = -1;
}

int ocs_palette_at_line(const OcsVideoSource *source, int raster_line,
                        uint16_t rgb4[32])
{
    if (!source || !source->read8 || !rgb4 || raster_line < 0 ||
        raster_line >= 312)
        return 0;
    PaletteScan scan;
    memset(&scan, 0, sizeof scan);
    scan.source = *source;
    scan.pc = source->copper_address & source->chip_mask;
    scan.wait_line = scan.pc ? 0 : -1;
    for (int line = 0; line <= raster_line; line++) run_line(&scan, line);
    memcpy(rgb4, scan.color, sizeof scan.color);
    return scan.writes;
}

uint32_t ocs_rgb4_rgba(uint16_t value)
{
    uint32_t red = (value >> 8) & 15;
    uint32_t green = (value >> 4) & 15;
    uint32_t blue = value & 15;
    return UINT32_C(0xff000000) | (blue * 17 << 16) |
           (green * 17 << 8) | red * 17;
}
