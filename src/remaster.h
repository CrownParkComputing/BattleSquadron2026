#ifndef BS_REMASTER_H
#define BS_REMASTER_H
#include <stdint.h>
/* Atlas slots exported from the unchanged first 32 map rows encountered in play.
 * The map word is an identity, not an index to rewrite in the game data. */
static const uint16_t opening_tile_ids[25] = {
    640, 720, 800, 880, 960, 2640, 31680, 31760, 31840, 32960,
    33280, 33360, 33440, 34880, 34960, 35040, 36480, 36560, 36640,
    38080, 38160, 38240, 39680, 39760, 39840
};
static inline int opening_tile_index(uint16_t word) {
    for (int i = 0; i < 25; i++) if (opening_tile_ids[i] == word) return i;
    return -1;
}
#endif
