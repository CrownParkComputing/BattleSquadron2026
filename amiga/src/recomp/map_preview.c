#include "bs_map.h"
#include "raylib.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    const char *data_path;
    const char *trace_path;
    const char *extract_path;
    const char *ppm_path;
    size_t frames;
    int headless;
} Options;

static void usage(const char *program)
{
    fprintf(stderr,
        "usage: %s [--trace FILE | --extract FILE] [options]\n"
        "  --frames N    rows to extract (default 2048)\n"
        "  --ppm FILE    export the unrolled terrain as PPM\n"
        "  --data DIR    Battle Squadron WHDLoad data directory\n"
        "  --headless    extract/check without opening the player\n",
        program);
}

static int parse_options(int argc, char **argv, Options *options)
{
    *options = (Options){
        .data_path = "original/whdload/BattleSquadron/data",
        .frames = 2048
    };
    for (int index = 1; index < argc; index++) {
        if (!strcmp(argv[index], "--headless")) options->headless = 1;
        else if ((!strcmp(argv[index], "--trace") ||
                  !strcmp(argv[index], "--extract") ||
                  !strcmp(argv[index], "--ppm") ||
                  !strcmp(argv[index], "--data") ||
                  !strcmp(argv[index], "--frames")) && index + 1 < argc) {
            const char *argument = argv[++index];
            if (!strcmp(argv[index - 1], "--trace"))
                options->trace_path = argument;
            else if (!strcmp(argv[index - 1], "--extract"))
                options->extract_path = argument;
            else if (!strcmp(argv[index - 1], "--ppm"))
                options->ppm_path = argument;
            else if (!strcmp(argv[index - 1], "--data"))
                options->data_path = argument;
            else {
                char *end = NULL;
                unsigned long value = strtoul(argument, &end, 10);
                if (!end || *end || value < BS_MAP_VIEW_LINES ||
                    value > 1000000) return 0;
                options->frames = (size_t)value;
            }
        } else return 0;
    }
    return !(options->trace_path && options->extract_path);
}

static int capture_trace(ScrollMapTrace *trace, const Options *options)
{
    BsRecomp *machine = malloc(sizeof *machine);
    if (!machine) return 0;
    if (bs_recomp_init(machine, options->data_path) != BS_RECOMP_OK) {
        fprintf(stderr, "map extractor init: %s\n", machine->error);
        free(machine);
        return 0;
    }
    unsigned guard = 0;
    while (machine->cpu.pc != 0x7d0 && guard++ < 2000) {
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) {
            fprintf(stderr, "map extractor boot: %s\n", machine->error);
            free(machine);
            return 0;
        }
    }
    if (machine->cpu.pc != 0x7d0) {
        fprintf(stderr, "map extractor: terrain entry was not reached\n");
        free(machine);
        return 0;
    }
    bs_map_configure_trace(trace, machine);
    size_t step_limit = options->frames * 96 + 50000;
    size_t pending_object_frame = SIZE_MAX;
    size_t object_pixels = 0;
    for (size_t steps = 0;
         (trace->frame_count < options->frames ||
          pending_object_frame != SIZE_MAX) && steps < step_limit;
         steps++) {
        uint32_t pc = machine->cpu.pc;
        BsMapState before, after;
        bs_map_get_state(machine, &before);
        if (bs_recomp_run(machine, 1) != BS_RECOMP_OK) {
            fprintf(stderr, "map extractor runtime: %s (pc=$%06x)\n",
                    machine->error, machine->cpu.pc);
            break;
        }
        if (pc == 0xb6e && pending_object_frame != SIZE_MAX) {
            int changed = bs_map_capture_object_line(
                trace, pending_object_frame, machine);
            if (changed < 0) {
                fprintf(stderr, "map extractor: object-layer capture failed\n");
                break;
            }
            object_pixels += (size_t)changed;
            pending_object_frame = SIZE_MAX;
        }
        if (pc != 0x7d0 && pc != 0xb34) continue;
        bs_map_get_state(machine, &after);
        if (after.progress == before.progress) continue;
        if (trace->frame_count >= options->frames) continue;
        int captured = bs_map_append_frame(trace, machine,
                                           machine->translated_steps);
        if (captured <= 0) {
            fprintf(stderr, "map extractor: %s at row %zu\n",
                    captured ? "source/ring pixel mismatch"
                             : "trace allocation failure",
                    trace->frame_count);
            break;
        }
        if (pc == 0xb34) pending_object_frame = trace->frame_count - 1;
    }
    bs_map_update_palette(trace, machine);
    if (trace->frame_count != options->frames) {
        fprintf(stderr, "map extractor: captured %zu/%zu rows\n",
                trace->frame_count, options->frames);
        free(machine);
        return 0;
    }
    fprintf(stderr, "map extractor: object layer=%zu changed pixels\n",
            object_pixels);
    free(machine);
    return 1;
}

