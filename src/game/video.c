/*
 * Video: the game's picture on the (pseudo) SNES (ADR-0005, ADR-0006).
 *
 * The game describes each frame as before: a sprite list with cell numbers
 * and logical palettes (oam_add, PAL_*), two tile maps (g_bg0 room or title,
 * g_bg1 text) and two colour windows. Once per tick, video_frame() turns
 * that description into what the SNES needs, through the PVSnesLib-shaped
 * functions of snes/snes.h:
 *   - the sprite list into the OAM shadow: the cell's slot in the loaded
 *     sprite sheet (gfx_sheet_slot), its SNES palette (table below), y - 1
 *     because the SNES shows sprites one line lower
 *   - map rows the game marked as changed (bg_mark_row) into VRAM, with the
 *     game's tile numbers translated to the tiles loaded there
 *   - the colour windows into HDMA tables for the window registers, and
 *     colour math with the window's colour as the fixed colour
 *   - "the room only below the HUD" into an HDMA table for the main screen
 *   - the palettes that change per frame: a player's flash, frozen grey or
 *     the yellow flash before an enemy shoots
 * It also loads the sprite sheet of the round's group, the tilesets of the
 * two room pages and the title logo when they are needed.
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
#include "snes/snes.h"

#define VRAM_BG1_MAP   0x0000
#define VRAM_BG3_MAP   0x0800
#define VRAM_BG1_TILES 0x1000
#define VRAM_BG3_TILES 0x3000
#define VRAM_OBJ       0x4000

#define SPR_PRIORITY   2        /* above the room, below the text (BG3 with priority) */
#define SPR_OFF_Y      ATTR2_DISABLED

/* Sprite palettes (ADR-0006) */
enum { OP_P1, OP_P2, OP_EFFECTS, OP_ITEMS, OP_ENEMIES, OP_ENEMIES_RED, OP_ENEMIES_TINT, OP_PLAYER_GREY };

uint8_t g_player_tint[NUM_PLAYERS];   /* PAL_NONE, PAL_YELLOW or PAL_ORANGE: set by player_draw */

static uint8_t sheet = 0xFF;            /* loaded sprite sheet */
static uint8_t page_tileset[2] = { 0xFF, 0xFF };
static uint8_t title_loaded;
static uint8_t dirty0[BG0_ROWS], dirty1[BG_H];
static uint8_t tint6;                   /* GFX_TINT_* in sprite palette 6 */
static uint8_t tint_p[NUM_PLAYERS];     /* GFX_TINT_* in sprite palettes 0, 1 */
#ifdef BB_DEBUG
uint16_t g_video_missing;               /* sprites whose cell is not in the loaded sheet */
#endif

/* Text colours: palette k of BG3 is PAL k's colour for the letters and black
 * for the outline (the PC tinted white letters with these colours). */
static const uint16_t text_colors[8] = {
    RGB8(255, 255, 255), RGB8(255, 80, 80), RGB8(110, 255, 110), RGB8(110, 220, 255),
    RGB8(255, 230, 80), RGB8(255, 130, 210), RGB8(255, 160, 60), RGB8(150, 150, 150)
};

/* --- Map rows ------------------------------------------------------------------ */
void bg_mark_row(uint8_t layer, uint8_t row)
{
    if (layer == 0) {
        if (row < BG0_ROWS) dirty0[row] = 1;
    } else if (row < BG_H) {
        dirty1[row] = 1;
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
        uint16_t m = gfx_title_map[(k >> 5) * GFX_TITLE_W + (k & 31)];
        return (uint16_t)((m & 0xFC00u) | ((m & 0x03FFu) + 1));
    }
    if (t >= CHR_TILESET_BASE) {
        uint8_t page = (uint8_t)(row >= PAGE_ROWS);
        uint16_t k = (uint16_t)((t - CHR_TILESET_BASE) & (CHR_TILESET_STRIDE - 1));
        return (uint16_t)((1 + page * GFX_TILESET_TILES + k) | BG_TIL_PAL(2 + page));
    }
    return 0;
}

