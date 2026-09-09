#include "exeboot.h"

#include "amiga.h"
#include "hunk.h"
#include "m68k.h"

#include <stdio.h>
#include <string.h>

/* Boot a disk game the way the CLI would: load its executable's hunks and
 * hand it an exec.library it can call before it takes the machine over.  The
 * data it would trackload is served from files by a title hook, so no floppy
 * is emulated and no disk image is read.
 *
 * The stub ExecBase is a field of RTS: a 1988 loader only calls Forbid (and
 * sometimes Disable/SuperState) before it stops using the OS, and every one
 * of those is a no-op once nothing else is running. */
#define STUB_LVO_BASE  0x000400
#define STUB_EXEC_BASE 0x002000
#define STUB_LOAD_AT   0x003000

static void poke16(uint32_t address, uint16_t value)
{
    chip[address] = (uint8_t)(value >> 8);
    chip[address + 1] = (uint8_t)value;
}

static void poke32(uint32_t address, uint32_t value)
{
    poke16(address, (uint16_t)(value >> 16));
    poke16(address + 2, (uint16_t)value);
}

bool exeboot(const char *exe_path, ExeBoot *out)
{
    amiga_init_bare();
    bs_loader_hooks = false;
    HunkImage image;
    if (!hunk_load(exe_path, chip, CHIP_SIZE, STUB_LOAD_AT, &image))
        return false;

    /* exec.library: every LVO below the base returns immediately with d0
     * untouched, and ExecBase itself is zeroed so a LibVersion read is 0. */
    for (uint32_t at = STUB_LVO_BASE; at < STUB_EXEC_BASE; at += 2)
        poke16(at, 0x4e75);                      /* RTS */
    memset(chip + STUB_EXEC_BASE, 0, 0x400);
    poke32(0x0004, STUB_EXEC_BASE);              /* AbsExecBase */

    /* Start it on a stack below the loader, in supervisor state, exactly
     * where the first hunk begins. */
    poke32(0x0000, STUB_LOAD_AT - 0x100);        /* SSP */
    m68k_pulse_reset();
    m68k_set_reg(M68K_REG_PC, image.entry);
    m68k_set_reg(M68K_REG_SP, STUB_LOAD_AT - 0x100);
    m68k_set_reg(M68K_REG_A6, STUB_EXEC_BASE);
    m68k_set_reg(M68K_REG_D0, 0);

    if (out) {
        out->entry = image.entry;
        out->end = image.end;
        out->hunks = image.hunks;
    }
    return true;
}
