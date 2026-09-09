#ifndef AMIGA_RECOMP_OCS_VIDEO_H
#define AMIGA_RECOMP_OCS_VIDEO_H

#include <stdint.h>

#define OCS_VIDEO_WIDTH 320
#define OCS_VIDEO_HEIGHT 256

typedef uint8_t (*OcsVideoRead8)(void *user, uint32_t address);

typedef struct {
    void *user;
    OcsVideoRead8 read8;
    uint32_t chip_mask;
    uint32_t copper_address;
} OcsVideoSource;

typedef struct OcsVideo OcsVideo;

OcsVideo *ocs_video_create(void);
void ocs_video_destroy(OcsVideo *video);

/* Runs one PAL copper/bitplane/sprite scan and returns an RGBA8888 image.
 * The returned pixels remain owned by OcsVideo and are valid until the next
 * render or destroy call. */
const uint32_t *ocs_video_render(OcsVideo *video,
                                 const OcsVideoSource *source);

#endif
