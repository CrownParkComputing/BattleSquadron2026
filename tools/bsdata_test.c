/* bsdata_test.c -- identity tests for src/bsdata.c (re/ASSETS.md §9).
 * 1. Depacks every module from the WHDLoad install and byte-compares it with
 *    the reference extractions in ~/BattleSquadron-Amiga/original/modules/.
 * 2. Dumps decoded assets (map indices, tiles, hostile/object frames, font,
 *    hw sprites, palette) as P5/P6 files into OUTDIR for tools/test_bsdata.sh
 *    to diff against the proven python decoders (sprite_dump.py formulas).
 * usage: bsdata_test DATA_DIR REF_MODULES_DIR OUTDIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/bsdata.h"

static BsData d;
static int failures;

static void check(int ok, const char *what)
{
    if (!ok) { printf("FAIL %s\n", what); failures++; }
    else printf("ok   %s\n", what);
}

static void pgm(const char *dir, const char *name, const uint8_t *px, int w, int h)
{
    char path[700];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    fprintf(f, "P5\n%d %d\n255\n", w, h);
    fwrite(px, 1, (size_t)w * h, f);
    fclose(f);
}

static void module_compare(const char *ref_dir)
{
    for (size_t i = 0; i < d.nmods; i++) {
        const BsModule *m = &d.mods[i];
        uint8_t *mem = calloc(1, BS_CHIP_SIZE);
        size_t size = 0;
        char data_path[700];
        snprintf(data_path, sizeof data_path, "%s/%s", d.dir, m->name);
        FILE *df = fopen(data_path, "rb");
        if (!df) { printf("skip %s (not in the WHDLoad install)\n", m->name); free(mem); continue; }
        fclose(df);
        int err = bs_module_load(d.dir, m, mem, BS_CHIP_SIZE, &size);
        char what[128];
        snprintf(what, sizeof what, "depack %s (%zu bytes)", m->name, size);
        if (err) { check(0, what); free(mem); continue; }
        char path[700];
        snprintf(path, sizeof path, "%s/%s.bin", ref_dir, m->name);
        FILE *f = fopen(path, "rb");
        if (!f) { printf("skip %s (no reference)\n", m->name); free(mem); continue; }
        fseek(f, 0, SEEK_END);
        long ref_size = ftell(f);
        fseek(f, 0, SEEK_SET);
        uint8_t *ref = malloc((size_t)ref_size);
        fread(ref, 1, (size_t)ref_size, f);
        fclose(f);
        int same = ((long)size == ref_size) && !memcmp(mem + m->load_address, ref, size);
        if (!same && !strcmp(m->name, "LODST1")) {
            /* known-bad reference extraction (re/ASSETS.md OPEN ISSUE): check the
             * map invariant on OUR depack instead: 512 rows x 24 words, each word
             * must address the tile strip (< $14000 bytes / 2). */
            int good = 1;
            for (uint32_t a = 0x44000; a < 0x4A000; a += 2) {
                uint16_t w = (uint16_t)((mem[a] << 8) | mem[a + 1]);
                if (w >= 0xA000) { good = 0; break; }
            }
            printf("note LODST1 reference differs (known-bad extraction); native map invariant %s\n",
                   good ? "HOLDS" : "fails");
            check(good, "LODST1 native depack map invariant");
            free(ref); free(mem);
            continue;
        }
        snprintf(what, sizeof what, "%s == reference (%ld bytes)", m->name, ref_size);
        check(same, what);
        free(ref);
        free(mem);
    }
}

