/*
 * Walker enemies. AI ported from AiEnemySystem of the Java version:
 * walk, turn at walls, decide at edges (walk over, jump the gap, turn),
 * jump up to the platform above when the player is higher.
 */

#include "game/internal.h"

#define SPEED           FX(1, 0)
#define SPEED_ANGRY     FX(1, 77)    /* 1.3 px/frame */
#define JUMP_PIXELS     44
#define GAP_JUMP_PIXELS 24
#define THINK_FRAMES    120          /* how often "jump up?" is considered */

static uint8_t think_timer;

void enemies_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        g_enemies[i].active = 0;
    }
    think_timer = 0;
}

static Enemy *alloc_enemy(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (!g_enemies[i].active) {
            Enemy *e = &g_enemies[i];
            e->active = 1;
            e->angry = 0;
            e->entering = 0;
            e->jump_left = 0;
            e->gap_jump = 0;
            e->walk_over_edge = 0;
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
        Enemy *e = alloc_enemy();
        if (!e) {
            return;
        }
        e->facing_left = (uint8_t)((le->flags & LEVEL_ENEMY_FACE_LEFT) != 0);
        e->target_x = (uint8_t)(le->x << 3);
        e->target_y = (uint8_t)(FIELD_Y + (le->y << 3) - 8);
        e->x = TO_POS(e->target_x);
        e->y = TO_POS(FIELD_Y);
        e->entering = 1;
    }
}

void enemy_spawn(uint16_t x, uint16_t y, uint8_t angry, uint8_t facing_left)
{
    Enemy *e = alloc_enemy();
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

static void step_towards(uint16_t *v, uint8_t target)
{
    uint8_t px = PX(*v);
    if (px < target) {
        *v = TO_POS(px + 1);
    } else if (px > target) {
        *v = TO_POS(px - 1);
    }
}

static void step_x(Enemy *e, int8_t dir)
{
    fx8_8 speed = e->angry ? SPEED_ANGRY : SPEED;
    if (!level_wall_ahead(e->x, e->y, dir)) {
        e->x = (uint16_t)(dir < 0 ? e->x - (uint16_t)speed : e->x + (uint16_t)speed);
    }
}

static void think(Enemy *e, uint8_t think_now)
{
    int8_t dir = (int8_t)(e->facing_left ? -1 : 1);
    int16_t ey = PX(e->y);
    int16_t py = PX(g_player.y);
    uint8_t player_above = (uint8_t)(ey > py + 16);
    uint8_t player_below = (uint8_t)(ey < py - 32);

    if (e->jump_left) {
        e->jump_left--;
        e->y = (uint16_t)(e->y - TO_POS(1));
        if (e->gap_jump) {
            step_x(e, dir);
        }
    } else if (level_on_ground(e->x, e->y)) {
        e->gap_jump = 0;
        if (think_now && player_above && level_can_jump_up(e->x, e->y) && chance(102)) {
            e->jump_left = JUMP_PIXELS;
        } else {
            if (level_gap_ahead(e->x, e->y, dir)) {
                if ((player_below && chance(204)) || chance(26)) {
                    e->walk_over_edge = 1;
                }
                if (!e->walk_over_edge) {
                    if (level_can_gap_jump(e->x, e->y, dir) && chance(64)) {
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
            step_towards(&e->x, e->target_x);
            step_towards(&e->y, e->target_y);
            if (PX(e->x) == e->target_x && PX(e->y) == e->target_y) {
                e->entering = 0;
            }
            continue;
        }
        if (g_state == GS_PLAY) {
            think(e, think_now);
        }
    }
}

void enemies_draw(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        uint8_t tile;
        if (!e->active) {
            continue;
        }
        if (e->angry) {
            tile = (uint8_t)(SPR_E_ANGRY0 + ((g_frame >> 3) & 1));
        } else {
            tile = (uint8_t)(SPR_E_WALK0 + ((g_frame >> 4) & 1));
        }
        oam_add(PX(e->x), PX(e->y), tile, (uint8_t)(e->facing_left ? SPR_ATTR_FLIP_X : 0u));
    }
}
