#ifndef BATTLE_SQUADRON_HYBRIS_FILES_H
#define BATTLE_SQUADRON_HYBRIS_FILES_H

#include <stdbool.h>
#include <stdint.h>

/* Hybris' data as files, the way Battle Squadron's already is.
 *
 * The cracked loader asks for a run of tracks, not a filename: its dispatcher
 * turns a file descriptor into (first track, track count) and calls the
 * trackloader.  Every file on the disk starts on a track boundary and is
 * contiguous, so a track number resolves to a file and an offset inside it,
 * and the whole MFM/DMA/CIA path becomes a memcpy from a file on the host. */

#define HYBRIS_TRACK_BYTES 5632

bool hybris_files_load(const char *directory);   /* reads disk-map.txt */

/* Copy `length` bytes starting at `track` into `destination`.  Returns the
 * number of bytes served; short means the run ran off the end of the file,
 * which is what the loader's own limit check expects. */
uint32_t hybris_files_read(int track, uint8_t *destination, uint32_t length);

/* The file id covering `track`, or NULL. */
const char *hybris_files_id(int track);

#endif
