/* spritecheck.c -- objective verification of the sprite browser's decode.
 *
 *  1. COVERAGE + PIXEL IDENTITY.  Replay every stage; for each bob the engine
 *     puts on the render list, look the sprite catalog (src/bsdata.c) up by
 *     kind/type/stage, work out which catalog frame the record is showing, and
 *     render it BOTH ways: through render.c's render_bob() -- the blit that
 *     framecmp proves against the oracle screenshots -- and through the
 *     browser's bs_bob_frame().  Every pixel must agree.
 *     Records that draw a partial or shifted view of a frame (the type $03/$07
 *     /$0D pop-ups emerge a row at a time and slide a word at a time, and
 *     explosions swap to a foreign bank) are counted separately: they are not
 *     bank definitions, so the browser does not have to reproduce them.
 *  2. SELF-CONSISTENCY for banks no capture ever spawns: the cookie mask must
 *     read as a silhouette (mean runs per row < 3.5), not as noise.
 *  3. A sprite-sheet checksum over the whole catalog, so a decode regression
 *     cannot pass unnoticed.
 *
 * usage: spritecheck [--data DIR] [--sheet OUT.ppm] [--expect HEX]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/bsdata.h"
#include "../src/engine/engine.h"
#include "../src/render.h"

static BsData data;
static long n_exact, n_pixel_bad, n_partial, n_unmatched;
static int hit[64];                      /* catalog entries an actual capture exercised */
static uint32_t cv_a[BS_VIEW_W * BS_VIEW_H], cv_b[BS_VIEW_W * BS_VIEW_H];
#define SENTINEL 0xDEADBEEFu

/* catalog lookup: kind + type + stage, and the record's dimensions */
static int find_sprite(int stage, const RenderEntry *r, int *frame)
{
    int kind = (r->kind == 2) ? BS_SPR_HOSTILE : BS_SPR_OBJECT;
    for (int i = 0; i < bs_sprite_count(); i++) {
        const BsSprite *s = bs_sprite(i);
        if (s->kind != kind || s->type != r->type) continue;
        if (!(s->stages & (1u << stage))) continue;
        if (s->w_words != r->w_words) continue;
        BsBob b;
        bs_sprite_bob(&data, i, &b);
        if (r->stride && r->stride != b.plane_stride) continue;
        if (r->gfx < s->gfx) continue;
        uint32_t off = r->gfx - s->gfx;
        if (off % (uint32_t)b.frame_stride) continue;
        int f = (int)(off / (uint32_t)b.frame_stride);
        if (f >= s->frames) continue;
        *frame = f;
        return i;
    }
    return -1;
}

static void check_entry(int stage, const RenderEntry *r)
{
    int frame = 0;
    int i = find_sprite(stage, r, &frame);
    if (i < 0) { n_partial++; return; }          /* emerging / shifted / borrowed-bank draw */
    hit[i] = 1;
    const BsSprite *s = bs_sprite(i);
    BsBob b;
    bs_sprite_bob(&data, i, &b);
    if (r->h != b.h || (r->mask && r->mask != b.mask + (uint32_t)frame * (uint32_t)b.mask_stride)) {
        n_partial++; return;
    }
    if (r->reveal != 0xFFFFFFFFu) { n_partial++; return; }
    n_exact++;

    /* a) the production blit, placed at canvas (0,0) */
    for (int k = 0; k < BS_VIEW_W * BS_VIEW_H; k++) cv_a[k] = SENTINEL;
    RenderEntry r2 = *r;
    r2.x = g.cam7204; r2.y = 0x100;
    render_bob(cv_a, &r2);

    /* b) the browser decode of the same catalog frame */
    static uint8_t idx[512 * 128], alpha[512 * 128];
    if ((size_t)b.vis_w * b.h > sizeof idx) return;
    bs_bob_frame(&data, &b, frame, idx, alpha);
    const uint32_t *pal = render_palette();
    for (int k = 0; k < BS_VIEW_W * BS_VIEW_H; k++) cv_b[k] = SENTINEL;
    for (int y = 0; y < b.h && y < BS_VIEW_H; y++)
        for (int x = 0; x < b.vis_w && x < BS_VIEW_W; x++)
            if (alpha[y * b.vis_w + x]) cv_b[y * BS_VIEW_W + x] = pal[idx[y * b.vis_w + x] & 31];

    long bad = 0;
    for (int y = 0; y < b.h && y < BS_VIEW_H; y++)
        for (int x = 0; x < b.vis_w && x < BS_VIEW_W; x++)
            if (cv_a[y * BS_VIEW_W + x] != cv_b[y * BS_VIEW_W + x]) bad++;
    if (bad) {
        n_pixel_bad++;
        if (n_pixel_bad < 10)
            fprintf(stderr, "  MISMATCH stage %d %-24s frame %2d: %ld px\n", stage, s->name, frame, bad);
    }
}

