/*
 * Bubbles: shot horizontally, then drift with the level's airflow.
 * A shot bubble that hits an enemy traps it; a trapped enemy escapes
 * (angry) after a while. Floating bubbles push each other apart, and a
 * popped bubble pops every bubble it touches (chain reaction); each trapped
 * enemy in one chain is worth twice as much as the one before. Everything
 * one dash sweeps through counts as one chain.
 * Plus mode adds the push bubble: it traps nothing and shoves the floating
 * bubble it hits, like a cue ball.
 * Ported from AiBubbleSystem / AirflowSystem, extended per the design guide.
 */

#include "game/internal.h"

#define SHOOT_SPEED   FX(4, 0)
#define SHOOT_FRAMES  8
#define FLOAT_SPEED   FX(0, 90)    /* 0.35 px/frame */
#define TRAP_SPEED    FX(0, 77)    /* 0.30 px/frame */
#define BUBBLE_LIFE   600          /* 10 s, then it bursts */
#define BUBBLE_ORANGE (BUBBLE_LIFE - 180)
#define BUBBLE_RED    (BUBBLE_LIFE - 60)
#define TRAP_LIFE     300          /*  5 s, then the enemy escapes */
#define TRAP_ORANGE   (TRAP_LIFE - 180)
#define TRAP_RED      (TRAP_LIFE - 90)
#define POP_DELAY     5            /* frames between links of a chain */
#define CHAIN_REACH   15           /* bubbles closer than this pop each other */
#define PUSH_REACH    13           /* floating bubbles closer than this push apart */
#define SPECIAL_LIFE  1200         /* special and letter bubbles: 20 s */
#define PUSH_SPEED    FX(6, 0)     /* Plus mode push bubble: small and fast */
#define PUSH_FRAMES   12           /* about 72 px, then it vanishes */
#define PUSH_HIT      10           /* hit box of the push bubble against others */
#define PUSH_THIRD    4            /* |dy| below this hits the middle of a bubble */
#define SHOVE_SPEED   FX(3, 0)     /* momentum a hit bubble gets */
#define SHOVE_STOP    FX(0, 32)    /* below this the air takes over again */
#define SHOT_EDGE     6            /* Plus mode: leading edge of a shot, from its centre */
#define SHOT_RANGE_POWER 14        /* SHOOT_FRAMES with the range candy */
#define SHOOT_SPEED_POWER FX(6, 0)
#define TOP_LIMIT     (FIELD_Y + 6)
#define BOTTOM_LIMIT  (FIELD_BOTTOM - 8 - 14)

static uint8_t chain_kills;   /* trapped enemies popped in the running chain */
static int8_t  chain_dir;     /* direction the first defeated enemy flies */
static uint8_t chain_owner;   /* player who started the chain */

/* Plus mode push bubble, one per player: it traps nothing, it shoves the
 * floating bubble it hits (like a cue ball) and bursts. */
typedef struct {
    uint8_t  active;
    uint8_t  age;
    uint8_t  dir;          /* AIR_LEFT, AIR_RIGHT or AIR_UP */
    uint16_t x, y;         /* 8.8 */
} PushShot;
static PushShot push_shots[NUM_PLAYERS];

void bubbles_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        g_bubbles[i].state = BS_OFF;
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        push_shots[i].active = 0;
    }
    vacuums_clear();
    chain_kills = 0;
}

static uint8_t floating(const Bubble *b)
{
    return (uint8_t)(b->state == BS_FLOAT || b->state == BS_TRAPPED);
}

static Bubble *alloc_bubble(void)
{
    uint8_t i, oldest = 0xFF;

    /* Take a free slot, or replace the oldest plain floating bubble. */
    for (i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        if (b->state == BS_OFF) {
            return &g_bubbles[i];
        }
        if (b->state == BS_FLOAT && b->kind == BK_NORMAL &&
            (oldest == 0xFF || b->age > g_bubbles[oldest].age)) {
            oldest = i;
        }
    }
    return oldest == 0xFF ? 0 : &g_bubbles[oldest];
}

