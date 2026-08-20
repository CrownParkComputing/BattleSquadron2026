/* bsview -- native Battle Squadron front end (raylib, no 68000).
 *
 * Title -> play (1-2 players) -> pause -> options, 50 Hz.  The engine runs one
 * game frame per two display frames (as the original); eng_display_hook
 * renders BOTH display frames of an iteration into a small canvas queue so
 * ship/shot/effect motion is a true 50 Hz.  Canvas 288x255 (the game's real
 * visible window) drawn integer-scaled and centred; fullscreen keeps the
 * aspect.  Controls per docs/itch-battle-squadron.md: P1 arrows +
 * Space/Ctrl/Enter fire + X/Shift nova; P2 WASD + Alt/C fire + V/Tab nova;
 * pads picked up automatically (A/RB fire, B/LB nova, START pause).
 */
#include "bsdata.h"
#include "engine/engine.h"
#include "render.h"
#include "audio.h"
#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define SCALE 3
#define WIN_PREF_W 1920
#define WIN_PREF_H 1080
#define TOP_H 72                                    /* strip above the canvas: the Retro Recompilation logo */
#define BAR_H 96                                   /* grey bar under the canvas (logo + marquee), as SWIV */
#define WIN_W WIN_PREF_W
#define WIN_H WIN_PREF_H

static BsData data;
static uint32_t cbuf[2][BS_VIEW_W * BS_VIEW_H];   /* the two display frames of one engine iteration */
static int hook_n;
static Texture2D tex;
static uint32_t show[BS_VIEW_W * BS_VIEW_H];
static int view_layers = BS_L_ALL;      /* play hides BS_L_HUD: the panel is drawn in the grey bar */
static Texture2D rr_logo; static Font ui_font; static int ui_font_ok;
/* all viewer UI text goes through the same TTF SWIV uses (assets/DejaVuSans.ttf) */
static void ui_text(const char *t, int x, int y, int fs, Color c)
{ if (ui_font_ok) DrawTextEx(ui_font, t, (Vector2){ (float)x, (float)y }, (float)fs, 1, c); else DrawText(t, x, y, fs, c); }
static int ui_measure(const char *t, int fs)
{ return ui_font_ok ? (int)MeasureTextEx(ui_font, t, (float)fs, 1).x : MeasureText(t, fs); }
static Texture2D ttex;                            /* title picture canvas */
static uint32_t tshow[BS_TITLE_W * BS_TITLE_H];

static int mode;                     /* 0 title, 1 play, 2 options, 3 attract demo,
                                        4 debug map, 5 debug sprites, 6 debug sfx */
static long title_idle;              /* display frames on the title without input */
static long smoke;                   /* --smoke N: auto-run N frames, screenshot, exit */
static int debugshots;
static int sp_auto_zoom = 1;     /* sprite page: scale the bob to fill the window */
static int start_mode = -1;      /* BS_START_MODE: jump straight to a page (testing) */               /* --debugshots: screenshot the debug screens, exit */
static int paused, pause_sel, opt_return, title_guard, continue_prompt;   /* opt_return: 0 = title, 1 = back into the paused game */
static int opt_sel;
static int players_sel = 1;
static long vbl;

static struct {
    int master, music_on, sfx_on, fullscreen, difficulty, weapon, lives;
    long hiscore;
} opt = { 8, 1, 1, 0, 1, 3, 3, 1000000 };

static void options_save(void)
{
    FILE *f = fopen("options.txt", "w");
    if (!f) return;
    fprintf(f, "master %d\nmusic %d\nsfx %d\nfullscreen %d\ndifficulty %d\nweapon %d\nlives %d\nhiscore %ld\n",
            opt.master, opt.music_on, opt.sfx_on, opt.fullscreen, opt.difficulty,
            opt.weapon, opt.lives, opt.hiscore);
    fclose(f);
}

static void options_load(void)
{
    FILE *f = fopen("options.txt", "r");
    char k[32];
    long v;
    if (!f) return;
    while (fscanf(f, "%31s %ld", k, &v) == 2) {
        if (!strcmp(k, "master")) opt.master = (int)v;
        else if (!strcmp(k, "music")) opt.music_on = (int)v;
        else if (!strcmp(k, "sfx")) opt.sfx_on = (int)v;
        else if (!strcmp(k, "fullscreen")) opt.fullscreen = (int)v;
        else if (!strcmp(k, "difficulty")) opt.difficulty = (int)v;
        else if (!strcmp(k, "weapon")) opt.weapon = (int)v;
        else if (!strcmp(k, "lives")) opt.lives = (int)v;
        else if (!strcmp(k, "hiscore")) opt.hiscore = v;
    }
    fclose(f);
}

static void options_apply(void)
{
    audio_set(opt.master / 10.0f, opt.music_on, opt.sfx_on);
    render_hiscore = (int)opt.hiscore;
    if (opt.fullscreen != IsWindowFullscreen()) {
        if (opt.fullscreen) {
            int m = GetCurrentMonitor();
            SetWindowSize(GetMonitorWidth(m), GetMonitorHeight(m));
            ToggleFullscreen();
        } else {
            ToggleFullscreen();
            SetWindowSize(WIN_W, WIN_H);
        }
    }
}

/* ---- gamepads: skip keyboard/mouse receivers reported as joysticks ---- */
static int real_pad(int nth)
{
#ifdef __ANDROID__
    return (nth == 0 && IsGamepadAvailable(0)) ? 0 : -1;
#else
    int found = 0;
    for (int i = 0; i < 9; i++) {
        if (!IsGamepadAvailable(i)) continue;
        const char *n = GetGamepadName(i);
        if (!n) n = "";
        if (strcasestr(n, "mouse") || strcasestr(n, "keyboard") ||
            strcasestr(n, "receiver") || GetGamepadAxisCount(i) < 2) continue;
        if (found++ == nth) return i;
    }
    return -1;
#endif
}

static uint8_t pad_joy(int nth)
{
    int p = real_pad(nth);
    uint8_t j = 0;
    if (p < 0) return 0;
    float ax = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X);
    float ay = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
    if (ay < -0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP)) j |= JOY_UP;
    if (ax > 0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) j |= JOY_RIGHT;
    if (ay > 0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) j |= JOY_DOWN;
    if (ax < -0.4f || IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) j |= JOY_LEFT;
    /* FIRE = A / X / right trigger (RT or RB); NOVA = B / Y / left trigger (LT or LB):
     * separate buttons so the smart bomb (JOY_NOVA, input bit 5) never rides on fire */
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
        IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_LEFT) ||
        IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_1) ||
        IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_2) ||
        GetGamepadAxisMovement(p, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f) j |= JOY_FIRE;
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
        IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_UP) ||
        IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_TRIGGER_1) ||
        IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_TRIGGER_2) ||
        GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_TRIGGER) > 0.3f) j |= JOY_NOVA;
    return j;
}

static int pad_start_pressed(void)
{
    for (int n = 0; n < 2; n++) {
        int p = real_pad(n);
        if (p >= 0 && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_RIGHT)) return 1;
    }
    return 0;
}

static int pad_select_pressed(void)
{
    for (int n = 0; n < 2; n++) {
        int p = real_pad(n);
        if (p >= 0 && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_LEFT)) return 1;
    }
    return 0;
}

static uint8_t joy_p1(void)
{
    uint8_t j = 0;
    if (IsKeyDown(KEY_UP)) j |= JOY_UP;
    if (IsKeyDown(KEY_RIGHT)) j |= JOY_RIGHT;
    if (IsKeyDown(KEY_DOWN)) j |= JOY_DOWN;
    if (IsKeyDown(KEY_LEFT)) j |= JOY_LEFT;
    if (IsKeyDown(KEY_SPACE) || IsKeyDown(KEY_RIGHT_CONTROL) || IsKeyDown(KEY_ENTER)) j |= JOY_FIRE;
    if (IsKeyDown(KEY_X) || IsKeyDown(KEY_RIGHT_SHIFT) || IsKeyDown(KEY_LEFT_SHIFT)) j |= JOY_NOVA;
    return j | pad_joy(0);
}

static uint8_t joy_p2(void)
{
    uint8_t j = 0;
    if (IsKeyDown(KEY_W)) j |= JOY_UP;
    if (IsKeyDown(KEY_D)) j |= JOY_RIGHT;
    if (IsKeyDown(KEY_S)) j |= JOY_DOWN;
    if (IsKeyDown(KEY_A)) j |= JOY_LEFT;
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_C)) j |= JOY_FIRE;
    if (IsKeyDown(KEY_V) || IsKeyDown(KEY_TAB)) j |= JOY_NOVA;
    return j | pad_joy(1);
}

/* menu navigation: keyboard + first pad, with auto-repeat */
static int nav_dy_state, nav_repeat;
static int nav_dy(void)
{
    int d = 0;
    uint8_t j = pad_joy(0);
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W) || (j & JOY_UP)) d = -1;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S) || (j & JOY_DOWN)) d = 1;
    if (d == nav_dy_state) {
        if (nav_repeat) { nav_repeat--; return 0; }
        nav_repeat = 6;
        return d;
    }
    nav_dy_state = d;
    nav_repeat = 12;
    return d;
}
static int nav_dx(void)
{
    static int st, rep;
    int d = 0;
    uint8_t j = pad_joy(0);
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A) || (j & JOY_LEFT)) d = -1;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D) || (j & JOY_RIGHT)) d = 1;
    if (d == st) {
        if (rep) { rep--; return 0; }
        rep = 6;
        return d;
    }
    st = d;
    rep = 12;
    return d;
}
static int nav_back(void)                       /* B on the pad, Esc / Backspace on the keys */
{
    int p = real_pad(0);
    return IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_BACKSPACE) ||
           (p >= 0 && (IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) ||
                       IsGamepadButtonPressed(p, GAMEPAD_BUTTON_MIDDLE_LEFT)));
}
static int nav_ok(void)
{
    int p = real_pad(0);
    return IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_SPACE) ||
           (p >= 0 && IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
}

static void hook(void)               /* eng_display_hook: one display frame rendered */
{
    if (hook_n < 2) render_frame(cbuf[hook_n], view_layers);
    hook_n++;
}

