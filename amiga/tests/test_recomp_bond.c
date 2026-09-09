#include "bond.h"
#include "overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const streams[] = {
    "LODDAT", "LODSTO", "LODINT", "LODJOY", "LODS0F",
    "LODS0S", "LODS0T", "LODST1", "LODST2", "LODST3",
    "LODGAM", "LODEND", "LODFIN", "LODTEM", "LODLOD",
};

static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) || (*length = (size_t)ftell(file)) == 0 ||
        fseek(file, 0, SEEK_SET)) {
        fclose(file);
        return NULL;
    }
    unsigned char *data = malloc(*length);
    if (!data || fread(data, 1, *length, file) != *length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    return data;
}

static int test_real_stream(const char *name)
{
    char packed_path[256], expected_path[256];
    snprintf(packed_path, sizeof packed_path,
             "original/whdload/BattleSquadron/data/%s", name);
    snprintf(expected_path, sizeof expected_path,
             "original/modules/%s.bin", name);
    size_t packed_size = 0, expected_size = 0;
    unsigned char *packed = read_file(packed_path, &packed_size);
    unsigned char *expected = read_file(expected_path, &expected_size);
    if (!packed || !expected) {
        fprintf(stderr, "%s: could not read test inputs\n", name);
        free(packed); free(expected);
        return 1;
    }
    size_t declared = 0, written = 0;
    int error = bs_bond_output_size(packed, packed_size, &declared);
    unsigned char *actual = malloc(declared ? declared : 1);
    if (!error)
        error = bs_bond_depack(packed, packed_size, actual, declared, &written);
    int failed = error || written != expected_size ||
                 memcmp(actual, expected, expected_size);
    if (failed)
        fprintf(stderr, "%s: FAIL (%s, wrote %zu expected %zu)\n", name,
                bs_bond_error(error), written, expected_size);
    else
        printf("%s: native BOND parity (%zu bytes)\n", name, written);
    free(actual); free(expected); free(packed);
    return failed;
}

static int test_native_overlay_loader(void)
{
    size_t loader_size = 0;
    unsigned char *loader = read_file(
        "original/whdload/BattleSquadron/data/LOADER", &loader_size);
    if (!loader) {
        fputs("LOADER: could not read descriptor table\n", stderr);
        return 1;
    }
    BsModule modules[BS_MODULE_COUNT_MAX];
    size_t count = 0;
    int error = bs_modules_parse(loader, loader_size, modules,
                                 BS_MODULE_COUNT_MAX, &count);
    free(loader);
    if (error || count != 23 || strcmp(modules[count - 1].name, "LODSAV")) {
        fprintf(stderr, "native overlay table: FAIL (%s, count=%zu)\n",
                bs_overlay_error(error), count);
        return 1;
    }

    unsigned char *memory = calloc(1, 0x80000);
    int failed = memory == NULL;
    size_t loaded = 0;
    for (size_t i = 0; memory && i < count; i++) {
        if (!strcmp(modules[i].name, "LODTAK")) continue;
        char expected_path[256];
        snprintf(expected_path, sizeof expected_path,
                 "original/modules/%s.bin", modules[i].name);
        size_t expected_size = 0, runtime_size = 0;
        unsigned char *expected = read_file(expected_path, &expected_size);
        memset(memory, 0xA5, 0x80000);
        error = bs_module_load("original/whdload/BattleSquadron/data",
                               &modules[i], memory, 0x80000, &runtime_size);
        if (!expected || error || runtime_size != expected_size ||
            memcmp(memory + modules[i].load_address, expected, expected_size)) {
            fprintf(stderr, "%s: native overlay load FAIL (%s)\n",
                    modules[i].name, bs_overlay_error(error));
            failed = 1;
        } else {
            loaded++;
        }
        free(expected);
    }
    free(memory);
    if (!failed)
        printf("native overlay loader: PASS (%zu descriptors, %zu files)\n",
               count, loaded);
    return failed;
}

int main(void)
{
    int failed = 0;
    for (size_t i = 0; i < sizeof streams / sizeof streams[0]; i++)
        failed |= test_real_stream(streams[i]);
    failed |= test_native_overlay_loader();
    if (!failed) puts("native BOND translation: PASS (15/15)");
    return failed ? 1 : 0;
}
