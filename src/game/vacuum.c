/*
 * Plus mode vacuum (docs/brainstorm/vacuum_2026-11-14.md): the player puts
 * a vacuum where they stand. For a short time the air inside a circle
 * around it points to its centre instead of following the room's airflow,
 * so floating bubbles (and a partner's rescue bubble) gather there; then the
 * room's air takes over again. Bubbles move faster the closer they get.
 *
 * The area is shown the SNES way, with no sprites: a window (left and
 * right edge per scanline, an HDMA table on the real hardware) inside which
 * a colour is added to the background (colour math). The same span table
 * decides which bubbles are inside, so what you see is exactly what pulls.
 *
 * It lives in three phases: it grows from a point to full size in a few
 * frames, wobbles like jelly while active (round, tall, round, wide), then
 * shrinks back to a point while its colour fades. The wobble is an ellipse:
 * one quarter-circle table, stretched to the current width and height.
 *
 * No square roots: the shape and the speed come from small tables. The pull
 * goes along the axis with the bigger distance, like the room's air (four
 * directions only).
 */

#include "game/internal.h"

#define VACUUM_FRAMES 90    /* 1.5 s in all */
#define VACUUM_RADIUS 40    /* px, 5 tiles: the round size */
#define VACUUM_SHRINK 12    /* last frames: shrink to a point and fade */
#define VACUUM_WOBBLE 4     /* px the ellipse leans each way */
#define VACUUM_MAX    (VACUUM_RADIUS + VACUUM_WOBBLE)

typedef struct {
    uint8_t  timer;                   /* frames left, 0 = off */
    uint16_t scale;                   /* size this frame, 0..256 = full */
    uint8_t  ry;                      /* half height this frame (px) */
    uint16_t x, y;                    /* 8.8, top left of the player who placed it */
    uint8_t  span[VACUUM_MAX + 1];    /* half width at each distance from the middle line */
} Vacuum;

static Vacuum vacuums[NUM_PLAYERS];

/* Half width of the round circle (px) at each distance from its middle line. */
static const uint8_t half_width[VACUUM_RADIUS + 1] = {
    40, 39, 39, 39, 39, 39, 39, 39, 39, 38, 38, 38, 38, 37, 37, 37, 36, 36, 35, 35, 34,
    34, 33, 32, 32, 31, 30, 29, 28, 27, 26, 25, 24, 22, 21, 19, 17, 15, 12, 8, 0
};

/* Size while growing, by frames since placed (0..256 = full): fast from a
 * point, slowing down at the end. */
static const uint16_t grow_scale[VACUUM_GROW] = { 0, 60, 112, 156, 192, 220, 240, 252 };

/* Size while shrinking, by frames left: slow at first, then collapsing. */
static const uint16_t shrink_scale[VACUUM_SHRINK] = { 0, 41, 78, 112, 142, 169, 192, 212, 228, 240, 249, 254 };

/* One wobble period (32 frames, two per step), in 1/16 px: + is taller. */
static const int8_t wobble[16] = {
    0, 24, 45, 59, 64, 59, 45, 24, 0, -24, -45, -59, -64, -59, -45, -24
};

/* Distance in tiles from the centre (rounded), by tiles apart on the two
 * axes, capped at 5 (the edge of the circle). */
static const uint8_t ring_of[6][6] = {
    { 0, 1, 2, 3, 4, 5 },
    { 1, 1, 2, 3, 4, 5 },
    { 2, 2, 3, 4, 4, 5 },
    { 3, 3, 4, 4, 5, 5 },
    { 4, 4, 4, 5, 5, 5 },
    { 5, 5, 5, 5, 5, 5 },
};

/* Faster the closer to the centre: 3 px/frame there, 1 at the edge. */
static const fx8_8 ring_speed[6] = {
    FX(3, 0), FX(2, 128), FX(2, 0), FX(1, 128), FX(1, 64), FX(1, 0)
};

/* This frame's size (0..256) and the span table of its ellipse. */
static void shape(Vacuum *v)
{
    uint8_t age = (uint8_t)(VACUUM_FRAMES - v->timer);
    uint16_t s = 256;
    int16_t w = 0, rx, ry, dy;

    if (age < VACUUM_GROW) {
        s = grow_scale[age];
    } else if (v->timer < VACUUM_SHRINK) {
        s = shrink_scale[v->timer];
    }
    if (age >= VACUUM_GROW) {
        w = (int16_t)(wobble[((age - VACUUM_GROW) >> 1) & 15] * VACUUM_WOBBLE / 4);
    }
    rx = (int16_t)(((int32_t)(VACUUM_RADIUS * 16 - w) * s + 2048) >> 12);
    ry = (int16_t)(((int32_t)(VACUUM_RADIUS * 16 + w) * s + 2048) >> 12);
    if (rx < 1) rx = 1;
    if (ry < 1) ry = 1;

    v->scale = s;
    v->ry = (uint8_t)ry;
    for (dy = 0; dy <= ry; dy++) {
        int16_t row = (int16_t)((dy * VACUUM_RADIUS + ry / 2) / ry);   /* row of the round table */
        v->span[dy] = (uint8_t)((half_width[row] * rx + VACUUM_RADIUS / 2) / VACUUM_RADIUS);
    }
}

void vacuums_clear(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        vacuums[i].timer = 0;
    }
}

