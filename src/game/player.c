/*
 * The players: walking with acceleration, variable-height jump, shooting
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

#define WALK_ACCEL_SHOES FX(0, 80)
#define WALK_MAX_SHOES   FX(1, 74)  /* 1.3 px/frame */
#define SHOOT_DELAY   15
#define SHOOT_DELAY_RATE 7         /* with the rate candy */
#define SHOOT_POSE    10
#define DEATH_FRAMES  120
#define INVULN_FRAMES 120

/* Player 1 starts bottom-left facing right, player 2 bottom-right facing left. */
static uint8_t spawn_x(const Player *p)
{
    return (uint8_t)(p->id == 0 ? PLAYER_SPAWN_X : SCREEN_W - PLAYER_SPAWN_X - 16);
}

void player_start(Player *p)
{
    p->state = PS_ALIVE;
    p->facing_left = p->id;
    p->on_ground = 1;
    p->shoot_cooldown = 0;
    p->shoot_pose = 0;
    p->invulnerable = INVULN_FRAMES;
    p->timer = 0;
    p->x = TO_POS(spawn_x(p));
    p->y = TO_POS(PLAYER_SPAWN_Y);
    p->vx = 0;
    p->vy = 0;
    p->stun = 0;
    p->carried = 0;
}

/* A new round: drift from the current position to the spawn point. */
void player_enter_round(Player *p)
{
    if (p->state == PS_OFF) {
        return;
    }
    if (p->state != PS_ALIVE) {
        player_start(p);
    }
    p->state = PS_ENTERING;
    p->vx = 0;
    p->vy = 0;
    p->shoot_pose = 0;
}

uint8_t players_entering(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (g_players[i].state == PS_ENTERING) {
            return 1;
        }
    }
    return 0;
}

uint8_t players_in_game(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < NUM_PLAYERS; i++) {
        n = (uint8_t)(n + (g_players[i].state != PS_OFF));
    }
    return n;
}

/* The living player closest to (x, y); player 1 if nobody is alive. */
const Player *player_nearest(uint16_t x, uint16_t y)
{
    const Player *best = &g_players[0];
    uint16_t best_d = 0xFFFFu;
    uint8_t i;

    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        int16_t dx, dy;
        uint16_t d;
        if (p->state != PS_ALIVE) {
            continue;
        }
        dx = (int16_t)((int16_t)PX(p->x) - (int16_t)PX(x));
        dy = (int16_t)((int16_t)PX(p->y) - (int16_t)PX(y));
        d = (uint16_t)((dx < 0 ? -dx : dx) + (dy < 0 ? -dy : dy));
        if (d < best_d) {
            best_d = d;
            best = p;
        }
    }
    return best;
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

static void walk(Player *p, uint16_t pad)
{
    uint8_t shoes = (uint8_t)((p->powers & (1u << POWER_SHOES)) != 0);
    fx8_8 accel = shoes ? WALK_ACCEL_SHOES : WALK_ACCEL;
    fx8_8 max = shoes ? WALK_MAX_SHOES : WALK_MAX;

    if (pad & PAD_LEFT) {
        p->vx = (fx8_8)(p->vx - accel);
        if (p->vx < -max) {
            p->vx = (fx8_8)-max;
        }
        p->facing_left = 1;
    } else if (pad & PAD_RIGHT) {
        p->vx = (fx8_8)(p->vx + accel);
        if (p->vx > max) {
            p->vx = max;
        }
        p->facing_left = 0;
    } else if (p->vx > 0) {
        p->vx = (fx8_8)(p->vx > accel ? p->vx - accel : 0);
    } else if (p->vx < 0) {
        p->vx = (fx8_8)(p->vx < -accel ? p->vx + accel : 0);
    }

    if (p->vx != 0) {
        int8_t dir = (int8_t)(p->vx < 0 ? -1 : 1);
        if (level_wall_ahead(p->x, p->y, dir)) {
            p->vx = 0;
        } else {
            uint8_t before = PX(p->x);
            p->x = (uint16_t)(p->x + (uint16_t)p->vx);
            level_clamp_x(&p->x);
            if (PX(p->x) != before && g_count.walked < 0xFFFFu) {
                g_count.walked++;
            }
        }
    }
}

