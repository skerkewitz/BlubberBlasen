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
#define RESCUE_FRAMES 420         /* 7 s in the rescue bubble */
#define RESCUE_HELP_AGAIN (RESCUE_FRAMES / 3)   /* second call for help: 2/3 of the time is over */
#define RESCUE_SPEED  FX(0, 90)
#define DASH_SPEED    FX(4, 128)
#define DASH_POP_X    14          /* a dash pops bubbles this close in x ... */
#define DASH_POP_Y    20          /* ... and in y: a band taller than the body */
#define DASH_BUFFER   6           /* a dash pressed up to 6 frames too early still happens */
#define DASH_EXTEND   48          /* how far a dash may run on to get out of a wall */
#define CHARGE_FRAMES 30          /* hold Y this long with a full bar */
#define ENERGY_DRAIN_MASK 3       /* drain 1 every 4 frames */
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
    p->rescue = 0;
    p->dash = 0;
    p->dash_used = 0;
    p->dash_buffer = 0;
    p->charge = 0;
}

/* --- Plus mode energy -------------------------------------------------------- */
void energy_gain(uint8_t player, uint8_t amount)
{
    Player *p = &g_players[player];
    if (!g_opt.plus || player >= NUM_PLAYERS || p->state == PS_OFF) {
        return;
    }
    p->energy = (uint8_t)(p->energy + amount > ENERGY_MAX ? ENERGY_MAX : p->energy + amount);
    p->energy_hold = ENERGY_HOLD;
}

void energy_tick(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        Player *p = &g_players[i];
        if (!g_opt.plus || p->state == PS_OFF) {
            continue;
        }
        if (DBG(DBG_FULL_ENERGY)) {
            p->energy = ENERGY_MAX;
        } else if (p->energy_hold) {
            p->energy_hold--;
        } else if (p->energy && (g_frame & ENERGY_DRAIN_MASK) == 0) {
            p->energy--;   /* waiting costs energy */
        }
    }
}

/* Dash (design guide "Dash"): a fast burst of about 7 tiles; the player
 * keeps facing the same way. It pops every bubble it sweeps through (a band
 * taller than the body, see touch_bubbles), so a dash along a row of
 * bubbles sets them all off. Invulnerable for the whole dash and DASH_GRACE
 * frames after it (the player blinks), long enough to jump clear of an
 * enemy the dash ended next to. Passes through inner walls and platforms;
 * the outer walls stop it. One per jump. A press up to 6 frames too early
 * is kept. */
uint8_t player_dash_invulnerable(const Player *p)
{
    return (uint8_t)(p->dash != 0);
}

/* The body (12 x 16 px) overlaps a solid tile. */
static uint8_t inside_wall(uint16_t x, uint16_t y)
{
    int16_t l = (int16_t)(PX(x) + 2), r = (int16_t)(PX(x) + 13);
    int16_t t = (int16_t)(PX(y) + 2), b = (int16_t)(PX(y) + 15);
    return (uint8_t)(level_solid_px(l, t) || level_solid_px(r, t) || level_solid_px(l, b) || level_solid_px(r, b));
}

/* L or R; both together (the keyboard's single dash key) mean: the held
 * direction, or the facing direction if none is held. */
static int8_t dash_direction(const Player *p, uint16_t pad, uint16_t pressed)
{
    if ((pressed & (PAD_L | PAD_R)) == (PAD_L | PAD_R)) {
        if (pad & PAD_LEFT) {
            return -1;
        }
        if (pad & PAD_RIGHT) {
            return 1;
        }
        return (int8_t)(p->facing_left ? -1 : 1);
    }
    return (int8_t)((pressed & PAD_L) ? -1 : 1);
}

static uint8_t try_dash(Player *p, uint16_t pad, uint16_t pressed)
{
    if (!g_opt.plus || p->carried) {
        return 0;
    }
    if (pressed & (PAD_L | PAD_R)) {
        p->dash_buffer = DASH_BUFFER;
        p->dash_buffer_dir = dash_direction(p, pad, pressed);
    }
    if (!p->dash_buffer) {
        return 0;
    }
    p->dash_buffer--;
    if (p->dash || p->dash_used || p->energy < DASH_COST) {
        return 0;   /* kept for a few frames, e.g. until landing */
    }
    p->dash = DASH_FRAMES;
    p->dash_dir = p->dash_buffer_dir;
    p->dash_buffer = 0;
    p->dash_used = 1;
    p->last_free_x = p->x;
    if (!DBG(DBG_FULL_ENERGY)) {
        p->energy = (uint8_t)(p->energy - DASH_COST);
    }
    p->vy = 0;
    sfx(SFX_DASH);
    return 1;
}