/* title/attract audio residency: LODMUS ($3D800, over LODS0S) + LODSPE
 * ($246F0, over LODGAM), exactly the original's overlay swap; game start
 * swaps them back */
static void title_enter(void)
{
    static int welcomed;
    audio_stop();
    if (!bs_load_module(&data, "LODMUS") && !bs_load_module(&data, "LODSPE")) {
        data.stage = -1;                 /* LODMUS clobbered LODS0S: force a stage reload */
        audio_start_title();
        if (!welcomed) { welcomed = 1; audio_title_speech(); }
    }
    mode = 0;
    title_guard = 20;                    /* ignore a stray fire/A on the frame we arrive */
}

/* the original's attract: after the title, the recorded two-player demo runs
 * (engine parity-proven against the oracle's idle run); menu music keeps
 * playing, sfx are the original's RTS-patched stubs (muted), any input
 * returns to the title, nothing is scored persistently */
static void demo_enter(void)
{
    bs_chip = data.chip;
    if (data.stage != 0) {               /* stage-0 gfx without LODS0S (LODMUS lives there) */
        if (bs_load_module(&data, "LODS0F") || bs_load_module(&data, "LODS0T")) return;
        data.stage = 0;
    }
    eng_demo_init();
    render_stage(&data);
    view_layers = BS_L_ALL;                          /* the attract pages use the game's own panel labels */
    render_frame(cbuf[0], view_layers);
    memcpy(cbuf[1], cbuf[0], sizeof cbuf[0]);
    hook_n = 2;
    mode = 3;
}

static void start_game(void)
{
    bs_chip = data.chip;
    bs_load_module(&data, "LODGAM");     /* back over LODSPE */
    data.stage = -1;                     /* always reload: the title left LODMUS over LODS0S */
    bs_load_stage(&data, 0);
    eng_init(0, players_sel, opt.weapon, opt.lives, opt.difficulty);
    render_stage(&data);
    audio_start_game();
    view_layers = BS_L_ALL & ~BS_L_HUD;              /* the status panel is drawn in the grey bar */
    render_frame(cbuf[0], view_layers);
    memcpy(cbuf[1], cbuf[0], sizeof cbuf[0]);
    hook_n = 2;
    paused = 0;
    mode = 1; view_layers = BS_L_ALL & ~BS_L_HUD;
}

static void hs_submit(const char *name, long score, int shots, int hits);
static void hs_load(void);
static void end_game(void)
{
    long s1 = 0, s2 = 0;
    for (int i = 0; i < 9; i++) {
        char c1 = g.players[0].score[i], c2 = g.players[1].score[i];
        s1 = s1 * 10 + (c1 >= '0' && c1 <= '9' ? c1 - '0' : 0);
        s2 = s2 * 10 + (c2 >= '0' && c2 <= '9' ? c2 - '0' : 0);
    }
    if (s1 > opt.hiscore) opt.hiscore = s1;
    if (s2 > opt.hiscore) opt.hiscore = s2;
    render_hiscore = (int)opt.hiscore;
    options_save();
    { char nm[8];
      for (int i = 0; i < 2; i++) {
          const Player *p = &g.players[i];
          if (!p->joined39) continue;
          snprintf(nm, sizeof nm, "%.3s", p->initials[0] ? (const char *)p->initials : (i ? "P2" : "P1"));
          hs_submit(nm, i ? s2 : s1, g.stat_shots[i], g.stat_hits[i]);
      } }
    title_enter();
}

/* ---- canvas text helpers (chip font via render_text, scaled by pixel copy) ---- */
static void clear_canvas(uint32_t *c, uint32_t rgba)
{
    for (int i = 0; i < BS_VIEW_W * BS_VIEW_H; i++) c[i] = rgba;
}
static void text_big(uint32_t *c, int cx, int y, const char *s, uint32_t col, int scale)
{
    static uint32_t tmp[BS_VIEW_W * 10];
    int w = (int)strlen(s) * 8;
    if (w > BS_VIEW_W) w = BS_VIEW_W;
    memset(tmp, 0, sizeof tmp);
    render_text(tmp, 0, 0, s, col);
    int x0 = cx - w * scale / 2;
    for (int yy = 0; yy < 10 * scale; yy++)
        for (int xx = 0; xx < w * scale; xx++) {
            uint32_t v = tmp[(yy / scale) * BS_VIEW_W + xx / scale];
            int dx = x0 + xx, dy = y + yy;
            if (v && dx >= 0 && dx < BS_VIEW_W && dy >= 0 && dy < BS_VIEW_H)
                c[dy * BS_VIEW_W + dx] = v;
        }
}
static void text_c(uint32_t *c, int y, const char *s, uint32_t col)
{
    text_big(c, BS_VIEW_W / 2, y, s, col, 1);
}

#define GOLD32 0xFF44AADDu           /* 0xAABBGGRR: da4-ish */
#define WHITE32 0xFFFFFFFFu
#define GREY32 0xFF999999u
#define CYAN32 0xFFDDCC66u

static const char *W_NAMES[4] = { "MAGNETIC TORPEDO", "SIDEWINDER", "SEEKER MISSILE", "NOSE CANNON" };

/* the real LODINT title picture; the F3/F4/F5 labels are the title code's
 * dynamic menu bands, reproduced with the chip font at the measured positions
 * (titlecmp proves the picture itself pixel-exact against the oracle) */
/* the picture's baked F-key row is covered by a pad-navigable menu (up/down to
 * move, left/right or fire to change, fire on START to play) */
static int title_sel, title_page;                  /* page 0 = main menu, 1 = debug menu */
#define TITLE_MAIN_N 7
#define TITLE_DBG_N  4
static int title_count(void) { return title_page ? TITLE_DBG_N : TITLE_MAIN_N; }
static void title_line(int i, char *out, size_t n)
{
    if (title_page) {
        static const char *D[TITLE_DBG_N] = { "MAP VIEWER", "SPRITE VIEWER", "SOUND TEST", "BACK" };
        snprintf(out, n, "%s", D[i]);
        return;
    }
    switch (i) {
    case 0: snprintf(out, n, "START GAME"); break;
    case 1: snprintf(out, n, "SOUND FX %s", opt.sfx_on ? "ON" : "OFF"); break;
    case 2: snprintf(out, n, "MUSIC %s", opt.music_on ? "ON" : "OFF"); break;
    case 3: snprintf(out, n, "HIGH SCORES"); break;
    case 4: snprintf(out, n, "OPTIONS"); break;
    case 5: snprintf(out, n, "DEBUG"); break;
    default: snprintf(out, n, "QUIT"); break;
    }
}
/* a drifting starfield fills the menu panels instead of flat black */
static void menu_backdrop(uint32_t *c, int y0, int y1)
{
    for (int y = y0; y < y1; y++)
        for (int x = 8; x < BS_TITLE_W - 8; x++) c[y * BS_TITLE_W + x] = 0xFF000000u;
    static const uint32_t COL[4] = { 0xFF6688AAu, 0xFFAACCEEu, 0xFF88AAFFu, 0xFF445577u };
    uint32_t r = 0x1234567u;
    int drift = (int)(vbl / 3);
    for (int i = 0; i < 150; i++) {
        r = r * 1103515245u + 12345u;
        int x = 10 + (int)((r >> 16) % (uint32_t)(BS_TITLE_W - 20));
        r = r * 1103515245u + 12345u;
        int y = y0 + (int)(((r >> 16) + (uint32_t)drift) % (uint32_t)(y1 - y0));
        c[y * BS_TITLE_W + x] = COL[(i + (drift >> 4)) & 3];
    }
}
static void draw_title(uint32_t *c)
{
    render_title(c);
    /* measured bands of the baked picture: 1UP/2UP 95..102, F1..F4 105..117,
     * FX/MUSIC text 119..130, F5 + player line 144..156, PRESS BUTTON 165..178 */
    menu_backdrop(c, 90, 190);

}

static void draw_options(uint32_t *c)            /* same chip font / look as the title menu */
{
    render_title(c);
    menu_backdrop(c, 90, 200);
}

/* the option rows themselves are drawn in the raylib layer (draw_menu_overlay) */
static void draw_options_rows_unused(void)
{
}