void vacuum_start(uint8_t player, uint16_t x, uint16_t y)
{
    Vacuum *v = &vacuums[player];
    v->timer = VACUUM_FRAMES;   /* a second press moves it here, growing anew */
    v->x = x;
    v->y = y;
    shape(v);
}

uint8_t vacuum_active(uint8_t player)
{
    return (uint8_t)(vacuums[player].timer != 0);
}

void vacuums_tick(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (vacuums[i].timer && --vacuums[i].timer) {
            shape(&vacuums[i]);
        }
    }
}

static int16_t iabs(int16_t v)
{
    return (int16_t)(v < 0 ? -v : v);
}

/* One step of `speed` along *pos towards target, never past it. */
static uint16_t step_to(uint16_t pos, uint16_t target, fx8_8 speed)
{
    int16_t d = (int16_t)(target - pos);   /* within the circle, so it fits */
    if (iabs(d) <= speed) {
        return target;
    }
    return (uint16_t)(d > 0 ? pos + (uint16_t)speed : pos - (uint16_t)speed);
}

/* Developer switch: walls stop bubbles inside a vacuum (to compare). */
static uint8_t blocked(uint16_t x, uint16_t y)
{
    return (uint8_t)(DBG(DBG_VACUUM_WALLS) && level_solid_px((int16_t)(PX(x) + 8), (int16_t)(PX(y) + 8)));
}

uint8_t vacuum_move(uint16_t *x, uint16_t *y)
{
    const Vacuum *best = 0;
    uint8_t best_ring = 0xFF, i;
    int16_t dx, dy;
    fx8_8 speed;
    uint16_t nx, ny;

    for (i = 0; i < NUM_PLAYERS; i++) {
        const Vacuum *v = &vacuums[i];
        int16_t adx, ady;
        uint8_t ring;
        if (!v->timer) {
            continue;
        }
        adx = iabs((int16_t)((int16_t)PX(*x) - (int16_t)PX(v->x)));
        ady = iabs((int16_t)((int16_t)PX(*y) - (int16_t)PX(v->y)));
        if (ady > v->ry || adx > v->span[ady]) {
            continue;                    /* outside the shape shown this frame */
        }
        ring = ring_of[ady >> 3][adx >> 3];
        if (ring < best_ring) {          /* the nearer vacuum wins */
            best_ring = ring;
            best = v;
        }
    }
    if (!best) {
        return 0;                        /* the room's air as usual */
    }

    speed = ring_speed[best_ring];
    dx = (int16_t)(best->x - *x);
    dy = (int16_t)(best->y - *y);
    if (dx == 0 && dy == 0) {
        return 1;                        /* at the centre: stay */
    }
    nx = *x;
    ny = *y;
    if (iabs(dx) >= iabs(dy)) {
        nx = step_to(*x, best->x, speed);
    } else {
        ny = step_to(*y, best->y, speed);
    }
    if (blocked(nx, ny)) {
        /* the main way is walled off: try the other axis */
        nx = *x;
        ny = *y;
        if (iabs(dx) >= iabs(dy)) {
            ny = step_to(*y, best->y, speed);
        } else {
            nx = step_to(*x, best->x, speed);
        }
        if (blocked(nx, ny)) {
            return 1;                    /* stuck behind the wall, held */
        }
    }
    *x = nx;
    *y = ny;
    return 1;
}

/* Window i shows player i's vacuum: a soft blue glow that pulses gently,
 * in this frame's shape. It fades while the vacuum shrinks at the end.
 * Colours are SNES 5-bit (0..31). Returns bit i set for each window it
 * wrote lines into. */
uint8_t vacuums_windows(ColorWindow *w)
{
    static const uint8_t pulse[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    uint8_t i, written = 0;
    for (i = 0; i < NUM_PLAYERS && i < NUM_WINDOWS; i++) {
        const Vacuum *v = &vacuums[i];
        ColorWindow *wi = &w[i];   /* once: 816-tcc multiplies for every w[i] */
        int16_t cx, cy, dy;
        uint8_t amount;
        if (!v->timer) {
            continue;
        }
        written = (uint8_t)(written | (1u << i));
        amount = (uint8_t)(3 + pulse[(g_frame >> 2) & 7]);
        if (v->timer < VACUUM_SHRINK) {
            amount = (uint8_t)(((uint16_t)amount * v->scale) >> 8);
        }
        wi->add_r = (uint8_t)(amount >> 1);
        wi->add_g = (uint8_t)(amount - (amount >> 2));
        wi->add_b = amount;
        cx = (int16_t)(PX(v->x) + 8);
        cy = (int16_t)(PX(v->y) + 8);
        for (dy = (int16_t)-v->ry; dy <= v->ry; dy++) {
            int16_t line = (int16_t)(cy + dy);
            uint8_t ady = (uint8_t)iabs(dy);   /* two steps: 816-tcc fails on v->span[iabs(dy)] (ICE 2) */
            int16_t hw = v->span[ady];
            int16_t l = (int16_t)(cx - hw), r = (int16_t)(cx + hw);
            if (line < FIELD_Y || line >= FIELD_BOTTOM) {
                continue;                /* not over the HUD */
            }
            wi->left[line] = (uint8_t)(l < 0 ? 0 : l);
            wi->right[line] = (uint8_t)(r > SCREEN_W - 1 ? SCREEN_W - 1 : r);
        }
    }
    return written;
}