static void dash_move(Player *p)
{
    p->x = (uint16_t)(p->dash_dir < 0 ? p->x - (uint16_t)DASH_SPEED : p->x + (uint16_t)DASH_SPEED);
    level_clamp_x(&p->x);     /* the outer walls always stop it */
    p->vx = 0;
    if (!inside_wall(p->x, p->y)) {
        p->last_free_x = p->x;
    }
    if (--p->dash == 0 && p->invulnerable < DASH_GRACE) {
        p->invulnerable = DASH_GRACE;
    }
    if (p->dash == 0 && inside_wall(p->x, p->y)) {
        /* ending inside a wall: run on to the next free spot, else back to the last one */
        uint16_t x = p->x;
        uint8_t k;
        for (k = 0; k < DASH_EXTEND; k++) {
            uint16_t before = x;
            x = (uint16_t)(p->dash_dir < 0 ? x - TO_POS(1) : x + TO_POS(1));
            level_clamp_x(&x);
            if (x == before) {
                break;
            }
            if (!inside_wall(x, p->y)) {
                p->x = x;
                return;
            }
        }
        p->x = p->last_free_x;
    }
}

/* Plus mode: chimes when a dash becomes available and when the bar is full. */
static void energy_feedback(Player *p)
{
    uint8_t now = (uint8_t)(!g_opt.plus ? 0 : p->energy >= ENERGY_MAX ? 2 : p->energy >= DASH_COST ? 1 : 0);
    if (now > p->energy_state) {
        sfx(now == 2 ? SFX_FULL : SFX_READY);
    }
    p->energy_state = now;
}

/* --- Rescue bubble (co-op rescue) ------------------------------------------- */
static Player *partner(const Player *p)
{
    return &g_players[p->id ^ 1];
}

static void trap_in_rescue_bubble(Player *p)
{
    p->state = PS_RESCUE;
    p->rescue = RESCUE_FRAMES;
    p->rescue_dir = AIR_UP;
    p->vx = 0;
    p->vy = 0;
    p->dash = 0;
    p->charge = 0;
    sfx(SFX_HELP);
}

static void update_rescue(Player *p)
{
    Player *o = partner(p);
    /* drift like a floating bubble */
    if ((p->rescue & 15) == 0) {
        p->rescue_dir = level_air_at((uint8_t)(PX(p->x) + 8), (uint8_t)(PX(p->y) + 8));
        if (p->rescue_dir == AIR_NONE) {
            p->rescue_dir = (uint8_t)(1 + (rng_next() & 3));
        }
    }
    switch (p->rescue_dir) {
    case AIR_UP:    p->y = (uint16_t)(p->y - (uint16_t)RESCUE_SPEED); break;
    case AIR_DOWN:  p->y = (uint16_t)(p->y + (uint16_t)RESCUE_SPEED); break;
    case AIR_LEFT:  p->x = (uint16_t)(p->x - (uint16_t)RESCUE_SPEED); break;
    case AIR_RIGHT: p->x = (uint16_t)(p->x + (uint16_t)RESCUE_SPEED); break;
    default: break;
    }
    level_clamp_x(&p->x);
    if (PX(p->y) < FIELD_Y + 6 || PX(p->y) >= WRAP_Y) {
        p->y = TO_POS(FIELD_Y + 6);
    } else if (PX(p->y) > FIELD_BOTTOM - 8 - 16) {
        p->y = TO_POS(FIELD_BOTTOM - 8 - 16);
    }
    /* the partner pops it: back in the game, no life lost */
    if (o->state == PS_ALIVE && overlap(p->x, p->y, o->x, o->y, 12, 12)) {
        p->state = PS_ALIVE;
        p->invulnerable = INVULN_FRAMES;
        p->rescue = 0;
        item_spawn(IT_BURST, p->x, p->y, 0);
        sfx(SFX_POP);
        return;
    }
    if (p->rescue == RESCUE_HELP_AGAIN) {
        sfx(SFX_HELP);   /* time is running out */
    }
    if (--p->rescue == 0) {
#ifdef BB_DEBUG
        g_dbg_deaths[11]++;
#endif
        p->state = PS_DYING;   /* it popped on its own: a life is lost */
        p->timer = 0;
        sfx(SFX_DEATH);
    }
}

