#include "runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned char *read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    fseek(file, 0, SEEK_END);
    *length = (size_t)ftell(file);
    rewind(file);
    unsigned char *data = malloc(*length);
    if (!data || fread(data, 1, *length, file) != *length) {
        free(data); data = NULL;
    }
    fclose(file);
    return data;
}

static uint32_t fnv1a(const uint8_t *data, size_t length)
{
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < length; i++) {
        hash ^= data[i];
        hash *= UINT32_C(16777619);
    }
    return hash;
}

static BsAudioSampleEvent welcome_event;
static int welcome_event_count;

static void capture_audio_sample(void *user, const BsAudioSampleEvent *event)
{
    (void)user;
    welcome_event = *event;
    welcome_event_count++;
}

static int projectile_cookie_cut_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    const uint32_t base = 0x8000;
    const uint32_t projectile = 0x2dc80;
    const uint32_t playfield = 0x40000;
    fixture->cpu.pc = 0xb7a;
    fixture->cpu.a[5] = base;
    fixture->cpu.a[6] = 0xdff000;
    bs_recomp_write32(fixture, base + 7208, playfield);
    bs_recomp_write32(fixture, base - 1800, 0x3000);
    bs_recomp_write16(fixture, projectile, 0x0108);
    bs_recomp_write16(fixture, projectile + 4, 0x0101);
    bs_recomp_write8(fixture, projectile + 8, 1);
    bs_recomp_write8(fixture, projectile + 11, 1);
    bs_recomp_write8(fixture, projectile + 31, 3);
    bs_recomp_write16(fixture, projectile + 44, 1);
    bs_recomp_write16(fixture, projectile + 52, 1);
    bs_recomp_write16(fixture, projectile + 68, 8);
    bs_recomp_write8(fixture, 0x36280, 0xa0);
    bs_recomp_write8(fixture, 0x36000, 0x80);
    bs_recomp_write8(fixture, 0x36001, 0x20);
    bs_recomp_write8(fixture, 0x36002, 0x00);
    bs_recomp_write8(fixture, 0x36003, 0xa0);
    bs_recomp_write8(fixture, 0x36004, 0x00);
    for (unsigned plane = 0; plane < 5; plane++)
        bs_recomp_write8(fixture,
            playfield + plane * 0x6000 + 0x31, 0x55);
    int result = bs_recomp_run(fixture, 1);
    static const uint8_t expected[5] = {0xd5, 0x75, 0x55, 0xf5, 0x55};
    int passed = result == BS_RECOMP_OK && fixture->cpu.pc == 0xb7e;
    for (unsigned plane = 0; plane < 5; plane++)
        passed &= bs_recomp_read8(fixture,
            playfield + plane * 0x6000 + 0x31) == expected[plane];
    free(fixture);
    return passed;
}

/* Drives one pass of the $B54 active-object pool over a hand-built object. */
static void step_object_pool(BsRecomp *fixture)
{
    fixture->cpu.pc = 0xb54;
    bs_recomp_run(fixture, 1);
}

/* An object that takes fatal damage must run its death animation from the
 * live limit up to its final frame and then be completed, not left sitting on
 * the last frame forever. */
static int object_death_animation_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    const uint32_t base = 0x8000;
    const uint32_t object = 0x2e040;
    fixture->cpu.a[5] = base;
    fixture->cpu.a[6] = 0xdff000;
    bs_recomp_write16(fixture, base + 7228, 0);     /* mode zero */
    bs_recomp_write16(fixture, base + 7222, 0);     /* no scroll advance */
    bs_recomp_write8(fixture, base - 28551, 0);     /* animation gate open */

    bs_recomp_write16(fixture, object, 0x0080);
    bs_recomp_write16(fixture, object + 2, 0x0140);
    bs_recomp_write16(fixture, object + 6, 0x0010);
    bs_recomp_write8(fixture, object + 17, 0x20);   /* type */
    bs_recomp_write8(fixture, object + 19, 4);      /* live limit */
    bs_recomp_write8(fixture, object + 28, 3);      /* health */
    bs_recomp_write8(fixture, object + 33, 9);      /* final frame */

    /* Fatal damage parks the object on its live limit and flags it. */
    bs_recomp_write8(fixture, object + 24, 5);
    step_object_pool(fixture);
    int passed = bs_recomp_read8(fixture, object + 25) == 4 &&
                 (bs_recomp_read8(fixture, object + 31) & 0x04) != 0 &&
                 bs_recomp_read16(fixture, object) != 0;

    /* The animation then advances one frame per pass up to the final one. */
    for (int frame = 5; frame <= 9 && passed; frame++) {
        step_object_pool(fixture);
        passed = bs_recomp_read8(fixture, object + 25) == frame;
    }

    /* On the final frame a player sitting on the wreck collects it: the BCD
     * bonus advances, the wreck moves one state past its last frame. */
    if (passed) {
        bs_recomp_write8(fixture, 0x4e3c + 38, 0);
        bs_recomp_write16(fixture, 0x4e3c + 4, 0x0080);
        bs_recomp_write16(fixture, 0x4e3c + 6, 0x0140);
        bs_recomp_write8(fixture, 0x4e3c + 97, 0x09);
        bs_recomp_write8(fixture, 0x4f46 + 38, 0xff);
        step_object_pool(fixture);
        passed = bs_recomp_read8(fixture, 0x4e3c + 97) == 0x10 &&
                 bs_recomp_read8(fixture, object + 25) == 10;
    }

    /* An uncollected wreck must not advance past its final frame. */
    if (passed) {
        BsRecomp *idle = calloc(1, sizeof *idle);
        if (!idle) { free(fixture); return 0; }
        idle->cpu.a[5] = base;
        idle->cpu.a[6] = 0xdff000;
        bs_recomp_write8(idle, base - 28551, 0);
        bs_recomp_write16(idle, object, 0x0080);
        bs_recomp_write16(idle, object + 2, 0x0140);
        bs_recomp_write16(idle, object + 6, 0x0010);
        bs_recomp_write8(idle, object + 17, 0x20);
        bs_recomp_write8(idle, object + 19, 4);
        bs_recomp_write8(idle, object + 25, 9);
        bs_recomp_write8(idle, object + 33, 9);
        bs_recomp_write8(idle, 0x4e3c + 38, 0xff);
        bs_recomp_write8(idle, 0x4f46 + 38, 0xff);
        step_object_pool(idle);
        passed = bs_recomp_read8(idle, object + 25) == 9;
        free(idle);
    }

    free(fixture);
    return passed;
}

static int collision_pass_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    const uint32_t base = 0x8000;
    const uint32_t player = 0x4e3c;
    const uint32_t shot = player + 122;
    const uint32_t entity = 0x2e040;
    const uint32_t projectile = 0x2dc80;
    fixture->cpu.a[5] = base;
    fixture->cpu.a[6] = 0xdff000;
    fixture->cpu.pc = 0xc4c;
    bs_recomp_write32(fixture, base - 18624, player);

    bs_recomp_write16(fixture, player + 62, 4);
    bs_recomp_write16(fixture, player + 64, 4);
    /* $C4C is LAB_34FA, the pass against the enemy/scenery records.  A
     * penetrating shot (negative damage byte) survives its own hit. */
    bs_recomp_write16(fixture, shot, 0x0128);
    bs_recomp_write16(fixture, shot + 2, 0x0140);
    bs_recomp_write8(fixture, shot + 11, 0xff);
    bs_recomp_write16(fixture, entity, 0x0120);
    bs_recomp_write16(fixture, entity + 48, 0x0120);
    bs_recomp_write16(fixture, entity + 50, 0x0140);
    bs_recomp_write16(fixture, entity + 52, 0x0130);
    bs_recomp_write16(fixture, entity + 54, 0x0150);
    if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK ||
        fixture->cpu.pc != 0xc50 ||
        bs_recomp_read16(fixture, shot) != 0x0128 ||
        bs_recomp_read8(fixture, entity + 24) != 2) {
        free(fixture);
        return 0;
    }

    /* $C50 is LAB_3424, the pass against the hostile/collectable records.  An
     * ordinary shot is consumed by its hit. */
    bs_recomp_write16(fixture, shot, 0x0128);
    bs_recomp_write16(fixture, shot + 2, 0x0140);
    bs_recomp_write8(fixture, shot + 11, 2);
    bs_recomp_write16(fixture, projectile, 0x0120);
    bs_recomp_write16(fixture, projectile + 16, 0x0120);
    bs_recomp_write16(fixture, projectile + 18, 0x0140);
    bs_recomp_write16(fixture, projectile + 20, 0x0130);
    bs_recomp_write16(fixture, projectile + 22, 0x0150);
    if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK ||
        fixture->cpu.pc != 0xc54 ||
        bs_recomp_read16(fixture, shot) != 0 ||
        bs_recomp_read8(fixture, projectile + 62) != 2) {
        free(fixture);
        return 0;
    }

    bs_recomp_write16(fixture, player + 2, 0x0180);
    bs_recomp_write16(fixture, player + 4, 0x0120);
    bs_recomp_write16(fixture, player + 6, 0x0140);
    bs_recomp_write16(fixture, 0x4976, 0x0130);
    bs_recomp_write16(fixture, 0x4976 + 4, 0x0150);
    if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK ||
        fixture->cpu.pc != 0xc58 ||
        bs_recomp_read8(fixture, player + 38) != 0x64 ||
        bs_recomp_read8(fixture, player + 49) != 0x46 ||
        bs_recomp_read16(fixture, player + 52) != 0x270f ||
        bs_recomp_read16(fixture, player + 2) != 0x0174) {
        free(fixture);
        return 0;
    }

    memset(fixture->memory + projectile, 0, 0x3c0);
    bs_recomp_write16(fixture, player + 52, 0);
    bs_recomp_write16(fixture, player + 66, 2);
    bs_recomp_write16(fixture, projectile, 0x0120);
    bs_recomp_write16(fixture, projectile + 16, 0x0120);
    bs_recomp_write16(fixture, projectile + 18, 0x0140);
    bs_recomp_write16(fixture, projectile + 20, 0x0140);
    bs_recomp_write16(fixture, projectile + 22, 0x0160);
    bs_recomp_write8(fixture, projectile + 28, 0x0a);
    bs_recomp_write8(fixture, projectile + 31, 5);
    if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK ||
        fixture->cpu.pc != 0xc5c ||
        bs_recomp_read16(fixture, projectile) != 0 ||
        bs_recomp_read16(fixture, player + 66) != 3) {
        free(fixture);
        return 0;
    }

    fixture->cpu.pc = 0xc58;
    bs_recomp_write16(fixture, shot, 0x0128);
    bs_recomp_write16(fixture, player + 60, 4);
    bs_recomp_write16(fixture, projectile, 0x0120);
    bs_recomp_write16(fixture, projectile + 16, 0x0120);
    bs_recomp_write16(fixture, projectile + 18, 0x0140);
    bs_recomp_write16(fixture, projectile + 20, 0x0140);
    bs_recomp_write16(fixture, projectile + 22, 0x0160);
    bs_recomp_write8(fixture, projectile + 28, 4);
    bs_recomp_write8(fixture, projectile + 31, 5);
    int passed = bs_recomp_run(fixture, 1) == BS_RECOMP_OK &&
        bs_recomp_read16(fixture, projectile) == 0 &&
        bs_recomp_read16(fixture, shot) == 0 &&
        bs_recomp_read8(fixture, player + 59) == 2 &&
        bs_recomp_read16(fixture, player + 60) == 5;
    free(fixture);
    return passed;
}

