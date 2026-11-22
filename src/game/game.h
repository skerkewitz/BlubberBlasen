#ifndef BB_GAME_H
#define BB_GAME_H

/*
 * Interface between the portable game code and the platform layer.
 * Joypad words in, one tick per frame. The picture goes out through
 * superdevkit's console API (sdevkit/snes.h): each tick ends with video.c
 * writing OAM, VRAM, CGRAM and the registers, which the SNES shows, and on the
 * PC Rachael, the kit's SNES in C, renders (ADR-0007).
 * The game's own description of the frame (tilemaps, colour windows below,
 * and in development builds its sprite list) stays readable here for replay
 * checksums and debugging.
 * Sound requests out. This header must never include platform headers.
 */

#include <stdint.h>

/* Development builds (BB_DEBUG: the debug switches, the debug view's registrations, the views the
 * tests and recordings read) are every build whose compiler is not dampf, unless the build says
 * BB_NO_DEBUG: CMake does for the public builds (BB_DEBUG_SWITCHES off) and for the console's
 * twin game_console. The ROM never has them; `superdevkit build linux|win64` always does
 * (ADR-0010). */
#if !defined(BB_DEBUG) && !defined(__DAMPF__) && !defined(BB_NO_DEBUG)
#define BB_DEBUG 1
#endif

/* --- Version: major.minor.revision, shown on the title screen -------------- */
#define GAME_VERSION "1.4.0"

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

/* --- Logical palettes ----------------------------------------------------------
 * The game names colours by what they mean; video.c picks the SNES palette
 * (ADR-0006). Sprites: PAL_NONE as drawn, PAL_P2 player 2's colours, PAL_RED
 * the red copy (angry), PAL_GREY the grey copy (frozen, afterimages, the sad
 * ending), PAL_YELLOW / PAL_ORANGE a flash. Text: each PAL_* is a text colour
 * (BG3 palette of the same number). */
#define PAL_NONE   0u
#define PAL_RED    1u
#define PAL_GREEN  2u
#define PAL_CYAN   3u   /* background only; on sprites this slot is PAL_P2 */
#define PAL_P2     3u   /* sprites: player 2 (green swapped for blue) */
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

/* --- Colour windows (SNES windows + colour math) -------------------------------
 * Up to NUM_WINDOWS shapes, each a left and right edge per scanline (on the
 * SNES an HDMA table for the window registers, which video.c builds). Inside
 * a window the colour is added to the background layer 0 and the backdrop,
 * not to sprites or layer 1 (colour math enabled for BG1 and the backdrop
 * only). On a line with left > right the window is empty. The SNES has one
 * fixed colour for the screen: when both windows are on, both use the
 * stronger of their colours.
 * `math` picks the SNES colour math mode (CGADSUB bits 7 and 6):
 *   0                        bg + colour
 *   CMATH_SUB                bg - colour (clamped at 0)
 *   CMATH_HALF               (bg + colour) / 2
 *   CMATH_SUB | CMATH_HALF   (bg - colour) / 2; colour 0 halves the brightness
 * On the SNES the mode is one register for the whole screen (HDMA can change
 * it per line), so windows with different modes must not share a frame.
 * Colour 0 without CMATH_HALF changes nothing: the window is off. */
#define NUM_WINDOWS 2
#define CMATH_SUB   0x01u
#define CMATH_HALF  0x02u

typedef struct {
    uint8_t left[SCREEN_H];          /* first pixel inside */
    uint8_t right[SCREEN_H];         /* last pixel inside */
    uint8_t add_r, add_g, add_b;     /* fixed colour, SNES 5-bit (0..31) */
    uint8_t math;                    /* CMATH_* flags, 0 = add */
} ColorWindow;

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
#define BG_ENTRY(tile, pal) ((uint16_t)((tile) | ((uint16_t)(pal) << BG_PAL_SHIFT)))

/* The game's tile numbers in the tilemaps (8x8 tiles). video.c translates
 * them to the tiles it loaded into VRAM (font, the pages' tilesets, the logo). */
#define CHR_FONT_BASE        0u     /* ASCII 32..95 -> tiles 0..63           */
#define CHR_TILESET_BASE     64u    /* tileset n (0..7): 32 tiles from here  */
#define CHR_TILESET_STRIDE   32u
#define CHR_TILESET_COUNT    8u
#define CHR_TITLE_BASE       320u   /* title logo: 32 x 22 tiles (256x176 px) */
#define CHR_TITLE_W          32u
#define CHR_TITLE_H          22u

/* --- Sound effects ---------------------------------------------------------
 * The game plays them itself, through superdevkit (sdevkit_sfx_play, ADR-0008),
 * on the SNES and on Rachael: the platform only plays Rachael's sound chip.
 * The effects and their numbers come from assets/sfx/sfx.json (the generated
 * sfx.h, internal.h). */

/* --- Music -----------------------------------------------------------------
 * The game plays it through superdevkit (sdevkit_music_play, sdevkit_music_stop);
 * the tracks are listed in superdevkit.toml, their numbers (MUSIC_TITLE,
 * MUSIC_GAME) come from the generated music.h (internal.h). */

/* --- Battery-backed save RAM (high score table) --------------------------- */
#define SRAM_SIZE 128   /* high scores (64 bytes) + options (from byte 64) */

