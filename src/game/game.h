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
 * Layer 0 is a 32 x 64 map (two screens on top of each other) that scrolls
 * vertically; layer 1 is a fixed 32 x 28 map. Entries of 8x8 tiles, SNES layout:
 *   bit 15 flip y, bit 14 flip x, bits 10..12 palette, bits 0..9 tile.
 * Layer 0 (level, title logo) is drawn below the sprites,
 * layer 1 (text, HUD) above them. Tile 0 is transparent. */
#define BG_W       32
#define BG_H       28     /* visible rows; size of layer 1 */
#define BG0_ROWS   64     /* rows of layer 0; scrolling wraps around after 512 px */
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
    SFX_READY, SFX_FULL, SFX_DASH,   /* Plus mode: dash available, bar full, dash */
    SFX_COUNT
};

/* --- Music (a track ID; the platform streams the file) --------------------- */
enum { MUSIC_NONE, MUSIC_TITLE, MUSIC_GAME, MUSIC_COUNT };

/* --- Battery-backed save RAM (high score table) --------------------------- */
#define SRAM_SIZE 128   /* high scores (64 bytes) + options (from byte 64) */

/* --- API called by the platform layer ------------------------------------ */
void            game_init(uint16_t seed);                    /* after SRAM was filled */
void            game_tick(const uint16_t pad[NUM_PLAYERS]);  /* exactly once per 60 Hz frame */
const Sprite   *game_oam(void);                              /* MAX_SPRITES entries */
const uint16_t *game_bg(uint8_t layer);   /* layer 0: BG_W * BG0_ROWS, layer 1: BG_W * BG_H entries */
uint16_t        game_bg_scroll_y(void);   /* layer 0 vertical scroll in pixels, 0..BG0_ROWS*8-1 */
uint8_t         game_bg_window_top(void); /* layer 0 is only shown from this screen y down */
uint16_t        game_sfx(void);    /* bit mask of SFX_* requested by the last tick */
uint8_t         game_music(void);  /* MUSIC_* that should be playing now */
uint8_t        *game_sram(void);   /* SRAM_SIZE bytes; platform loads/saves them */
uint8_t         game_sram_dirty(void); /* 1 once after the game changed SRAM */
/* --- Debug switches (development builds only, BB_DEBUG) ------------------- */
#ifdef BB_DEBUG
enum {
    DBG_INFINITE_LIVES,   /* lives never decrease */
    DBG_INVINCIBLE,       /* nothing hurts the players */
    DBG_HITBOXES,         /* platform draws collision boxes (game_debug_boxes) */
    DBG_AIR,              /* platform draws the air currents (game_debug_cell) */
    DBG_FULL_ENERGY,      /* Plus mode: energy bars stay full */
    DBG_CPU_VIEW,         /* platform draws the CPU partner's danger check (game_debug_cpu_view) */
    DBG_COUNT
};

typedef struct {
    uint8_t x, y, w, h;   /* screen pixels */
    uint8_t kind;         /* 0 player, 1 enemy, 2 bubble, 3 item, 4 boss */
} DebugBox;

#define DEBUG_SPAWN_COUNT 14   /* 10 items, water, fire, lightning, a letter */

void            game_debug_set(uint8_t sw, uint8_t on);
uint8_t         game_debug_get(uint8_t sw);
uint8_t         game_debug_boxes(DebugBox *out, uint8_t max);     /* returns the count */
uint8_t         game_debug_cell(uint8_t tx, uint8_t ty);          /* CELL_* byte of the room, 0 outside */
uint8_t         game_debug_room_shown(void);                      /* 1 while a room is on screen, not scrolling */
void            game_debug_spawn(uint8_t what);                   /* 0..DEBUG_SPAWN_COUNT-1, next to player 1 */
const char     *game_debug_spawn_name(uint8_t what);
void            game_debug_start_round(uint8_t round); /* new games start here (1-based) */
void            game_debug_clear_round(void);          /* remove all enemies now */
void            game_debug_skip_story(uint8_t skip);   /* no intro after the title */
void            game_debug_show_ending(uint8_t happy); /* jump to an ending */
void            game_debug_cpu(uint8_t mask);          /* bit i: CPU plays player i+1 (soak tests) */
void            game_debug_options(uint8_t plus, uint8_t rescue, uint8_t p2_cpu);  /* not saved */
void            game_debug_open_options(void);         /* show the options screen */
/* The CPU partner's last danger check: predicted danger positions (boxes)
 * and the name of the action it chose. Returns the number of boxes. */
uint8_t         game_debug_cpu_view(DebugBox *out, uint8_t max, const char **action);
/* Why players lost lives: 0..7 enemy body by type, 8 shot, 9 chaser, 10 boss, 11 rescue bubble ran out. */
#define DEBUG_DEATH_CAUSES 12
const uint16_t *game_debug_death_causes(void);
#endif

#endif /* BB_GAME_H */
