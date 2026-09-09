#include "overlay.h"

#include "bond.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    OVERLAY_OK = 0,
    OVERLAY_BAD_ARGUMENT = -100,
    OVERLAY_BAD_TABLE = -101,
    OVERLAY_TOO_MANY = -102,
    OVERLAY_OPEN_FAILED = -103,
    OVERLAY_READ_FAILED = -104,
    OVERLAY_SIZE_MISMATCH = -105,
    OVERLAY_MEMORY_RANGE = -106,
    OVERLAY_BOND_FAILED = -107,
};

#define TABLE_FILE_OFFSET 0x1880
#define DESCRIPTOR_SIZE 24

static uint32_t read_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

int bs_modules_parse(const uint8_t *loader, size_t loader_size,
                     BsModule *modules, size_t capacity, size_t *count)
{
    if (!loader || !modules || !count) return OVERLAY_BAD_ARGUMENT;
    size_t offset = TABLE_FILE_OFFSET, found = 0;
    while (offset + DESCRIPTOR_SIZE <= loader_size) {
        if (found == capacity) return OVERLAY_TOO_MANY;
        BsModule *module = &modules[found++];
        module->load_address = read_be32(loader + offset);
        module->packed_size = read_be32(loader + offset + 4);
        module->staging_address = read_be32(loader + offset + 8);
        module->mode = (int32_t)read_be32(loader + offset + 12);
        memcpy(module->name, loader + offset + 16, 8);
        module->name[8] = 0;
        for (int i = 7; i >= 0 && !module->name[i]; i--)
            module->name[i] = 0;
        offset += DESCRIPTOR_SIZE;
        if (!strcmp(module->name, "LODSAV")) {
            *count = found;
            return OVERLAY_OK;
        }
    }
    return OVERLAY_BAD_TABLE;
}

static uint8_t *read_file(const char *path, size_t expected)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    uint8_t *data = malloc(expected ? expected : 1);
    size_t got = data ? fread(data, 1, expected, file) : 0;
    int extra = fgetc(file);
    fclose(file);
    if (!data || got != expected || extra != EOF) {
        free(data);
        return NULL;
    }
    return data;
}

int bs_module_load(const char *data_directory, const BsModule *module,
                   uint8_t *memory, size_t memory_size,
                   size_t *runtime_size)
{
    if (!data_directory || !module || !memory)
        return OVERLAY_BAD_ARGUMENT;
    char path[512];
    int length = snprintf(path, sizeof path, "%s/%s",
                          data_directory, module->name);
    if (length < 0 || (size_t)length >= sizeof path)
        return OVERLAY_BAD_ARGUMENT;
    uint8_t *packed = read_file(path, module->packed_size);
    if (!packed) return OVERLAY_OPEN_FAILED;

    size_t unpacked_size = module->packed_size;
    int error = OVERLAY_OK;
    if (module->mode < 0) {
        int bond_error = bs_bond_output_size(packed, module->packed_size,
                                             &unpacked_size);
        if (bond_error) error = OVERLAY_BOND_FAILED;
    }
    if (!error && (module->load_address > memory_size ||
                   unpacked_size > memory_size - module->load_address))
        error = OVERLAY_MEMORY_RANGE;
    if (!error && module->mode < 0) {
        size_t written = 0;
        int bond_error = bs_bond_depack(
            packed, module->packed_size, memory + module->load_address,
            memory_size - module->load_address, &written);
        if (bond_error || written != unpacked_size)
            error = OVERLAY_BOND_FAILED;
    } else if (!error) {
        memcpy(memory + module->load_address, packed, unpacked_size);
    }
    free(packed);
    if (!error && runtime_size) *runtime_size = unpacked_size;
    return error;
}

const char *bs_overlay_error(int error)
{
    switch (error) {
    case OVERLAY_OK: return "ok";
    case OVERLAY_BAD_ARGUMENT: return "invalid argument";
    case OVERLAY_BAD_TABLE: return "module table has no LODSAV terminator";
    case OVERLAY_TOO_MANY: return "module table capacity exceeded";
    case OVERLAY_OPEN_FAILED: return "module missing or its size is wrong";
    case OVERLAY_READ_FAILED: return "module read failed";
    case OVERLAY_SIZE_MISMATCH: return "module size mismatch";
    case OVERLAY_MEMORY_RANGE: return "module exceeds native memory map";
    case OVERLAY_BOND_FAILED: return "native BOND decompression failed";
    default: return "unknown overlay error";
    }
}