static int trace_is_exact(const ScrollMapTrace *trace,
                          ScrollMapValidation *validation)
{
    *validation = scroll_map_validate(trace);
    return !validation->primary_discontinuities &&
           !validation->ring_discontinuities &&
           !validation->cross_discontinuities &&
           !validation->phase_discontinuities &&
           !validation->source_mismatches;
}

static void draw_player(const ScrollMapTrace *trace,
                        ScrollMapValidation validation)
{
    uint32_t *pixels = malloc((size_t)trace->viewport_pixels *
                              trace->viewport_lines * sizeof *pixels);
    if (!pixels) return;
    size_t frame = trace->frame_count > trace->viewport_lines
                 ? (size_t)trace->viewport_lines - 1
                 : trace->frame_count - 1;
    int playing = 1, coarse_only = 0, seams = 0, show_objects = 1;
    int manual_x = -1;
    scroll_map_render(trace, frame, coarse_only, show_objects, pixels);
    Image image = {
        .data = pixels,
        .width = trace->viewport_pixels,
        .height = trace->viewport_lines,
        .mipmaps = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    SetConfigFlags(FLAG_WINDOW_RESIZABLE);
    InitWindow(1000, 860, "Amiga Recomp Map Lab - Battle Squadron");
    SetExitKey(KEY_ESCAPE);
    SetTargetFPS(50);
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);

    while (!WindowShouldClose()) {
        if (IsKeyPressed(KEY_SPACE)) playing = !playing;
        if (IsKeyPressed(KEY_C)) coarse_only = !coarse_only;
        if (IsKeyPressed(KEY_O)) show_objects = !show_objects;
        if (IsKeyPressed(KEY_G)) seams = !seams;
        if (IsKeyPressed(KEY_A)) manual_x = -1;
        if (IsKeyPressed(KEY_HOME)) {
            frame = trace->viewport_lines - 1;
            playing = 0;
        }
        if (IsKeyPressed(KEY_END)) {
            frame = trace->frame_count - 1;
            playing = 0;
        }
        if (IsKeyPressed(KEY_UP) && frame >= trace->viewport_lines) {
            frame--;
            playing = 0;
        }
        if (IsKeyPressed(KEY_DOWN) && frame + 1 < trace->frame_count) {
            frame++;
            playing = 0;
        }
        if (IsKeyPressed(KEY_PAGE_UP)) {
            size_t first = (size_t)trace->viewport_lines - 1;
            size_t amount = frame >= first + 16 ? 16 : frame - first;
            frame -= amount;
            playing = 0;
        }
        if (IsKeyPressed(KEY_PAGE_DOWN)) {
            size_t remaining = trace->frame_count - 1 - frame;
            frame += remaining < 16 ? remaining : 16;
            playing = 0;
        }
        if (playing && frame + 1 < trace->frame_count) frame++;
        else if (frame + 1 == trace->frame_count) playing = 0;
        const ScrollMapRecord *record = &trace->records[frame];
        int max_x = trace->line_pixels - trace->viewport_pixels;
        if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_RIGHT)) {
            if (manual_x < 0) manual_x = record->cross_position;
            int speed = IsKeyDown(KEY_LEFT_SHIFT) ||
                        IsKeyDown(KEY_RIGHT_SHIFT) ? 4 : 1;
            if (IsKeyDown(KEY_LEFT)) manual_x -= speed;
            if (IsKeyDown(KEY_RIGHT)) manual_x += speed;
            if (manual_x < 0) manual_x = 0;
            if (manual_x > max_x) manual_x = max_x;
        }
        int view_x = manual_x < 0 ? record->cross_position : manual_x;
        scroll_map_render_offset(trace, frame, coarse_only, show_objects,
            view_x - record->cross_position, pixels);
        UpdateTexture(texture, pixels);

        float available_h = GetScreenHeight() - 178.0f;
        float scale_x = GetScreenWidth() / (float)trace->viewport_pixels;
        float scale_y = available_h / trace->viewport_lines;
        float scale = scale_x < scale_y ? scale_x : scale_y;
        if (scale < 1) scale = 1;
        float draw_w = trace->viewport_pixels * scale;
        float draw_h = trace->viewport_lines * scale;
        float draw_x = (GetScreenWidth() - draw_w) * 0.5f;
        Rectangle destination = {draw_x, 112, draw_w, draw_h};
        BeginDrawing();
        ClearBackground((Color){7, 10, 18, 255});
        DrawText("AMIGA RECOMP MAP LAB  |  REAL TERRAIN STREAM  |  NO MUSASHI",
                 16, 12, 19, (Color){100, 220, 255, 255});
        DrawText(TextFormat("row %zu/%zu  source $%05X  ring $%05X  "
                            "phase %u/15", frame + 1, trace->frame_count,
                            record->source_address, record->ring_address,
                            record->phase), 16, 42, 18, RAYWHITE);
        DrawText(TextFormat("world %d  view x %d px (%s)  coarse %d  fine %d",
                            record->primary_position,
                            view_x, manual_x < 0 ? "AUTO" : "MANUAL",
                            record->cross_coarse_position,
                            record->cross_coarse_position -
                            record->cross_position), 16, 66, 18, LIGHTGRAY);
        DrawText(coarse_only ?
                 "COARSE-ONLY BUG SIMULATION (C toggles corrected view)" :
                 "PIXEL-SMOOTH PLAYBACK (C simulates missing fine scroll)",
                 16, 88, 17, coarse_only ? ORANGE : GREEN);
        DrawText(show_objects ? "OBJECT LAYER ON (O toggles)" :
                               "RAW TERRAIN ONLY (O toggles)",
                 GetScreenWidth() - 270, 88, 16,
                 show_objects ? SKYBLUE : GRAY);
        DrawTexturePro(texture,
            (Rectangle){0, 0, trace->viewport_pixels, trace->viewport_lines},
            destination, (Vector2){0, 0}, 0, WHITE);
        if (seams) {
            for (unsigned x = 0; x <= trace->viewport_pixels; x += 16)
                DrawLine((int)(draw_x + x * scale), 112,
                         (int)(draw_x + x * scale), (int)(112 + draw_h),
                         (Color){255, 80, 80, 100});
        }
        int footer = (int)(118 + draw_h);
        DrawText("Left/right pan (Shift fast) | Up/down rows | PgUp/PgDn 16 | A auto | Space play",
                 16, footer, 16, LIGHTGRAY);
        DrawText(TextFormat("checks: source=%zu ring=%zu progress=%zu "
                            "cross=%zu phase=%zu",
                            validation.source_mismatches,
                            validation.ring_discontinuities,
                            validation.primary_discontinuities,
                            validation.cross_discontinuities,
                            validation.phase_discontinuities),
                 16, footer + 23, 16,
                 trace_is_exact(trace, &validation) ? GREEN : RED);
        EndDrawing();
    }
    UnloadTexture(texture);
    CloseWindow();
    free(pixels);
}

