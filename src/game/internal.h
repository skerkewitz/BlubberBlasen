#ifndef BB_INTERNAL_H
#define BB_INTERNAL_H

/*
 * State and helpers shared by the game modules. Old-school layout:
 * fixed-size object tables, all state static, no heap, no floating point.
 */

#include "game/game.h"
#include "game/fixed.h"
#include "game/level_data.h"

/* --- Playfield -------------------------------------------------------------
 * The level (32 x 26 tiles) sits below a two-row HUD. Map row 0 is drawn at
 * screen y FIELD_Y. All object positions are screen space. */
#define HUD_ROWS     2
#define FIELD_Y      (HUD_ROWS * 8)
#define FIELD_BOTTOM (FIELD_Y + LEVEL_H * 8)   /* 224 */
#define WRAP_Y       FIELD_BOTTOM              /* sprite top >= this wraps to the top */

/* Horizontal limits for an object's 12 px wide body (centre x). */
#define BODY_MIN_CX  22
#define BODY_MAX_CX  234   /* right edge at x 239, next to the wall */

/* Object positions: top-left of the 16x16 sprite, unsigned 8.8 fixed point. */
#define PX(v)      ((uint8_t)((uint16_t)(v) >> 8))
#define TO_POS(px) ((uint16_t)((uint16_t)(px) << 8))

/* Player spawn (feet on the floor, bottom-left). */
#define PLAYER_SPAWN_X 24
#define PLAYER_SPAWN_Y (FIELD_BOTTOM - 8 - 16)

/* --- Sprite sheet cells (assets/gfx/sprites.png) --------------------------- */
enum {
    SPR_P_WALK0 = 0, SPR_P_WALK1 = 1, SPR_P_JUMP = 2, SPR_P_FALL = 3, SPR_P_SHOOT = 4,
    SPR_P_DEATH0 = 5, SPR_P_RING = 9,
    SPR_E_WALK0 = 16, SPR_E_ANGRY0 = 18, SPR_E_THROWN0 = 20,
    SPR_BUBBLE0 = 32, SPR_TRAP0 = 35, SPR_BURST0 = 38, SPR_BURST1 = 39, SPR_TRAP_ANGRY = 40,
    SPR_GEM0 = 48, SPR_PTS10 = 50, SPR_PTS500 = 51, SPR_PTS1000 = 52
};

/* --- Top-level state ------------------------------------------------------- */
typedef enum {
    GS_TITLE,
    GS_ROUND_INTRO,
    GS_PLAY,
    GS_PAUSE,
    GS_GAME_OVER,
    GS_NAME_ENTRY,
    GS_HISCORES
} GameState;

/* --- Objects --------------------------------------------------------------- */
typedef enum { PS_OFF, PS_ALIVE, PS_DYING, PS_ENTERING } PlayerState;

typedef struct {
    uint8_t  state;        /* PlayerState */
    uint8_t  facing_left;
    uint8_t  on_ground;
    uint8_t  shoot_cooldown;
    uint8_t  shoot_pose;   /* frames left showing the shoot graphic */
    uint8_t  invulnerable; /* frames left */
    uint8_t  timer;        /* death animation */
    uint16_t x, y;         /* 8.8 */
    fx8_8    vx, vy;
} Player;

#define MAX_ENEMIES 8

typedef struct {
    uint8_t  active;
    uint8_t  facing_left;
    uint8_t  angry;
    uint8_t  entering;     /* moving to its spawn point, AI paused */
    uint8_t  jump_left;    /* pixels left in the current jump */
    uint8_t  gap_jump;
    uint8_t  walk_over_edge;
    uint8_t  target_x, target_y;  /* spawn point (sprite top-left, pixels) */
    uint16_t x, y;         /* 8.8 */
} Enemy;

#define MAX_BUBBLES 16
typedef enum { BS_OFF, BS_SHOOT, BS_FLOAT, BS_TRAPPED } BubbleState;

typedef struct {
    uint8_t  state;        /* BubbleState */
    uint8_t  dir;          /* AIR_* direction it currently drifts in */
    uint8_t  angry;        /* trapped enemy was angry */
    uint8_t  anim;         /* animation phase offset */
    uint16_t age;          /* frames */
    uint16_t x, y;         /* 8.8 */
} Bubble;

#define MAX_ITEMS 16
typedef enum { IT_OFF, IT_THROWN, IT_FALLING, IT_GEM, IT_BURST, IT_POPUP } ItemKind;

typedef struct {
    uint8_t  kind;         /* ItemKind */
    uint8_t  tile;         /* popup graphic */
    int8_t   dx, dy;       /* thrown direction */
    uint16_t timer;        /* frames left in this phase */
    uint16_t x, y;         /* 8.8 */
} Item;

