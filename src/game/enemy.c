/*
 * Enemies.
 *  - Walker: walk, turn at walls, decide at edges (walk over, jump the gap,
 *    turn), jump up to the platform above when the player is higher.
 *    Ported from AiEnemySystem of the Java version.
 *  - Thrower: a walker that stops and throws a rock when the player is at
 *    its height and in front of it.
 *  - Bouncer: flies diagonally through platforms, bouncing off the walls,
 *    the floor and the ceiling.
 *  - Flutter: flies freely through platforms and changes direction often.
 *  - Hopper: bounces in arcs on a spring, rebounds off walls.
 *  - Spitter: fast walker, weak jumper, spits fireballs.
 *  - Dropper: patrols left and right, drops off edges, shoots straight down.
 *  - Shield: a slow walker behind a steel plate. It neither jumps up nor
 *    over gaps. A bubble from the front bursts on the plate; one from behind
 *    traps it. A dash through it knocks the plate off: it stands dazed for
 *    a moment and walks on as a plain walker.
 *  All three shooters stop and flash for ENEMY_WINDUP frames before a shot
 *  (internal.h), so a player can see it coming.
 *  - Chaser: appears some time after HURRY UP, is invincible, flies through
 *    everything towards the player and gets faster.
 */

#include "game/internal.h"

#define SPEED           FX(1, 0)
#define SPEED_ANGRY     FX(1, 77)    /* 1.3 px/frame */
#define FLY_SPEED       FX(0, 192)
#define FLY_SPEED_ANGRY FX(1, 0)
#define JUMP_PIXELS     44
#define GAP_JUMP_PIXELS 24
#define THINK_FRAMES    120          /* how often "jump up?" is considered */
#define THROW_COOLDOWN  120          /* + random 0..127 */
#define SPEED_SPITTER   FX(1, 128)
#define SPEED_DROPPER   FX(0, 192)
#define SPEED_SHIELD    FX(0, 160)   /* 0.63 px/frame: the plate is heavy */
#define ANGRY_EXTRA     FX(0, 77)
#define HOP_SPEED       FX(0, 204)   /* sideways while hopping */
#define HOP_JUMP        FX(2, 128)   /* + random up to 0.5 */
#define HOP_GRAVITY     FX(0, 28)
#define HOP_MAX_FALL    FX(2, 128)
#define SPIT_COOLDOWN   90           /* + random 0..63 */
#define DROP_COOLDOWN   80           /* + random 0..63 */
#define FLY_TOP         (FIELD_Y + 8)
#define FLY_BOTTOM      (FIELD_BOTTOM - 8 - 16)

#define CHASER_SPEED0   FX(0, 96)
#define CHASER_SPEED_MAX FX(2, 0)

static uint8_t think_timer;

/* One chaser per living player, each hunting its own player. */
static struct {
    uint8_t  active;
    fx8_8    speed;
    uint16_t x, y;
} chasers[NUM_PLAYERS];

void enemies_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies[i].active = 0;
    }
    think_timer = 0;
    chaser_reset();
}

static Enemy *alloc_enemy(uint8_t type)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies[i].active) {
            Enemy *e = &g_enemies[i];
            e->active = 1;
            e->type = type;
            e->angry = 0;
            e->entering = 0;
            e->spawn_delay = 0;
            e->jump_left = 0;
            e->gap_jump = 0;
            e->walk_over_edge = 0;
            e->moving_down = 1;
            e->throw_timer = (uint8_t)(THROW_COOLDOWN >> 1);
            e->throw_pose = 0;
            e->vy = 0;
            e->stun = 0;
            return e;
        }
    }
    return 0;
}

/* Enemies of the level drop in from the top to their spawn points. */
void enemies_spawn_level(void)
{
    const LevelDef *def = &level_defs[g_level];
    uint8_t i;

    for (i = 0; i < def->enemy_count; i++) {
        const LevelEnemy *le = &def->enemies[i];
        Enemy *e = alloc_enemy(le->type);
        if (!e) {
            return;
        }
        e->facing_left = (uint8_t)((le->flags & LEVEL_ENEMY_FACE_LEFT) != 0);
        e->target_x = (uint8_t)(le->x << 3);
        e->target_y = (uint8_t)(FIELD_Y + (le->y << 3) - 8);
        e->x = TO_POS(e->target_x);
        e->y = TO_POS(FIELD_Y);
        e->entering = 1;
        e->spawn_delay = le->delay;
    }
}

