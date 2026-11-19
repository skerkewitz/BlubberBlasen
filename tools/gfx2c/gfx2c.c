/*
 * gfx2c - converts the graphics in assets/gfx into SNES data (ADR-0004,
 * ADR-0006): 4 bpp and 2 bpp planar tiles, 15-bit palettes, tile maps and
 * one sprite sheet per room group, as const C arrays the game copies into
 * VRAM and CGRAM.
 *
 * Usage: gfx2c -o <gfx_data.c> -H <gfx_data.h> <gfx.txt> <level.map>...
 *
 * The settings file (assets/gfx/gfx.txt) says what each PNG is. The level
 * files tell which enemy types each sheet's rounds use.
 *
 * The tool converts, it does not repair: art that breaks a SNES limit
 * fails the build with the place and the reason:
 *   - a colour off the 5-bit grid (each channel must be v5 << 3 | v5 >> 2)
 *   - more than 15 colours in a palette (tileset, sprite palette, a tile of
 *     the title; the title's tiles must fit its palettes)
 *   - a sprite sheet with more than 128 cells of 16 x 16
 *   - an asset larger than 32 KB, the size of a ROM bank
 * It prints a summary of what it made.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/mapfile.h"
#include "common/pngread.h"

#define TOOL "gfx2c"
#define MAX_COLORS 15
#define CELLS 256
#define SHEET_CELLS 128
#define BANK 32768
#define MAX_SHEETS 16
#define MAX_TILESETS 8
#define TILESET_TILES 18           /* 6 x 3 tiles of 8 x 8 */
#define ENEMY_TYPES 8
#define NUM_TINTS 4
#define MAX_LEVELS 128

/* --- Settings ------------------------------------------------------------------ */
static char dir[1024];
static char font_file[256], title_file[256], sprites_file[256];
static uint32_t font_colors[3];
static int font_color_count;
static char tileset_files[MAX_TILESETS][256];
static int tileset_count;
static int title_palettes;

typedef struct {
    char    name[32];
    int     number;            /* sprite palette 0..7 */
    uint8_t cells[CELLS];      /* in order */
    int     count;
} PaletteClass;
static PaletteClass classes[8];
static int class_count;
static int enemies_class = -1;

static uint8_t enemy_cells[ENEMY_TYPES + 1][32];
static int enemy_count[ENEMY_TYPES + 1];
static uint8_t boss_cells[32];
static int boss_count;

static uint32_t p2_from[16], p2_to[16];
static int p2_count;

static const char *tint_names[NUM_TINTS] = { "red", "grey", "yellow", "orange" };
static uint32_t tints[NUM_TINTS];
static int tint_set[NUM_TINTS];

typedef struct {
    char    name[32];          /* for the generated comments */
    int     first, last;       /* rounds, 1-based; 0 = not a round sheet */
    int     boss;
    uint8_t enemy_types[ENEMY_TYPES + 1];   /* 1 = holds that type */
    int     explicit_enemies;
    int     last_slot;         /* slots used */
} Sheet;
static Sheet sheets[MAX_SHEETS];
static int sheet_count;

static MapFile *levels;
static int num_levels;

static int errors;

static void error_at(const char *file, int line, const char *msg)
{
    fprintf(stderr, "%s: %s:%d: %s\n", TOOL, file, line, msg);
    errors++;
}

/* --- Colours ------------------------------------------------------------------- */
static int on_grid(uint32_t c)
{
    int s;
    for (s = 0; s < 24; s += 8) {
        unsigned v = (c >> s) & 0xFF;
        if (v != (((v >> 3) << 3) | (v >> 5))) {
            return 0;
        }
    }
    return 1;
}

static uint32_t canon(uint32_t c)
{
    uint32_t out = 0;
    int s;
    for (s = 0; s < 24; s += 8) {
        unsigned v = (c >> s) & 0xFF;
        out |= (uint32_t)(((v >> 3) << 3) | (v >> 5)) << s;
    }
    return out;
}

static uint16_t bgr15(uint32_t c)
{
    return (uint16_t)(((c >> 19) & 31) | (((c >> 11) & 31) << 5) | (((c >> 3) & 31) << 10));
}

static int luma(uint32_t c)
{
    return (int)(((c >> 16) & 0xFF) * 3 + ((c >> 8) & 0xFF) * 6 + (c & 0xFF));
}

/* A palette: up to 15 colours, index 0 is transparent. */
typedef struct {
    uint32_t c[MAX_COLORS + 1];
    int      n;
} Palette;

static int pal_find(const Palette *p, uint32_t c)
{
    int i;
    for (i = 0; i < p->n; i++) {
        if (p->c[i] == c) {
            return i + 1;
        }
    }
    return 0;
}

/* Adds c; returns 0 when the palette is full. */
static int pal_add(Palette *p, uint32_t c)
{
    if (pal_find(p, c)) {
        return 1;
    }
    if (p->n == MAX_COLORS) {
        return 0;
    }
    p->c[p->n++] = c;
    return 1;
}

static int by_luma(const void *a, const void *b)
{
    return luma(*(const uint32_t *)a) - luma(*(const uint32_t *)b);
}

static void pal_sort(Palette *p)
{
    qsort(p->c, (size_t)p->n, sizeof p->c[0], by_luma);
}

/* 16 BGR 5:5:5 colours; index 0 = 0 */
static void pal_words(const Palette *p, uint16_t out[16])
{
    int i;
    memset(out, 0, 16 * sizeof out[0]);
    for (i = 0; i < p->n; i++) {
        out[i + 1] = bgr15(p->c[i]);
    }
}

static uint16_t tint15(uint16_t c, uint32_t t)
{
    unsigned r = (c & 31) * ((t >> 16) & 0xFF) / 255, g = ((c >> 5) & 31) * ((t >> 8) & 0xFF) / 255,
             b = ((c >> 10) & 31) * (t & 0xFF) / 255;
    return (uint16_t)(r | (g << 5) | (b << 10));
}

/* --- Images -------------------------------------------------------------------- */
static int load(const char *file, PngImage *img)
{
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", dir, file);
    if (!png_load(TOOL, path, img)) {
        errors++;
        return 0;
    }
    return 1;
}

