#ifndef BATTLE_SQUADRON_HYBRIS_LOADER_H
#define BATTLE_SQUADRON_HYBRIS_LOADER_H

#include <stdbool.h>

/* Serve the cracked loader's track reads from the extracted files in
 * `directory` (which must hold disk-map.txt and data/).  No disk image is
 * involved at runtime. */
bool hybris_loader_install(const char *directory);

/* Where remastered art lives.  Android unpacks assets at the APK root, so the
 * path differs by platform. */
void hybris_set_sprite_folder(const char *folder);

extern long hybris_load_count;
extern long hybris_load_bytes;
extern long hybris_action_key;
extern long hybris_action_pot;

/* Remastered artwork found in assets/sprites.  A file named after the blit
 * source it replaces -- 018620.png, or 018620-0188a0.png for an explicit
 * range -- is registered automatically, so adding art needs no code and no
 * manifest.  The frontend loads these paths and draws them by id. */
typedef struct {
    int  id;
    char path[256];
} HybrisSpriteArt;

extern HybrisSpriteArt hybris_sprite_art[32];
extern int hybris_sprite_art_count;

#endif