void enemy_spawn(uint16_t x, uint16_t y, uint8_t type, uint8_t angry, uint8_t facing_left)
{
    Enemy *e = alloc_enemy(type);
    if (e) {
        e->x = x;
        e->y = y;
        e->angry = angry;
        e->facing_left = facing_left;
    }
}

uint8_t enemies_entering(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (g_enemies[i].active && g_enemies[i].entering) {
            return 1;
        }
    }
    return 0;
}

uint8_t enemies_count(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        n = (uint8_t)(n + g_enemies[i].active);
    }
    return n;
}

void enemies_make_angry(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies[i].angry = 1;
    }
}

/* Soap Star / Rinse Wave: every enemy in the room becomes a gem. The
 * caller plays the sound (multikill_award). */
uint8_t enemies_to_gems(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering) {
            item_spawn_thrown(e->x, e->y, (int8_t)((i & 1) ? -1 : 1), GEM_TIER);
            e->active = 0;
            n++;
        }
    }
    return n;
}

/* Steam Whistle: a frozen enemy touched by a player is defeated. The
 * caller plays the sound (multikill_award). */
uint8_t enemies_defeat_at(uint16_t x, uint16_t y)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(x, y, e->x, e->y, 12, 12)) {
            item_spawn_thrown(e->x, e->y, (int8_t)(PX(e->x) < PX(x) ? -1 : 1), GEM_TIER - 1);
            e->active = 0;
            n++;
        }
    }
    return n;
}

/* The plate covers the side the shield enemy faces: a bubble whose left
 * edge is at bx (8.8) and that is on that side bursts on it. */
uint8_t enemy_shield_blocks(const Enemy *e, uint16_t bx)
{
    if (e->type != ENEMY_TYPE_SHIELD) {
        return 0;
    }
    return (uint8_t)(e->facing_left ? PX(bx) < PX(e->x) : PX(bx) > PX(e->x));
}

/* A dash through a shield enemy knocks its plate off, from either side. */
void enemies_dash_hit(uint16_t x, uint16_t y)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && e->type == ENEMY_TYPE_SHIELD && overlap(x, y, e->x, e->y, 14, 14)) {
            e->type = ENEMY_TYPE_WALKER;
            e->stun = SHIELD_STUN;
            e->jump_left = 0;
            item_spawn(IT_BURST, e->x, e->y, 0);
            sfx(SFX_TRAP);
        }
    }
}

void enemies_calm_down(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies[i].angry = 0;
    }
}

/* Shots, the chaser and the boss always kill (TOUCH_DEADLY); a regular
 * enemy's body (TOUCH_ENEMY) traps instead when co-op rescue is on. */
uint8_t enemies_touch(uint16_t x, uint16_t y)
{
    uint8_t i;
    if (boss_touch(x, y)) {
        DBG_TOUCH(10);
        return TOUCH_DEADLY;
    }
    for (i = 0; i < MAX_ITEMS; i++) {
        const Item *it = &g_items[i];
        if (it->kind == IT_ROCK && overlap(x, y, it->x, it->y, 8, 8)) {
            DBG_TOUCH(8);
            return TOUCH_DEADLY;
        }
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (chasers[i].active && overlap(x, y, chasers[i].x, chasers[i].y, 10, 10)) {
            DBG_TOUCH(9);
            return TOUCH_DEADLY;
        }
    }
    for (i = 0; i < MAX_ENEMIES && !g_freeze; i++) {   /* frozen enemies are harmless */
        const Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(x, y, e->x, e->y, 10, 12)) {
            DBG_TOUCH(e->type & 7);
            return TOUCH_ENEMY;
        }
    }
    return TOUCH_NONE;
}

static void step_towards(uint16_t *v, uint8_t target)
{
    uint8_t px = PX(*v);
    if (px < target) {
        *v = TO_POS(px + 1);
    } else if (px > target) {
        *v = TO_POS(px - 1);
    }
}

