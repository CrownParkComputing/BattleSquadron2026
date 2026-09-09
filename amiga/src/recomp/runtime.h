#ifndef BATTLE_SQUADRON_RECOMP_RUNTIME_H
#define BATTLE_SQUADRON_RECOMP_RUNTIME_H

#include "overlay.h"
#include "../platform/recomp_68k.h"

#include <stddef.h>
#include <stdint.h>

#define BS_RECOMP_MEMORY_SIZE 0x80000

typedef Recomp68kContext BsCpuContext;

typedef struct {
    uint32_t address;
    uint16_t length_words;
    uint16_t period;
    uint8_t volume;
    uint8_t channel;
} BsAudioSampleEvent;

typedef void (*BsCustomWriteHook)(void *user, uint16_t reg, uint16_t value);
typedef void (*BsAudioSampleHook)(void *user,
                                  const BsAudioSampleEvent *event);

typedef struct {
    BsCpuContext cpu;
    uint8_t memory[BS_RECOMP_MEMORY_SIZE];
    uint16_t custom[0x100];
    uint16_t dmacon, intena, intreq;
    uint8_t ciaa[16], ciab[16];
    BsModule modules[BS_MODULE_COUNT_MAX];
    size_t module_count;
    long file_load_count;
    long translated_steps;
    uint8_t input[2];
    int live_input_enabled;
    int external_playfield_restore;
    BsCustomWriteHook custom_write_hook;
    void *custom_write_user;
    /* The music sequencer is off until a caller asks for it, so the
     * step-exact oracle tests keep their existing register traces. */
    int music_enabled;
    BsAudioSampleHook audio_sample_hook;
    void *audio_sample_user;
    char data_directory[512];
    char error[256];
} BsRecomp;

enum {
    BS_RECOMP_ERROR = -1,
    BS_RECOMP_OK = 0,
    BS_RECOMP_UNTRANSLATED = 1,
};

int bs_recomp_init(BsRecomp *machine, const char *data_directory);
int bs_recomp_run(BsRecomp *machine, long max_steps);
void bs_recomp_enable_live_input(BsRecomp *machine, int enabled);
void bs_recomp_set_input(BsRecomp *machine, unsigned player, uint8_t state);
void bs_recomp_set_external_playfield_restore(BsRecomp *machine, int enabled);
int bs_recomp_start_new_game(BsRecomp *machine);
void bs_recomp_set_custom_write_hook(BsRecomp *machine,
                                     BsCustomWriteHook hook, void *user);
void bs_recomp_set_music_enabled(BsRecomp *machine, int enabled);
/* One CIA-B tick of the $24F34 sequencer.  It is interrupt driven on the
 * real machine, so a host must keep ticking it on frames where it is not
 * running the game loop -- the title screen otherwise sits silent. */
void bs_recomp_music_tick(BsRecomp *machine);
void bs_recomp_set_audio_sample_hook(BsRecomp *machine,
                                     BsAudioSampleHook hook, void *user);

enum {
    BS_INPUT_UP = 0x01,
    BS_INPUT_DOWN = 0x02,
    BS_INPUT_LEFT = 0x04,
    BS_INPUT_RIGHT = 0x08,
    BS_INPUT_FIRE = 0x10,
    BS_INPUT_NOVA = 0x20
};

uint8_t bs_recomp_read8(const BsRecomp *machine, uint32_t address);
uint16_t bs_recomp_read16(const BsRecomp *machine, uint32_t address);
uint32_t bs_recomp_read32(const BsRecomp *machine, uint32_t address);
void bs_recomp_write8(BsRecomp *machine, uint32_t address, uint8_t value);
void bs_recomp_write16(BsRecomp *machine, uint32_t address, uint16_t value);
void bs_recomp_write32(BsRecomp *machine, uint32_t address, uint32_t value);

#endif
