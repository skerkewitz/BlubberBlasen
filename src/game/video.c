/*
 * Video: the game's picture on the SNES and on Rachael (ADR-0005, ADR-0006, ADR-0007).
 *
 * The game describes each frame as before: a sprite list with cell numbers
 * and logical palettes (oam_add, PAL_*), two tile maps (g_bg0 room or title,
 * g_bg1 text) and two colour windows. This module turns that into what the
 * SNES needs, through superdevkit's console API (sdevkit/snes.h), in two
 * halves, as a SNES game must:
 *
 * video_frame(), at the end of game_tick, while the picture is drawn: it
 * prepares everything that takes time. The sprite list goes into the OAM
 * shadow (the console's vertical blank copies it): the cell's slot in the
 * sprite sheet, its SNES palette (table below). The game's line y is row y of
 * the picture, which the SNES draws from scanline y + 1 (it never shows
 * scanline 0): a sprite at y appears there, HDMA's first entry lands there,
 * and both backgrounds are scrolled up by one line so that they do too.
 * Changed map rows are translated into a staging
 * buffer (up to STAGE_ROWS a frame). The colour windows and "the room only
 * below the HUD" go into HDMA tables in a second buffer. What has to reach
 * CGRAM and the registers is noted. The vertical blank is too short for C
 * code that loops over tiles: there, only copies run.
 *
 * video_vblank(), from game_vblank() in the vertical blank: it uploads. VRAM
 * and CGRAM only accept writes then, about 5.7 KB of them (snes.h), so it
 * keeps to VBLANK_BYTES per frame, most urgent first:
 *   1. registers: scroll, colour math, the HDMA tables just built
 *   2. per-frame palettes: a player's flash, frozen grey or the yellow flash
 *      before an enemy shoots
 *   3. a page's tileset (when a room is drawn), then the staged map rows
 *   4. a new room group's sprite sheet, a row of 8 cells (1 KB) at a time:
 *      it changes while the next room scrolls in, when no enemy is on screen
 * Only the title logo (15 KB) does not fit: it is loaded with the screen off,
 * which shows as one black frame when the title appears.
 *
 * VRAM (word addresses):
 *   0x0000 BG1 map, 32 x 64 (two pages, the room scrolls between them)
 *   0x0800 BG3 map, 32 x 32 (text and HUD)
 *   0x1000 BG1 tiles: the title logo, or one tileset per page (tile 0 empty)
 *   0x3000 BG3 tiles: the font
 *   0x4000 sprite tiles: the shared rows, then the sheet's own
 * CGRAM: BG3 palettes 0..7 (text, one per PAL_*), BG1 palettes 2..3 (page 0
 * and 1 tilesets) or 2..7 (title); sprite palettes as in ADR-0006.
 */

#include "game/internal.h"
#include "gfx_data.h"
#include "sdevkit/snes.h"

#define VRAM_BG1_MAP   0x0000
#define VRAM_BG3_MAP   0x0800
#define VRAM_BG1_TILES 0x1000
#define VRAM_BG3_TILES 0x3000
#define VRAM_OBJ       0x4000

#define VBLANK_BYTES   4096     /* below SDEVKIT__VBLANK_DMA_BYTES: the uploading code takes time too */
#define STAGE_ROWS     32       /* map rows translated per frame (2 KB, a room page) */
#define SPR_PRIORITY   2        /* above the room, below the text (BG3 with priority) */
#define SPR_OFF_Y      SDEVKIT_ATTR2_DISABLED

/* Sprite palettes (ADR-0006) */
enum { OP_P1, OP_P2, OP_EFFECTS, OP_ITEMS, OP_ENEMIES, OP_ENEMIES_RED, OP_ENEMIES_TINT, OP_PLAYER_GREY };

uint8_t g_player_tint[NUM_PLAYERS];   /* PAL_NONE, PAL_YELLOW or PAL_ORANGE: set by player_draw */
#ifdef BB_DEBUG
uint16_t g_video_missing;             /* sprites whose cell is not in the sheet */
#endif

