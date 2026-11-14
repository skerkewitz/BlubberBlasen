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
    SPR_BUBBLE0 = 32, SPR_PUSH = 35, SPR_BURST0 = 38, SPR_BURST1 = 39, SPR_TRAP_ANGRY = 40,
    SPR_BUBBLE_ORANGE0 = 41, SPR_BUBBLE_RED0 = 44,        /* empty bubble about to expire */
    SPR_FOOD0 = 48,                                       /* food tier 0..6 */
    SPR_TRAP_ORANGE0 = 56, SPR_TRAP_RED0 = 59,            /* enemy about to escape */
    SPR_POPUP0 = 64,                                      /* 32x16 pop-ups, two cells each */
    SPR_RING0 = 96, SPR_RING_ORANGE0 = 99, SPR_RING_RED0 = 102,  /* trapped: ring over ... */
    SPR_SMALL_ENEMY0 = 105,                               /* ... a small enemy, by type */
    SPR_BOUNCER0 = 112, SPR_THROWER0 = 118, SPR_THROWER_THROW = 120,
    SPR_ROCK0 = 121, SPR_CHASER0 = 123,
    SPR_SPECIAL_WATER = 128, SPR_SPECIAL_FIRE = 129, SPR_SPECIAL_LIGHTNING = 130,
    SPR_LETTER0 = 131,                                    /* E X T N D */
    SPR_WATER0 = 136, SPR_FIREBALL = 138, SPR_FIRE0 = 139, SPR_BOLT0 = 141,
    SPR_POWERUP0 = 144,                                   /* POWER_* order */
    SPR_BONUS0 = 149,                                     /* bonus food of the round */
    SPR_LIFE1 = 153, SPR_LIFE2 = 154,                     /* one / two life icons */
    SPR_FLUTTER0 = 160, SPR_HOPPER0 = 162, SPR_SPITTER0 = 164, SPR_DROPPER0 = 166,
    SPR_SPIT = 168, SPR_DOWN_SHOT = 169,
    SPR_SMALL_ENEMY4 = 170,                               /* small flutter, hopper, spitter, dropper */
    SPR_BOSS0 = 176, SPR_BOSS1 = 180,                     /* 32x32: four cells each, TL TR BL BR */
    SPR_GIANT_BUBBLE = 184, SPR_SOOT = 188, SPR_DOOR = 189,
    SPR_ITEM_STAR = 190,                                  /* star, wave, whistle, glove, ring */
    SPR_AURA0 = 196                                       /* glow outline of player frames 0..4 */
};

/* Enemy shots (Item.tier of an IT_ROCK). */
enum { SHOT_ROCK, SHOT_SPIT, SHOT_DOWN, SHOT_SOOT };

/* Score pop-ups (index into the pop-up cells). Food tier t uses POPUP_FOOD0 + t. */
enum { POPUP_10 = 0, POPUP_500 = 1, POPUP_FOOD0 = 2, POPUP_100 = 9, POPUP_200 = 10, POPUP_700 = 11 };
#define BONUS_TYPES 4   /* cherry 500, apple 700, melon 1000, cake 2000 */

/* Power-ups and special items (docs/story.md, design guide "Items").
 * Range, speed, rate, shoes and ring stay with a player until they lose a
 * life; the glove lasts for the round; the others act at once. */
enum {
    POWER_RANGE, POWER_SPEED, POWER_RATE, POWER_SHOES, POWER_UMBRELLA,
    POWER_STAR,      /* Soap Star: every enemy turns into a gem */
    POWER_WAVE,      /* Rinse Wave: a flood, every enemy turns into a gem */
    POWER_WHISTLE,   /* Steam Whistle: enemies freeze; touching one defeats it */
    POWER_GLOVE,     /* Fire Glove: fireballs instead of bubbles for the round */
    POWER_RING,      /* Bubble Ring: +100 points per bubble blown */
    POWER_COUNT
};
#define POWER_BIT(p) ((uint16_t)(1u << (p)))
#define GEM_TIER 3   /* enemies defeated by items become 8,000 point gems */

/* EXTEND: six letters per player (bit 0 = E ... bit 5 = D). */
#define EXTEND_ALL 0x3Fu
#define FOOD_TIERS 7    /* 1000, 2000, 4000, ... 64000 */