/* Contact with an enemy: rescue bubble or death. */
static void hurt(Player *p, uint8_t touch)
{
    if (touch == TOUCH_ENEMY && g_opt.rescue && partner(p)->state != PS_OFF) {
        trap_in_rescue_bubble(p);
    } else {
#ifdef BB_DEBUG
        g_dbg_deaths[g_dbg_touch]++;
#endif
        p->state = PS_DYING;
        p->timer = 0;
        p->carried = 0;
        sfx(SFX_DEATH);
    }
}

static uint8_t safe(const Player *p)
{
    return (uint8_t)(p->invulnerable || DBG(DBG_INVINCIBLE) || player_dash_invulnerable(p));
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

static void shoot(Player *p, uint16_t pad, uint16_t pressed)
{
    uint8_t fire = 0;

    if (p->shoot_cooldown) {
        p->shoot_cooldown--;
    }
    if (p->shoot_pose) {
        p->shoot_pose--;
    }

    /* Plus mode, full bar: holding Y charges the special shot; a short
     * press still blows a normal bubble when Y is let go. */
    if (g_opt.plus && p->energy >= ENERGY_MAX && !(p->powers & POWER_BIT(POWER_GLOVE))) {
        if (pad & PAD_Y) {
            if (p->charge < 255) {
                p->charge++;
            }
            if (p->charge == CHARGE_FRAMES) {
                uint16_t bx = (uint16_t)(p->facing_left ? p->x - TO_POS(8) : p->x + TO_POS(8));
                bubble_shoot_special(bx, p->y, p->facing_left);
                if (!DBG(DBG_FULL_ENERGY)) {
                    p->energy = 0;
                }
                p->shoot_pose = SHOOT_POSE;
                p->shoot_cooldown = SHOOT_DELAY;
                sfx(SFX_EXTRA);
            }
            return;
        }
        fire = (uint8_t)(p->charge > 0 && p->charge < CHARGE_FRAMES);
        p->charge = 0;
    } else {
        p->charge = 0;
        fire = (uint8_t)((pressed & PAD_Y) != 0);
    }

    if (fire && p->shoot_cooldown == 0) {
        uint16_t bx = (uint16_t)(p->facing_left ? p->x - TO_POS(8) : p->x + TO_POS(8));
        if (p->powers & POWER_BIT(POWER_GLOVE)) {
            effect_start_glove(bx, p->y, (int8_t)(p->facing_left ? -1 : 1), p->id);
        } else {
            bubble_shoot(bx, p->y, p->facing_left, p->powers);
        }
        if (p->powers & POWER_BIT(POWER_RING)) {
            score_add(p->score, 2, 1);   /* Bubble Ring: +100 per shot */
        }
        p->shoot_cooldown = (uint8_t)((p->powers & (1u << POWER_RATE)) ? SHOOT_DELAY_RATE : SHOOT_DELAY);
        p->shoot_pose = SHOOT_POSE;
        sfx(SFX_SHOOT);
    }
}

/* Touching a floating bubble pops it (and starts a chain reaction);
 * falling onto one with jump held bounces off it instead. A dash sweeps
 * a taller band and always pops. */
static void touch_bubbles(Player *p, uint16_t pad)
{
    uint8_t i;
    uint8_t w = (uint8_t)(p->dash ? DASH_POP_X : 12), h = (uint8_t)(p->dash ? DASH_POP_Y : 12);
    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if ((b->state != BS_FLOAT && b->state != BS_TRAPPED) || !overlap(p->x, p->y, b->x, b->y, w, h)) {
            continue;
        }
        if (!p->dash && p->vy > 0 && (pad & PAD_B) && PX(p->y) + 8 <= PX(b->y)) {
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

    case PS_RESCUE:
        update_rescue(p);
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
            uint8_t touch = safe(p) ? TOUCH_NONE : enemies_touch(p->x, p->y);
            if (touch) {
                hurt(p, touch);
            }
            break;
        }
        if (p->on_ground) {
            p->dash_used = 0;
        }
        energy_feedback(p);
        p->trail_pos = (uint8_t)((p->trail_pos + 1) & 7);
        p->trail_x[p->trail_pos] = PX(p->x);
        p->trail_y[p->trail_pos] = PX(p->y);
        if (p->dash || try_dash(p, pad, pressed)) {
            dash_move(p);
        } else {
            walk(p, pad);
            jump_and_fall(p, pad);
        }
        shoot(p, pad, pressed);
        touch_bubbles(p, pad);
        if (g_freeze) {
            enemies_defeat_at(p->x, p->y, p->id);
        }
        if (!safe(p)) {
            uint8_t touch = enemies_touch(p->x, p->y);
            if (touch) {
                hurt(p, touch);
            }
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

    /* Player 2 is the same character in other colours. */
    if (p->id == 1) {
        attr = (uint8_t)(attr | (PAL_P2 << SPR_ATTR_PAL_SHIFT));
    }

    switch (p->state) {
    case PS_ENTERING:
        oam_add(PX(p->x), PX(p->y), (uint8_t)(SPR_BUBBLE0 + ((g_frame >> 3) & 1)), 0);
        break;
    case PS_DYING:
        tile = (uint8_t)(SPR_P_DEATH0 + ((p->timer >> 3) & 3));
        break;
    case PS_RESCUE: {
        /* a bubble ring around the trapped player; it flashes red before it pops */
        uint8_t ring = (uint8_t)(p->rescue < 90 ? SPR_RING_RED0 : p->rescue < 210 ? SPR_RING_ORANGE0 : SPR_RING0);
        if (p->rescue < 90 && (p->rescue & 4)) {
            break;
        }
        static const uint8_t phase[4] = { 0, 1, 2, 1 };
        oam_add(PX(p->x), PX(p->y), (uint8_t)(ring + phase[(g_frame >> 3) & 3]), 0);
        tile = SPR_P_FALL;
        break;
    }
    case PS_ALIVE:
        if (p->invulnerable && (p->invulnerable & 4)) {
            return;   /* blink */
        }
        if (p->dash) {
            /* dashing: bright flash */
            attr = (uint8_t)((attr & (0xFFu ^ SPR_ATTR_PAL_MASK)) | (((g_frame >> 1) & 1 ? PAL_YELLOW : PAL_NONE) << SPR_ATTR_PAL_SHIFT));
        } else if (p->charge >= CHARGE_FRAMES / 2) {
            attr = (uint8_t)((attr & (0xFFu ^ SPR_ATTR_PAL_MASK)) | (PAL_YELLOW << SPR_ATTR_PAL_SHIFT));
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

    if (p->state != PS_ALIVE || !g_opt.plus) {
        return;
    }
    if (p->dash) {
        /* two fading afterimages behind the dash */
        uint8_t ghost = (uint8_t)((attr & (0xFFu ^ SPR_ATTR_PAL_MASK)) | (PAL_GREY << SPR_ATTR_PAL_SHIFT));
        oam_add(p->trail_x[(p->trail_pos - 2) & 7], p->trail_y[(p->trail_pos - 2) & 7], tile, ghost);
        if (g_frame & 1) {
            oam_add(p->trail_x[(p->trail_pos - 4) & 7], p->trail_y[(p->trail_pos - 4) & 7], tile, ghost);
        }
    } else if (p->energy_state && tile < 5) {
        /* glow outline: yellow = dash available, cycling colours = bar full */
        static const uint8_t pulse[4] = { PAL_YELLOW, PAL_ORANGE, PAL_PINK, PAL_GREEN };
        uint8_t pal = p->energy_state == 2 ? pulse[(g_frame >> 2) & 3] : PAL_YELLOW;
        oam_add(PX(p->x), PX(p->y), (uint8_t)(SPR_AURA0 + tile),
                (uint8_t)((attr & SPR_ATTR_FLIP_X) | (pal << SPR_ATTR_PAL_SHIFT)));
    }
}
