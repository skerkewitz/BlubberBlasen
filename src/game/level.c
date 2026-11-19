/*
 * Level: tile collision, airflow and the background tilemap of a round.
 * Ported from StaticMapContent / LevelUtils / World of the Java version.
 */

#include "game/internal.h"

static const uint8_t *cells;

/* Tile numbers inside a 6 x 3 tileset image (index = row * 6 + column). */
enum {
    TS_BLOCK_TL = 0, TS_BLOCK_TR = 1, TS_DEFAULT = 2, TS_SHADOW_START_V = 4,
    TS_BLOCK_BL = 6, TS_BLOCK_BR = 7, TS_SHADOW_INNER = 8, TS_SHADOW_H = 9,
    TS_SHADOW_OUTER = 10, TS_SHADOW_START_H = 11, TS_SHADOW_V = 14
};

/* Solid test in tile space. Left/right of the map is solid (walls),
 * above and below is open (objects can fall through gaps and wrap). */
uint8_t level_solid_tile(int16_t tx, int16_t ty)
{
    if (tx < 0 || tx >= LEVEL_W) {
        return 1;
    }
    if (ty < 0 || ty >= LEVEL_H) {
        return 0;
    }
    return (uint8_t)((cells[((uint16_t)ty << 5) + (uint16_t)tx] & CELL_SOLID) != 0);
}

uint8_t level_solid_px(int16_t sx, int16_t sy)
{
    if (sy < FIELD_Y) {
        return 0;
    }
    return level_solid_tile((int16_t)(sx >> 3), (int16_t)((sy - FIELD_Y) >> 3));
}

/* Like level_solid_px, but the top border row only counts at the walls,
 * so nothing can stand on top of the level. */
static uint8_t ground_px(int16_t sx, int16_t sy)
{
    int16_t tx = (int16_t)(sx >> 3);
    if (sy < FIELD_Y + 8 && tx >= 2 && tx < LEVEL_W - 2) {
        return 0;
    }
    return level_solid_px(sx, sy);
}

uint8_t level_cell(uint8_t tx, uint8_t ty)
{
    if (tx >= LEVEL_W || ty >= LEVEL_H) {
        return 0;
    }
    return cells[((uint16_t)ty << 5) + tx];
}

uint8_t level_air_at(uint8_t sx, uint8_t sy)
{
    if (sy < FIELD_Y || sy >= FIELD_BOTTOM) {
        return AIR_NONE;
    }
    uint8_t ty = (uint8_t)((sy - FIELD_Y) >> 3);
    return (uint8_t)(cells[((uint16_t)ty << 5) + (sx >> 3)] & CELL_AIR_MASK);
}

/* Standing: the feet pixel is free and the pixel below is solid, checked
 * under both sides of the body (centre - 4 and centre + 3). */
uint8_t level_on_ground(uint16_t x, uint16_t y)
{
    int16_t fy = (int16_t)(PX(y) + 15);
    int16_t lx = (int16_t)(PX(x) + 4);
    int16_t rx = (int16_t)(PX(x) + 11);

    if (!ground_px(lx, fy) && ground_px(lx, (int16_t)(fy + 1))) {
        return 1;
    }
    return (uint8_t)(!ground_px(rx, fy) && ground_px(rx, (int16_t)(fy + 1)));
}

void level_wrap_y(uint16_t *y)
{
    if (PX(*y) >= WRAP_Y) {
        *y = (uint16_t)(*y - TO_POS(WRAP_Y));
    }
}

/* Move down by dy, one pixel at a time, and stop on the first ground. */
uint8_t level_fall(uint16_t *x, uint16_t *y, fx8_8 dy)
{
    uint16_t target = (uint16_t)(*y + (uint16_t)dy);

    while (PX(*y) != PX(target)) {
        if (level_on_ground(*x, *y)) {
            *y &= 0xFF00u;
            return 1;
        }
        *y = (uint16_t)(*y + 0x100u);
        if (PX(*y) >= WRAP_Y) {
            *y = (uint16_t)(*y - TO_POS(WRAP_Y));
            target = (uint16_t)(target - TO_POS(WRAP_Y));
        }
    }
    *y = target;
    if (level_on_ground(*x, *y)) {
        *y &= 0xFF00u;
        return 1;
    }
    return 0;
}

/* Blocked if the next pixel at feet height is solid while the current one
 * is not (one-way platforms can be crossed from inside). */
uint8_t level_wall_ahead(uint16_t x, uint16_t y, int8_t dir)
{
    int16_t fy = (int16_t)(PX(y) + 15);
    int16_t edge = (int16_t)(dir < 0 ? PX(x) + 2 : PX(x) + 13);
    if (level_solid_px(edge, fy)) {
        return 0;
    }
    return level_solid_px((int16_t)(edge + dir), fy);
}

void level_clamp_x(uint16_t *x)
{
    uint8_t cx = (uint8_t)(PX(*x) + 8);
    if (cx < BODY_MIN_CX) {
        *x = TO_POS(BODY_MIN_CX - 8);
    } else if (cx > BODY_MAX_CX) {
        *x = TO_POS(BODY_MAX_CX - 8);
    }
}

