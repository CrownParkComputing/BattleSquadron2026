/* audio.c -- LODGAM sequencer + Paula mixer, native.
 *
 * The music/SFX driver is a byte-for-byte port of the parity-pinned
 * translation in ~/BattleSquadron-Amiga/src/recomp/runtime.c (LODGAM $24856/
 * $24F34 sequencer, $24DDE SelectMusic, $24D6E PlaySoundEffect, $247C8
 * channel init) operating on the chip image via rd/wr helpers; writes to the
 * custom-chip range go to a Paula model ported from
 * ~/BattleSquadron-Amiga/src/platform/paula_audio.c.
 *
 * Channel state (62 B at $252A4/$252E2/$25320/$2535E), instruments $25504
 * (23 x 32 B), driver state $251F8 (+1 master volume, +4 mute-all/music-off,
 * +5($251FD) sfx mute), song table $251F8+12 (+16/track), periods $25166,
 * sfx descriptors $2539C (16 x 12 B).  The CIA-B timer latch is $3100 ->
 * 709379/12544 = 56.55 Hz.  CIA ticks are scheduled in the generated PCM
 * sample timeline, independently of display cadence, so Android compositor
 * stalls cannot slow the music or make one-shot speech loop.
 */
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "engine/engine.h"
#include "audio.h"
#ifdef AUDIOTEST_MAIN
/* headless build: raylib stream stubs */
typedef struct { int _; } AudioStream;
static void InitAudioDevice(void) {}
static int IsAudioDeviceReady(void) { return 0; }
static void SetAudioStreamBufferSizeDefault(int n) { (void)n; }
static AudioStream LoadAudioStream(unsigned a, unsigned b, unsigned c) { (void)a;(void)b;(void)c; AudioStream s={0}; return s; }
static void PlayAudioStream(AudioStream s) { (void)s; }
static void StopAudioStream(AudioStream s) { (void)s; }
static void UnloadAudioStream(AudioStream s) { (void)s; }
static void CloseAudioDevice(void) {}
static int IsAudioStreamProcessed(AudioStream s) { (void)s; return 0; }
static void UpdateAudioStream(AudioStream s, const void *d, int n) { (void)s;(void)d;(void)n; }
#else
#include "raylib.h"
#endif

#define PAULA_CLOCK 3546895.0
#define OUT_RATE 44100

/* ---------------- Paula model ---------------- */
typedef struct {
    uint32_t lc, play, pos, bytelen;
    uint16_t lenw, period;
    uint8_t volume;
    double frac;
    int active;
} PaulaCh;
static PaulaCh pch[4];
static uint16_t pdmacon = 0x0200;   /* DMAEN master: set by the system, not LODGAM */
static float master_gain = 0.8f;
static int music_enabled = 1, sfx_enabled = 1;

static void paula_latch(PaulaCh *c)
{
    c->play = c->lc; c->pos = 0; c->bytelen = (uint32_t)c->lenw * 2u;
    c->frac = 0.0; c->active = c->bytelen != 0;
}

static void paula_write(uint16_t reg, uint16_t v)
{
    reg &= 0x1FE;
    if (reg == 0x096) {
        uint16_t old = pdmacon;
        if (v & 0x8000) pdmacon |= v & 0x7FFF; else pdmacon &= (uint16_t)~(v & 0x7FFF);
        for (unsigned i = 0; i < 4; i++) {
            uint16_t m = (uint16_t)(1u << i);
            int was = (old & 0x200) && (old & m), is = (pdmacon & 0x200) && (pdmacon & m);
            if (is && !was) paula_latch(&pch[i]);
            else if (!is && was) pch[i].active = 0;
        }
        return;
    }
    if (reg < 0x0A0 || reg > 0x0DE) return;
    unsigned i = (reg - 0x0A0) >> 4, off = (reg - 0x0A0) & 0x0F;
    if (i >= 4) return;
    switch (off) {
    case 0x0: pch[i].lc = (pch[i].lc & 0x0000FFFFu) | ((uint32_t)v << 16); break;
    case 0x2: pch[i].lc = (pch[i].lc & 0xFFFF0000u) | v; break;
    case 0x4: pch[i].lenw = v; break;
    case 0x6: pch[i].period = v; break;
    case 0x8: pch[i].volume = v > 64 ? 64 : (uint8_t)v; break;
    default: break;
    }
}

static int16_t clamp16(double v)
{ return v > 32767 ? 32767 : v < -32768 ? -32768 : (int16_t)lrint(v); }

static double filt_l, filt_r;            /* Paula output filter state */
static void paula_render(int16_t *stereo, size_t frames)
{
    for (size_t f = 0; f < frames; f++) {
        double l = 0, r = 0;
        for (unsigned i = 0; i < 4; i++) {
            PaulaCh *c = &pch[i];
            if (!c->active || !c->period || !c->bytelen) continue;
            uint32_t a = (c->play + c->pos) & 0x7FFFF;
            double lvl = (int8_t)bs_chip[a] * (double)c->volume;   /* same scale as the machine's mixer */
            if (i == 0 || i == 3) l += lvl; else r += lvl;
            c->frac += PAULA_CLOCK / (c->period * (double)OUT_RATE);
            uint32_t adv = (uint32_t)c->frac;
            c->frac -= adv; c->pos += adv;
            if (c->pos >= c->bytelen) {
                /* end of the block: Paula re-latches BOTH pointer and length.
                 * The sequencer's instruments are one-shot attack + short loop
                 * (it rewrites AUDxLEN mid-note), so without the length re-latch
                 * the whole attack sample repeats and that instrument sits far
                 * too loud over the rest of the mix. */
                c->play = c->lc;
                c->bytelen = (uint32_t)c->lenw * 2u;
                if (!c->bytelen) { c->active = 0; continue; }
                c->pos %= c->bytelen;
            }
        }
        /* soft A500-style stereo blend, then Paula's output filter: the
         * machine does not put the raw step waveform on the jacks, and without
         * it the sequencer's short wavetables alias into a metallic buzz.  Same
         * one-pole the oracle's mixer runs (host amiga.c audio_mix), so a
         * capture from either side matches. */
        double lo = 0.75 * l + 0.25 * r, ro = 0.75 * r + 0.25 * l;
        filt_l += 0.45 * (lo - filt_l);
        filt_r += 0.45 * (ro - filt_r);
        stereo[f * 2] = clamp16(filt_l * 1.8 * master_gain);
        stereo[f * 2 + 1] = clamp16(filt_r * 1.8 * master_gain);
    }
}

