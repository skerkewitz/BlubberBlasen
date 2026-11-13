/*
 * Bubbles: shot horizontally, then drift with the level's airflow.
 * A shot bubble that hits an enemy traps it; a trapped enemy escapes
 * (angry) after a while. Floating bubbles push each other apart, and a
 * popped bubble pops every bubble it touches (chain reaction); each trapped
 * enemy in one chain is worth twice as much as the one before.
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
#define SHOT_RANGE_POWER 14        /* SHOOT_FRAMES with the range candy */
#define SHOOT_SPEED_POWER FX(6, 0)
#define TOP_LIMIT     (FIELD_Y + 6)
#define BOTTOM_LIMIT  (FIELD_BOTTOM - 8 - 14)

static uint8_t chain_kills;   /* trapped enemies popped in the running chain */
static int8_t  chain_dir;     /* direction the first defeated enemy flies */
static uint8_t chain_owner;   /* player who started the chain */

void bubbles_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        g_bubbles[i].state = BS_OFF;
    }
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
    b->dir = AIR_UP;
    b->angry = 0;
    b->trapped = 0;
    b->pop_timer = 0;
    b->anim = 0;
    b->age = 1;
    b->x = x;
    b->y = y;
}

void bubble_shoot(uint16_t x, uint16_t y, uint8_t facing_left, uint8_t powers)
{
    Bubble *b = alloc_bubble();
    if (!b) {
        return;
    }
    b->state = BS_SHOOT;
    b->kind = BK_NORMAL;
    b->shot_frames = (uint8_t)((powers & (1u << POWER_RANGE)) ? SHOT_RANGE_POWER : SHOOT_FRAMES);
    b->shot_speed = (powers & (1u << POWER_SPEED)) ? SHOOT_SPEED_POWER : SHOOT_SPEED;
    if (g_count.blown < 255) {
        g_count.blown++;
    }
    b->dir = (uint8_t)(facing_left ? AIR_LEFT : AIR_RIGHT);
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
        sfx(SFX_KILL);
    } else if (b->kind == BK_LETTER) {
        extend_collect(chain_owner, b->letter);
        item_spawn(IT_BURST, b->x, b->y, 0);
    } else if (b->kind != BK_NORMAL) {
        /* Lightning flies away from the player who popped it. */
        int8_t dir = b->kind == BK_LIGHTNING ? (int8_t)-chain_dir : chain_dir;
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

static void update_shot(Bubble *b)
{
    uint8_t i;
    uint16_t before = b->x;

    b->x = (uint16_t)(b->dir == AIR_LEFT ? b->x - (uint16_t)b->shot_speed : b->x + (uint16_t)b->shot_speed);
    level_clamp_x(&b->x);

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(b->x, b->y, e->x, e->y, 12, 12)) {
            e->active = 0;
            b->state = BS_TRAPPED;
            b->trapped = 1;
            b->etype = e->type;
            b->angry = e->angry;
            b->age = 0;
            b->dir = AIR_UP;
            sfx(SFX_TRAP);
            return;
        }
    }

    if (b->age > b->shot_frames || b->x == before) {
        b->state = BS_FLOAT;
        b->dir = AIR_UP;
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

void bubbles_update(void)
{
    uint8_t i;
    uint8_t popping = 0;

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

        /* Follow the airflow; re-read it every 16 frames. */
        if ((b->age & 15) == 0) {
            b->dir = level_air_at((uint8_t)(PX(b->x) + 8), (uint8_t)(PX(b->y) + 8));
            if (b->dir == AIR_NONE) {
                b->dir = (uint8_t)(1 + (rng_next() & 3));
            }
        }
        drift(b, b->state == BS_TRAPPED ? TRAP_SPEED : FLOAT_SPEED);

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

    /* A chain is over when nothing is popping any more. */
    for (i = 0; i < MAX_BUBBLES && !popping; i++) {
        popping = (uint8_t)(g_bubbles[i].state == BS_POPPING);
    }
    if (!popping) {
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

        if (b->state == BS_TRAPPED || (b->state == BS_POPPING && b->trapped)) {
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
}
