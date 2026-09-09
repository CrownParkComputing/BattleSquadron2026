#include "paula_audio.h"

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define PAULA_CLOCK 3546895.0
#define PAULA_RING_FRAMES 32768u

typedef struct {
    uint32_t lc;
    uint32_t play_address;
    uint32_t position;
    uint32_t byte_length;
    uint16_t length_words;
    uint16_t period;
    uint8_t volume;
    double fraction;
    int active;
    int one_shot;
    int centred;
} PaulaChannel;

struct PaulaAudio {
    PaulaAudioSource source;
    PaulaChannel channel[4];
    uint16_t dmacon;
    int16_t ring[PAULA_RING_FRAMES * 2];
    size_t write_position;
    size_t read_position;
    _Atomic size_t fill;
};

static int16_t clamp16(double value)
{
    if (value > 32767.0) return 32767;
    if (value < -32768.0) return -32768;
    return (int16_t)lrint(value);
}

PaulaAudio *paula_audio_create(PaulaAudioSource source)
{
    if (!source.read8) return NULL;
    PaulaAudio *paula = calloc(1, sizeof *paula);
    if (!paula) return NULL;
    paula->source = source;
    return paula;
}

void paula_audio_destroy(PaulaAudio *paula)
{
    free(paula);
}

void paula_audio_reset(PaulaAudio *paula)
{
    if (!paula) return;
    PaulaAudioSource source = paula->source;
    memset(paula, 0, sizeof *paula);
    paula->source = source;
}

static void latch_channel(PaulaChannel *channel)
{
    channel->play_address = channel->lc;
    channel->position = 0;
    channel->byte_length = (uint32_t)channel->length_words * 2u;
    channel->fraction = 0.0;
    channel->active = channel->byte_length != 0;
    channel->one_shot = 0;
    channel->centred = 0;
}

void paula_audio_write(PaulaAudio *paula, uint16_t reg, uint16_t value)
{
    if (!paula) return;
    reg &= 0x1fe;
    if (reg == 0x096) {
        uint16_t old = paula->dmacon;
        if (value & 0x8000) paula->dmacon |= value & 0x7fff;
        else paula->dmacon &= (uint16_t)~(value & 0x7fff);
        for (unsigned index = 0; index < 4; index++) {
            uint16_t mask = (uint16_t)(1u << index);
            int was_on = (old & 0x0200) && (old & mask);
            int is_on = (paula->dmacon & 0x0200) &&
                        (paula->dmacon & mask);
            if (is_on && !was_on)
                latch_channel(&paula->channel[index]);
            else if (!is_on && was_on)
                paula->channel[index].active = 0;
        }
        return;
    }
    if (reg < 0x0a0 || reg > 0x0de) return;
    unsigned index = (reg - 0x0a0) >> 4;
    unsigned offset = (reg - 0x0a0) & 0x0f;
    if (index >= 4) return;
    PaulaChannel *channel = &paula->channel[index];
    switch (offset) {
    case 0x0:
        channel->lc = (channel->lc & 0x0000ffffu) |
                      ((uint32_t)value << 16);
        break;
    case 0x2:
        channel->lc = (channel->lc & 0xffff0000u) | value;
        break;
    case 0x4: channel->length_words = value; break;
    case 0x6: channel->period = value; break;
    case 0x8: channel->volume = value > 64 ? 64 : (uint8_t)value; break;
    default: break;
    }
}

void paula_audio_play_one_shot(PaulaAudio *paula, unsigned index,
                               uint32_t address, uint16_t length_words,
                               uint16_t period, uint8_t volume, int centred)
{
    if (!paula || index >= 4) return;
    PaulaChannel *channel = &paula->channel[index];
    channel->lc = address;
    channel->play_address = address;
    channel->position = 0;
    channel->length_words = length_words;
    channel->byte_length = (uint32_t)length_words * 2u;
    channel->period = period ? period : 1;
    channel->volume = volume > 64 ? 64 : volume;
    channel->fraction = 0.0;
    channel->active = channel->byte_length != 0;
    channel->one_shot = 1;
    channel->centred = centred != 0;
}