static uint32_t px(const PngImage *img, int x, int y)
{
    return img->px[(size_t)y * (size_t)img->w + (size_t)x];
}

/* Every colour on the 5-bit grid; reports the first offender per image. */
static int check_grid(const char *file, const PngImage *img)
{
    int x, y;
    for (y = 0; y < img->h; y++) {
        for (x = 0; x < img->w; x++) {
            uint32_t c = px(img, x, y);
            if (c != PNG_TRANSPARENT && !on_grid(c)) {
                fprintf(stderr, "%s: %s: pixel %d,%d: colour %06lX is not a SNES colour (nearest: %06lX)\n", TOOL, file, x,
                        y, (unsigned long)c, (unsigned long)canon(c));
                errors++;
                return 0;
            }
        }
    }
    return 1;
}

/* An 8 x 8 tile as colour indices (0 = transparent). Returns 0 if a colour
 * is not in the palette. */
static int tile_indices(const PngImage *img, int tx, int ty, const Palette *p, uint8_t idx[64])
{
    int x, y;
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            uint32_t c = px(img, tx + x, ty + y);
            int i = c == PNG_TRANSPARENT ? 0 : pal_find(p, c);
            if (c != PNG_TRANSPARENT && !i) {
                return 0;
            }
            idx[y * 8 + x] = (uint8_t)i;
        }
    }
    return 1;
}

/* SNES planar tile: 4 bpp = 32 bytes, 2 bpp = 16 bytes. */
static void planar(const uint8_t idx[64], int bpp, uint8_t *out)
{
    int x, y;
    memset(out, 0, (size_t)(bpp * 8));
    for (y = 0; y < 8; y++) {
        for (x = 0; x < 8; x++) {
            uint8_t c = idx[y * 8 + x], bit = (uint8_t)(0x80 >> x);
            if (c & 1) out[y * 2] |= bit;
            if (c & 2) out[y * 2 + 1] |= bit;
            if (bpp == 4) {
                if (c & 4) out[16 + y * 2] |= bit;
                if (c & 8) out[16 + y * 2 + 1] |= bit;
            }
        }
    }
}

/* --- Output ------------------------------------------------------------------------ */
static FILE *out_c, *out_h, *out_asm;   /* out_asm set: the SNES build, arrays go there */
static size_t arr_count;

/* One array, either as C (PC build) or as a ROM section in assembly (SNES
 * build, --snes): 816-tcc would copy global const data into RAM, and a
 * section must fit a 32 KB bank (check_bank). The label is the C name. */
static void arr_begin(const char *c_decl, const char *label)
{
    arr_count = 0;
    if (out_asm) {
        fprintf(out_asm, ".SECTION \".%s\" SUPERFREE\n%s:", label, label);
    } else {
        fprintf(out_c, "%s = {", c_decl);
    }
}

static void arr_byte(unsigned v)
{
    if (out_asm) {
        fprintf(out_asm, "%s$%02X", arr_count % 16 ? "," : "\n.db ", v & 0xFFu);
    } else {
        fprintf(out_c, "%s0x%02X,", arr_count % 16 ? " " : "\n    ", v & 0xFFu);
    }
    arr_count++;
}

static void arr_word(unsigned v)
{
    if (out_asm) {
        fprintf(out_asm, "%s$%04X", arr_count % 8 ? "," : "\n.dw ", v & 0xFFFFu);
    } else {
        fprintf(out_c, "%s0x%04X,", arr_count % 8 ? " " : "\n    ", v & 0xFFFFu);
    }
    arr_count++;
}

static void arr_bytes(const uint8_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) arr_byte(d[i]);
}

static void arr_words(const uint16_t *d, size_t n)
{
    size_t i;
    for (i = 0; i < n; i++) arr_word(d[i]);
}

/* Inner braces of a multi-dimensional array (C only). */
static void arr_sub_begin(void)
{
    if (!out_asm) {
        fprintf(out_c, "\n    {");
        arr_count = 0;
    }
}

static void arr_sub_end(void)
{
    if (!out_asm) {
        fprintf(out_c, "\n    },");
    }
}

static void arr_end(void)
{
    if (out_asm) {
        fprintf(out_asm, "\n.ENDS\n\n");
    } else {
        fprintf(out_c, "\n};\n\n");
    }
}

static void check_bank(const char *what, size_t bytes)
{
    if (bytes > BANK) {
        fprintf(stderr, "%s: %s: %lu bytes, more than a ROM bank (%d)\n", TOOL, what, (unsigned long)bytes, BANK);
        errors++;
    }
}

/* --- Font: 2 bpp, 64 glyphs (ASCII 32..95), 32 per row ------------------------------------ */
static void convert_font(void)
{
    PngImage img;
    Palette p;
    static uint8_t data[64 * 16];
    int g;
    if (!load(font_file, &img)) {
        return;
    }
    memset(&p, 0, sizeof p);
    for (g = 0; g < font_color_count; g++) {
        pal_add(&p, canon(font_colors[g]));
    }
    if (img.w < 256 || img.h < 16) {
        error_at(font_file, 0, "the font must be 256 x 16 (64 glyphs)");
    } else if (check_grid(font_file, &img)) {
        for (g = 0; g < 64; g++) {
            uint8_t idx[64];
            if (!tile_indices(&img, (g % 32) * 8, (g / 32) * 8, &p, idx)) {
                fprintf(stderr, "%s: %s: glyph %d ('%c') uses a colour that is not on the font line of gfx.txt\n", TOOL,
                        font_file, g, 32 + g);
                errors++;
                break;
            }
            planar(idx, 2, data + g * 16);
        }
    }
    png_free(&img);
    fprintf(out_h, "/* Font: 2 bpp tiles for ASCII 32..95 (tile = code - 32); colours 1..3 as on the font line. */\n");
    fprintf(out_h, "#define GFX_FONT_BYTES %d\nextern const uint8_t gfx_font_tiles[GFX_FONT_BYTES];\n\n", (int)sizeof data);
    arr_begin("const uint8_t gfx_font_tiles[GFX_FONT_BYTES]", "gfx_font_tiles");
    arr_bytes(data, sizeof data);
    arr_end();
    printf("%s: font: 64 glyphs, %d bytes\n", TOOL, (int)sizeof data);
}

