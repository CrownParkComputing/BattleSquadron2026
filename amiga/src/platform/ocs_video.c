#include "ocs_video.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

struct OcsVideo {
    OcsVideoSource source;
    uint32_t pixels[OCS_VIDEO_WIDTH * OCS_VIDEO_HEIGHT];
    uint32_t bplpt[6], sprpt[8], cop_pc;
    uint16_t color[32];
    uint16_t bplcon0, bplcon1, bplcon2;
    uint16_t diwstrt, diwstop, ddfstrt, ddfstop;
    int16_t bpl1mod, bpl2mod;
    int cop_wait_line;
};

static uint8_t rb(const OcsVideo *video, uint32_t address)
{
    return video->source.read8(video->source.user,
                               address & video->source.chip_mask);
}

static uint16_t rw(const OcsVideo *video, uint32_t address)
{
    return ((uint16_t)rb(video, address) << 8) | rb(video, address + 1);
}

static uint32_t rgb4(uint16_t value)
{
    uint32_t red = (value >> 8) & 15;
    uint32_t green = (value >> 4) & 15;
    uint32_t blue = value & 15;
    return UINT32_C(0xff000000) | (blue * 17 << 16) |
           (green * 17 << 8) | red * 17;
}

static void write_register(OcsVideo *video, uint16_t reg, uint16_t value)
{
    switch (reg) {
    case 0x08e: video->diwstrt = value; break;
    case 0x090: video->diwstop = value; break;
    case 0x092: video->ddfstrt = value; break;
    case 0x094: video->ddfstop = value; break;
    case 0x100: video->bplcon0 = value; break;
    case 0x102: video->bplcon1 = value; break;
    case 0x104: video->bplcon2 = value; break;
    case 0x108: video->bpl1mod = (int16_t)value; break;
    case 0x10a: video->bpl2mod = (int16_t)value; break;
    default:
        if (reg >= 0x0e0 && reg < 0x0f8) {
            unsigned plane = (reg - 0x0e0) / 4;
            if (reg & 2)
                video->bplpt[plane] =
                    (video->bplpt[plane] & UINT32_C(0xffff0000)) |
                    (value & 0xfffe);
            else
                video->bplpt[plane] =
                    (video->bplpt[plane] & UINT32_C(0x0000ffff)) |
                    ((uint32_t)value << 16);
        } else if (reg >= 0x120 && reg < 0x140) {
            unsigned sprite = (reg - 0x120) / 4;
            if (reg & 2)
                video->sprpt[sprite] =
                    (video->sprpt[sprite] & UINT32_C(0xffff0000)) |
                    (value & 0xfffe);
            else
                video->sprpt[sprite] =
                    (video->sprpt[sprite] & UINT32_C(0x0000ffff)) |
                    ((uint32_t)value << 16);
        } else if (reg >= 0x180 && reg < 0x1c0) {
            video->color[(reg - 0x180) / 2] = value & 0x0fff;
        }
        break;
    }
}

static void run_copper_line(OcsVideo *video, int line)
{
    if (video->cop_wait_line < 0 || line < video->cop_wait_line) return;
    for (int guard = 0; guard < 4096; guard++) {
        uint16_t first = rw(video, video->cop_pc);
        uint16_t second = rw(video, video->cop_pc + 2);
        if (!(first & 1)) {
            video->cop_pc = (video->cop_pc + 4) &
                            video->source.chip_mask;
            write_register(video, first & 0x01fe, second);
        } else if (!(second & 1)) {
            if (first == 0xffff && second == 0xfffe) {
                video->cop_wait_line = -1;
                return;
            }
            int target = (first >> 8) & 0xff;
            int mask = ((second >> 8) & 0x7f) | 0x80;
            if (((line & 0xff) & mask) >= (target & mask)) {
                video->cop_pc = (video->cop_pc + 4) &
                                video->source.chip_mask;
            } else {
                video->cop_wait_line = (line & 0x100) | target;
                if (video->cop_wait_line <= line)
                    video->cop_wait_line += 0x100;
                return;
            }
        } else {
            video->cop_pc = (video->cop_pc + 4) &
                            video->source.chip_mask;
        }
    }
    video->cop_wait_line = -1;
}

