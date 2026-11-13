/*
 * The player: walking with acceleration, variable-height jump, shooting
 * bubbles, popping bubbles, bouncing on bubbles, dying.
 * Physics ported from PlayerSystem / PlayerComponent of the Java version;
 * its floating-point values (pixels per second at 60 Hz) converted to 8.8 per frame.
 */

#include "game/internal.h"

#define WALK_ACCEL    FX(0, 53)    /* 0.21 px/frame^2 */
#define WALK_MAX      FX(0, 235)   /* 0.92 px/frame   */
#define JUMP_SPEED    FX(2, 145)   /* 2.57 px/frame -> 42 px high */
#define JUMP_CUT      FX(0, 217)   /* released jump button caps upward speed */
#define GRAVITY_UP    FX(0, 20)
#define GRAVITY_DOWN  FX(0, 80)
#define MAX_FALL      FX(2, 0)

#define SHOOT_DELAY   15
#define SHOOT_POSE    10
#define DEATH_FRAMES  120
#define INVULN_FRAMES 120

static Player *const p = &g_player;

void player_start(void)
{
    p->state = PS_ALIVE;
    p->facing_left = 0;
    p->on_ground = 1;
    p->shoot_cooldown = 0;
    p->shoot_pose = 0;
    p->invulnerable = INVULN_FRAMES;
    p->timer = 0;
    p->x = TO_POS(PLAYER_SPAWN_X);
    p->y = TO_POS(PLAYER_SPAWN_Y);
    p->vx = 0;
    p->vy = 0;
}

/* A new round: drift from the current position to the spawn point. */
void player_enter_round(void)
{
    if (p->state != PS_ALIVE) {
        player_start();
    }
    p->state = PS_ENTERING;
    p->vx = 0;
    p->vy = 0;
    p->shoot_pose = 0;
}

uint8_t player_entering(void)
{
    return (uint8_t)(p->state == PS_ENTERING);
}

static void move_towards(uint16_t *v, uint8_t target)
{
    uint8_t px = PX(*v);
    if (px < target) {
        *v = TO_POS(px + 1);
    } else if (px > target) {
        *v = TO_POS(px - 1);
    } else {
        *v = TO_POS(target);
    }
}

static void walk(uint16_t pad)
{
    if (pad & PAD_LEFT) {
        p->vx = (fx8_8)(p->vx - WALK_ACCEL);
        if (p->vx < -WALK_MAX) {
            p->vx = (fx8_8)-WALK_MAX;
        }
        p->facing_left = 1;
    } else if (pad & PAD_RIGHT) {
        p->vx = (fx8_8)(p->vx + WALK_ACCEL);
        if (p->vx > WALK_MAX) {
            p->vx = WALK_MAX;
        }
        p->facing_left = 0;
    } else if (p->vx > 0) {
        p->vx = (fx8_8)(p->vx > WALK_ACCEL ? p->vx - WALK_ACCEL : 0);
    } else if (p->vx < 0) {
        p->vx = (fx8_8)(p->vx < -WALK_ACCEL ? p->vx + WALK_ACCEL : 0);
    }

    if (p->vx != 0) {
        int8_t dir = (int8_t)(p->vx < 0 ? -1 : 1);
        if (level_wall_ahead(p->x, p->y, dir)) {
            p->vx = 0;
        } else {
            p->x = (uint16_t)(p->x + (uint16_t)p->vx);
            level_clamp_x(&p->x);
        }
    }
}

static void jump_and_fall(uint16_t pad)
{
    uint8_t jump_held = (uint8_t)((pad & PAD_B) != 0);

    p->on_ground = level_on_ground(p->x, p->y);
    if (p->on_ground && p->vy >= 0) {
        if (jump_held) {
            p->vy = (fx8_8)-JUMP_SPEED;
            p->on_ground = 0;
            sfx(SFX_JUMP);
        } else {
            p->vy = 0;
        }
    } else {
        if (!jump_held && p->vy < -JUMP_CUT) {
            p->vy = (fx8_8)-JUMP_CUT;
        }
        p->vy = (fx8_8)(p->vy + (p->vy < 0 ? GRAVITY_UP : GRAVITY_DOWN));
        if (p->vy > MAX_FALL) {
            p->vy = MAX_FALL;
        }
    }

    if (p->vy < 0) {
        /* Upward movement is never blocked: platforms are one-way. */
        p->y = (uint16_t)(p->y + (uint16_t)p->vy);
        if (PX(p->y) < FIELD_Y || PX(p->y) >= WRAP_Y) {
            p->y = TO_POS(FIELD_Y);
            p->vy = 0;
        }
    } else if (p->vy > 0) {
        if (level_fall(&p->x, &p->y, p->vy)) {
            p->vy = 0;
            p->on_ground = 1;
        }
    }
}

