#ifndef BB_GAME_H
#define BB_GAME_H

/*
 * Interface between the portable game code and the platform layer.
 * Shaped like the SNES: joypad words in; sprite table, two tilemaps and
 * sound requests out; one tick per frame.
 * This header must never include platform headers (raylib etc.).
 */

#include <stdint.h>

/* --- Screen --------------------------------------------------------------- */
#define SCREEN_W 256
#define SCREEN_H 224

/* --- Joypad: SNES bit layout (JOY1L/JOY1H as a 16-bit word) --------------- */
#define PAD_B      0x8000u
#define PAD_Y      0x4000u
#define PAD_SELECT 0x2000u
#define PAD_START  0x1000u
#define PAD_UP     0x0800u
#define PAD_DOWN   0x0400u
#define PAD_LEFT   0x0200u
#define PAD_RIGHT  0x0100u
#define PAD_A      0x0080u
#define PAD_X      0x0040u
#define PAD_L      0x0020u
#define PAD_R      0x0010u

#define NUM_PLAYERS 2

/* --- Palettes ---------------------------------------------------------------
 * Stage 1 uses 24-bit graphics. A "palette" is a tint the platform multiplies
 * the graphic with; palette 0 leaves the graphic unchanged. */
#define PAL_NONE   0u
#define PAL_RED    1u
#define PAL_GREEN  2u
#define PAL_CYAN   3u
#define PAL_YELLOW 4u
#define PAL_PINK   5u
#define PAL_ORANGE 6u
#define PAL_GREY   7u

/* --- Shadow sprite table (OAM-like) --------------------------------------- */
#define MAX_SPRITES     128
#define SPRITE_SIZE     16
#define SPRITE_HIDDEN_Y 240   /* y >= SCREEN_H means "off screen", like on SNES */

#define SPR_ATTR_PAL_MASK  0x0Eu  /* palette 0..7 in bits 1..3 */
#define SPR_ATTR_PAL_SHIFT 1
#define SPR_ATTR_FLIP_X    0x40u
#define SPR_ATTR_FLIP_Y    0x80u

typedef struct {
    uint8_t x;      /* screen x, top-left */
    uint8_t y;      /* screen y, top-left; >= SCREEN_H = hidden */
    uint8_t tile;   /* 16x16 cell in the sprite sheet: row * 16 + column */
    uint8_t attr;   /* palette and flip bits */
} Sprite;

/* --- Background tilemaps -----------------------------------------------------
 * Two layers of 32 x 28 entries of 8x8 tiles, SNES entry layout:
 *   bit 15 flip y, bit 14 flip x, bits 10..12 palette, bits 0..9 tile.
 * Layer 0 (level, title logo) is drawn below the sprites,
 * layer 1 (text, HUD) above them. Tile 0 is transparent. */
#define BG_W       32
#define BG_H       28
#define BG_LAYERS  2
#define BG_TILE_MASK   0x03FFu
#define BG_PAL_SHIFT   10
#define BG_FLIP_X      0x4000u
#define BG_FLIP_Y      0x8000u
#define BG_ENTRY(tile, pal) ((uint16_t)((tile) | ((pal) << BG_PAL_SHIFT)))

/* Background CHR layout (8x8 tiles, 32 per row, 1024 tiles = 256x256 px).
 * The platform builds this "VRAM" from the PNG files in assets/gfx. */
#define CHR_FONT_BASE        0u     /* ASCII 32..95 -> tiles 0..63           */
#define CHR_TILESET_BASE     64u    /* tileset n (0..7): 32 tiles from here  */
#define CHR_TILESET_STRIDE   32u
#define CHR_TILESET_COUNT    8u
#define CHR_TITLE_BASE       320u   /* title logo: 32 x 22 tiles (256x176 px) */
#define CHR_TITLE_W          32u
#define CHR_TITLE_H          22u

/* --- Sound effects (one bit each, requested per tick) --------------------- */
enum {
    SFX_JUMP, SFX_SHOOT, SFX_POP, SFX_TRAP, SFX_KILL, SFX_GEM, SFX_DEATH,
    SFX_START, SFX_CLEAR, SFX_HURRY, SFX_GAMEOVER, SFX_MENU, SFX_PAUSE, SFX_EXTRA,
    SFX_COUNT
};

/* --- Battery-backed save RAM (high score table) --------------------------- */
#define SRAM_SIZE 64

/* --- API called by the platform layer ------------------------------------ */
void            game_init(uint16_t seed);                    /* after SRAM was filled */
void            game_tick(const uint16_t pad[NUM_PLAYERS]);  /* exactly once per 60 Hz frame */
const Sprite   *game_oam(void);                              /* MAX_SPRITES entries */
const uint16_t *game_bg(uint8_t layer);                      /* BG_W * BG_H entries */
uint16_t        game_sfx(void);    /* bit mask of SFX_* requested by the last tick */
uint8_t        *game_sram(void);   /* SRAM_SIZE bytes; platform loads/saves them */
uint8_t         game_sram_dirty(void); /* 1 once after the game changed SRAM */
void            game_debug_start_round(uint8_t round); /* developer option: new games start here (1-based) */

#endif /* BB_GAME_H */