static int display_stop(const OcsVideo *video)
{
    int start = (video->diwstrt >> 8) & 0xff;
    int stop = (video->diwstop >> 8) & 0xff;
    if (stop <= start) stop += 0x100;
    return stop;
}

static int fetch_bytes(const OcsVideo *video)
{
    int start = video->ddfstrt & 0xfc;
    int stop = video->ddfstop & 0xfc;
    int words = stop >= start ? ((stop - start) >> 3) + 1 : 20;
    if (words < 1) words = 1;
    if (words > 25) words = 25;
    return words * 2;
}

/* Return a bit relative to the current DMA pointer.  Fine scrolling delays
 * the fetched stream, so the visible display can consume the carry on either
 * side of the nominal DDF span.  Treating DDFSTRT/DDFSTOP as a hard clipping
 * rectangle makes that carry black and exposes a 16-pixel strip whenever the
 * coarse pointer rolls over.  The adjacent words are present in the planar
 * row (and are exactly what the Amiga shifter consumes). */
static int plane_bit(const OcsVideo *video, uint32_t pointer, int source_x)
{
    int byte = source_x >= 0 ? source_x / 8 : -((-source_x + 7) / 8);
    int bit_in_byte = source_x - byte * 8;
    return (rb(video, pointer + (uint32_t)byte) >>
            (7 - bit_in_byte)) & 1;
}

static void render_bitplane_line(OcsVideo *video, int line)
{
    int vstart = (video->diwstrt >> 8) & 0xff;
    int y = line - vstart;
    if (line < vstart || line >= display_stop(video) ||
        y < 0 || y >= OCS_VIDEO_HEIGHT)
        return;
    uint32_t *output = video->pixels + y * OCS_VIDEO_WIDTH;
    int depth = (video->bplcon0 >> 12) & 7;
    if (depth > 6) depth = 6;
    int fetch_lead = (0x38 - (video->ddfstrt & 0xfc)) * 2;
    /* Same correction as the Musashi host: the playfield is positioned from
     * the DDF fetch start while sprites are positioned from DIWSTRT, so they
     * only agree under the textbook pairing DIWSTRT_H = DDFSTRT*2 + 17.
     * Battle Squadron sets DIWSTRT_H $90 against DDFSTRT $38, biasing the
     * window 15 lores pixels, and its display window is 288 wide -- drawing
     * fetched data across all 320 left content stranded on the right. */
    int window_start = video->diwstrt & 0xff;
    int window_stop = (int)(video->diwstop & 0xff) | 0x100;
    int diw_bias = window_start - ((int)(video->ddfstrt & 0xfc) * 2 + 17);
    int visible = window_stop - window_start;
    if (visible < 0) visible = 0;
    if (visible > OCS_VIDEO_WIDTH) visible = OCS_VIDEO_WIDTH;
    bool dual = (video->bplcon0 & 0x0400) != 0;
    bool pf2_priority = (video->bplcon2 & 0x0040) != 0;
    for (int x = 0; x < OCS_VIDEO_WIDTH; x++) {
        if (x >= visible) {
            output[x] = rgb4(video->color[0]);
            continue;
        }
        int index = 0;
        if (!dual) {
            int source_x = x + fetch_lead + diw_bias -
                           (video->bplcon1 & 15);
            for (int plane = 0; plane < depth; plane++)
                index |= plane_bit(video, video->bplpt[plane], source_x)
                         << plane;
        } else {
            int first = 0, second = 0;
            for (int plane = 0; plane < depth; plane++) {
                int scroll = (plane & 1) ? ((video->bplcon1 >> 4) & 15)
                                         : (video->bplcon1 & 15);
                int source_x = x + fetch_lead + diw_bias - scroll;
                int value = plane_bit(video, video->bplpt[plane], source_x);
                if (plane & 1) second |= value << (plane >> 1);
                else first |= value << (plane >> 1);
            }
            if (pf2_priority)
                index = second ? 8 + second : first;
            else
                index = first ? first : (second ? 8 + second : 0);
        }
        output[x] = rgb4(video->color[index & 31]);
    }
    for (int plane = 0; plane < depth; plane++)
        video->bplpt[plane] += fetch_bytes(video) +
            ((plane & 1) ? video->bpl2mod : video->bpl1mod);
}

typedef struct {
    bool active, attached;
    int hstart;
    uint16_t low, high;
} SpriteLine;

