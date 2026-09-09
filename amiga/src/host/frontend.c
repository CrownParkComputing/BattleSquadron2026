#include "amiga.h"
#include "pad.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>

#ifdef PLATFORM_ANDROID
#include <android/api-level.h>
#include <android/native_window.h>
#include <android_native_app_glue.h>

/* Exported by raylib's Android platform layer (not yet in raylib.h). */
extern struct android_app *GetAndroidApp(void);
#endif

enum {
    RAW_SPACE = 0x40,
    RAW_RETURN = 0x44,
    RAW_ESCAPE = 0x45,
    RAW_F1 = 0x50
};

static void audio_callback(void *buffer, unsigned int frames)
{
    amiga_audio_pull((int16_t *)buffer, (int)frames);
}

#ifdef PLATFORM_ANDROID
static void request_pal_frame_rate(void)
{
    struct android_app *app = GetAndroidApp();
    int result = -1;
    if (app && app->window && android_get_device_api_level() >= 30)
        result = ANativeWindow_setFrameRate(
            app->window, 50.0f,
            ANATIVEWINDOW_FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
    TraceLog(LOG_WARNING, "ANDROID: requested PAL 50 Hz presentation (%d)",
             result);
}
#endif


/* The title screen defaults to TWO PLAYERS, so fire on player one alone still
 * started a two-player game.  The loader forces player one on at $10AA and
 * copies the title's selection byte at 10965(A5) = $AAD5 into player two's
 * enable flag at $10B0, so holding that byte clear makes fire a ONE-player
 * start.  It is released as soon as player two presses their own fire, which
 * is how they join.  Verified: player two then parks off-screen at $3E7
 * instead of joining at 496,512. */
static void select_player_count(uint8_t player_two_controls)
{
    static bool player_two_joined;
    if (player_two_controls & 0x10) player_two_joined = true;
    if (!player_two_joined) chip[0xaad5] = 0;
}

static void update_input(void)
{
    joy_state[1] = keyboard_stick(true) | gamepad_stick(0);
    joy_state[0] = keyboard_stick(false) | gamepad_stick(1);
    select_player_count(joy_state[0]);
    map_raw_key(KEY_SPACE, RAW_SPACE);
    map_raw_key(KEY_ENTER, RAW_RETURN);
    for (int number = 0; number < 10; number++)
        map_raw_key(KEY_F1 + number, (uint8_t)(RAW_F1 + number));
}

static void draw_splash(Texture2D logo)
{
    float scale_x = GetScreenWidth() * 0.84f / logo.width;
    float scale_y = GetScreenHeight() * 0.78f / logo.height;
    float scale = scale_x < scale_y ? scale_x : scale_y;
    float width = logo.width * scale;
    float height = logo.height * scale;
    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(logo,
                   (Rectangle){0, 0, (float)logo.width, (float)logo.height},
                   (Rectangle){(GetScreenWidth() - width) * 0.5f,
                               (GetScreenHeight() - height) * 0.5f,
                               width, height},
                   (Vector2){0, 0}, 0, WHITE);
    EndDrawing();
}

int main(int argc, char **argv)
{
#ifdef PLATFORM_ANDROID
    const char *data = "data";
#else
    const char *data = "original/whdload/BattleSquadron/data";
#endif
    if (argc == 3 && !strcmp(argv[1], "--data")) data = argv[2];
    else if (argc != 1) {
        fprintf(stderr, "usage: %s [--data DIR]\n", argv[0]);
        return 2;
    }

    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(SCREEN_W * 3, SCREEN_H * 3,
               "Battle Squadron - Amiga native runner");
    SetExitKey(KEY_ESCAPE);
    SetTargetFPS(50);
#ifdef PLATFORM_ANDROID
    request_pal_frame_rate();
#endif
    for (int pad = 0; pad < 4; pad++) {
        if (IsGamepadAvailable(pad)) {
#ifdef PLATFORM_ANDROID
            TraceLog(LOG_WARNING, "ANDROID: controller %d: %s", pad,
                     GetGamepadName(pad));
#else
            fprintf(stderr, "controller %d: %s\n", pad,
                    GetGamepadName(pad));
#endif
        }
    }

#ifdef PLATFORM_ANDROID
    const char *logo_path = "retro-recomp.png";
#else
    const char *logo_path = "assets/retro-recomp.png";
#endif
    Texture2D splash = LoadTexture(logo_path);

    amiga_init(data);
    /* The original loader spends several seconds in display-independent
     * delay loops. Run those at host speed, then turn on exact scanout. */
    int16_t audio_buffer[1024 * 2];
    /* Only accelerate the silent loader.  Paula becomes audible near frame
     * 168; stopping at 140 preserves the complete spoken "Welcome to Battle
     * Squadron" sample as well as the title music. */
    while (bs_frame_no < 140 && !amiga_stopped()) {
        amiga_run_frame();
        /* Advance Paula with the emulated clock during the fast boot, but
         * discard those samples rather than playing several seconds late. */
        amiga_audio_frame();
        amiga_audio_pull(audio_buffer, 882);
    }
    double splash_until = GetTime() + 1.5;
    while (GetTime() < splash_until && !WindowShouldClose())
        draw_splash(splash);
    UnloadTexture(splash);
    if (WindowShouldClose()) {
        CloseWindow();
        return 0;
    }
    amiga_enable_video(true);
    for (int frame = 0; frame < 3 && !amiga_stopped(); frame++)
        amiga_run_frame();

    Image image = {
        .data = framebuf,
        .width = SCREEN_W,
        .height = SCREEN_H,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    InitAudioDevice();
    SetAudioStreamBufferSizeDefault(256);
    AudioStream stream = LoadAudioStream(44100, 16, 2);
    SetAudioStreamCallback(stream, audio_callback);
    bool audio_started = false;

#ifdef PLATFORM_ANDROID
    /* Let EGL present at the device-selected rate (ideally the requested
     * 50 Hz).  The emulation clock below remains exactly PAL-paced even when
     * the panel can only scan at 60 Hz. */
    SetTargetFPS(120);
    double next_emulated_frame = GetTime();
    double next_diagnostic = next_emulated_frame + 5.0;
    long rendered_frames = 0;
#endif

    bool paused = false;
    while (!WindowShouldClose() && !amiga_stopped()) {
        /* P pauses the emulation: the frame keeps being presented so the
         * picture stays up, but no Amiga frame is run and no input reaches the
         * game, which also stops the ship drifting while paused. */
        js_poll();
        static bool back_was_down;
        /* START pauses (BACK too, for pads that put it somewhere odd). */
        bool back_down = js_present(0)
            ? (js_button_down(0, 7) || js_button_down(0, 6))
            : (IsGamepadAvailable(0) &&
               (IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
                IsGamepadButtonDown(0, GAMEPAD_BUTTON_MIDDLE_LEFT)));
        if (IsKeyPressed(KEY_P) || (back_down && !back_was_down))
            paused = !paused;
        back_was_down = back_down;
        if (paused) {
            BeginDrawing();
            ClearBackground(BLACK);
            Rectangle where = fit_screen();
            DrawTexturePro(texture, (Rectangle){0, 0, SCREEN_W, SCREEN_H},
                           where, (Vector2){0, 0}, 0, WHITE);
            DrawText("PAUSED  -  P to resume",
                     (int)where.x + 12, (int)where.y + 12, 20,
                     (Color){255, 220, 90, 255});
            EndDrawing();
            continue;
        }
        update_input();
#ifdef PLATFORM_ANDROID
        double now = GetTime();
        if (now - next_emulated_frame > 0.080)
            next_emulated_frame = now;
        int frames_run = 0;
        while (now + 0.0005 >= next_emulated_frame && frames_run < 4) {
            amiga_run_frame();
            amiga_audio_frame();
            next_emulated_frame += 1.0 / 50.0;
            frames_run++;
        }
#else
        amiga_run_frame();
        amiga_audio_frame();
#endif
        if (!audio_started && amiga_audio_fill() >= 1764) {
            /* Two PAL frames cover normal device callback jitter without the
             * long push queue that sounds echoey on Android/OpenSL. */
            PlayAudioStream(stream);
            audio_started = true;
        }
#ifdef PLATFORM_ANDROID
        if (frames_run)
#endif
        UpdateTexture(texture, framebuf);
        BeginDrawing();
        ClearBackground(BLACK);
        Rectangle destination = fit_screen();
        DrawTexturePro(texture,
                       (Rectangle){0, 0, SCREEN_W, SCREEN_H},
                       destination, (Vector2){0, 0}, 0, WHITE);
        EndDrawing();
#ifdef PLATFORM_ANDROID
        rendered_frames++;
        now = GetTime();
        if (now >= next_diagnostic) {
            TraceLog(LOG_WARNING,
                     "ANDROID: clock emulated=%ld rendered=%ld audio_fill=%d",
                     bs_frame_no, rendered_frames, amiga_audio_fill());
            next_diagnostic = now + 5.0;
        }
#endif
    }

    if (audio_started) StopAudioStream(stream);
    UnloadAudioStream(stream);
    CloseAudioDevice();
    UnloadTexture(texture);
    CloseWindow();
    amiga_report();
    return amiga_stopped() ? 1 : 0;
}
