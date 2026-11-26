/*
 * Graphics from tools/gfx2c, end to end: the generated tiles, maps and
 * palettes are copied into Rachael (the kit's SNES in C) as the game does,
 * rendered, and every pixel is compared with the PNG it came from. So the
 * converter and the renderer are checked against the art itself.
 *
 *   test_gfx <assets/gfx folder>
 */

#include <stdio.h>
#include <string.h>

#include "common/pngread.h"
#include "gfx_data.h"
#include "rachael.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint16_t frame[SDEVKIT__W * SDEVKIT__H];
static char dir[1024];

#define BACKDROP SDEVKIT_RGB5(31, 0, 31)   /* magenta: transparent pixels show it */

static uint32_t rgb(uint16_t c)
{
    uint32_t r = c & 31, g = (c >> 5) & 31, b = (c >> 10) & 31;
    return ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
}

static int load(const char *file, PngImage *img)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    return png_load("test_gfx", path, img);
}

/* Compares a w x h area of the picture at (sx, sy) with the image at (ix, iy).
 * Returns the number of differing pixels (reports the first). */
static int compare(const PngImage *img, int ix, int iy, int sx, int sy, int w, int h, const char *what)
{
    int x, y, bad = 0;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            uint32_t want = img->px[(iy + y) * img->w + ix + x];
            uint32_t got = rgb(frame[(sy + y) * SDEVKIT__W + sx + x]);
            if (want != got) {
                if (!bad) {
                    printf("  %s: pixel %d,%d is %06lX, the PNG has %06lX\n", what, x, y, (unsigned long)got,
                           (unsigned long)want);
                }
                bad++;
            }
        }
    }
    return bad;
}

/* The screen stays off while the test loads (Rachael starts with it off): no DMA limit, and
 * registers may be written at any time. show() turns it on. */
static void setup(void)
{
    sdevkit__reset();
    sdevkit_reg_write(SDEVKIT_PPU_BGMODE, SDEVKIT_BG_MODE1 | SDEVKIT_BG3_MODE1_PRIORITY_HIGH);
    sdevkit_reg_write(SDEVKIT_PPU_TM, SDEVKIT_BG1_ENABLE | SDEVKIT_BG2_ENABLE | SDEVKIT_BG3_ENABLE | SDEVKIT_OBJ_ENABLE);
    sdevkit_cgram_set(0, BACKDROP);
    /* Row y of the picture is scanline y + 1, as on the SNES: scrolled up by one line, the
     * picture starts with row 0 of each map, and a sprite at y is on row y. */
    sdevkit_reg_write_twice(SDEVKIT_PPU_BG1VOFS, 0x3FF);
    sdevkit_reg_write_twice(SDEVKIT_PPU_BG2VOFS, 0x3FF);
    sdevkit_reg_write_twice(SDEVKIT_PPU_BG3VOFS, 0x3FF);
}

static void show(void)
{
    sdevkit__vblank();
    sdevkit_reg_write(SDEVKIT_PPU_INIDISP, 15);
    sdevkit__render(frame);
}

static void map_entry(uint16_t map, uint16_t index, uint16_t entry)
{
    uint8_t b[2];
    b[0] = (uint8_t)entry;
    b[1] = (uint8_t)(entry >> 8);
    sdevkit_vram_copy((uint16_t)(map + index), b, 2);
}

static void test_font(void)
{
    PngImage img;
    static const uint16_t text_pal[4] = { 0, SDEVKIT_RGB5(31, 31, 31), SDEVKIT_RGB5(0, 0, 0), 0 };
    int g;
    if (!load("font.png", &img)) {
        failures++;
        return;
    }
    setup();
    sdevkit_vram_copy(0x3000, gfx_font_tiles, GFX_FONT_BYTES);
    sdevkit_cgram_copy(0, text_pal, sizeof text_pal);
    sdevkit_reg_write(SDEVKIT_PPU_BG34NBA, SDEVKIT_BGNBA(0x3000, 0));
    sdevkit_cgram_set(0, BACKDROP);   /* the text palette's colour 0 is also the backdrop */
    sdevkit_reg_write(SDEVKIT_PPU_BG3SC, SDEVKIT_BGSC(0x0800, SDEVKIT_SC_32x32));
    for (g = 0; g < 64; g++) {
        map_entry(0x0800, (uint16_t)((g / 32) * 32 + g % 32), (uint16_t)g);
    }
    show();
    CHECK(compare(&img, 0, 0, 0, 0, 256, 16, "font") == 0, "the font differs from font.png");
    png_free(&img);
}

