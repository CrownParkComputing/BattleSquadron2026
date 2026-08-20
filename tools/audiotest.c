/* audiotest -- headless check of the LODGAM sequencer + Paula port: renders
 * N seconds of a track (with optional SFX injections) to a WAV. */
#define AUDIOTEST_MAIN
#include "../src/audio.c"
#include "../src/bsdata.h"
#include <stdlib.h>

static BsData data;
int main(int argc, char **argv)
{
    int track = argc > 1 ? atoi(argv[1]) : 1;
    double secs = argc > 2 ? atof(argv[2]) : 8.0;
    const char *out = argc > 3 ? argv[3] : "build/audiotest.wav";
    int sfx_n = argc > 4 ? atoi(argv[4]) : -1;
    if (bs_open(&data, "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data")) return 1;
    bs_chip = data.chip;
    int menu = track >= 100;                          /* 101/102 = LODMUS menu track 1/2 */
    if (menu) {
        if (bs_load_module(&data, "LODMUS") || bs_load_module(&data, "LODSPE")) return 1;
        if (!lodmus_resident()) { fprintf(stderr, "LODMUS not resident\n"); return 1; }
        menu_start();
        if (track != 101) { wr16(MG + 4, (uint16_t)(track - 100)); wr8(MG + 0, 1); }
    } else {
        if (!lodgam_resident()) { fprintf(stderr, "LODGAM not resident\n"); return 1; }
        for (int i = 0; i < 4; i++) init_channel(chans[i]);
        wr16(0xDFF096, 0x800F);
        select_music((uint16_t)track);
    }
    long frames = (long)(secs * OUT_RATE);
    int16_t *pcm = malloc((size_t)frames * 4);
    long done = 0;
    double tickper = OUT_RATE * (menu ? 9472.0 : 12544.0) / 709379.0;   /* samples per CIA tick */
    double next = 0;
    long tick = 0;
    while (done < frames) {
        if (menu) menu_interrupt(); else music_interrupt();
        tick++;
        if (sfx_n >= 0 && tick == 120) {
            if (menu) menu_speech(0x246F0, 0x17CD, 0x01AC, 64, 110);
            else play_sound_effect((uint16_t)sfx_n);
        }
        next += tickper;
        long upto = (long)next; if (upto > frames) upto = frames;
        paula_render(pcm + done * 2, (size_t)(upto - done));
        done = upto;
    }
    /* wav */
    FILE *f = fopen(out, "wb");
    uint32_t dlen = (uint32_t)frames * 4, rate = OUT_RATE;
    uint32_t riff = 36 + dlen, fmt16 = 16; uint16_t pcmf = 1, ch = 2, bits = 16;
    uint32_t brate = rate * 4; uint16_t balign = 4;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmt16, 4, 1, f); fwrite(&pcmf, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&rate, 4, 1, f); fwrite(&brate, 4, 1, f); fwrite(&balign, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dlen, 4, 1, f); fwrite(pcm, 4, frames, f); fclose(f);
    /* stats */
    double rms = 0; long nz = 0;
    for (long i = 0; i < frames * 2; i++) { rms += (double)pcm[i] * pcm[i]; if (pcm[i]) nz++; }
    printf("track %d: %.1fs rms=%.0f nonzero=%.1f%%\n", track, secs, sqrt(rms / (frames * 2)), 100.0 * nz / (frames * 2));
    return 0;
}