/* ---- in-game side panels: P1 down the left margin, P2 down the right ---- */
static void panel_logo(int cx, int y, int maxw)
{
    if (!rr_logo.id) return;
    float ls = maxw / (float)rr_logo.width;
    if (ls > 0.46f) ls = 0.46f;
    DrawTextureEx(rr_logo, (Vector2){ cx - rr_logo.width * ls / 2, (float)y }, 0, ls, WHITE);
}
static void panel_line(int x, int *y, const char *lab, const char *val, Color lc, Color vc, int fs, int rt)
{
    /* rt: right-aligned panel (player two) - value hugs the window edge, label sits inboard */
    if (rt) {
        if (val) ui_text(val, x - ui_measure(val, fs), *y - 2, fs, vc);
        ui_text(lab, x - 130 - ui_measure(lab, fs - 6), *y, fs - 6, lc);
    } else {
        ui_text(lab, x, *y, fs - 6, lc);
        if (val) ui_text(val, x + 130, *y - 2, fs, vc);
    }
    *y += fs + 10;
}
static void draw_side_stats(int sw, int margin, int right_x)
{
    static const char *DIFFN[3] = { "EASY", "NORMAL", "HARD" };
    const Color C_GOLD = { 255, 238, 136, 255 }, C_WHT = { 235, 235, 235, 255 },
                C_CY = { 136, 221, 255, 255 }, C_GY = { 150, 152, 166, 255 };
    char b[64];
    for (int pi = 0; pi < 2; pi++) {
        const Player *pl = &g.players[pi];
        int rt = (pi == 1);
        int x = rt ? sw - 24 : 24;                   /* panel edge the text is aligned to */
        int cx = rt ? right_x + margin / 2 : margin / 2;
        panel_logo(cx, 12, margin - 48);
        int y = 12 + (int)(rr_logo.id ? rr_logo.height * 0.46f : 40) + 26;
        const char *up = rt ? "2UP" : "1UP";
        ui_text(up, rt ? x - ui_measure(up, 30) : x, y, 30, rt ? C_CY : C_GOLD);
        y += 38;
        char sc[16]; memcpy(sc, pl->score, 8); sc[8] = 0;
        if (!sc[0]) memcpy(sc, "00000000", 9);
        ui_text(sc, rt ? x - ui_measure(sc, 34) : x, y, 34, C_WHT);
        y += 52;
        if (!pl->joined39) {
            if (rt) ui_text("PRESS FIRE", x - ui_measure("PRESS FIRE", 22), y, 22, C_GY);
            continue;
        }
        int shots = g.stat_shots[pi], hits = g.stat_hits[pi];
        snprintf(b, sizeof b, "%d", pl->lives56);  panel_line(x, &y, "LIVES", b, C_GY, C_WHT, 26, rt);
        snprintf(b, sizeof b, "%d", pl->nova66);   panel_line(x, &y, "NOVA",  b, C_GY, C_WHT, 26, rt);
        snprintf(b, sizeof b, "%02X", pl->bonus97);panel_line(x, &y, "GOLD",  b, C_GY, C_GOLD, 26, rt);
        snprintf(b, sizeof b, "%d", shots);        panel_line(x, &y, "SHOTS", b, C_GY, C_WHT, 26, rt);
        snprintf(b, sizeof b, "%d", hits);         panel_line(x, &y, "HITS",  b, C_GY, C_WHT, 26, rt);
        snprintf(b, sizeof b, "%d%%", shots ? hits * 100 / shots : 0);
        panel_line(x, &y, "ACC", b, C_GY, C_CY, 26, rt);
        if (!rt) {
            snprintf(b, sizeof b, "%08ld", (long)opt.hiscore);
            y += 18; panel_line(x, &y, "HIGH", b, C_GY, C_GOLD, 26, 0);
            panel_line(x, &y, "MODE", DIFFN[opt.difficulty % 3], C_GY, C_WHT, 26, 0);
        }
    }
}

static void option_adjust(int d);
static int button(Rectangle r, const char *label, int active);
/* menu text for the title / options / high-score pages: drawn in the raylib
 * layer with the UI font so it is not cramped by the 320x200 chip font.  Rows
 * are given in canvas coordinates and mapped onto the scaled picture. */
static int menu_dx, menu_dy, menu_dw, menu_dh;
static int menu_fs(void) { int f = menu_dh / 30; return f < 14 ? 14 : (f > 34 ? 34 : f); }
static void menu_row(int canvas_y, const char *s, int sel, int fs, Color col)
{
    int y = menu_dy + canvas_y * menu_dh / BS_TITLE_H;
    int w = ui_measure(s, fs), x = menu_dx + (menu_dw - w) / 2;
    if (sel) {
        DrawRectangle(x - 22, y - 4, w + 44, fs + 8, (Color){ 30, 54, 92, 210 });
        DrawRectangleLines(x - 22, y - 4, w + 44, fs + 8, (Color){ 120, 175, 240, 255 });
    }
    ui_text(s, x, y, fs, sel ? RAYWHITE : col);
}
static void menu_at(int canvas_x, int canvas_y, const char *s, int fs, Color col, int right)
{
    int y = menu_dy + canvas_y * menu_dh / BS_TITLE_H;
    int x = menu_dx + canvas_x * menu_dw / BS_TITLE_W;
    if (right) x -= ui_measure(s, fs);
    ui_text(s, x, y, fs, col);
}
static void menu_row_lr(int canvas_y, const char *label, const char *val, int sel, int fs)
{
    char b[64];
    snprintf(b, sizeof b, val && val[0] ? "%s   %s" : "%s%s", label, val ? val : "");
    menu_row(canvas_y, b, sel, fs, (Color){ 185, 190, 205, 255 });
}
static int ui_pressed(void);
static int ui_hit(Rectangle r);
#define PAUSE_ITEMS 3
static void draw_pause_ui(int sw, int sh)
{
    const int H = sh;                                /* in game the bar is gone: the panel owns the window */
    DrawRectangle(0, 0, sw, H, (Color){ 0, 0, 0, 170 });
    const char *t = "PAUSED";
    ui_text(t, (sw - ui_measure(t, 56)) / 2, H / 2 - 190, 56, (Color){ 255, 214, 92, 255 });
    static const char *IT[PAUSE_ITEMS] = { "RESUME", "OPTIONS", "QUIT TO TITLE" };
    int w = 420, x0 = (sw - w) / 2, y0 = H / 2 - 90, rh = 70;
    for (int i = 0; i < PAUSE_ITEMS; i++) {
        Rectangle r = { (float)x0, (float)(y0 + i * rh), (float)w, (float)(rh - 10) };
        int sel = (pause_sel == i);
        DrawRectangleRec(r, sel ? (Color){ 34, 58, 96, 255 } : (Color){ 22, 22, 29, 235 });
        DrawRectangleLinesEx(r, 2, sel ? (Color){ 120, 175, 240, 255 } : (Color){ 55, 55, 66, 255 });
        int fs = 30;
        ui_text(IT[i], x0 + (w - ui_measure(IT[i], fs)) / 2, (int)r.y + (int)(r.height - fs) / 2, fs,
                sel ? RAYWHITE : (Color){ 185, 185, 198, 255 });
        if (ui_hit(r)) { pause_sel = i; if (ui_pressed()) { if (i == 0) paused = 0; else if (i == 1) { opt_sel = 0; opt_return = 1; mode = 2; } else end_game(); } }
    }
    const char *hint = "pad: up/down select, A confirm, START resume";
    ui_text(hint, (sw - ui_measure(hint, 20)) / 2, y0 + PAUSE_ITEMS * rh + 16, 20, (Color){ 150, 150, 165, 255 });
}

static void option_adjust(int d)
{
    switch (opt_sel) {
    case 0: opt.master += d; if (opt.master < 0) opt.master = 0; if (opt.master > 10) opt.master = 10; break;
    case 1: opt.music_on = !opt.music_on; break;
    case 2: opt.sfx_on = !opt.sfx_on; break;
    case 3: opt.difficulty = (opt.difficulty + d + 3) % 3; break;
    case 4: opt.weapon = (opt.weapon + d + 4) % 4; break;
    case 5: opt.lives = opt.lives + d; if (opt.lives < 1) opt.lives = 1; if (opt.lives > 4) opt.lives = 4; break;
    case 6: opt.fullscreen = !opt.fullscreen; break;
    }
    options_apply();
    options_save();
}

static void overlay_initials(uint32_t *c)
{
    for (int i = 0; i < 2; i++) {
        Player *p = &g.players[i];
        if (!p->joined39 || p->state38 != 0xC8) continue;
        char buf[32];
        if (!p->f91) {
            snprintf(buf, sizeof buf, "P%d NAME %c%c%c%c", i + 1,
                     p->initials[0], p->initials[1], p->initials[2],
                     (vbl & 8) ? (p->cursor42 < 3 ? '_' : ']') : p->initials[3]);
            text_c(c, 150 + i * 14, buf, WHITE32);
        } else continue_prompt |= 1 << i;            /* drawn in the bar: the board keeps its rows */
    }
}

/* ---- native hiscore table: the original keeps one score, we keep a board with
 * shots fired and accuracy per entry (bs_hiscores.txt) ---- */
typedef struct { char name[8]; long score; int shots, hits, diff; } HsRow;
static HsRow hs_rows[8];
static void hs_load(void)
{
    FILE *f = fopen("bs_hiscores.txt", "r");
    if (!f) return;
    for (int i = 0; i < 8; i++) {
        char nm[16];
        if (fscanf(f, "%7s %ld %d %d %d", nm, &hs_rows[i].score, &hs_rows[i].shots,
                   &hs_rows[i].hits, &hs_rows[i].diff) != 5) break;
        snprintf(hs_rows[i].name, sizeof hs_rows[i].name, "%s", nm);
    }
    fclose(f);
}
static void hs_save(void)
{
    FILE *f = fopen("bs_hiscores.txt", "w");
    if (!f) return;
    for (int i = 0; i < 8; i++)
        if (hs_rows[i].score)
            fprintf(f, "%s %ld %d %d %d\n", hs_rows[i].name[0] ? hs_rows[i].name : "---",
                    hs_rows[i].score, hs_rows[i].shots, hs_rows[i].hits, hs_rows[i].diff);
    fclose(f);
}
static void hs_submit(const char *name, long score, int shots, int hits)
{
    if (score <= 0) return;
    int at = 8;
    for (int i = 0; i < 8; i++) if (score > hs_rows[i].score) { at = i; break; }
    if (at == 8) return;
    for (int i = 7; i > at; i--) hs_rows[i] = hs_rows[i - 1];
    snprintf(hs_rows[at].name, sizeof hs_rows[at].name, "%.4s", name && name[0] ? name : "---");
    hs_rows[at].score = score; hs_rows[at].shots = shots; hs_rows[at].hits = hits;
    hs_rows[at].diff = opt.difficulty;
    hs_save();
}
static void draw_hiscores(uint32_t *c)          /* same chip font as the title menu */
{
    render_title(c);
    menu_backdrop(c, 90, 200);
}

static void draw_hiscore_rows_unused(void)
{
}

/* ==================== DEBUG screens ====================
 * SWIV-viewer style: a DEBUG toggle on the title (F9 or the on-screen button)
 * exposes MAP (mode 4), SPRITES (mode 5) and SFX/MUSIC (mode 6) dev screens.
 * All navigable with the controller cursor (left stick / d-pad moves, A
 * clicks) or the mouse, exactly like SWIV's viewer.c. */