/* --- Top-level state ------------------------------------------------------- */
typedef enum {
    GS_TITLE,
    GS_STORY,            /* intro pages after the title (docs/story.md) */
    GS_OPTIONS,          /* options screen (Select on the title) */
    GS_TRANSITION,       /* the next round scrolls in */
    GS_ROUND_INTRO,
    GS_PLAY,
    GS_PAUSE,
    GS_CONTINUE,
    GS_GAME_OVER,
    GS_ENDING,           /* after the boss (docs/story.md) */
    GS_NAME_ENTRY,
    GS_HISCORES
} GameState;

/* --- Objects --------------------------------------------------------------- */
typedef enum { PS_OFF, PS_ALIVE, PS_DYING, PS_ENTERING, PS_RESCUE } PlayerState;

/* Options (saved in SRAM). Plus mode = dash, energy, push bubble. */
typedef struct {
    uint8_t plus;          /* 0 Arcade, 1 Plus */
    uint8_t rescue;        /* co-op rescue on/off (both modes) */
    uint8_t p2_cpu;        /* player 2 is a CPU partner */
} Options;
extern Options g_opt;

/* What touched a player (enemies_touch). */
enum { TOUCH_NONE, TOUCH_ENEMY, TOUCH_DEADLY };

typedef struct {
    uint8_t  id;           /* 0 = player 1, 1 = player 2 */
    uint8_t  state;        /* PlayerState; PS_OFF = not in the game */
    uint8_t  lives;
    uint8_t  extends_given;/* score-based extra lives already awarded */
    uint8_t  score[8];     /* decimal digits, most significant first */
    uint8_t  letters;      /* EXTEND letters collected (bit mask) */
    uint16_t powers;       /* power-ups held (POWER_BIT mask) */
    uint8_t  stun;         /* frames left stunned by fire */
    uint8_t  carried;      /* swept along by a water stream */
    uint16_t rescue;       /* frames left in the rescue bubble */
    uint8_t  rescue_dir;   /* AIR_* the rescue bubble drifts in */
    uint8_t  cpu;          /* controlled by the CPU partner (cpu.c) */
    uint8_t  energy;       /* Plus mode: 0..ENERGY_MAX */
    uint8_t  energy_hold;  /* frames before the energy starts to drain */
    uint8_t  dash;         /* Plus mode: frames left in a dash */
    int8_t   dash_dir;
    uint8_t  dash_used;    /* one dash per jump */
    uint8_t  dash_buffer;  /* frames a too-early dash press is kept */
    int8_t   dash_buffer_dir;
    uint16_t last_free_x;  /* dash: last position not inside a wall */
    uint8_t  energy_state; /* 0 low, 1 dash available, 2 full (for the chimes) */
    uint8_t  trail_x[8], trail_y[8];   /* recent positions, for the dash afterimages */
    uint8_t  trail_pos;
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

/* Shooting enemies (thrower, spitter, dropper) stop and flash for
 * ENEMY_WINDUP frames before the shot leaves, then stand for
 * ENEMY_RECOVER frames more. The direction is fixed when the wind-up
 * starts, so the shot can be seen coming and dodged. */
#define ENEMY_WINDUP   30
#define ENEMY_RECOVER  8
#define ENEMY_POSE     (ENEMY_WINDUP + ENEMY_RECOVER)

/* Enemy shot speeds (px per frame, 8.8). */
#define ROCK_SPEED      FX(2, 0)
#define SPIT_SPEED      FX(2, 128)
#define DOWN_SHOT_SPEED FX(2, 128)
#define SPIT_OFFSET     6      /* the spit starts this far in front of the spitter */
#define DOWN_SHOT_OFFSET 8     /* the dropper's shot starts this far below it */

typedef struct {
    uint8_t  active;
    uint8_t  type;         /* ENEMY_TYPE_* */
    uint8_t  facing_left;
    uint8_t  moving_down;  /* bouncer / flutter: 0 up, 1 down, 2 level */
    uint8_t  throw_timer;  /* shooters: frames until the next shot; flutter: until it turns */
    uint8_t  throw_pose;   /* shooters: frames left standing in the shooting pose */
    fx8_8    vy;           /* hopper */
    uint8_t  angry;
    uint8_t  entering;     /* moving to its spawn point, AI paused */
    uint8_t  spawn_delay;  /* frames until it starts entering (hidden until then) */
    uint8_t  jump_left;    /* pixels left in the current jump */
    uint8_t  gap_jump;
    uint8_t  walk_over_edge;
    uint8_t  target_x, target_y;  /* spawn point (sprite top-left, pixels) */
    uint16_t x, y;         /* 8.8 */
} Enemy;

#define MAX_BUBBLES 16
typedef enum { BS_OFF, BS_SHOOT, BS_FLOAT, BS_TRAPPED, BS_POPPING } BubbleState;
typedef enum { BK_NORMAL, BK_WATER, BK_FIRE, BK_LIGHTNING, BK_LETTER } BubbleKind;

typedef struct {
    uint8_t  state;        /* BubbleState */
    uint8_t  dir;          /* AIR_* direction it currently drifts in */
    uint8_t  angry;        /* trapped enemy was angry */
    uint8_t  anim;         /* animation phase offset */
    uint8_t  trapped;      /* holds an enemy (also while popping) */
    uint8_t  etype;        /* type of the trapped enemy */
    uint8_t  kind;         /* BubbleKind */
    uint8_t  letter;       /* BK_LETTER: 0..5 = E X T E N D */
    uint8_t  shot_frames;  /* how long it flies (range power-up) */
    uint8_t  owner;        /* player who blew it */
    fx8_8    shot_speed;   /* how fast it flies (speed power-up) */
    fx8_8    vx, vy;       /* floating: momentum from a push bubble, fades out */
    uint8_t  pop_timer;    /* frames until a popping bubble bursts */
    uint16_t age;          /* frames */
    uint16_t x, y;         /* 8.8 */
} Bubble;

#define MAX_ITEMS 16
typedef enum { IT_OFF, IT_THROWN, IT_FOOD, IT_BURST, IT_POPUP, IT_ROCK, IT_POWERUP, IT_BONUS } ItemKind;

typedef struct {
    uint8_t  kind;         /* ItemKind */
    uint8_t  tier;         /* food tier (thrown, food) or POPUP_* (pop-up) */
    uint16_t timer;        /* frames left in this phase */
    uint16_t x, y;         /* 8.8 */
    fx8_8    vx, vy;       /* thrown enemy */
} Item;

/* --- Debug switches: always 0 in builds without BB_DEBUG --------------------- */
#ifdef BB_DEBUG
extern uint8_t g_dbg[DBG_COUNT];
#define DBG(sw) (g_dbg[sw])
extern uint16_t g_dbg_deaths[DEBUG_DEATH_CAUSES];
extern uint8_t  g_dbg_touch;   /* what enemies_touch found last */
#define DBG_TOUCH(cause) (g_dbg_touch = (uint8_t)(cause))
#else
#define DBG(sw) 0
#define DBG_TOUCH(cause) ((void)0)
#endif

/* --- Shared state (defined in game.c) -------------------------------------- */
extern GameState g_state;
extern uint16_t  g_frame;          /* global frame counter */
extern uint16_t  g_level_frames;   /* frames since the round started */
extern uint8_t   g_level;          /* 0-based index into level_defs */
extern uint8_t   g_round;          /* 1-based round number shown to the player */
extern uint8_t   g_hurry;          /* hurry-up mode active */
extern uint32_t  g_sfx;
extern uint8_t   g_skip_rounds;    /* >0: end the round now and advance that many */
extern uint16_t  g_freeze;         /* frames left of the Steam Whistle */

/* Hidden counters that unlock power-ups (shared by both players). */
typedef struct {
    uint8_t  blown;        /* bubbles blown          -> range candy  */
    uint8_t  popped;       /* bubbles popped         -> speed candy  */
    uint8_t  jumps;        /* jumps                  -> rate candy   */
    uint8_t  water;        /* water bubbles popped   -> umbrella     */
    uint16_t walked;       /* pixels walked          -> shoes        */
    uint8_t  food;         /* food items collected   -> Soap Star    */
    uint8_t  water_kills;  /* enemies washed away    -> Rinse Wave   */
    uint8_t  defeated;     /* enemies defeated       -> Steam Whistle */
    uint8_t  fire_kills;   /* enemies burnt          -> Fire Glove   */
    uint8_t  candies;      /* candies collected      -> Bubble Ring  */
    uint16_t queued;       /* unlocked power-ups waiting (POWER_BIT mask) */
} Counters;
extern Counters  g_count;

extern Player    g_players[NUM_PLAYERS];
extern Enemy     g_enemies[MAX_ENEMIES];
extern Bubble    g_bubbles[MAX_BUBBLES];
extern Item      g_items[MAX_ITEMS];

extern uint16_t  g_bg0[BG_W * BG0_ROWS];   /* level / title, scrolls */
extern uint16_t  g_bg1[BG_W * BG_H];       /* text and HUD */
extern uint16_t  g_scroll_y;               /* layer 0 scroll */
extern uint8_t   g_window_top;             /* layer 0 hidden above this y */

/* Layer 0 holds two screens ("pages"), 32 rows apart. */
#define PAGE_ROWS  32
#define PAGE_PX    (PAGE_ROWS * 8)

/* --- game.c ----------------------------------------------------------------- */
uint16_t rng_next(void);
uint8_t  chance(uint8_t p256);     /* 1 with probability p256 / 256 */
void     sfx(uint8_t id);
void     oam_add(uint8_t x, uint8_t y, uint8_t tile, uint8_t attr);
uint8_t  overlap(uint16_t ax, uint16_t ay, uint16_t bx, uint16_t by, uint8_t w, uint8_t h);
void     player_killed(Player *p);

/* --- level.c ---------------------------------------------------------------- */
void     level_load(uint8_t index, uint8_t page);   /* collision + draw into a page */
uint8_t  level_solid_tile(int16_t tx, int16_t ty);
uint8_t  level_air_at(uint8_t sx, uint8_t sy);       /* screen pixel */
uint8_t  level_cell(uint8_t tx, uint8_t ty);         /* raw CELL_* byte, 0 outside */
uint8_t  level_solid_px(int16_t sx, int16_t sy);     /* screen pixel */
uint8_t  level_on_ground(uint16_t x, uint16_t y);    /* object top-left, 8.8 */
uint8_t  level_fall(uint16_t *x, uint16_t *y, fx8_8 dy);  /* returns 1 when it landed */
uint8_t  level_wall_ahead(uint16_t x, uint16_t y, int8_t dir);
void     level_clamp_x(uint16_t *x);
void     level_wrap_y(uint16_t *y);
uint8_t  level_can_jump_up(uint16_t x, uint16_t y);
uint8_t  level_gap_ahead(uint16_t x, uint16_t y, int8_t dir);
void     level_opening(uint16_t *x, uint16_t *y);  /* where special bubbles enter */
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
void     player_start(Player *p);
void     player_enter_round(Player *p);
uint8_t  players_entering(void);
uint8_t  players_in_game(void);
const Player *player_nearest(uint16_t x, uint16_t y);
void     player_update(Player *p, uint16_t pad, uint16_t pressed);
void     player_draw(const Player *p);
uint8_t  player_dash_invulnerable(const Player *p);

/* Plus mode energy (design guide "Plus mode"; tuning values there). */
#define ENERGY_MAX        96
#define ENERGY_PER_ENEMY  16
#define ENERGY_COMBO      8      /* extra per further enemy in the same chain */
#define ENERGY_HOLD       180    /* frames before the bar drains after a gain */
#define DASH_COST         16
#define DASH_FRAMES       12     /* 12 x 4.5 px = 54 px, about 7 tiles */
#define DASH_GRACE        30     /* invulnerable after a dash: time to react and jump clear */
void     energy_gain(uint8_t player, uint8_t amount);
void     energy_tick(void);      /* drain; call only while a round is being played */

/* --- enemy.c ---------------------------------------------------------------- */
void     enemies_clear(void);
void     enemies_spawn_level(void);
void     enemy_spawn(uint16_t x, uint16_t y, uint8_t type, uint8_t angry, uint8_t facing_left);
uint8_t  enemies_entering(void);
uint8_t  enemies_count(void);
void     enemies_make_angry(void);
void     enemies_calm_down(void);
uint8_t  enemies_touch(uint16_t x, uint16_t y);   /* TOUCH_*: an enemy body, or something always deadly */
uint8_t  enemy_shot_in(const Enemy *e);           /* frames until its shot leaves; 0 = not winding up */
void     enemies_to_gems(void);                   /* items: every enemy becomes a gem */
void     enemies_defeat_at(uint16_t x, uint16_t y, uint8_t player); /* Steam Whistle: touching frozen enemies */

/* Hurry Up chaser: invincible, flies through walls towards the player. */
void     chaser_reset(void);
void     chaser_spawn(void);
uint8_t  chaser_active(void);
uint8_t  chaser_get(uint8_t i, uint16_t *x, uint16_t *y);   /* 1 if chaser i is out */
void     chaser_update(void);
void     chaser_draw(void);
void     enemies_update(void);
void     enemies_draw(void);

/* --- bubble.c --------------------------------------------------------------- */
void     bubbles_clear(void);
void     bubble_shoot(uint16_t x, uint16_t y, uint8_t facing_left, uint16_t powers);
void     bubbles_trapped_to_gems(void);   /* items: trapped enemies become gems */
void     bubble_spawn_special(uint8_t kind, uint8_t letter, uint16_t x, uint16_t y);
uint8_t  push_shoot(uint8_t player, uint16_t x, uint16_t y, uint8_t facing_left);   /* Plus mode; 0 = one already flying */
uint8_t  push_active(uint8_t player);
void     bubble_pop(Bubble *b, int8_t dir, uint8_t player);   /* starts a chain reaction */
uint8_t  bubbles_trapped_count(void);
void     bubbles_update(void);
void     bubbles_draw(void);

/* --- item.c ----------------------------------------------------------------- */
void     items_clear(void);
void     item_spawn(uint8_t kind, uint16_t x, uint16_t y, uint8_t tier);
void     item_spawn_thrown(uint16_t x, uint16_t y, int8_t dir, uint8_t tier);
void     item_spawn_shot(uint8_t shot, uint16_t x, uint16_t y, int8_t dir);   /* SHOT_* */
void     item_spawn_soot(uint16_t x, uint16_t y, fx8_8 vx, fx8_8 vy);
uint8_t  powerup_tile(uint8_t power);
uint8_t  small_enemy_tile(uint8_t type);   /* picture inside a trapped bubble */
void     item_spawn_powerup(uint8_t power, uint16_t x, uint16_t y);
void     items_clear_rocks(void);
void     bonus_round_tick(void);    /* places the round's bonus food */
void     items_update(void);
void     items_draw(void);

/* --- cpu.c ------------------------------------------------------------------ */
void     cpu_reset(void);
uint16_t cpu_pad(Player *p);   /* the CPU partner's joypad word for this frame */

/* --- effect.c --------------------------------------------------------------- */
void     effects_clear(void);                     /* new round */
void     effect_start(uint8_t kind, uint16_t x, uint16_t y, int8_t dir, uint8_t player);
void     effect_start_glove(uint16_t x, uint16_t y, int8_t dir, uint8_t player);  /* fireball shot */
void     effects_update(void);
void     effects_draw(void);
void     extend_letters_earned(uint8_t count);    /* queue letter bubbles */
void     extend_collect(uint8_t player, uint8_t letter);
void     counters_check(void);                    /* unlock power-ups */
void     powerup_round_tick(void);                /* place a queued power-up */

/* --- boss.c ----------------------------------------------------------------- */
void     boss_reset(void);
void     boss_spawn(void);
uint8_t  boss_fighting(void);
uint8_t  boss_hp(void);
uint8_t  boss_defeated(void);           /* the giant bubble has been popped */
uint16_t boss_center_x(void);
uint16_t boss_center_y(void);
uint8_t  boss_touch(uint16_t x, uint16_t y);
uint8_t  boss_hit_by_bolt(uint16_t x, uint16_t y);
void     boss_update(void);
void     boss_draw(void);

/* --- hiscore.c (save RAM: high scores and options) ---------------------------- */
#define HISCORE_COUNT 5
void     options_load(void);
void     options_save(void);
void     hiscore_init(void);
const uint8_t *hiscore_best(void);
uint8_t  hiscore_rank(const uint8_t *score);    /* 0..4, or HISCORE_COUNT if not good enough */
void     hiscore_insert(uint8_t rank, const char *name, const uint8_t *score, uint8_t round);
void     hiscore_draw_table(uint8_t first_row, uint8_t highlight);

#endif /* BB_INTERNAL_H */