/* What is (or is being) loaded, and what the next vertical blank must do. */
static BB_FAST uint8_t sheet;                  /* sprite sheet the sprites refer to */
static uint8_t sheet_rows_left;        /* rows of 8 cells of it still to upload */
static uint8_t page_tileset[2];        /* tileset wanted on each room page */
static uint8_t page_loaded[2];         /* tileset uploaded for each page (0xFF none) */
static uint8_t title_wanted, title_loaded;
static uint8_t dirty0[BG0_ROWS], dirty1[BG_H];
static BB_FAST uint8_t dirty0_count, dirty1_count;   /* rows marked in dirty0 / dirty1 */
BB_FAST uint16_t g_bg1_changes;
static uint8_t tint6, tint6_loaded;    /* GFX_TINT_* in sprite palette 6 */
static uint8_t tint_p[NUM_PLAYERS], tint_p_loaded[NUM_PLAYERS];   /* in sprite palettes 0, 1 */
static BB_FAST uint8_t screen_on;

/* Registers to write in the vertical blank. */
static struct {
    uint16_t scroll_y;
    uint8_t  wobjsel, cgwsel, cgadsub, r, g, b;
} regs;

/* HDMA tables: built in one buffer while the other is read by the picture
 * being drawn; the vertical blank switches. Window tables: the lines above
 * and below the window's lines held empty (3 bytes per run of up to 127
 * lines), its lines in repeat mode (two bytes per line, a count per 127). */
#define WINDOW_TABLE_BYTES (2 * 3 + 2 + SCREEN_H * 2 + 2 * 3 + 1)
static uint8_t window_table[2][NUM_WINDOWS][WINDOW_TABLE_BYTES];
static uint8_t tm_table[2][8];
static BB_FAST uint8_t hdma_back;              /* the buffer video_frame fills */

/* Map rows translated by video_frame, copied by video_vblank: one buffer,
 * row after row, so that rows next to each other in VRAM go in one copy. */
static uint8_t  stage[STAGE_ROWS * BG_W * 2];
static uint16_t stage_vram[STAGE_ROWS];
static uint8_t  stage_run[STAGE_ROWS];     /* rows from here on that follow each other in VRAM */
static BB_FAST uint8_t  stage_count, stage_next;   /* rows staged, first row not yet copied */
static BB_FAST uint8_t hdma_ready;             /* the back buffer is complete */

/* Text colours: palette k of BG3 is PAL k's colour for the letters and black
 * for the outline (the PC tinted white letters with these colours). */
static const uint16_t text_colors[8] = {
    SDEVKIT_RGB8(255, 255, 255), SDEVKIT_RGB8(255, 80, 80), SDEVKIT_RGB8(110, 255, 110), SDEVKIT_RGB8(110, 220, 255),
    SDEVKIT_RGB8(255, 230, 80), SDEVKIT_RGB8(255, 130, 210), SDEVKIT_RGB8(255, 160, 60), SDEVKIT_RGB8(150, 150, 150)
};

/* --- Map rows ------------------------------------------------------------------ */
void bg_mark_row(uint8_t layer, uint8_t row)
{
    if (layer == 0) {
        if (row < BG0_ROWS && !dirty0[row]) {
            dirty0[row] = 1;
            dirty0_count++;
        }
    } else {
        g_bg1_changes++;
        if (row < BG_H && !dirty1[row]) {
            dirty1[row] = 1;
            dirty1_count++;
        }
    }
}

void bg_mark_all(uint8_t layer)
{
    uint8_t r;
    for (r = 0; r < (layer == 0 ? BG0_ROWS : BG_H); r++) {
        bg_mark_row(layer, r);
    }
}

/* A g_bg0 entry as the VRAM entry of BG1. */
static uint16_t room_entry(uint16_t e, uint8_t row)
{
    uint16_t t = (uint16_t)(e & BG_TILE_MASK);
    if (t == 0) {
        return 0;
    }
    if (t >= CHR_TITLE_BASE) {
        uint16_t k = (uint16_t)(t - CHR_TITLE_BASE);
        uint16_t m = gfx_title_map[k];   /* the title map is 32 wide, like the game's tile numbers */
        return (uint16_t)((m & 0xFC00u) | ((m & 0x03FFu) + 1));
    }
    if (t >= CHR_TILESET_BASE) {
        uint8_t page = (uint8_t)(row >= PAGE_ROWS);
        uint16_t k = (uint16_t)((t - CHR_TILESET_BASE) & (CHR_TILESET_STRIDE - 1));
        return (uint16_t)((1 + (page ? GFX_TILESET_TILES : 0) + k) | SDEVKIT_BG_TIL_PAL(2 + page));
    }
    return 0;
}