static SpriteLine read_sprite_line(const OcsVideo *video,
                                   int number, int line)
{
    SpriteLine result = {0};
    uint32_t pointer = video->sprpt[number] & video->source.chip_mask;
    if (!pointer) return result;
    for (int guard = 0; guard < 64; guard++) {
        uint16_t pos = rw(video, pointer);
        uint16_t ctl = rw(video, pointer + 2);
        if (!pos && !ctl) break;
        int vstart = (pos >> 8) | ((ctl & 4) << 6);
        int vstop = (ctl >> 8) | ((ctl & 2) << 7);
        int rows = vstop - vstart;
        if (rows <= 0 || rows > 300) break;
        if (line >= vstart && line < vstop) {
            uint32_t data = pointer + 4 + (uint32_t)(line - vstart) * 4;
            result.active = true;
            result.attached = (number & 1) && (ctl & 0x0080);
            result.hstart = ((pos & 0xff) << 1) | (ctl & 1);
            result.low = rw(video, data);
            result.high = rw(video, data + 2);
            return result;
        }
        pointer = (pointer + 4 + (uint32_t)rows * 4) &
                  video->source.chip_mask;
    }
    return result;
}

static void render_sprites_line(OcsVideo *video, int line)
{
    int y = line - ((video->diwstrt >> 8) & 0xff);
    if (y < 0 || y >= OCS_VIDEO_HEIGHT) return;
    /* Same DIWSTOP clip as the Musashi host: sprite-drawn text otherwise
     * piles up in the right-hand border. */
    int sprite_visible =
        ((int)(video->diwstop & 0xff) | 0x100) - (video->diwstrt & 0xff);
    if (sprite_visible < 0) sprite_visible = 0;
    if (sprite_visible > OCS_VIDEO_WIDTH) sprite_visible = OCS_VIDEO_WIDTH;
    for (int pair = 3; pair >= 0; pair--) {
        uint8_t pixels[2][OCS_VIDEO_WIDTH] = {{0}};
        SpriteLine lines[2] = {
            read_sprite_line(video, pair * 2, line),
            read_sprite_line(video, pair * 2 + 1, line)
        };
        for (int which = 0; which < 2; which++) {
            if (!lines[which].active) continue;
            for (int bit = 0; bit < 16; bit++) {
                int x = lines[which].hstart -
                        (video->diwstrt & 0xff) + bit;
                if (x < 0 || x >= OCS_VIDEO_WIDTH) continue;
                pixels[which][x] =
                    ((lines[which].low >> (15 - bit)) & 1) |
                    (((lines[which].high >> (15 - bit)) & 1) << 1);
            }
        }
        int bank = 16 + pair * 4;
        for (int x = 0; x < sprite_visible; x++) {
            if (lines[1].attached) {
                int index = pixels[0][x] | (pixels[1][x] << 2);
                if (index)
                    video->pixels[y * OCS_VIDEO_WIDTH + x] =
                        rgb4(video->color[16 + index]);
            } else {
                int index = pixels[1][x];
                if (index)
                    video->pixels[y * OCS_VIDEO_WIDTH + x] =
                        rgb4(video->color[bank + index]);
                index = pixels[0][x];
                if (index)
                    video->pixels[y * OCS_VIDEO_WIDTH + x] =
                        rgb4(video->color[bank + index]);
            }
        }
    }
}

OcsVideo *ocs_video_create(void)
{
    return calloc(1, sizeof(OcsVideo));
}

void ocs_video_destroy(OcsVideo *video)
{
    free(video);
}

const uint32_t *ocs_video_render(OcsVideo *video,
                                 const OcsVideoSource *source)
{
    if (!video || !source || !source->read8) return NULL;
    memset(video, 0, sizeof *video);
    video->source = *source;
    video->diwstrt = 0x2c81;
    video->diwstop = 0x2cc1;
    video->ddfstrt = 0x0038;
    video->ddfstop = 0x00d0;
    video->cop_pc = source->copper_address & source->chip_mask;
    video->cop_wait_line = video->cop_pc ? 0 : -1;
    for (int line = 0; line < 312; line++) {
        run_copper_line(video, line);
        render_bitplane_line(video, line);
        render_sprites_line(video, line);
    }
    return video->pixels;
}