/* --- Shared state (defined in game.c) -------------------------------------- */
extern GameState g_state;
extern uint16_t  g_frame;          /* global frame counter */
extern uint16_t  g_level_frames;   /* frames since the round started */
extern uint8_t   g_level;          /* 0-based index into level_defs */
extern uint8_t   g_round;          /* 1-based round number shown to the player */
extern uint8_t   g_hurry;          /* hurry-up mode active */
extern uint8_t   g_lives;
extern uint8_t   g_score[8];       /* decimal digits, most significant first */
extern uint16_t  g_sfx;

extern Player    g_player;
extern Enemy     g_enemies[MAX_ENEMIES];
extern Bubble    g_bubbles[MAX_BUBBLES];
extern Item      g_items[MAX_ITEMS];

extern uint16_t  g_bg[BG_LAYERS][BG_W * BG_H];

/* --- game.c ----------------------------------------------------------------- */
uint16_t rng_next(void);
uint8_t  chance(uint8_t p256);     /* 1 with probability p256 / 256 */
void     sfx(uint8_t id);
void     oam_add(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr);
uint8_t  overlap(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by, uint8_t w, uint8_t h);
void     player_killed(void);

/* --- level.c ---------------------------------------------------------------- */
void     level_load(uint8_t index);
uint8_t  level_solid_tile(int16_t tx, int16_t ty);
uint8_t  level_air_at(uint8_t sx, uint8_t sy);       /* screen pixel */
uint8_t  level_solid_px(int16_t sx, int16_t sy);     /* screen pixel */
uint8_t  level_on_ground(uint16_t x, uint16_t y);    /* object top-left, 8.8 */
uint8_t  level_fall(uint16_t *x, uint16_t *y, fx8_8 dy);  /* returns 1 when it landed */
uint8_t  level_wall_ahead(uint16_t x, uint16_t y, int8_t dir);
void     level_clamp_x(uint16_t *x);
void     level_wrap_y(uint16_t *y);
uint8_t  level_can_jump_up(uint16_t x, uint16_t y);
uint8_t  level_gap_ahead(uint16_t x, uint16_t y, int8_t dir);
uint8_t  level_can_gap_jump(uint16_t x, uint16_t y, int8_t dir);

/* --- text.c ----------------------------------------------------------------- */
void     bg_clear(uint8_t layer);
void     text_put(uint8_t col, uint8_t row, const char *s, uint8_t pal);
void     text_clear_row(uint8_t row);
void     text_digits(uint8_t col, uint8_t row, const uint8_t *digits, uint8_t n, uint8_t pal);
void     text_number(uint8_t col, uint8_t row, uint8_t value, uint8_t pal);  /* 2 digits */
void     score_add(uint8_t *score, uint8_t pos, uint8_t digit);
int8_t   score_cmp(const uint8_t *a, const uint8_t *b);

/* --- player.c --------------------------------------------------------------- */
void     player_start(void);
void     player_enter_round(void);
uint8_t  player_entering(void);
void     player_update(uint16_t pad, uint16_t pressed);
void     player_draw(void);

/* --- enemy.c ---------------------------------------------------------------- */
void     enemies_clear(void);
void     enemies_spawn_level(void);
void     enemy_spawn(uint16_t x, uint16_t y, uint8_t angry, uint8_t facing_left);
uint8_t  enemies_entering(void);
uint8_t  enemies_count(void);
void     enemies_make_angry(void);
void     enemies_update(void);
void     enemies_draw(void);

/* --- bubble.c --------------------------------------------------------------- */
void     bubbles_clear(void);
void     bubble_shoot(uint16_t x, uint16_t y, uint8_t facing_left);
uint8_t  bubbles_trapped_count(void);
void     bubbles_update(void);
void     bubbles_draw(void);

/* --- item.c ----------------------------------------------------------------- */
void     items_clear(void);
void     item_spawn(uint8_t kind, uint16_t x, uint16_t y, int8_t dx, uint8_t tile);
uint8_t  items_thrown_count(void);
void     items_update(void);
void     items_draw(void);

/* --- hiscore.c -------------------------------------------------------------- */
#define HISCORE_COUNT 5
void     hiscore_init(void);
const uint8_t *hiscore_best(void);
uint8_t  hiscore_rank(const uint8_t *score);    /* 0..4, or HISCORE_COUNT if not good enough */
void     hiscore_insert(uint8_t rank, const char *name, const uint8_t *score, uint8_t round);
void     hiscore_draw_table(uint8_t first_row, uint8_t highlight);

#endif /* BB_INTERNAL_H */
