#ifndef BATTLE_SQUADRON_EXEBOOT_H
#define BATTLE_SQUADRON_EXEBOOT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t entry;
    uint32_t end;
    int      hunks;
} ExeBoot;

/* Load an AmigaDOS hunk executable and start the 68000 on it, the way the
 * CLI would.  No disk of any kind is involved: a title booted this way gets
 * its data from files through a title-specific hook. */
bool exeboot(const char *exe_path, ExeBoot *out);

#endif