static int live_input_test(void)
{
    BsRecomp *fixture = malloc(sizeof *fixture);
    if (!fixture) return 0;
    int result = bs_recomp_init(fixture,
        "original/whdload/BattleSquadron/data");
    if (result == BS_RECOMP_OK) result = bs_recomp_run(fixture, 8309);
    if (result == BS_RECOMP_OK) {
        bs_recomp_enable_live_input(fixture, 1);
        bs_recomp_set_input(fixture, 0,
            BS_INPUT_DOWN | BS_INPUT_LEFT | BS_INPUT_FIRE | BS_INPUT_NOVA);
        bs_recomp_set_input(fixture, 1, BS_INPUT_UP | BS_INPUT_RIGHT);
        bs_recomp_write8(fixture, 0x4e68, 0);
        bs_recomp_write8(fixture, 0x4f72, 0);
        for (int step = 0; step < 100 && result == BS_RECOMP_OK; step++) {
            result = bs_recomp_run(fixture, 1);
            if (bs_recomp_read8(fixture, 0x4e68) == 0x3c &&
                bs_recomp_read8(fixture, 0x4f72) == 0x03)
                break;
        }
    }
    int passed = result == BS_RECOMP_OK && fixture->live_input_enabled &&
        bs_recomp_read8(fixture, 0x109c) == 0 &&
        bs_recomp_read8(fixture, 0x4e68) == 0x3c &&
        bs_recomp_read8(fixture, 0x4f72) == 0x03;
    if (passed) passed = bs_recomp_run(fixture, 1000) == BS_RECOMP_OK;
    free(fixture);
    return passed;
}

/* $3FB0: with fire held the primary repeats every sixteenth game frame.  A
 * running cooldown at +46 must end LAB_3F54 outright; falling through to the
 * LAB_3FD0 fire path decremented the +57 auto-repeat counter twice on those
 * frames and the ship fired every thirteenth frame instead. */
static int primary_fire_cadence_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);

    long volleys[6];
    unsigned found = 0;
    int previously_live = 0;
    for (long frame = 0; frame < 400 && found < 6; frame++) {
        bs_recomp_set_input(fixture, 0, BS_INPUT_FIRE);
        int reached = 0;
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; g < 512 && !reached; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xbce) reached = 1;
        }
        if (!reached) goto done;
        /* The shot slot the primary always loads first. */
        int live = bs_recomp_read16(fixture, 0x4e3c + 122 + 2 * 12) != 0;
        if (live && !previously_live) volleys[found++] = frame;
        previously_live = live;
    }
    passed = found == 6;
    for (unsigned i = 1; passed && i < found; i++)
        passed = volleys[i] - volleys[i - 1] == 16;
    if (!passed)
        fprintf(stderr, "fire cadence: %u volleys, gaps", found);
    for (unsigned i = 1; !passed && i < found; i++)
        fprintf(stderr, " %ld", volleys[i] - volleys[i - 1]);
    if (!passed) fprintf(stderr, "\n");
done:
    free(fixture);
    return passed;
}

static unsigned gameplay_sfx_events;
static int gameplay_sfx_bad;

static void count_gameplay_sample(void *user, const BsAudioSampleEvent *event)
{
    (void)user;
    gameplay_sfx_events++;
    /* $2539C descriptors: a real sample in the bank, an audible 1..64 volume
     * and a plausible Paula period.  Reading the volume as a byte at +8 -- it
     * is a word -- handed the platform a silent zero. */
    if (event->address < 0x20000 || event->address >= BS_RECOMP_MEMORY_SIZE ||
        event->length_words == 0 || event->volume == 0 ||
        event->volume > 64 || event->period == 0)
        gameplay_sfx_bad = 1;
}

/* Shooting things has to make a noise: the $2470E sound sites in the collision
 * passes, the impact/debris pass and the nova must reach the platform hook. */
static int gameplay_sfx_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0;
    gameplay_sfx_events = 0;
    gameplay_sfx_bad = 0;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);
    bs_recomp_set_audio_sample_hook(fixture, count_gameplay_sample, NULL);
    for (long frame = 0; frame < 600; frame++) {
        bs_recomp_set_input(fixture, 0, BS_INPUT_FIRE);
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xbce) break;
        }
    }
    passed = gameplay_sfx_events >= 10 && !gameplay_sfx_bad;
    if (!passed)
        fprintf(stderr, "gameplay sfx: %u events, malformed=%d\n",
                gameplay_sfx_events, gameplay_sfx_bad);
done:
    free(fixture);
    return passed;
}

/* $4836 picks between an aimed enemy shot and stationary debris on the
 * -14384(A5) flag.  Only the debris exit used to be translated, so every enemy
 * bullet got the debris sprite ($C6B6 entry $60, 12 lines) and a non-zero +19
 * timer, which sends the updater down its home-in-on-the-player branch.  The
 * shots drifted at the ship as tall figures -- "floating gold men" -- instead
 * of flying out as small round dots.  An aimed shot must be height 7, graphics
 * $58, timer 0 and carry a real velocity. */
static int aimed_enemy_shot_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0, aimed = 0, debris_shaped = 0;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);
    for (long frame = 0; frame < 400; frame++) {
        bs_recomp_set_input(fixture, 0, BS_INPUT_FIRE);
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xbce) break;
        }
        for (unsigned slot = 0; slot < 16; slot++) {
            uint32_t effect = 0x4976 + slot * 20;
            if (bs_recomp_read16(fixture, effect) == 0) continue;
            uint8_t height = bs_recomp_read8(fixture, effect + 16);
            uint8_t graphic = bs_recomp_read8(fixture, effect + 17);
            uint32_t vx = bs_recomp_read32(fixture, effect + 8);
            uint32_t vy = bs_recomp_read32(fixture, effect + 12);
            if (height == 7 && graphic == 0x58 && (vx || vy)) aimed++;
            if (height == 12 && graphic == 0x60) debris_shaped++;
        }
    }
    /* The opening waves shoot; if nothing aimed ever appears the aimed exit
     * is unreachable again. */
    passed = aimed > 0 && aimed > debris_shaped;
    if (!passed)
        fprintf(stderr, "aimed enemy shots: %d aimed, %d debris-shaped\n",
                aimed, debris_shaped);
done:
    free(fixture);
    return passed;
}

/* LAB_40BE at $34DC/$35B2 was never called, so the eight ASCII score digits
 * ending at -18620(A5) stayed "00000000" for the whole game.  Shooting things
 * has to move them, and the result has to stay a decimal string. */
static int score_award_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);
    for (long frame = 0; frame < 300; frame++) {
        bs_recomp_set_input(fixture, 0, BS_INPUT_FIRE);
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xbce) break;
        }
    }
    /* Player one's eight digits, $4EA6..$4EAD (the record base plus 106). */
    int scored = 0, decimal = 1;
    for (int column = 0; column < 8; column++) {
        uint8_t digit = bs_recomp_read8(fixture, 0x4e3c + 106 + column);
        if (digit < '0' || digit > '9') decimal = 0;
        if (digit != '0') scored = 1;
    }
    passed = scored && decimal;
    if (!passed) {
        fprintf(stderr, "score: \"");
        for (int column = 0; column < 8; column++)
            fputc(bs_recomp_read8(fixture, 0x4e3c + 106 + column), stderr);
        fprintf(stderr, "\" (scored=%d decimal=%d)\n", scored, decimal);
    }
done:
    free(fixture);
    return passed;
}

/* $24F34/$24856: the music sequencer.  Audio bring-up armed the CIA-B timer
 * and pointed $000008 at $24F34, but the dispatcher never runs guest interrupt
 * code, so nothing restarted a channel and Paula stayed silent.  Driven from
 * the frame loop it has to keep all four channels supplied with periods and
 * volumes and leave the driver out of its idle state. */
static unsigned music_period_writes[4], music_volume_writes[4];
static unsigned music_dmacon_writes;

static void count_music_write(void *user, uint16_t reg, uint16_t value)
{
    (void)user; (void)value;
    if (reg == 0x096) music_dmacon_writes++;
    for (int channel = 0; channel < 4; channel++) {
        if (reg == 0x0a6 + channel * 16) music_period_writes[channel]++;
        if (reg == 0x0a8 + channel * 16) music_volume_writes[channel]++;
    }
}

