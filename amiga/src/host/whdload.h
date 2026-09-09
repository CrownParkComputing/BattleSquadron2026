#ifndef WHDLOAD_HOST_H
#define WHDLOAD_HOST_H

#include <stdbool.h>
#include <stdint.h>

/* Booting a WHDLoad title that has no self-contained LOADER.
 *
 * Battle Squadron ships a LOADER that runs from $100 with nothing underneath
 * it, so its host just reads that file in and sets PC.  Hybris does not: its
 * entry point is the WHDLoad SLAVE, which WHDLoad calls with A0 pointing at a
 * jump table of resload functions and which then pulls the game in itself.
 *
 * So the host has to be WHDLoad for the duration: park the slave somewhere the
 * game will not tread, publish a resload table, and service the calls the slave
 * makes.  Each table entry is a distinct address that the instruction hook
 * recognises, performs, and returns from -- no 68000 stub code required.
 */

/* The slave sits at the top of base memory.  WHDLoad would place it outside
 * the game's allocation entirely; with a 512K chip array the top is the least
 * likely place for a 1989 title to reach. */
#define WHD_SLAVE_BASE   0x00078000u
#define WHD_RESLOAD_BASE 0x00077000u
#define WHD_RESLOAD_SIZE 0x00000100u
/* WHDLoad allocates ws_ExpMem bytes and writes the ADDRESS back into the
 * slave header before entering it.  Hybris asks for $1000 and builds its
 * stack at ExpMem+$E00; passing the size through unchanged put the stack at
 * $1E00, which the 64K main.pal load at $380 then wiped out. */
#define WHD_EXPMEM_BASE  0x00076000u

/* resload jump-table offsets actually used by the Hybris slave, confirmed by
 * disassembling its entry at $150: $34 first, then $1C, $18, $6C and $64. */
#define WHD_INSTALL            0x00
#define WHD_ABORT              0x04
#define WHD_LOADFILE           0x08
#define WHD_SAVEFILE           0x0c
#define WHD_SETCACR            0x10
#define WHD_LISTFILES          0x14
#define WHD_DECRUNCH           0x18
#define WHD_LOADFILEDECRUNCH   0x1c
#define WHD_FLUSHCACHE         0x20
#define WHD_GETFILESIZE        0x24
#define WHD_DISKLOAD           0x28
#define WHD_DISKLOADDEV        0x2c
#define WHD_CRC16              0x30
#define WHD_CONTROL            0x34
#define WHD_SAVEFILEOFFSET     0x38
#define WHD_PATCH              0x64
#define WHD_EXAMINE            0x6c

/* Load the slave, publish the resload table and prepare the CPU to enter it.
 * `data_dir` is the WHDLoad install's data/ directory that file requests are
 * resolved against. */
bool whdload_boot(const char *slave_path, const char *data_dir);

/* Called from the instruction hook.  Returns true when `pc` landed inside the
 * resload table, in which case the call has been serviced and the CPU has been
 * returned to the caller. */
bool whdload_trap(uint32_t pc);

/* True once whdload_boot has run, so the host's title-specific hooks can stand
 * aside. */
bool whdload_active(void);

/* Diagnostics: how many resload calls have been serviced, and the name of the
 * last file requested. */
extern long whd_call_count;
extern long whd_file_count;

#endif
