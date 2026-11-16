/*
 * Graphics from tools/gfx2c, end to end: the generated tiles, maps and
 * palettes are copied into the pseudo SNES (src/snes) as the game will do,
 * rendered, and every pixel is compared with the PNG it came from. So the
 * converter and the renderer are checked against the art itself.
 *
 *   test_gfx <assets/gfx folder>
 */

#include <stdio.h>
#include <string.h>

#include "common/pngread.h"
#include "gfx_data.h"
#include "snes/snes.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static u16 frame[SNES_W * SNES_H];
static char dir[1024];

#define BACKDROP RGB5(31, 0, 31)   /* magenta: transparent pixels show it */

static uint32_t rgb(u16 c)
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
            uint32_t got = rgb(frame[(sy + y) * SNES_W + sx + x]);
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

static void setup(void)
{
    snes_reset();
    setMode(BG_MODE1, BG3_MODE1_PRIORITY_HIGH);
    setPaletteColor(0, BACKDROP);
    setScreenOn();
}

static void map_entry(u16 map, u16 index, u16 entry)
{
    u8 b[2];
    b[0] = (u8)entry;
    b[1] = (u8)(entry >> 8);
    dmaCopyVram(b, (u16)(map + index), 2);
}

static void test_font(void)
{
    PngImage img;
    static const u16 text_pal[4] = { 0, RGB5(31, 31, 31), RGB5(0, 0, 0), 0 };
    int g;
    if (!load("font.png", &img)) {
        failures++;
        return;
    }
    setup();
    bgInitTileSet(2, gfx_font_tiles, (const u8 *)text_pal, 0, GFX_FONT_BYTES, sizeof text_pal, BG_4COLORS, 0x3000);
    setPaletteColor(0, BACKDROP);   /* the text palette's colour 0 is also the backdrop */
    bgSetMapPtr(2, 0x0800, SC_32x32);
    for (g = 0; g < 64; g++) {
        map_entry(0x0800, (u16)((g / 32) * 32 + g % 32), (u16)g);
    }
    snes_vblank();
    snes_render(frame);
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
        bgInitTileSet(0, gfx_tileset_tiles[t], (const u8 *)gfx_tileset_pal[t], 2, GFX_TILESET_TILES * 32, 32,
                      BG_16COLORS, 0x1000);
        bgSetMapPtr(0, 0x0000, SC_32x32);
        for (i = 0; i < GFX_TILESET_TILES; i++) {
            map_entry(0x0000, (u16)((i / 6) * 32 + i % 6), (u16)(i | BG_TIL_PAL(2)));
        }
        snes_vblank();
        snes_render(frame);
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
    dmaCopyVram(gfx_title_tiles, 0x1000, GFX_TITLE_TILES * 32);
    bgSetGfxPtr(0, 0x1000);
    dmaCopyCGram((const u8 *)gfx_title_pal, 2 * 16, GFX_TITLE_PALETTES * 32);
    bgSetMapPtr(0, 0x0000, SC_32x32);
    for (i = 0; i < GFX_TITLE_W * GFX_TITLE_H; i++) {
        map_entry(0x0000, (u16)((i / GFX_TITLE_W) * 32 + i % GFX_TITLE_W), gfx_title_map[i]);
    }
    snes_vblank();
    snes_render(frame);
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
            setup();
            oamInitGfxAttr(0x4000, OBJ_SIZE8_L16);
            oamInit();
            dmaCopyVram(gfx_sprites_shared, 0x4000, GFX_SPRITE_FIRST_ROW * 1024);
            dmaCopyVram(gfx_sheet_tiles[s], (u16)(0x4000 + GFX_SPRITE_FIRST_ROW * 512), gfx_sheet_bytes[s]);
            dmaCopyCGram((const u8 *)gfx_pal_player[0][GFX_TINT_NONE], 128 + 0 * 16, 32);
            dmaCopyCGram((const u8 *)gfx_pal_player[1][GFX_TINT_NONE], 128 + 1 * 16, 32);
            dmaCopyCGram((const u8 *)gfx_pal_effects[GFX_TINT_NONE], 128 + 2 * 16, 32);
            dmaCopyCGram((const u8 *)gfx_pal_items[GFX_TINT_NONE], 128 + 3 * 16, 32);
            dmaCopyCGram((const u8 *)gfx_pal_enemies[s][GFX_TINT_NONE], 128 + 4 * 16, 32);
            for (k = 0; k < n; k++) {
                int cell = cells[first + k];
                u8 slot = gfx_sheet_slot[s][cell];
                u16 name = (u16)((slot % 8) * 2 + (slot / 8) * 32);
                oamSet((u16)(k * 4), (u16)((k % 16) * 16), (u16)((k / 16) * 16 - 1), 0, 0, 0, name, gfx_cell_pal[cell]);
                oamSetEx((u16)(k * 4), OBJ_LARGE, OBJ_SHOW);
            }
            snes_vblank();
            snes_render(frame);
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
        u16 a = gfx_pal_player[0][GFX_TINT_NONE][i], b = gfx_pal_player[1][GFX_TINT_NONE][i];
        u16 r = gfx_pal_player[0][GFX_TINT_RED][i];
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