uint8_t small_enemy_tile(uint8_t type)
{
    if (type >= ENEMY_TYPE_FLUTTER && type <= ENEMY_TYPE_SHIELD) {
        return (uint8_t)(SPR_SMALL_ENEMY4 + type - ENEMY_TYPE_FLUTTER);
    }
    return (uint8_t)(SPR_SMALL_ENEMY0 + (type >= 1 && type <= 3 ? type - 1 : 0));
}

static fx8_8 walk_speed(const Enemy *e)
{
    fx8_8 base = e->type == ENEMY_TYPE_SPITTER ? SPEED_SPITTER : e->type == ENEMY_TYPE_DROPPER ? SPEED_DROPPER :
                 e->type == ENEMY_TYPE_SHIELD ? SPEED_SHIELD : SPEED;
    return (fx8_8)(e->angry ? base + ANGRY_EXTRA : base);
}

static void step_x(Enemy *e, int8_t dir)
{
    fx8_8 speed = walk_speed(e);
    if (!level_wall_ahead(e->x, e->y, dir)) {
        e->x = (uint16_t)(dir < 0 ? e->x - (uint16_t)speed : e->x + (uint16_t)speed);
    }
}

/* Some living player at the same height, in front of the enemy. */
static uint8_t player_in_line(const Enemy *e, int8_t dir)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        int16_t dy, dx;
        if (p->state != PS_ALIVE) {
            continue;
        }
        dy = (int16_t)((int16_t)PX(e->y) - (int16_t)PX(p->y));
        dx = (int16_t)((int16_t)PX(p->x) - (int16_t)PX(e->x));
        if (dy > -8 && dy < 8 && ((dir < 0 && dx < 0) || (dir > 0 && dx > 0))) {
            return 1;
        }
    }
    return 0;
}

/* Some living player below the enemy, roughly in its column. */
static uint8_t player_below(const Enemy *e)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        int16_t dx;
        if (p->state != PS_ALIVE || PX(p->y) <= PX(e->y) + 8) {
            continue;
        }
        dx = (int16_t)((int16_t)PX(p->x) - (int16_t)PX(e->x));
        if (dx > -12 && dx < 12) {
            return 1;
        }
    }
    return 0;
}

static uint8_t is_shooter(uint8_t type)
{
    return (uint8_t)(type == ENEMY_TYPE_THROWER || type == ENEMY_TYPE_SPITTER || type == ENEMY_TYPE_DROPPER);
}

uint8_t enemy_shot_in(const Enemy *e)
{
    if (!e->active || !is_shooter(e->type) || e->throw_pose <= ENEMY_RECOVER) {
        return 0;
    }
    return (uint8_t)(e->throw_pose - ENEMY_RECOVER);
}

static void release_shot(const Enemy *e, int8_t dir)
{
    switch (e->type) {
    case ENEMY_TYPE_THROWER:
        item_spawn_shot(SHOT_ROCK, e->x, e->y, dir);
        break;
    case ENEMY_TYPE_SPITTER:
        item_spawn_shot(SHOT_SPIT, (uint16_t)(dir < 0 ? e->x - TO_POS(SPIT_OFFSET) : e->x + TO_POS(SPIT_OFFSET)), e->y, dir);
        break;
    default:
        item_spawn_shot(SHOT_DOWN, e->x, (uint16_t)(e->y + TO_POS(DOWN_SHOT_OFFSET)), 0);
        break;
    }
}

/* Thrower, spitter, dropper: once a player is in the line of fire, stop,
 * wind up (the enemy flashes, see enemies_draw), shoot in the direction
 * it faced when the wind-up began, stand a moment longer. Returns 1 while
 * the enemy stands in its shooting pose. */
static uint8_t try_shoot(Enemy *e, int8_t dir)
{
    uint8_t target;

    if (e->throw_pose) {
        e->throw_pose--;
        if (e->throw_pose == ENEMY_RECOVER) {
            release_shot(e, dir);
        }
        return 1;
    }
    if (e->throw_timer) {
        e->throw_timer--;
        return 0;
    }
    if (!level_on_ground(e->x, e->y)) {
        return 0;
    }
    target = (uint8_t)(e->type == ENEMY_TYPE_DROPPER ? player_below(e) : player_in_line(e, dir));
    if (!target) {
        return 0;
    }
    e->throw_pose = ENEMY_POSE;
    e->throw_timer = (uint8_t)(e->type == ENEMY_TYPE_THROWER ? THROW_COOLDOWN + (rng_next() & 127) :
                               e->type == ENEMY_TYPE_SPITTER ? SPIT_COOLDOWN + (rng_next() & 63) :
                                                               DROP_COOLDOWN + (rng_next() & 63));
    return 1;
}