/* --- Tilesets: 18 tiles of 4 bpp and one palette each ---------------------------------------- */
static void convert_tilesets(void)
{
    static uint8_t data[MAX_TILESETS][TILESET_TILES * 32];
    uint16_t pals[MAX_TILESETS][16];
    int t, i;
    memset(pals, 0, sizeof pals);
    for (t = 0; t < tileset_count; t++) {
        PngImage img;
        Palette p;
        int x, y, ok = 1;
        if (!load(tileset_files[t], &img)) {
            continue;
        }
        memset(&p, 0, sizeof p);
        if (img.w != 48 || img.h != 24) {
            error_at(tileset_files[t], 0, "a tileset must be 48 x 24 (6 x 3 tiles)");
            ok = 0;
        }
        ok = ok && check_grid(tileset_files[t], &img);
        for (y = 0; ok && y < img.h; y++) {
            for (x = 0; x < img.w; x++) {
                uint32_t c = px(&img, x, y);
                if (c != PNG_TRANSPARENT && !pal_add(&p, c)) {
                    fprintf(stderr, "%s: %s: more than %d colours\n", TOOL, tileset_files[t], MAX_COLORS);
                    errors++;
                    ok = 0;
                    break;
                }
            }
        }
        if (ok) {
            pal_sort(&p);
            for (i = 0; i < TILESET_TILES; i++) {
                uint8_t idx[64];
                tile_indices(&img, (i % 6) * 8, (i / 6) * 8, &p, idx);
                planar(idx, 4, data[t] + i * 32);
            }
            pal_words(&p, pals[t]);
            printf("%s: %s: %d colours\n", TOOL, tileset_files[t], p.n);
        }
        png_free(&img);
    }
    fprintf(out_h, "/* Room tilesets: %d tiles of 4 bpp each (a 6 x 3 grid), with one palette. */\n", TILESET_TILES);
    fprintf(out_h, "#define GFX_TILESETS %d\n#define GFX_TILESET_TILES %d\n", tileset_count, TILESET_TILES);
    fprintf(out_h, "extern const uint8_t  gfx_tileset_tiles[GFX_TILESETS][GFX_TILESET_TILES * 32];\n");
    fprintf(out_h, "extern const uint16_t gfx_tileset_pal[GFX_TILESETS][16];\n\n");
    arr_begin("const uint8_t gfx_tileset_tiles[GFX_TILESETS][GFX_TILESET_TILES * 32]", "gfx_tileset_tiles");
    for (t = 0; t < tileset_count; t++) {
        arr_sub_begin();
        arr_bytes(data[t], TILESET_TILES * 32);
        arr_sub_end();
    }
    arr_end();
    arr_begin("const uint16_t gfx_tileset_pal[GFX_TILESETS][16]", "gfx_tileset_pal");
    for (t = 0; t < tileset_count; t++) {
        arr_sub_begin();
        arr_words(pals[t], 16);
        arr_sub_end();
    }
    arr_end();
    (void)i;
}