static int music_sequencer_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0;
    music_dmacon_writes = 0;
    for (int channel = 0; channel < 4; channel++)
        music_period_writes[channel] = music_volume_writes[channel] = 0;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);
    bs_recomp_set_music_enabled(fixture, 1);
    bs_recomp_set_custom_write_hook(fixture, count_music_write, NULL);
    for (long frame = 0; frame < 200; frame++) {
        bs_recomp_set_input(fixture, 0, BS_INPUT_FIRE);
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xbce) break;
        }
    }
    passed = music_dmacon_writes > 100 &&
             bs_recomp_read8(fixture, 0x251f8) == 0;
    for (int channel = 0; channel < 4; channel++)
        if (!music_period_writes[channel] || !music_volume_writes[channel])
            passed = 0;
    if (!passed) {
        fprintf(stderr, "music: dmacon=%u idle=%u periods",
                music_dmacon_writes, bs_recomp_read8(fixture, 0x251f8));
        for (int channel = 0; channel < 4; channel++)
            fprintf(stderr, " %u/%u", music_period_writes[channel],
                    music_volume_writes[channel]);
        fprintf(stderr, "\n");
    }
done:
    free(fixture);
    return passed;
}

/* LAB_410A used to be an eleven-line stub, so the recompilation played level
 * one forever: 120,000 game frames without the mode ever changing.  Losing
 * every life has to run the initials entry ($4232), which parks the ship at
 * $3E7 and arms the $FA hold at -16122(A5); LAB_410A then counts that down and
 * stages the next screen by setting 8514 and 8524. */
static int end_of_game_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0, entry_done = 0, staged = 0;
    long entry_frame = -1, staged_frame = -1;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);
    for (long frame = 0; frame < 2000 && !staged; frame++) {
        /* Hold fire while there are lives left, then let go: $436C restarts
         * the ship the moment fire is seen at the game-over screen, which is
         * the arcade "press fire to play again" path.  Releasing it lets the
         * $FA hold expire so the screen change can be observed instead. */
        bs_recomp_set_input(fixture, 0,
            bs_recomp_read8(fixture, 0x4e3c + 56) ? BS_INPUT_FIRE : 0);
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) goto done;
            if (fixture->cpu.pc == 0xbce) break;
        }
        if (!entry_done && bs_recomp_read8(fixture, 0x4e3c + 91) == 0xff) {
            entry_done = 1;
            entry_frame = frame;
        }
        if (bs_recomp_read16(fixture, 0x8000 + 8514) != 0) {
            staged = 1;
            staged_frame = frame;
        }
    }
    passed = entry_done && staged &&
             bs_recomp_read16(fixture, 0x8000 + 8524) == 0xffff &&
             bs_recomp_read16(fixture, 0x4e3c + 68) == 0x03e6;

    /* The other exit: with fire held, $437A resets the ship through $127E for
     * another game rather than changing screen. */
    if (passed) {
        BsRecomp *again = calloc(1, sizeof *again);
        int restarted = 0;
        if (again && bs_recomp_init(again,
                "original/whdload/BattleSquadron/data") == BS_RECOMP_OK) {
            int ok = 1;
            for (unsigned e = 0; e < 4 && ok; e++)
                ok = bs_recomp_run(again, edges[e].steps) == BS_RECOMP_OK &&
                     again->cpu.pc == edges[e].pc;
            if (ok) ok = bs_recomp_start_new_game(again) == BS_RECOMP_OK;
            if (ok) bs_recomp_enable_live_input(again, 1);
            for (long frame = 0; ok && frame < 1200 && !restarted; frame++) {
                bs_recomp_set_input(again, 0, BS_INPUT_FIRE);
                for (int g = 0; g < 512; g++) {
                    if (bs_recomp_run(again, 1) != BS_RECOMP_OK) { ok = 0; break; }
                    if (again->cpu.pc == 0xaa0) break;
                }
                for (int g = 0; ok && g < 512; g++) {
                    if (bs_recomp_run(again, 1) != BS_RECOMP_OK) { ok = 0; break; }
                    if (again->cpu.pc == 0xbce) break;
                }
                if (ok && bs_recomp_read8(again, 0x4e3c + 91) == 0 &&
                    bs_recomp_read8(again, 0x4e3c + 38) == 0x96 &&
                    bs_recomp_read8(again, 0x4e3c + 56) > 0 && frame > 810)
                    restarted = 1;
            }
        }
        free(again);
        if (!restarted) {
            fprintf(stderr, "end of game: fire at the game-over screen did "
                    "not restart the ship\n");
            passed = 0;
        }
    }
    if (!passed)
        fprintf(stderr, "end of game: entry=%ld staged=%ld 8524=$%04X "
                "p1+68=$%04X\n", entry_frame, staged_frame,
                bs_recomp_read16(fixture, 0x8000 + 8524),
                bs_recomp_read16(fixture, 0x4e3c + 68));
done:
    free(fixture);
    return passed;
}

/* The stage never used to change: 7230(A5) held the pending stage but nothing
 * applied it, so the recompilation replayed stage zero for ever.  LAB_7002
 * now runs the clear sequence and $7180 copies 7230 into 7228.  Reaching it
 * needs a ship parked in the type-$27 gate's 32-pixel box for ten frames, so
 * this steers towards the gate rather than holding fire and hoping. */
static int stage_advance_test(void)
{
    BsRecomp *fixture = calloc(1, sizeof *fixture);
    if (!fixture) return 0;
    int passed = 0;
    unsigned reached = 0;
    if (bs_recomp_init(fixture,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK)
        goto done;
    static const struct { long steps; uint32_t pc; } edges[] = {
        {38, 0x4ec}, {46, 0x5f6}, {16, 0x6fa}, {40, 0x7d0}};
    for (unsigned e = 0; e < 4; e++)
        if (bs_recomp_run(fixture, edges[e].steps) != BS_RECOMP_OK ||
            fixture->cpu.pc != edges[e].pc)
            goto done;
    if (bs_recomp_start_new_game(fixture) != BS_RECOMP_OK) goto done;
    bs_recomp_enable_live_input(fixture, 1);
    for (long frame = 0; frame < 5000; frame++) {
        int gate_x = -1, gate_y = -1;
        for (unsigned slot = 0; slot < 18; slot++) {
            uint32_t object = 0x2e040 + slot * 0x40;
            if (bs_recomp_read16(fixture, object) == 0) continue;
            if (bs_recomp_read8(fixture, object + 17) == 0x27) {
                gate_x = bs_recomp_read16(fixture, object);
                gate_y = bs_recomp_read16(fixture, object + 2);
                break;
            }
        }
        unsigned input = BS_INPUT_FIRE;
        if (gate_x >= 0) {
            int px = bs_recomp_read16(fixture, 0x4e3c + 4);
            int py = bs_recomp_read16(fixture, 0x4e3c + 6);
            if (px < gate_x - 2) input |= BS_INPUT_RIGHT;
            else if (px > gate_x + 2) input |= BS_INPUT_LEFT;
            if (py < gate_y - 2) input |= BS_INPUT_DOWN;
            else if (py > gate_y + 2) input |= BS_INPUT_UP;
        }
        bs_recomp_set_input(fixture, 0, input);
        int running = 1;
        for (int g = 0; g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) { running = 0; break; }
            if (fixture->cpu.pc == 0xaa0) break;
        }
        for (int g = 0; running && g < 512; g++) {
            if (bs_recomp_run(fixture, 1) != BS_RECOMP_OK) { running = 0; break; }
            if (fixture->cpu.pc == 0xbce) break;
        }
        unsigned stage = bs_recomp_read16(fixture, 0x8000 + 7228);
        if (stage > reached) reached = stage;
        if (!running) break;
    }
    /* Stage one is enough to prove the transition; its own object behaviour
     * is a separate translation still outstanding. */
    passed = reached >= 1;
    if (!passed)
        fprintf(stderr, "stage advance: never left stage %u\n", reached);
done:
    free(fixture);
    return passed;
}