static uint16_t text_entry(uint16_t e)
{
    if (!(e & BG_TILE_MASK)) {
        return 0;
    }
    return (uint16_t)((e & BG_TILE_MASK) | SDEVKIT_BG_TIL_PAL((e >> BG_PAL_SHIFT) & 7) | SDEVKIT_BG_TIL_PRIO);
}

/* Translates changed rows into the staging buffer (in the frame), once the
 * last batch has been copied. Most ticks change no row at all: the counts
 * end the scan as soon as every marked row is staged, and skip it when none
 * is marked (checking all 92 flags every tick cost a tenth of a tick). */
static void stage_rows(void)
{
    uint8_t r, c;
    if (stage_next < stage_count) {
        return;
    }
    stage_count = stage_next = 0;
    for (r = 0; r < BG0_ROWS && dirty0_count && stage_count < STAGE_ROWS; r++) {
        uint8_t *buf;
        if (!dirty0[r]) continue;
        dirty0[r] = 0;
        dirty0_count--;
        buf = &stage[(uint16_t)stage_count << 6];
        for (c = 0; c < BG_W; c++) {
            uint16_t v = room_entry(g_bg0[(r << 5) + c], r);
            buf[c * 2] = (uint8_t)v;
            buf[c * 2 + 1] = (uint8_t)(v >> 8);
        }
        stage_vram[stage_count++] = (uint16_t)(VRAM_BG1_MAP + (r << 5));
    }
    for (r = 0; r < BG_H && dirty1_count && stage_count < STAGE_ROWS; r++) {
        uint8_t *buf;
        if (!dirty1[r]) continue;
        dirty1[r] = 0;
        dirty1_count--;
        buf = &stage[(uint16_t)stage_count << 6];
        for (c = 0; c < BG_W; c++) {
            uint16_t v = text_entry(g_bg1[(r << 5) + c]);
            buf[c * 2] = (uint8_t)v;
            buf[c * 2 + 1] = (uint8_t)(v >> 8);
        }
        stage_vram[stage_count++] = (uint16_t)(VRAM_BG3_MAP + (r << 5));
    }
    /* the runs, counted here and not in the vertical blank: from the last row
     * back, each row's run is 1 plus the next row's if that one follows */
    if (stage_count) {
        uint8_t k = (uint8_t)(stage_count - 1);
        stage_run[k] = 1;
        while (k--) {
            stage_run[k] = (uint8_t)(stage_vram[k + 1] == stage_vram[k] + BG_W ? stage_run[k + 1] + 1 : 1);
        }
    }
}

/* May a copy of this many bytes run now? Always with the screen off; else
 * within the budget and while the vertical blank lasts. */
static uint8_t fits(uint16_t used, uint16_t bytes)
{
    return (uint8_t)(!screen_on || (used + bytes <= VBLANK_BYTES && sdevkit_dma_fits(bytes)));
}

/* Copies staged rows (in the vertical blank), rows next to each other in VRAM
 * in one copy, while there is time; returns the bytes used. */
static uint16_t upload_rows(uint16_t used)
{
    uint16_t start = used;
    while (stage_next < stage_count) {
        uint8_t n = stage_run[stage_next];
        uint16_t bytes = (uint16_t)((uint16_t)n << 6);
        if (!fits(used, bytes)) {
            if (n == 1 || !fits(used, BG_W * 2)) {
                break;
            }
            n = 1;   /* not the whole run: one row */
            bytes = BG_W * 2;
        }
        sdevkit_dma_copy_vram(&stage[(uint16_t)stage_next << 6], stage_vram[stage_next], bytes);
        used = (uint16_t)(used + bytes);
        stage_next = (uint8_t)(stage_next + n);
    }
    return (uint16_t)(used - start);
}

/* --- Room tiles and the title --------------------------------------------------- */
void video_tileset(uint8_t page, uint8_t tileset)
{
    page &= 1;
    page_tileset[page] = (uint8_t)(tileset < GFX_TILESETS ? tileset : 0);
    title_wanted = 0;
}