/* ---------------- chip access with the MMIO seam ---------------- */
static uint8_t  rd8(uint32_t a)  { return a < 0x80000 ? bs_chip[a] : 0; }
static uint16_t rd16(uint32_t a) { return a + 1 < 0x80000 ? cw(a) : 0; }
static uint32_t rd32(uint32_t a) { return a + 3 < 0x80000 ? cl(a) : 0; }
static void wr8(uint32_t a, uint8_t v)  { if (a < 0x80000) bs_chip[a] = v; }
static void wr16(uint32_t a, uint16_t v)
{
    if (a < 0x80000) { bs_chip[a] = (uint8_t)(v >> 8); bs_chip[a + 1] = (uint8_t)v; }
    else if (a >= 0xDFF000 && a < 0xDFF200) paula_write((uint16_t)(a - 0xDFF000), v);
    /* CIA ($BFxxxx) and vectors: ignored (the tick cadence is native) */
}
static void wr32(uint32_t a, uint32_t v)
{ wr16(a, (uint16_t)(v >> 16)); wr16(a + 2, (uint16_t)v); }

#define G UINT32_C(0x251F8)
#define PERIODS UINT32_C(0x25166)
static const uint32_t chans[4] = { 0x252A4, 0x252E2, 0x25320, 0x2535E };

/* LODGAM $247C8 InitAudioChannel (port of init_gameplay_channel) */
static void init_channel(uint32_t s)
{
    static const uint8_t clr[] = { 0x30,0x31,0x32,0x33,0x34,0x39,0x35,0x37,0x36,0x3B,0x3C };
    for (size_t i = 0; i < sizeof clr; i++) wr8(s + clr[i], 0);
    wr8(s + 0x3A, 1);
    wr8(s + 0x3D, 0);
    wr16(s + 0x28, 0); wr16(s + 0x2A, 0); wr16(s + 0x2C, 0);
    wr32(s + 0x14, 0); wr32(s + 0x18, 0); wr32(s + 0x1C, 0);
    wr32(s + 4, 0x25504);
    uint32_t sample = rd32(0x25504);
    uint32_t paula = rd32(s);
    wr32(paula, rd32(sample));
    wr16(paula + 4, rd16(sample + 4));
    wr16(paula + 8, 0);
    uint32_t seq = rd32(s + 8);
    wr32(s + 0x0C, seq);
    wr32(s + 0x10, rd32(seq));
    wr16(s + 0x20, rd16(seq + 6));
    wr16(s + 0x22, (uint16_t)(rd16(seq + 0x0A) - 1));
}