void bubble_spawn_special(uint8_t kind, uint8_t letter, uint16_t x, uint16_t y)
{
    Bubble *b = alloc_bubble();
    if (!b) {
        return;
    }
    b->state = BS_FLOAT;
    b->kind = kind;
    b->letter = letter;
    b->vx = 0;
    b->vy = 0;
    b->dir = AIR_UP;
    b->angry = 0;
    b->trapped = 0;
    b->pop_timer = 0;
    b->anim = 0;
    b->age = 1;
    b->x = x;
    b->y = y;
}

/* Plus mode: a bubble's centre inside a solid tile. */
static uint8_t in_wall(uint16_t x, uint16_t y)
{
    return (uint8_t)(g_opt.plus && level_solid_px((int16_t)(PX(x) + 8), (int16_t)(PX(y) + 8)));
}

void bubble_shoot(uint16_t x, uint16_t y, uint8_t dir, uint16_t powers)
{
    Bubble *b;
    if (in_wall(x, y)) {
        /* Plus mode: blown into a wall at point-blank, it pops at once */
        item_spawn(IT_BURST, x, y, 0);
        sfx(SFX_POP);
        return;
    }
    b = alloc_bubble();
    if (!b) {
        return;
    }
    b->state = BS_SHOOT;
    b->kind = BK_NORMAL;
    b->vx = 0;
    b->vy = 0;
    b->shot_frames = (uint8_t)((powers & (1u << POWER_RANGE)) ? SHOT_RANGE_POWER : SHOOT_FRAMES);
    b->shot_speed = (powers & (1u << POWER_SPEED)) ? SHOOT_SPEED_POWER : SHOOT_SPEED;
    if (g_count.blown < 255) {
        g_count.blown++;
    }
    b->dir = dir;
    b->angry = 0;
    b->trapped = 0;
    b->pop_timer = 0;
    b->anim = (uint8_t)(rng_next() & 3);
    b->age = 0;
    b->x = x;
    b->y = y;
    level_clamp_x(&b->x);
}

void bubble_pop(Bubble *b, int8_t dir, uint8_t player)
{
    if (!floating(b)) {
        return;
    }
    b->trapped = (uint8_t)(b->state == BS_TRAPPED);
    b->state = BS_POPPING;
    b->pop_timer = 1;
    chain_dir = dir;
    chain_owner = player;
}

void bubbles_trapped_to_gems(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if (b->state == BS_TRAPPED || (b->state == BS_POPPING && b->trapped)) {
            item_spawn_thrown(b->x, b->y, (int8_t)((i & 1) ? -1 : 1), GEM_TIER);
            b->state = BS_OFF;
        }
    }
}

uint8_t push_shoot(uint8_t player, uint16_t x, uint16_t y, uint8_t dir)
{
    PushShot *s = &push_shots[player];
    if (s->active) {
        return 0;          /* one at a time */
    }
    s->active = 1;
    s->age = 0;
    s->dir = dir;
    s->x = x;
    s->y = y;
    level_clamp_x(&s->x);
    return 1;
}

uint8_t push_active(uint8_t player)
{
    return push_shots[player].active;
}

uint8_t bubbles_trapped_count(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        n = (uint8_t)(n + (b->state == BS_TRAPPED || (b->state == BS_POPPING && b->trapped)));
    }
    return n;
}

static void clamp_bubble(Bubble *b)
{
    level_clamp_x(&b->x);
    if (PX(b->y) < TOP_LIMIT || PX(b->y) >= WRAP_Y) {
        b->y = TO_POS(TOP_LIMIT);
    } else if (PX(b->y) > BOTTOM_LIMIT) {
        b->y = TO_POS(BOTTOM_LIMIT);
    }
}

static void drift(Bubble *b, fx8_8 speed)
{
    switch (b->dir) {
    case AIR_UP:    b->y = (uint16_t)(b->y - (uint16_t)speed); break;
    case AIR_DOWN:  b->y = (uint16_t)(b->y + (uint16_t)speed); break;
    case AIR_LEFT:  b->x = (uint16_t)(b->x - (uint16_t)speed); break;
    case AIR_RIGHT: b->x = (uint16_t)(b->x + (uint16_t)speed); break;
    default: break;
    }
    clamp_bubble(b);
}