void video_title(void)
{
    title_wanted = 1;
}

/* --- Sprite sheets -------------------------------------------------------------- */
static uint8_t wanted_sheet(void)
{
    switch (g_state) {
    case GS_TITLE:
    case GS_STORY:
    case GS_OPTIONS:
    case GS_ENDING:
    case GS_NAME_ENTRY:
    case GS_HISCORES:
        return GFX_SHEET_SCREENS;
    default:
        if (g_level < GFX_LEVELS && gfx_level_sheet[g_level] != 0xFF) {
            return gfx_level_sheet[g_level];
        }
        return sheet;   /* secret room: keep */
    }
}

/* --- Sprites -------------------------------------------------------------------- */
/* The SNES palette of a cell of palette class `base` (gfx_cell_pal) drawn
 * with the game's logical palette `pal`. */
static uint8_t sprite_palette(uint8_t base, uint8_t pal)
{
    switch (base) {
    case OP_P1:
        if (pal == PAL_P2) return OP_P2;
        if (pal == PAL_GREY) return OP_PLAYER_GREY;
        if (pal == PAL_YELLOW || pal == PAL_ORANGE) {
            uint8_t want = (uint8_t)(pal == PAL_YELLOW ? GFX_TINT_YELLOW : GFX_TINT_ORANGE);
            return (uint8_t)(tint_p[0] == want ? OP_P1 : OP_P2);
        }
        return OP_P1;
    case OP_ENEMIES:
        if (pal == PAL_RED) return OP_ENEMIES_RED;
        if (pal == PAL_GREY) return tint6 == GFX_TINT_GREY ? OP_ENEMIES_TINT : OP_ENEMIES;
        if (pal == PAL_YELLOW) return tint6 == GFX_TINT_YELLOW ? OP_ENEMIES_TINT : OP_ENEMIES;
        return OP_ENEMIES;
    default:
        return base;
    }
}

/* Sprites into the OAM shadow, and which tints the per-frame palettes need
 * (grey wins over yellow when both are wanted in one frame). One pass over
 * the sprites the game drew, written straight into sdevkit_oam_memory (the SNES
 * layout, snes.h): calling sdevkit_oam_set and sdevkit_oam_set_ex for 128 sprites took most of
 * a frame on the console. All sprites are large (the high table is set once
 * in video_init) and x stays below 256. The entries used last frame and not
 * in this one are hidden.
 *
 * Only an enemy drawn grey or yellow depends on the tint the whole frame
 * wants, which is known after the pass: those entries are noted and their
 * palette set afterwards (a second pass over all sprites cost a fifth of the
 * time). The game's flip bits are the SNES's (bits 7 and 6), so they are
 * copied as they are. */
static BB_FAST uint8_t oam_shown;   /* entries filled last frame */
static uint8_t tint_later[MAX_SPRITES];       /* OAM entries whose palette waits for the tint */
static uint8_t tint_later_pal[MAX_SPRITES];   /* and their logical palettes (PAL_GREY or PAL_YELLOW) */
typedef char flip_bits_are_the_snes[SPR_ATTR_FLIP_Y == 0x80u && SPR_ATTR_FLIP_X == 0x40u ? 1 : -1];