/* LODGAM $24856 music_channel_update (verbatim port) */
static void channel_update(uint32_t s)
{
    uint32_t a1 = rd32(s + 4);
    uint32_t a2 = rd32(s + 16);
    const uint32_t a3 = rd32(s);

    if (rd8(s + 61) == 0) {
        if (rd8(s + 59) != 0) {
            if (rd8(s + 60) != 0) wr8(s + 60, (uint8_t)(rd8(s + 60) - 1));
            else {
                wr8(s + 59, 0);
                if (rd8(G + 4) == 0) {
                    wr8(s + 58, rd8(a1 + 8));
                    uint32_t sample = rd32(a1);
                    wr32(a3, rd32(sample));
                    wr16(a3 + 4, rd16(sample + 4));
                }
            }
        }
        wr16(0xDFF096, (uint16_t)(0x8000 | rd16(s + 46)));
        if (rd8(s + 58) != 0) { wr8(s + 58, 0); wr16(a3 + 4, 1); }
    }

    if (rd8(G + 4) != 0) return;

    if (rd8(s + 49) != 0) {
        wr8(s + 49, (uint8_t)(rd8(s + 49) - 1));
        if (rd8(s + 59) != 0) return;

        if (rd8(a1 + 9) != 0) {          /* waveform modulation */
            if (rd8(s + 54) != 0) wr8(s + 54, (uint8_t)(rd8(s + 54) - 1));
            else {
                wr8(s + 54, (uint8_t)(rd8(a1 + 9) - 1));
                uint32_t sample = rd32(a1);
                uint16_t length = rd16(sample + 6);
                length = (uint16_t)((length & 0xFF00) | ((length - rd8(a1 + 10)) & 0xFF));
                uint32_t wave = rd32(sample) + (int16_t)length;
                uint8_t position = rd8(a1 + 24);
                wr8(wave + position, rd8(a1 + 22));
                int flip;
                if ((int8_t)rd8(a1 + 23) >= 0) { position++; flip = (uint8_t)(rd8(a1 + 10) * 2) == position; }
                else { position--; flip = position == 0; }
                if (flip) { wr8(a1 + 23, (uint8_t)~rd8(a1 + 23)); wr8(a1 + 22, (uint8_t)~rd8(a1 + 22)); }
                wr8(a1 + 24, position);
            }
        }

        if (rd8(s + 55) != 0) {          /* portamento */
            uint16_t step = rd8(s + 55);
            uint16_t target = rd16(s + 38);
            uint16_t period = rd16(s + 36);
            if ((int16_t)(target - period) < 0) {
                period = (uint16_t)(period - step);
                if ((int16_t)(target - period) >= 0) period = target;
            } else {
                period = (uint16_t)(period + step);
                if ((int16_t)(target - period) < 0) period = target;
            }
            wr16(s + 36, period);
            if (rd8(s + 61) == 0) wr16(a3 + 6, period);
        } else {                          /* arpeggio */
            uint16_t index = rd16(s + 44);
            uint16_t note = (uint16_t)((rd8(s + 20 + index) + rd8(s + 48)) & 0xFF);
            note = (uint16_t)(note + rd16(s + 32));
            uint16_t period = rd16(PERIODS + (uint16_t)(note * 2));
            wr16(s + 36, period);
            if (rd8(s + 61) == 0) wr16(a3 + 6, period);
            index = (uint16_t)(index - 1);
            if ((int16_t)index < 0) index = (uint16_t)(index + 12);
            wr16(s + 44, index);
        }

        if (rd8(a1 + 11) != 0) {         /* vibrato */
            if (rd8(s + 53) != 0) wr8(s + 53, (uint8_t)(rd8(s + 53) - 1));
            else {
                uint16_t position = rd16(s + 42);
                uint32_t table = rd32(rd32(a1 + 4));
                int16_t sample = (int8_t)rd8(table + position);
                int16_t depth = rd8(a1 + 12);
                uint16_t period = (uint16_t)(sample * depth + (int16_t)rd16(s + 36));
                if (rd8(s + 61) == 0) wr16(a3 + 6, period);
                uint16_t next = (uint16_t)(position - rd8(a1 + 11));
                wr16(s + 42, next);
                if ((int16_t)next < 0) {
                    uint32_t vib = rd32(a1 + 4);
                    wr16(s + 42, (uint16_t)(rd16(vib + 4) * 2));
                }
            }
        }

        /* volume envelope */
        uint8_t count = (uint8_t)(rd8(s + 52) - 1);
        wr8(s + 52, count);
        if ((int8_t)count >= 0) return;
        wr8(s + 52, rd8(s + 51));
        uint16_t stage = rd16(s + 40);
        uint8_t level = rd8(a1 + 14 + stage);
        uint8_t rate = rd8(a1 + 18 + stage);
        uint8_t volume = rd8(s + 57);
        int advance = 0;
        if ((int8_t)(uint8_t)(level - volume) < 0) {
            volume = (uint8_t)(volume - rate);
            if ((int8_t)(uint8_t)(level - volume) >= 0) { volume = level; advance = 1; }
        } else {
            uint8_t mvol = rd8(G + 1);
            volume = (uint8_t)(volume + rate);
            if ((int8_t)(uint8_t)(volume - mvol) >= 0) { volume = mvol; advance = 1; }
            else if ((int8_t)(uint8_t)(level - volume) < 0) { volume = level; advance = 1; }
        }
        if (advance && stage != 3) stage = (uint16_t)(stage + 1);
        wr16(s + 40, stage);
        wr8(s + 57, volume);
        if (rd8(s + 61) == 0) wr16(a3 + 8, (uint16_t)(volume & 0x3F));
        return;
    }

    /* note ended: read commands until a note */
    if (rd8(G + 4) != 0) return;
    wr8(s + 55, 0);
    uint8_t delay = rd8(a1 + 13);
    if (delay != 0) wr8(s + 53, (uint8_t)((delay << 2) - 1));

    for (int guard = 0; guard < 64; guard++) {
        if (rd8(a2) == 0x80) {           /* arpeggio table */
            wr16(s + 44, 0);
            uint32_t entry = UINT32_C(0x2545C) + (uint32_t)rd8(a2 + 1) * 12;
            wr32(s + 20, rd32(entry)); wr32(s + 24, rd32(entry + 4)); wr32(s + 28, rd32(entry + 8));
            a2 += 2;
        }
        if (rd8(a2) == 0x81) {           /* portamento command */
            wr16(s + 40, 0);
            uint16_t note = rd8(a2 + 1);
            wr8(s + 50, (uint8_t)note);
            note = (uint16_t)(note + rd16(s + 32));
            wr16(s + 38, rd16(PERIODS + (uint16_t)(note * 2)));
            wr8(s + 55, rd8(a2 + 2));
            wr8(s + 49, (uint8_t)((rd8(a2 + 3) << 2) - 1));
            a2 += 4;
            goto store_and_pitch;
        }
        if (rd8(a2) == 0x82) {           /* instrument change */
            wr8(s + 57, 0);
            wr16(s + 40, 0);
            a1 = UINT32_C(0x25504) + ((uint32_t)rd8(a2 + 1) << 5);
            wr32(s + 4, a1);
            if (rd8(s + 61) == 0 && rd8(s + 59) == 0) {
                wr16(0xDFF096, rd16(s + 46));
                uint32_t sample = rd32(a1);
                wr32(a3, rd32(sample));
                wr16(a3 + 4, rd16(sample + 4));
            }
            a2 += 2;
        }
        if (rd8(a2) == 0x83) {           /* end of song */
            wr8(G + 0, 1); wr16(G + 8, 0); wr16(G + 6, 1);
            return;
        }
        if (rd8(a2) == 0x84) { wr8(s + 51, rd8(a2 + 1)); a2 += 2; }
        if ((int8_t)rd8(a2) >= 0) break;

        uint32_t entry;                   /* end of pattern */
        if (rd16(s + 34) != 0) {
            wr16(s + 34, (uint16_t)(rd16(s + 34) - 1));
            entry = rd32(s + 12);
            a2 = rd32(entry);
            wr32(s + 16, a2);
            wr16(s + 32, rd16(entry + 6));
            continue;
        }
        wr32(s + 12, rd32(s + 12) + 12);
        entry = rd32(s + 12);
        if ((int8_t)rd8(entry) < 0) { entry = rd32(s + 8); wr32(s + 12, entry); }
        a2 = rd32(entry);
        wr32(s + 16, a2);
        wr16(s + 32, rd16(entry + 6));
        wr16(s + 34, (uint16_t)(rd16(entry + 10) - 1));
    }

    {   /* a note */
        uint8_t duration = rd8(a2 + 1);
        if (duration == 0) {
            uint16_t note = rd8(a2);
            wr8(s + 48, (uint8_t)note);
            note = (uint16_t)(note + rd16(s + 32));
            wr16(s + 36, rd16(PERIODS + (uint16_t)(note * 2)));
            a2 += 2;
            wr32(s + 16, a2);
            return;
        }
        wr8(s + 49, (uint8_t)((duration << 2) - 1));
        wr16(s + 40, 0);
        uint16_t note = rd8(a2);
        wr8(s + 48, (uint8_t)note);
        note = (uint16_t)(note + rd16(s + 32));
        wr16(s + 36, rd16(PERIODS + (uint16_t)(note * 2)));
        if (rd8(s + 61) != 0 || rd8(s + 59) != 0) {
            a2 += 2;
            wr32(s + 16, a2);
            return;
        }
        wr16(0xDFF096, rd16(s + 46));
        uint32_t sample = rd32(a1);
        wr32(a3, rd32(sample));
        wr16(a3 + 4, rd16(sample + 4));
        a2 += 2;
        if (rd8(a1 + 8) != 0) wr8(s + 58, 1);
    }
store_and_pitch:
    wr32(s + 16, a2);
    if (rd8(s + 61) == 0) wr16(a3 + 6, rd16(s + 36));
}