static void think_walker(Enemy *e, uint8_t think_now)
{
    int8_t dir = (int8_t)(e->facing_left ? -1 : 1);
    int16_t ey = PX(e->y);
    int16_t py = PX(player_nearest(e->x, e->y)->y);
    uint8_t player_above = (uint8_t)(ey > py + 16);
    uint8_t player_below = (uint8_t)(ey < py - 32);

    uint8_t can_climb = (uint8_t)(e->type == ENEMY_TYPE_WALKER || e->type == ENEMY_TYPE_THROWER);
    uint8_t can_gap_jump = (uint8_t)(e->type != ENEMY_TYPE_DROPPER && e->type != ENEMY_TYPE_SHIELD);

    if (is_shooter(e->type) && !e->jump_left && try_shoot(e, dir)) {
        return;
    }
    if (e->type == ENEMY_TYPE_DROPPER) {
        e->walk_over_edge = 1;   /* always walks off edges */
    }

    if (e->jump_left) {
        e->jump_left--;
        e->y = (uint16_t)(e->y - TO_POS(1));
        if (e->gap_jump) {
            step_x(e, dir);
        }
    } else if (level_on_ground(e->x, e->y)) {
        e->gap_jump = 0;
        if (e->type == ENEMY_TYPE_DROPPER) {
            e->walk_over_edge = 1;
        }
        if (can_climb && think_now && player_above && level_can_jump_up(e->x, e->y) && chance(102)) {
            e->jump_left = JUMP_PIXELS;
        } else {
            if (level_gap_ahead(e->x, e->y, dir)) {
                if ((player_below && chance(204)) || chance(26)) {
                    e->walk_over_edge = 1;
                }
                if (!e->walk_over_edge) {
                    if (can_gap_jump && level_can_gap_jump(e->x, e->y, dir) && chance(64)) {
                        e->jump_left = GAP_JUMP_PIXELS;
                        e->gap_jump = 1;
                    } else {
                        dir = (int8_t)-dir;
                    }
                }
            }
            if (level_wall_ahead(e->x, e->y, dir)) {
                dir = (int8_t)-dir;
                e->walk_over_edge = 0;
            }
            step_x(e, dir);
        }
    } else {
        e->walk_over_edge = 0;
        if (e->gap_jump) {
            step_x(e, dir);
        }
        (void)level_fall(&e->x, &e->y, FX(1, 0));
    }

    e->facing_left = (uint8_t)(dir < 0);
    level_clamp_x(&e->x);
    level_wrap_y(&e->y);
}

/* Bouncer and flutter: straight lines through everything, bouncing off the
 * room's edges. The flutter also picks a new direction now and then. */
static void think_flyer(Enemy *e)
{
    fx8_8 speed = e->angry ? FLY_SPEED_ANGRY : FLY_SPEED;
    uint8_t cx;

    if (e->type == ENEMY_TYPE_FLUTTER) {
        if (e->throw_timer) {
            e->throw_timer--;
        } else {
            uint16_t r = rng_next();
            e->facing_left = (uint8_t)(r & 1);
            e->moving_down = (uint8_t)((r >> 1) & 3);   /* 0 up, 1 down, 2 and 3 level */
            if (e->moving_down == 3) {
                e->moving_down = 2;
            }
            e->throw_timer = (uint8_t)(32 + ((r >> 3) & 63));
        }
    }

    e->x = (uint16_t)(e->facing_left ? e->x - (uint16_t)speed : e->x + (uint16_t)speed);
    if (e->moving_down == 1) {
        e->y = (uint16_t)(e->y + (uint16_t)speed);
    } else if (e->moving_down == 0) {
        e->y = (uint16_t)(e->y - (uint16_t)speed);
    }

    cx = (uint8_t)(PX(e->x) + 8);
    if (cx < BODY_MIN_CX || PX(e->x) >= 0xF0u) {
        e->facing_left = 0;
        e->x = TO_POS(BODY_MIN_CX - 8);
    } else if (cx > BODY_MAX_CX) {
        e->facing_left = 1;
        e->x = TO_POS(BODY_MAX_CX - 8);
    }
    if (PX(e->y) < FLY_TOP || PX(e->y) >= WRAP_Y) {
        e->moving_down = 1;
        e->y = TO_POS(FLY_TOP);
    } else if (PX(e->y) > FLY_BOTTOM) {
        e->moving_down = 0;
        e->y = TO_POS(FLY_BOTTOM);
    }
}