static void upload_rows(void)
{
    static uint8_t buf[BG_W * 2];
    uint8_t r, c;
    for (r = 0; r < BG0_ROWS; r++) {
        if (!dirty0[r]) continue;
        dirty0[r] = 0;
        for (c = 0; c < BG_W; c++) {
            uint16_t v = room_entry(g_bg0[(r << 5) + c], r);
            buf[c * 2] = (uint8_t)v;
            buf[c * 2 + 1] = (uint8_t)(v >> 8);
        }
        dmaCopyVram(buf, (uint16_t)(VRAM_BG1_MAP + (r << 5)), sizeof buf);
    }
    for (r = 0; r < BG_H; r++) {
        if (!dirty1[r]) continue;
        dirty1[r] = 0;
        for (c = 0; c < BG_W; c++) {
            uint16_t e = g_bg1[(r << 5) + c], v = 0;
            if (e & BG_TILE_MASK) {
                v = (uint16_t)((e & BG_TILE_MASK) | BG_TIL_PAL((e >> BG_PAL_SHIFT) & 7) | BG_TIL_PRIO);
            }
            buf[c * 2] = (uint8_t)v;
            buf[c * 2 + 1] = (uint8_t)(v >> 8);
        }
        dmaCopyVram(buf, (uint16_t)(VRAM_BG3_MAP + (r << 5)), sizeof buf);
    }
}

/* --- Room tiles and the title --------------------------------------------------- */
void video_tileset(uint8_t page, uint8_t tileset)
{
    page &= 1;
    if (tileset >= GFX_TILESETS) {
        tileset = 0;
    }
    if (title_loaded || page_tileset[page] != tileset) {
        dmaCopyVram(gfx_tileset_tiles[tileset], (uint16_t)(VRAM_BG1_TILES + 16 + page * GFX_TILESET_TILES * 16),
                    GFX_TILESET_TILES * 32);
        dmaCopyCGram((const uint8_t *)gfx_tileset_pal[tileset], (uint16_t)((2 + page) * 16), 32);
        page_tileset[page] = tileset;
    }
    if (title_loaded) {
        /* the title's tiles are gone: the other page must load again too */
        page_tileset[page ^ 1] = 0xFF;
        title_loaded = 0;
    }
}

void video_title(void)
{
    if (!title_loaded) {
        dmaCopyVram(gfx_title_tiles, VRAM_BG1_TILES + 16, GFX_TITLE_TILES * 32);
        dmaCopyCGram((const uint8_t *)gfx_title_pal, 2 * 16, GFX_TITLE_PALETTES * 32);
        title_loaded = 1;
        page_tileset[0] = page_tileset[1] = 0xFF;
    }
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
        return sheet == 0xFF ? GFX_SHEET_SCREENS : sheet;   /* secret room: keep */
    }
}

