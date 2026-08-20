/* titlecmp -- pixel-diff the native LODINT title render against the oracle's
 * idle title frame (re/trace/shots/title_36000.ppm = host --dump-frame at
 * display frame 36000, 352x288; the 320x200 title DIW sits at offset (16,38)).
 *
 * The picture (logo, F-key icons, PRESS BUTTON TO START, INNERPRISE line) is
 * expected byte-exact; the only tolerated differences are the two menu bands
 * the title code draws at runtime (rows 120..139 icons + on/on labels, rows
 * 147..155 TWO PLAYERS), which the viewer reproduces with the chip font
 * instead of the original title font.  Pass: >= 97.5% overall AND 100% exact
 * outside those bands.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/bsdata.h"
#include "../src/engine/engine.h"
#include "../src/render.h"



static unsigned char *read_ppm(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    char magic[3] = { 0 };
    int maxv;
    if (!f) return NULL;
    if (fscanf(f, "%2s %d %d %d", magic, w, h, &maxv) != 4 || strcmp(magic, "P6") || maxv != 255) {
        fclose(f);
        return NULL;
    }
    fgetc(f);
    unsigned char *d = malloc((size_t)*w * (size_t)*h * 3);
    if (!d || fread(d, 3, (size_t)*w * (size_t)*h, f) != (size_t)*w * (size_t)*h) {
        free(d);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return d;
}

static int in_band(int y) { return (y >= 120 && y <= 139) || (y >= 147 && y <= 155); }

int main(int argc, char **argv)
{
    const char *dir = "/home/jon/BattleSquadron-Amiga/original/whdload/BattleSquadron/data";
    const char *ref = "re/trace/shots/title_36000.ppm";
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--data") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--ref") && i + 1 < argc) ref = argv[++i];
    }
    static BsData data;
    if (bs_open(&data, dir)) { fprintf(stderr, "titlecmp: cannot open %s\n", dir); return 2; }
    if (bs_load_module(&data, "LODINT")) { fprintf(stderr, "titlecmp: LODINT load failed\n"); return 2; }
    bs_chip = data.chip;

    static uint32_t rgba[BS_TITLE_W * BS_TITLE_H];
    render_title(rgba);

    int w, h;
    unsigned char *host = read_ppm(ref, &w, &h);
    if (!host) { fprintf(stderr, "titlecmp: cannot read %s\n", ref); return 2; }
    const int ox = 16, oy = 38;
    long total = 0, diff = 0, static_diff = 0;
    for (int y = 0; y < BS_TITLE_H; y++)
        for (int x = 0; x < BS_TITLE_W; x++) {
            const unsigned char *hp = host + ((size_t)(y + oy) * (size_t)w + (size_t)(x + ox)) * 3;
            uint32_t v = rgba[(size_t)y * BS_TITLE_W + x];
            int same = hp[0] == (v & 0xFF) && hp[1] == ((v >> 8) & 0xFF) && hp[2] == ((v >> 16) & 0xFF);
            total++;
            if (!same) {
                diff++;
                if (!in_band(y)) static_diff++;
            }
        }
    free(host);
    double pct = 100.0 * (double)(total - diff) / (double)total;
    printf("titlecmp: %.2f%% exact (%ld/%ld px; %ld outside the dynamic menu bands)\n",
           pct, total - diff, total, static_diff);
    if (pct < 97.5 || static_diff != 0) { printf("titlecmp: FAIL\n"); return 1; }
    printf("titlecmp: OK\n");
    return 0;
}