/* ---- controller cursor: virtual pointer on the first pad, A = click ---- */
static Vector2 vptr = { 430, 600 }; static int vptr_on, vclick, vdown;
static void vptr_update(int menus_active)
{
    vclick = vdown = 0;
    int p = real_pad(0);
    if (p < 0 || !menus_active) { vptr_on = 0; return; }
    float ax = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_X);
    float ay = GetGamepadAxisMovement(p, GAMEPAD_AXIS_LEFT_Y);
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_LEFT)) ax = -1;
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) ax = 1;
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_UP)) ay = -1;
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_FACE_DOWN)) ay = 1;
    if (ax > -0.25f && ax < 0.25f) ax = 0;
    if (ay > -0.25f && ay < 0.25f) ay = 0;
    if (ax != 0 || ay != 0) { vptr.x += ax * 12; vptr.y += ay * 12; vptr_on = 1; }
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    if (vptr.x < 0) vptr.x = 0;
    if (vptr.x > sw - 1) vptr.x = (float)(sw - 1);
    if (vptr.y < 0) vptr.y = 0;
    if (vptr.y > sh - 1) vptr.y = (float)(sh - 1);
    if (IsGamepadButtonPressed(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) { vclick = 1; vptr_on = 1; }
    if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_FACE_DOWN)) vdown = 1;
    if (GetMouseDelta().x != 0 || GetMouseDelta().y != 0) vptr_on = 0;
}
static void vptr_draw(void)
{
    if (!vptr_on) return;
    DrawCircleV(vptr, 11, (Color){ 0, 0, 0, 150 });
    DrawCircleV(vptr, 8, (Color){ 255, 238, 136, 255 });
}
static int ui_pressed(void) { return IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || vclick; }
static int ui_down(void) { return IsMouseButtonDown(MOUSE_BUTTON_LEFT) || vdown; }
static int ui_hit(Rectangle r)
{
    Vector2 p = vptr_on ? vptr : GetMousePosition();
    return CheckCollisionPointRec(p, r);
}
static int button(Rectangle r, const char *label, int active)
{
    int hot = ui_hit(r);
    Color bg = active ? (Color){ 70, 130, 200, 255 } : hot ? (Color){ 80, 80, 90, 255 }
                                                          : (Color){ 50, 50, 58, 255 };
    DrawRectangleRec(r, bg);
    DrawRectangleLinesEx(r, 1, (Color){ 120, 120, 130, 255 });
    int fs = 18, tw = ui_measure(label, fs);
    while (tw > r.width - 6 && fs > 8) { fs -= 2; tw = ui_measure(label, fs); }
    ui_text(label, (int)(r.x + (r.width - tw) / 2), (int)(r.y + (r.height - fs) / 2), fs, RAYWHITE);
    return hot && ui_pressed();
}
static int held(Rectangle r) { return ui_hit(r) && ui_down(); }

static int debug_ui;                     /* title DEBUG toggle */
static int dbg_stage;                    /* stage shown in MAP/SPRITES (0..3) */
static uint32_t pal32(uint16_t v)        /* rgb12 -> canvas RGBA (R8G8B8A8 LE) */
{
    uint8_t r, gg, b;
    bs_rgb12(v, &r, &gg, &b);
    return 0xFF000000u | ((uint32_t)b << 16) | ((uint32_t)gg << 8) | r;
}

/* ---- MAP viewer: the full 384 x 8192 stage map (bsdata decoders) ---- */
#define MAP_PXW 384
#define MAP_PXH 8192
static Texture2D map_tex; static int map_tex_stage = -1;
static float map_scroll = MAP_PXH;       /* texture row at the top of the view (clamped) */
static int map_overlay = 1;

/* the LAB_3078 tile-word spawn triggers (src/engine/engine.c object_spawner)
 * + template names from re/handlers_objects.txt */
typedef struct { uint16_t tile; uint32_t tmpl; const char *name; int pmin, pmax; } MapTrig;
static const MapTrig TRIG0[] = { { 0x6180, 0x2DA8, "TURRET", 0, 0 }, { 0x5640, 0x2E68, "BUILDING (pair $5CD0)", 0, 0 },
    { 0x0280, 0x2B98, "LAUNCH PAD ($3E8-$1F40)", 0x3E8, 0x1F40 }, { 0x8020, 0x2EC8, "BEACON", 0, 0 },
    { 0x5D20, 0x2EF8, "BUILDING", 0, 0 }, { 0x5D70, 0x2F28, "BUILDING", 0, 0 },
    { 0x5910, 0x2F58, "MINE A", 0, 0 }, { 0x59B0, 0x2F88, "MINE B", 0, 0 } };
static const MapTrig TRIG1[] = { { 0x5C80, 0x2DD8, "HATCH GUN", 0, 0 }, { 0x00A0, 0x2FE8, "HATCH SPINNER", 0, 0 },
    { 0x0230, 0x3018, "HATCH SPINNER", 0, 0 }, { 0x0D70, 0x2C28, "2-PHASE CANNON", 0, 0 },
    { 0x92E0, 0x3048, "INVIS SPAWNER (<$19C8)", 0, 0x19C8 } };
static const MapTrig TRIG2[] = { { 0x7260, 0x2BC8, "EMERGING TURRET", 0, 0 }, { 0x9420, 0x2E98, "BEACON", 0, 0 },
    { 0x0CD0, 0x2E08, "SILO", 0, 0 } };
static const MapTrig TRIG3[] = { { 0x8C00, 0x2B68, "BUNKER", 0, 0 }, { 0x9420, 0x2E38, "BLOCK", 0, 0 },
    { 0x92E0, 0x2BF8, "RISING TURRET 25% (<$DDE)", 0, 0x0DDE }, { 0x3A70, 0x2C58, "CRATE", 0, 0 },
    { 0x4BF0, 0x2C88, "HIDDEN", 0, 0 }, { 0x3ED0, 0x2CB8, "HIDDEN", 0, 0 }, { 0x3340, 0x2CE8, "HIDDEN", 0, 0 },
    { 0x4E70, 0x2D18, "HIDDEN", 0, 0 }, { 0x4290, 0x2D48, "HIDDEN", 0, 0 }, { 0x3520, 0x2D78, "HIDDEN", 0, 0 } };
static const MapTrig *STAGE_TRIGS[4] = { TRIG0, TRIG1, TRIG2, TRIG3 };
static const int STAGE_NTRIGS[4] = { 8, 5, 3, 10 };
/* stage-0 hangar gates: object_spawner spawns the gate ($2FB8) at these progress marks */
static const struct { int progress, col; } GATES[3] = { { 0x0F10, 11 }, { 0x1490, 9 }, { 0x1DD0, 12 } };
static int gate_row(int progress)        /* map row index for a progress value (see draw_terrain) */
{ return 512 - (((progress - 1) >> 4) + 1); }

typedef struct { short row, col, trig; } TrigHit;
static TrigHit trig_hits[4096]; static int n_trig_hits;

static int dbg_stage_load(void)          /* make dbg_stage's modules resident */
{
    if (data.stage == dbg_stage) return 0;
    audio_stop();                        /* stage 0 reload puts LODS0S back over LODMUS */
    if (bs_load_stage(&data, dbg_stage)) return -1;
    return 0;
}