static void load_sheet(uint8_t s)
{
    dmaCopyVram(gfx_sheet_tiles[s], (uint16_t)(VRAM_OBJ + GFX_SPRITE_FIRST_ROW * 512), gfx_sheet_bytes[s]);
    dmaCopyCGram((const uint8_t *)gfx_pal_enemies[s][GFX_TINT_NONE], 128 + OP_ENEMIES * 16, 32);
    dmaCopyCGram((const uint8_t *)gfx_pal_enemies[s][GFX_TINT_RED], 128 + OP_ENEMIES_RED * 16, 32);
    tint6 = 0xFF;   /* palette 6 is a copy of this sheet's enemies: write it again */
    sheet = s;
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

/* Per-frame palettes: the players' flashes and palette 6 (grey wins over
 * yellow when both are wanted in one frame). */
static void frame_palettes(const Sprite *oam)
{
    uint8_t i, want6 = GFX_TINT_NONE;
    for (i = 0; i < MAX_SPRITES; i++) {
        uint8_t pal = (uint8_t)((oam[i].attr & SPR_ATTR_PAL_MASK) >> SPR_ATTR_PAL_SHIFT);
        if (oam[i].y >= SCREEN_H || gfx_cell_pal[oam[i].tile] != OP_ENEMIES) continue;
        if (pal == PAL_GREY) want6 = GFX_TINT_GREY;
        if (pal == PAL_YELLOW && want6 == GFX_TINT_NONE) want6 = GFX_TINT_YELLOW;
    }
    if (want6 != GFX_TINT_NONE && want6 != tint6 && sheet != 0xFF) {
        dmaCopyCGram((const uint8_t *)gfx_pal_enemies[sheet][want6], 128 + OP_ENEMIES_TINT * 16, 32);
        tint6 = want6;
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        uint8_t t = (uint8_t)(g_player_tint[i] == PAL_YELLOW ? GFX_TINT_YELLOW
                            : g_player_tint[i] == PAL_ORANGE ? GFX_TINT_ORANGE : GFX_TINT_NONE);
        if (t != tint_p[i]) {
            /* a flashing player shows player 1's colours tinted, as on the PC */
            const uint16_t *src = t == GFX_TINT_NONE ? gfx_pal_player[i][GFX_TINT_NONE] : gfx_pal_player[0][t];
            dmaCopyCGram((const uint8_t *)src, (uint16_t)(128 + i * 16), 32);
            tint_p[i] = t;
        }
    }
}

static void upload_sprites(const Sprite *oam)
{
    uint8_t i;
    for (i = 0; i < MAX_SPRITES; i++) {
        const Sprite *s = &oam[i];
        uint16_t id = (uint16_t)(i * 4);
        uint8_t slot = s->y < SCREEN_H ? gfx_sheet_slot[sheet][s->tile] : 0xFF;
        if (slot == 0xFF) {
#ifdef BB_DEBUG
            if (s->y < SCREEN_H) g_video_missing++;
#endif
            oamSet(id, 0, SPR_OFF_Y, 0, 0, 0, 0, 0);
            oamSetEx(id, OBJ_LARGE, OBJ_HIDE);
            continue;
        }
        oamSet(id, s->x, (uint8_t)(s->y - 1), SPR_PRIORITY, (uint8_t)((s->attr & SPR_ATTR_FLIP_X) != 0),
               (uint8_t)((s->attr & SPR_ATTR_FLIP_Y) != 0), (uint16_t)((slot & 7) * 2 + (slot >> 3) * 32),
               sprite_palette(gfx_cell_pal[s->tile], (uint8_t)((s->attr & SPR_ATTR_PAL_MASK) >> SPR_ATTR_PAL_SHIFT)));
        oamSetEx(id, OBJ_LARGE, OBJ_SHOW);
    }
}

/* --- Windows and the HUD line ---------------------------------------------------- */
/* HDMA tables are read while the frame is drawn, so they live here. Each
 * window: two lines of 112 entries in repeat mode, two bytes per line. */
static uint8_t window_table[NUM_WINDOWS][2 + SCREEN_H * 2 + 1];
static uint8_t tm_table[8];

static void build_window_table(const ColorWindow *w, uint8_t *t)
{
    uint8_t half, y;
    for (half = 0; half < 2; half++) {
        uint8_t *p = t + half * (1 + 112 * 2);
        *p++ = 0x80 + 112;
        for (y = 0; y < 112; y++) {
            *p++ = w->left[half * 112 + y];
            *p++ = w->right[half * 112 + y];
        }
    }
    t[2 + SCREEN_H * 2] = 0;
}

static uint8_t window_on(const ColorWindow *w)
{
    return (uint8_t)(w->add_r || w->add_g || w->add_b || (w->math & CMATH_HALF));
}

static void upload_windows(const ColorWindow *w)
{
    uint8_t i, on = 0, math = 0, r = 0, g = 0, b = 0, sel = 0;
    for (i = 0; i < NUM_WINDOWS; i++) {
        if (!window_on(&w[i])) continue;
        on++;
        math = w[i].math;
        /* one fixed colour for the screen: the stronger of the two */
        if (w[i].add_r > r) r = w[i].add_r;
        if (w[i].add_g > g) g = w[i].add_g;
        if (w[i].add_b > b) b = w[i].add_b;
        sel = (uint8_t)(sel | (i == 0 ? 0x20 : 0x80));   /* colour window = window 1 / 2 */
        build_window_table(&w[i], window_table[i]);
        snesHdmaSet((uint8_t)(4 + i), (uint8_t)(i == 0 ? PPU_WH0 : PPU_WH2), 1, window_table[i]);
    }
    snesRegWrite(PPU_WOBJSEL, sel);
    snesRegWrite(PPU_WOBJLOG, 0);   /* window 1 or window 2 */
    setColorEffect(on ? CM_MATWIN_ENABLE : CM_NEVER_ENABLE,
                   (uint8_t)(CM_MSCR_BG1 | CM_MSCR_BACK | ((math & CMATH_SUB) ? CM_SUB_MAINSUB : 0) |
                             ((math & CMATH_HALF) ? CM_DIV2_DIVIDE : 0)));
    setColorIntensity(CM_APPLY_RED, r);
    setColorIntensity(CM_APPLY_GREEN, g);
    setColorIntensity(CM_APPLY_BLUE, b);
}

/* BG1 (the room) only from g_window_top down: the main screen register per line. */
static void upload_hud_line(void)
{
    const uint8_t all = BG1_ENABLE | BG3_ENABLE | OBJ_ENABLE, top = g_window_top;
    uint8_t *p = tm_table;
    if (top > 0) {
        uint8_t n = top > 127 ? 127 : top;
        *p++ = n;
        *p++ = (uint8_t)(all & ~BG1_ENABLE);
        if (top > 127) {
            *p++ = (uint8_t)(top - 127);
            *p++ = (uint8_t)(all & ~BG1_ENABLE);
        }
    }
    *p++ = 1;
    *p++ = all;
    *p = 0;
    snesHdmaSet(3, PPU_TM, 0, tm_table);
}

/* --- Frame ------------------------------------------------------------------------ */
void video_init(void)
{
    uint8_t i;
    static uint16_t text_pal[8 * 4];

    setMode(BG_MODE1, BG3_MODE1_PRIORITY_HIGH);
    bgSetMapPtr(0, VRAM_BG1_MAP, SC_32x64);
    bgSetGfxPtr(0, VRAM_BG1_TILES);
    bgSetMapPtr(2, VRAM_BG3_MAP, SC_32x32);
    bgSetDisable(1);
    for (i = 0; i < 8; i++) {
        text_pal[i * 4] = 0;
        text_pal[i * 4 + 1] = text_colors[i];
        text_pal[i * 4 + 2] = 0;             /* outline: black */
        text_pal[i * 4 + 3] = 0;
    }
    bgInitTileSet(2, gfx_font_tiles, (const uint8_t *)text_pal, 0, GFX_FONT_BYTES, sizeof text_pal, BG_4COLORS,
                  VRAM_BG3_TILES);
    setPaletteColor(0, 0);                   /* backdrop: black */
    oamInitGfxAttr(VRAM_OBJ, OBJ_SIZE8_L16);
    oamInit();
    dmaCopyVram(gfx_sprites_shared, VRAM_OBJ, GFX_SPRITE_FIRST_ROW * 1024);
    dmaCopyCGram((const uint8_t *)gfx_pal_player[0][GFX_TINT_NONE], 128 + OP_P1 * 16, 32);
    dmaCopyCGram((const uint8_t *)gfx_pal_player[1][GFX_TINT_NONE], 128 + OP_P2 * 16, 32);
    dmaCopyCGram((const uint8_t *)gfx_pal_effects[GFX_TINT_NONE], 128 + OP_EFFECTS * 16, 32);
    dmaCopyCGram((const uint8_t *)gfx_pal_items[GFX_TINT_NONE], 128 + OP_ITEMS * 16, 32);
    dmaCopyCGram((const uint8_t *)gfx_pal_player[0][GFX_TINT_GREY], 128 + OP_PLAYER_GREY * 16, 32);
    sheet = 0xFF;
    title_loaded = 0;
    page_tileset[0] = page_tileset[1] = 0xFF;
    tint6 = 0xFF;
    for (i = 0; i < NUM_PLAYERS; i++) {
        tint_p[i] = GFX_TINT_NONE;
        g_player_tint[i] = PAL_NONE;
    }
    bg_mark_all(0);
    bg_mark_all(1);
    snesHdmaEnable(HDMA_CHANNEL3 | HDMA_CHANNEL4 | HDMA_CHANNEL5);
    setScreenOn();
}

#ifdef BB_DEBUG
/* Map entries in VRAM that differ from the game's maps (should be 0 after
 * video_frame: a difference means a write to g_bg0/g_bg1 was not marked). */
uint16_t video_check_maps(void)
{
    const uint16_t *vram = snes_vram();
    uint16_t bad = 0;
    uint8_t r, c;
    for (r = 0; r < BG0_ROWS; r++) {
        for (c = 0; c < BG_W; c++) {
            bad = (uint16_t)(bad + (vram[VRAM_BG1_MAP + (r << 5) + c] != room_entry(g_bg0[(r << 5) + c], r)));
        }
    }
    for (r = 0; r < BG_H; r++) {
        for (c = 0; c < BG_W; c++) {
            uint16_t e = g_bg1[(r << 5) + c];
            uint16_t v = (uint16_t)((e & BG_TILE_MASK) ? (e & BG_TILE_MASK) | BG_TIL_PAL((e >> BG_PAL_SHIFT) & 7) | BG_TIL_PRIO : 0);
            bad = (uint16_t)(bad + (vram[VRAM_BG3_MAP + (r << 5) + c] != v));
        }
    }
    return bad;
}
#endif

void video_frame(const Sprite *oam, const ColorWindow *windows)
{
    uint8_t s = wanted_sheet();
    if (s != sheet) {
        load_sheet(s);
    }
    upload_rows();
    bgSetScroll(0, 0, g_scroll_y);
    upload_hud_line();
    upload_windows(windows);
    frame_palettes(oam);
    upload_sprites(oam);
}