/* Hopper: jumps again as soon as it lands, sometimes towards a player. */
static void think_hopper(Enemy *e)
{
    fx8_8 speed = (fx8_8)(e->angry ? HOP_SPEED + ANGRY_EXTRA : HOP_SPEED);
    int8_t dir;

    if (e->vy >= 0 && level_on_ground(e->x, e->y)) {
        e->vy = (fx8_8)-(HOP_JUMP + (fx8_8)(rng_next() & 127));
        if (chance(128)) {
            e->facing_left = (uint8_t)(PX(player_nearest(e->x, e->y)->x) < PX(e->x));
        }
    }
    dir = (int8_t)(e->facing_left ? -1 : 1);
    if (level_wall_ahead(e->x, e->y, dir)) {
        e->facing_left = (uint8_t)!e->facing_left;
    } else {
        uint16_t before = e->x;
        e->x = (uint16_t)(dir < 0 ? e->x - (uint16_t)speed : e->x + (uint16_t)speed);
        level_clamp_x(&e->x);
        if (e->x == before) {
            e->facing_left = (uint8_t)!e->facing_left;
        }
    }

    e->vy = (fx8_8)(e->vy + HOP_GRAVITY);
    if (e->vy > HOP_MAX_FALL) {
        e->vy = HOP_MAX_FALL;
    }
    if (e->vy < 0) {
        e->y = (uint16_t)(e->y + (uint16_t)e->vy);
        if (PX(e->y) < FLY_TOP || PX(e->y) >= WRAP_Y) {
            e->y = TO_POS(FLY_TOP);
            e->vy = 0;
        }
    } else if (level_fall(&e->x, &e->y, e->vy)) {
        e->vy = 0;
    }
}

void enemies_update(void)
{
    uint8_t i;
    uint8_t think_now = 0;

    if (++think_timer >= THINK_FRAMES) {
        think_timer = 0;
        think_now = 1;
    }

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (!e->active) {
            continue;
        }
        if (e->entering) {
            if (e->spawn_delay) {
                e->spawn_delay--;
                continue;
            }
            step_towards(&e->x, e->target_x);
            step_towards(&e->y, e->target_y);
            if (PX(e->x) == e->target_x && PX(e->y) == e->target_y) {
                e->entering = 0;
            }
            continue;
        }
        if (g_state != GS_PLAY || g_freeze) {
            continue;
        }
        if (e->stun) {
            e->stun--;     /* dazed: stands (or drops) where it is */
            if (!level_on_ground(e->x, e->y)) {
                (void)level_fall(&e->x, &e->y, FX(1, 0));
                level_wrap_y(&e->y);
            }
            continue;
        }
        switch (e->type) {
        case ENEMY_TYPE_BOUNCER:
        case ENEMY_TYPE_FLUTTER:
            think_flyer(e);
            break;
        case ENEMY_TYPE_HOPPER:
            think_hopper(e);
            break;
        default:
            think_walker(e, think_now);
            break;
        }
    }
}

