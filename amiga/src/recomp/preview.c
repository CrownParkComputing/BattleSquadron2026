#include "runtime.h"
#include "ocs_video.h"
#include "paula_audio.h"
#include "raylib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int player_pad[2] = {-1, -1};
static PaulaAudio *audio_output;

static void audio_callback(void *buffer, unsigned int frames)
{
    paula_audio_pull(audio_output, (int16_t *)buffer, frames);
}

static uint8_t read_recomp_memory(void *user, uint32_t address)
{
    return bs_recomp_read8((const BsRecomp *)user, address);
}

static void custom_audio_write(void *user, uint16_t reg, uint16_t value)
{
    paula_audio_write((PaulaAudio *)user, reg, value);
}

static void play_original_sample(void *user,
                                 const BsAudioSampleEvent *event)
{
    PaulaAudio *paula = user;
    paula_audio_play_one_shot(paula, event->channel, event->address,
                              event->length_words, event->period,
                              event->volume, 1);
    fprintf(stderr,
            "recomp audio: speech $%06x, %u bytes, period %u, volume %u\n",
            event->address, event->length_words * 2u,
            event->period, event->volume);
}

static OcsVideoSource video_source(BsRecomp *machine)
{
    return (OcsVideoSource){
        .user = machine,
        .read8 = read_recomp_memory,
        .chip_mask = BS_RECOMP_MEMORY_SIZE - 1,
        .copper_address =
            ((uint32_t)machine->custom[0x080 >> 1] << 16) |
            machine->custom[0x082 >> 1]
    };
}

static uint8_t keyboard_input(int arrows)
{
    uint8_t state = 0;
    if (IsKeyDown(arrows ? KEY_UP : KEY_W)) state |= BS_INPUT_UP;
    if (IsKeyDown(arrows ? KEY_DOWN : KEY_S)) state |= BS_INPUT_DOWN;
    if (IsKeyDown(arrows ? KEY_LEFT : KEY_A)) state |= BS_INPUT_LEFT;
    if (IsKeyDown(arrows ? KEY_RIGHT : KEY_D)) state |= BS_INPUT_RIGHT;
    if (arrows) {
        if (IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_LEFT_CONTROL) ||
            IsKeyDown(KEY_ENTER)) state |= BS_INPUT_FIRE;
        if (IsKeyDown(KEY_X) || IsKeyDown(KEY_LEFT_SHIFT))
            state |= BS_INPUT_NOVA;
    } else {
        if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_C))
            state |= BS_INPUT_FIRE;
        if (IsKeyDown(KEY_V) || IsKeyDown(KEY_TAB))
            state |= BS_INPUT_NOVA;
    }
    return state;
}

static int non_gamepad_name(const char *name)
{
    if (!name) return 1;
    return strstr(name, "Mouse") || strstr(name, "mouse") ||
           strstr(name, "Keyboard") || strstr(name, "keyboard");
}

static void discover_gamepads(int announce)
{
    int selected[2] = {-1, -1};
    unsigned found = 0;
    for (int pad = 0; pad < 16; pad++) {
        if (!IsGamepadAvailable(pad)) continue;
        const char *name = GetGamepadName(pad);
        if (announce)
            fprintf(stderr, "recomp preview joystick %d: %s%s\n", pad,
                    name ? name : "(unnamed)",
                    non_gamepad_name(name) ? " [ignored]" : "");
        if (!non_gamepad_name(name) && found < 2)
            selected[found++] = pad;
    }
    for (unsigned player = 0; player < 2; player++) {
        if (selected[player] != player_pad[player] && selected[player] >= 0)
            fprintf(stderr, "recomp preview: player %u uses joystick %d (%s)\n",
                    player + 1, selected[player],
                    GetGamepadName(selected[player]));
        player_pad[player] = selected[player];
    }
}

static uint8_t gamepad_input(unsigned player)
{
    int pad = player < 2 ? player_pad[player] : -1;
    if (pad < 0 || !IsGamepadAvailable(pad)) return 0;
    uint8_t state = 0;
    float x = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_X);
    float y = GetGamepadAxisMovement(pad, GAMEPAD_AXIS_LEFT_Y);
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_UP) || y < -0.35f)
        state |= BS_INPUT_UP;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || y > 0.35f)
        state |= BS_INPUT_DOWN;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_LEFT) || x < -0.35f)
        state |= BS_INPUT_LEFT;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) || x > 0.35f)
        state |= BS_INPUT_RIGHT;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
        IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||
        IsGamepadButtonDown(pad, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
        IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_2))
        state |= BS_INPUT_FIRE;
    if (IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
        IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_FACE_UP) ||
        IsGamepadButtonDown(pad, GAMEPAD_BUTTON_LEFT_TRIGGER_1) ||
        IsGamepadButtonDown(pad, GAMEPAD_BUTTON_RIGHT_TRIGGER_1))
        state |= BS_INPUT_NOVA;
    return state;
}

