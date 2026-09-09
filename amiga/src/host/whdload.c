#include "amiga.h"
#include "whdload.h"
#include "m68k.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

long whd_call_count;
long whd_file_count;

static bool active;
static char data_path[512];

bool whdload_active(void) { return active; }

static uint16_t rd16(uint32_t address)
{
    return (uint16_t)((chip[address & (CHIP_SIZE - 1)] << 8) |
                      chip[(address + 1) & (CHIP_SIZE - 1)]);
}

static uint32_t rd32(uint32_t address)
{
    return ((uint32_t)rd16(address) << 16) | rd16(address + 2);
}

static void wr32(uint32_t address, uint32_t value)
{
    chip[address & (CHIP_SIZE - 1)] = (uint8_t)(value >> 24);
    chip[(address + 1) & (CHIP_SIZE - 1)] = (uint8_t)(value >> 16);
    chip[(address + 2) & (CHIP_SIZE - 1)] = (uint8_t)(value >> 8);
    chip[(address + 3) & (CHIP_SIZE - 1)] = (uint8_t)value;
}

static void wr16(uint32_t address, uint16_t value)
{
    chip[address & (CHIP_SIZE - 1)] = (uint8_t)(value >> 8);
    chip[(address + 1) & (CHIP_SIZE - 1)] = (uint8_t)value;
}

/* Read a NUL-terminated Amiga string out of emulated memory. */
static void read_string(uint32_t address, char *out, size_t limit)
{
    size_t i = 0;
    while (i + 1 < limit) {
        uint8_t c = chip[(address + i) & (CHIP_SIZE - 1)];
        if (!c) break;
        out[i++] = (char)c;
    }
    out[i] = 0;
}

/* resload_LoadFile / resload_LoadFileDecrunch.  The Hybris data files carry no
 * recognised packer signature (no RNC, IMP! or PP20 magic), so a plain read is
 * the decrunched form; if a title ever needs real depacking it belongs here. */
static uint32_t load_file(uint32_t name_address, uint32_t destination)
{
    char name[256];
    read_string(name_address, name, sizeof name);
    char path[800];
    snprintf(path, sizeof path, "%s/%s", data_path, name);
    FILE *file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "whdload: MISSING FILE '%s' (%s)\n", name, path);
        return 0;
    }
    uint32_t total = 0;
    int c;
    while ((c = fgetc(file)) != EOF) {
        chip[(destination + total) & (CHIP_SIZE - 1)] = (uint8_t)c;
        total++;
    }
    fclose(file);
    whd_file_count++;
    fprintf(stderr, "whdload: load '%s' -> $%06x (%u bytes)\n",
            name, destination, total);
    return total;
}

/* resload_Patch: a list of word commands.  Only the forms the Hybris slave
 * actually emits are handled; anything else is reported rather than guessed
 * at, so an unknown command shows up as itself instead of corrupting memory. */
static void apply_patch_list(uint32_t list, uint32_t destination)
{
    if (getenv("WHD_DUMP_PATCH")) {
        fprintf(stderr, "whdload: patch list at $%06x (dest $%06x):\n  ",
                list, destination);
        for (int i = 0; i < 64; i++)
            fprintf(stderr, "%02x%s", chip[(list + i) & (CHIP_SIZE - 1)],
                    (i % 16 == 15) ? "\n  " : " ");
        fprintf(stderr, "\n");
        return;
    }
    for (int guard = 0; guard < 4096; guard++) {
        uint16_t command = rd16(list);
        if (!command) return;
        uint16_t offset = rd16(list + 2);
        uint16_t value = rd16(list + 4);
        switch (command) {
        case 0x8002:                       /* patch a word */
        case 0x8006:
        case 0x8007:
            wr16(destination + offset, value);
            break;
        default:
            fprintf(stderr, "whdload: unhandled patch command $%04x "
                    "(offset $%04x value $%04x)\n", command, offset, value);
            break;
        }
        list += 6;
    }
}