void enemies_draw(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        uint8_t tile;
        uint8_t attr = (uint8_t)(e->facing_left ? SPR_ATTR_FLIP_X : 0u);
        uint8_t fast = (uint8_t)(e->angry ? 3 : 4);

        if (!e->active || e->spawn_delay) {
            continue;
        }
        switch (e->type) {
        case ENEMY_TYPE_BOUNCER:
            tile = (uint8_t)(SPR_BOUNCER0 + ((g_frame >> (fast - 1)) & 1));
            break;
        case ENEMY_TYPE_THROWER:
            tile = e->throw_pose ? (uint8_t)SPR_THROWER_THROW
                                 : (uint8_t)(SPR_THROWER0 + ((g_frame >> fast) & 1));
            break;
        case ENEMY_TYPE_FLUTTER:
            tile = (uint8_t)(SPR_FLUTTER0 + ((g_frame >> 2) & 1));
            break;
        case ENEMY_TYPE_HOPPER:
            tile = (uint8_t)(SPR_HOPPER0 + (e->vy == 0 || e->vy > FX(1, 128) ? 1 : 0));
            break;
        case ENEMY_TYPE_SPITTER:
            tile = (uint8_t)(SPR_SPITTER0 + ((g_frame >> (fast - 1)) & 1));
            break;
        case ENEMY_TYPE_DROPPER:
            tile = (uint8_t)(SPR_DROPPER0 + ((g_frame >> 4) & 1));
            break;
        case ENEMY_TYPE_SHIELD:
            tile = (uint8_t)(SPR_SHIELD0 + ((g_frame >> (fast + 1)) & 1));
            break;
        default:
            tile = (uint8_t)((e->angry ? SPR_E_ANGRY0 : SPR_E_WALK0) + ((g_frame >> fast) & 1));
            break;
        }
        /* The walker has its own angry graphics; the others are tinted red.
         * Frozen enemies are grey. A shooter winding up flashes yellow. */
        if (g_freeze || (e->stun && (g_frame & 4))) {
            attr = (uint8_t)(attr | (PAL_GREY << SPR_ATTR_PAL_SHIFT));
        } else if (enemy_shot_in(e) && ((g_frame >> 2) & 1)) {
            attr = (uint8_t)(attr | (PAL_YELLOW << SPR_ATTR_PAL_SHIFT));
        } else if (e->angry && e->type != ENEMY_TYPE_WALKER) {
            attr = (uint8_t)(attr | (PAL_RED << SPR_ATTR_PAL_SHIFT));
        }
        oam_add(PX(e->x), PX(e->y), tile, attr);
    }
}

/* --- Chaser ------------------------------------------------------------------ */
void chaser_reset(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        chasers[i].active = 0;
    }
}

uint8_t chaser_get(uint8_t i, uint16_t *x, uint16_t *y)
{
    if (i >= NUM_PLAYERS || !chasers[i].active) {
        return 0;
    }
    *x = chasers[i].x;
    *y = chasers[i].y;
    return 1;
}

uint8_t chaser_active(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (chasers[i].active) {
            return 1;
        }
    }
    return 0;
}

void chaser_spawn(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (g_players[i].state == PS_ALIVE) {
            chasers[i].active = 1;
            chasers[i].speed = CHASER_SPEED0;
            chasers[i].x = TO_POS(i == 0 ? 104 : 136);
            chasers[i].y = TO_POS(FIELD_Y);
        }
    }
}

static void chase_axis(uint16_t *v, uint8_t target, fx8_8 speed)
{
    uint8_t px = PX(*v);
    if (px + 1 < target) {
        *v = (uint16_t)(*v + (uint16_t)speed);
    } else if (px > target + 1) {
        *v = (uint16_t)(*v - (uint16_t)speed);
    }
}

void chaser_update(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        if (!chasers[i].active) {
            continue;
        }
        if (p->state == PS_OFF) {
            chasers[i].active = 0;   /* its player left the game */
            continue;
        }
        if (p->state != PS_ALIVE) {
            continue;
        }
        chase_axis(&chasers[i].x, PX(p->x), chasers[i].speed);
        chase_axis(&chasers[i].y, PX(p->y), chasers[i].speed);
        if ((g_frame & 31) == 0 && chasers[i].speed < CHASER_SPEED_MAX) {
            chasers[i].speed = (fx8_8)(chasers[i].speed + 4);
        }
    }
}

void chaser_draw(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (chasers[i].active) {
            oam_add(PX(chasers[i].x), PX(chasers[i].y), (uint8_t)(SPR_CHASER0 + ((g_frame >> 3) & 1)),
                    (uint8_t)(PX(chasers[i].x) > PX(g_players[i].x) ? SPR_ATTR_FLIP_X : 0u));
        }
    }
}