int main(int argc, char **argv)
{
    Options options;
    if (!parse_options(argc, argv, &options)) {
        usage(argv[0]);
        return 2;
    }
    ScrollMapTrace trace;
    scroll_map_init(&trace, NULL, 0, 0, 0, 0);
    int ok = options.trace_path ? scroll_map_load(&trace, options.trace_path)
                                : capture_trace(&trace, &options);
    if (!ok) {
        fprintf(stderr, "map lab: could not %s %s\n",
                options.trace_path ? "load" : "capture",
                options.trace_path ? options.trace_path : "terrain stream");
        scroll_map_free(&trace);
        return 1;
    }
    if (options.extract_path &&
        !scroll_map_save(&trace, options.extract_path)) {
        fprintf(stderr, "map lab: could not save %s\n", options.extract_path);
        scroll_map_free(&trace);
        return 1;
    }
    if (options.ppm_path &&
        !scroll_map_export_ppm(&trace, options.ppm_path)) {
        fprintf(stderr, "map lab: could not export %s\n", options.ppm_path);
        scroll_map_free(&trace);
        return 1;
    }
    ScrollMapValidation validation;
    int exact = trace_is_exact(&trace, &validation);
    fprintf(stderr, "map lab: rows=%zu source=%zu ring=%zu progress=%zu "
                    "cross=%zu phase=%zu%s%s\n",
            trace.frame_count, validation.source_mismatches,
            validation.ring_discontinuities,
            validation.primary_discontinuities,
            validation.cross_discontinuities,
            validation.phase_discontinuities,
            options.extract_path ? " trace=" : "",
            options.extract_path ? options.extract_path : "");
    if (!exact) {
        scroll_map_free(&trace);
        return 1;
    }
    if (!options.headless) draw_player(&trace, validation);
    scroll_map_free(&trace);
    return 0;
}
