#include "hybris_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char     id[8];
    int      track;
    int      tracks;
    uint32_t length;
    uint8_t *bytes;
} HybrisFile;

static HybrisFile files[32];
static int file_count;

static HybrisFile *find(int track)
{
    for (int i = 0; i < file_count; i++)
        if (track >= files[i].track && track < files[i].track + files[i].tracks)
            return &files[i];
    return NULL;
}

bool hybris_files_load(const char *directory)
{
    char path[640];
    snprintf(path, sizeof path, "%s/disk-map.txt", directory);
    FILE *map = fopen(path, "r");
    if (!map) { perror(path); return false; }

    file_count = 0;
    char line[256];
    while (fgets(line, sizeof line, map)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        HybrisFile entry;
        memset(&entry, 0, sizeof entry);
        if (sscanf(line, "%7s %d %d %u", entry.id, &entry.track,
                   &entry.tracks, &entry.length) != 4) {
            fprintf(stderr, "%s: cannot parse '%s'", path, line);
            fclose(map); return false;
        }
        if (file_count == (int)(sizeof files / sizeof files[0])) {
            fprintf(stderr, "%s: more files than this host holds\n", path);
            fclose(map); return false;
        }
        snprintf(path, sizeof path, "%s/data/%s", directory, entry.id);
        FILE *handle = fopen(path, "rb");
        if (!handle) { perror(path); fclose(map); return false; }
        entry.bytes = malloc(entry.length);
        if (!entry.bytes ||
            fread(entry.bytes, 1, entry.length, handle) != entry.length) {
            fprintf(stderr, "%s: short read\n", path);
            free(entry.bytes); fclose(handle); fclose(map); return false;
        }
        fclose(handle);
        files[file_count++] = entry;
    }
    fclose(map);
    if (!file_count) {
        fprintf(stderr, "%s/disk-map.txt lists no files\n", directory);
        return false;
    }
    return true;
}

const char *hybris_files_id(int track)
{
    HybrisFile *file = find(track);
    return file ? file->id : NULL;
}

uint32_t hybris_files_read(int track, uint8_t *destination, uint32_t length)
{
    HybrisFile *file = find(track);
    if (!file) return 0;
    uint32_t offset = (uint32_t)(track - file->track) * HYBRIS_TRACK_BYTES;
    if (offset >= file->length) return 0;
    uint32_t available = file->length - offset;
    if (length > available) length = available;
    memcpy(destination, file->bytes + offset, length);
    return length;
}
