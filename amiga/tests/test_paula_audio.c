#include "paula_audio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint8_t sample_memory[256];

static uint8_t read_sample(void *user, uint32_t address)
{
    (void)user;
    return sample_memory[address & 255];
}

static int has_energy(const int16_t *samples, size_t frames, int channel)
{
    for (size_t i = 0; i < frames; i++)
        if (samples[i * 2 + channel]) return 1;
    return 0;
}

int main(void)
{
    for (unsigned i = 0; i < sizeof sample_memory; i++)
        sample_memory[i] = (uint8_t)((i & 1) ? 96 : -96);
    PaulaAudio *paula = paula_audio_create((PaulaAudioSource){
        .read8 = read_sample,
        .address_mask = 255,
    });
    if (!paula) return 1;
    int failed = 0;
    #define CHECK(condition, message) do { if (!(condition)) { \
        fprintf(stderr, "paula audio: %s\n", message); failed = 1; \
    } } while (0)

    int16_t output[512 * 2];
    paula_audio_play_one_shot(paula, 0, 0, 64, 80, 64, 1);
    paula_audio_render(paula, output, 256);
    CHECK(has_energy(output, 256, 0) && has_energy(output, 256, 1),
          "centred one-shot produced no stereo energy");
    for (size_t i = 0; i < 256; i++)
        CHECK(output[i * 2] == output[i * 2 + 1],
              "centred one-shot channels differ");
    CHECK(!paula_audio_channel_active(paula, 0),
          "one-shot did not stop at its descriptor length");

    paula_audio_reset(paula);
    paula_audio_write(paula, 0x0a0, 0);
    paula_audio_write(paula, 0x0a2, 0);
    paula_audio_write(paula, 0x0a4, 4);
    paula_audio_write(paula, 0x0a6, 80);
    paula_audio_write(paula, 0x0a8, 64);
    paula_audio_write(paula, 0x096, 0x8201);
    paula_audio_render(paula, output, 128);
    CHECK(has_energy(output, 128, 0) && !has_energy(output, 128, 1),
          "Paula channel 0 pan/DMA latch is wrong");
    CHECK(paula_audio_channel_active(paula, 0),
          "DMA sample did not loop");
    paula_audio_write(paula, 0x096, 0x0001);
    CHECK(!paula_audio_channel_active(paula, 0),
          "DMACON clear did not stop the channel");

    paula_audio_play_one_shot(paula, 1, 0, 128, 428, 64, 1);
    paula_audio_queue_pal_frame(paula);
    CHECK(paula_audio_fill(paula) == PAULA_AUDIO_FRAME_SAMPLES,
          "PAL producer queued the wrong sample count");
    size_t pulled = paula_audio_pull(paula, output, 512);
    CHECK(pulled == 512 && paula_audio_fill(paula) == 370,
          "callback ring accounting differs");

    paula_audio_destroy(paula);
    if (!failed)
        puts("paula audio: PASS (PCM, one-shot, DMA loop, pan, ring)");
    return failed;
}
