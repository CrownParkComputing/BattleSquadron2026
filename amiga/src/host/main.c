#include "amiga.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static long parse_frames(const char *text)
{
    char *end = NULL;
    errno = 0;
    long value = strtol(text, &end, 10);
    if (errno || !end || *end || value < 0) {
        fprintf(stderr, "invalid frame count: %s\n", text);
        exit(2);
    }
    return value;
}

static int dump_frame(const char *path)
{
    FILE *file = fopen(path, "wb");
    if (!file) {
        perror(path);
        return 1;
    }
    fprintf(file, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int pixel = 0; pixel < SCREEN_W * SCREEN_H; pixel++) {
        uint32_t value = framebuf[pixel];
        fputc(value & 0xff, file);
        fputc((value >> 8) & 0xff, file);
        fputc((value >> 16) & 0xff, file);
    }
    if (fclose(file)) {
        perror(path);
        return 1;
    }
    fprintf(stderr, "native: dumped %s\n", path);
    return 0;
}

static void write_le16(FILE *file, unsigned value)
{
    fputc(value & 0xff, file);
    fputc((value >> 8) & 0xff, file);
}

static void write_le32(FILE *file, unsigned value)
{
    write_le16(file, value);
    write_le16(file, value >> 16);
}

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

int main(int argc, char **argv)
{
    const char *data_dir = "original/whdload/BattleSquadron/data";
    long frames = 10;
    long video_from = -1;
    long expect_files = 0;
    long expect_blits = 0;
    const char *dump = NULL;
    bool mix_audio = false;
    bool autofire = false;
    bool fire = false;
    long dump_state = 0;
    const char *audio_dump_path = NULL;
    const char *frame_seq = NULL;
    long frame_every = 250;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) {
            data_dir = argv[++i];
        } else if (!strcmp(argv[i], "--frames") && i + 1 < argc) {
            frames = parse_frames(argv[++i]);
        } else if (!strcmp(argv[i], "--expect-files") && i + 1 < argc) {
            expect_files = parse_frames(argv[++i]);
        } else if (!strcmp(argv[i], "--expect-blits") && i + 1 < argc) {
            expect_blits = parse_frames(argv[++i]);
        } else if (!strcmp(argv[i], "--video-from") && i + 1 < argc) {
            video_from = parse_frames(argv[++i]);
        } else if (!strcmp(argv[i], "--video")) {
            video_from = 0;
        } else if (!strcmp(argv[i], "--dump-frame") && i + 1 < argc) {
            dump = argv[++i];
        } else if (!strcmp(argv[i], "--mix-audio")) {
            mix_audio = true;
        } else if (!strcmp(argv[i], "--dump-state") && i + 1 < argc) {
            dump_state = parse_frames(argv[++i]);
        } else if (!strcmp(argv[i], "--autofire")) {
            autofire = true;
        } else if (!strcmp(argv[i], "--fire")) {
            fire = true;
        } else if (!strcmp(argv[i], "--dump-frame-seq") && i + 1 < argc) {
            /* One run, many frames: re-running from zero for every capture
             * costs minutes once the attract cycle is deep. */
            frame_seq = argv[++i];
            video_from = 0;
        } else if (!strcmp(argv[i], "--dump-every") && i + 1 < argc) {
            frame_every = parse_frames(argv[++i]);
        } else if (!strcmp(argv[i], "--dump-audio") && i + 1 < argc) {
            audio_dump_path = argv[++i];
            mix_audio = true;
        } else if (!strcmp(argv[i], "--selftest")) {
            int failed = amiga_blitter_selftest();
            failed |= amiga_video_selftest();
            failed |= amiga_input_selftest();
            failed |= amiga_audio_selftest();
            return failed;
        } else {
            fprintf(stderr,
                    "usage: %s [--data DIR] [--frames N] "
                    "[--expect-files N] [--expect-blits N] "
                    "[--video|--video-from N] [--dump-frame FILE] "
                    "[--mix-audio] [--dump-audio FILE] [--autofire] [--fire] "
                    "[--dump-state N] "
                    "[--selftest]\n",
                    argv[0]);
            return 2;
        }
    }

    amiga_init(data_dir);
    FILE *audio_dump = NULL;
    unsigned audio_frames = 0;
    if (audio_dump_path) {
        audio_dump = fopen(audio_dump_path, "wb+");
        if (!audio_dump) { perror(audio_dump_path); return 1; }
        write_wav_header(audio_dump, 0);
    }
    while (bs_frame_no < frames && !amiga_stopped()) {
        /* BS_HOLD=<hex> holds a joystick state on player one, so a
         * diagnostic run can drive the ship somewhere specific. */
        {
            const char *hold = getenv("BS_HOLD");
            if (hold) joy_state[1] = (uint8_t)strtoul(hold, NULL, 16);
        }
        if (fire && !getenv("BS_HOLD"))
            joy_state[1] = 0x10;
        else if (autofire)
            joy_state[1] = (bs_frame_no % 100) < 20 ? 0x10 : 0;
        if (video_from >= 0 && bs_frame_no >= video_from)
            amiga_enable_video(true);
        /* $10B0 copies the title's selection byte at 10965(A5) into player
         * two's enable flag, and $10AA forces player one on, so clearing it
         * before the game starts is a one-player start. */
        if (getenv("BS_ONE_PLAYER")) chip[0xaad5] = 0;
        amiga_run_frame();
        if (frame_seq && bs_frame_no % frame_every == 0) {
            char path[700];
            snprintf(path, sizeof path, "%s%05ld.ppm", frame_seq, bs_frame_no);
            dump_frame(path);
        }
        if (dump_state && bs_frame_no % dump_state == 0) {
            /* Reference values for the recompilation to diff against: the
             * player record and every live active-object slot. */
            printf("scroll=%u\n", (chip[0x8000+7204]<<8)|chip[0x8000+7205]);
            printf("state frame=%ld t1078=%u mode=%u p1 x=%u y=%u "
                   "p2x=%u p2y=%u s38=%u c48=%u d49=%u inv52=%u "
                   "lives56=%u wpn60=%u\n",
                   bs_frame_no,
                   (chip[0x1078] << 8) | chip[0x1079],
                   (chip[0x8000 + 7228] << 8) | chip[0x8000 + 7229],
                   (chip[0x4e40] << 8) | chip[0x4e41],
                   (chip[0x4e42] << 8) | chip[0x4e43],
                   (chip[0x4f4a] << 8) | chip[0x4f4b],
                   (chip[0x4f4c] << 8) | chip[0x4f4d],
                   chip[0x4e3c + 38], chip[0x4e3c + 48], chip[0x4e3c + 49],
                   (chip[0x4e3c + 52] << 8) | chip[0x4e3c + 53],
                   chip[0x4e3c + 56],
                   (chip[0x4e3c + 60] << 8) | chip[0x4e3c + 61]);
            for (unsigned slot = 0; slot < 18; slot++) {
                unsigned object = 0x2e040 + slot * 0x40;
                unsigned x = (chip[object] << 8) | chip[object + 1];
                if (!x) continue;
                printf("  obj %2u x=%4u y=%4u type=$%02X limit19=%3u "
                       "state25=%3u health28=%3u status30=%3u final33=%3u\n",
                       slot, x, (chip[object + 2] << 8) | chip[object + 3],
                       chip[object + 17], chip[object + 19],
                       chip[object + 25], chip[object + 28],
                       chip[object + 30], chip[object + 33]);
            }
            if (getenv("BS_DUMP_PLAYER")) {
                printf("  praw");
                for (unsigned b = 0; b < 128; b++)
                    printf(" %02X", chip[0x4e3c + b]);
                printf("\n");
            }
            for (unsigned slot = 0; slot < 16; slot++) {
                unsigned effect = 0x4976 + slot * 20;
                unsigned x = (chip[effect] << 8) | chip[effect + 1];
                if (!x) continue;
                printf("  eff %2u x=%4u y=%4u vx=%08X vy=%08X h16=%3u g17=%3u "
                       "s18=%3u t19=%3u\n",
                       slot, x, (chip[effect+4] << 8) | chip[effect+5],
                       (chip[effect+8]<<24)|(chip[effect+9]<<16)|
                       (chip[effect+10]<<8)|chip[effect+11],
                       (chip[effect+12]<<24)|(chip[effect+13]<<16)|
                       (chip[effect+14]<<8)|chip[effect+15],
                       chip[effect+16], chip[effect+17],
                       chip[effect+18], chip[effect+19]);
            }
            for (unsigned p = 0; p < 2; p++) {
                unsigned player = p ? 0x4f46 : 0x4e3c;
                for (unsigned slot = 0; slot < 12; slot++) {
                    unsigned shot = player + 122 + slot * 12;
                    unsigned x = (chip[shot] << 8) | chip[shot + 1];
                    if (!x) continue;
                    printf("  sht %u.%-2u x=%4u y=%4u vx=%04X vy=%04X "
                           "dmg=%3d\n", p, slot, x,
                           (chip[shot+2] << 8) | chip[shot+3],
                           (chip[shot+4] << 8) | chip[shot+5],
                           (chip[shot+6] << 8) | chip[shot+7],
                           (int)(int8_t)chip[shot+11]);
                }
            }
            for (unsigned slot = 0; slot < 12; slot++) {
                unsigned record = 0x2dc80 + slot * 0x50;
                unsigned x = (chip[record] << 8) | chip[record + 1];
                if (!x) continue;
                char flags_text[20];
                snprintf(flags_text, sizeof flags_text, " fl30=$%02X",
                         chip[record + 30]);
                printf("  hos %2u x=%4u y=%4u type=$%02X f29=%3u f63=%3u "
                       "gfx36=$%06X gfx32=$%06X h50=%u w52=%u "
                       "v12=$%08X f57=%u d62=%u%s\n",
                       slot, x, (chip[record + 4] << 8) | chip[record + 5],
                       chip[record + 31], chip[record + 29], chip[record + 63],
                       (chip[record+36]<<24)|(chip[record+37]<<16)|
                       (chip[record+38]<<8)|chip[record+39],
                       (chip[record+32]<<24)|(chip[record+33]<<16)|
                       (chip[record+34]<<8)|chip[record+35],
                       (chip[record+50]<<8)|chip[record+51],
                       (chip[record+52]<<8)|chip[record+53],
                       (chip[record+12]<<24)|(chip[record+13]<<16)|
                       (chip[record+14]<<8)|chip[record+15],
                       chip[record+57], chip[record+62],
                       getenv("BS_DUMP_FLAGS") ? flags_text : "");
            }
            if (getenv("BS_DUMP_FLAGS")) {
                /* -26242(A5) selects the short $ACA half-frame over the full
                 * $B34 one; -1792/-1791 are the pool pass selectors. */
                printf("  flags h=%02X p1792=%02X p1791=%02X\n",
                       chip[0x8000 - 26242], chip[0x8000 - 1792],
                       chip[0x8000 - 1791]);
            }
            if (getenv("BS_DUMP_MAP")) {
                unsigned p = 0x8000 + 7214;
                unsigned cursor = ((unsigned)chip[p] << 24) |
                                  ((unsigned)chip[p+1] << 16) |
                                  ((unsigned)chip[p+2] << 8) | chip[p+3];
                unsigned c = (cursor - 0x30) & 0x7ffff;
                printf("  map progress=%04x g7212=%04x g7222=%04x g7228=%04x "
                       "cursor=%06x words:",
                       (chip[0x8000+7206] << 8) | chip[0x8000+7207],
                       (chip[0x8000+7212] << 8) | chip[0x8000+7213],
                       (chip[0x8000+7222] << 8) | chip[0x8000+7223],
                       (chip[0x8000+7228] << 8) | chip[0x8000+7229], c);
                for (int w = 0; w < 24; w++)
                    printf(" %04x", (chip[c + w*2] << 8) | chip[c + w*2 + 1]);
                printf("\n");
            }
            fflush(stdout);
        }
        if (mix_audio) {
            int16_t audio[882 * 2];
            amiga_audio_frame();
            amiga_audio_pull(audio, 882);
            if (audio_dump) {
                fwrite(audio, sizeof audio[0], 882 * 2, audio_dump);
                audio_frames += 882;
            }
        }
    }
    amiga_report();
    if (audio_dump) {
        write_wav_header(audio_dump, audio_frames);
        fclose(audio_dump);
        fprintf(stderr, "native: dumped %s (%u frames)\n",
                audio_dump_path, audio_frames);
    }
    if (dump && dump_frame(dump)) return 1;
    if (amiga_stopped()) return 1;
    if (bs_file_load_count < expect_files) {
        fprintf(stderr, "native: expected at least %ld file loads, got %ld\n",
                expect_files, bs_file_load_count);
        return 1;
    }
    if (bs_blit_count < expect_blits) {
        fprintf(stderr, "native: expected at least %ld blits, got %ld\n",
                expect_blits, bs_blit_count);
        return 1;
    }
    return 0;
}