static void update_input(BsRecomp *machine)
{
    /* BS_PREVIEW_INPUT=fire drives the same held-fire input the headless
     * tools use, so a preview dump can be diffed against render_at at the
     * same game cycle instead of against a different playthrough. */
    if (getenv("BS_PREVIEW_INPUT")) {
        bs_recomp_set_input(machine, 0, BS_INPUT_FIRE);
        bs_recomp_set_input(machine, 1, 0);
        return;
    }
    static unsigned rescan;
    if (++rescan >= 100) {
        discover_gamepads(0);
        rescan = 0;
    }
    bs_recomp_set_input(machine, 0,
        keyboard_input(1) | gamepad_input(0));
    bs_recomp_set_input(machine, 1,
        keyboard_input(0) | gamepad_input(1));
}

static Rectangle fitted_screen(void)
{
    float sx = GetScreenWidth() / (float)OCS_VIDEO_WIDTH;
    float sy = (GetScreenHeight() - 58) / (float)OCS_VIDEO_HEIGHT;
    float scale = sx < sy ? sx : sy;
    float width = OCS_VIDEO_WIDTH * scale;
    float height = OCS_VIDEO_HEIGHT * scale;
    return (Rectangle){(GetScreenWidth() - width) * 0.5f, 54,
                       width, height};
}

typedef enum {
    PRESENT_INTRO,
    PRESENT_TITLE,
    PRESENT_SECOND_TITLE,
    PRESENT_TRANSFORM,
    PRESENT_GAME,
} PresentationStage;

static const char *stage_name(PresentationStage stage)
{
    switch (stage) {
    case PRESENT_INTRO: return "ORIGINAL SPOKEN INTRO";
    case PRESENT_TITLE: return "TITLE - FIRE TO START";
    case PRESENT_SECOND_TITLE: return "PREPARING SQUADRON";
    case PRESENT_TRANSFORM: return "TRANSFORMATION";
    case PRESENT_GAME: return "LIVE GAME";
    }
    return "";
}

static int any_fire(void)
{
    return (keyboard_input(1) | keyboard_input(0) |
            gamepad_input(0) | gamepad_input(1)) & BS_INPUT_FIRE;
}

static int run_exact(BsRecomp *machine, long steps, uint32_t expected_pc,
                     const char *name)
{
    int result = bs_recomp_run(machine, steps);
    if (result != BS_RECOMP_OK || machine->cpu.pc != expected_pc) {
        fprintf(stderr,
                "recomp preview %s: pc=$%06x steps=%ld: %s\n",
                name, machine->cpu.pc, machine->translated_steps,
                machine->error);
        return 0;
    }
    return 1;
}

/* One pass around $7D0 is one original display frame.  Keeping this as a
 * presentation boundary makes the translated tile-column transformation
 * visible instead of collapsing all 256 frames during startup. */
static int run_transform_frame(BsRecomp *machine, int *finished)
{
    for (int step = 0; step < 64; step++) {
        int result = bs_recomp_run(machine, 1);
        if (result != BS_RECOMP_OK) return 0;
        if (machine->cpu.pc == 0x7d0) return 1;
        if (machine->cpu.pc == 0xaa0) {
            *finished = 1;
            return 1;
        }
    }
    snprintf(machine->error, sizeof machine->error,
             "transform did not reach a frame boundary");
    return 0;
}

static int run_game_display_frame(BsRecomp *machine,
                                  uint8_t *clean_playfield)
{
    /* $1078 advances at both halves of a game loop and $B54 is only the
     * post-scroll/pre-restore midpoint.  $AA0 is the completed frame edge,
     * after both terrain-restore halves and every transient draw pass. */
    for (int guard = 0; guard < 512; guard++) {
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) return 0;
        if (machine->cpu.pc == 0xb54) break;
        if (guard == 511) {
            snprintf(machine->error, sizeof machine->error,
                     "game did not reach its clean-terrain boundary");
            return 0;
        }
    }
    memcpy(clean_playfield, machine->memory + 0x62000, 0x1e000);
    for (int guard = 0; guard < 512; guard++) {
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) return 0;
        if (machine->cpu.pc == 0xaa0) return 1;
    }
    snprintf(machine->error, sizeof machine->error,
             "game did not reach its completed-frame boundary");
    return 0;
}

