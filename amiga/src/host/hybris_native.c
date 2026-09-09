/* Headless Hybris runner: the same chipset as the Battle Squadron oracle, but
 * booted through the WHDLoad slave because Hybris ships no self-contained
 * LOADER.  Reports what the slave asks resload for, which is how the title's
 * memory map gets discovered in the first place. */
#include "amiga.h"
#include "whdload.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "m68k.h"

int main(int argc, char **argv)
{
    const char *install = "/home/jon/Downloads/whdload-pipeline/HybrisNTSC";
    /* The extracted CODE HUNK, not the shipped .Slave: the latter is an
     * AmigaDOS hunk file starting $000003F3, while the slave proper starts
     * with the $70FF magic that WHDLoad checks. */
    const char *slave_bin = "original/hybris/slave.bin";
    long frames = 500;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--install") && i + 1 < argc) install = argv[++i];
        else if (!strcmp(argv[i], "--slave") && i + 1 < argc) slave_bin = argv[++i];
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atol(argv[++i]);
        else {
            fprintf(stderr, "usage: %s [--install DIR] [--frames N]\n", argv[0]);
            return 2;
        }
    }
    char data[700];
    snprintf(data, sizeof data, "%s/data", install);

    amiga_init_bare();
    if (!whdload_boot(slave_bin, data)) return 1;

    long last_calls = -1;
    for (long frame = 0; frame < frames && !amiga_stopped(); frame++) {
        amiga_run_frame();
        if (frame < 8 || frame % 50 == 0 || whd_call_count != last_calls) {
            fprintf(stderr, "frame %3ld pc=$%06x sp=$%06x calls=%ld\n",
                    frame, m68k_get_reg(NULL, M68K_REG_PC),
                    m68k_get_reg(NULL, M68K_REG_A7), whd_call_count);
            last_calls = whd_call_count;
        }
    }

    printf("hybris: ran %ld frames, %ld resload calls, %ld files loaded\n",
           bs_frame_no, whd_call_count, whd_file_count);
    return 0;
}