static void jump_and_fall(Player *p, uint16_t pad)
{
    uint8_t jump_held = (uint8_t)((pad & PAD_B) != 0);

    p->on_ground = level_on_ground(p->x, p->y);
    if (p->on_ground && p->vy >= 0) {
        if (jump_held) {
            p->vy = (fx8_8)-JUMP_SPEED;
            p->on_ground = 0;
            if (g_count.jumps < 255) {
                g_count.jumps++;
            }
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

static void shoot(Player *p, uint16_t pressed)
{
    if (p->shoot_cooldown) {
        p->shoot_cooldown--;
    }
    if (p->shoot_pose) {
        p->shoot_pose--;
    }
    if ((pressed & PAD_Y) && p->shoot_cooldown == 0) {
        uint16_t bx = (uint16_t)(p->facing_left ? p->x - TO_POS(8) : p->x + TO_POS(8));
        bubble_shoot(bx, p->y, p->facing_left, p->powers);
        p->shoot_cooldown = (uint8_t)((p->powers & (1u << POWER_RATE)) ? SHOOT_DELAY_RATE : SHOOT_DELAY);
        p->shoot_pose = SHOOT_POSE;
        sfx(SFX_SHOOT);
    }
}

/* Touching a floating bubble pops it (and starts a chain reaction);
 * falling onto one with jump held bounces off it instead. */
static void touch_bubbles(Player *p, uint16_t pad)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if ((b->state != BS_FLOAT && b->state != BS_TRAPPED) || !overlap(p->x, p->y, b->x, b->y, 12, 12)) {
            continue;
        }
        if (p->vy > 0 && (pad & PAD_B) && PX(p->y) + 8 <= PX(b->y)) {
            p->vy = (fx8_8)-JUMP_SPEED;
            sfx(SFX_JUMP);
        } else {
            bubble_pop(b, (int8_t)(p->facing_left ? -1 : 1), p->id);
        }
    }
}

void player_update(Player *p, uint16_t pad, uint16_t pressed)
{
    switch (p->state) {
    case PS_ENTERING:
        move_towards(&p->x, spawn_x(p));
        move_towards(&p->y, PLAYER_SPAWN_Y);
        if (PX(p->x) == spawn_x(p) && PX(p->y) == PLAYER_SPAWN_Y) {
            p->state = PS_ALIVE;
            p->facing_left = p->id;
            p->on_ground = 1;
        }
        break;

    case PS_DYING:
        if (++p->timer >= DEATH_FRAMES) {
            player_killed(p);
        }
        break;

    case PS_ALIVE:
        if (p->invulnerable) {
            p->invulnerable--;
        }
        if (p->stun) {
            p->stun--;          /* burnt: no control for a moment */
            pad = 0;
            pressed = 0;
        }
        if (p->carried) {
            /* Riding a water stream: the stream moves the player. */
            if (!p->invulnerable && enemies_touch(p->x, p->y)) {
                p->state = PS_DYING;
                p->timer = 0;
                p->carried = 0;
                sfx(SFX_DEATH);
            }
            break;
        }
        walk(p, pad);
        jump_and_fall(p, pad);
        shoot(p, pressed);
        touch_bubbles(p, pad);
        if (!p->invulnerable && enemies_touch(p->x, p->y)) {
            p->state = PS_DYING;
            p->timer = 0;
            sfx(SFX_DEATH);
        }
        break;

    default:
        break;
    }
}

void player_draw(const Player *p)
{
    uint8_t tile = SPR_P_WALK0;
    uint8_t attr = (uint8_t)(p->facing_left ? SPR_ATTR_FLIP_X : 0u);

    /* Player 2 is the same character, tinted. */
    if (p->id == 1) {
        attr = (uint8_t)(attr | (PAL_CYAN << SPR_ATTR_PAL_SHIFT));
    }

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
        if (p->stun) {
            attr = (uint8_t)((attr & (0xFFu ^ SPR_ATTR_PAL_MASK)) | (PAL_ORANGE << SPR_ATTR_PAL_SHIFT));
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
