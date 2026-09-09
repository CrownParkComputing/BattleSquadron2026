/* Headless Hybris runner: purely file based.
 *
 * Hybris ships no self-contained LOADER, so the WHDLoad host had to be
 * WHDLoad -- and stalled on the slave's undocumented patch list.  This route
 * needs neither: the loader executable and the sixteen game files are
 * ordinary files on disk, and the loader's own track reads are intercepted
 * and served from them.  Nothing here emulates a floppy. */
#include "exeboot.h"
#include "amiga.h"
#include "hybris_loader.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"

static void write_le16(FILE *file, unsigned value)
{
    fputc(value & 0xff, file); fputc((value >> 8) & 0xff, file);
}

static void write_le32(FILE *file, unsigned value)
{
    write_le16(file, value); write_le16(file, value >> 16);
}

/* Same 44.1 kHz stereo WAV the Battle Squadron runner writes, so the two
 * can be compared with the same tools. */
static void write_wav_header(FILE *file, unsigned frames)
{
    unsigned bytes = frames * 4;
    rewind(file);
    fwrite("RIFF", 1, 4, file); write_le32(file, 36 + bytes);
    fwrite("WAVEfmt ", 1, 8, file); write_le32(file, 16);
    write_le16(file, 1); write_le16(file, 2);
    write_le32(file, 44100); write_le32(file, 44100 * 4);
    write_le16(file, 4); write_le16(file, 16);
    fwrite("data", 1, 4, file); write_le32(file, bytes);
}

static void write_ppm(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) { perror(path); return; }
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint32_t pixel = framebuf[i];
        uint8_t rgb[3] = { (uint8_t)(pixel >> 16), (uint8_t)(pixel >> 8),
                           (uint8_t)pixel };
        fwrite(rgb, 1, 3, file);
    }
    fclose(file);
}