/* --- API called by the platform layer ------------------------------------ */
void            game_init(uint16_t seed);                    /* after the save RAM (sdevkit_sram) was filled */
void            game_tick(const uint16_t pad[NUM_PLAYERS]);  /* exactly once per 60 Hz frame */
/* In the vertical blank after each tick (after sdevkit__vblank on the PC, from
 * the kit's main loop on the SNES, which waits for it): the uploads the tick
 * prepared. */
void            game_vblank(void);
const uint16_t *game_bg(uint8_t layer);   /* layer 0: BG_W * BG0_ROWS, layer 1: BG_W * BG_H entries */
uint16_t        game_bg_scroll_y(void);   /* layer 0 vertical scroll in pixels, 0..BG0_ROWS*8-1 */
uint8_t         game_bg_window_top(void); /* layer 0 is only shown from this screen y down */
/* --- Debug switches (development builds only, BB_DEBUG) ------------------- */
/* The frame's sprites as the game drew them, MAX_SPRITES entries (unused ones
 * at SPRITE_HIDDEN_Y). Development builds only: on the console oam_add writes
 * the SNES's sprite table directly (video.c), this list is for the recordings'
 * checksums and the tests. */
#ifdef BB_DEBUG
const Sprite   *game_oam(void);
/* The sound effects the last tick asked for: bit SFX_<ID> (sfx.h) of each. For the tests. */
uint32_t        game_sfx(void);
/* The colour windows, NUM_WINDOWS entries: their lines are kept in these builds only (on the
 * console the HDMA tables are written directly, video.c). */
const ColorWindow *game_windows(void);
#endif

#ifdef BB_DEBUG
enum {
    DBG_INFINITE_LIVES,   /* lives never decrease */
    DBG_INVINCIBLE,       /* nothing hurts the players */
    DBG_HITBOXES,         /* platform draws collision boxes (game_debug_boxes) */
    DBG_AIR,              /* platform draws the air currents (game_debug_cell) */
    DBG_CPU_VIEW,         /* platform draws the CPU partner's danger check (game_debug_cpu_view) */
    DBG_VACUUM_WALLS,     /* walls stop bubbles inside a vacuum (A/B test) */
    DBG_NO_STICKY,        /* floating bubbles don't cling together (A/B test) */
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
void            game_debug_cpu(uint8_t mask);          /* bit i: CPU plays player i+1 (room tests) */
void            game_debug_options(uint8_t rescue, uint8_t p2_cpu);  /* not saved */
void            game_debug_open_options(void);         /* show the options screen */
/* The CPU partner's last danger check: predicted danger positions (boxes)
 * and the name of the action it chose. Returns the number of boxes. */
uint8_t         game_debug_cpu_view(DebugBox *out, uint8_t max, const char **action);
/* Why players lost lives: 0..7 enemy body by type, 8 shot, 9 chaser, 10 boss, 11 rescue bubble ran out. */
#define DEBUG_DEATH_CAUSES 12
const uint16_t *game_debug_death_causes(void);
/* The goal the CPU partner chose last (priority list in cpu.c), as a name. */
const char     *game_debug_cpu_goal(void);
/* Ends the room now and scrolls to that round (1-based), keeping scores and
 * lives. Only while a round is being played; returns 1 if it did. */
uint8_t         game_debug_goto_round(uint8_t round);

/* Read-only views for the debug panels (platform debug view). */
enum { DOBJ_PLAYER, DOBJ_ENEMY, DOBJ_BUBBLE, DOBJ_ITEM, DOBJ_KINDS };
#define DOBJ_ANGRY       0x01u
#define DOBJ_TRAPPED     0x02u
#define DOBJ_ON_GROUND   0x04u
#define DOBJ_FACING_LEFT 0x08u
#define DOBJ_CPU         0x10u
#define DOBJ_STUNNED     0x20u
#define DOBJ_ENTERING    0x40u   /* enemy walking in, or waiting to (spawn delay) */

typedef struct {
    uint8_t  kind;      /* DOBJ_* */
    uint8_t  slot;      /* index in its table */
    uint8_t  type;      /* enemy type, bubble kind, item kind; players: lives */
    uint8_t  state;     /* player or bubble state; enemies, items: 0 */
    uint16_t x, y;      /* 8.8, sprite top-left */
    int16_t  vx, vy;    /* 8.8 */
    uint16_t timer;     /* bubble age, item timer, player invulnerability, enemy throw timer */
    uint8_t  flags;     /* DOBJ_ANGRY ... */
} DebugObject;

typedef struct {
    const char *state;      /* name of the game state */
    uint8_t  round;         /* 1-based */
    uint16_t round_frames;  /* frames since the round started */
    uint16_t frame;
    uint8_t  hurry;
    uint16_t freeze;        /* Steam Whistle frames left */
    uint8_t  lives[NUM_PLAYERS];
    uint8_t  letters[NUM_PLAYERS];   /* BLASEN bits */
    uint8_t  score[NUM_PLAYERS][8];  /* decimal digits */
} DebugStatus;

uint8_t         game_debug_objects(DebugObject *out, uint8_t max);   /* returns the count */
void            game_debug_status(DebugStatus *out);
/* Names for the numbers in DebugObject: type and state of an object of this kind. */
const char     *game_debug_type_name(uint8_t kind, uint8_t type);
const char     *game_debug_state_name(uint8_t kind, uint8_t state);
#endif

#endif /* BB_GAME_H */