static void build_sprites(const Sprite *oam, uint8_t count)
{
    const uint8_t *slots = gfx_sheet_slot[sheet];
    uint8_t *o = sdevkit_oam_memory;
    uint8_t i, want6 = GFX_TINT_NONE, filled = 0, later = 0;

    for (i = 0; i < NUM_PLAYERS; i++) {
        tint_p[i] = (uint8_t)(g_player_tint[i] == PAL_YELLOW ? GFX_TINT_YELLOW
                            : g_player_tint[i] == PAL_ORANGE ? GFX_TINT_ORANGE : GFX_TINT_NONE);
    }

    for (i = 0; i < count; i++, oam++) {
        uint8_t base = gfx_cell_pal[oam->tile];
        uint8_t pal = (uint8_t)((oam->attr & SPR_ATTR_PAL_MASK) >> SPR_ATTR_PAL_SHIFT);
        uint8_t slot = slots[oam->tile];
        uint16_t name;
        uint8_t tinted = 0;
        if (base == OP_ENEMIES) {
            if (pal == PAL_GREY) {
                want6 = GFX_TINT_GREY;
                tinted = 1;
            } else if (pal == PAL_YELLOW) {
                if (want6 == GFX_TINT_NONE) want6 = GFX_TINT_YELLOW;
                tinted = 1;
            }
        }
        if (slot == 0xFF) {
#ifdef BB_DEBUG
            g_video_missing++;
#endif
            continue;
        }
        if (tinted) {
            tint_later[later] = filled;     /* its palette once the frame's tint is known */
            tint_later_pal[later] = pal;
            later++;
        }
        name = (uint16_t)(((slot & 7u) << 1) | ((uint16_t)(slot >> 3) << 5));
        o[0] = oam->x;
        o[1] = oam->y;
        o[2] = (uint8_t)name;
        o[3] = (uint8_t)((oam->attr & (SPR_ATTR_FLIP_Y | SPR_ATTR_FLIP_X)) | ((uint8_t)SPR_PRIORITY << 4) |
                         ((uint8_t)sprite_palette(base, pal) << 1) | ((name >> 8) & 1u));
        o += 4;
        filled++;
    }
    if (want6 != GFX_TINT_NONE) {
        tint6 = want6;
    }
    for (i = 0; i < later; i++) {
        uint8_t *e = sdevkit_oam_memory + ((uint16_t)tint_later[i] << 2);
        e[3] = (uint8_t)((e[3] & 0xF1u) | (sprite_palette(OP_ENEMIES, tint_later_pal[i]) << 1));   /* bits 1..3 */
    }
    for (i = filled; i < oam_shown; i++, o += 4) {
        o[1] = SPR_OFF_Y;
    }
    oam_shown = filled;
}

/* --- Windows and the HUD line ---------------------------------------------------- */
/* n lines of an empty window (left 255, right 0), held: one write per run. */
static uint8_t *hold_empty(uint8_t *t, uint8_t n)
{
    while (n) {
        uint8_t k = n > 127 ? 127 : n;
        *t++ = k;
        *t++ = 255;
        *t++ = 0;
        n = (uint8_t)(n - k);
    }
    return t;
}

/* The window's table: empty above line `from` and from line `to` on, which
 * internal.h promises; in between the edges line by line. The same values on
 * every line as a table of all 224 lines, in a fraction of the time. */
static void build_window_table(const ColorWindow *w, uint8_t *t, uint8_t from, uint8_t to)
{
    const uint8_t *l = w->left + from, *r = w->right + from;
    uint8_t n = (uint8_t)(to - from);
    t = hold_empty(t, from);
    while (n) {
        uint8_t k = n > 127 ? 127 : n, y;
        *t++ = (uint8_t)(0x80 + k);
        for (y = 0; y < k; y++) {
            *t++ = *l++;
            *t++ = *r++;
        }
        n = (uint8_t)(n - k);
    }
    t = hold_empty(t, (uint8_t)(SCREEN_H - to));
    *t = 0;
}

static uint8_t window_on(const ColorWindow *w)
{
    return (uint8_t)(w->add_r || w->add_g || w->add_b || (w->math & CMATH_HALF));
}

static void build_windows(const ColorWindow *w)
{
    uint8_t i, on = 0, math = 0;
    uint8_t *t = window_table[hdma_back][0];
    regs.r = regs.g = regs.b = 0;
    regs.wobjsel = 0;
    for (i = 0; i < NUM_WINDOWS; i++, w++, t += WINDOW_TABLE_BYTES) {
        if (!window_on(w)) {
            /* an empty table, so the window register holds nothing stale */
            t[0] = 1;
            t[1] = 255;
            t[2] = 0;
            t[3] = 0;
            continue;
        }
        on++;
        math = w->math;
        /* one fixed colour for the screen: the stronger of the two */
        if (w->add_r > regs.r) regs.r = w->add_r;
        if (w->add_g > regs.g) regs.g = w->add_g;
        if (w->add_b > regs.b) regs.b = w->add_b;
        regs.wobjsel = (uint8_t)(regs.wobjsel | (i == 0 ? 0x20 : 0x80));   /* colour window = window 1 / 2 */
        build_window_table(w, t, g_window_from[i], g_window_to[i]);
    }
    regs.cgwsel = (uint8_t)(on ? SDEVKIT_CM_MATWIN_ENABLE : SDEVKIT_CM_NEVER_ENABLE);
    regs.cgadsub = (uint8_t)(SDEVKIT_CM_MSCR_BG1 | SDEVKIT_CM_MSCR_BACK | ((math & CMATH_SUB) ? SDEVKIT_CM_SUB_MAINSUB : 0) |
                             ((math & CMATH_HALF) ? SDEVKIT_CM_DIV2_DIVIDE : 0));
}

