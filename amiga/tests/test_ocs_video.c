#include "ocs_video.h"
#include "ocs_palette.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint8_t memory[0x1000];

static uint8_t read_memory(void *user, uint32_t address)
{
    (void)user;
    return memory[address & 0x0fff];
}

static void write_word(uint32_t address, uint16_t value)
{
    memory[address & 0x0fff] = (uint8_t)(value >> 8);
    memory[(address + 1) & 0x0fff] = (uint8_t)value;
}

static void copper_move(uint32_t *cursor, uint16_t reg, uint16_t value)
{
    write_word(*cursor, reg);
    write_word(*cursor + 2, value);
    *cursor += 4;
}

int main(void)
{
    memset(memory, 0, sizeof memory);
    uint32_t copper = 0x100;
    copper_move(&copper, 0x00e0, 0x0000);
    copper_move(&copper, 0x00e2, 0x0200);
    copper_move(&copper, 0x0100, 0x1000);
    copper_move(&copper, 0x0180, 0x0000);
    copper_move(&copper, 0x0182, 0x0f00);
    copper_move(&copper, 0x0120, 0x0000);
    copper_move(&copper, 0x0122, 0x0400);
    copper_move(&copper, 0x01a2, 0x000f);
    write_word(copper, 0x2d01);       /* line 45 */
    write_word(copper + 2, 0xfffe);
    copper += 4;
    copper_move(&copper, 0x0182, 0x00f0);
    write_word(copper, 0xffff);
    write_word(copper + 2, 0xfffe);

    memory[0x200] = 0x80;
    memory[0x228] = 0x80;             /* next 40-byte scanline */
    write_word(0x400, 0x2c41);        /* line 44, hstart 131 */
    write_word(0x402, 0x2d01);        /* stop line 45 */
    write_word(0x404, 0x8000);
    write_word(0x406, 0x0000);
    write_word(0x408, 0x0000);
    write_word(0x40a, 0x0000);

    OcsVideo *video = ocs_video_create();
    OcsVideoSource source = {
        .read8 = read_memory,
        .chip_mask = 0x0fff,
        .copper_address = 0x100
    };
    const uint32_t *pixels = ocs_video_render(video, &source);
    uint16_t palette[32];
    int failed = 0;
    #define CHECK(condition, message) do { if (!(condition)) { \
        fprintf(stderr, "ocs video: %s\n", message); failed = 1; \
    } } while (0)
    CHECK(pixels != NULL, "render returned no image");
    CHECK(pixels[0] == UINT32_C(0xff0000ff),
          "first bitplane scanline did not use the pre-WAIT red palette");
    CHECK(pixels[OCS_VIDEO_WIDTH] == UINT32_C(0xff00ff00),
          "Copper WAIT did not change the second scanline to green");
    CHECK(pixels[2] == UINT32_C(0xffff0000),
          "hardware sprite pixel/palette was not composed");
    CHECK(pixels[1] == UINT32_C(0xff000000),
          "transparent playfield pixel changed");
    CHECK(ocs_palette_at_line(&source, 44, palette) >= 3 &&
          palette[1] == 0x0f00,
          "palette scan changed a colour before its Copper WAIT");
    CHECK(ocs_palette_at_line(&source, 45, palette) >= 4 &&
          palette[1] == 0x00f0 && palette[17] == 0x000f,
          "palette scan missed a Copper WAIT or sprite colour");

    /* Battle Squadron uses a 304-pixel nominal fetch inside a 320-pixel
     * display and relies on the fine-scroll carry at both edges.  Neither
     * edge may turn into a coarse 16-pixel black strip. */
    memset(memory, 0, sizeof memory);
    copper = 0x100;
    copper_move(&copper, 0x00e0, 0x0000);
    copper_move(&copper, 0x00e2, 0x0600);
    copper_move(&copper, 0x0094, 0x00c8);
    copper_move(&copper, 0x0100, 0x1000);
    copper_move(&copper, 0x0102, 0x000f);
    copper_move(&copper, 0x0180, 0x0000);
    copper_move(&copper, 0x0182, 0x0f00);
    write_word(copper, 0xffff);
    write_word(copper + 2, 0xfffe);
    memory[0x05fe] = 0x40; /* relative pixel -15 -> display pixel 0 */
    memory[0x0626] = 0x80; /* relative pixel 304 -> display pixel 319 */
    source.copper_address = 0x100;
    pixels = ocs_video_render(video, &source);
    CHECK(pixels[0] == UINT32_C(0xff0000ff),
          "fine-scroll carry was clipped at the left edge");
    CHECK(pixels[OCS_VIDEO_WIDTH - 1] == UINT32_C(0xff0000ff),
          "fine-scroll carry was clipped at the right edge");
    #undef CHECK
    ocs_video_destroy(video);
    if (!failed) puts("reusable OCS video: PASS (Copper, bitplane, sprite)");
    return failed;
}
