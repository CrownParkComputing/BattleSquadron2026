/* audio.h -- native Battle Squadron sound: the LODGAM sequencer (music + SFX)
 * ported from the parity-pinned translation in
 * ~/BattleSquadron-Amiga/src/recomp/runtime.c, driving a 4-channel Paula
 * model (port of src/platform/paula_audio.c) that reads samples straight from
 * the chip image.  All state lives in bs_chip at the original addresses
 * ($251F8 driver, $252A4.. channels, $25504 instruments, $2539C sfx table),
 * so the original's own song data just plays. */
#ifndef BS_AUDIO_H
#define BS_AUDIO_H

int  audio_init(void);              /* raylib device + stream; 0 = ok (no-ops if no device) */
void audio_close(void);
void audio_start_game(void);        /* LAB_A42: channel init + select track 1 (in-game music) */
void audio_start_title(void);       /* $3D800 entry: LODMUS menu engine, plays the armed menu tune
                                       (requires LODMUS resident at $3D800; 74.90 Hz tick) */
void audio_title_speech(void);      /* boot "welcome" speech: whole LODSPE at $246F0 on channel 0 */
void audio_track(int track);        /* SelectMusic 1..10 (1 game, 2 restart, 3 game over,
                                       4 stage clear, 5 death, 8 extra life, 9 bonus, 10 nova) */
void audio_stop(void);              /* $246F6 shutdown: DMA off, channels silent */
void audio_tick(void);              /* call once per 50 Hz display frame: CIA cadence + stream feed */
void audio_sfx(int n);              /* the engine's sfx(n): n >= 0 -> PlaySoundEffect;
                                       n < 0 -> the $2470E-table jingle entries (engine passes -0x732 etc) */
void audio_set(float master, int music_on, int sfx_on);
#endif