/* Burst one link of a chain and light the fuse of every bubble it touches. */
static void burst(Bubble *b)
{
    uint8_t i;

    if (b->trapped) {
        uint8_t tier = (uint8_t)(chain_kills < FOOD_TIERS - 1 ? chain_kills : FOOD_TIERS - 1);
        int8_t dir = chain_kills == 0 ? chain_dir : (int8_t)((rng_next() & 1) ? 1 : -1);
        if (chain_kills < 255) {
            chain_kills++;
        }
        item_spawn_thrown(b->x, b->y, dir, tier);
        /* The third enemy of a chain earns letters: say so right away,
         * once per chain, while the player still watches it. */
        sfx(chain_kills == 3 ? SFX_MULTIKILL : SFX_KILL);
    } else if (b->kind == BK_LETTER) {
        extend_collect(chain_owner, b->letter);
        item_spawn(IT_BURST, b->x, b->y, 0);
    } else if (b->kind != BK_NORMAL) {
        /* Lightning flies away from the player who popped it,
         * or straight at the boss in round 100. */
        int8_t dir = b->kind == BK_LIGHTNING ? (int8_t)-chain_dir : chain_dir;
        if (b->kind == BK_LIGHTNING && boss_fighting()) {
            dir = (int8_t)(boss_center_x() > b->x + TO_POS(8) ? 1 : -1);
        }
        if (b->kind == BK_WATER && g_count.water < 255) {
            g_count.water++;
        }
        effect_start(b->kind, b->x, b->y, dir, chain_owner);
        sfx(SFX_POP);
    } else {
        item_spawn(IT_BURST, b->x, b->y, 0);
        score_add(g_players[chain_owner].score, 1, 1);   /* +10 */
        if (g_count.popped < 255) {
            g_count.popped++;
        }
        sfx(SFX_POP);
    }
    b->state = BS_OFF;

    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *o = &g_bubbles[i];
        if (floating(o) && overlap(b->x, b->y, o->x, o->y, CHAIN_REACH, CHAIN_REACH)) {
            o->trapped = (uint8_t)(o->state == BS_TRAPPED);
            o->state = BS_POPPING;
            o->pop_timer = POP_DELAY;
        }
    }
}

/* An empty bubble on the move (fired, or shoved by a push bubble) traps
 * the first enemy it touches and stops. A shield enemy's plate bursts a
 * bubble that comes from the front. Returns 1 if the bubble is used up. */
static uint8_t catch_enemy(Bubble *b)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(b->x, b->y, e->x, e->y, 12, 12)) {
            if (enemy_shield_blocks(e, b->x)) {
                item_spawn(IT_BURST, b->x, b->y, 0);
                sfx(SFX_POP);
                b->state = BS_OFF;
                return 1;
            }
            e->active = 0;
            sfx(SFX_TRAP);
            b->etype = e->type;
            b->angry = e->angry;
            b->state = BS_TRAPPED;
            b->trapped = 1;
            b->age = 0;
            b->dir = AIR_UP;
            b->vx = 0;
            b->vy = 0;
            return 1;
        }
    }
    return 0;
}

/* Plus mode: shots stop at walls and platforms (only a push gets a bubble
 * through); the leading edge decides. In Arcade mode they only know the
 * room's outer walls. */
static uint8_t shot_hits_wall(const Bubble *b)
{
    int16_t x = (int16_t)(PX(b->x) + 8), y = (int16_t)(PX(b->y) + 8);
    if (!g_opt.plus) {
        return 0;
    }
    if (b->dir == AIR_LEFT) {
        x = (int16_t)(x - SHOT_EDGE);
    } else if (b->dir == AIR_RIGHT) {
        x = (int16_t)(x + SHOT_EDGE);
    } else {
        y = (int16_t)(y - SHOT_EDGE);
    }
    return level_solid_px(x, y);
}

