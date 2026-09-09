#ifndef AMIGA_RECOMP_PAULA_AUDIO_H
#define AMIGA_RECOMP_PAULA_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#define PAULA_AUDIO_RATE 44100
#define PAULA_AUDIO_FRAME_SAMPLES 882

typedef uint8_t (*PaulaAudioRead8)(void *user, uint32_t address);

typedef struct PaulaAudio PaulaAudio;

typedef struct {
    void *user;
    PaulaAudioRead8 read8;
    uint32_t address_mask;
} PaulaAudioSource;

PaulaAudio *paula_audio_create(PaulaAudioSource source);
void paula_audio_destroy(PaulaAudio *paula);
void paula_audio_reset(PaulaAudio *paula);

/* Observe a write to a custom-chip register, expressed as its $DFFxxx
 * offset.  This is suitable for any recomp runtime's MMIO write seam. */
void paula_audio_write(PaulaAudio *paula, uint16_t reg, uint16_t value);

/* A one-shot is useful for an original driver event whose sample descriptor
 * has been translated before the complete tracker interrupt is available. */
void paula_audio_play_one_shot(PaulaAudio *paula, unsigned channel,
                               uint32_t address, uint16_t length_words,
                               uint16_t period, uint8_t volume, int centred);
void paula_audio_stop(PaulaAudio *paula, unsigned channel);
int paula_audio_channel_active(const PaulaAudio *paula, unsigned channel);

void paula_audio_render(PaulaAudio *paula, int16_t *stereo, size_t frames);
void paula_audio_queue_pal_frame(PaulaAudio *paula);
size_t paula_audio_pull(PaulaAudio *paula, int16_t *stereo, size_t frames);
size_t paula_audio_fill(const PaulaAudio *paula);

#endif
