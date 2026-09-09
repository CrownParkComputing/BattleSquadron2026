/* The Hybris file server on its own: no 68000, no disk image.
 *
 * The loader asks for a run of tracks; this proves a track resolves to the
 * right file at the right offset, that a run stops at the file's real end
 * instead of walking into the next one, and that the map agrees with the
 * (track, count) pairs the game's own dispatcher uses. */
#include "hybris_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); \
                   failures++; } \
} while (0)

/* The dispatcher at $ED9C-$EE52 in the decrunched loader, transcribed: each
 * entry is the (first track, track count) it passes to the trackloader.
 * File 05 is loaded by a block outside that window, so it is not listed. */
static const struct { const char *id; int track, tracks; } dispatch[] = {
    { "09",   2,  3 }, { "11",   6,  5 }, { "13",  12,  8 },
    { "08",  20, 20 }, { "10",  40, 16 }, { "12",  56, 16 },
    { "16",  72,  4 }, { "04",  76,  3 }, { "07",  82,  4 },
    { "06",  86, 11 }, { "17",  98,  5 }, { "18", 104,  2 },
    { "03", 106,  6 }, { "14", 112,  3 }, { "15", 116,  3 },
};

static uint8_t buffer[64 * 1024];

int main(int argc, char **argv)
{
    const char *directory = argc > 1 ? argv[1] : "original/hybris";
    if (!hybris_files_load(directory)) {
        printf("FAIL: cannot load %s\n", directory);
        return 1;
    }

    for (size_t i = 0; i < sizeof dispatch / sizeof dispatch[0]; i++) {
        const char *id = hybris_files_id(dispatch[i].track);
        CHECK(id && !strcmp(id, dispatch[i].id),
              "track %d resolves to %s, the loader asks for %s",
              dispatch[i].track, id ? id : "nothing", dispatch[i].id);

        /* Every track of the run must belong to the same file: a run that
         * strayed into its neighbour would load the wrong data silently. */
        for (int t = dispatch[i].track;
             t < dispatch[i].track + dispatch[i].tracks; t++) {
            const char *covering = hybris_files_id(t);
            CHECK(covering && !strcmp(covering, dispatch[i].id),
                  "file %s: track %d belongs to %s", dispatch[i].id, t,
                  covering ? covering : "nothing");
        }

        /* The track before the run must NOT be this file, or the map has
         * the start in the wrong place. */
        const char *before = hybris_files_id(dispatch[i].track - 1);
        CHECK(!before || strcmp(before, dispatch[i].id),
              "file %s also covers track %d, so its start is wrong",
              dispatch[i].id, dispatch[i].track - 1);
    }

    /* A whole-run read is clamped to the file, and its first bytes are the
     * file's first bytes. */
    for (size_t i = 0; i < sizeof dispatch / sizeof dispatch[0]; i++) {
        uint32_t asked = (uint32_t)dispatch[i].tracks * HYBRIS_TRACK_BYTES;
        if (asked > sizeof buffer) asked = sizeof buffer;
        memset(buffer, 0xcd, sizeof buffer);
        uint32_t served = hybris_files_read(dispatch[i].track, buffer, asked);
        CHECK(served > 0 && served <= asked,
              "file %s served %u bytes for a %u byte run", dispatch[i].id,
              served, asked);

        char path[512];
        snprintf(path, sizeof path, "%s/data/%s", directory, dispatch[i].id);
        FILE *handle = fopen(path, "rb");
        CHECK(handle != NULL, "cannot open %s", path);
        if (!handle) continue;
        static uint8_t reference[64 * 1024];
        size_t got = fread(reference, 1, sizeof reference, handle);
        fclose(handle);
        CHECK(got >= served && memcmp(buffer, reference, served) == 0,
              "file %s: served bytes differ from the file on disk",
              dispatch[i].id);
    }

    /* Reading from the middle of a file lands at the right offset. */
    memset(buffer, 0, sizeof buffer);
    uint32_t mid = hybris_files_read(108, buffer, HYBRIS_TRACK_BYTES);
    CHECK(mid == HYBRIS_TRACK_BYTES, "mid-file track served %u bytes", mid);
    FILE *handle = fopen("original/hybris/data/03", "rb");
    if (handle) {
        static uint8_t reference[64 * 1024];
        size_t got = fread(reference, 1, sizeof reference, handle);
        fclose(handle);
        uint32_t offset = 2 * HYBRIS_TRACK_BYTES;   /* track 108 of 106..111 */
        CHECK(got > offset + HYBRIS_TRACK_BYTES &&
              memcmp(buffer, reference + offset, HYBRIS_TRACK_BYTES) == 0,
              "track 108 is not file 03 at offset %u", offset);
    }

    /* A track no file covers must serve nothing rather than guess. */
    CHECK(hybris_files_id(140) == NULL, "track 140 claims to be a file");
    CHECK(hybris_files_read(140, buffer, HYBRIS_TRACK_BYTES) == 0,
          "an unmapped track served data");

    if (failures) { printf("hybris files: %d failure(s)\n", failures); return 1; }
    printf("hybris files: PASS (%zu dispatch entries, map agrees with the "
           "loader)\n", sizeof dispatch / sizeof dispatch[0]);
    return 0;
}