void paula_audio_stop(PaulaAudio *paula, unsigned channel)
{
    if (paula && channel < 4) paula->channel[channel].active = 0;
}

int paula_audio_channel_active(const PaulaAudio *paula, unsigned channel)
{
    return paula && channel < 4 && paula->channel[channel].active;
}

void paula_audio_render(PaulaAudio *paula, int16_t *stereo, size_t frames)
{
    if (!stereo) return;
    if (!paula) {
        memset(stereo, 0, frames * 2 * sizeof *stereo);
        return;
    }
    for (size_t frame = 0; frame < frames; frame++) {
        double left = 0.0, right = 0.0;
        for (unsigned index = 0; index < 4; index++) {
            PaulaChannel *channel = &paula->channel[index];
            if (!channel->active || !channel->period ||
                !channel->byte_length) continue;
            uint32_t address = channel->play_address + channel->position;
            if (paula->source.address_mask)
                address &= paula->source.address_mask;
            int sample = (int8_t)paula->source.read8(paula->source.user,
                                                     address);
            double level = sample * channel->volume * 2.0;
            if (channel->centred) {
                left += level;
                right += level;
            } else if (index == 0 || index == 3) {
                left += level;
            } else {
                right += level;
            }
            channel->fraction += PAULA_CLOCK /
                                 (channel->period * PAULA_AUDIO_RATE);
            uint32_t advance = (uint32_t)channel->fraction;
            channel->fraction -= advance;
            channel->position += advance;
            if (channel->position >= channel->byte_length) {
                if (channel->one_shot) {
                    channel->active = 0;
                } else {
                    channel->position %= channel->byte_length;
                    channel->play_address = channel->lc;
                }
            }
        }
        stereo[frame * 2] = clamp16(left);
        stereo[frame * 2 + 1] = clamp16(right);
    }
}

void paula_audio_queue_pal_frame(PaulaAudio *paula)
{
    if (!paula) return;
    size_t fill = atomic_load_explicit(&paula->fill, memory_order_acquire);
    size_t space = PAULA_RING_FRAMES - fill;
    size_t frames = space < PAULA_AUDIO_FRAME_SAMPLES
                  ? space : PAULA_AUDIO_FRAME_SAMPLES;
    while (frames) {
        size_t contiguous = PAULA_RING_FRAMES - paula->write_position;
        if (contiguous > frames) contiguous = frames;
        paula_audio_render(paula, paula->ring + paula->write_position * 2,
                           contiguous);
        paula->write_position = (paula->write_position + contiguous) %
                                 PAULA_RING_FRAMES;
        atomic_fetch_add_explicit(&paula->fill, contiguous,
                                  memory_order_release);
        frames -= contiguous;
    }
}

size_t paula_audio_pull(PaulaAudio *paula, int16_t *stereo, size_t frames)
{
    if (!stereo) return 0;
    size_t available = paula
        ? atomic_load_explicit(&paula->fill, memory_order_acquire) : 0;
    size_t pulled = frames < available ? frames : available;
    size_t remaining = pulled;
    while (remaining) {
        size_t contiguous = PAULA_RING_FRAMES - paula->read_position;
        if (contiguous > remaining) contiguous = remaining;
        memcpy(stereo + (pulled - remaining) * 2,
               paula->ring + paula->read_position * 2,
               contiguous * 2 * sizeof *stereo);
        paula->read_position = (paula->read_position + contiguous) %
                                PAULA_RING_FRAMES;
        atomic_fetch_sub_explicit(&paula->fill, contiguous,
                                  memory_order_release);
        remaining -= contiguous;
    }
    if (pulled < frames)
        memset(stereo + pulled * 2, 0,
               (frames - pulled) * 2 * sizeof *stereo);
    return pulled;
}

size_t paula_audio_fill(const PaulaAudio *paula)
{
    return paula
        ? atomic_load_explicit(&paula->fill, memory_order_acquire) : 0;
}