static void map_build(void)
{
    if (map_tex_stage == dbg_stage) return;
    if (dbg_stage_load()) return;
    static uint8_t *idx; static uint32_t *rgba;
    if (!idx) { idx = malloc((size_t)MAP_PXW * MAP_PXH); rgba = malloc((size_t)MAP_PXW * MAP_PXH * 4); }
    bs_map_render(&data, idx, MAP_PXW);
    uint16_t pal[32]; uint32_t cols[32];
    bs_palette(&data, dbg_stage, pal);
    for (int i = 0; i < 32; i++) cols[i] = pal32(pal[i]);
    for (size_t i = 0; i < (size_t)MAP_PXW * MAP_PXH; i++) rgba[i] = cols[idx[i] & 31];
    Image im = { .data = rgba, .width = MAP_PXW, .height = MAP_PXH,
                 .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    if (map_tex.id) UnloadTexture(map_tex);
    map_tex = LoadTextureFromImage(im);
    /* precompute the trigger-tile matches for the overlay */
    n_trig_hits = 0;
    for (int r = 0; r < 512; r++)
        for (int c = 0; c < 24; c++) {
            uint16_t w = bs_w(&data, 0x44000 + (uint32_t)r * 0x30 + (uint32_t)c * 2);
            int prog = (512 - r) * 16;   /* exposure progress of this row (row 511 = start) */
            for (int t = 0; t < STAGE_NTRIGS[dbg_stage]; t++) {
                const MapTrig *tr = &STAGE_TRIGS[dbg_stage][t];
                if (w != tr->tile) continue;
                if (tr->pmin && prog < tr->pmin) continue;   /* outside the engine's */
                if (tr->pmax && prog >= tr->pmax) continue;  /* progress window */
                if (n_trig_hits < 4096)
                    trig_hits[n_trig_hits++] = (TrigHit){ (short)r, (short)c, (short)t };
            }
        }
    map_tex_stage = dbg_stage;
    map_scroll = MAP_PXH;                /* start at the bottom: row 511 = level start */
}

/* ---- SPRITES viewer ---- */
#define SPBUF 192
static int sp_cat, sp_idx, sp_frame, sp_anim, sp_zoom = 4;
static double sp_t;
static Texture2D sp_tex; static uint32_t sp_rgba[SPBUF * SPBUF];
static int sp_w, sp_h, sp_nframes = 1;

/* The browsable bobs are the sprite catalog in bsdata.c, filtered by the stage
 * whose overlays are resident: a descriptor's gfx pointer normally lands in a
 * stage module, so showing every type against every stage is what used to fill
 * this screen with noise.  Frame counts and run-time dimensions come from the
 * catalog too (tools/spritecheck.c proves them against the live renderer). */
static int sp_list[64], sp_nlist, sp_list_key = -1;
static void sp_relist(void)
{
    int key = sp_cat * 8 + dbg_stage;
    if (key == sp_list_key) return;
    sp_list_key = key;
    sp_nlist = 0;
    if (sp_cat < 2)
        for (int i = 0; i < bs_sprite_count() && sp_nlist < 64; i++) {
            const BsSprite *e = bs_sprite(i);
            if (e->kind != (sp_cat == 0 ? BS_SPR_HOSTILE : BS_SPR_OBJECT)) continue;
            if (!(e->stages & (1u << dbg_stage))) continue;
            sp_list[sp_nlist++] = i;
        }
    if (sp_idx >= sp_nlist) sp_idx = 0;
}
static int sp_count(void) { return sp_cat == 2 ? 128 : (sp_nlist ? sp_nlist : 1); }
static int sp_frames_of(void)
{
    if (sp_cat == 2 || !sp_nlist) return 1;
    return bs_sprite(sp_list[sp_idx])->frames;
}
static void sp_build(void)
{
    if (dbg_stage_load()) return;
    sp_relist();
    uint16_t pal[32]; uint32_t cols[32];
    bs_palette(&data, dbg_stage, pal);
    for (int i = 0; i < 32; i++) cols[i] = pal32(pal[i]);
    for (int i = 0; i < SPBUF * SPBUF; i++)          /* checkerboard = transparent */
        sp_rgba[i] = ((i % SPBUF) / 8 + (i / SPBUF) / 8) & 1 ? 0xFF303038u : 0xFF26262Cu;
    sp_w = sp_h = 16;
    sp_nframes = sp_frames_of();
    if (sp_frame >= sp_nframes) sp_frame = 0;
    if (sp_cat < 2) {
        BsBob b;
        int ok = sp_nlist && !bs_sprite_bob(&data, sp_list[sp_idx], &b);
        if (ok && b.vis_w > 0 && b.h > 0) {
            static uint8_t idx[SPBUF * SPBUF], alpha[SPBUF * SPBUF];
            int w = b.vis_w > SPBUF ? SPBUF : b.vis_w, h = b.h > SPBUF ? SPBUF : b.h;
            if (b.vis_w <= SPBUF && b.h <= SPBUF) {
                bs_bob_frame(&data, &b, sp_frame, idx, alpha);
                for (int y = 0; y < h; y++)
                    for (int x = 0; x < w; x++)
                        if (alpha[y * b.vis_w + x])
                            sp_rgba[y * SPBUF + x] = b.cloak ? 0xFFFFFFFFu
                                                             : cols[idx[y * b.vis_w + x] & 31];
                sp_w = w; sp_h = h;
            }
        }
    } else {                                         /* HW sprites / effect frames ($C6B6 table) */
        uint32_t ptr = bs_hwsprite_ptr(&data, sp_idx);
        int h = (sp_idx >= 0x58 && sp_idx < 0x60) ? 8 : (sp_idx >= 0x60 && sp_idx < 0x80) ? 12 : 16;
        if (ptr && ptr < 0x80000 - (uint32_t)h * 4) {
            static uint8_t hw[16 * 64];
            bs_hwsprite(&data, ptr, h, hw);
            /* effect sprite colour bank $1F1E (LAB_1FAE row 0), as render.c */
            uint32_t bank[4] = { 0, pal32(bs_w(&data, 0x1F1E)),
                                 pal32(bs_w(&data, 0x1F1E + 16)), pal32(bs_w(&data, 0x1F1E + 32)) };
            for (int y = 0; y < h; y++)
                for (int x = 0; x < 16; x++)
                    if (hw[y * 16 + x])
                        sp_rgba[y * SPBUF + x] = bank[hw[y * 16 + x] & 3];
            sp_w = 16; sp_h = h;
        }
    }
    UpdateTexture(sp_tex, sp_rgba);
}

/* ---- SFX / MUSIC screen: the LODGAM triggers (re/sfx_triggers.txt) ---- */
typedef struct { int n; const char *label; } SfxRow;
static const SfxRow SFX_ROWS[] = {
    { 59, "JINGLE: TITLE / STAGE CLEAR (ch3)" },
    { 57, "NOVA BOMB FIRED" },
    { 56, "MINE DROP / HAZARD" },
    { 53, "WRECK BONUS COLLECTED" },
    { 28, "HOSTILE DESTROYED" },
    { 29, "SCENERY DESTROYED / TYPE-2 KILL" },
    { 20, "HOSTILE DESTROYED (TYPE 2 ALT)" },
    { 50, "HOSTILE HIT (TYPE 3/D)" },
    { 51, "HOSTILE HIT (TYPE 8/C)" },
    { 52, "HOSTILE HIT (TYPE 4)" },
    { 24, "OBJECT TYPE $28 / SPAWN WARNING" },
    { 25, "OBJECT TYPE 03/04" },
    { 26, "HOSTILE SCRIPT OP" },
    { 30, "OBJECT TYPE 02/04" },
    { 31, "OBJECT TYPE $2x SHARED" },
    { 48, "FIRE: MAGNETIC TORPEDO" },
    { 54, "FIRE: SIDEWINDER" },
    { 49, "FIRE: SEEKER MISSILE" },
    { 55, "FIRE: NOSE CANNON" },
    { 40, "FIRE: UPGRADE ROW 5" },
    { 124, "FIRE: UPGRADE ROW 6" },
    { 78, "FIRE: UPGRADE ROW 9" },
    { 60, "FIRE: UPGRADE ROW 10" },
    { 97, "FIRE: UPGRADE ROW 11" },
    { 8, "FIRE: UPGRADE ROW 14" },
};
#define N_SFX_ROWS ((int)(sizeof SFX_ROWS / sizeof SFX_ROWS[0]))
static const char *TRACK_NAMES[10] = { "1 GAME", "2 RESTART", "3 GAME OVER", "4 STAGE CLEAR", "5 DEATH",
    "6", "7", "8 EXTRA LIFE", "9 BONUS", "10 NOVA" };
static int sfx_amode;                    /* 0 none, 1 LODGAM game driver, 2 LODMUS menu driver */
static int sfx_track = -1;
static void sfx_mode_game(void)          /* route through src/audio.c: LODGAM resident + driver up */
{
    if (sfx_amode == 1) return;
    bs_load_module(&data, "LODGAM");     /* over LODSPE ($246F0) */
    audio_set(opt.master / 10.0f, 0, 1); /* music muted so the triggers are audible */
    audio_start_game();
    sfx_amode = 1; sfx_track = -1;
}
static void sfx_play(int n) { sfx_mode_game(); audio_sfx(n); }
static void sfx_music(int track)
{
    sfx_mode_game();
    audio_set(opt.master / 10.0f, 1, 1);
    audio_track(track);
    sfx_track = track;
}
static void sfx_menu_music(void)
{
    bs_load_module(&data, "LODMUS");     /* over LODS0S ($3D800) */
    data.stage = -1;
    map_tex_stage = -1;
    audio_set(opt.master / 10.0f, 1, opt.sfx_on);
    audio_start_title();
    sfx_amode = 2; sfx_track = 0;
}
static void sfx_silence(void)
{
    if (sfx_amode == 1) { audio_set(opt.master / 10.0f, 0, 1); sfx_track = -1; }
    else if (sfx_amode == 2) { audio_stop(); sfx_amode = 0; sfx_track = -1; }
}

static void debug_leave(void)            /* back to the title: original overlay residency + menu music */
{
    audio_stop();
    sfx_amode = 0; sfx_track = -1;
    title_enter();
    options_apply();
}

/* one full display frame of any debug screen (modes 4/5/6): input + draw */
static void debug_frame(void)
{
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    int bar = 42;
    BeginDrawing();
    ClearBackground((Color){ 24, 24, 30, 255 });
    /* shared top bar */
    if (button((Rectangle){ 4, 4, 84, 34 }, "MAP", mode == 4)) mode = 4;
    if (button((Rectangle){ 92, 4, 84, 34 }, "SPRITES", mode == 5)) mode = 5;
    if (button((Rectangle){ 180, 4, 84, 34 }, "SFX", mode == 6)) { mode = 6; }
    if (mode != 6)
        for (int s = 0; s < 4; s++) {
            char l[8]; snprintf(l, sizeof l, "ST %d", s + 1);
            if (button((Rectangle){ 280 + s * 58, 4, 54, 34 }, l, dbg_stage == s)) { dbg_stage = s; sp_frame = 0; sp_relist(); }
        }
    if (button((Rectangle){ (float)(sw - 88), 4, 84, 34 }, "TITLE", 0) || nav_back()) {
        debug_leave();
        EndDrawing();
        return;
    }

    if (mode == 4) {                     /* ---------- MAP ---------- */
        map_build();
        int vh = sh - bar, vrows = vh / 2;
        float maxs = (float)(MAP_PXH - vrows);
        if (button((Rectangle){ 520, 4, 110, 34 }, "TRIGGERS", map_overlay)) map_overlay ^= 1;
        map_scroll -= GetMouseWheelMove() * 48;
        if (IsKeyDown(KEY_UP)) map_scroll -= 8;
        if (IsKeyDown(KEY_DOWN)) map_scroll += 8;
        if (IsKeyDown(KEY_PAGE_UP)) map_scroll -= 64;
        if (IsKeyDown(KEY_PAGE_DOWN)) map_scroll += 64;
        { int p = real_pad(0);           /* shoulder buttons scroll (cursor owns the stick) */
          if (p >= 0) { if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) map_scroll -= 10;
                        if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) map_scroll += 10; } }
        static int dragging;
        Vector2 mp = GetMousePosition();
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) && (dragging || (mp.y > bar && mp.x < 768))) {
            dragging = 1;
            map_scroll -= GetMouseDelta().y / 2;
        } else dragging = 0;
        if (held((Rectangle){ (float)(sw - 88), (float)(bar + 4), 84, 40 })) map_scroll -= 6;
        if (held((Rectangle){ (float)(sw - 88), (float)(sh - 46), 84, 40 })) map_scroll += 6;
        if (map_scroll < 0) map_scroll = 0;
        if (map_scroll > maxs) map_scroll = maxs;
        if (map_tex.id)
            DrawTexturePro(map_tex, (Rectangle){ 0, map_scroll, MAP_PXW, (float)vrows },
                           (Rectangle){ 0, (float)bar, MAP_PXW * 2, (float)vh }, (Vector2){ 0, 0 }, 0, WHITE);
        if (map_overlay) {
            for (int i = 0; i < n_trig_hits; i++) {
                const TrigHit *t = &trig_hits[i];
                float ty = t->row * 16.0f;
                if (ty + 16 < map_scroll || ty > map_scroll + vrows) continue;
                float y = bar + (ty - map_scroll) * 2, x = t->col * 32.0f;
                Rectangle box = { x, y, 32, 32 };
                DrawRectangleLinesEx(box, 2, RED);
                if (ui_hit(box)) {       /* name on hover to keep the map readable */
                    const MapTrig *tr = &STAGE_TRIGS[dbg_stage][t->trig];
                    char l[96];
                    snprintf(l, sizeof l, "%s  tmpl $%X  tile $%04X  row %d col %d",
                             tr->name, tr->tmpl, tr->tile, t->row, t->col);
                    DrawRectangle((int)x, (int)y - 18, ui_measure(l, 14) + 8, 18, (Color){ 0, 0, 0, 210 });
                    ui_text(l, (int)x + 4, (int)y - 16, 14, YELLOW);
                }
            }
            if (dbg_stage == 0)
                for (int gi = 0; gi < 3; gi++) {
                    float ty = gate_row(GATES[gi].progress) * 16.0f;
                    if (ty < map_scroll || ty > map_scroll + vrows) continue;
                    float y = bar + (ty - map_scroll) * 2;
                    DrawLineEx((Vector2){ 0, y }, (Vector2){ MAP_PXW * 2, y }, 2, SKYBLUE);
                    char l[64];
                    snprintf(l, sizeof l, "GATE %d (progress $%X, col %d)", gi + 1, GATES[gi].progress, GATES[gi].col);
                    ui_text(l, GATES[gi].col * 32, (int)y - 14, 12, SKYBLUE);
                }
        }
        button((Rectangle){ (float)(sw - 88), (float)(bar + 4), 84, 40 }, "UP", 0);
        button((Rectangle){ (float)(sw - 88), (float)(sh - 46), 84, 40 }, "DOWN", 0);
        char st[160];
        { int br = ((int)map_scroll + vrows - 1) / 16; if (br > 511) br = 511;
          snprintf(st, sizeof st, "STAGE %d  rows %d-%d of 511 (bottom row 511 = level start, hover a box for its trigger)  wheel/drag/arrows/LB-RB scroll  triggers %d",
                   dbg_stage + 1, (int)map_scroll / 16, br, n_trig_hits); }
        DrawRectangle(0, sh - 22, sw, 22, (Color){ 0, 0, 0, 170 });
        ui_text(st, 6, sh - 18, 14, RAYWHITE);
    } else if (mode == 5) {              /* ---------- SPRITES ---------- */
        if (!sp_tex.id) {
            Image im = { .data = sp_rgba, .width = SPBUF, .height = SPBUF,
                         .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
            sp_tex = LoadTextureFromImage(im);
        }
        static const char *CATS[3] = { "HOSTILES", "OBJECTS", "HW/FX" };
        int r1 = bar + 6;
        for (int c = 0; c < 3; c++)
            if (button((Rectangle){ (float)(4 + c * 108), (float)r1, 104, 34 }, CATS[c], sp_cat == c)) {
                sp_cat = c; sp_idx = 0; sp_frame = 0;
            }
        sp_relist();
        int n = sp_count();
        if (button((Rectangle){ 340, (float)r1, 44, 34 }, "<", 0) || IsKeyPressed(KEY_PAGE_UP)) { sp_idx = (sp_idx + n - 1) % n; sp_frame = 0; }
        if (button((Rectangle){ 388, (float)r1, 44, 34 }, ">", 0) || IsKeyPressed(KEY_PAGE_DOWN)) { sp_idx = (sp_idx + 1) % n; sp_frame = 0; }
        sp_idx = (sp_idx + n - (int)GetMouseWheelMove()) % n;
        if (button((Rectangle){ 448, (float)r1, 60, 34 }, "< FR", 0) || IsKeyPressed(KEY_LEFT)) sp_frame = (sp_frame + sp_nframes - 1) % (sp_nframes ? sp_nframes : 1);
        if (button((Rectangle){ 512, (float)r1, 60, 34 }, "FR >", 0) || IsKeyPressed(KEY_RIGHT)) sp_frame = (sp_frame + 1) % (sp_nframes ? sp_nframes : 1);
        if (button((Rectangle){ 588, (float)r1, 70, 34 }, "ANIM", sp_anim)) sp_anim ^= 1;
        char zl[8];
        snprintf(zl, sizeof zl, "x%d", sp_zoom);
        if (button((Rectangle){ 664, (float)r1, 54, 34 }, zl, 0)) { sp_auto_zoom = 0; sp_zoom = sp_zoom >= 8 ? 1 : sp_zoom * 2; }
        if (button((Rectangle){ 724, (float)r1, 64, 34 }, "FIT", sp_auto_zoom)) sp_auto_zoom ^= 1;
        if (sp_anim && sp_nframes > 1 && (sp_t += GetFrameTime()) > 0.12) {
            sp_t = 0;
            sp_frame = (sp_frame + 1) % sp_nframes;
        }
        sp_build();
        { int availw = sw - 40, availh = sh - (bar + 56) - 40;      /* fill the page: auto-fit the zoom */
          int fitz = 1;
          while (fitz < 16 && sp_w * (fitz + 1) <= availw && sp_h * (fitz + 1) <= availh) fitz++;
          if (sp_zoom > fitz) sp_zoom = fitz;
          if (sp_auto_zoom) sp_zoom = fitz; }
        int zx = (sw - sp_w * sp_zoom) / 2, zy = bar + 56 + (sh - bar - 78 - sp_h * sp_zoom) / 2;
        DrawRectangle(zx - 4, zy - 4, sp_w * sp_zoom + 8, sp_h * sp_zoom + 8, (Color){ 60, 60, 70, 255 });
        DrawTexturePro(sp_tex, (Rectangle){ 0, 0, (float)sp_w, (float)sp_h },
                       (Rectangle){ (float)zx, (float)zy, (float)(sp_w * sp_zoom), (float)(sp_h * sp_zoom) },
                       (Vector2){ 0, 0 }, 0, WHITE);
        char st[192];
        if (sp_cat < 2 && sp_nlist) {
            const BsSprite *e = bs_sprite(sp_list[sp_idx]);
            BsBob b; bs_sprite_bob(&data, sp_list[sp_idx], &b);
            snprintf(st, sizeof st, "%s %s  %s $%02X  frame %d/%d  %dx%d  gfx $%X  mask $%X  stage %d",
                     sp_cat == 0 ? "HOSTILE" : "OBJECT", e->name,
                     sp_cat == 0 ? "type" : "tmpl", e->type,
                     sp_frame + 1, sp_nframes, sp_w, sp_h, b.planes, b.mask, dbg_stage + 1);
        } else if (sp_cat < 2)
            snprintf(st, sizeof st, "no %s bobs live in stage %d's overlays",
                     sp_cat == 0 ? "hostile" : "object", dbg_stage + 1);
        else
            snprintf(st, sizeof st, "HW SPRITE / EFFECT frame %d ($C6B6 slot)  ptr $%X  16x%d  wheel/pgup-pgdn browse",
                     sp_idx, bs_hwsprite_ptr(&data, sp_idx), sp_h);
        DrawRectangle(0, sh - 22, sw, 22, (Color){ 0, 0, 0, 170 });
        ui_text(st, 6, sh - 18, 14, RAYWHITE);
    } else {                             /* ---------- SFX / MUSIC ---------- */
        static float sc;
        int row_h = 40, list_y = bar + 30, list_h = sh - list_y - 30, lw = sw / 2 - 30;
        ui_text("SFX TRIGGERS (LODGAM $2470E)", 8, bar + 6, 16, (Color){ 255, 238, 136, 255 });
        sc -= GetMouseWheelMove() * row_h * 2;
        { int p = real_pad(0);
          if (p >= 0) { if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) sc += 8;
                        if (IsGamepadButtonDown(p, GAMEPAD_BUTTON_LEFT_TRIGGER_1)) sc -= 8; } }
        float maxs = (float)(N_SFX_ROWS * row_h - list_h);
        if (maxs < 0) maxs = 0;
        if (sc < 0) sc = 0;
        if (sc > maxs) sc = maxs;
        BeginScissorMode(0, list_y, lw, list_h);
        for (int i = 0; i < N_SFX_ROWS; i++) {
            float y = list_y + i * row_h - sc;
            if (y + row_h < list_y || y > list_y + list_h) continue;
            char nl[8];
            snprintf(nl, sizeof nl, "%d", SFX_ROWS[i].n);
            ui_text(nl, 8, (int)y + 10, 16, LIGHTGRAY);
            ui_text(SFX_ROWS[i].label, 48, (int)y + 10, 16, RAYWHITE);
            if (button((Rectangle){ (float)(lw - 92), y + 3, 84, 32 }, "PLAY", 0)) sfx_play(SFX_ROWS[i].n);
        }
        EndScissorMode();
        int mx = lw + 24;
        ui_text("MUSIC (LODGAM tracks)", mx, bar + 6, 16, (Color){ 255, 238, 136, 255 });
        for (int t = 0; t < 10; t++) {
            Rectangle r = { (float)(mx + (t % 2) * ((sw - mx - 24) / 2)), (float)(list_y + (t / 2) * 44), (float)((sw - mx - 40) / 2), 38 };
            if (button(r, TRACK_NAMES[t], sfx_amode == 1 && sfx_track == t + 1)) sfx_music(t + 1);
        }
        if (button((Rectangle){ (float)mx, (float)(list_y + 5 * 44 + 10), (float)((sw - mx - 40) / 2), 38 }, "MENU MUSIC", sfx_amode == 2))
            sfx_menu_music();
        if (button((Rectangle){ (float)(mx + (sw - mx - 24) / 2), (float)(list_y + 5 * 44 + 10), (float)((sw - mx - 40) / 2), 38 }, "SILENCE", 0))
            sfx_silence();
        ui_text("volume: master slider on the OPTIONS screen", mx, list_y + 6 * 44 + 20, 14, LIGHTGRAY);
        ui_text("tracks: 1 game 2 restart 3 game-over 4 stage-clear 5 death", mx, list_y + 6 * 44 + 40, 14, LIGHTGRAY);
        ui_text("8 extra-life 9 bonus 10 nova; MENU = LODMUS title tune", mx, list_y + 6 * 44 + 58, 14, LIGHTGRAY);
        DrawRectangle(0, sh - 22, sw, 22, (Color){ 0, 0, 0, 170 });
        ui_text("re/sfx_triggers.txt routed through src/audio.c (LODGAM driver); leaving restores the title audio", 6, sh - 18, 14, RAYWHITE);
    }
    vptr_draw();
    EndDrawing();
}