/* LODGAM $24F34 music_interrupt (verbatim port) */
static void music_interrupt(void)
{
    if (rd16(0x24E32) != 0) {
        wr16(0x24E32, 0);
        uint8_t toggled = (uint8_t)(rd8(G + 4) ^ 1);
        wr8(G + 4, toggled);
        if (toggled)
            for (int i = 0; i < 4; i++) wr16(0xDFF0A0 + (uint32_t)i * 16 + 8, 0);
    }

    if (rd8(G + 0) == 0) {
        if (rd8(G + 2) != 0) {           /* fade */
            if (rd8(G + 1) != 0) {
                uint8_t tick = (uint8_t)(rd8(0x251F6) - 1);
                wr8(0x251F6, tick);
                if ((int8_t)tick < 0) {
                    wr8(0x251F6, 2);
                    wr8(G + 1, (uint8_t)(rd8(G + 1) - 1));
                }
            } else {
                wr8(G + 0, 1);
                wr8(G + 1, rd8(G + 3));
                wr8(G + 2, 0); wr8(G + 3, 0);
                for (int i = 0; i < 4; i++) wr16(0xDFF0A0 + (uint32_t)i * 16 + 8, 0);
                return;
            }
        }
        for (int i = 0; i < 4; i++) channel_update(chans[i]);
        return;
    }

    if (rd16(G + 8) != 0) {              /* new track: save the live bank, re-seed */
        for (unsigned w = 0; w < 128; w++)
            wr16(0x24E34 + w * 2, rd16(chans[0] + w * 2));
        wr16(0xDFF096, 0x000F);
        uint16_t track = (uint16_t)(rd16(G + 8) & 0x0F);
        uint32_t songs = G + 12 + (uint32_t)((track - 1) << 4);
        for (int i = 0; i < 4; i++) {
            wr32(chans[i] + 8, rd32(songs + (uint32_t)i * 4));
            init_channel(chans[i]);
        }
        wr8(G + 0, 0);
        wr16(G + 8, 0);
        return;
    }

    if (rd16(G + 6) == 0) return;

    /* restore the saved bank */
    for (unsigned w = 0; w < 128; w++)
        wr16(chans[0] + w * 2, rd16(0x24E34 + w * 2));
    for (int i = 0; i < 4; i++) {
        uint32_t s = chans[i];
        wr16(0xDFF096, rd16(s + 46));
        uint32_t paula = rd32(s);
        wr16(paula + 6, rd16(s + 36));
        uint32_t sample = rd32(rd32(s + 4));
        wr32(paula, rd32(sample));
        wr16(paula + 4, rd16(sample + 4));
        wr16(paula + 8, rd8(s + 57));
    }
    wr16(G + 10, 0); wr16(G + 6, 0); wr8(G + 0, 0);
}

/* LODGAM $24DDE SelectMusic */
static void select_music(uint16_t track)
{
    wr8(G + 2, 0);
    if (rd8(G + 3) != 0) { wr8(G + 1, rd8(G + 3)); wr8(G + 3, 0); }
    if (rd8(G + 4) != 0) return;
    if (rd16(G + 10) != 0) return;
    wr16(G + 8, track);
    wr8(G + 0, 1);
    if (track != 1) wr16(G + 10, 1);
}

/* LODGAM $24D6E PlaySoundEffect */
static void play_sound_effect(uint16_t sound)
{
    if (rd8(0x251FD) != 0) return;
    uint32_t channel = rd32(0x24E22 + (uint32_t)((sound & 0x0030) >> 2));
    if (rd8(channel + 61) != 0) return;
    wr8(channel + 58, 1);
    wr16(0xDFF096, rd16(channel + 46));
    uint32_t entry = 0x2539C + (uint32_t)((sound & 0x000F) * 12);
    wr8(channel + 60, rd8(entry + 11));
    wr8(channel + 59, 1);
    uint32_t paula = rd32(channel);
    wr32(paula, rd32(entry));
    wr16(paula + 4, rd16(entry + 4));
    wr16(paula + 6, rd16(entry + 6));
    wr16(paula + 8, rd16(entry + 8));
}