static void test_tilesets(void)
{
    int t, i;
    for (t = 0; t < GFX_TILESETS; t++) {
        PngImage img;
        char name[32];
        snprintf(name, sizeof name, "tileset%02d.png", t + 1);
        if (!load(name, &img)) {
            failures++;
            continue;
        }
        setup();
        sdevkit_vram_copy(0x1000, gfx_tileset_tiles[t], GFX_TILESET_TILES * 32);
        sdevkit_cgram_copy(2 * 16, gfx_tileset_pal[t], 32);
        sdevkit_reg_write(SDEVKIT_PPU_BG12NBA, SDEVKIT_BGNBA(0x1000, 0));
        sdevkit_reg_write(SDEVKIT_PPU_BG1SC, SDEVKIT_BGSC(0x0000, SDEVKIT_SC_32x32));
        for (i = 0; i < GFX_TILESET_TILES; i++) {
            map_entry(0x0000, (uint16_t)((i / 6) * 32 + i % 6), (uint16_t)(i | SDEVKIT_BG_TIL_PAL(2)));
        }
        show();
        CHECK(compare(&img, 0, 0, 0, 0, 48, 24, name) == 0, "%s differs", name);
        png_free(&img);
    }
}

static void test_title(void)
{
    PngImage img;
    int i;
    if (!load("title.png", &img)) {
        failures++;
        return;
    }
    setup();
    sdevkit_vram_copy(0x1000, gfx_title_tiles, GFX_TITLE_TILES * 32);
    sdevkit_reg_write(SDEVKIT_PPU_BG12NBA, SDEVKIT_BGNBA(0x1000, 0));
    sdevkit_cgram_copy(2 * 16, gfx_title_pal, GFX_TITLE_PALETTES * 32);
    sdevkit_reg_write(SDEVKIT_PPU_BG1SC, SDEVKIT_BGSC(0x0000, SDEVKIT_SC_32x32));
    for (i = 0; i < GFX_TITLE_W * GFX_TITLE_H; i++) {
        map_entry(0x0000, (uint16_t)((i / GFX_TITLE_W) * 32 + i % GFX_TITLE_W), gfx_title_map[i]);
    }
    show();
    CHECK(compare(&img, 0, 0, 0, 0, GFX_TITLE_W * 8, GFX_TITLE_H * 8, "title") == 0, "the title differs from title.png");
    png_free(&img);
}

/* Every cell of every sheet, drawn as a sprite with its palette, 16 x 7
 * cells per picture. */