static void update_shot(Bubble *b)
{
    uint16_t bx = b->x, by = b->y;
    uint8_t stop = 0;

    if (b->dir == AIR_UP) {
        if (PX(b->y) < TOP_LIMIT + PX(b->shot_speed) + 1) {
            b->y = TO_POS(TOP_LIMIT);   /* the room's ceiling */
            stop = 1;
        } else {
            b->y = (uint16_t)(b->y - (uint16_t)b->shot_speed);
        }
    } else {
        b->x = (uint16_t)(b->dir == AIR_LEFT ? b->x - (uint16_t)b->shot_speed : b->x + (uint16_t)b->shot_speed);
        level_clamp_x(&b->x);
        stop = (uint8_t)(b->x == bx);
    }

    if (shot_hits_wall(b)) {
        b->x = bx;                       /* stays in front of the wall */
        b->y = by;
        stop = 1;
    } else if (catch_enemy(b)) {
        return;
    }

    if (stop || b->age > b->shot_frames || boss_touch(b->x, b->y)) {
        b->state = BS_FLOAT;
        b->dir = AIR_UP;
        /* In the boss round every bubble becomes a lightning bubble. */
        if (level_defs[g_level].kind == LEVEL_BOSS) {
            b->kind = BK_LIGHTNING;
            b->age = 1;
        }
    }
}

/* Floating bubbles push each other apart, the deeper the overlap the
 * harder, so they pile up in clusters instead of on top of each other. */
static int16_t abs16(int16_t v)
{
    return (int16_t)(v < 0 ? -v : v);
}

static void separate(void)
{
    uint8_t i, j;
    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *a = &g_bubbles[i];
        if (!floating(a)) {
            continue;
        }
        for (j = (uint8_t)(i + 1); j < MAX_BUBBLES; j++) {
            Bubble *b = &g_bubbles[j];
            int16_t dx, dy, adx, ady;
            uint16_t push;

            if (!floating(b)) {
                continue;
            }
            dx = (int16_t)((int16_t)PX(a->x) - (int16_t)PX(b->x));
            dy = (int16_t)((int16_t)PX(a->y) - (int16_t)PX(b->y));
            adx = abs16(dx);
            ady = abs16(dy);
            if (adx >= PUSH_REACH || ady >= PUSH_REACH) {
                continue;
            }
            if (adx >= ady) {
                push = (uint16_t)((PUSH_REACH - adx) << 4);
                if (dx > 0 || (dx == 0 && (i & 1))) {
                    a->x = (uint16_t)(a->x + push);
                    b->x = (uint16_t)(b->x - push);
                } else {
                    a->x = (uint16_t)(a->x - push);
                    b->x = (uint16_t)(b->x + push);
                }
            } else {
                push = (uint16_t)((PUSH_REACH - ady) << 4);
                if (dy > 0) {
                    a->y = (uint16_t)(a->y + push);
                    b->y = (uint16_t)(b->y - push);
                } else {
                    a->y = (uint16_t)(a->y - push);
                    b->y = (uint16_t)(b->y + push);
                }
            }
            clamp_bubble(a);
            clamp_bubble(b);
        }
    }
}

/* A shoved bubble slides, ignoring the air, and slows down; a wall stops
 * it. Only shifts, no multiplication (65816). */
static fx8_8 slow_down(fx8_8 v)
{
    v = (fx8_8)(v > 0 ? v - (v >> 4) : v + ((fx8_8)-v >> 4));
    return (fx8_8)(abs16(v) < SHOVE_STOP ? 0 : v);
}

static void slide(Bubble *b)
{
    uint16_t x = (uint16_t)(b->x + (uint16_t)b->vx);
    uint16_t y = (uint16_t)(b->y + (uint16_t)b->vy);
    b->x = x;
    b->y = y;
    clamp_bubble(b);
    b->vx = b->x != x ? 0 : slow_down(b->vx);
    b->vy = b->y != y ? 0 : slow_down(b->vy);
    if (b->state == BS_FLOAT && b->kind == BK_NORMAL) {
        (void)catch_enemy(b);   /* a shoved empty bubble traps like a fired one */
    }
}

/* Where the push bubble hits decides the shove: the upper third pushes the
 * bubble down, the lower third up, the middle along the push bubble's way. */