int main(int argc, char **argv)
{
    const char *data_dir = argc > 1 ? argv[1] :
        "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    const char *ref_dir = argc > 2 ? argv[2] : "/home/jon/BattleSquadron-Amiga/original/modules";
    const char *out_dir = argc > 3 ? argv[3] : "build/assets_check";
    if (bs_open(&d, data_dir)) return 1;
    module_compare(ref_dir);
    check(bs_load_stage(&d, 0) == 0, "load stage 0");

    /* map indices 384 x 8192 */
    static uint8_t map[384 * 8192];
    bs_map_render(&d, map, 384);
    pgm(out_dir, "map0_idx.pgm", map, 384, 8192);

    /* tile strip: first 256 tiles as indices, 16 per row */
    static uint8_t tiles[256 * 16 * 16];
    for (int t = 0; t < 256; t++) {
        uint8_t buf[256];
        bs_tile(&d, (uint16_t)(t * 80), buf);
        for (int y = 0; y < 16; y++)
            memcpy(tiles + ((t / 16) * 16 + y) * 256 + (t % 16) * 16, buf + y * 16, 16);
    }
    pgm(out_dir, "tiles_idx.pgm", tiles, 256, 256);

    /* hostile frames: type 0 frame 0, type 6 frame 0, explosion frame 0.
     * masked-off pixels forced to 255 so the mask is part of the identity. */
    for (int t = 0; t < 14; t++) {
        BsBob b;
        if (bs_hostile_gfx(&d, t, &b)) continue;
        if (t == 2 || t == 9 || t == 0x0C || t == 0x0D) continue;   /* stage 1-3 gfx, not resident */
        static uint8_t idx[128 * 64], alpha[128 * 64];
        bs_bob_frame(&d, &b, 0, idx, alpha);
        for (int i = 0; i < b.vis_w * b.h; i++) if (!alpha[i]) idx[i] = 255;
        char name[64];
        snprintf(name, sizeof name, "hostile%02x_f0.pgm", t);
        pgm(out_dir, name, idx, b.vis_w, b.h);
    }

    /* object template frames (stage-0 templates): 12 (turret), 16 (scenery) */
    static const int tmpls[] = { 1, 12, 16, 17, 21, 22 };
    for (unsigned i = 0; i < sizeof tmpls / sizeof *tmpls; i++) {
        BsBob b;
        if (bs_object_gfx(&d, tmpls[i], &b)) continue;
        static uint8_t idx[128 * 64];
        bs_bob_frame(&d, &b, 0, idx, NULL);
        char name[64];
        snprintf(name, sizeof name, "objtmpl%02d_f0.pgm", tmpls[i]);
        pgm(out_dir, name, idx, b.vis_w, b.h);
    }

    /* font: glyphs '0'..'Z' in one strip, 8x9 each */
    static uint8_t font[9 * 43 * 8];
    for (int c = 0; c < 43; c++) {
        const uint8_t *gl = bs_glyph(&d, '0' + c);
        for (int y = 0; y < 9; y++)
            for (int x = 0; x < 8; x++)
                font[y * 43 * 8 + c * 8 + x] = (uint8_t)(((gl[y] >> (7 - x)) & 1) * 255);
    }
    pgm(out_dir, "font.pgm", font, 43 * 8, 9);

    /* hw sprite 0 (ship left half, 30 rows) */
    static uint8_t hw[16 * 30];
    bs_hwsprite(&d, 0x10000, 30, hw);
    pgm(out_dir, "hwsprite0.pgm", hw, 16, 30);

    /* palette as text */
    {
        uint16_t pal[32];
        bs_palette(&d, 0, pal);
        char path[700];
        snprintf(path, sizeof path, "%s/palette0.txt", out_dir);
        FILE *f = fopen(path, "w");
        for (int i = 0; i < 32; i++) fprintf(f, "%03x\n", pal[i]);
        fclose(f);
    }

    /* sfx table sanity: every trigger number maps to a non-empty sample */
    static const int trig[] = { 59, 29, 57, 56, 25, 30, 31, 53, 24, 26, 28, 20, 48, 54, 49, 55 };
    int sfx_ok = 1;
    for (unsigned i = 0; i < sizeof trig / sizeof *trig; i++) {
        BsSfx s; int ch;
        if (bs_sfx_desc(&d, trig[i], &s, &ch)) { printf("  sfx %d empty\n", trig[i]); sfx_ok = 0; }
    }
    check(sfx_ok, "sfx triggers map to non-empty samples");

    /* stages 1-3: the depacked+fixed-up chip must byte-match the oracle's live
     * RAM (LODST1 needs LoadModule's $2EF20..$5E000 byte-reversal + the
     * LAB_71CA object-pointer patch; a raw depack renders as noise) */
    for (int st = 1; st <= 3; st++) {
        char path[700];
        snprintf(path, sizeof path, "re/trace/chip_stage%d_16000.bin", st);
        FILE *f = fopen(path, "rb");
        if (!f) { printf("skip stage %d oracle (no %s)\n", st, path); continue; }
        static uint8_t oracle[BS_CHIP_SIZE];
        size_t got = fread(oracle, 1, BS_CHIP_SIZE, f);
        fclose(f);
        char what[128];
        snprintf(what, sizeof what, "load stage %d", st);
        int ok = got == BS_CHIP_SIZE && bs_load_stage(&d, st) == 0;
        check(ok, what);
        if (!ok) continue;
        uint32_t lo = (st == 1) ? 0x2E89A : (st == 2) ? 0x2E4C0 : 0x2E840;   /* whole module */
        snprintf(what, sizeof what, "stage %d chip [$%X..$5E000) == oracle RAM", st, lo);
        check(!memcmp(d.chip + lo, oracle + lo, 0x5E000 - lo), what);
    }

    printf(failures ? "bsdata_test: %d FAILURES\n" : "bsdata_test: all ok\n", failures);
    return failures != 0;
}