bool whdload_trap(uint32_t pc)
{
    if (!active) return false;
    if (pc < WHD_RESLOAD_BASE || pc >= WHD_RESLOAD_BASE + WHD_RESLOAD_SIZE)
        return false;

    uint32_t offset = pc - WHD_RESLOAD_BASE;
    uint32_t a0 = m68k_get_reg(NULL, M68K_REG_A0);
    uint32_t a1 = m68k_get_reg(NULL, M68K_REG_A1);
    uint32_t result = 0;
    whd_call_count++;

    switch (offset) {
    case WHD_LOADFILE:
    case WHD_LOADFILEDECRUNCH:
        result = load_file(a0, a1);
        break;
    case WHD_GETFILESIZE: {
        char name[256];
        read_string(a0, name, sizeof name);
        char path[800];
        snprintf(path, sizeof path, "%s/%s", data_path, name);
        FILE *file = fopen(path, "rb");
        if (file) {
            fseek(file, 0, SEEK_END);
            result = (uint32_t)ftell(file);
            fclose(file);
        }
        break;
    }
    case WHD_DECRUNCH:
        /* Source already plain: copy is the identity transform.  Length is
         * unknown here, so report it rather than guess at a size. */
        fprintf(stderr, "whdload: Decrunch $%06x -> $%06x (treated as plain)\n",
                a0, a1);
        break;
    case WHD_CRC16: {
        /* resload_CRC16(a0 = data, d0 = length): the slave CRCs main.pal and
         * compares against $94DE or $AC8B to tell the data versions apart. */
        uint32_t length = m68k_get_reg(NULL, M68K_REG_D0);
        /* WHDLoad's resload_CRC16 is CRC-16/ARC: reflected, polynomial
         * $A001, initial value ZERO.  Starting from $FFFF gave $65BE where
         * the Hybris slave wanted $AC8B, and it aborted on the version check. */
        uint16_t crc = 0x0000;
        for (uint32_t i = 0; i < length; i++) {
            crc ^= chip[(a0 + i) & (CHIP_SIZE - 1)];
            for (int bit = 0; bit < 8; bit++)
                crc = (crc & 1) ? (uint16_t)((crc >> 1) ^ 0xa001)
                                : (uint16_t)(crc >> 1);
        }
        result = crc;
        fprintf(stderr, "whdload: CRC16 $%06x len %u -> $%04x\n",
                a0, length, crc);
        break;
    }
    case WHD_PATCH:
        apply_patch_list(a0, a1);
        break;
    case WHD_CONTROL:
    case WHD_SETCACR:
    case WHD_FLUSHCACHE:
    case WHD_INSTALL:
        break;                              /* nothing to do on a host */
    case WHD_ABORT:
        fprintf(stderr, "whdload: slave called resload_Abort\n");
        amiga_stop();
        break;
    default:
        fprintf(stderr, "whdload: UNIMPLEMENTED resload offset $%02x "
                "(a0=$%06x a1=$%06x)\n", offset, a0, a1);
        break;
    }

    /* Return to the caller: pop the return address the JSR pushed. */
    uint32_t sp = m68k_get_reg(NULL, M68K_REG_A7);
    uint32_t ret = rd32(sp);
    m68k_set_reg(M68K_REG_A7, sp + 4);
    m68k_set_reg(M68K_REG_PC, ret);
    m68k_set_reg(M68K_REG_D0, result);
    return true;
}

bool whdload_boot(const char *slave_path, const char *data_dir)
{
    snprintf(data_path, sizeof data_path, "%s", data_dir);

    FILE *file = fopen(slave_path, "rb");
    if (!file) {
        perror(slave_path);
        return false;
    }
    uint32_t size = 0;
    int c;
    while ((c = fgetc(file)) != EOF && WHD_SLAVE_BASE + size < CHIP_SIZE)
        chip[WHD_SLAVE_BASE + size++] = (uint8_t)c;
    fclose(file);
    fprintf(stderr, "whdload: slave %u bytes at $%06x\n", size,
            WHD_SLAVE_BASE);

    /* The resload table is pure marker: every entry is an address the
     * instruction hook recognises.  Fill it with NOPs so a stray fall-through
     * is harmless and obvious rather than executing whatever was there. */
    for (uint32_t i = 0; i < WHD_RESLOAD_SIZE; i += 2)
        wr16(WHD_RESLOAD_BASE + i, 0x4e71);

    /* Slave header: $70FF4E75 security stub, "WHDLOADS", then version at $0C,
     * flags $0E, BaseMemSize $10, ExecInstall $14, GameLoader $18 (the entry
     * offset, a WORD) and CurrentDir $1A. */
    uint32_t entry = WHD_SLAVE_BASE + rd16(WHD_SLAVE_BASE + 0x18);
    fprintf(stderr, "whdload: version %u flags $%04x basemem $%08x "
            "currentdir $%04x\n", rd16(WHD_SLAVE_BASE + 0x0c),
            rd16(WHD_SLAVE_BASE + 0x0e), rd32(WHD_SLAVE_BASE + 0x10),
            rd16(WHD_SLAVE_BASE + 0x1a));
    if (rd16(WHD_SLAVE_BASE) != 0x70ff) {
        fprintf(stderr, "whdload: not a slave (magic $%04x)\n",
                rd16(WHD_SLAVE_BASE));
        return false;
    }
    fprintf(stderr, "whdload: entry $%06x, resload $%06x\n",
            entry, WHD_RESLOAD_BASE);

    /* Hand back the ADDRESS of the expansion memory, as WHDLoad does; the
     * slave reads it from $20 and builds its stack there. */
    uint32_t expmem_size = rd32(WHD_SLAVE_BASE + 0x20);
    wr32(WHD_SLAVE_BASE + 0x20, WHD_EXPMEM_BASE);
    memset(chip + WHD_EXPMEM_BASE, 0, expmem_size ? expmem_size : 0x1000);
    fprintf(stderr, "whdload: expmem %u bytes at $%06x\n",
            expmem_size, WHD_EXPMEM_BASE);

    m68k_set_reg(M68K_REG_A0, WHD_RESLOAD_BASE);
    m68k_set_reg(M68K_REG_A7, WHD_EXPMEM_BASE + 0xe00);
    m68k_set_reg(M68K_REG_PC, entry);
    active = true;
    return true;
}