int main(int argc, char **argv)
{
    const char *data = "original/whdload/BattleSquadron/data";
    if (argc == 3 && !strcmp(argv[1], "--data")) data = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--data DIR]\n", argv[0]);
        return 2;
    }
    BsRecomp *machine = malloc(sizeof *machine);
    OcsVideo *video = ocs_video_create();
    uint8_t *clean_playfield = malloc(0x1e000);
    if (!machine || !video || !clean_playfield) return 1;
    if (bs_recomp_init(machine, data) != BS_RECOMP_OK) {
        fprintf(stderr, "recomp preview: %s\n", machine->error);
        return 1;
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(OCS_VIDEO_WIDTH * 3, OCS_VIDEO_HEIGHT * 3 + 34,
               "Battle Squadron - genuine recomp preview");
    SetExitKey(KEY_ESCAPE);
    SetTargetFPS(50);
    discover_gamepads(1);

    audio_output = paula_audio_create((PaulaAudioSource){
        .user = machine,
        .read8 = read_recomp_memory,
        .address_mask = BS_RECOMP_MEMORY_SIZE - 1,
    });
    if (!audio_output) {
        fprintf(stderr, "recomp preview: could not create Paula audio\n");
        return 1;
    }
    bs_recomp_set_custom_write_hook(machine, custom_audio_write,
                                    audio_output);
    bs_recomp_set_audio_sample_hook(machine, play_original_sample,
                                    audio_output);
    /* Run the $24F34 sequencer.  It is off by default so the step-exact
     * oracle tests keep their register traces; the preview wants the music. */
    bs_recomp_set_music_enabled(machine, 1);
    bs_recomp_set_external_playfield_restore(machine, 1);
    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(256);
    AudioStream stream = LoadAudioStream(PAULA_AUDIO_RATE, 16, 2);
    SetAudioStreamCallback(stream, audio_callback);

    if (!run_exact(machine, 38, 0x4ec, "spoken intro")) return 1;
    PresentationStage stage = PRESENT_INTRO;
    int stage_frame = 0;
    long game_cycles = 0;
    int audio_started = 0;
    OcsVideoSource source = video_source(machine);
    const uint32_t *pixels = ocs_video_render(video, &source);
    Image image = {
        .data = (void *)pixels,
        .width = OCS_VIDEO_WIDTH,
        .height = OCS_VIDEO_HEIGHT,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    int finished = 0;
    while (!WindowShouldClose()) {
        if (!finished) {
            int advanced = 1;
            int fire = any_fire();
            if (stage == PRESENT_INTRO && stage_frame >= 90) {
                if (!run_exact(machine, 46, 0x5f6, "title"))
                    finished = 1;
                else {
                    stage = PRESENT_TITLE;
                    stage_frame = 0;
                }
            } else if (stage == PRESENT_TITLE &&
                       (stage_frame >= 250 ||
                        (stage_frame >= 20 && fire))) {
                if (!run_exact(machine, 16, 0x6fa, "second title"))
                    finished = 1;
                else {
                    stage = PRESENT_SECOND_TITLE;
                    stage_frame = 0;
                }
            } else if (stage == PRESENT_SECOND_TITLE &&
                       (stage_frame >= 100 ||
                        (stage_frame >= 20 && fire))) {
                if (!run_exact(machine, 40, 0x7d0, "game setup"))
                    finished = 1;
                else if (bs_recomp_start_new_game(machine) != BS_RECOMP_OK)
                    finished = 1;
                else {
                    /* Ring replacement is intentionally hidden behind the
                     * transition.  The first visible map is fully valid. */
                    stage = PRESENT_GAME;
                    stage_frame = 0;
                    bs_recomp_enable_live_input(machine, 1);
                }
            } else if (stage == PRESENT_TRANSFORM) {
                int transform_finished = 0;
                if (!run_transform_frame(machine, &transform_finished))
                    finished = 1;
                else if (transform_finished) {
                    stage = PRESENT_GAME;
                    stage_frame = 0;
                    bs_recomp_enable_live_input(machine, 1);
                }
            } else if (stage == PRESENT_GAME) {
                /* One $AA0-to-$AA0 cycle covers both halves of the game loop
                 * and so advances $1078 twice.  The reference advances it once
                 * per PAL frame, which makes the loop a 25Hz one: running a
                 * whole cycle every displayed frame ran the game at double
                 * speed, which also halved every animation's duration. */
                static int logic_phase;
                update_input(machine);
                advanced = !(logic_phase++ & 1);
                if (advanced) {
                    if (!run_game_display_frame(machine, clean_playfield))
                        finished = 1;
                    else
                        game_cycles++;
                }
            }
            /* The sequencer is a CIA-B interrupt on the real machine, so it
             * has to keep running on frames the dispatcher is not: the intro
             * and title stages never step the machine at all, which left them
             * completely silent. */
            if (stage != PRESENT_GAME)
                bs_recomp_music_tick(machine);
            paula_audio_queue_pal_frame(audio_output);
            if (!audio_started && paula_audio_fill(audio_output) >= 1764) {
                PlayAudioStream(stream);
                audio_started = 1;
            }
            /* The capture/restore pair only makes sense on a frame that ran
             * the game.  Rendering a skipped frame would show the restored
             * pre-object playfield and flicker the sprites off every other
             * frame, and restoring it again would put a stale image back. */
            if (advanced) {
                source = video_source(machine);
                pixels = ocs_video_render(video, &source);
                UpdateTexture(texture, pixels);
                /* BS_PREVIEW_DUMP=<frame> writes exactly what the preview is
                 * showing to build/preview_dump.ppm and quits, so the on-screen
                 * image can be diffed against the oracle without screenshots. */
                {
                    static long shown;
                    const char *want = getenv("BS_PREVIEW_DUMP");
                    if (want && ++shown == atol(want)) {
                        FILE *out = fopen("build/preview_dump.ppm", "wb");
                        if (out) {
                            fprintf(out, "P6\n%d %d\n255\n",
                                    OCS_VIDEO_WIDTH, OCS_VIDEO_HEIGHT);
                            for (int i = 0;
                                 i < OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT; i++) {
                                fputc(pixels[i] & 0xff, out);
                                fputc((pixels[i] >> 8) & 0xff, out);
                                fputc((pixels[i] >> 16) & 0xff, out);
                            }
                            fclose(out);
                        }
                        fprintf(stderr, "preview: dumped shown-frame %ld (stage %s) after "
                                "%ld game cycles, t1078=%u\n", shown,
                                stage_name(stage), game_cycles,
                                bs_recomp_read16(machine, 0x1078));
                        finished = 1;
                        break;
                    }
                }
                if (stage == PRESENT_GAME && getenv("BS_TRACE_GOLD")) {
                    /* Report compact clusters of the gold sprite colours and
                     * every record that could be drawing them. */
                    static int trace_frame;
                    int gold = 0, minx = 999, maxx = -1, miny = 999,
                        maxy = -1;
                    for (int i = 0;
                         i < OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT; i++) {
                        unsigned R = pixels[i] & 0xff;
                        unsigned G = (pixels[i] >> 8) & 0xff;
                        unsigned B = (pixels[i] >> 16) & 0xff;
                        if (R > 150 && B < 110 && G < R) {
                            int x = i % OCS_VIDEO_WIDTH;
                            int y = i / OCS_VIDEO_WIDTH;
                            gold++;
                            if (x < minx) minx = x;
                            if (x > maxx) maxx = x;
                            if (y < miny) miny = y;
                            if (y > maxy) maxy = y;
                        }
                    }
                    if (gold >= 15 && (++trace_frame % 20) == 0) {
                        unsigned scroll =
                            bs_recomp_read16(machine, 0x8000 + 7204);
                        fprintf(stderr, "GOLD n=%d bbox %d..%d,%d..%d "
                                "scroll=%u\n", gold, minx, maxx, miny,
                                maxy, scroll);
                        for (int k = 0; k < 12; k++) {
                            uint32_t r = 0x2dc80 + k * 0x50;
                            if (!bs_recomp_read16(machine, r)) continue;
                            fprintf(stderr, "  hos %2d screen %4d,%4d "
                                    "type=$%02X f29=%u f63=%u h50=%u\n", k,
                                (int)bs_recomp_read16(machine, r) -
                                    (int)scroll,
                                (int)bs_recomp_read16(machine, r + 4) - 0x100,
                                bs_recomp_read8(machine, r + 31),
                                bs_recomp_read8(machine, r + 29),
                                bs_recomp_read8(machine, r + 63),
                                bs_recomp_read16(machine, r + 50));
                        }
                        for (int k = 0; k < 16; k++) {
                            uint32_t e = 0x4976 + k * 20;
                            if (!bs_recomp_read16(machine, e)) continue;
                            fprintf(stderr, "  fx  %2d screen %4d,%4d\n", k,
                                (int)bs_recomp_read16(machine, e) -
                                    (int)scroll,
                                (int)bs_recomp_read16(machine, e + 4) - 0x100);
                        }
                    }
                }
                if (stage == PRESENT_GAME)
                    memcpy(machine->memory + 0x62000, clean_playfield,
                           0x1e000);
            }
        }
        BeginDrawing();
        ClearBackground(BLACK);
        DrawText("GENUINE RECOMP PREVIEW  |  NO MUSASHI  |  LIVE INPUT",
                 12, 9, 16, (Color){120, 220, 255, 255});
        DrawText("P1: arrows / pad 1   fire: Space/A   Nova: X/B",
                 12, 28, 15, LIGHTGRAY);
        DrawText(stage_name(stage), GetScreenWidth() - 240, 28, 15,
                 stage == PRESENT_GAME ? GREEN : YELLOW);
        DrawTexturePro(texture,
            (Rectangle){0, 0, OCS_VIDEO_WIDTH, OCS_VIDEO_HEIGHT},
            fitted_screen(), (Vector2){0, 0}, 0, WHITE);
        if (stage == PRESENT_GAME) {
            /* Collision diagnostic.  Everything here is read straight out of
             * the player record so what is on screen can be checked against
             * what the translated collision passes actually did. */
            static uint8_t was_dying, was_respawning;
            static uint16_t was_weapon;
            static unsigned deaths, respawns, pickups, hit_flash;
            uint8_t dying = bs_recomp_read8(machine, 0x4e3c + 49);
            uint8_t respawning = bs_recomp_read8(machine, 0x4e3c + 48);
            uint16_t weapon = bs_recomp_read16(machine, 0x4e3c + 60);
            if (dying && !was_dying) { deaths++; hit_flash = 40; }
            if (respawning == 0x91 && was_respawning != 0x91) respawns++;
            if (weapon > was_weapon) { pickups++; hit_flash = 20; }
            was_dying = dying;
            was_respawning = respawning;
            was_weapon = weapon;
            if (hit_flash) hit_flash--;

            unsigned objects = 0;
            for (int slot = 0; slot < 18; slot++)
                if (bs_recomp_read16(machine, 0x2e040 + slot * 0x40) != 0)
                    objects++;
            unsigned shots = 0;
            for (int slot = 0; slot < 12; slot++)
                if (bs_recomp_read16(machine, 0x4e3c + 122 + slot * 12) != 0)
                    shots++;

            char line[192];
            snprintf(line, sizeof line,
                     "LIVES %u  WPN %u  ST $%02X  DYING %3u  INVUL %5u  "
                     "OBJ %2u  SHOTS %2u  | DEATHS %u  RESPAWNS %u  "
                     "PICKUPS %u",
                     bs_recomp_read8(machine, 0x4e3c + 56), weapon,
                     bs_recomp_read8(machine, 0x4e3c + 38), dying,
                     bs_recomp_read16(machine, 0x4e3c + 52),
                     objects, shots, deaths, respawns, pickups);
            int y = GetScreenHeight() - 22;
            DrawRectangle(0, y - 5, GetScreenWidth(), 27,
                          (Color){0, 0, 0, 210});
            DrawText(line, 10, y, 14,
                     hit_flash ? (Color){255, 210, 80, 255}
                               : (Color){170, 255, 170, 255});
        }
        if (finished)
            DrawText("NATIVE FRONTIER REACHED - SEE TERMINAL DIAGNOSTIC",
                     18, GetScreenHeight() - 52, 16, YELLOW);
        EndDrawing();
        stage_frame++;
    }
    if (audio_started) StopAudioStream(stream);
    UnloadAudioStream(stream);
    CloseAudioDevice();
    UnloadTexture(texture);
    CloseWindow();
    fprintf(stderr, "recomp preview: steps=%ld pc=$%06x %s\n",
            machine->translated_steps, machine->cpu.pc, machine->error);
    ocs_video_destroy(video);
    paula_audio_destroy(audio_output);
    free(clean_playfield);
    free(machine);
    return 0;
}