/* --- Title: tiles share up to title_palettes palettes, identical tiles once ------------------ */
static void convert_title(void)
{
    PngImage img;
    Palette pals[8];
    int tw, th, t, i, used = 0, tiles = 0, ok = 1;
    static uint8_t data[32 * 32 * 32];
    static uint16_t map[32 * 32];
    static uint8_t tile_of[32 * 32];
    static int tile_pal[32 * 32];
    typedef struct { uint32_t c[MAX_COLORS]; int n; int t; } TileColors;
    static TileColors tc[32 * 32];
    static uint8_t seen[32 * 32 * 64];
    int order[32 * 32];

    if (!load(title_file, &img)) {
        return;
    }
    memset(pals, 0, sizeof pals);
    tw = img.w / 8;
    th = img.h / 8;
    if (img.w % 8 || img.h % 8 || tw > 32 || th > 32) {
        error_at(title_file, 0, "the title must be a multiple of 8 and at most 256 x 256");
        ok = 0;
    }
    ok = ok && check_grid(title_file, &img);

    /* each tile's colours */
    for (t = 0; ok && t < tw * th; t++) {
        int x, y;
        tc[t].n = 0;
        tc[t].t = t;
        for (y = 0; y < 8 && ok; y++) {
            for (x = 0; x < 8; x++) {
                uint32_t c = px(&img, (t % tw) * 8 + x, (t / tw) * 8 + y);
                int k, have = 0;
                if (c == PNG_TRANSPARENT) continue;
                for (k = 0; k < tc[t].n; k++) have |= tc[t].c[k] == c;
                if (have) continue;
                if (tc[t].n == MAX_COLORS) {
                    fprintf(stderr, "%s: %s: tile %d,%d has more than %d colours\n", TOOL, title_file, t % tw, t / tw,
                            MAX_COLORS);
                    errors++;
                    ok = 0;
                    break;
                }
                tc[t].c[tc[t].n++] = c;
            }
        }
        order[t] = t;
    }
    /* palettes: tiles with the most colours first; each goes into the palette
     * that already has most of its colours and still has room (best fit) */
    for (i = 0; ok && i < tw * th; i++) {
        int j, best = 0;
        for (j = i + 1; j < tw * th; j++) {
            if (tc[order[j]].n > tc[order[best + i]].n) best = j - i;
        }
        t = order[i + best];
        order[i + best] = order[i];
        order[i] = t;
    }
    for (i = 0; ok && i < tw * th; i++) {
        const TileColors *c = &tc[order[i]];
        int p, best = -1, best_new = 99;
        for (p = 0; p < title_palettes; p++) {
            int k, missing = 0;
            for (k = 0; k < c->n; k++) missing += !pal_find(&pals[p], c->c[k]);
            if (pals[p].n + missing <= MAX_COLORS && missing < best_new && (p < used || missing < best_new)) {
                best = p;
                best_new = missing;
            }
        }
        if (best < 0) {
            fprintf(stderr, "%s: %s: the tiles do not fit into %d palettes of %d colours (stuck at tile %d,%d)\n", TOOL,
                    title_file, title_palettes, MAX_COLORS, c->t % tw, c->t / tw);
            errors++;
            ok = 0;
            break;
        }
        for (t = 0; t < c->n; t++) pal_add(&pals[best], c->c[t]);
        if (best >= used) used = best + 1;
        tile_pal[c->t] = best;
    }
    /* tiles, identical ones once (palette included) */
    for (t = 0; ok && t < tw * th; t++) {
        uint8_t idx[64], plan[32];
        int k, found = -1;
        tile_indices(&img, (t % tw) * 8, (t / tw) * 8, &pals[tile_pal[t]], idx);
        for (k = 0; k < tiles; k++) {
            if (memcmp(seen + k * 64, idx, 64) == 0 && tile_of[k] == tile_pal[t]) {
                found = k;
                break;
            }
        }
        if (found < 0) {
            found = tiles++;
            memcpy(seen + found * 64, idx, 64);
            tile_of[found] = (uint8_t)tile_pal[t];
            planar(idx, 4, plan);
            memcpy(data + found * 32, plan, 32);
        }
        map[t] = (uint16_t)(found | ((2 + tile_pal[t]) << 10));   /* BG palettes 2.. */
    }
    png_free(&img);
    check_bank("title tiles", (size_t)tiles * 32);

    fprintf(out_h, "/* Title logo: 4 bpp tiles, a %d x %d map (tile numbers from 0, BG palettes 2..), its palettes. */\n",
            tw, th);
    fprintf(out_h, "#define GFX_TITLE_W %d\n#define GFX_TITLE_H %d\n#define GFX_TITLE_TILES %d\n#define GFX_TITLE_PALETTES %d\n",
            tw, th, tiles, title_palettes);
    fprintf(out_h, "extern const uint8_t  gfx_title_tiles[GFX_TITLE_TILES * 32];\n");
    fprintf(out_h, "extern const uint16_t gfx_title_map[GFX_TITLE_W * GFX_TITLE_H];\n");
    fprintf(out_h, "extern const uint16_t gfx_title_pal[GFX_TITLE_PALETTES][16];\n\n");
    arr_begin("const uint8_t gfx_title_tiles[GFX_TITLE_TILES * 32]", "gfx_title_tiles");
    arr_bytes(data, (size_t)tiles * 32);
    arr_end();
    arr_begin("const uint16_t gfx_title_map[GFX_TITLE_W * GFX_TITLE_H]", "gfx_title_map");
    arr_words(map, (size_t)(tw * th));
    arr_end();
    arr_begin("const uint16_t gfx_title_pal[GFX_TITLE_PALETTES][16]", "gfx_title_pal");
    for (i = 0; i < title_palettes; i++) {
        uint16_t w[16];
        pal_words(&pals[i], w);
        arr_sub_begin();
        arr_words(w, 16);
        arr_sub_end();
    }
    arr_end();
    if (ok) {
        printf("%s: %s: %d of %d tiles after merging copies, %d palettes used\n", TOOL, title_file, tiles, tw * th, used);
    }
}

/* --- Sprites ------------------------------------------------------------------------------- */
static PngImage sheet_img;
static int cell_class[CELLS];          /* palette class of each cell, -1 = none */
static int cell_same[CELLS];           /* the first cell with exactly the same pixels */

static int cell_used(int c)
{
    int x, y;
    for (y = 0; y < 16; y++)
        for (x = 0; x < 16; x++)
            if (px(&sheet_img, (c % 16) * 16 + x, (c / 16) * 16 + y) != PNG_TRANSPARENT) return 1;
    return 0;
}

static int cells_equal(int a, int b)
{
    int y;
    for (y = 0; y < 16; y++) {
        if (memcmp(&sheet_img.px[(size_t)((a / 16) * 16 + y) * (size_t)sheet_img.w + (size_t)((a % 16) * 16)],
                   &sheet_img.px[(size_t)((b / 16) * 16 + y) * (size_t)sheet_img.w + (size_t)((b % 16) * 16)],
                   16 * sizeof(uint32_t)) != 0) {
            return 0;
        }
    }
    return 1;
}

/* Adds a cell's colours; returns 0 when the palette overflows. */
static int add_cell_colors(Palette *p, int c)
{
    int x, y;
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            uint32_t col = px(&sheet_img, (c % 16) * 16 + x, (c / 16) * 16 + y);
            if (col != PNG_TRANSPARENT && !pal_add(p, col)) {
                return 0;
            }
        }
    }
    return 1;
}

/* The cell into a VRAM image at a slot: 8 slots per row of 16 x 2 tiles. */
static void put_cell(uint8_t *vram, int slot, int c, const Palette *p)
{
    int q;
    for (q = 0; q < 4; q++) {
        uint8_t idx[64];
        int name = (slot % 8) * 2 + (slot / 8) * 32 + (q & 1) + (q >> 1) * 16;
        tile_indices(&sheet_img, (c % 16) * 16 + (q & 1) * 8, (c / 16) * 16 + (q >> 1) * 8, p, idx);
        planar(idx, 4, vram + name * 32);
    }
}

/* A palette as drawn and its tinted copies (GFX_TINTS x 16 words). */
static void emit_palette_set(const Palette *base, int swap)
{
    uint16_t w[16];
    int t, i;
    pal_words(base, w);
    if (swap) {
        for (i = 0; i < base->n; i++) {
            int k;
            for (k = 0; k < p2_count; k++) {
                if (base->c[i] == canon(p2_from[k])) w[i + 1] = bgr15(canon(p2_to[k]));
            }
        }
    }
    arr_sub_begin();
    arr_words(w, 16);
    arr_sub_end();
    for (t = 0; t < NUM_TINTS; t++) {
        uint16_t v[16];
        for (i = 0; i < 16; i++) v[i] = tint15(w[i], tints[t]);
        arr_sub_begin();
        arr_words(v, 16);
        arr_sub_end();
    }
}