/* BG1 (the room) only from g_window_top down: the main screen register per line. */
static void build_hud_line(void)
{
    const uint8_t all = SDEVKIT_BG1_ENABLE | SDEVKIT_BG3_ENABLE | SDEVKIT_OBJ_ENABLE, top = g_window_top;
    uint8_t *p = tm_table[hdma_back];
    if (top > 0) {
        uint8_t n = top > 127 ? 127 : top;
        *p++ = n;
        *p++ = (uint8_t)(all & ~SDEVKIT_BG1_ENABLE);
        if (top > 127) {
            *p++ = (uint8_t)(top - 127);
            *p++ = (uint8_t)(all & ~SDEVKIT_BG1_ENABLE);
        }
    }
    *p++ = 1;
    *p++ = all;
    *p = 0;
}

/* --- Setup, frame and vertical blank ------------------------------------------------- */
/* Everything that never changes, with the screen off (also when a new game
 * starts from a replay): the next vertical blank turns it on again. */
void video_init(void)
{
    uint8_t i;
    static uint16_t text_pal[8 * 4];

    sdevkit_set_screen_off();
    sdevkit_set_mode(SDEVKIT_BG_MODE1, SDEVKIT_BG3_MODE1_PRIORITY_HIGH);
    sdevkit_bg_set_map_ptr(0, VRAM_BG1_MAP, SDEVKIT_SC_32x64);
    sdevkit_bg_set_gfx_ptr(0, VRAM_BG1_TILES);
    sdevkit_bg_set_map_ptr(2, VRAM_BG3_MAP, SDEVKIT_SC_32x32);
    sdevkit_bg_set_scroll(2, 0, 0x3FF);       /* one line up, as BG1 (see the top) */
    sdevkit_bg_set_disable(1);
    for (i = 0; i < 8; i++) {
        text_pal[i * 4] = 0;
        text_pal[i * 4 + 1] = text_colors[i];
        text_pal[i * 4 + 2] = 0;             /* outline: black */
        text_pal[i * 4 + 3] = 0;
    }
    sdevkit_bg_init_tile_set(2, gfx_font_tiles, (const uint8_t *)text_pal, 0, GFX_FONT_BYTES, sizeof text_pal, SDEVKIT_BG_4COLORS,
                  VRAM_BG3_TILES);
    sdevkit_set_palette_color(0, 0);                   /* backdrop: black */
    sdevkit_oam_init_gfx_attr(VRAM_OBJ, SDEVKIT_OBJ_SIZE8_L16);
    sdevkit_oam_init();
    for (i = 0; i < 32; i++) {
        sdevkit_oam_memory[512 + i] = 0xAA;           /* all 128 sprites large, x below 256 */
    }
    oam_shown = 0;
    sdevkit_dma_copy_vram(gfx_sprites_shared, VRAM_OBJ, GFX_SPRITE_FIRST_ROW * 1024);
    sdevkit_dma_copy_cgram((const uint8_t *)gfx_pal_effects[GFX_TINT_NONE], 128 + OP_EFFECTS * 16, 32);
    sdevkit_dma_copy_cgram((const uint8_t *)gfx_pal_items[GFX_TINT_NONE], 128 + OP_ITEMS * 16, 32);
    sdevkit_dma_copy_cgram((const uint8_t *)gfx_pal_player[0][GFX_TINT_GREY], 128 + OP_PLAYER_GREY * 16, 32);
    sheet = GFX_SHEET_SCREENS;
    sheet_rows_left = (uint8_t)(gfx_sheet_bytes[sheet] >> 10);
    page_tileset[0] = page_tileset[1] = 0;
    page_loaded[0] = page_loaded[1] = 0xFF;
    title_wanted = title_loaded = 0;
    tint6 = GFX_TINT_GREY;
    tint6_loaded = 0xFF;
    screen_on = 0;
    hdma_back = 0;
    hdma_ready = 0;
    for (i = 0; i < NUM_PLAYERS; i++) {
        tint_p[i] = GFX_TINT_NONE;
        tint_p_loaded[i] = 0xFF;
        g_player_tint[i] = PAL_NONE;
    }
    regs.scroll_y = 0;
    stage_count = stage_next = 0;
    bg_mark_all(0);
    bg_mark_all(1);
}