int main(int argc, char **argv)
{
    const char *dir = "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--smoke") && i + 1 < argc) smoke = atol(argv[++i]);
        else if (!strcmp(argv[i], "--debugshots")) debugshots = 1;
    }
    if (bs_open(&data, dir)) { fprintf(stderr, "bsview: cannot open %s\n", dir); return 1; }
    if (bs_load_stage(&data, 0)) { fprintf(stderr, "bsview: stage 0 load failed\n"); return 1; }
    if (bs_load_module(&data, "LODINT")) { fprintf(stderr, "bsview: LODINT load failed\n"); return 1; }
    bs_chip = data.chip;

    options_load();
    hs_load();
    if (getenv("BS_START_MODE")) start_mode = atoi(getenv("BS_START_MODE"));
    SetConfigFlags(FLAG_VSYNC_HINT);
    InitWindow(WIN_W, WIN_H, "Battle Squadron");
    if (FileExists("assets/retro_recomp_logo.png")) { rr_logo = LoadTexture("assets/retro_recomp_logo.png"); SetTextureFilter(rr_logo, TEXTURE_FILTER_BILINEAR); }
    if (FileExists("assets/DejaVuSans.ttf")) { ui_font = LoadFontEx("assets/DejaVuSans.ttf", 40, NULL, 0); ui_font_ok = ui_font.texture.id != 0; if (ui_font_ok) SetTextureFilter(ui_font.texture, TEXTURE_FILTER_BILINEAR); }
    SetExitKey(KEY_NULL);
    SetTargetFPS(50);
    audio_init();
    options_apply();

    Image img = { .data = show, .width = BS_VIEW_W, .height = BS_VIEW_H,
                  .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    tex = LoadTextureFromImage(img);
    Image timg = { .data = tshow, .width = BS_TITLE_W, .height = BS_TITLE_H,
                   .mipmaps = 1, .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
    ttex = LoadTextureFromImage(timg);
    eng_display_hook = hook;
    eng_sfx_hook = audio_sfx;
    title_enter();                       /* menu music + the boot welcome speech */

    if (start_mode >= 0) mode = start_mode;      /* testing: open a page directly */
    while (!WindowShouldClose()) {
        audio_tick();
        if (getenv("BS_DEMO_SMOKE")) {               /* attract-demo smoke: straight into the demo */
            if (vbl == 10 && mode == 0) demo_enter();
            if (mode == 3 && vbl == atol(getenv("BS_DEMO_SMOKE"))) {
                TakeScreenshot("build/smoke_demo.png");
                break;
            }
        }
        if (smoke) {
            if (vbl == 24 && mode == 0) TakeScreenshot("build/smoke_title.png");
            if (vbl == 25 && mode == 0) start_game();          /* smoke: auto-start after half a second */
            if (mode == 1 && (long)vbl >= smoke) {
                TakeScreenshot("build/smoke.png");
                break;
            }
        }
        if (start_mode >= 0) {                       /* BS_START_MODE: shoot one page and exit */
            if (vbl == 20) { TakeScreenshot("build/page.png"); break; }
        }
        if (debugshots) {                            /* scripted debug-screen tour + screenshots */
            if (vbl == 2) debug_ui = 1;
            if (vbl == 8) TakeScreenshot("build/debug_title.png");
            if (vbl == 10) {
                mode = 4;
                if (getenv("BS_DBG_STAGE")) dbg_stage = atoi(getenv("BS_DBG_STAGE")) & 3;
            }
            if (vbl == 40 && getenv("BS_DBG_ROW")) map_scroll = (float)(atoi(getenv("BS_DBG_ROW")) * 16);
            if (vbl == 60) TakeScreenshot("build/debug_map.png");
            if (vbl == 62) {
                mode = 5;
                if (getenv("BS_DBG_CAT")) sp_cat = atoi(getenv("BS_DBG_CAT")) % 3;
                if (getenv("BS_DBG_IDX")) sp_idx = atoi(getenv("BS_DBG_IDX"));
            }
            if (vbl == 112) TakeScreenshot("build/debug_sprites.png");
            if (vbl == 114) { mode = 6; sfx_mode_game(); }
            if (vbl == 164) TakeScreenshot("build/debug_sfx.png");
            if (vbl >= 170) break;
        }
        vptr_update(mode >= 4 && mode <= 6);
        if (mode >= 4 && mode <= 6) {                /* DEBUG screens own the whole frame (7 = high scores) */
            debug_frame();
            vbl++;
            continue;
        }
        if (mode == 0) {                             /* title: the original's F-key menu */
            title_idle++;
            if (GetKeyPressed() || pad_joy(0) || pad_joy(1) || joy_p1() || joy_p2() ||
                vptr_on || GetMouseDelta().x != 0 || GetMouseDelta().y != 0) title_idle = 0;
            if (IsKeyPressed(KEY_F9)) debug_ui = !debug_ui;
            { int dy = nav_dy();
              if (title_guard) title_guard--;
              if (dy) { title_sel = (title_sel + dy + title_count()) % title_count(); if (getenv("BS_MENU_DEBUG")) TraceLog(LOG_INFO, "menu: row -> %d", title_sel); }
              int dx = nav_dx(), ok = title_guard ? 0 : nav_ok();
              if (title_page) {                                  /* debug menu */
                  if (nav_back()) { title_page = 0; title_sel = 5; }
                  else if (ok) {
                      if (title_sel == 0) mode = 4; else if (title_sel == 1) mode = 5;
                      else if (title_sel == 2) { mode = 6; sfx_mode_game(); }
                      else { title_page = 0; title_sel = 5; }
                  }
              } else if (title_sel == 1 && (dx || ok)) { opt.sfx_on = !opt.sfx_on; options_apply(); options_save(); }
              else if (title_sel == 2 && (dx || ok)) { opt.music_on = !opt.music_on; options_apply(); options_save(); }
              else if (ok) {
                  if (getenv("BS_MENU_DEBUG")) TraceLog(LOG_INFO, "menu: fire on row %d (page %d)", title_sel, title_page);
                  if (title_sel == 0) start_game();
                  else if (title_sel == 3) { mode = 7; title_guard = 20; }
                  else if (title_sel == 4) { opt_sel = 0; mode = 2; }
                  else if (title_sel == 5) { title_page = 1; title_sel = 0; }
                  else break;                                    /* QUIT */
              }
            }
            if (IsKeyPressed(KEY_F1)) { players_sel = 1; start_game(); }
            else if (IsKeyPressed(KEY_F2)) { players_sel = 2; start_game(); }
            else if (IsKeyPressed(KEY_F3)) { opt.sfx_on = !opt.sfx_on; options_apply(); options_save(); }
            else if (IsKeyPressed(KEY_F4)) { opt.music_on = !opt.music_on; options_apply(); options_save(); }
            else if (IsKeyPressed(KEY_F5)) players_sel = players_sel == 2 ? 1 : 2;
            else if (IsKeyPressed(KEY_O)) { opt_sel = 0; mode = 2; }
            else if (title_idle > 600 && !smoke) demo_enter();   /* 12 s idle -> attract */
            draw_title(tshow);
        } else if (mode == 3) {                      /* attract demo */
            if (GetKeyPressed() || joy_p1() || joy_p2() || pad_start_pressed() ||
                g.demo_frames >= 0xFA0) {            /* $D32: demo runs 4000 game frames */
                title_idle = 0;
                title_enter();
            } else {
                if ((vbl & 1) == 0) {
                    uint8_t joy[2] = { 0, 0 };       /* ignored: the engine replays the recording */
                    hook_n = 0;
                    eng_frame(joy);
                }
                memcpy(show, cbuf[(vbl & 1) < hook_n ? (int)(vbl & 1) : 0], sizeof show);
            }
        } else if (mode == 7) {                      /* high scores */
            if (title_guard) title_guard--;
            if (!title_guard && (nav_ok() || nav_back() || pad_start_pressed())) { mode = 0; title_guard = 10; }
            draw_hiscores(tshow);
        } else if (mode == 2) {                      /* options */
            int d = nav_dy();
            opt_sel = (opt_sel + d + 8) % 8;
            int dx = nav_dx();
            if (dx) option_adjust(dx);
            if (nav_ok() && opt_sel == 7) { mode = opt_return ? 1 : 0; opt_return = 0; }
            else if (nav_ok() && opt_sel != 7) option_adjust(1);
            if (nav_back() || pad_start_pressed()) { mode = opt_return ? 1 : 0; opt_return = 0; }
            draw_options(tshow);
        } else {                                     /* play */
            if (IsKeyPressed(KEY_P) || IsKeyPressed(KEY_ESCAPE) || pad_start_pressed()) {
                paused = !paused; pause_sel = 0;                 /* START / P / ESC = pause menu */
            }
            if (paused) {                                        /* pad-driven pause menu */
                int dy = nav_dy();
                if (dy) pause_sel = (pause_sel + dy + PAUSE_ITEMS) % PAUSE_ITEMS;
                if (nav_ok()) {
                    if (pause_sel == 0) paused = 0;
                    else if (pause_sel == 1) { opt_sel = 0; opt_return = 1; mode = 2; }
                    else end_game();
                }
                if (pad_select_pressed()) end_game();
                if (nav_back()) paused = 0;
            }
            if (g.game_over8524 && nav_ok()) end_game();
            if (!paused) {
                if (!g.players[1].joined39 && (joy_p2() & JOY_FIRE)) eng_join_player2();
                if ((vbl & 1) == 0) {
                    uint8_t joy[2] = { joy_p1(), joy_p2() };
                    if (smoke) joy[0] |= (vbl % 100) < 40 ? JOY_FIRE : 0;
                    hook_n = 0;
                    eng_frame(joy);
                }
                memcpy(show, cbuf[(vbl & 1) < hook_n ? (int)(vbl & 1) : 0], sizeof show);
                continue_prompt = 0;
                overlay_initials(show);
            }
        }

        int title_pic = (mode == 0 || mode == 2 || mode == 7);
        if (title_pic) UpdateTexture(ttex, tshow);
        else UpdateTexture(tex, show);
        BeginDrawing();
        ClearBackground(BLACK);
        int cw = title_pic ? BS_TITLE_W : BS_VIEW_W, ch = title_pic ? BS_TITLE_H : BS_VIEW_H;
        int sw = GetScreenWidth(), sh = GetScreenHeight();
        int top = 0;                                 /* never crop the canvas: the logo uses the side margin */
        int in_game = (mode == 1 && !smoke);         /* playing: no bottom bar, stats live in the side margins */
        int view_h = sh - (in_game ? 0 : BAR_H);
        float sx_ = (float)sw / cw, sy_ = (float)view_h / ch;
        float fs_ = sx_ < sy_ ? sx_ : sy_;
        if (fs_ < 1) fs_ = 1;
        int dw = (int)(cw * fs_), dh = (int)(ch * fs_);                 /* aspect-correct fit */
        DrawTexturePro(title_pic ? ttex : tex, (Rectangle){ 0, 0, (float)cw, (float)ch },
                       (Rectangle){ (float)((sw - dw) / 2), (float)(top + (view_h - dh) / 2), (float)dw, (float)dh },
                       (Vector2){ 0, 0 }, 0, WHITE);
        /* ---- grey bar: Retro Recompilation logo + scrolling message ---- */
        if (!in_game) {
            int by = sh - BAR_H;
            DrawRectangle(0, by, sw, BAR_H, (Color){ 28, 28, 34, 255 });
            int lw = 8;
            {
            static float mx = 0;
            const char *msg = "In 2026 Retro Recomps brings you BATTLE SQUADRON, fully native.  Enjoy this all-in-one package.  "
                              "See you in the next one.        P1 arrows + Space fire + X bomb  --  P2 WASD + Alt fire + V bomb  --  "
                              "pads: A fire, B smart bomb, START pause        ";
            int fs = 22;
            int tw = ui_font_ok ? (int)MeasureTextEx(ui_font, msg, (float)fs, 1).x : ui_measure(msg, fs);
            mx -= 1.5f; if (mx < -tw) mx += tw;
            BeginScissorMode(0, by, sw, BAR_H);
            for (int k = 0; k < 2; k++) {
                float tx = lw + mx + k * tw;
                if (ui_font_ok) DrawTextEx(ui_font, msg, (Vector2){ tx, (float)(by + BAR_H - 32) }, (float)fs, 1, (Color){ 255, 238, 136, 255 });
                else ui_text(msg, (int)tx, by + BAR_H - 32, fs, (Color){ 255, 238, 136, 255 });
            }
            EndScissorMode();
            if (rr_logo.id) {                        /* centred in the bar, the message scrolls behind it */
                float ls = (BAR_H - 16) / (float)rr_logo.height;
                DrawTextureEx(rr_logo, (Vector2){ (sw - rr_logo.width * ls) / 2, (float)(by + 8) }, 0, ls, WHITE);
            }
            }
        }
        if (in_game) draw_side_stats(sw, (sw - dw) / 2, (sw - dw) / 2 + dw);
        if (in_game && continue_prompt && (vbl & 16)) {   /* initials entered: wait for fire */
            const char *cp = "PRESS FIRE TO CONTINUE";
            int cfs = 30;
            ui_text(cp, (sw - ui_measure(cp, cfs)) / 2, sh - 70, cfs, (Color){ 255, 238, 136, 255 });
        }
        if (!smoke && (mode == 0 || mode == 2 || mode == 7)) {
            menu_dx = (sw - dw) / 2; menu_dy = top + (view_h - dh) / 2; menu_dw = dw; menu_dh = dh;
            int fs = menu_fs();
            if (mode == 0) {                          /* title menu */
                for (int i = 0; i < title_count(); i++) {
                    char body[40]; title_line(i, body, sizeof body);
                    menu_row(96 + i * 11, body, i == title_sel, fs, (Color){ 170, 185, 215, 255 });
                }
            } else if (mode == 2) {                   /* options */
                static const char *DIFF[3] = { "EASY", "NORMAL", "HARD" };
                static const char *LBL[8] = { "VOLUME", "MUSIC", "SOUND FX", "DIFFICULTY", "WEAPON",
                                              "LIVES", "FULLSCREEN", "BACK" };
                menu_row(92, "OPTIONS", 0, fs, (Color){ 255, 214, 92, 255 });
                for (int i = 0; i < 8; i++) {
                    char val[24] = "";
                    switch (i) {
                    case 0: snprintf(val, sizeof val, "%d", opt.master); break;
                    case 1: snprintf(val, sizeof val, "%s", opt.music_on ? "ON" : "OFF"); break;
                    case 2: snprintf(val, sizeof val, "%s", opt.sfx_on ? "ON" : "OFF"); break;
                    case 3: snprintf(val, sizeof val, "%s", DIFF[opt.difficulty % 3]); break;
                    case 4: snprintf(val, sizeof val, "%s", W_NAMES[opt.weapon & 3]); break;
                    case 5: snprintf(val, sizeof val, "%d", opt.lives); break;
                    case 6: snprintf(val, sizeof val, "%s", opt.fullscreen ? "ON" : "OFF"); break;
                    default: break;
                    }
                    /* two columns: 0..3 left, 4..7 right */
                    int col = i / 4, y = 112 + (i % 4) * 16;
                    int lx = col ? 170 : 26, vx = col ? 298 : 154;
                    int selr = (opt_sel == i);
                    if (selr) {
                        int sy = menu_dy + (y - 3) * menu_dh / BS_TITLE_H;
                        int sx = menu_dx + (lx - 8) * menu_dw / BS_TITLE_W;
                        int sw2 = (vx - lx + 20) * menu_dw / BS_TITLE_W, sh2 = 14 * menu_dh / BS_TITLE_H;
                        DrawRectangle(sx, sy, sw2, sh2, (Color){ 30, 54, 92, 210 });
                        DrawRectangleLines(sx, sy, sw2, sh2, (Color){ 120, 175, 240, 255 });
                    }
                    menu_at(lx, y, LBL[i], fs - 8, selr ? RAYWHITE : (Color){ 185, 190, 205, 255 }, 0);
                    if (i != 7) menu_at(vx, y, val, fs - 8, selr ? RAYWHITE : (Color){ 225, 228, 236, 255 }, 1);
                }
                menu_row(194, "LEFT-RIGHT CHANGE   FIRE SELECT   B BACK", 0, fs - 14, (Color){ 150, 155, 170, 255 });
            } else {                                  /* high scores: the game's own table, plus our stats */
                static const char *D_NAME[3] = { "EASY", "NORM", "HARD" };
                menu_row(92, "HIGH SCORES", 0, fs, (Color){ 255, 214, 92, 255 });
                { const int CX_N = 62, CX_S = 176, CX_SH = 228, CX_A = 262, CX_D = 300;
                  const Color hdr = { 150, 165, 190, 255 }, val = { 225, 228, 236, 255 };
                  int hfs = fs - 12, rfs = fs - 10;
                  menu_at(CX_N, 105, "NAME", hfs, hdr, 0);
                  menu_at(CX_S, 105, "SCORE", hfs, hdr, 1);
                  menu_at(CX_SH, 105, "SHOTS", hfs, hdr, 1);
                  menu_at(CX_A, 105, "ACC", hfs, hdr, 1);
                  menu_at(CX_D, 105, "DIFF", hfs, hdr, 1);
                  int row = 0;
                  for (int i = 0; i < 12 && row < 10; i++) {
                      uint32_t a = 0xFD1A + (uint32_t)i * 20;
                      char nm[8] = { 0 }, sc[12] = { 0 };
                      for (int k = 0; k < 4; k++) { char ch = (char)data.chip[a - 4 + k]; nm[k] = (ch >= 32 && ch < 127) ? ch : ' '; }
                      for (int k = 0; k < 8; k++) { char ch = (char)data.chip[a + k]; sc[k] = (ch >= '0' && ch <= '9') ? ch : '0'; }
                      for (int k = 3; k >= 0 && nm[k] == ' '; k--) nm[k] = 0;
                      long score = atol(sc);
                      if (!score && !nm[0]) continue;
                      int shots = 0, hits = 0, diff = -1;
                      for (int r = 0; r < 8; r++)
                          if (hs_rows[r].score == score && !strncmp(hs_rows[r].name, nm, 3)) {
                              shots = hs_rows[r].shots; hits = hs_rows[r].hits; diff = hs_rows[r].diff; break;
                          }
                      int y = 116 + row * 8;
                      char b[24];
                      menu_at(CX_N, y, nm[0] ? nm : "---", rfs, val, 0);
                      menu_at(CX_S, y, sc, rfs, val, 1);
                      if (shots) {
                          snprintf(b, sizeof b, "%d", shots); menu_at(CX_SH, y, b, rfs, val, 1);
                          snprintf(b, sizeof b, "%d%%", hits * 100 / shots); menu_at(CX_A, y, b, rfs, val, 1);
                          menu_at(CX_D, y, D_NAME[diff < 0 ? 1 : diff % 3], rfs, val, 1);
                      } else {
                          menu_at(CX_SH, y, "-", rfs, hdr, 1);
                          menu_at(CX_A, y, "-", rfs, hdr, 1);
                          menu_at(CX_D, y, "-", rfs, hdr, 1);
                      }
                      row++;
                  }
                }
                menu_row(194, "FIRE OR B TO GO BACK", 0, fs - 14, (Color){ 150, 155, 170, 255 });
            }
        }
        if (mode == 1 && paused && !smoke) draw_pause_ui(sw, sh);
        EndDrawing();
        vbl++;
    }
    options_save();
    audio_close();
    CloseWindow();
    return 0;
}