/* ---------------- LODMUS menu engine ($3D800 overlay) ----------------
 * The title/attract music driver, ported from asm/lodmus.asm (IRA of
 * original/modules/LODMUS.bin at $3D800; discover_code entries incl. the
 * CIA-B interrupt $3DDA2).  Same Ron Klaren engine family as LODGAM with a
 * few differences kept faithfully: no pause/mute byte, no sfx-bank swap
 * (commands $83 AND $85 stop the song outright), zero-duration notes
 * continue the command loop, and the volume envelope clamps to the master
 * byte at $3DF16+1 (which the stop request fades to zero at half tick
 * rate).  Channel state 62 B at $3DF3C/$3DF7A/$3DFB8/$3DFF6, driver state
 * $3DF16 (+0 stopped, +2 stop request, +3 saved master, +4 track request,
 * +6 song table 16 B/track: track 1 = the menu tune, pre-armed in the
 * module so the $3D800 entry starts it), periods $3DE84, instruments
 * $3E118 (32 B), arpeggio tables $3E088 (12 B), speech sfx descriptors
 * $3E038 (7 x 12 B into LODSPE at $246F0).  CIA-B latch $2500 ->
 * 709379/9472 = 74.90 Hz tick. */
#define MG      UINT32_C(0x3DF16)
#define MTOGGLE UINT32_C(0x3DF14)
#define MPER    UINT32_C(0x3DE84)
#define MINSTR  UINT32_C(0x3E118)
#define MARP    UINT32_C(0x3E088)
#define MSFX    UINT32_C(0x3E038)
static const uint32_t mchans[4] = { 0x3DF3C, 0x3DF7A, 0x3DFB8, 0x3DFF6 };

static int lodmus_resident(void)
{ return rd16(0x3D800) == 0x4EB9 && rd32(0x3D802) == UINT32_C(0x0003D830); }

static void menu_init_channel(uint32_t s)             /* LAB_3D852 */
{
    static const uint8_t clr[] = { 48, 49, 50, 51, 52, 57, 53, 55, 54, 59, 60 };
    for (size_t i = 0; i < sizeof clr; i++) wr8(s + clr[i], 0);
    wr16(s + 40, 0); wr16(s + 42, 0); wr16(s + 44, 0);
    wr32(s + 20, 0); wr32(s + 24, 0); wr32(s + 28, 0);
    wr32(s + 4, MINSTR);
    uint32_t sample = rd32(MINSTR);
    uint32_t paula = rd32(s);
    wr32(paula, rd32(sample));
    wr16(paula + 4, rd16(sample + 4));
    uint32_t seq = rd32(s + 8);
    wr32(s + 12, seq);
    wr32(s + 16, rd32(seq));
    wr16(s + 32, rd16(seq + 6));
    wr16(s + 34, (uint16_t)(rd16(seq + 10) - 1));
}

