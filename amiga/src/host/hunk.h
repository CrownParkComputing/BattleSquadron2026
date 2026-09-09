#ifndef BATTLE_SQUADRON_HUNK_H
#define BATTLE_SQUADRON_HUNK_H

#include <stdbool.h>
#include <stdint.h>

/* Minimal AmigaDOS hunk loader: enough to start a disk game's executable the
 * way the CLI would, so a cracked ADF boots without a WHDLoad slave.
 * Supports HUNK_CODE/DATA/BSS and HUNK_RELOC32, which is everything a 1988
 * game loader is built from. */

typedef struct {
    uint32_t base[16];        /* where each hunk landed */
    uint32_t size[16];
    int      hunks;
    uint32_t entry;           /* first hunk: where execution starts */
    uint32_t end;             /* first free address after the last hunk */
} HunkImage;

/* Load `path` into `memory` (a flat address space of `memory_size` bytes)
 * starting at `load_at`.  Returns false and prints why on failure. */
bool hunk_load(const char *path, uint8_t *memory, uint32_t memory_size,
               uint32_t load_at, HunkImage *out);

#endif
