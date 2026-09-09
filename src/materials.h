#ifndef BS_MATERIALS_H
#define BS_MATERIALS_H
#include "remaster.h"
/* Original palette indices, never sprite colours. Class 0 remains unchanged. */
enum { MAT_ORIGINAL, MAT_ROCK, MAT_CLOUD, MAT_LAVA, MAT_STARS, MAT_METAL };
static inline int terrain_material(int stage, uint16_t tile, int colour, int mechanical) {
    int purple = colour >= 8 && colour <= 12;
    int rock = colour == 1 || colour == 6 || colour == 15 || colour == 16 ||
               colour == 20 || colour == 28 || (stage == 0 && colour == 13);
    if (stage == 0) {
        int slot = opening_tile_index(tile);
        if (slot >= 0 && slot < 5) return MAT_STARS;
        if (slot >= 6 && (purple || colour == 0 || (colour >= 25 && colour != 28)))
            return MAT_CLOUD;
    }
    if (stage == 1 && !mechanical && (purple || colour == 0))
        return MAT_CLOUD;
    if (stage == 2 && (mechanical == 2 || tile == 20480 || tile == 20560 || tile == 22080 || tile == 22160))
        return MAT_CLOUD;
    if (stage == 2 && !mechanical && purple) return MAT_CLOUD;
    if (stage == 3 && purple) return MAT_CLOUD;
    if ((stage == 1 || stage == 3) && (colour == 22 || colour == 23)) return MAT_LAVA;
    if (rock) return MAT_ROCK;
    if (purple || colour == 2 || colour >= 25) return MAT_METAL;
    return MAT_ORIGINAL;
}
#endif