static void menu_channel_update(uint32_t s)           /* LAB_3D8D2 */
{
    uint32_t a1 = rd32(s + 4);
    uint32_t a2 = rd32(s + 16);
    const uint32_t a3 = rd32(s);

    if (rd8(s + 59) != 0) {                           /* speech borrow running */
        if (rd8(s + 60) != 0) wr8(s + 60, (uint8_t)(rd8(s + 60) - 1));
        else {
            wr8(s + 59, 0);
            uint32_t sample = rd32(a1);
            wr32(a3, rd32(sample));
            wr16(a3 + 4, rd16(sample + 4));
        }
    }
    wr16(0xDFF096, (uint16_t)(0x8000 | rd16(s + 46)));
    if (rd8(s + 58) != 0) { wr8(s + 58, 0); wr16(a3 + 4, 1); }

    if (rd8(s + 49) != 0) {                           /* note still sounding */
        wr8(s + 49, (uint8_t)(rd8(s + 49) - 1));
        if (rd8(s + 59) != 0) return;

        if (rd8(a1 + 9) != 0) {                       /* waveform modulation */
            if (rd8(s + 54) != 0) wr8(s + 54, (uint8_t)(rd8(s + 54) - 1));
            else {
                wr8(s + 54, (uint8_t)(rd8(a1 + 9) - 1));
                uint32_t sample = rd32(a1);
                uint16_t length = rd16(sample + 6);
                length = (uint16_t)((length & 0xFF00) | ((length - rd8(a1 + 10)) & 0xFF));
                uint32_t wave = rd32(sample) + (int16_t)length;
                uint8_t position = rd8(a1 + 24);
                wr8(wave + position, rd8(a1 + 22));
                int flip;
                if ((int8_t)rd8(a1 + 23) >= 0) { position++; flip = (uint8_t)(rd8(a1 + 10) * 2) == position; }
                else { position--; flip = position == 0; }
                if (flip) { wr8(a1 + 23, (uint8_t)~rd8(a1 + 23)); wr8(a1 + 22, (uint8_t)~rd8(a1 + 22)); }
                wr8(a1 + 24, position);
            }
        }

        if (rd8(s + 55) != 0) {                       /* portamento */
            uint16_t step = rd8(s + 55);
            uint16_t target = rd16(s + 38);
            uint16_t period = rd16(s + 36);
            if ((int16_t)(target - period) < 0) {
                period = (uint16_t)(period - step);
                if ((int16_t)(target - period) >= 0) period = target;
            } else {
                period = (uint16_t)(period + step);
                if ((int16_t)(target - period) < 0) period = target;
            }
            wr16(s + 36, period);
            wr16(a3 + 6, period);
        } else {                                      /* arpeggio */
            uint16_t index = rd16(s + 44);
            uint16_t note = (uint16_t)((rd8(s + 20 + index) + rd8(s + 48)) & 0xFF);
            note = (uint16_t)(note + rd16(s + 32));
            uint16_t period = rd16(MPER + (uint16_t)(note * 2));
            wr16(s + 36, period);
            wr16(a3 + 6, period);
            index = (uint16_t)(index - 1);
            if ((int16_t)index < 0) index = (uint16_t)(index + 12);
            wr16(s + 44, index);
        }

        if (rd8(a1 + 11) != 0) {                      /* vibrato */
            if (rd8(s + 53) != 0) wr8(s + 53, (uint8_t)(rd8(s + 53) - 1));
            else {
                uint16_t position = rd16(s + 42);
                uint32_t table = rd32(rd32(a1 + 4));
                int16_t sample = (int8_t)rd8(table + position);
                int16_t depth = rd8(a1 + 12);
                uint16_t period = (uint16_t)(sample * depth + (int16_t)rd16(s + 36));
                wr16(a3 + 6, period);
                uint16_t next = (uint16_t)(position - rd8(a1 + 11));
                wr16(s + 42, next);
                if ((int16_t)next < 0) {
                    uint32_t vib = rd32(a1 + 4);
                    wr16(s + 42, (uint16_t)(rd16(vib + 4) * 2));
                }
            }
        }

        /* volume envelope (clamped to the fading master byte MG+1) */
        uint8_t count = (uint8_t)(rd8(s + 52) - 1);
        wr8(s + 52, count);
        if ((int8_t)count >= 0) return;
        wr8(s + 52, rd8(s + 51));
        uint16_t stage = rd16(s + 40);
        uint8_t level = rd8(a1 + 14 + stage);
        uint8_t rate = rd8(a1 + 18 + stage);
        uint8_t volume = rd8(s + 57);
        int advance = 0;
        if ((int8_t)(uint8_t)(level - volume) < 0) {
            volume = (uint8_t)(volume - rate);
            if ((int8_t)(uint8_t)(level - volume) >= 0) { volume = level; advance = 1; }
        } else {
            uint8_t mvol = rd8(MG + 1);
            volume = (uint8_t)(volume + rate);
            if ((int8_t)(uint8_t)(volume - mvol) >= 0) { volume = mvol; advance = 1; }
            else if ((int8_t)(uint8_t)(level - volume) < 0) { volume = level; advance = 1; }
        }
        if (advance && stage != 3) stage = (uint16_t)(stage + 1);
        wr16(s + 40, stage);
        wr8(s + 57, volume);
        wr16(a3 + 8, (uint16_t)(volume & 0x3F));
        return;
    }

    /* note ended: read commands until a note (LAB_3DAC2) */
    wr8(s + 55, 0);
    uint8_t vdelay = rd8(a1 + 13);
    if (vdelay != 0) wr8(s + 53, (uint8_t)((vdelay << 2) - 1));

    for (int guard = 0; guard < 64; guard++) {
        if (rd8(a2) == 0x80) {                        /* arpeggio table */
            wr16(s + 44, 0);
            uint32_t entry = MARP + (uint32_t)rd8(a2 + 1) * 12;
            wr32(s + 20, rd32(entry)); wr32(s + 24, rd32(entry + 4)); wr32(s + 28, rd32(entry + 8));
            a2 += 2;
        }
        if (rd8(a2) == 0x81) {                        /* portamento command */
            wr16(s + 40, 0);
            uint16_t note = rd8(a2 + 1);
            wr8(s + 50, (uint8_t)note);
            note = (uint16_t)(note + rd16(s + 32));
            wr16(s + 38, rd16(MPER + (uint16_t)(note * 2)));
            wr8(s + 55, rd8(a2 + 2));
            wr8(s + 49, (uint8_t)((rd8(a2 + 3) << 2) - 1));
            a2 += 4;
            wr32(s + 16, a2);                         /* LAB_3DC8C */
            wr16(a3 + 6, rd16(s + 36));
            return;
        }
        if (rd8(a2) == 0x82) {                        /* instrument change */
            wr8(s + 57, 0);
            wr16(s + 40, 0);
            a1 = MINSTR + ((uint32_t)rd8(a2 + 1) << 5);
            wr32(s + 4, a1);
            if (rd8(s + 59) == 0) {
                wr16(0xDFF096, rd16(s + 46));
                uint32_t sample = rd32(a1);
                wr32(a3, rd32(sample));
                wr16(a3 + 4, rd16(sample + 4));
            }
            a2 += 2;
        }
        if (rd8(a2) == 0x83 || rd8(a2) == 0x85) {     /* end of song: stop */
            wr8(MG + 0, 1);
            return;
        }
        if (rd8(a2) == 0x84) { wr8(s + 51, rd8(a2 + 1)); a2 += 2; }
        if ((int8_t)rd8(a2) < 0) {                    /* end of pattern */
            uint32_t entry;
            if (rd16(s + 34) != 0) {
                wr16(s + 34, (uint16_t)(rd16(s + 34) - 1));
                entry = rd32(s + 12);
                a2 = rd32(entry);
                wr32(s + 16, a2);
                wr16(s + 32, rd16(entry + 6));
                continue;
            }
            wr32(s + 12, rd32(s + 12) + 12);
            entry = rd32(s + 12);
            if ((int8_t)rd8(entry) < 0) { entry = rd32(s + 8); wr32(s + 12, entry); }
            a2 = rd32(entry);
            wr32(s + 16, a2);
            wr16(s + 32, rd16(entry + 6));
            wr16(s + 34, (uint16_t)(rd16(entry + 10) - 1));
            continue;
        }
        /* a note (LAB_3DC12) */
        uint8_t duration = rd8(a2 + 1);
        if (duration == 0) {                          /* pitch change only, keep reading */
            uint16_t note = rd8(a2);
            wr8(s + 48, (uint8_t)note);
            note = (uint16_t)(note + rd16(s + 32));
            wr16(s + 36, rd16(MPER + (uint16_t)(note * 2)));
            a2 += 2;
            continue;
        }
        wr8(s + 49, (uint8_t)((duration << 2) - 1));
        wr16(s + 40, 0);
        uint16_t note = rd8(a2);
        wr8(s + 48, (uint8_t)note);
        note = (uint16_t)(note + rd16(s + 32));
        wr16(s + 36, rd16(MPER + (uint16_t)(note * 2)));
        if (rd8(s + 59) != 0) {
            a2 += 2;
            wr32(s + 16, a2);
            return;
        }
        wr16(0xDFF096, rd16(s + 46));
        uint32_t sample = rd32(a1);
        wr32(a3, rd32(sample));
        wr16(a3 + 4, rd16(sample + 4));
        a2 += 2;
        if (rd8(a1 + 8) != 0) wr8(s + 58, 1);
        wr32(s + 16, a2);                             /* LAB_3DC8C */
        wr16(a3 + 6, rd16(s + 36));
        return;
    }
}

