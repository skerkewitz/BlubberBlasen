/*
 * Plus mode vacuum (docs/brainstorm/vacuum_2026-11-14.md): the player puts
 * a vacuum where they stand. For a short time the air inside a circle
 * around it points to its centre instead of following the room's airflow,
 * so floating bubbles (and a partner's rescue bubble) gather there; then the
 * room's air takes over again. Bubbles move faster the closer they get.
 *
 * The circle is shown the SNES way, with no sprites: a window (left and
 * right edge per scanline, an HDMA table on the real hardware) inside which
 * a colour is added to the background (colour math). The same table decides
 * which bubbles are inside, so what you see is exactly what pulls.
 *
 * No square roots: the circle and the speed come from small tables. The pull
 * goes along the axis with the bigger distance, like the room's air (four
 * directions only).
 */

#include "game/internal.h"

#define VACUUM_FRAMES 90    /* 1.5 s */
#define VACUUM_RADIUS 40    /* px, 5 tiles */
#define VACUUM_FADE   30    /* the glow fades out over the last frames */

typedef struct {
    uint8_t  timer;         /* frames left, 0 = off */
    uint16_t x, y;          /* 8.8, top left of the player who placed it */
} Vacuum;

static Vacuum vacuums[NUM_PLAYERS];

/* Half width of the circle (px) at each distance from its middle line. */
static const uint8_t half_width[VACUUM_RADIUS + 1] = {
    40, 39, 39, 39, 39, 39, 39, 39, 39, 38, 38, 38, 38, 37, 37, 37, 36, 36, 35, 35, 34,
    34, 33, 32, 32, 31, 30, 29, 28, 27, 26, 25, 24, 22, 21, 19, 17, 15, 12, 8, 0
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

void vacuums_clear(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        vacuums[i].timer = 0;
    }
}

void vacuum_start(uint8_t player, uint16_t x, uint16_t y)
{
    vacuums[player].timer = VACUUM_FRAMES;   /* a second press moves it here */
    vacuums[player].x = x;
    vacuums[player].y = y;
}

uint8_t vacuum_active(uint8_t player)
{
    return (uint8_t)(vacuums[player].timer != 0);
}

void vacuums_tick(void)
{
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (vacuums[i].timer) {
            vacuums[i].timer--;
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
        if (ady > VACUUM_RADIUS || adx > half_width[ady]) {
            continue;                    /* outside the circle */
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

/* Window i shows player i's vacuum: a soft blue glow that pulses gently
 * and fades out at the end. Colours are SNES 5-bit (0..31). */
void vacuums_windows(ColorWindow *w)
{
    static const uint8_t pulse[8] = { 0, 1, 2, 3, 3, 2, 1, 0 };
    uint8_t i;
    for (i = 0; i < NUM_PLAYERS && i < NUM_WINDOWS; i++) {
        const Vacuum *v = &vacuums[i];
        int16_t cx, cy, dy;
        uint8_t amount;
        if (!v->timer) {
            continue;
        }
        amount = (uint8_t)(3 + pulse[(g_frame >> 2) & 7]);
        if (v->timer < VACUUM_FADE / 3) {
            amount = (uint8_t)(amount >> 2);
        } else if (v->timer < VACUUM_FADE) {
            amount = (uint8_t)(amount >> 1);
        }
        w[i].add_r = (uint8_t)(amount >> 1);
        w[i].add_g = (uint8_t)(amount - (amount >> 2));
        w[i].add_b = amount;
        cx = (int16_t)(PX(v->x) + 8);
        cy = (int16_t)(PX(v->y) + 8);
        for (dy = -VACUUM_RADIUS; dy <= VACUUM_RADIUS; dy++) {
            int16_t line = (int16_t)(cy + dy);
            int16_t hw = half_width[iabs(dy)];
            int16_t l = (int16_t)(cx - hw), r = (int16_t)(cx + hw);
            if (line < FIELD_Y || line >= FIELD_BOTTOM) {
                continue;                /* not over the HUD */
            }
            w[i].left[line] = (uint8_t)(l < 0 ? 0 : l);
            w[i].right[line] = (uint8_t)(r > SCREEN_W - 1 ? SCREEN_W - 1 : r);
        }
    }
}