void video_frame(const Sprite *oam, uint8_t count, const ColorWindow *windows)
{
    uint8_t s = wanted_sheet();
    if (s != sheet) {
        sheet = s;   /* the sprites below already refer to it; its rows follow in the vertical blanks */
        sheet_rows_left = (uint8_t)(gfx_sheet_bytes[s] >> 10);
    }
    build_sprites(oam, count);
    regs.scroll_y = g_scroll_y;
    build_windows(windows);
    build_hud_line();
    stage_rows();
    hdma_ready = 1;
    /* The tick's checksum (the library adds the sprite table): the two tilemaps, so that a ROM
     * that computes something else than the PC shows at the first tick it differs
     * (tools/snescheck). On the SNES each call only hands address and size to the checker. */
    sdevkit_dbg_hash(g_bg0, sizeof g_bg0);
    sdevkit_dbg_hash(g_bg1, sizeof g_bg1);
}

/* Uploads what the last video_frame prepared (see the top of this file). */
void video_vblank(void)
{
    uint16_t used = 0;
    uint8_t i;

    /* the title logo does not fit into a vertical blank: screen off for it */
    if (title_wanted && !title_loaded) {
        sdevkit_set_screen_off();
        sdevkit_dma_copy_vram(gfx_title_tiles, VRAM_BG1_TILES + 16, GFX_TITLE_TILES * 32);
        sdevkit_dma_copy_cgram((const uint8_t *)gfx_title_pal, 2 * 16, GFX_TITLE_PALETTES * 32);
        title_loaded = 1;
        page_loaded[0] = page_loaded[1] = 0xFF;
        screen_on = 0;
    }

    /* 1. registers and the HDMA tables just built */
    sdevkit_bg_set_scroll(0, 0, (uint16_t)((regs.scroll_y - 1) & 0x3FF));   /* one line up: see the top */
    sdevkit_reg_write(SDEVKIT_PPU_WOBJSEL, regs.wobjsel);
    sdevkit_reg_write(SDEVKIT_PPU_WOBJLOG, 0);   /* window 1 or window 2 */
    sdevkit_set_color_effect(regs.cgwsel, regs.cgadsub);
    sdevkit_set_color_intensity(SDEVKIT_CM_APPLY_RED, regs.r);
    sdevkit_set_color_intensity(SDEVKIT_CM_APPLY_GREEN, regs.g);
    sdevkit_set_color_intensity(SDEVKIT_CM_APPLY_BLUE, regs.b);
    if (hdma_ready) {
        sdevkit_hdma_set(3, SDEVKIT_PPU_TM, 0, tm_table[hdma_back]);
        sdevkit_hdma_set(4, SDEVKIT_PPU_WH0, 1, window_table[hdma_back][0]);
        sdevkit_hdma_set(5, SDEVKIT_PPU_WH2, 1, window_table[hdma_back][1]);
        sdevkit_hdma_enable(SDEVKIT_HDMA_CHANNEL3 | SDEVKIT_HDMA_CHANNEL4 | SDEVKIT_HDMA_CHANNEL5);
        hdma_back ^= 1;
        hdma_ready = 0;
    }

    /* 2. per-frame palettes */
    if (tint6 != tint6_loaded && fits(used, 32)) {
        sdevkit_dma_copy_cgram((const uint8_t *)gfx_pal_enemies[sheet][tint6], 128 + OP_ENEMIES_TINT * 16, 32);
        tint6_loaded = tint6;
        used = (uint16_t)(used + 32);
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (tint_p[i] != tint_p_loaded[i] && fits(used, 32)) {
            /* a flashing player shows player 1's colours tinted, as on the PC */
            const uint16_t *src = tint_p[i] == GFX_TINT_NONE ? gfx_pal_player[i][GFX_TINT_NONE] : gfx_pal_player[0][tint_p[i]];
            sdevkit_dma_copy_cgram((const uint8_t *)src, (uint16_t)(128 + i * 16), 32);
            tint_p_loaded[i] = tint_p[i];
            used = (uint16_t)(used + 32);
        }
    }

    /* 3. room tilesets, then the staged map rows */
    for (i = 0; i < 2 && !title_wanted; i++) {
        if (page_tileset[i] != page_loaded[i] && fits(used, GFX_TILESET_TILES * 32 + 32)) {
            sdevkit_dma_copy_vram(gfx_tileset_tiles[page_tileset[i]], (uint16_t)(VRAM_BG1_TILES + 16 + i * GFX_TILESET_TILES * 16),
                        GFX_TILESET_TILES * 32);
            sdevkit_dma_copy_cgram((const uint8_t *)gfx_tileset_pal[page_tileset[i]], (uint16_t)((2 + i) * 16), 32);
            page_loaded[i] = page_tileset[i];
            title_loaded = 0;
            used = (uint16_t)(used + GFX_TILESET_TILES * 32 + 32);
        }
    }
    used = (uint16_t)(used + upload_rows(used));

    /* 4. the sprite sheet, a row at a time; its enemy palettes with the last row */
    while (sheet_rows_left && fits(used, 1024 + 64)) {
        uint16_t done = (uint16_t)((gfx_sheet_bytes[sheet] >> 10) - sheet_rows_left);
        sdevkit_dma_copy_vram(gfx_sheet_tiles[sheet] + done * 1024, (uint16_t)(VRAM_OBJ + (GFX_SPRITE_FIRST_ROW + done) * 512), 1024);
        used = (uint16_t)(used + 1024);
        if (--sheet_rows_left == 0) {
            sdevkit_dma_copy_cgram((const uint8_t *)gfx_pal_enemies[sheet][GFX_TINT_NONE], 128 + OP_ENEMIES * 16, 32);
            sdevkit_dma_copy_cgram((const uint8_t *)gfx_pal_enemies[sheet][GFX_TINT_RED], 128 + OP_ENEMIES_RED * 16, 32);
            tint6_loaded = 0xFF;   /* palette 6 copies this sheet's enemies: write it again */
            used = (uint16_t)(used + 64);
        }
    }

    /* the screen goes on once the first picture is complete (not sdevkit_set_screen_on,
     * which waits for a vertical blank, and this is one) */
    if (!screen_on) {
        sdevkit_set_brightness(15);
        screen_on = 1;
    }
}