static void convert_sprites(void)
{
    static uint8_t vram[MAX_SHEETS][SHEET_CELLS * 128];
    static uint8_t slot_of[MAX_SHEETS][CELLS];
    Palette fixed[8], enemy_pal[MAX_SHEETS];
    int c, k, s, i, shared_count = 0, first_row, ok = 1, player_class = -1;
    uint8_t shared_slot[CELLS];

    if (!load(sprites_file, &sheet_img)) {
        return;
    }
    if (sheet_img.w != 256 || sheet_img.h != 256) {
        error_at(sprites_file, 0, "the sprite sheet must be 256 x 256 (16 x 16 cells)");
        return;
    }
    if (!check_grid(sprites_file, &sheet_img)) {
        return;
    }
    memset(fixed, 0, sizeof fixed);
    memset(enemy_pal, 0, sizeof enemy_pal);
    memset(slot_of, 0xFF, sizeof slot_of);
    memset(shared_slot, 0xFF, sizeof shared_slot);
    for (c = 0; c < CELLS; c++) {
        cell_class[c] = -1;
        cell_same[c] = c;
    }
    for (k = 0; k < class_count; k++) {
        for (i = 0; i < classes[k].count; i++) cell_class[classes[k].cells[i]] = k;
        if (classes[k].number == 0) player_class = k;
    }
    for (k = 1; k <= ENEMY_TYPES; k++) {
        for (i = 0; i < enemy_count[k]; i++) cell_class[enemy_cells[k][i]] = enemies_class;
    }
    for (i = 0; i < boss_count; i++) cell_class[boss_cells[i]] = enemies_class;
    for (c = 0; c < CELLS; c++) {
        int d;
        if (cell_class[c] < 0) continue;
        if (!cell_used(c)) {
            fprintf(stderr, "%s: %s: cell %d is listed in gfx.txt but empty\n", TOOL, sprites_file, c);
            errors++;
            ok = 0;
        }
        for (d = 0; d < c; d++) {
            if (cell_class[d] == cell_class[c] && cells_equal(c, d)) {
                cell_same[c] = cell_same[d];
                break;
            }
        }
    }
    if (!ok || player_class < 0) {
        if (player_class < 0) error_at("gfx.txt", 0, "no sprite palette 0 (player 1)");
        return;
    }

    /* the fixed palettes: player, effects, items */
    for (k = 0; k < class_count; k++) {
        if (k == enemies_class) continue;
        for (i = 0; i < classes[k].count; i++) {
            if (!add_cell_colors(&fixed[k], classes[k].cells[i])) {
                fprintf(stderr, "%s: %s: palette '%s' needs more than %d colours (at cell %d)\n", TOOL, sprites_file,
                        classes[k].name, MAX_COLORS, classes[k].cells[i]);
                errors++;
                return;
            }
        }
        pal_sort(&fixed[k]);
    }
    for (i = 0; i < p2_count; i++) {
        if (!pal_find(&fixed[player_class], canon(p2_from[i]))) {
            fprintf(stderr, "%s: gfx.txt: p2: player 1 has no colour %06lX\n", TOOL, (unsigned long)p2_from[i]);
            errors++;
        }
    }

    /* the shared slots: cells of the fixed palettes, in the order of the
     * palette lines, copies once. They are the same in every sheet. */
    for (k = 0; k < class_count; k++) {
        if (k == enemies_class) continue;
        for (i = 0; i < classes[k].count; i++) {
            c = classes[k].cells[i];
            if (cell_same[c] == c) shared_slot[c] = (uint8_t)shared_count++;
        }
    }
    if (shared_count > SHEET_CELLS) {
        fprintf(stderr, "%s: the shared sprites alone need more than %d cells of 16 x 16 (%d)\n", TOOL, SHEET_CELLS,
                shared_count);
        errors++;
        return;
    }
    first_row = shared_count / 8;   /* from this row of 8 cells on, each sheet brings its own */

    /* each sheet: the shared cells, then the enemies palette's cells: the
     * ones every sheet holds, the sheet's enemy types, the boss */
    for (s = 0; s < sheet_count; s++) {
        Sheet *sh = &sheets[s];
        uint8_t list[CELLS];
        int n = 0, count = shared_count, t;
        for (i = 0; i < classes[enemies_class].count; i++) list[n++] = classes[enemies_class].cells[i];
        for (t = 1; t <= ENEMY_TYPES; t++) {
            if (!sh->enemy_types[t]) continue;
            for (i = 0; i < enemy_count[t]; i++) list[n++] = enemy_cells[t][i];
        }
        if (sh->boss) {
            for (i = 0; i < boss_count; i++) list[n++] = boss_cells[i];
        }
        for (i = 0; i < n; i++) {
            if (!add_cell_colors(&enemy_pal[s], list[i])) {
                fprintf(stderr, "%s: %s: sheet '%s': the enemies palette needs more than %d colours (at cell %d)\n", TOOL,
                        sprites_file, sh->name, MAX_COLORS, list[i]);
                errors++;
                return;
            }
        }
        pal_sort(&enemy_pal[s]);
        memcpy(slot_of[s], shared_slot, CELLS);
        memset(vram[s], 0, sizeof vram[s]);
        for (c = 0; c < CELLS; c++) {
            if (shared_slot[c] != 0xFF) put_cell(vram[s], shared_slot[c], c, &fixed[cell_class[c]]);
        }
        for (i = 0; i < n; i++) {
            c = cell_same[list[i]];
            if (slot_of[s][c] != 0xFF) continue;
            if (count == SHEET_CELLS) {
                fprintf(stderr, "%s: sheet '%s' needs more than %d cells of 16 x 16\n", TOOL, sh->name, SHEET_CELLS);
                errors++;
                return;
            }
            slot_of[s][c] = (uint8_t)count;
            put_cell(vram[s], count, c, &enemy_pal[s]);
            count++;
        }
        for (c = 0; c < CELLS; c++) {
            if (cell_class[c] >= 0) slot_of[s][c] = slot_of[s][cell_same[c]];
        }
        sh->last_slot = count;
        printf("%s: sheet %-8s %3d of %d cells, enemies palette %2d colours\n", TOOL, sh->name, count, SHEET_CELLS,
               enemy_pal[s].n);
    }
    printf("%s: shared sprites: %d cells (copies merged), fixed palettes:", TOOL, shared_count);
    for (k = 0; k < class_count; k++) {
        if (k != enemies_class) printf(" %s %d", classes[k].name, fixed[k].n);
    }
    printf(" colours\n");

    /* --- output --- */
    fprintf(out_h,
            "/* Sprites: sheets of 16 x 16 cells in VRAM order, 8 cells per row of 16 x 2 tiles\n"
            " * (slot s = tile s %% 8 * 2 + s / 8 * 32). Rows 0 .. GFX_SPRITE_FIRST_ROW - 1 are the\n"
            " * same in every sheet (gfx_sprites_shared, 1 KB per row); a sheet brings the rows\n"
            " * from GFX_SPRITE_FIRST_ROW on (gfx_sheet_tiles). gfx_sheet_slot maps a cell of\n"
            " * sprites.png (the SPR_* numbers) to its slot in a sheet, 0xFF = not in it. */\n");
    fprintf(out_h, "#define GFX_SPRITE_FIRST_ROW %d\n#define GFX_SHEETS %d\n", first_row, sheet_count);
    for (s = 0; s < sheet_count; s++) {
        if (sheets[s].boss) fprintf(out_h, "#define GFX_SHEET_BOSS %d\n", s);
        if (!sheets[s].first) fprintf(out_h, "#define GFX_SHEET_SCREENS %d\n", s);
    }
    fprintf(out_h, "extern const uint8_t  gfx_sprites_shared[GFX_SPRITE_FIRST_ROW * 1024];\n");
    fprintf(out_h, "extern const uint8_t *const gfx_sheet_tiles[GFX_SHEETS];\n");
    fprintf(out_h, "extern const uint16_t gfx_sheet_bytes[GFX_SHEETS];\n");
    fprintf(out_h, "extern const uint8_t  gfx_sheet_slot[GFX_SHEETS][256];\n");
    fprintf(out_h, "extern const uint8_t  gfx_cell_pal[256];   /* sprite palette of each cell */\n");
    fprintf(out_h, "#define GFX_LEVELS %d\n", num_levels);
    fprintf(out_h, "extern const uint8_t  gfx_level_sheet[GFX_LEVELS];  /* sheet of each level (round - 1), 0xFF = keep the last */\n");
    fprintf(out_h, "/* Palettes: as drawn, then tinted copies (what the PC did by multiplying colours). */\n");
    fprintf(out_h, "enum { GFX_TINT_NONE, GFX_TINT_RED, GFX_TINT_GREY, GFX_TINT_YELLOW, GFX_TINT_ORANGE, GFX_TINTS };\n");
    fprintf(out_h, "extern const uint16_t gfx_pal_player[2][GFX_TINTS][16];   /* player 1, player 2 */\n");
    for (k = 0; k < class_count; k++) {
        if (k != enemies_class && k != player_class) {
            fprintf(out_h, "extern const uint16_t gfx_pal_%s[GFX_TINTS][16];\n", classes[k].name);
        }
    }
    fprintf(out_h, "extern const uint16_t gfx_pal_enemies[GFX_SHEETS][GFX_TINTS][16];\n\n");

    check_bank("shared sprites", (size_t)first_row * 1024);
    arr_begin("const uint8_t gfx_sprites_shared[GFX_SPRITE_FIRST_ROW * 1024]", "gfx_sprites_shared");
    arr_bytes(vram[0], (size_t)first_row * 1024);
    arr_end();
    for (s = 0; s < sheet_count; s++) {
        char decl[64], label[32];
        int rows = (sheets[s].last_slot + 7) / 8 - first_row;
        size_t bytes = (size_t)rows * 1024;
        check_bank(sheets[s].name, bytes);
        snprintf(decl, sizeof decl, "static const uint8_t gfx_sheet%d[%d]", s, (int)bytes);
        snprintf(label, sizeof label, "gfx_sheet%d", s);
        arr_begin(decl, label);
        arr_bytes(vram[s] + first_row * 1024, bytes);
        arr_end();
    }
    /* the list of sheets has pointers: always C, laid out by the compiler */
    for (s = 0; s < sheet_count && out_asm; s++) fprintf(out_c, "extern const uint8_t gfx_sheet%d[];\n", s);
    fprintf(out_c, "const uint8_t *const gfx_sheet_tiles[GFX_SHEETS] = {");
    for (s = 0; s < sheet_count; s++) fprintf(out_c, " gfx_sheet%d%s", s, s + 1 < sheet_count ? "," : "");
    fprintf(out_c, " };\n\n");
    arr_begin("const uint16_t gfx_sheet_bytes[GFX_SHEETS]", "gfx_sheet_bytes");
    for (s = 0; s < sheet_count; s++) arr_word((unsigned)(((sheets[s].last_slot + 7) / 8 - first_row) * 1024));
    arr_end();
    arr_begin("const uint8_t gfx_sheet_slot[GFX_SHEETS][256]", "gfx_sheet_slot");
    for (s = 0; s < sheet_count; s++) {
        arr_sub_begin();
        arr_bytes(slot_of[s], CELLS);
        arr_sub_end();
    }
    arr_end();
    arr_begin("const uint8_t gfx_cell_pal[256]", "gfx_cell_pal");
    for (c = 0; c < CELLS; c++) arr_byte(cell_class[c] >= 0 ? (unsigned)classes[cell_class[c]].number : 0u);
    arr_end();
    arr_begin("const uint8_t gfx_level_sheet[GFX_LEVELS]", "gfx_level_sheet");
    for (i = 0; i < num_levels; i++) {
        int found = 0xFF;
        for (s = 0; s < sheet_count; s++) {
            if (sheets[s].first && i + 1 >= sheets[s].first && i + 1 <= sheets[s].last) found = s;
        }
        arr_byte((unsigned)found);
    }
    arr_end();
    arr_begin("const uint16_t gfx_pal_player[2][GFX_TINTS][16]", "gfx_pal_player");
    arr_sub_begin();
    emit_palette_set(&fixed[player_class], 0);
    arr_sub_end();
    arr_sub_begin();
    emit_palette_set(&fixed[player_class], 1);
    arr_sub_end();
    arr_end();
    for (k = 0; k < class_count; k++) {
        char decl[64], label[48];
        if (k == enemies_class || k == player_class) continue;
        snprintf(decl, sizeof decl, "const uint16_t gfx_pal_%s[GFX_TINTS][16]", classes[k].name);
        snprintf(label, sizeof label, "gfx_pal_%s", classes[k].name);
        arr_begin(decl, label);
        emit_palette_set(&fixed[k], 0);
        arr_end();
    }
    arr_begin("const uint16_t gfx_pal_enemies[GFX_SHEETS][GFX_TINTS][16]", "gfx_pal_enemies");
    for (s = 0; s < sheet_count; s++) {
        arr_sub_begin();
        emit_palette_set(&enemy_pal[s], 0);
        arr_sub_end();
    }
    arr_end();
    png_free(&sheet_img);
}

