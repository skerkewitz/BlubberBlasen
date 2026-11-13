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
 *  - Spitter: fast walker, weak jumper, spits fireballs while running.
 *  - Dropper: patrols left and right, drops off edges, shoots straight down.
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
#define THROW_POSE      24           /* frames standing still while throwing */
#define THROW_RELEASE   10           /* the rock leaves at this pose frame */
#define THROW_COOLDOWN  120          /* + random 0..127 */
#define SPEED_SPITTER   FX(1, 128)
#define SPEED_DROPPER   FX(0, 192)
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

void enemies_calm_down(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies[i].angry = 0;
    }
}

uint8_t enemies_touch(uint16_t x, uint16_t y)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(x, y, e->x, e->y, 10, 12)) {
            return 1;
        }
    }
    for (i = 0; i < MAX_ITEMS; i++) {
        const Item *it = &g_items[i];
        if (it->kind == IT_ROCK && overlap(x, y, it->x, it->y, 8, 8)) {
            return 1;
        }
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (chasers[i].active && overlap(x, y, chasers[i].x, chasers[i].y, 10, 10)) {
            return 1;
        }
    }
    return 0;
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
    if (type >= ENEMY_TYPE_FLUTTER && type <= ENEMY_TYPE_DROPPER) {
        return (uint8_t)(SPR_SMALL_ENEMY4 + type - ENEMY_TYPE_FLUTTER);
    }
    return (uint8_t)(SPR_SMALL_ENEMY0 + (type >= 1 && type <= 3 ? type - 1 : 0));
}

static fx8_8 walk_speed(const Enemy *e)
{
    fx8_8 base = e->type == ENEMY_TYPE_SPITTER ? SPEED_SPITTER : e->type == ENEMY_TYPE_DROPPER ? SPEED_DROPPER : SPEED;
    return (fx8_8)(e->angry ? base + ANGRY_EXTRA : base);
}

static void step_x(Enemy *e, int8_t dir)
{
    fx8_8 speed = walk_speed(e);
    if (!level_wall_ahead(e->x, e->y, dir)) {
        e->x = (uint16_t)(dir < 0 ? e->x - (uint16_t)speed : e->x + (uint16_t)speed);
    }
}

/* Thrower: returns 1 while it stands still throwing. */
static uint8_t try_throw(Enemy *e, int8_t dir)
{
    int16_t dy, dx;

    if (e->throw_pose) {
        e->throw_pose--;
        if (e->throw_pose == THROW_RELEASE) {
            item_spawn_shot(SHOT_ROCK, e->x, e->y, dir);
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
    {
        uint8_t i;
        for (i = 0; i < NUM_PLAYERS; i++) {
            const Player *p = &g_players[i];
            if (p->state != PS_ALIVE) {
                continue;
            }
            dy = (int16_t)((int16_t)PX(e->y) - (int16_t)PX(p->y));
            dx = (int16_t)((int16_t)PX(p->x) - (int16_t)PX(e->x));
            if (dy > -8 && dy < 8 && ((dir < 0 && dx < 0) || (dir > 0 && dx > 0))) {
                e->throw_pose = THROW_POSE;
                e->throw_timer = (uint8_t)(THROW_COOLDOWN + (rng_next() & 127));
                return 1;
            }
        }
    }
    return 0;
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

/* Spitter and dropper shoot while they keep moving. */
static void shoot_on_the_move(Enemy *e, int8_t dir)
{
    if (e->throw_timer) {
        e->throw_timer--;
        return;
    }
    if (e->type == ENEMY_TYPE_SPITTER && player_in_line(e, dir)) {
        item_spawn_shot(SHOT_SPIT, (uint16_t)(dir < 0 ? e->x - TO_POS(6) : e->x + TO_POS(6)), e->y, dir);
        e->throw_timer = (uint8_t)(SPIT_COOLDOWN + (rng_next() & 63));
    } else if (e->type == ENEMY_TYPE_DROPPER && player_below(e)) {
        item_spawn_shot(SHOT_DOWN, e->x, (uint16_t)(e->y + TO_POS(8)), 0);
        e->throw_timer = (uint8_t)(DROP_COOLDOWN + (rng_next() & 63));
    }
}

static void think_walker(Enemy *e, uint8_t think_now)
{
    int8_t dir = (int8_t)(e->facing_left ? -1 : 1);
    int16_t ey = PX(e->y);
    int16_t py = PX(player_nearest(e->x, e->y)->y);
    uint8_t player_above = (uint8_t)(ey > py + 16);
    uint8_t player_below = (uint8_t)(ey < py - 32);

    uint8_t can_climb = (uint8_t)(e->type == ENEMY_TYPE_WALKER || e->type == ENEMY_TYPE_THROWER);
    uint8_t can_gap_jump = (uint8_t)(e->type != ENEMY_TYPE_DROPPER);

    if (e->type == ENEMY_TYPE_THROWER && !e->jump_left && try_throw(e, dir)) {
        return;
    }
    if (e->type == ENEMY_TYPE_SPITTER || e->type == ENEMY_TYPE_DROPPER) {
        shoot_on_the_move(e, dir);
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
        if (g_state != GS_PLAY) {
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
        default:
            tile = (uint8_t)((e->angry ? SPR_E_ANGRY0 : SPR_E_WALK0) + ((g_frame >> fast) & 1));
            break;
        }
        /* The walker has its own angry graphics; the others are tinted red. */
        if (e->angry && e->type != ENEMY_TYPE_WALKER) {
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