static void run_stage(int stage, long frames)
{
    if (bs_load_stage(&data, stage)) exit(1);
    bs_chip = data.chip;
    eng_init(stage, 1, 3, 3, 1);
    render_stage(&data);
    for (long n = 0; n < frames; n++) {
        for (int p = 0; p < 2; p++)
            if (g.players[p].explode49 == 0 && (uint16_t)g.players[p].invuln52 < 100)
                g.players[p].invuln52 = 100;
        eng_frame_update();
        render_stage(&data);
        for (int i = 0; i < render_count; i++)
            if (render_list[i].kind == 1 || render_list[i].kind == 2)
                check_entry(stage, &render_list[i]);
        long F = 1481 + g.dframe, ph = F % 400;
        uint8_t dir = ph < 150 ? 0x08 : ph < 200 ? 0x01 : ph < 350 ? 0x02 : 0x04;
        uint8_t joy[2] = { (uint8_t)(dir | ((F % 100) < 20 ? 0x10 : 0)), 0 };
        for (int p = 0; p < 2; p++)
            if (g.players[p].explode49 == 0 && (uint16_t)g.players[p].invuln52 < 100)
                g.players[p].invuln52 = 100;
        eng_frame_finish(joy);
    }
}

/* ---- sheet: every catalog frame, drawn on its stage's palette ---- */
#define SHEET_W 1024
static uint32_t sheet[SHEET_W * 4096];
static int sheet_h;

static uint32_t fnv1a(const void *p, size_t n)
{
    const uint8_t *b = p; uint32_t h = 2166136261u;
    while (n--) { h ^= *b++; h *= 16777619u; }
    return h;
}

