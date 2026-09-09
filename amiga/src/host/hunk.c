#include "hunk.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HUNK_CODE    0x3e9
#define HUNK_DATA    0x3ea
#define HUNK_BSS     0x3eb
#define HUNK_RELOC32 0x3ec
#define HUNK_SYMBOL  0x3f0
#define HUNK_DEBUG   0x3f1
#define HUNK_END     0x3f2
#define HUNK_HEADER  0x3f3

typedef struct {
    const uint8_t *data;
    size_t length;
    size_t position;
    bool overrun;
} Reader;

static uint32_t read32(Reader *r)
{
    if (r->position + 4 > r->length) { r->overrun = true; return 0; }
    const uint8_t *p = r->data + r->position;
    r->position += 4;
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

static void poke32(uint8_t *memory, uint32_t address, uint32_t value)
{
    memory[address] = (uint8_t)(value >> 24);
    memory[address + 1] = (uint8_t)(value >> 16);
    memory[address + 2] = (uint8_t)(value >> 8);
    memory[address + 3] = (uint8_t)value;
}

static uint32_t peek32(const uint8_t *memory, uint32_t address)
{
    return ((uint32_t)memory[address] << 24) |
           ((uint32_t)memory[address + 1] << 16) |
           ((uint32_t)memory[address + 2] << 8) | memory[address + 3];
}

bool hunk_load(const char *path, uint8_t *memory, uint32_t memory_size,
               uint32_t load_at, HunkImage *out)
{
    FILE *file = fopen(path, "rb");
    if (!file) { perror(path); return false; }
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    rewind(file);
    uint8_t *raw = malloc((size_t)length);
    if (!raw || fread(raw, 1, (size_t)length, file) != (size_t)length) {
        fprintf(stderr, "%s: short read\n", path);
        free(raw); fclose(file); return false;
    }
    fclose(file);

    Reader reader = { raw, (size_t)length, 0, false };
    memset(out, 0, sizeof *out);

    if (read32(&reader) != HUNK_HEADER) {
        fprintf(stderr, "%s: not a HUNK_HEADER file\n", path);
        free(raw); return false;
    }
    while (read32(&reader)) { }                  /* resident library names */
    uint32_t table_size = read32(&reader);
    uint32_t first = read32(&reader);
    uint32_t last = read32(&reader);
    if (table_size > 16 || last < first || last >= 16) {
        fprintf(stderr, "%s: %u hunks is more than this loader handles\n",
                path, table_size);
        free(raw); return false;
    }
    out->hunks = (int)(last - first + 1);

    uint32_t cursor = load_at;
    for (int i = 0; i < out->hunks; i++) {
        uint32_t longs = read32(&reader) & 0x3fffffff;
        out->size[i] = longs * 4;
        out->base[i] = cursor;
        cursor += out->size[i];
        cursor = (cursor + 7) & ~7u;             /* AmigaDOS aligns to 8 */
        if (cursor > memory_size) {
            fprintf(stderr, "%s: hunk %d does not fit in %u bytes\n",
                    path, i, memory_size);
            free(raw); return false;
        }
    }
    out->entry = out->base[0];
    out->end = cursor;

    int hunk = 0;
    while (reader.position < reader.length && !reader.overrun) {
        uint32_t type = read32(&reader) & 0x3fffffff;
        switch (type) {
        case HUNK_CODE:
        case HUNK_DATA: {
            uint32_t longs = read32(&reader);
            if (reader.position + longs * 4 > reader.length) {
                fprintf(stderr, "%s: hunk %d truncated\n", path, hunk);
                free(raw); return false;
            }
            memcpy(memory + out->base[hunk], raw + reader.position, longs * 4);
            reader.position += longs * 4;
            break;
        }
        case HUNK_BSS:
            read32(&reader);
            memset(memory + out->base[hunk], 0, out->size[hunk]);
            break;
        case HUNK_RELOC32:
            for (;;) {
                uint32_t count = read32(&reader);
                if (!count) break;
                uint32_t target = read32(&reader);
                if (target >= (uint32_t)out->hunks) {
                    fprintf(stderr, "%s: reloc targets hunk %u\n", path,
                            target);
                    free(raw); return false;
                }
                for (uint32_t i = 0; i < count; i++) {
                    uint32_t offset = read32(&reader);
                    uint32_t at = out->base[hunk] + offset;
                    if (at + 4 > memory_size) continue;
                    poke32(memory, at, peek32(memory, at) + out->base[target]);
                }
            }
            break;
        case HUNK_SYMBOL:
            for (;;) {
                uint32_t longs = read32(&reader);
                if (!longs) break;
                reader.position += longs * 4 + 4;
            }
            break;
        case HUNK_DEBUG:
            reader.position += read32(&reader) * 4;
            break;
        case HUNK_END:
            hunk++;
            break;
        default:
            fprintf(stderr, "%s: unsupported hunk type $%x at offset %zu\n",
                    path, type, reader.position - 4);
            free(raw); return false;
        }
    }
    free(raw);
    if (reader.overrun) {
        fprintf(stderr, "%s: ran off the end of the file\n", path);
        return false;
    }
    return true;
}
