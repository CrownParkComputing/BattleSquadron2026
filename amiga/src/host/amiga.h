#ifndef BATTLE_SQUADRON_AMIGA_H
#define BATTLE_SQUADRON_AMIGA_H

#include <stdbool.h>
#include <stdint.h>

#define CHIP_SIZE 0x80000
/* A whole PAL raster wide, not just the textbook 320: Hybris opens a
 * 336-pixel window at hpos 120 for its title picture and its credit
 * scroller, and at 320 both lost eight pixels off each side. */
#define SCREEN_W 352
/* A whole PAL raster, not a textbook 256-line screen.  The buffer has to be
 * tall enough that every window a title opens can be CENTRED in it with room
 * above for sprites, which are not clipped to the display window: Hybris'
 * game window is 254 lines with sprites 12 lines above it, its title picture
 * is 200, and Battle Squadron's is 256. */
#define SCREEN_H 288
#define LINES_PER_FRAME 312
#define CYCLES_PER_LINE 455

/* Musashi's callback is function-like, which modern CMake deliberately does
 * not pass as a command-line definition.  This header is force-included for
 * every core translation unit, so keep the shared hook spelling here. */
#ifndef M68K_INSTRUCTION_CALLBACK
#define M68K_INSTRUCTION_CALLBACK(pc) bs_instr_hook(pc)
#endif

extern uint8_t chip[CHIP_SIZE];
extern uint32_t framebuf[SCREEN_W * SCREEN_H];
extern long bs_frame_no;
extern long bs_blit_count;
extern long bs_file_load_count;
extern long bs_copper_moves;
extern long bs_nonblack_pixels;
extern long bs_audio_writes;
extern uint8_t joy_state[2];
/* Battle Squadron's LOADER hooks fire on bare PC values; a title booted from
 * disk runs its own code there, so it clears this. */
extern bool bs_loader_hooks;
/* Lores-pixel nudge of the playfield relative to the sprites, for checking a
 * title's DDFSTRT/DIWSTRT pairing against the real machine.  0 is the
 * host's own derivation. */
extern int bs_playfield_shift;

/* A title-specific instruction hook: the host calls it with every PC, and
 * the title's own module decides what to intercept.  Per-title addresses
 * belong there, not in the chipset. */
typedef void (*BsPcHook)(unsigned int pc);
void amiga_set_pc_hook(BsPcHook hook);
void amiga_display_state(uint16_t *bplcon0, uint16_t *dmacon,
                         uint16_t *diwstrt, uint16_t *diwstop);
void amiga_display_bounds(int *first_row, int *last_row);
void amiga_palette(uint16_t *out);   /* 32 entries */

/* Replace-on-blit.  A title can claim a range of blit source addresses; when
 * the blitter draws from that range the original pixels are suppressed and a
 * request is recorded here instead, with the screen rectangle it would have
 * covered.  A frontend then draws whatever it likes there -- at any colour
 * depth or resolution, because nothing about that is the chipset's business
 * any more.  This is why the 32-colour limit is not a limit: it belongs to
 * the game's data, not to the renderer. */
typedef struct {
    int      x, y;          /* framebuffer position of the top-left */
    int      width, height; /* the size the original would have drawn */
    int      id;            /* whatever the title registered */
} BsSpriteDraw;

extern BsSpriteDraw bs_sprite_draws[64];
extern int bs_sprite_draw_count;

void amiga_register_replacement(uint32_t source_low, uint32_t source_high,
                                int id);
void amiga_clear_replacements(void);
void amiga_replacements_suppress(bool on);
void amiga_return_from_hook(void);

void amiga_init(const char *data_dir);
/* Reset the chipset without loading a title; the WHDLoad host supplies its
 * own entry vector afterwards. */
void amiga_init_bare(void);
void amiga_stop(void);
void amiga_run_frame(void);
void amiga_enable_video(bool enabled);
void amiga_key_event(uint8_t rawcode, bool up);
void amiga_audio_frame(void);
int amiga_audio_pull(int16_t *output, int frames);
int amiga_audio_fill(void);
void bs_instr_hook(unsigned int pc);
int amiga_blitter_selftest(void);
int amiga_video_selftest(void);
int amiga_input_selftest(void);
int amiga_audio_selftest(void);
bool amiga_stopped(void);
void amiga_report(void);

unsigned int m68k_read_memory_8(unsigned int address);
unsigned int m68k_read_memory_16(unsigned int address);
unsigned int m68k_read_memory_32(unsigned int address);
void m68k_write_memory_8(unsigned int address, unsigned int value);
void m68k_write_memory_16(unsigned int address, unsigned int value);
void m68k_write_memory_32(unsigned int address, unsigned int value);

#endif