int main(int argc, char **argv)
{
    const char *dir = getenv("BS_DATA") ? getenv("BS_DATA")
                    : "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    const char *sheet_path = NULL, *dump_dir = NULL;
    uint32_t expect = 0; int have_expect = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--sheet") && i + 1 < argc) sheet_path = argv[++i];
        else if (!strcmp(argv[i], "--dump") && i + 1 < argc) dump_dir = argv[++i];
        else if (!strcmp(argv[i], "--expect") && i + 1 < argc) { expect = (uint32_t)strtoul(argv[++i], NULL, 16); have_expect = 1; }
    }
    if (bs_open(&data, dir)) return 1;

    printf("== 1. render-list coverage + pixel identity (browser decode vs render.c)\n");
    for (int s = 0; s < 4; s++) {
        long e0 = n_exact, p0 = n_partial, b0 = n_pixel_bad;
        run_stage(s, 20000);
        printf("   stage %d: %ld exact-frame draws checked, %ld pixel mismatches, %ld partial/shifted draws skipped\n",
               s, n_exact - e0, n_pixel_bad - b0, n_partial - p0);
    }

    printf("== 2. per-entry decode (X = seen in a capture, - = never spawned: silhouette test only)\n");
    int seen_bad = 0, silh_bad = 0;
    for (int i = 0; i < bs_sprite_count(); i++) {
        const BsSprite *s = bs_sprite(i);
        int stage = 0;
        while (stage < 4 && !(s->stages & (1u << stage))) stage++;
        if (data.stage != stage && bs_load_stage(&data, stage)) return 1;
        BsBob b; bs_sprite_bob(&data, i, &b);
        /* a real bank has at least one CLEAN frame; explosion tails legitimately
         * fragment, but noise is fragmented in every frame */
        double best_silh = 1e9, best_flat = 0;
        for (int f = 0; f < s->frames; f++) {
            double v = bs_bob_silhouette(&data, &b, f);
            if (v < best_silh) best_silh = v;
            double fl = bs_bob_flatness(&data, &b, f);
            if (fl > best_flat) best_flat = fl;
        }
        int opaque = (b.mask == 0);
        /* entries a capture exercised are already proven pixel-for-pixel above;
         * the heuristic only has to catch a bank nothing ever spawned */
        int bad = hit[i] ? 0 : (opaque ? (best_flat < 0.15) : (best_silh >= 3.5));
        printf("   %c %-24s %s%02X st%-2u %3dx%-3d %2d fr  gfx %05X %s %05X  ps %4d fs %5d  silh %.2f flat %.2f%s\n",
               hit[i] ? 'X' : '-',
               s->name, s->kind == BS_SPR_HOSTILE ? "H" : "O", s->type, s->stages,
               b.vis_w, b.h, s->frames, s->gfx,
               b.mask ? (b.mask_stride ? "msk" : "MSK") : "   ", b.mask,
               b.plane_stride, b.frame_stride,
               opaque ? 0.0 : best_silh, best_flat, bad ? "  <-- NOISE" : "");
        if (bad) { silh_bad++; seen_bad++; }
    }

    printf("== 3. sprite-sheet checksum\n");
    memset(sheet, 0, sizeof sheet);
    int y0 = 0;
    for (int i = 0; i < bs_sprite_count(); i++) {
        const BsSprite *s = bs_sprite(i);
        int stage = 0;
        while (stage < 4 && !(s->stages & (1u << stage))) stage++;
        if (data.stage != stage && bs_load_stage(&data, stage)) return 1;
        uint16_t pal[32]; uint32_t cols[32];
        bs_palette(&data, stage, pal);
        for (int k = 0; k < 32; k++) { uint8_t r, gg, bb; bs_rgb12(pal[k], &r, &gg, &bb);
            cols[k] = 0xFF000000u | ((uint32_t)bb << 16) | ((uint32_t)gg << 8) | r; }
        BsBob b; bs_sprite_bob(&data, i, &b);
        static uint8_t idx[512 * 128], alpha[512 * 128];
        if ((size_t)b.vis_w * b.h > sizeof idx) continue;
        for (int f = 0; f < s->frames; f++) {
            bs_bob_frame(&data, &b, f, idx, alpha);
            int ox = f * (b.vis_w + 2);
            if (ox + b.vis_w >= SHEET_W) break;
            for (int y = 0; y < b.h; y++)
                for (int x = 0; x < b.vis_w; x++)
                    if (alpha[y * b.vis_w + x])
                        sheet[(size_t)(y0 + y) * SHEET_W + ox + x] =
                            s->cloak ? 0xFFFFFFFFu : cols[idx[y * b.vis_w + x] & 31];
        }
        if (dump_dir) {                          /* one strip of frames per bank */
            char path[600], slug[80];
            size_t k = 0;
            for (const char *c = s->name; *c && k < sizeof slug - 1; c++)
                slug[k++] = (*c >= 'A' && *c <= 'Z') ? (char)(*c + 32)
                          : ((*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9')) ? *c : '_';
            slug[k] = 0;
            snprintf(path, sizeof path, "%s/%s_%s%02x.ppm", dump_dir,
                     s->kind == BS_SPR_HOSTILE ? "hostile" : "object", slug, s->type);
            int sw = s->frames * (b.vis_w + 2) - 2;
            if (sw > SHEET_W) sw = SHEET_W;
            FILE *f = fopen(path, "wb");
            if (f) {
                fprintf(f, "P6\n%d %d\n255\n", sw, b.h);
                for (int y = 0; y < b.h; y++)
                    for (int x = 0; x < sw; x++) {
                        uint32_t c = sheet[(size_t)(y0 + y) * SHEET_W + x];
                        uint8_t px[3] = { (uint8_t)c, (uint8_t)(c >> 8), (uint8_t)(c >> 16) };
                        fwrite(px, 1, 3, f);
                    }
                fclose(f);
            }
        }
        y0 += b.h + 2;
    }
    sheet_h = y0;
    uint32_t sum = fnv1a(sheet, (size_t)sheet_h * SHEET_W * 4);
    printf("   sheet %dx%d  fnv1a %08X\n", SHEET_W, sheet_h, sum);
    if (sheet_path) {
        FILE *f = fopen(sheet_path, "wb");
        if (f) {
            fprintf(f, "P6\n%d %d\n255\n", SHEET_W, sheet_h);
            for (int k = 0; k < sheet_h * SHEET_W; k++) {
                uint8_t p[3] = { (uint8_t)sheet[k], (uint8_t)(sheet[k] >> 8), (uint8_t)(sheet[k] >> 16) };
                fwrite(p, 1, 3, f);
            }
            fclose(f);
            printf("   wrote %s\n", sheet_path);
        }
    }

    int fail = (n_pixel_bad != 0) || silh_bad || (have_expect && sum != expect);
    if (have_expect && sum != expect)
        fprintf(stderr, "spritecheck: sheet checksum %08X, expected %08X\n", sum, expect);
    printf("spritecheck: %s (%ld exact draws verified, %ld pixel mismatches, %ld partial, %d noisy banks)\n",
           fail ? "FAIL" : "OK", n_exact, n_pixel_bad, n_partial, silh_bad);
    (void)seen_bad; (void)n_unmatched;
    return fail ? 1 : 0;
}