/* --- Settings file ------------------------------------------------------------------------- */
/* "12" or "12-20" into first..last; returns 0 on bad syntax. */
static int range(const char *s, int *first, int *last)
{
    char *end;
    long a = strtol(s, &end, 10), b = a;
    if (end == s) return 0;
    if (*end == '-') {
        const char *t = end + 1;
        b = strtol(t, &end, 10);
        if (end == t) return 0;
    }
    if (*end || a < 0 || b < a) return 0;
    *first = (int)a;
    *last = (int)b;
    return 1;
}

static int add_cells(const char *file, int line, char *tok, uint8_t *cells, int *count, int max)
{
    for (; tok; tok = strtok(NULL, " \t\r\n")) {
        int a, b, c;
        if (!range(tok, &a, &b) || b >= CELLS) {
            error_at(file, line, "bad cell number or range");
            return 0;
        }
        for (c = a; c <= b; c++) {
            if (*count == max) {
                error_at(file, line, "too many cells");
                return 0;
            }
            cells[(*count)++] = (uint8_t)c;
        }
    }
    return 1;
}

static void read_settings(const char *path)
{
    FILE *f = fopen(path, "r");
    char buf[1024];
    int line = 0;
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) {
        snprintf(dir, sizeof dir, "%.*s", (int)(slash - path), path);
    } else {
        snprintf(dir, sizeof dir, ".");
    }
    if (!f) {
        fprintf(stderr, "%s: %s: cannot open file\n", TOOL, path);
        exit(1);
    }
    while (fgets(buf, sizeof buf, f)) {
        char *hash = strchr(buf, '#'), *key;
        line++;
        if (hash) *hash = 0;
        key = strtok(buf, " \t\r\n");
        if (!key) continue;
        if (strcmp(key, "font") == 0) {
            char *file = strtok(NULL, " \t\r\n"), *col;
            if (!file) { error_at(path, line, "font <file> <colour 1> [<colour 2> [<colour 3>]]"); continue; }
            snprintf(font_file, sizeof font_file, "%s", file);
            while ((col = strtok(NULL, " \t\r\n")) && font_color_count < 3) {
                font_colors[font_color_count++] = (uint32_t)strtoul(col, NULL, 16);
            }
        } else if (strcmp(key, "tilesets") == 0) {
            char *t;
            while ((t = strtok(NULL, " \t\r\n")) && tileset_count < MAX_TILESETS) {
                snprintf(tileset_files[tileset_count++], sizeof tileset_files[0], "%s", t);
            }
        } else if (strcmp(key, "title") == 0) {
            char *file = strtok(NULL, " \t\r\n"), *n = strtok(NULL, " \t\r\n");
            if (!file || !n || atoi(n) < 1 || atoi(n) > 6) { error_at(path, line, "title <file> <palettes 1..6>"); continue; }
            snprintf(title_file, sizeof title_file, "%s", file);
            title_palettes = atoi(n);
        } else if (strcmp(key, "sprites") == 0) {
            char *file = strtok(NULL, " \t\r\n");
            if (!file) { error_at(path, line, "sprites <file>"); continue; }
            snprintf(sprites_file, sizeof sprites_file, "%s", file);
        } else if (strcmp(key, "palette") == 0) {
            char *name = strtok(NULL, " \t\r\n"), *num = strtok(NULL, " \t\r\n");
            PaletteClass *pc = &classes[class_count];
            if (!name || !num || class_count == 8 || atoi(num) < 0 || atoi(num) > 7) {
                error_at(path, line, "palette <name> <number 0..7> <cells>");
                continue;
            }
            snprintf(pc->name, sizeof pc->name, "%s", name);
            pc->number = atoi(num);
            if (strcmp(name, "enemies") == 0) enemies_class = class_count;
            add_cells(path, line, strtok(NULL, " \t\r\n"), pc->cells, &pc->count, CELLS);
            class_count++;
        } else if (strcmp(key, "enemy") == 0) {
            char *t = strtok(NULL, " \t\r\n");
            int type = t ? atoi(t) : 0;
            if (type < 1 || type > ENEMY_TYPES) { error_at(path, line, "enemy <type 1..8> <cells>"); continue; }
            add_cells(path, line, strtok(NULL, " \t\r\n"), enemy_cells[type], &enemy_count[type], 32);
        } else if (strcmp(key, "boss") == 0) {
            add_cells(path, line, strtok(NULL, " \t\r\n"), boss_cells, &boss_count, 32);
        } else if (strcmp(key, "p2") == 0) {
            char *t;
            while ((t = strtok(NULL, " \t\r\n")) && p2_count < 16) {
                char *colon = strchr(t, ':');
                if (!colon) { error_at(path, line, "p2 <from>:<to> ..."); break; }
                p2_from[p2_count] = (uint32_t)strtoul(t, NULL, 16);
                p2_to[p2_count++] = (uint32_t)strtoul(colon + 1, NULL, 16);
            }
        } else if (strcmp(key, "tint") == 0) {
            char *name = strtok(NULL, " \t\r\n"), *col = strtok(NULL, " \t\r\n");
            int t;
            for (t = 0; t < NUM_TINTS; t++) {
                if (name && strcmp(name, tint_names[t]) == 0) break;
            }
            if (t == NUM_TINTS || !col) { error_at(path, line, "tint <red|grey|yellow|orange> <colour>"); continue; }
            tints[t] = (uint32_t)strtoul(col, NULL, 16);
            tint_set[t] = 1;
        } else if (strcmp(key, "sheet") == 0) {
            char *what = strtok(NULL, " \t\r\n"), *t;
            Sheet *sh = &sheets[sheet_count];
            if (!what || sheet_count == MAX_SHEETS) { error_at(path, line, "sheet <rounds|name> [boss] [enemies <types>]"); continue; }
            memset(sh, 0, sizeof *sh);
            snprintf(sh->name, sizeof sh->name, "%s", what);
            if (!range(what, &sh->first, &sh->last)) {
                sh->first = sh->last = 0;   /* a named sheet, e.g. "screens" */
            }
            while ((t = strtok(NULL, " \t\r\n"))) {
                if (strcmp(t, "boss") == 0) {
                    sh->boss = 1;
                } else if (strcmp(t, "enemies") == 0) {
                    sh->explicit_enemies = 1;
                } else if (sh->explicit_enemies) {
                    int a, b, k;
                    if (!range(t, &a, &b) || a < 1 || b > ENEMY_TYPES) { error_at(path, line, "bad enemy type"); break; }
                    for (k = a; k <= b; k++) sh->enemy_types[k] = 1;
                } else {
                    error_at(path, line, "unknown word in a sheet line");
                    break;
                }
            }
            sheet_count++;
        } else {
            error_at(path, line, "unknown setting");
        }
    }
    fclose(f);
    if (!font_file[0] || !title_file[0] || !sprites_file[0] || !tileset_count || enemies_class < 0 || !sheet_count) {
        fprintf(stderr, "%s: %s: needs font, tilesets, title, sprites, an 'enemies' palette and sheets\n", TOOL, path);
        exit(1);
    }
    {
        int t;
        for (t = 0; t < NUM_TINTS; t++) {
            if (!tint_set[t]) {
                fprintf(stderr, "%s: %s: tint %s is missing\n", TOOL, path, tint_names[t]);
                exit(1);
            }
        }
    }
}