static void test_sprite_sheets(void)
{
    PngImage img;
    int s;
    if (!load("sprites.png", &img)) {
        failures++;
        return;
    }
    for (s = 0; s < GFX_SHEETS; s++) {
        int cells[256], count = 0, c, first, bad = 0;
        for (c = 0; c < 256; c++) {
            if (gfx_sheet_slot[s][c] != 0xFF) cells[count++] = c;
        }
        for (first = 0; first < count; first += 112) {
            int k, n = count - first < 112 ? count - first : 112;
            static uint8_t oam[SDEVKIT_OAM_BYTES];
            setup();
            sdevkit_reg_write(SDEVKIT_PPU_OBSEL, SDEVKIT_OBSEL(0x4000, SDEVKIT_OBJ_SIZE8_L16));
            sdevkit_vram_copy(0x4000, gfx_sprites_shared, GFX_SPRITE_FIRST_ROW * 1024);
            sdevkit_vram_copy((uint16_t)(0x4000 + GFX_SPRITE_FIRST_ROW * 512), gfx_sheet_tiles[s], gfx_sheet_bytes[s]);
            sdevkit_cgram_copy(128 + 0 * 16, gfx_pal_player[0][GFX_TINT_NONE], 32);
            sdevkit_cgram_copy(128 + 1 * 16, gfx_pal_player[1][GFX_TINT_NONE], 32);
            sdevkit_cgram_copy(128 + 2 * 16, gfx_pal_effects[GFX_TINT_NONE], 32);
            sdevkit_cgram_copy(128 + 3 * 16, gfx_pal_items[GFX_TINT_NONE], 32);
            sdevkit_cgram_copy(128 + 4 * 16, gfx_pal_enemies[s][GFX_TINT_NONE], 32);
            memset(oam, 0, sizeof oam);
            for (k = 0; k < 128; k++) {
                oam[k * 4 + 1] = SDEVKIT_OAM_HIDE_Y;
            }
            for (k = 0; k < n; k++) {
                int cell = cells[first + k];
                uint8_t slot = gfx_sheet_slot[s][cell];
                uint16_t name = (uint16_t)((slot % 8) * 2 + (slot / 8) * 32);
                oam[k * 4] = (uint8_t)((k % 16) * 16);
                oam[k * 4 + 1] = (uint8_t)((k / 16) * 16);
                oam[k * 4 + 2] = (uint8_t)name;
                oam[k * 4 + 3] = (uint8_t)(SDEVKIT_OAM_ATTR(gfx_cell_pal[cell], 0, 0, 0) | (name >> 8));
                oam[512 + k / 4] |= (uint8_t)(2 << (2 * (k % 4)));   /* large, x below 256 */
            }
            sdevkit_oam_copy(0, oam, sizeof oam);
            show();
            for (k = 0; k < n; k++) {
                int cell = cells[first + k];
                if (compare(&img, (cell % 16) * 16, (cell / 16) * 16, (k % 16) * 16, (k / 16) * 16, 16, 16, "cell")) {
                    printf("  sheet %d, cell %d\n", s, cell);
                    bad++;
                }
            }
        }
        CHECK(bad == 0, "sheet %d: %d of %d cells differ from sprites.png", s, bad, count);
    }
    png_free(&img);
}

/* Player 2 is player 1 with four colours swapped for blue ones; the tinted
 * copies scale each channel by the tint (red: red stays, green and blue drop). */
static void test_palette_copies(void)
{
    int i, swapped = 0;
    for (i = 1; i < 16; i++) {
        uint16_t a = gfx_pal_player[0][GFX_TINT_NONE][i], b = gfx_pal_player[1][GFX_TINT_NONE][i];
        uint16_t r = gfx_pal_player[0][GFX_TINT_RED][i];
        if (a != b) {
            swapped++;
            CHECK(((b >> 10) & 31) > ((a >> 10) & 31), "player 2's colour %d should be bluer", i);
        }
        CHECK((r & 31) == (a & 31) && ((r >> 5) & 31) == ((a >> 5) & 31) * 0x50 / 255 &&
              ((r >> 10) & 31) == ((a >> 10) & 31) * 0x50 / 255, "red copy of player colour %d", i);
    }
    CHECK(swapped == 4, "player 2 swaps 4 colours, swapped %d", swapped);
}

/* Cells with exactly the same pixels are stored once, and both numbers
 * lead to that one slot: the score pop-up halves, the boss's top half
 * (mouth open or shut), a dash glow outline. */
static void test_copies_merged(void)
{
    static const int copies[][2] = { { 83, 67 }, { 85, 67 }, { 75, 69 }, { 81, 77 }, { 199, 196 } };
    int s, i;
    for (s = 0; s < GFX_SHEETS; s++) {
        for (i = 0; i < (int)(sizeof copies / sizeof copies[0]); i++) {
            CHECK(gfx_sheet_slot[s][copies[i][0]] != 0xFF && gfx_sheet_slot[s][copies[i][0]] == gfx_sheet_slot[s][copies[i][1]],
                  "sheet %d: cell %d should share the slot of cell %d", s, copies[i][0], copies[i][1]);
        }
    }
    CHECK(gfx_sheet_slot[GFX_SHEET_BOSS][180] != 0xFF && gfx_sheet_slot[GFX_SHEET_BOSS][180] == gfx_sheet_slot[GFX_SHEET_BOSS][176],
          "the boss's top half is stored once");
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        printf("usage: test_gfx <assets/gfx folder>\n");
        return 2;
    }
    snprintf(dir, sizeof dir, "%s", argv[1]);
    test_font();
    test_tilesets();
    test_title();
    test_sprite_sheets();
    test_palette_copies();
    test_copies_merged();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