static void menu_interrupt(void)                      /* LAB_3DDA2 */
{
    if (rd8(MG + 0) == 0) {                           /* playing */
        if (rd8(MG + 2) != 0) {                       /* stop requested: fade */
            if (rd8(MG + 1) != 0) {
                uint8_t t = (uint8_t)(rd8(MTOGGLE) ^ 1);   /* EORI.B #1,$3DF14: fade at half tick rate */
                wr8(MTOGGLE, t);
                if (t == 0) wr8(MG + 1, (uint8_t)(rd8(MG + 1) - 1));
            } else {
                wr8(MG + 0, 1);
                wr8(MG + 1, rd8(MG + 3));
                wr8(MG + 2, 0); wr8(MG + 3, 0);
                for (int i = 0; i < 4; i++) wr16(0xDFF0A0 + (uint32_t)i * 16 + 8, 0);
                return;
            }
        }
        for (int i = 0; i < 4; i++) menu_channel_update(mchans[i]);
        return;
    }
    if (rd16(MG + 4) != 0) {                          /* track request */
        wr16(0xDFF096, 0x000F);
        uint16_t track = (uint16_t)(rd16(MG + 4) & 0x0F);
        uint32_t row = MG + 6 + (uint32_t)(track - 1) * 16;
        for (int i = 0; i < 4; i++) {
            wr32(mchans[i] + 8, rd32(row + (uint32_t)i * 4));
            menu_init_channel(mchans[i]);
        }
        wr8(MG + 0, 0);
        wr16(MG + 4, 0);
    }
}

static void menu_start(void)                          /* $3D800: init + play the armed track */
{
    for (int i = 0; i < 4; i++) menu_init_channel(mchans[i]);
    wr16(0xDFF096, 0x800F);
    wr8(MG + 0, 0); wr8(MG + 2, 0); wr8(MG + 3, 0);
    wr16(MG + 4, 0);
    wr8(MG + 1, music_enabled ? 0x3F : 0);
}

static void menu_speech(uint32_t ptr, uint16_t words, uint16_t per, uint16_t vol, uint8_t ticks)
{                                                     /* LAB_3DD22 shape, channel 0 */
    if (!sfx_enabled) return;
    uint32_t s = mchans[0];
    wr8(s + 58, 1);
    wr16(0xDFF096, rd16(s + 46));
    wr8(s + 60, ticks);
    wr8(s + 59, 1);
    uint32_t paula = rd32(s);
    wr32(paula, ptr);
    wr16(paula + 4, words);
    wr16(paula + 6, per);
    wr16(paula + 8, vol);
    wr16(0xDFF096, (uint16_t)(0x8000 | rd16(s + 46)));
}

/* ---------------- public API ---------------- */
static AudioStream stream;
static int ready;
static double cia_next_sample;
static uint64_t audio_sample_clock;
static int16_t sbuf[2048];

static int lodgam_resident(void)
{ return rd16(0x246F0) == 0x4EF9 && rd32(0x246F2) == UINT32_C(0x00024C6E); }

int audio_init(void)
{
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return -1;
    SetAudioStreamBufferSizeDefault(1024);
    stream = LoadAudioStream(OUT_RATE, 16, 2);
    PlayAudioStream(stream);
    ready = 1;
    return 0;
}

void audio_close(void)
{
    if (!ready) return;
    StopAudioStream(stream);
    UnloadAudioStream(stream);
    CloseAudioDevice();
    ready = 0;
}

static int amode;                        /* 0 silent, 1 LODGAM (game), 2 LODMUS (title/attract) */

static void audio_clock_reset(void)
{
    cia_next_sample = 0;
    audio_sample_clock = 0;
}

void audio_start_game(void)
{
    if (!lodgam_resident()) return;
    audio_stop();
    /* $24C6E AudioSystemInit + LAB_A42 track select */
    for (int i = 0; i < 4; i++) init_channel(chans[i]);
    wr16(0xDFF096, 0x800F);
    wr8(G + 4, music_enabled ? 0 : 1);
    wr8(0x251FD, sfx_enabled ? 0 : 1);
    select_music(1);
    amode = 1;
    audio_clock_reset();
}

void audio_start_title(void)
{
    if (!lodmus_resident()) return;
    audio_stop();
    menu_start();
    amode = 2;
    audio_clock_reset();
}

void audio_title_speech(void)
{
    /* the boot's "welcome to Battle Squadron": the whole LODSPE payload as
     * one Paula one-shot (exact descriptor from the runtime.c bootstrap) */
    if (amode != 2 || !lodmus_resident()) return;
    menu_speech(0x246F0, 0x17CD, 0x01AC, 64, 110);
}

void audio_track(int track)
{
    if (!lodgam_resident() || track < 1 || track > 10) return;
    select_music((uint16_t)track);
}

void audio_stop(void)
{
    wr16(0xDFF096, 0x000F);
    for (int i = 0; i < 4; i++) { pch[i].active = 0; wr16(0xDFF0A0 + (uint32_t)i * 16 + 8, 0); }
    amode = 0;
    audio_clock_reset();
}