/* Enemy types of each round sheet, from the level files. */
static void sheet_enemies(void)
{
    int s, l, e;
    for (s = 0; s < sheet_count; s++) {
        Sheet *sh = &sheets[s];
        if (sh->explicit_enemies || !sh->first) continue;
        if (sh->last > num_levels) {
            fprintf(stderr, "%s: sheet %s: there are only %d levels\n", TOOL, sh->name, num_levels);
            errors++;
            continue;
        }
        for (l = sh->first - 1; l < sh->last; l++) {
            for (e = 0; e < levels[l].enemy_count; e++) {
                int type = levels[l].enemies[e].type;
                if (type >= 1 && type <= ENEMY_TYPES) sh->enemy_types[type] = 1;
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *out_c_path = NULL, *out_h_path = NULL, *out_asm_path = NULL, *settings = NULL;
    int i;
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_c_path = argv[++i];
        } else if (strcmp(argv[i], "-H") == 0 && i + 1 < argc) {
            out_h_path = argv[++i];
        } else if (strcmp(argv[i], "--snes") == 0 && i + 1 < argc) {
            out_asm_path = argv[++i];
        } else if (!settings) {
            settings = argv[i];
        } else {
            break;
        }
    }
    if (!out_c_path || !out_h_path || !settings) {
        fprintf(stderr, "usage: gfx2c -o <gfx_data.c> -H <gfx_data.h> [--snes <gfx_data.asm>] <gfx.txt> <level.map>...\n");
        return 2;
    }
    read_settings(settings);
    num_levels = argc - i;
    if (num_levels > MAX_LEVELS) num_levels = MAX_LEVELS;
    levels = (MapFile *)calloc((size_t)(num_levels ? num_levels : 1), sizeof(MapFile));
    for (; i < argc && i - (argc - num_levels) < num_levels; i++) {
        if (!mapfile_load(TOOL, argv[i], &levels[i - (argc - num_levels)])) errors++;
    }
    sheet_enemies();
    if (errors) return 1;

    out_c = fopen(out_c_path, "w");
    out_h = fopen(out_h_path, "w");
    out_asm = out_asm_path ? fopen(out_asm_path, "w") : NULL;
    if (!out_c || !out_h || (out_asm_path && !out_asm)) {
        fprintf(stderr, "%s: cannot write the output files\n", TOOL);
        return 1;
    }
    fprintf(out_h, "/* Generated by tools/gfx2c from assets/gfx (gfx.txt). Do not edit. */\n");
    fprintf(out_h, "#ifndef BB_GFX_DATA_H\n#define BB_GFX_DATA_H\n\n#include <stdint.h>\n\n");
    fprintf(out_c, "/* Generated by tools/gfx2c from assets/gfx (gfx.txt). Do not edit. */\n");
    fprintf(out_c, "#include \"gfx_data.h\"\n\n");
    if (out_asm) {
        fprintf(out_c, "/* SNES build: the arrays are in the assembly file (ROM); only what has\n"
                       " * pointers is here. */\n\n");
        fprintf(out_asm, "; Generated by tools/gfx2c (--snes) from assets/gfx (gfx.txt). Do not edit.\n");
        fprintf(out_asm, "; The graphics for the SNES build, one ROM section per array.\n\n.include \"hdr.asm\"\n\n");
    }
    convert_font();
    convert_tilesets();
    convert_title();
    convert_sprites();
    fprintf(out_h, "#endif /* BB_GFX_DATA_H */\n");
    fclose(out_c);
    fclose(out_h);
    if (out_asm) fclose(out_asm);
    if (errors) {
        remove(out_c_path);
        remove(out_h_path);
        if (out_asm_path) remove(out_asm_path);
        return 1;
    }
    return 0;
}