static void shove(Bubble *b, const PushShot *s)
{
    int16_t dy = (int16_t)((int16_t)PX(s->y) - (int16_t)PX(b->y));
    b->vx = 0;
    b->vy = 0;
    if (s->dir == AIR_UP) {
        b->vy = (fx8_8)-SHOVE_SPEED;     /* from below: always up */
    } else if (dy < -PUSH_THIRD) {
        b->vy = SHOVE_SPEED;
    } else if (dy > PUSH_THIRD) {
        b->vy = (fx8_8)-SHOVE_SPEED;
    } else {
        b->vx = (fx8_8)(s->dir == AIR_LEFT ? -SHOVE_SPEED : SHOVE_SPEED);
    }
}

static void push_burst(PushShot *s)
{
    item_spawn(IT_BURST, s->x, s->y, 0);
    s->active = 0;
}

/* It bursts on the first thing it meets: a floating bubble (shoved, or
 * set off like a touch if it is a special one), an enemy shot (destroyed),
 * an enemy, another player or the boss (nothing happens), or a wall. It
 * collects nothing. */
static void update_push(uint8_t id)
{
    PushShot *s = &push_shots[id];
    uint16_t before;
    uint8_t i;

    if (!s->active) {
        return;
    }
    before = s->x;
    if (s->dir == AIR_UP) {
        if (PX(s->y) < TOP_LIMIT + PX(PUSH_SPEED) + 1) {
            push_burst(s);               /* the room's ceiling */
            return;
        }
        s->y = (uint16_t)(s->y - (uint16_t)PUSH_SPEED);
        before = 0xFFFF;                 /* no side wall to meet */
    } else {
        s->x = (uint16_t)(s->dir == AIR_LEFT ? s->x - (uint16_t)PUSH_SPEED : s->x + (uint16_t)PUSH_SPEED);
        level_clamp_x(&s->x);
    }
    s->age++;

    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if (floating(b) && overlap(s->x, s->y, b->x, b->y, PUSH_HIT, PUSH_HIT)) {
            if (b->kind != BK_NORMAL) {
                bubble_pop(b, (int8_t)(s->dir == AIR_LEFT ? -1 : 1), id);
            } else {
                shove(b, s);
                sfx(SFX_POP);
            }
            push_burst(s);
            return;
        }
    }
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &g_items[i];
        if (it->kind == IT_ROCK && overlap(s->x, s->y, it->x, it->y, PUSH_HIT, PUSH_HIT)) {
            it->kind = IT_OFF;
            sfx(SFX_POP);
            push_burst(s);
            return;
        }
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(s->x, s->y, e->x, e->y, PUSH_HIT, PUSH_HIT)) {
            push_burst(s);
            return;
        }
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        if (i != id && (p->state == PS_ALIVE || p->state == PS_RESCUE) &&
            overlap(s->x, s->y, p->x, p->y, PUSH_HIT, PUSH_HIT)) {
            push_burst(s);
            return;
        }
    }
    if (boss_touch(s->x, s->y) || s->x == before || s->age >= PUSH_FRAMES) {
        push_burst(s);
    }
}

