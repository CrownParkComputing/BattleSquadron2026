#include "engine.h"
#include "bsdata.h"
#include "render.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static BsData data;
static uint32_t original[BS_VIEW_W * BS_VIEW_H], enhanced[BS_VIEW_W * BS_VIEW_H], again[BS_VIEW_W * BS_VIEW_H];
static int interval(int weapon, int upgrade, int demo, int player) {
    eng_init(0, 2, weapon, 3, 1);
    g.demo = demo;
    Player *p = &g.players[player];
    p->state38 = 0; p->level60 = upgrade;
    reload_weapon(p);
    p->joy44 = JOY_FIRE;
    int first = -1;
    for (int frame = 0; frame < 128; frame++) {
        memset(p->shots, 0, sizeof p->shots); /* measure cadence without pool saturation */
        int before = g.stat_shots[player];
        player_fire(p);
        if (g.stat_shots[player] != before) {
            if (first >= 0) return frame - first;
            first = frame;
        }
    }
    assert(!"no volley"); return 0;
}
static void save(const char *name, uint32_t *pixels) {
    FILE *f = fopen(name, "wb"); assert(f);
    fprintf(f,"P6\n%d %d\n255\n",BS_VIEW_W,BS_VIEW_H);
    for(int i=0;i<BS_VIEW_W*BS_VIEW_H;i++) fwrite(&pixels[i],1,3,f);
    fclose(f);
}
int main(void) {
    assert(bs_open(&data,"amiga/original/whdload/BattleSquadron/data") == 0);
    bs_chip = data.chip;
    assert(bs_load_stage(&data,0) == 0);
    for(int p=0;p<2;p++) for(int w=0;w<4;w++) for(int u=0;u<6;u++) {
        int old=interval(w,u,1,p), fast=interval(w,u,0,p);
        if (old != fast*2) fprintf(stderr,"weapon %d upgrade %d original %d fast %d\n",w,u,old,fast);
        assert(old == fast*2);
    }
    eng_init(0,1,3,3,1);
    for(int i=0;i<450;i++) {
        g.players[0].invuln52=300;
        uint8_t joy[2]={JOY_FIRE,0}; eng_frame(joy);
    }
    g.players[0].state38 = 0; g.players[0].explode49 = 0;
    g.players[0].x = 0x190; g.players[0].y = 0x1A0;
    /* Include a visible explosion and intermediate terrain frame. */
    g_790A[0][0]=g.cam7204+110; g_790A[0][1]=0x100+110; g_790A[0][2]=5;
    g.dframe |= 1;
    BsGame before=g;
    render_enhanced=0; render_frame(original,BS_L_ALL);
    render_enhanced=1; render_frame(enhanced,BS_L_ALL);
    render_frame(again,BS_L_ALL);
    assert(memcmp(original,enhanced,sizeof original));
    assert(!memcmp(enhanced,again,sizeof enhanced));
    assert(!memcmp(&before,&g,sizeof g));
    save("build/graphics-original.ppm",original);
    save("build/graphics-enhanced.ppm",enhanced);
    render_enhanced=0; render_frame(again,BS_L_ALL);
    assert(!memcmp(original,again,sizeof original));
    g.demo=1;
    render_frame(original,BS_L_ALL); render_enhanced=1; render_frame(enhanced,BS_L_ALL);
    assert(!memcmp(original,enhanced,sizeof original));
    puts("PASS: 2x autofire, all weapons/upgrades/players; graphics reversible, deterministic, state unchanged, demo unchanged");
}