int main(int argc, char **argv)
{
    const char *exe = "original/hybris/hybris.exe";
    const char *data = "original/hybris";
    long frames = 500;
    bool trace = false;
    const char *dump = NULL, *ppm = NULL, *wav = NULL, *ppm_seq = NULL;
    long ppm_every = 10;
    long fire_from = -1, left_from = -1, video_from = -1;
    long play_from = -1, second_from = -1, space_from = -1;
    long fire_period = 0;   /* 0 = hold, N = toggle every N frames */
    long key_at = -1; unsigned key_code = 0x44;
    const char *watch = NULL; uint32_t watch_at = 0, watch_len = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--exe") && i + 1 < argc) exe = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atol(argv[++i]);
        else if (!strcmp(argv[i], "--data") && i + 1 < argc)
            data = argv[++i];
        else if (!strcmp(argv[i], "--ppm") && i + 1 < argc)
            ppm = argv[++i];
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc)
            wav = argv[++i];
        else if (!strcmp(argv[i], "--ppm-seq") && i + 1 < argc)
            ppm_seq = argv[++i];
        else if (!strcmp(argv[i], "--ppm-every") && i + 1 < argc)
            ppm_every = atol(argv[++i]);
        else if (!strcmp(argv[i], "--shift") && i + 1 < argc)
            bs_playfield_shift = (int)atol(argv[++i]);
        else if (!strcmp(argv[i], "--fire-from") && i + 1 < argc)
            fire_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--left-from") && i + 1 < argc)
            left_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--video-from") && i + 1 < argc)
            video_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--play-from") && i + 1 < argc)
            play_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--second-from") && i + 1 < argc)
            second_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--space-from") && i + 1 < argc)
            space_from = atol(argv[++i]);
        else if (!strcmp(argv[i], "--fire-period") && i + 1 < argc)
            fire_period = atol(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 2 < argc) {
            watch_at = (uint32_t)strtoul(argv[++i], NULL, 16);
            watch_len = (uint32_t)strtoul(argv[++i], NULL, 16);
            watch = "build/watch.bin";
        }
        else if (!strcmp(argv[i], "--key-at") && i + 2 < argc) {
            key_at = atol(argv[++i]);
            key_code = (unsigned)strtoul(argv[++i], NULL, 16);
        }
        else if (!strcmp(argv[i], "--trace")) trace = true;
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc)
            dump = argv[++i];
        else {
            fprintf(stderr,
                    "usage: %s [--exe HUNKFILE] [--data DIR] [--frames N]\n"
                    "       [--ppm FILE] [--dump FILE] [--trace]\n",
                    argv[0]);
            return 2;
        }
    }
    ExeBoot boot;
    if (!exeboot(exe, &boot)) return 1;
    if (!hybris_loader_install(data)) return 1;
    amiga_enable_video(video_from < 0);
    fprintf(stderr, "booted %s: %d hunk(s), entry $%06x, ends $%06x\n",
            exe, boot.hunks, boot.entry, boot.end);

    FILE *audio_dump = NULL;
    unsigned audio_frames = 0;
    if (wav) {
        audio_dump = fopen(wav, "wb+");
        if (!audio_dump) { perror(wav); return 1; }
        write_wav_header(audio_dump, 0);
    }

    FILE *watch_file = NULL;
    if (watch) {
        watch_file = fopen(watch, "wb");
        if (!watch_file) { perror(watch); return 1; }
    }

    long last_reads = -1;
    for (long frame = 0; frame < frames && !amiga_stopped(); frame++) {
        /* Tap fire so the attract screen starts a real game: the demo does
         * not necessarily draw everything a game in progress does. */
        if (video_from >= 0 && frame == video_from) amiga_enable_video(true);
        if (key_at >= 0 && (frame == key_at || frame == key_at + 3))
            amiga_key_event((uint8_t)key_code, frame != key_at);
        if (space_from >= 0 && frame >= space_from && frame % 25 == 0)
            amiga_key_event(0x40, frame % 50 != 0);   /* SPACE down / up */
        if (second_from >= 0 && frame >= second_from) {
            uint8_t held = (frame / 25) % 2 ? 0x20 : 0x00;
            joy_state[1] = (uint8_t)((joy_state[1] & ~0x20) | held);
            joy_state[0] = held;
        }
        if (fire_from >= 0 && frame >= fire_from)
            joy_state[1] = fire_period == 0 ? 0x10
                         : ((frame / fire_period) % 2 ? 0x10 : 0x00);
        if (left_from >= 0 && frame >= left_from)
            joy_state[1] = (uint8_t)((joy_state[1] & 0x10) | 0x04);
        if (play_from >= 0 && frame >= play_from) {
            /* Crude but enough to exercise a real game: keep firing and
             * weave about so the ship collides, dies and respawns. */
            long phase = (frame - play_from) % 200;
            uint8_t stick = (frame / 8) % 2 ? 0x10 : 0x00;
            if (phase < 50) stick |= 0x04;
            else if (phase < 100) stick |= 0x08;
            else if (phase < 150) stick |= 0x01;
            else stick |= 0x02;
            joy_state[1] = stick;
        }
        amiga_run_frame();
        if (audio_dump) {
            int16_t audio[882 * 2];
            amiga_audio_frame();
            amiga_audio_pull(audio, 882);
            fwrite(audio, sizeof audio[0], 882 * 2, audio_dump);
            audio_frames += 882;
        }
        if (getenv("BS_TRACE_REPLACE") && bs_sprite_draw_count) {
            fprintf(stderr, "frame %ld: %d replacement(s)", frame,
                    bs_sprite_draw_count);
            for (int i = 0; i < bs_sprite_draw_count && i < 4; i++)
                fprintf(stderr, "  id%d %dx%d at (%d,%d)",
                        bs_sprite_draws[i].id, bs_sprite_draws[i].width,
                        bs_sprite_draws[i].height, bs_sprite_draws[i].x,
                        bs_sprite_draws[i].y);
            fprintf(stderr, "\n");
        }
        if (watch_file && frame > frames - 200)
            fwrite(chip + watch_at, 1, watch_len, watch_file);
        if (ppm_seq && bs_frame_no % ppm_every == 0) {
            char path[700];
            snprintf(path, sizeof path, "%s%05ld.ppm", ppm_seq, bs_frame_no);
            write_ppm(path);
        }
        if (trace || frame < 4 || frame % 100 == 0 ||
            hybris_load_count != last_reads) {
            uint16_t bplcon0, dmacon, diwstrt, diwstop;
            amiga_display_state(&bplcon0, &dmacon, &diwstrt, &diwstop);
            fprintf(stderr, "frame %4ld pc=$%06x loads=%ld bplcon0=$%04x "
                    "(%d planes) dmacon=$%04x diw=$%04x/$%04x\n",
                    frame, m68k_get_reg(NULL, M68K_REG_PC), hybris_load_count,
                    bplcon0, (bplcon0 >> 12) & 7, dmacon, diwstrt, diwstop);
            last_reads = hybris_load_count;
        }
    }

    if (watch_file) { fclose(watch_file);
        fprintf(stderr, "wrote build/watch.bin\n"); }

    if (watch_file) {
        fclose(watch_file);
        fprintf(stderr, "wrote build/watch.bin\n");
    }

    if (audio_dump) {
        write_wav_header(audio_dump, audio_frames);
        fclose(audio_dump);
        fprintf(stderr, "wrote %s (%u sample frames)\n", wav, audio_frames);
    }

    if (ppm) write_ppm(ppm);

    if (dump) {
        FILE *file = fopen(dump, "wb");
        if (!file) { perror(dump); return 1; }
        fwrite(chip, 1, CHIP_SIZE, file);
        fclose(file);
        fprintf(stderr, "wrote %s (%d bytes of chip RAM)\n", dump, CHIP_SIZE);
        char palette_path[700];
        snprintf(palette_path, sizeof palette_path, "%s.pal", dump);
        file = fopen(palette_path, "wb");
        if (file) {
            uint16_t palette[32];
            amiga_palette(palette);
            for (int i = 0; i < 32; i++) {
                fputc(palette[i] >> 8, file);
                fputc(palette[i] & 0xff, file);
            }
            fclose(file);
            fprintf(stderr, "wrote %s (live palette)\n", palette_path);
        }
    }

    printf("hybris: action bit via key %ld, via POT %ld\n",
           hybris_action_key, hybris_action_pot);
    printf("hybris: %ld frames, %ld file loads (%ld bytes), %ld blits, "
           "%ld copper moves, %ld Paula writes\n", bs_frame_no,
           hybris_load_count, hybris_load_bytes, bs_blit_count,
           bs_copper_moves, bs_audio_writes);
    return 0;
}