void bubbles_update(void)
{
    uint8_t i;
    uint8_t popping = 0;

    vacuums_tick();
    for (i = 0; i < NUM_PLAYERS; i++) {
        update_push(i);
    }

    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if (b->state == BS_OFF) {
            continue;
        }
        b->age++;

        if (b->state == BS_POPPING) {
            if (--b->pop_timer == 0) {
                burst(b);
            } else {
                popping = 1;
            }
            continue;
        }

        if (b->state == BS_SHOOT) {
            update_shot(b);
            continue;
        }

        if (b->vx || b->vy) {
            slide(b);              /* shoved by a push bubble */
        } else if (vacuum_move(&b->x, &b->y)) {
            clamp_bubble(b);       /* drawn in by a vacuum */
        } else {
            /* Follow the airflow; re-read it every 16 frames. */
            if ((b->age & 15) == 0) {
                b->dir = level_air_at((uint8_t)(PX(b->x) + 8), (uint8_t)(PX(b->y) + 8));
                if (b->dir == AIR_NONE) {
                    b->dir = (uint8_t)(1 + (rng_next() & 3));
                }
            }
            drift(b, b->state == BS_TRAPPED ? TRAP_SPEED : FLOAT_SPEED);
        }

        if (b->state == BS_FLOAT && b->kind != BK_NORMAL) {
            if (b->age >= SPECIAL_LIFE) {
                b->state = BS_OFF;   /* drifts away unused */
            }
        } else if (b->state == BS_FLOAT && b->age >= BUBBLE_LIFE) {
            item_spawn(IT_BURST, b->x, b->y, 0);
            b->state = BS_OFF;
        } else if (b->state == BS_TRAPPED && b->age >= TRAP_LIFE) {
            enemy_spawn(b->x, b->y, b->etype, 1, (uint8_t)(rng_next() & 1));
            item_spawn(IT_BURST, b->x, b->y, 0);
            b->state = BS_OFF;
        }
    }

    separate();

    /* A chain is over when nothing is popping any more. A dash holds it
     * open: everything one dash sweeps through is one chain, even bubbles
     * that do not touch (Plus mode, design guide "Dash"). */
    for (i = 0; i < MAX_BUBBLES && !popping; i++) {
        popping = (uint8_t)(g_bubbles[i].state == BS_POPPING);
    }
    if (!popping && !player_dash_invulnerable(&g_players[chain_owner])) {
        /* A chain of three or more enemies earns EXTEND letters. */
        if (chain_kills >= 3) {
            extend_letters_earned((uint8_t)((chain_kills - 1) >> 1));
        }
        chain_kills = 0;
    }
}

void bubbles_draw(void)
{
    static const uint8_t phase[4] = { 0, 1, 2, 1 };
    uint8_t i;

    for (i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        uint8_t ph, tile;
        if (b->state == BS_OFF) {
            continue;
        }
        ph = phase[((b->age >> 3) + b->anim) & 3];

        if (b->kind != BK_NORMAL && !(b->state == BS_POPPING && b->trapped)) {
            static const uint8_t letter_cell[6] = { 0, 1, 2, 0, 3, 4 };   /* E X T E N D */
            if (b->age >= SPECIAL_LIFE - 120 && (b->age & 4)) {
                continue;   /* about to drift away */
            }
            switch (b->kind) {
            case BK_WATER:     tile = SPR_SPECIAL_WATER; break;
            case BK_FIRE:      tile = SPR_SPECIAL_FIRE; break;
            case BK_LIGHTNING: tile = SPR_SPECIAL_LIGHTNING; break;
            default:           tile = (uint8_t)(SPR_LETTER0 + letter_cell[b->letter]); break;
            }
            oam_add(PX(b->x), PX(b->y), tile, 0);
            continue;
        }

        if (b->trapped && b->state != BS_OFF && b->state != BS_FLOAT) {
            /* A ring drawn over a small picture of the trapped enemy. */
            uint8_t inner = small_enemy_tile(b->etype);
            uint8_t inner_attr = (uint8_t)(b->angry ? (PAL_RED << SPR_ATTR_PAL_SHIFT) : 0u);
            if (b->age >= TRAP_RED) {
                if (b->age & 4) {
                    continue;   /* flashing: about to escape */
                }
                tile = (uint8_t)(SPR_RING_RED0 + ph);
            } else if (b->age >= TRAP_ORANGE) {
                tile = (uint8_t)(SPR_RING_ORANGE0 + ph);
            } else {
                tile = (uint8_t)(SPR_RING0 + ph);
            }
            oam_add(PX(b->x), PX(b->y), tile, 0);
            oam_add(PX(b->x), PX(b->y), inner, inner_attr);
            continue;
        } else if (b->state != BS_SHOOT && b->age >= BUBBLE_RED) {
            if (b->age & 4) {
                continue;
            }
            tile = (uint8_t)(SPR_BUBBLE_RED0 + ph);
        } else if (b->state != BS_SHOOT && b->age >= BUBBLE_ORANGE) {
            tile = (uint8_t)(SPR_BUBBLE_ORANGE0 + ph);
        } else {
            tile = (uint8_t)(SPR_BUBBLE0 + ph);
        }
        oam_add(PX(b->x), PX(b->y), tile, 0);
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (push_shots[i].active) {
            oam_add(PX(push_shots[i].x), PX(push_shots[i].y), SPR_PUSH, 0);
        }
    }
}