static int16_t feet_tile_row(uint16_t y)
{
    return (int16_t)(((int16_t)PX(y) + 16 - FIELD_Y) >> 3);   /* row the feet stand on */
}

/* A platform exactly five rows up, with room above it. Row 0 is the
 * ceiling, not a platform: jumping at it would carry the enemy out of the
 * top of the room, where it wraps to the bottom. */
uint8_t level_can_jump_up(uint16_t x, uint16_t y)
{
    int16_t tx = (int16_t)((PX(x) + 8) >> 3);
    int16_t ty = feet_tile_row(y);

    if (ty - 5 < 1) {
        return 0;
    }

    for (uint8_t i = 1; i <= 4; i++) {
        if (level_solid_tile(tx, (int16_t)(ty - i))) {
            return 0;
        }
    }
    return (uint8_t)(level_solid_tile(tx, (int16_t)(ty - 5)) && !level_solid_tile(tx, (int16_t)(ty - 6)));
}

uint8_t level_gap_ahead(uint16_t x, uint16_t y, int8_t dir)
{
    int16_t edge = (int16_t)(dir < 0 ? PX(x) + 1 : PX(x) + 14);
    return (uint8_t)!ground_px(edge, (int16_t)(PX(y) + 16));
}

/* Ground within four tiles ahead and nothing in the way at head height. */
uint8_t level_can_gap_jump(uint16_t x, uint16_t y, int8_t dir)
{
    int16_t tx = (int16_t)(((dir < 0 ? PX(x) + 1 : PX(x) + 14)) >> 3);
    int16_t ty = feet_tile_row(y);

    for (uint8_t i = 0; i < 4; i++) {
        if (level_solid_tile(tx, (int16_t)(ty - 1))) {
            return 0;
        }
        if (level_solid_tile(tx, ty)) {
            return 1;
        }
        tx = (int16_t)(tx + dir);
    }
    return 0;
}

/* Pick the tileset graphic for a cell from its neighbours (blocks, and the
 * drop shadow they cast to the bottom right). */
static uint8_t autotile(int16_t x, int16_t y)
{
    if (level_solid_tile(x, y) && x >= 0 && x < LEVEL_W) {
        if (x == 0 || x == LEVEL_W - 2) {
            return (y & 1) ? TS_BLOCK_BL : TS_BLOCK_TL;
        }
        if (x == 1 || x == LEVEL_W - 1) {
            return (y & 1) ? TS_BLOCK_BR : TS_BLOCK_TR;
        }
        return TS_DEFAULT;
    }

    uint8_t above      = (uint8_t)(y > 0 && level_solid_tile(x, (int16_t)(y - 1)));
    uint8_t left       = (uint8_t)(x > 0 && level_solid_tile((int16_t)(x - 1), y));
    uint8_t above_left = (uint8_t)(x > 0 && y > 0 && level_solid_tile((int16_t)(x - 1), (int16_t)(y - 1)));

    if (above) {
        if (left) {
            return TS_SHADOW_INNER;
        }
        return above_left ? TS_SHADOW_H : TS_SHADOW_START_H;
    }
    if (left) {
        return above_left ? TS_SHADOW_V : TS_SHADOW_START_V;
    }
    if (above_left) {
        return TS_SHADOW_OUTER;
    }
    return 0xFF;   /* empty */
}

/* Special bubbles drift in through a gap in the bottom row, else the top
 * row; rooms without an opening release them near the bottom centre. */
void level_opening(uint16_t *x, uint16_t *y)
{
    for (int16_t row = LEVEL_H - 1; row >= 0; row = (int16_t)(row - (LEVEL_H - 1))) {
        for (int16_t c = 2; c < LEVEL_W - 2; c++) {
            if (!level_solid_tile(c, row)) {
                *x = TO_POS(c << 3);   /* centred over the first open column */
                *y = TO_POS(row == 0 ? FIELD_Y + 8 : FIELD_BOTTOM - 8 - 16);
                return;
            }
        }
        if (row == 0) {
            break;
        }
    }
    *x = TO_POS(120);
    *y = TO_POS(FIELD_BOTTOM - 40);
}

void level_load(uint8_t index, uint8_t page)
{
    uint8_t ts = level_defs[index].tileset;

    if (ts >= CHR_TILESET_COUNT) {
        ts = 0;
    }
    uint16_t base = (uint16_t)(CHR_TILESET_BASE + ((uint16_t)ts << 5));

    cells = level_defs[index].cells;
    video_tileset(page, ts);

    {
        uint16_t *dst = &g_bg0[(uint16_t)(page * PAGE_ROWS) << 5];
        for (uint16_t i = 0; i < BG_W * PAGE_ROWS; i++) {
            dst[i] = 0;
        }
    }
    for (int16_t y = 0; y < PAGE_ROWS; y++) {
        bg_mark_row(0, (uint8_t)(page * PAGE_ROWS + y));
    }
    for (int16_t y = 0; y < LEVEL_H; y++) {
        uint16_t *row = &g_bg0[(uint16_t)(page * PAGE_ROWS + y + HUD_ROWS) << 5];
        for (int16_t x = 0; x < LEVEL_W; x++) {
            uint8_t t = autotile(x, y);
            row[x] = (t == 0xFF) ? 0u : (uint16_t)(base + t);
        }
    }
}
