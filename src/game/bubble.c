/*
 * Bubbles: shot horizontally, then drift with the level's airflow.
 * A shot bubble that hits an enemy traps it; a trapped enemy escapes
 * (angry) after a while. Ported from AiBubbleSystem / AirflowSystem.
 */

#include "game/internal.h"

#define SHOOT_SPEED   FX(4, 0)
#define SHOOT_FRAMES  8
#define FLOAT_SPEED   FX(0, 90)    /* 0.35 px/frame */
#define TRAP_SPEED    FX(0, 77)    /* 0.30 px/frame */
#define BUBBLE_LIFE   600          /* 10 s, then it bursts */
#define TRAP_LIFE     300          /*  5 s, then the enemy escapes */
#define TRAP_WARN     90           /* blink before the escape */
#define TOP_LIMIT     (FIELD_Y + 6)
#define BOTTOM_LIMIT  (FIELD_BOTTOM - 8 - 14)

void bubbles_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        g_bubbles[i].state = BS_OFF;
    }
}

void bubble_shoot(uint16_t x, uint16_t y, uint8_t facing_left)
{
    uint8_t i, oldest = 0;
    Bubble *b;

    /* Take a free slot, or replace the oldest floating bubble. */
    for (i = 0; i < MAX_BUBBLES; i++) {
        if (g_bubbles[i].state == BS_OFF) {
            break;
        }
        if (g_bubbles[i].state == BS_FLOAT && g_bubbles[i].age > g_bubbles[oldest].age) {
            oldest = i;
        }
    }
    if (i == MAX_BUBBLES) {
        if (g_bubbles[oldest].state != BS_FLOAT) {
            return;
        }
        i = oldest;
    }

    b = &g_bubbles[i];
    b->state = BS_SHOOT;
    b->dir = (uint8_t)(facing_left ? AIR_LEFT : AIR_RIGHT);
    b->angry = 0;
    b->anim = (uint8_t)(rng_next() & 3);
    b->age = 0;
    b->x = x;
    b->y = y;
    level_clamp_x(&b->x);
}

uint8_t bubbles_trapped_count(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_BUBBLES; i++) {
        n = (uint8_t)(n + (g_bubbles[i].state == BS_TRAPPED));
    }
    return n;
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
    level_clamp_x(&b->x);
    if (PX(b->y) < TOP_LIMIT || PX(b->y) >= WRAP_Y) {
        b->y = TO_POS(TOP_LIMIT);
    } else if (PX(b->y) > BOTTOM_LIMIT) {
        b->y = TO_POS(BOTTOM_LIMIT);
    }
}

static void update_shot(Bubble *b)
{
    uint8_t i;
    uint16_t before = b->x;

    b->x = (uint16_t)(b->dir == AIR_LEFT ? b->x - (uint16_t)SHOOT_SPEED : b->x + (uint16_t)SHOOT_SPEED);
    level_clamp_x(&b->x);

    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(b->x, b->y, e->x, e->y, 12, 12)) {
            e->active = 0;
            b->state = BS_TRAPPED;
            b->angry = e->angry;
            b->age = 0;
            b->dir = AIR_UP;
            sfx(SFX_TRAP);
            return;
        }
    }

    if (b->age > SHOOT_FRAMES || b->x == before) {
        b->state = BS_FLOAT;
        b->dir = AIR_UP;
    }
}

void bubbles_update(void)
{
    uint8_t i;
    for (i = 0; i < MAX_BUBBLES; i++) {
        Bubble *b = &g_bubbles[i];
        if (b->state == BS_OFF) {
            continue;
        }
        b->age++;

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

        if (b->state == BS_FLOAT && b->age >= BUBBLE_LIFE) {
            item_spawn(IT_BURST, b->x, b->y, 0, 0);
            b->state = BS_OFF;
        } else if (b->state == BS_TRAPPED && b->age >= TRAP_LIFE) {
            enemy_spawn(b->x, b->y, 1, (uint8_t)(rng_next() & 1));
            item_spawn(IT_BURST, b->x, b->y, 0, 0);
            b->state = BS_OFF;
        }
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
        if (b->state == BS_TRAPPED) {
            if (b->age >= TRAP_LIFE - TRAP_WARN && (b->age & 4)) {
                continue;   /* blink: about to escape */
            }
            tile = b->angry ? (uint8_t)SPR_TRAP_ANGRY : (uint8_t)(SPR_TRAP0 + ph);
        } else {
            tile = (uint8_t)(SPR_BUBBLE0 + ph);
        }
        oam_add(PX(b->x), PX(b->y), tile, 0);
    }
}