/* Nothing waits for a vertical blank (tests: the picture is complete). */
uint8_t video_pending(void)
{
    uint8_t r, n = 0;
    n = (uint8_t)(stage_next < stage_count);
    for (r = 0; r < BG0_ROWS; r++) n = (uint8_t)(n | dirty0[r]);
    for (r = 0; r < BG_H; r++) n = (uint8_t)(n | dirty1[r]);
    if (title_wanted) {
        n = (uint8_t)(n | !title_loaded);
    } else {
        n = (uint8_t)(n | (page_tileset[0] != page_loaded[0]) | (page_tileset[1] != page_loaded[1]));
    }
    return (uint8_t)(n || sheet_rows_left || tint6 != tint6_loaded);
}

#ifdef BB_DEBUG
/* Map entries in VRAM that differ from the game's maps (should be 0 once
 * nothing is pending: a difference means a write to g_bg0/g_bg1 was not
 * marked). */
uint16_t video_check_maps(void)
{
    const uint16_t *vram = sdevkit_dbg_vram();
    uint16_t bad = 0;
    uint8_t r, c;
    for (r = 0; r < BG0_ROWS; r++) {
        for (c = 0; c < BG_W; c++) {
            bad = (uint16_t)(bad + (vram[VRAM_BG1_MAP + (r << 5) + c] != room_entry(g_bg0[(r << 5) + c], r)));
        }
    }
    for (r = 0; r < BG_H; r++) {
        for (c = 0; c < BG_W; c++) {
            bad = (uint16_t)(bad + (vram[VRAM_BG3_MAP + (r << 5) + c] != text_entry(g_bg1[(r << 5) + c])));
        }
    }
    return bad;
}
#endif