int main(void)
{
    BsRecomp *machine = malloc(sizeof *machine);
    if (!machine) return 1;
    if (bs_recomp_init(machine,
            "original/whdload/BattleSquadron/data") != BS_RECOMP_OK) {
        fprintf(stderr, "recomp init: %s\n", machine->error);
        free(machine);
        return 1;
    }
    uint8_t before[128];
    memcpy(before, machine->memory + 0xaf14, sizeof before);
    int failed = 0;
    #define CHECK(condition, message) do { if (!(condition)) { \
        fprintf(stderr, "recomp boot: %s\n", message); failed = 1; \
    } } while (0)
    CHECK(projectile_cookie_cut_test(),
          "native projectile cookie-cut compositor failed");
    CHECK(collision_pass_test(),
          "native collision/damage/pickup passes failed");
    CHECK(object_death_animation_test(),
          "object death animation/completion failed");
    CHECK(live_input_test(),
          "live two-player input mapping/continuation failed");
    CHECK(primary_fire_cadence_test(),
          "primary fire did not repeat every sixteenth game frame");
    CHECK(gameplay_sfx_test(),
          "gameplay produced no usable sound-effect samples");
    CHECK(aimed_enemy_shot_test(),
          "enemy shots spawned as stationary debris (the gold men)");
    CHECK(score_award_test(),
          "shooting things awarded no score");
    CHECK(music_sequencer_test(),
          "the music sequencer produced no Paula activity");
    CHECK(end_of_game_test(),
          "losing every life never ended the game");
    CHECK(stage_advance_test(),
          "clearing the stage never advanced the level");
    bs_recomp_set_audio_sample_hook(machine, capture_audio_sample, NULL);
    int result = bs_recomp_run(machine, 38);
    CHECK(result == BS_RECOMP_OK,
          "did not reach the intro return checkpoint");
    CHECK(machine->cpu.pc == 0x4ec, "unexpected boundary PC");
    CHECK(machine->translated_steps == 38, "unexpected translated step count");
    CHECK(machine->file_load_count == 7, "initial overlays were not loaded");
    CHECK(machine->cpu.a[5] == 0x8000 && machine->cpu.a[6] == 0xdff000 &&
          machine->cpu.a[7] == 0x10000, "bootstrap base registers differ");
    CHECK(machine->cpu.a[0] == 0xaea8,
          "palette destination register differs");
    CHECK(machine->cpu.sr == 0x2714, "status register differs from oracle");
    CHECK(machine->cpu.d[0] == 0xffff, "fade D0 differs from oracle");
    CHECK(machine->cpu.d[1] == 2 && machine->cpu.d[2] == 0 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.d[4] == 0 &&
          machine->cpu.d[5] == 0xff && machine->cpu.d[6] == 0xfc &&
          machine->cpu.d[7] == 0xffff,
          "palette routine data registers differ from oracle");
    CHECK(machine->cpu.a[1] == 0x79700 && machine->cpu.a[2] == 0x7f028 &&
          machine->cpu.a[3] == 0x1069a && machine->cpu.a[4] == 0x7fb00,
          "address register differs from oracle");
    CHECK(bs_recomp_read32(machine, 0x3fc) == 0x5e000,
          "bootstrap high-memory pointer differs");
    CHECK(machine->custom[0x80 >> 1] == 0x0000 &&
          machine->custom[0x82 >> 1] == 0xadde,
          "COP1LC writes differ");
    for (int i = 0; i < 32; i++) {
        CHECK(machine->memory[0xaf16 + i * 4] == before[2 + i * 4] &&
              machine->memory[0xaf17 + i * 4] == before[3 + i * 4],
              "palette expansion changed an interleaved word");
    }
    CHECK(fnv1a(machine->memory + 0xaf14, 0x80) == UINT32_C(0x8d03039f),
          "darkened palette memory hash differs from oracle");
    CHECK(fnv1a(machine->memory + 0xae28, 0x80) == UINT32_C(0x8d03039f),
          "cleared secondary palette hash differs from oracle");
    CHECK(fnv1a(machine->memory + 0x3df3c, 0xfc) == UINT32_C(0x9e98741f),
          "music state hash differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1737a, 0x18) == UINT32_C(0x3670b87d),
          "loader callback table differs from oracle");
    CHECK(fnv1a(machine->memory + 0x44000, 0x9c44) == UINT32_C(0x3be300e7),
          "intro bitmap differs from oracle");
    CHECK(fnv1a(machine->memory + 0x7f000, 0x40) == UINT32_C(0xe8b84dff),
          "intro line buffer differs from oracle");
    CHECK(fnv1a(machine->memory + 0x7fb30, 8) == UINT32_C(0x1b47a997),
          "intro cursor differs from oracle");
    CHECK(bs_recomp_read32(machine, 8) == 0x3dda2,
          "CIA-B interrupt vector differs");
    CHECK(machine->intena == 0x4000 && machine->dmacon == 0x000f,
          "music interrupt/DMA enable differs");
    CHECK(machine->ciab[4] == 0 && machine->ciab[5] == 0x25 &&
          machine->ciab[13] == 0x81 && machine->ciab[14] == 0x11,
          "music CIA-B setup differs");
    CHECK(welcome_event_count == 1 &&
          welcome_event.address == 0x246f0 &&
          welcome_event.length_words == 0x17cd &&
          welcome_event.period == 0x01ac &&
          welcome_event.volume == 64 && welcome_event.channel == 0,
          "translated welcome-speech event differs from LODMUS descriptor");
    CHECK(fnv1a(machine->memory + welcome_event.address,
                (size_t)welcome_event.length_words * 2) ==
                UINT32_C(0x917f84cb),
          "welcome-speech payload differs from byte-exact LODSPE");
    size_t expected_size = 0;
    unsigned char *expected = read_file("original/modules/LODLOD.bin",
                                        &expected_size);
    CHECK(expected && expected_size == 41600 &&
          !memcmp(machine->memory + 0x30000, expected, expected_size),
          "LODLOD runtime image differs");
    free(expected);

    result = bs_recomp_run(machine, 31);
    CHECK(result == BS_RECOMP_OK,
          "did not reach the second-stage checkpoint");
    CHECK(machine->cpu.pc == 0x58a, "unexpected final boundary PC");
    CHECK(machine->translated_steps == 69,
          "unexpected final translated step count");
    CHECK(machine->file_load_count == 10,
          "second-stage overlays were not loaded");
    CHECK(machine->cpu.sr == 0x2710 && machine->cpu.d[0] == 0xffff &&
          machine->cpu.d[1] == 2 && machine->cpu.d[2] == 0 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.d[4] == 0x800 &&
          machine->cpu.d[5] == 0xff && machine->cpu.d[6] == 0xfc &&
          machine->cpu.d[7] == 0x0f,
          "second-stage data registers differ from oracle");
    CHECK(machine->cpu.a[0] == 0xaf94 && machine->cpu.a[1] == 0xaf14 &&
          machine->cpu.a[2] == 0xaf14 && machine->cpu.a[3] == 0x193a &&
          machine->cpu.a[4] == 0x1b60,
          "second-stage address registers differ from oracle");
    CHECK(machine->custom[0x80 >> 1] == 0x0000 &&
          machine->custom[0x82 >> 1] == 0xaeca,
          "second-stage COP1LC differs from oracle");
    CHECK(fnv1a(machine->memory + 0x62000, 0xa280) == UINT32_C(0xf77fddaf),
          "relocated LODLOD image differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2e508, 0xf2f8) == UINT32_C(0x86b57c08),
          "relocated LODS0F data differs from oracle");
    CHECK(fnv1a(machine->memory + 0x9ea6, 0x2500) == UINT32_C(0x19aed1ec),
          "title object adjustments differ from oracle");

    result = bs_recomp_run(machine, 5);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x5a4,
          "did not reach the title-state checkpoint");
    CHECK(machine->translated_steps == 74 && machine->file_load_count == 11,
          "title-state checkpoint counters differ");
    CHECK(machine->cpu.sr == 0x2704 && machine->cpu.d[0] == 0xffff &&
          machine->cpu.d[1] == 0xc0520000 && machine->cpu.a[0] == 0x2e040 &&
          machine->cpu.a[4] == 0x19b0,
          "title-state registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x1400, 0xb800) == UINT32_C(0x74f439f1),
          "low title-state memory differs from oracle");
    CHECK(fnv1a(machine->memory + 0x10000, 0x1e4c0) == UINT32_C(0x8cdb022a),
          "high title-state memory differs from oracle");
    result = bs_recomp_run(machine, 10);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x5f6,
          "did not reach the title-wait checkpoint");
    CHECK(machine->translated_steps == 84 && machine->file_load_count == 11,
          "title-wait checkpoint counters differ");
    CHECK(machine->cpu.sr == 0x2710 && machine->cpu.d[0] == 0x1f3 &&
          machine->cpu.d[1] == 2 && machine->cpu.d[2] == 0xb00 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.d[4] == 0xb00 &&
          machine->cpu.d[5] == 0xfff && machine->cpu.d[6] == 0x0c &&
          machine->cpu.d[7] == 0xffff,
          "title-wait data registers differ from oracle");
    CHECK(machine->cpu.a[0] == 0xb0a8 && machine->cpu.a[1] == 0x17fa &&
          machine->cpu.a[2] == 0xb028 && machine->cpu.a[3] == 0x183a &&
          machine->cpu.a[4] == 0x19b0,
          "title-wait address registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0xb028, 0x80) == UINT32_C(0x2fd61cbd),
          "title palette differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1000, 0xc000) == UINT32_C(0x2e31419f),
          "title-wait memory differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x64c,
          "did not complete the translated title wait");
    CHECK(machine->translated_steps == 85 && machine->file_load_count == 11,
          "title-wait completion counters differ");
    CHECK(machine->cpu.sr == 0x2709 && machine->cpu.d[0] == 0xffff &&
          machine->cpu.d[1] == 0 && machine->cpu.d[2] == 0xb00 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.d[4] == 0xffff &&
          machine->cpu.d[5] == 0xffff && machine->cpu.d[6] == 0xffff &&
          machine->cpu.d[7] == 0xffff,
          "title-wait completion registers differ from oracle");
    CHECK(machine->cpu.a[0] == 0x6d31d && machine->cpu.a[1] == 0x246f0 &&
          machine->cpu.a[2] == 0xaada && machine->cpu.a[3] == 0x6b56d,
          "keyboard redraw registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x62000, 0xa280) == UINT32_C(0x0ecbe4d1),
          "keyboard-help bitmap differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1000, 0xc000) == UINT32_C(0xa742cb5c),
          "title-wait completion memory differs from oracle");
    result = bs_recomp_run(machine, 15);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x6fa,
          "did not reach the second title-fade checkpoint");
    CHECK(machine->translated_steps == 100 && machine->file_load_count == 12,
          "second title-fade counters differ");
    CHECK(machine->cpu.sr == 0x2710 && machine->cpu.d[0] == 0xffff &&
          machine->cpu.d[1] == 2 && machine->cpu.d[2] == 0xf00 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.d[4] == 0xf00 &&
          machine->cpu.d[5] == 0xfff && machine->cpu.d[6] == 0x0c &&
          machine->cpu.d[7] == 0xffff,
          "second title-fade registers differ from oracle");
    CHECK(machine->cpu.a[0] == 0xacd8 && machine->cpu.a[1] == 0x179a &&
          machine->cpu.a[2] == 0xac58 && machine->cpu.a[3] == 0x17da &&
          machine->cpu.a[4] == 0x19c8,
          "second title-fade address registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x62000, 0xa280) == UINT32_C(0x0ecbe4d1),
          "second title-fade bitmap differs from oracle");
    result = bs_recomp_run(machine, 12);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x73a,
          "did not reach the game-mode reinitialisation checkpoint");
    CHECK(machine->translated_steps == 112 && machine->file_load_count == 12,
          "game-mode reinitialisation counters differ");
    CHECK(machine->cpu.sr == 0x2714 && machine->cpu.d[0] == 0xffff &&
          machine->cpu.d[1] == 0xc0520000 && machine->cpu.d[2] == 0 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.d[4] == 0xf00 &&
          machine->cpu.d[5] == 0xff && machine->cpu.d[6] == 0xfc &&
          machine->cpu.d[7] == 0x0f,
          "game-mode reinitialisation registers differ from oracle");
    CHECK(machine->cpu.a[0] == 0x2e040 && machine->cpu.a[1] == 0xac58 &&
          machine->cpu.a[2] == 0xac58 && machine->cpu.a[3] == 0x17da &&
          machine->cpu.a[4] == 0x19c8 && machine->cpu.a[7] == 0xfffe,
          "game-mode reinitialisation address registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x10000, 0x1e4c0) == UINT32_C(0x904de28b),
          "game-mode state memory differs from oracle");
    CHECK(fnv1a(machine->memory + 0x5e000, 0x4000) == UINT32_C(0x38699dc5),
          "cleared high work area differs from oracle");
    result = bs_recomp_run(machine, 14);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x784,
          "did not reach the player-state checkpoint");
    CHECK(machine->translated_steps == 126 && machine->file_load_count == 12,
          "player-state checkpoint counters differ");
    CHECK(machine->cpu.sr == 0x2714 && machine->cpu.a[7] == 0x10000,
          "player-state checkpoint registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x4700, 0x900) == UINT32_C(0x567ccc75),
          "player-state memory differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x78e,
          "did not initialise player one");
    CHECK(machine->translated_steps == 128 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x5c &&
          machine->cpu.d[2] == 0x14 && machine->cpu.a[0] == 0x4f46 &&
          machine->cpu.a[1] == 0x3e50 && machine->cpu.a[2] == 0x2080,
          "player-one initialisation registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x10a) == UINT32_C(0x5bacc13f),
          "player-one object differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x798,
          "did not initialise player two");
    CHECK(machine->translated_steps == 130 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x40 &&
          machine->cpu.d[2] == 0x10 && machine->cpu.a[0] == 0x5050 &&
          machine->cpu.a[1] == 0x3c40 && machine->cpu.a[2] == 0x2064,
          "player-two initialisation registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0x779fc67a),
          "two-player object state differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x79c &&
          machine->translated_steps == 131 && machine->cpu.a[0] == 0xc6c2 &&
          machine->cpu.a[1] == 0x2a5e && machine->cpu.d[0] == 0xffff,
          "player-one capacity expansion differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2a4e, 0x20) == UINT32_C(0xfd3780ff),
          "capacity lookup table differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7a0 &&
          machine->translated_steps == 132 && machine->cpu.a[0] == 0xc6f6 &&
          machine->cpu.a[1] == 0x2a5e && machine->cpu.d[0] == 0xffff,
          "player-two capacity expansion differs from oracle");
    CHECK(fnv1a(machine->memory + 0xc000, 0x700) == UINT32_C(0x652ae30f),
          "expanded capacity bars differ from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7a8 &&
          machine->translated_steps == 134 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[5] == 0 &&
          machine->cpu.a[0] == 0xb7d4 && machine->cpu.a[2] == 0x4461 &&
          machine->cpu.a[3] == 0x4451,
          "life-icon rendering differs from oracle");
    CHECK(fnv1a(machine->memory + 0xaf00, 0x600) == UINT32_C(0xab1db069),
          "life-icon bitmap differs from oracle");
    result = bs_recomp_run(machine, 6);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7d0 &&
          machine->translated_steps == 140 && machine->file_load_count == 12,
          "did not reach the translated frame-loop edge");
    CHECK(machine->cpu.sr == 0x2700 && machine->cpu.d[0] == 0xff &&
          machine->cpu.a[0] == 0xb7d4 && machine->cpu.a[1] == 0x2a5e &&
          machine->cpu.a[2] == 0x4461 && machine->cpu.a[3] == 0x4451,
          "frame-loop entry registers differ from oracle");
    CHECK(fnv1a(machine->memory + 0x7500, 0x2900) == UINT32_C(0x3aca336a),
          "frame-loop entry state differs from oracle");
    CHECK(fnv1a(machine->memory + 0x62000, 0x6000) == UINT32_C(0xed3ba1db) &&
          fnv1a(machine->memory + 0x68000, 0x6000) == UINT32_C(0xfbee397b) &&
          fnv1a(machine->memory + 0x6e000, 0x6000) == UINT32_C(0xcc2cfa4f),
          "frame-loop entry screen planes differ from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7d8 &&
          machine->translated_steps == 141 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x000800ff &&
          machine->cpu.d[2] == 0xf0 && machine->cpu.d[3] == 1 &&
          machine->cpu.a[0] == 0xb2a0 && machine->cpu.a[1] == 0x4da3e &&
          machine->cpu.a[2] == 0x47420 && machine->cpu.a[3] == 0x4a01e &&
          machine->cpu.a[4] == 0x68000 && machine->cpu.a[7] == 0xfffe,
          "first native scroll frame differs from oracle");
    CHECK(fnv1a(machine->memory + 0x9c24, 0x20) == UINT32_C(0x860fc998),
          "first scroll state differs from oracle");
    CHECK(fnv1a(machine->memory + 0x44000, 0x1a000) == UINT32_C(0xc0f64448),
          "first native tile-column copy differs from oracle");
    CHECK(fnv1a(machine->memory + 0x62000, 0x6000) == UINT32_C(0x241e8de0) &&
          fnv1a(machine->memory + 0x68000, 0x6000) == UINT32_C(0x28bb425a) &&
          fnv1a(machine->memory + 0x6e000, 0x6000) == UINT32_C(0xe8bfc0e1),
          "first native tile-column screen planes differ from oracle");
    BsRecomp *second_frame = malloc(sizeof *second_frame);
    CHECK(second_frame != NULL, "could not allocate second-frame parity clone");
    if (second_frame) {
        memcpy(second_frame, machine, sizeof *second_frame);
        int second_result = bs_recomp_run(second_frame, 5);
        CHECK(second_result == BS_RECOMP_OK && second_frame->cpu.pc == 0x7d8,
              "second native transform frame did not reach its checkpoint");
        CHECK(fnv1a(second_frame->memory + 0x62000, 0x6000) ==
                  UINT32_C(0x0514efb4) &&
              fnv1a(second_frame->memory + 0x68000, 0x6000) ==
                  UINT32_C(0x7631ff62) &&
              fnv1a(second_frame->memory + 0x6e000, 0x6000) ==
                  UINT32_C(0xa4b9ee96),
              "second native tile-column screen planes differ from oracle");
        free(second_frame);
    }
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7dc &&
          machine->translated_steps == 142 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[3] == 0xffff && machine->cpu.a[1] == 0x5c2a &&
          machine->cpu.a[4] == 0x2e4c0,
          "empty entity-pool update differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2e040, 0x480) == UINT32_C(0x684a07c5),
          "empty entity pool differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7e0 &&
          machine->translated_steps == 143 && machine->cpu.sr == 0x2700,
          "first wave-spawner gate differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2d000, 0x10000) == UINT32_C(0x6bdfae53),
          "first translated frame memory differs from oracle");
    result = bs_recomp_run(machine, 1274);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7dc &&
          machine->translated_steps == 1417 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x000800ff &&
          machine->cpu.d[2] == 0xf0 && machine->cpu.d[3] == 0xffff &&
          machine->cpu.a[0] == 0xb2a0 && machine->cpu.a[1] == 0x5c2a &&
          machine->cpu.a[2] == 0x47150 && machine->cpu.a[3] == 0x4a000 &&
          machine->cpu.a[4] == 0x2e4c0 && machine->cpu.a[7] == 0xfffe,
          "256th translated scroll/entity edge differs from oracle");
    CHECK(fnv1a(machine->memory + 0x9c24, 0x20) == UINT32_C(0x93908c14),
          "256-frame scroll state differs from oracle");
    CHECK(fnv1a(machine->memory + 0x44000, 0x1a000) == UINT32_C(0xc0f64448),
          "256-frame tile-ring image differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2e040, 0x480) == UINT32_C(0x684a07c5),
          "pre-spawn entity pool differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7e0 &&
          machine->translated_steps == 1418 && machine->cpu.sr == 0x2709 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x00084ec0 &&
          machine->cpu.d[2] == 0x000005a0 && machine->cpu.d[3] == 1 &&
          machine->cpu.d[4] == 0x0f00 && machine->cpu.d[5] == 0 &&
          machine->cpu.d[6] == 0xfc && machine->cpu.d[7] == 0x10 &&
          machine->cpu.a[0] == 0xb2a0 && machine->cpu.a[1] == 0x47120 &&
          machine->cpu.a[2] == 0x2da8 && machine->cpu.a[3] == 0x4a000 &&
          machine->cpu.a[4] == 0x2e080 && machine->cpu.a[7] == 0xfffe,
          "first native wave spawn differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2e040, 0x480) == UINT32_C(0x62ea2996),
          "first spawned entity records differ from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7e6 &&
          machine->translated_steps == 1420 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.a[7] == 0x10000,
          "transform scroll loop did not terminate like the oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0x7f8 &&
          machine->translated_steps == 1421 && machine->cpu.sr == 0x2719 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 2 &&
          machine->cpu.d[2] == 0x0900 && machine->cpu.d[3] == 0xffff &&
          machine->cpu.d[4] == 0x0900 && machine->cpu.d[5] == 0x0fff &&
          machine->cpu.d[6] == 0x0c && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[0] == 0xb320 && machine->cpu.a[1] == 0x14f6 &&
          machine->cpu.a[2] == 0xb2a0 && machine->cpu.a[3] == 0x1536 &&
          machine->cpu.a[4] == 0x2e080,
          "post-transform palette handoff differs from oracle");
    CHECK(fnv1a(machine->memory + 0xb2a0, 0x80) == UINT32_C(0xb8a2e897),
          "post-transform palette differs from oracle");
    result = bs_recomp_run(machine, 6);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xaa0 &&
          machine->translated_steps == 1427 && machine->cpu.sr == 0x2718 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 2 &&
          machine->cpu.d[2] == 0x0900 && machine->cpu.d[3] == 0xffff &&
          machine->cpu.d[4] == 0x0900 && machine->cpu.d[5] == 0x0fff &&
          machine->cpu.d[6] == 0x0c && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[0] == 0xb320 && machine->cpu.a[1] == 0x14f6 &&
          machine->cpu.a[2] == 0xb2a0 && machine->cpu.a[3] == 0x1536 &&
          machine->cpu.a[4] == 0x2e080 && machine->cpu.a[7] == 0x10000,
          "native main-game entry differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1000, 0xc000) == UINT32_C(0x8281277b),
          "native main-game entry memory differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xabc &&
          machine->translated_steps == 1428 && machine->cpu.sr == 0x2714 &&
          machine->cpu.a[1] == 0x7834 && machine->cpu.a[2] == 0x7770,
          "main-game display-list selection differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb34 &&
          machine->translated_steps == 1429 && machine->cpu.sr == 0x2714 &&
          bs_recomp_read32(machine, 0x78f8) == 0x7834 &&
          bs_recomp_read32(machine, 0x78fc) == 0x7770,
          "main-game player-order setup differs from oracle");
    CHECK(fnv1a(machine->memory + 0x7700, 0x200) == UINT32_C(0x04e0b1d1),
          "main-game display-list state differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb54 &&
          machine->translated_steps == 1430 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x000800ff &&
          machine->cpu.d[2] == 0xf0 && machine->cpu.d[3] == 1 &&
          machine->cpu.d[4] == 0x0900 && machine->cpu.d[5] == 0x0fff &&
          machine->cpu.d[6] == 0x0c && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[0] == 0xb2a0 && machine->cpu.a[1] == 0x53d9e &&
          machine->cpu.a[2] == 0x47120 && machine->cpu.a[3] == 0x4a01e &&
          machine->cpu.a[4] == 0x68000 && machine->cpu.a[7] == 0x10000,
          "first native main-game scroll differs from oracle");
    CHECK(fnv1a(machine->memory + 0x9c24, 0x20) == UINT32_C(0x75249d3e),
          "first native main-game scroll state differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb58 &&
          machine->translated_steps == 1431 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 1 && machine->cpu.d[1] == 0x0220 &&
          machine->cpu.d[2] == 0x0100 && machine->cpu.d[3] == 0xffff &&
          machine->cpu.d[4] == 0x0101 && machine->cpu.d[5] == 2 &&
          machine->cpu.d[6] == 0x02f8 && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[0] == 0xb2a0 && machine->cpu.a[1] == 0x5c4a &&
          machine->cpu.a[2] == 0x307ba && machine->cpu.a[3] == 0x4a01e &&
          machine->cpu.a[4] == 0x2e4c0 && machine->cpu.a[7] == 0x10000,
          "first active native object update differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2e040, 0x480) == UINT32_C(0xb6e622d6),
          "first active object-pool image differs from oracle");
    CHECK(fnv1a(machine->memory + 0x5c2a, 0x100) == UINT32_C(0x22b22834),
          "first native object render-list differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb6a &&
          machine->translated_steps == 1432 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[1] == 0x00011b00,
          "main-game post-object raster seam differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb6e &&
          machine->translated_steps == 1433 && machine->cpu.sr == 0x2704 &&
          machine->cpu.a[4] == 0x7770,
          "empty secondary render-list pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x44000, 0x30000) ==
              UINT32_C(0x366ba5d8),
          "pre-object-draw graphics image differs from oracle");
    uint32_t pre_draw_62000 = fnv1a(machine->memory + 0x62000, 0x6000);
    uint32_t pre_draw_68000 = fnv1a(machine->memory + 0x68000, 0x6000);
    uint32_t pre_draw_6e000 = fnv1a(machine->memory + 0x6e000, 0x6000);
    CHECK(pre_draw_62000 == UINT32_C(0x8e2f37e5) &&
          pre_draw_68000 == UINT32_C(0x93a7d005) &&
          pre_draw_6e000 == UINT32_C(0xe1638885),
          "pre-object-draw screen planes differ from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb72 &&
          machine->translated_steps == 1434 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0x43 && machine->cpu.d[1] == 0x00010024 &&
          machine->cpu.d[2] == 0 && machine->cpu.d[3] == 0xffff &&
          machine->cpu.d[4] == 0x0101 && machine->cpu.d[5] == 0x0120 &&
          machine->cpu.d[6] == 0x6000 && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[1] == 0x82ff4 && machine->cpu.a[2] == 0x30d5a &&
          machine->cpu.a[4] == 0x5c4a,
          "first native object draw pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x44000, 0x30000) ==
              UINT32_C(0x1666aaa0),
          "first native object draw image differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb7a &&
          machine->translated_steps == 1436 && machine->cpu.sr == 0x2704 &&
          bs_recomp_read8(machine, 0x7900) == 0 &&
          bs_recomp_read8(machine, 0x7901) == 0,
          "projectile-pass selectors differ from oracle");
    CHECK(fnv1a(machine->memory + 0x2dc80, 0x3c0) == UINT32_C(0x3314a0c5),
          "pre-projectile pool differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb7e &&
          machine->translated_steps == 1437 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.a[1] == 0x7834 &&
          machine->cpu.a[4] == 0x2e040,
          "first empty projectile pass differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xb86 &&
          machine->translated_steps == 1439 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.a[1] == 0x7834 &&
          machine->cpu.a[4] == 0x2e040 &&
          bs_recomp_read8(machine, 0x7901) == 0xff,
          "second empty projectile pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2dc80, 0x3c0) == UINT32_C(0x3314a0c5),
          "post-projectile pool differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xba0 &&
          machine->translated_steps == 1440 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[1] == 0x00012600,
          "post-projectile raster seam differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xbae &&
          machine->translated_steps == 1441 && machine->cpu.sr == 0x2709,
          "HUD update gate differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xbb2 &&
          machine->translated_steps == 1442 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0xc052 &&
          machine->cpu.d[2] == 0xff44 && machine->cpu.d[3] == 0x90 &&
          machine->cpu.d[4] == 0xff && machine->cpu.d[5] == 0x0c94 &&
          machine->cpu.d[6] == 0x0521 && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[0] == 0xbcd6 && machine->cpu.a[1] == 0x26c6 &&
          machine->cpu.a[2] == 0x26fe && machine->cpu.a[3] == 0xb312 &&
          machine->cpu.a[4] == 0x220e,
          "first native HUD/copper rebuild differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2100, 0x700) == UINT32_C(0xc80fb1f0),
          "first native HUD list differs from oracle");
    CHECK(fnv1a(machine->memory + 0xb300, 0x900) == UINT32_C(0x74c1cd0d),
          "first native HUD copper image differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xbba &&
          machine->translated_steps == 1444 && machine->cpu.sr == 0x2704 &&
          machine->cpu.a[4] == 0x5c4a,
          "lower display-list passes differ from oracle");
    CHECK(fnv1a(machine->memory + 0x44000, 0x30000) ==
              UINT32_C(0x1666aaa0),
          "lower display-list passes changed the graphics image");
    result = bs_recomp_run(machine, 6);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xbe8 &&
          machine->translated_steps == 1450 && machine->cpu.sr == 0x2709 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0xd800 &&
          machine->cpu.d[2] == 0xff44 && machine->cpu.d[3] == 0x90 &&
          machine->cpu.d[4] == 0xff && machine->cpu.d[5] == 0x0c94 &&
          machine->cpu.d[6] == 0x0521 && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[1] == 0x7834 && machine->cpu.a[4] == 0x2e040 &&
          bs_recomp_read8(machine, 0x7900) == 0xff &&
          bs_recomp_read8(machine, 0x7901) == 0xff,
          "lower projectile/raster pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x7900, 0x20) == UINT32_C(0xe1782055),
          "lower projectile selector state differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xbfa &&
          machine->translated_steps == 1452 && machine->cpu.sr == 0x2709 &&
          machine->cpu.d[1] == 0xd800,
          "first live player-update gate differs from oracle");
    CHECK(bs_recomp_read16(machine, 0x1078) == 1,
          "frame-animation counter differs from oracle");
    CHECK(bs_recomp_read16(machine, 0x51d0) == 0x2000,
          "player draw offset differs from oracle");
    CHECK(fnv1a(machine->memory + 0x51c0, 0x20) == UINT32_C(0xff2cce67),
          "player draw setup differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1070, 0x20) == UINT32_C(0x0698ae57),
          "first live player-update state differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc00 &&
          machine->translated_steps == 1453 && machine->cpu.sr == 0x2700 &&
          machine->cpu.a[0] == 0x22f82,
          "first recorded-input frame differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1090, 0x20) == UINT32_C(0x27703c89),
          "recorded-input control state differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0x0141dd68),
          "recorded player input bytes differ from oracle");
    CHECK(fnv1a(machine->memory + 0x9a90, 0x20) == UINT32_C(0x743efb1c),
          "recorded-input cursor differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc04 &&
          machine->translated_steps == 1454 && machine->cpu.sr == 0x2708 &&
          machine->cpu.a[4] == 0x4f46,
          "first inactive-player timer pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0x0141dd68),
          "inactive-player timer pass changed player state");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc08 &&
          machine->translated_steps == 1455 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x1d &&
          machine->cpu.d[2] == 0x14 && machine->cpu.d[3] == 0 &&
          machine->cpu.d[4] == UINT32_C(0x20002000) &&
          machine->cpu.d[7] == 0xffff && machine->cpu.a[0] == 0xcbce &&
          machine->cpu.a[1] == 0x17408 && machine->cpu.a[2] == 0xca3a &&
          machine->cpu.a[3] == 0x10a90 && machine->cpu.a[4] == 0x4f46 &&
          machine->cpu.a[6] == 0xdff000,
          "first native respawn-mask pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0xa691fe88),
          "respawn-mask player state differs from oracle");
    CHECK(fnv1a(machine->memory + 0xc800, 0x400) == UINT32_C(0x13a1ef02),
          "respawn-mask bitmap differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc0c &&
          machine->translated_steps == 1456 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x30 &&
          machine->cpu.d[2] == 2 && machine->cpu.d[3] == 1 &&
          machine->cpu.a[4] == 0x4f46,
          "first native respawning-player movement pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0xe155067c),
          "respawning-player state differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc10 &&
          machine->translated_steps == 1457 && machine->cpu.sr == 0x2709 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0xaf &&
          machine->cpu.d[2] == 0x144 && machine->cpu.d[3] == 0x8000 &&
          machine->cpu.d[4] == 0x782 && machine->cpu.d[5] == 0x1c &&
          machine->cpu.d[6] == 0x120 && machine->cpu.d[7] == 0x250 &&
          machine->cpu.a[0] == 0x5050 && machine->cpu.a[1] == 0x61d10 &&
          machine->cpu.a[2] == 0x61910 && machine->cpu.a[3] == 0x4f46 &&
          machine->cpu.a[4] == 0xc6d2,
          "first native ship sprite-list pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x61000, 0x1000) == UINT32_C(0xdc871a89),
          "native ship sprite lists differ from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc14 &&
          machine->translated_steps == 1458 && machine->cpu.sr == 0x2704,
          "empty player-burst pass differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc18 &&
          machine->translated_steps == 1459 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0 &&
          machine->cpu.d[3] == 0 && machine->cpu.d[6] == 0x120 &&
          machine->cpu.d[7] == 0x250 && machine->cpu.a[0] == 0x4ab6 &&
          machine->cpu.a[1] == 0x60c00,
          "first empty native effect-pool pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4970, 0x160) == UINT32_C(0x46e3484d),
          "effect-pool selector state differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4ac0, 0x30) == UINT32_C(0xa3d1ff81),
          "effect-pool list pointers differ from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc1c &&
          machine->translated_steps == 1460 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x62000 &&
          machine->cpu.d[2] == 0x400 && machine->cpu.a[0] == 0xb378 &&
          machine->cpu.a[1] == 0x55ae,
          "first native sprite pointer pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0xb330, 0x60) == UINT32_C(0xaee96a64),
          "sprite bitplane pointers differ from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc22 &&
          machine->translated_steps == 1461 && machine->cpu.sr == 0x2704,
          "inactive special-sequence pass differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc4c &&
          machine->translated_steps == 1462 && machine->cpu.sr == 0x2700 &&
          bs_recomp_read32(machine, 0x3740) == 0x4e3c &&
          bs_recomp_read32(machine, 0x3744) == 0x4eae,
          "first-player collision pointer selection differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc54 &&
          machine->translated_steps == 1464 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[1] == 0x00060000 && machine->cpu.d[7] == 0xffff &&
          machine->cpu.a[0] == 0x4f46 && machine->cpu.a[4] == 0x4e3c,
          "empty player-satellite collision passes differ from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc58 &&
          machine->translated_steps == 1465 && machine->cpu.sr == 0x2700,
          "invulnerable-player effect scan differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc5c &&
          machine->translated_steps == 1466 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x00060189 &&
          machine->cpu.d[2] == 0x217 && machine->cpu.d[3] == 0x177 &&
          machine->cpu.d[4] == 0x207 && machine->cpu.a[0] == 0x2e040,
          "empty enemy-projectile collision pass differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc60 &&
          machine->translated_steps == 1467 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x00060140 &&
          machine->cpu.d[2] == 0x100 && machine->cpu.a[0] == 0xb7c8 &&
          machine->cpu.a[1] == 0x44d0 && machine->cpu.a[2] == 0x10698 &&
          machine->cpu.a[3] == 0x10698,
          "first native demo-scoreboard pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0xb780, 0x100) == UINT32_C(0xb9e2f5d8),
          "demo scoreboard bitmap differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc72 &&
          machine->translated_steps == 1469 && machine->cpu.sr == 0x2709,
          "first native credit/frame branch differs from oracle");
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc76 &&
          machine->translated_steps == 1470 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0xc052 &&
          machine->cpu.d[2] == 0x62 && machine->cpu.d[3] == 0x140 &&
          machine->cpu.d[4] == 0 && machine->cpu.d[5] == 0x0c94 &&
          machine->cpu.d[6] == 0x0521 && machine->cpu.a[0] == 0xbc96 &&
          machine->cpu.a[1] == 0x26be && machine->cpu.a[2] == 0x26f6 &&
          machine->cpu.a[3] == 0xb312 && machine->cpu.a[4] == 0x22be,
          "second native HUD/copper rebuild differs from oracle");
    CHECK(fnv1a(machine->memory + 0x2100, 0x700) == UINT32_C(0x3245db5c),
          "second native HUD list differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc7e &&
          machine->translated_steps == 1472 && machine->cpu.sr == 0x2700 &&
          machine->cpu.d[1] == 0 && machine->cpu.a[1] == 0x1f1e,
          "first native frame-palette pass differs from oracle");
    result = bs_recomp_run(machine, 2);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xc86 &&
          machine->translated_steps == 1474 && machine->cpu.sr == 0x2708 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.a[0] == 0x2e4c0,
          "first native impact/input scan differs from oracle");
    result = bs_recomp_run(machine, 3);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xcaa &&
          machine->translated_steps == 1477 && machine->cpu.sr == 0x2709 &&
          machine->cpu.d[1] == 0 && bs_recomp_read16(machine, 0x1078) == 2,
          "native mid-frame raster/bookkeeping pass differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1070, 0x20) == UINT32_C(0xe1211cd5),
          "mid-frame animation state differs from oracle");
    result = bs_recomp_run(machine, 5);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xcbe &&
          machine->translated_steps == 1482 && machine->cpu.sr == 0x2704 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0 &&
          machine->cpu.d[2] == 0x144 && machine->cpu.d[3] == 0 &&
          machine->cpu.d[4] == 0x00040782 && machine->cpu.d[5] == 0x1c &&
          machine->cpu.d[6] == 0x120 && machine->cpu.d[7] == 0x250 &&
          machine->cpu.a[0] == 0x4ab6 && machine->cpu.a[1] == 0x5ec00,
          "native second-half player/effect passes differ from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0xc76e5cec),
          "second-half player state differs from oracle");
    CHECK(fnv1a(machine->memory + 0xc800, 0x400) == UINT32_C(0x23867976),
          "second-half respawn masks differ from oracle");
    CHECK(fnv1a(machine->memory + 0x5f000, 0x1000) == UINT32_C(0xac7d9f55),
          "second-half sprite lists differ from oracle");
    result = bs_recomp_run(machine, 10);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xaa0 &&
          machine->translated_steps == 1492 && machine->cpu.sr == 0x2719 &&
          machine->cpu.d[0] == 0xffff && machine->cpu.d[1] == 0x00050000 &&
          machine->cpu.d[2] == 0x400 && machine->cpu.d[3] == 0 &&
          machine->cpu.d[4] == 0x00040782 && machine->cpu.d[5] == 0x1c &&
          machine->cpu.d[6] == 0x120 && machine->cpu.d[7] == 0x250 &&
          machine->cpu.a[0] == 0xb378 && machine->cpu.a[1] == 0x55ae &&
          machine->cpu.a[4] == 0x4e3c,
          "first complete native gameplay frame differs from oracle");
    CHECK(fnv1a(machine->memory + 0x1070, 0x20) == UINT32_C(0xe1211cd5),
          "complete-frame animation state differs from oracle");
    CHECK(fnv1a(machine->memory + 0x4e3c, 0x214) == UINT32_C(0xc76e5cec),
          "complete-frame player state differs from oracle");
    result = bs_recomp_run(machine, 6817);
    CHECK(result == BS_RECOMP_OK && machine->cpu.pc == 0xcde &&
          machine->translated_steps == 8309 && machine->cpu.sr == 0x2709 &&
          machine->cpu.d[0] == 0x0b && machine->cpu.d[1] == 0x100a &&
          machine->cpu.d[2] == 0x0300 && machine->cpu.d[3] == 0x60 &&
          machine->cpu.d[4] == 0x0002e802 &&
          machine->cpu.a[0] == 0x2dff0 && machine->cpu.a[1] == 0xd3ca &&
          machine->cpu.a[2] == 0xcdda,
          "first scheduled enemy projectile differs from oracle");
    CHECK(bs_recomp_read32(machine, 0x7550) == 0xd3ca,
          "scheduled-projectile script cursor did not advance");
    CHECK(fnv1a(machine->memory + 0x2dff0, 0x50) == UINT32_C(0x0870b440),
          "scheduled enemy projectile record differs from oracle");
    /* With the death animation translated the attract demo now spends its
     * three lives instead of leaving a permanently dead ship, so it no longer
     * reaches 8000 frames.  It stops at the game-over record path, which is
     * the next thing to translate. */
    /* The demo now plays through deaths, respawns, cleared waves and game
     * over without reaching an untranslated edge. */
    result = bs_recomp_run(machine, 1000000);
    CHECK(result == BS_RECOMP_OK,
          "recorded demo reached an untranslated edge");
    CHECK(bs_recomp_read16(machine, 0x1078) > 0x1f40,
          "recorded demo did not keep advancing its terrain");
    fprintf(stderr, "recorded demo: %ld steps, no untranslated edge\n",
            (long)machine->translated_steps);

    /* The demo-exit and fire-to-start paths are driven on a second machine,
     * early enough in the demo that its lives are still intact. */
    BsRecomp *demo = calloc(1, sizeof *demo);
    CHECK(demo != NULL, "could not allocate the demo-exit fixture");
    if (demo && bs_recomp_init(demo,
            "original/whdload/BattleSquadron/data") == BS_RECOMP_OK) {
        result = bs_recomp_run(demo, 20000);
        CHECK(result == BS_RECOMP_OK, "demo fixture did not reach the demo");
        for (long guard = 0; guard < 4000 && demo->cpu.pc != 0xd16; guard++)
            if ((result = bs_recomp_run(demo, 1)) != BS_RECOMP_OK) break;
        CHECK(result == BS_RECOMP_OK && demo->cpu.pc == 0xd16 &&
              bs_recomp_read8(demo, demo->cpu.a[5] - 28516) != 0 &&
              demo->ciaa[0] == 0xff,
              "demo fixture did not reach a $D16 edge in the demo state");

        /* $D16 with both fire buttons released and the demo counter at its
         * $0FA0 expiry takes the $D3C fade back into the title at LAB_58A. */
        bs_recomp_write16(demo, demo->cpu.a[5] - 28550, 0x0fa0);
        result = bs_recomp_run(demo, 1);
        CHECK(result == BS_RECOMP_OK && demo->cpu.pc == 0x58a &&
              bs_recomp_read32(demo, demo->cpu.a[5] + 13182) == 0xfffffffe &&
              demo->cpu.a[1] == 0xb2a0 && (demo->cpu.sr & 0x1f) == 0x10,
              "expired demo did not take the $D3C title-restart path");
        int faded = 1;
        for (unsigned entry = 0; entry < 32; entry++)
            faded &= (bs_recomp_read16(demo, 0xb2a0 + entry * 4) & 0x0fff) == 0;
        CHECK(faded, "demo-exit fade did not darken the palette to black");
        free(demo);
    }

    /* Fire-to-start, driven the way a frontend does: through the public input
     * API, which must reach CIA-A port A bit 7 for player one. */
    BsRecomp *start = calloc(1, sizeof *start);
    CHECK(start != NULL, "could not allocate the fire-to-start fixture");
    if (start && bs_recomp_init(start,
            "original/whdload/BattleSquadron/data") == BS_RECOMP_OK) {
        result = bs_recomp_run(start, 20000);
        bs_recomp_set_input(start, 0, BS_INPUT_FIRE);
        CHECK(start->ciaa[0] == 0x7f,
              "player-one fire did not reach CIA-A port A bit 7");
        int reached = 0;
        for (long guard = 0; guard < 20000 && !reached; guard++) {
            if ((result = bs_recomp_run(start, 1)) != BS_RECOMP_OK) break;
            if (start->cpu.pc == 0xd52) reached = 1;
        }
        CHECK(result == BS_RECOMP_OK && reached,
              "held fire button did not reach LAB_D52 from the attract demo");

        /* LODCOM and LODMUS share the $3D800 load address, so the resident
         * jump at $3D80C decides which music-stop routine $D52 calls. */
        CHECK(bs_recomp_read16(start, 0x3d80c) == 0x4ef9 &&
              bs_recomp_read32(start, 0x3d80e) == 0x0003dce4,
              "the resident $3D80C entry is not LODMUS's music-stop jump");
        result = bs_recomp_run(start, 1);
        CHECK(result == BS_RECOMP_OK && start->cpu.pc == 0xd58 &&
              (bs_recomp_read16(start, 0xdff096) & 0x000f) == 0 &&
              (bs_recomp_read16(start, 0xdff09a) & 0x4000) != 0 &&
              bs_recomp_read8(start, 0xbfde00) == 0,
              "fire-to-start did not stop the LODMUS timer and audio DMA");

        result = bs_recomp_run(start, 13);
        CHECK(result == BS_RECOMP_OK && start->cpu.pc == 0xaa0,
              "fire-to-start did not complete the $926 new-game sequence");
        CHECK(bs_recomp_read8(start, start->cpu.a[5] - 28516) == 0,
              "fire-to-start left the attract-demo flag set");
        CHECK(bs_recomp_read32(start, start->cpu.a[5] + 13182) == 0xfffffffe &&
              bs_recomp_read8(start, start->cpu.a[5] - 26245) == 0x01,
              "fire-to-start new-game state differs from the title entry");

        /* $A56 brings up LODGAM's audio system. */
        CHECK(bs_recomp_read8(start, 0xbfd500) == 0x31 &&
              bs_recomp_read8(start, 0xbfde00) == 0x11 &&
              bs_recomp_read8(start, 0xbfdd00) == 0x81 &&
              bs_recomp_read32(start, 0x000008) == 0x00024f34,
              "the gameplay audio system did not arm the CIA-B timer");
        CHECK((bs_recomp_read16(start, 0xdff096) & 0x000f) == 0x000f,
              "the gameplay audio system did not enable the audio DMA");
        for (unsigned channel = 0; channel < 4; channel++) {
            uint32_t state = 0x252a4 + channel * 0x3e;
            uint32_t paula = bs_recomp_read32(start, state);
            CHECK(paula == 0xdff0a0 + channel * 0x10 &&
                  bs_recomp_read16(start, paula + 8) == 0 &&
                  bs_recomp_read32(start, state + 4) == 0x25504 &&
                  bs_recomp_read32(start, state + 0x0c) ==
                      bs_recomp_read32(start, state + 8) &&
                  bs_recomp_read8(start, state + 0x3a) == 1,
                  "a gameplay audio channel was not initialised from $25504");
        }
        CHECK(bs_recomp_read8(start, 0x251f8) == 1 &&
              bs_recomp_read16(start, 0x251f8 + 8) == 1 &&
              bs_recomp_read16(start, 0x251f8 + 10) == 0,
              "the audio system did not latch a request for track one");

        /* The started game runs as a live game whose ship explodes and
         * respawns instead of staying dead.  Three deaths consume the three
         * lives and the game then reaches its untranslated game-over path. */
        CHECK(bs_recomp_read8(start, start->cpu.a[5] - 28516) == 0,
              "fire-to-start game did not run on as a live game");
        bs_recomp_set_input(start, 0, 0);
        bs_recomp_enable_live_input(start, 1);
        uint8_t was_dying = 0, was_respawning = 0;
        int deaths = 0, respawns = 0;
        for (long step = 0; step < 400000; step++) {
            if ((result = bs_recomp_run(start, 1)) != BS_RECOMP_OK) break;
            uint8_t dying = bs_recomp_read8(start, 0x4e3c + 49);
            uint8_t respawning = bs_recomp_read8(start, 0x4e3c + 48);
            if (dying != 0 && was_dying == 0) deaths++;
            if (respawning == 0x91 && was_respawning != 0x91) respawns++;
            was_dying = dying;
            was_respawning = respawning;
        }
        CHECK(deaths == 3 && respawns == 3 &&
              bs_recomp_read8(start, 0x4e3c + 56) == 0,
              "the ship did not explode and respawn through its three lives");
        CHECK(result == BS_RECOMP_OK,
              "the live game reached an untranslated edge");
        fprintf(stderr,
                "live game: %d deaths, %d respawns, %ld steps, stopped at "
                "$%06x\n", deaths, respawns, (long)start->translated_steps,
                start->cpu.pc);
        free(start);
    }

    uint32_t translated_pc = machine->cpu.pc;
    machine->cpu.pc = 0x00dead;
    result = bs_recomp_run(machine, 1);
    CHECK(result == BS_RECOMP_UNTRANSLATED && machine->cpu.pc == 0x00dead,
          "unknown translated edge did not fail closed");
    machine->cpu.pc = translated_pc;
    if (failed) {
        fprintf(stderr,
                "observed: pc=%06x sr=%04x d0=%08x d1=%08x d2=%08x "
                "d3=%08x d4=%08x d5=%08x d6=%08x d7=%08x\n",
                machine->cpu.pc, machine->cpu.sr,
                machine->cpu.d[0], machine->cpu.d[1], machine->cpu.d[2],
                machine->cpu.d[3], machine->cpu.d[4], machine->cpu.d[5],
                machine->cpu.d[6], machine->cpu.d[7]);
        fprintf(stderr, "hashes: palette=%08x secondary=%08x music=%08x callbacks=%08x\n",
                fnv1a(machine->memory + 0xaf14, 0x80),
                fnv1a(machine->memory + 0xae28, 0x80),
                fnv1a(machine->memory + 0x3df3c, 0xfc),
                fnv1a(machine->memory + 0x1737a, 0x18));
        fprintf(stderr,
                "addresses: a0=%08x a1=%08x a2=%08x a3=%08x a4=%08x "
                "a5=%08x a6=%08x a7=%08x\n",
                machine->cpu.a[0], machine->cpu.a[1], machine->cpu.a[2],
                machine->cpu.a[3], machine->cpu.a[4], machine->cpu.a[5],
                machine->cpu.a[6], machine->cpu.a[7]);
        fprintf(stderr, "intro hashes: bitmap=%08x line=%08x cursor=%08x\n",
                fnv1a(machine->memory + 0x44000, 0x9c44),
                fnv1a(machine->memory + 0x7f000, 0x40),
                fnv1a(machine->memory + 0x7fb30, 8));
        fprintf(stderr,
                "main-entry hashes: all=%08x 1000=%08x 4000=%08x "
                "8000=%08x c000=%08x objects=%08x\n",
                fnv1a(machine->memory + 0x1000, 0xc000),
                fnv1a(machine->memory + 0x1000, 0x3000),
                fnv1a(machine->memory + 0x4000, 0x4000),
                fnv1a(machine->memory + 0x8000, 0x4000),
                fnv1a(machine->memory + 0xc000, 0x1000),
                fnv1a(machine->memory + 0x2e040, 0x480));
        fprintf(stderr, "low blocks: 1000=%08x 2000=%08x 3000=%08x\n",
                fnv1a(machine->memory + 0x1000, 0x1000),
                fnv1a(machine->memory + 0x2000, 0x1000),
                fnv1a(machine->memory + 0x3000, 0x1000));
        fprintf(stderr, "sprite copper: b300=%08x b330=%08x\n",
                fnv1a(machine->memory + 0xb300, 0xa00),
                fnv1a(machine->memory + 0xb330, 0x60));
        fprintf(stderr,
                "graphics blocks: 44000=%08x 5e000=%08x 62000=%08x\n",
                fnv1a(machine->memory + 0x44000, 0x1a000),
                fnv1a(machine->memory + 0x5e000, 0x4000),
                fnv1a(machine->memory + 0x62000, 0x12000));
        fprintf(stderr,
                "pre-draw screen planes: 62000=%08x 68000=%08x 6e000=%08x\n",
                pre_draw_62000, pre_draw_68000, pre_draw_6e000);
    }
    #undef CHECK
    if (!failed)
        printf("native recomp combat: PASS (%ld steps, pc=$%06x, files=%ld)\n",
               machine->translated_steps,
               machine->cpu.pc, machine->file_load_count);
    free(machine);
    return failed ? 1 : 0;
}
