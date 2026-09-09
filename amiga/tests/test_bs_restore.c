#include "runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t fnv1a(const uint8_t *data, size_t length)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

int main(void)
{
    BsRecomp *machine = malloc(sizeof *machine);
    if (!machine || bs_recomp_init(machine,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        return 1;
    unsigned hits = 0;
    for (long guard = 0; guard < 200000 && hits < 300; guard++) {
        if (machine->cpu.pc == 0xb6e) hits++;
        if (hits == 300) break;
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) {
            fprintf(stderr, "restore parity stopped: %s\n", machine->error);
            return 1;
        }
    }
    uint32_t hash = fnv1a(machine->memory + 0x62000, 0x1e000);
    printf("restore parity: hit=%u steps=%ld ring=%08x pc=$%06x "
           "old=$%06x row=$%04x list=$%06x progress=$%04x "
           "d0=%08x d1=%08x d2=%08x d3=%08x d4=%08x a4=%08x\n",
           hits, machine->translated_steps, hash, machine->cpu.pc,
           bs_recomp_read32(machine, 0x5bce),
           bs_recomp_read16(machine, 0x5bcc),
           bs_recomp_read32(machine, 0x78fc),
           bs_recomp_read16(machine, 0x9c26),
           machine->cpu.d[0], machine->cpu.d[1], machine->cpu.d[2],
           machine->cpu.d[3], machine->cpu.d[4], machine->cpu.a[4]);
    free(machine);
    return hits == 300 ? 0 : 1;
}