void audio_sfx(int n)
{
    if (amode != 1 || !lodgam_resident()) return;
    if (getenv("BS_SFX_LOG")) {                      /* which effect numbers the engine actually asks for */
        static FILE *lf;
        if (!lf) lf = fopen(getenv("BS_SFX_LOG"), "w");
        if (lf) { fprintf(lf, "%d\n", n); fflush(lf); }
    }
    if (n >= 0) { play_sound_effect((uint16_t)n); return; }
    /* engine jingle entries: sfx(-0xNNN) = JSR $24NNN; stubs at $2471A + 6k select track k+1 */
    int addr = 0x24000 + (-n);
    if (addr >= 0x2471A && addr <= 0x24750 && (addr - 0x2471A) % 6 == 0)
        select_music((uint16_t)((addr - 0x2471A) / 6 + 1));
}

static void audio_render_timed(int16_t *out, size_t frames)
{
    size_t done = 0;
    const double samples_per_tick = OUT_RATE * (amode == 1 ? 12544.0 : 9472.0) / 709379.0;
    while (done < frames) {
        /* This is a cumulative fractional schedule: interrupt at sample zero,
         * then alternate integer spans around the exact CIA period without
         * accumulating rounding drift. */
        if (audio_sample_clock >= (uint64_t)cia_next_sample) {
            if (amode == 1) music_interrupt(); else menu_interrupt();
            cia_next_sample += samples_per_tick;
        }
        uint64_t until_tick = (uint64_t)cia_next_sample - audio_sample_clock;
        size_t chunk = frames - done;
        if (until_tick < chunk) chunk = (size_t)until_tick;
        if (chunk == 0) continue;
        paula_render(out + done * 2, chunk);
        done += chunk;
        audio_sample_clock += chunk;
    }
}

/* BS_AUDIO_WAV=path: tee the mix to a WAV so a run can be compared against the
 * oracle's --dump-audio capture (header patched on exit). */
static FILE *tee_f;
static long tee_frames;
static void audio_tee_close(void)
{
    if (!tee_f) return;
    long data = tee_frames * 4;
    uint8_t h[44];
    memcpy(h, "RIFF", 4); uint32_t riff = (uint32_t)(36 + data); memcpy(h + 4, &riff, 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    uint32_t v = 16; memcpy(h + 16, &v, 4);
    uint16_t w = 1; memcpy(h + 20, &w, 2); w = 2; memcpy(h + 22, &w, 2);
    v = OUT_RATE; memcpy(h + 24, &v, 4); v = OUT_RATE * 4; memcpy(h + 28, &v, 4);
    w = 4; memcpy(h + 32, &w, 2); w = 16; memcpy(h + 34, &w, 2);
    memcpy(h + 36, "data", 4); v = (uint32_t)data; memcpy(h + 40, &v, 4);
    fseek(tee_f, 0, SEEK_SET); fwrite(h, 1, 44, tee_f); fclose(tee_f); tee_f = NULL;
}
long audio_capture_frames(void) { return tee_frames; }
static void audio_tee(const int16_t *pcm, size_t frames)
{
    static int checked;
    if (!checked) {
        const char *e = getenv("BS_AUDIO_WAV");
        checked = 1;
        if (e && (tee_f = fopen(e, "wb"))) {
            uint8_t z[44] = { 0 };
            fwrite(z, 1, 44, tee_f);
            atexit(audio_tee_close);
        }
    }
    if (!tee_f) return;
    fwrite(pcm, 4, frames, tee_f);
    tee_frames += (long)frames;
}

void audio_tick(void)
{
    static long fed, calls;
    static int dbg = -1;
    if (dbg < 0) dbg = getenv("BS_AUDIO_DEBUG") != NULL;
    if (!ready || amode == 0 || (amode == 1 && !lodgam_resident()) || (amode == 2 && !lodmus_resident())) {
        if (dbg && (++calls % 50) == 0)
            fprintf(stderr, "audio: idle (ready=%d amode=%d)\n", ready, amode);
        return;
    }
    while (IsAudioStreamProcessed(stream)) {
        audio_render_timed(sbuf, 1024);
        UpdateAudioStream(stream, sbuf, 1024);
        audio_tee(sbuf, 1024);              /* BS_AUDIO_WAV: same mix the speakers get */
        fed++;
    }
    if (dbg && (++calls % 50) == 0) {
        double rms = 0;
        for (int i = 0; i < 2048; i++) rms += (double)sbuf[i] * sbuf[i];
        fprintf(stderr, "audio: %ld buffers fed, last rms=%.0f, ch act %d%d%d%d per %d %d %d %d\n",
                fed, sqrt(rms / 2048),
                pch[0].active, pch[1].active, pch[2].active, pch[3].active,
                pch[0].period, pch[1].period, pch[2].period, pch[3].period);
    }
}

void audio_set(float master, int music_on, int sfx_on)
{
    master_gain = master;
    music_enabled = music_on;
    sfx_enabled = sfx_on;
    if (amode == 2 && lodmus_resident()) {
        wr8(MG + 1, music_on ? 0x3F : 0);
        if (!music_on)
            for (int i = 0; i < 4; i++)
                if (rd8(mchans[i] + 59) == 0) wr16(0xDFF0A0 + (uint32_t)i * 16 + 8, 0);
        return;
    }
    if (amode == 1 && lodgam_resident()) {
        uint8_t cur_off = rd8(G + 4);
        if (cur_off != (music_on ? 0 : 1)) {
            wr8(G + 4, music_on ? 0 : 1);
            if (!music_on)
                for (int i = 0; i < 4; i++) wr16(0xDFF0A0 + (uint32_t)i * 16 + 8, 0);
        }
        wr8(0x251FD, sfx_on ? 0 : 1);
    }
}
