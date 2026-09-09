#ifndef BATTLE_SQUADRON_RECOMP_OVERLAY_H
#define BATTLE_SQUADRON_RECOMP_OVERLAY_H

#include <stddef.h>
#include <stdint.h>

#define BS_MODULE_NAME_SIZE 9
#define BS_MODULE_COUNT_MAX 32

typedef struct {
    uint32_t load_address;
    uint32_t packed_size;
    uint32_t staging_address;
    int32_t mode;
    char name[BS_MODULE_NAME_SIZE];
} BsModule;

int bs_modules_parse(const uint8_t *loader, size_t loader_size,
                     BsModule *modules, size_t capacity, size_t *count);
int bs_module_load(const char *data_directory, const BsModule *module,
                   uint8_t *memory, size_t memory_size,
                   size_t *runtime_size);
const char *bs_overlay_error(int error);

#endif
