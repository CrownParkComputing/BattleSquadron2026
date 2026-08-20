/* render.h -- draw one Battle Squadron game frame from the engine state into an
 * RGBA canvas.  The visible window is 288 x 255 px (DIWSTRT $2690 / DIWSTOP
 * $25B0 = 288 wide, 255 lines) but the canvas is 288 x 256 for convenience;
 * row 255 is drawn from the same model (the original display just cuts it). */
#ifndef BS_RENDER_H
#define BS_RENDER_H
#include <stdint.h>
#include "bsdata.h"

#define BS_VIEW_W 288
#define BS_VIEW_H 256

enum {                                   /* layer mask for render_frame */
    BS_L_TERRAIN = 1,
    BS_L_BOBS    = 2,                    /* render_list: object + hostile bobs */
    BS_L_SPRITES = 4,                    /* effects, player shots, ships */
    BS_L_HUD     = 8,                    /* top status overlay (approximate) */
    BS_L_MSG     = 16,                   /* message overlay text (approximate) */
    BS_L_ALL     = 31
};

void render_stage(const BsData *d);      /* (re)latch palettes for g.stage7228 */
void render_frame(uint32_t *rgba, int layers);   /* BS_VIEW_W x BS_VIEW_H */
/* helpers reused by the viewer */
void render_text(uint32_t *rgba, int x, int y, const char *s, uint32_t colour);
/* one render-list bob, exactly as the game blits it (LAB_981C); pixels outside
 * the cookie mask are left untouched.  Needs <engine/engine.h> for RenderEntry. */
struct RenderEntry;
void render_bob(uint32_t *rgba, const struct RenderEntry *r);
const uint32_t *render_palette(void);    /* the 32 RGBA entries render_stage() latched */
extern int render_hiscore;               /* shown in the HUD HIGH column */

/* title screen: the LODINT picture (320 x 200 x 5 planes at $62000, palette
 * split measured from the live copper list -- see render.c) */
#define BS_TITLE_W 320
#define BS_TITLE_H 200
void render_title(uint32_t *rgba);       /* requires LODINT resident at $62000 */
void render_title_text(uint32_t *rgba, int x, int y, const char *s, uint32_t colour, int step);
#endif
