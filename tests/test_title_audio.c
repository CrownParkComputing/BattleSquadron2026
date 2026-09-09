#define AUDIOTEST_MAIN
#include "../src/audio.c"
#include "bsdata.h"
#include <assert.h>

int main(void) {
    BsData data;
    assert(bs_open(&data, "amiga/original/whdload/BattleSquadron/data") == 0);
    bs_chip = data.chip;
    assert(bs_load_module(&data, "LODMUS") == 0);
    assert(bs_load_module(&data, "LODSPE") == 0);
    audio_start_title();
    audio_title_speech();
    assert(pch[0].play == 0x246F0 && pch[0].bytelen == 0x17CD * 2);
    int16_t pcm[882];
    uint32_t last = 0;
    for (int i = 0; i < 145; i++) {
        audio_render_timed(pcm, 441);
        assert(pch[0].play == 0x246F0 && pch[0].pos > last);
        last = pch[0].pos;
        audio_title_speech(); /* repeated UI calls cannot restart the sample */
        assert(pch[0].pos == last);
    }
    for (int i = 0; i < 100; i++) audio_render_timed(pcm, 441);
    assert(rd8(mchans[0] + 59) == 0);
    assert(pch[0].play != 0x246F0);
    audio_stop();
    assert(bs_load_module(&data, "LODMUS") == 0);
    assert(bs_load_module(&data, "LODSPE") == 0);
    audio_start_title();
    audio_title_speech();
    assert(rd8(mchans[0] + 59) == 0);
    assert(pch[0].play != 0x246F0);
    puts("PASS: complete welcome sample once, music resumes, title re-entry does not repeat speech");
}