static void shoot(uint16_t pressed)
{
    if (p->shoot_cooldown) {
        p->shoot_cooldown--;
    }
    if (p->shoot_pose) {
        p->shoot_pose--;
    }
    if ((pressed & PAD_Y) && p->shoot_cooldown == 0) {
        uint16_t bx = (uint16_t)(p->facing_left ? p->x - TO_POS(8) : p->x + TO_POS(8));
        bubble_shoot(bx, p->y, p->facing_left);
        p->shoot_cooldown = SHOOT_DELAY;
        p->shoot_pose = SHOOT_POSE;
        sfx(SFX_SHOOT);
    }
}

static void touch_bubbles(uint16_t pad)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if (b->state == BS_OFF || b->state == BS_SHOOT || !overlap(p->x, p->y, b->x, b->y, 12, 12)) {
            continue;
        }
        if (b->state == BS_TRAPPED) {
            item_spawn(IT_THROWN, b->x, b->y, (int8_t)(p->facing_left ? -1 : 1), 0);
            item_spawn(IT_POPUP, b->x, b->y, 0, SPR_PTS1000);
            score_add(g_score, 3, 1);           /* +1000 */
            b->state = BS_OFF;
            sfx(SFX_KILL);
        } else if (p->vy > 0 && (pad & PAD_B) && PX(p->y) + 8 <= PX(b->y)) {
            /* Falling onto a bubble with jump held: bounce off it. */
            p->vy = (fx8_8)-JUMP_SPEED;
            sfx(SFX_JUMP);
        } else {
            item_spawn(IT_BURST, b->x, b->y, 0, 0);
            score_add(g_score, 1, 1);           /* +10 */
            b->state = BS_OFF;
            sfx(SFX_POP);
        }
    }
}

static void touch_enemies(void)
{
    uint8_t i;
    if (p->invulnerable) {
        return;
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(p->x, p->y, e->x, e->y, 10, 12)) {
            p->state = PS_DYING;
            p->timer = 0;
            sfx(SFX_DEATH);
            return;
        }
    }
}

void player_update(uint16_t pad, uint16_t pressed)
{
    switch (p->state) {
    case PS_ENTERING:
        move_towards(&p->x, PLAYER_SPAWN_X);
        move_towards(&p->y, PLAYER_SPAWN_Y);
        if (PX(p->x) == PLAYER_SPAWN_X && PX(p->y) == PLAYER_SPAWN_Y) {
            p->state = PS_ALIVE;
            p->facing_left = 0;
            p->on_ground = 1;
        }
        break;

    case PS_DYING:
        if (++p->timer >= DEATH_FRAMES) {
            player_killed();
        }
        break;

    case PS_ALIVE:
        if (p->invulnerable) {
            p->invulnerable--;
        }
        walk(pad);
        jump_and_fall(pad);
        shoot(pressed);
        touch_bubbles(pad);
        touch_enemies();
        break;

    default:
        break;
    }
}

void player_draw(void)
{
    uint8_t tile = SPR_P_WALK0;
    uint8_t attr = (uint8_t)(p->facing_left ? SPR_ATTR_FLIP_X : 0u);

    switch (p->state) {
    case PS_ENTERING:
        oam_add(PX(p->x), PX(p->y), (uint8_t)(SPR_BUBBLE0 + ((g_frame >> 3) & 1)), 0);
        break;
    case PS_DYING:
        tile = (uint8_t)(SPR_P_DEATH0 + ((p->timer >> 3) & 3));
        break;
    case PS_ALIVE:
        if (p->invulnerable && (p->invulnerable & 4)) {
            return;   /* blink */
        }
        if (p->shoot_pose) {
            tile = SPR_P_SHOOT;
        } else if (!p->on_ground) {
            tile = (uint8_t)(p->vy < 0 ? SPR_P_JUMP : SPR_P_FALL);
        } else if (p->vx != 0) {
            tile = (uint8_t)(SPR_P_WALK0 + ((g_frame >> 3) & 1));
        }
        break;
    default:
        return;
    }
    oam_add(PX(p->x), PX(p->y), tile, attr);
}
