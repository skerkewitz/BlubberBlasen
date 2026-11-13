/*
 * Short-lived objects: a defeated enemy flying through the room (thrown),
 * falling down and turning into a gem; gems; bubble bursts; score pop-ups.
 * Ported from ThrownEnemySystem / BonusItemSystem.
 */

#include "game/internal.h"

#define THROWN_SPEED   FX(2, 128)   /* 2.5 px/frame */
#define THROWN_FRAMES  90           /* + random 0..63 */
#define GEM_FRAMES     600
#define BURST_FRAMES   16
#define POPUP_FRAMES   60
#define POPUP_SPEED    FX(0, 100)
#define THROWN_TOP     (FIELD_Y + 8)
#define THROWN_BOTTOM  (FIELD_BOTTOM - 8 - 16)

void items_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        g_items[i].kind = IT_OFF;
    }
}

void item_spawn(uint8_t kind, uint16_t x, uint16_t y, int8_t dx, uint8_t tile)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &g_items[i];
        if (it->kind != IT_OFF) {
            continue;
        }
        it->kind = kind;
        it->x = x;
        it->y = y;
        it->dx = dx;
        it->dy = -1;
        it->tile = tile;
        switch (kind) {
        case IT_THROWN: it->timer = (uint16_t)(THROWN_FRAMES + (rng_next() & 63)); break;
        case IT_BURST:  it->timer = BURST_FRAMES; break;
        case IT_POPUP:  it->timer = POPUP_FRAMES; break;
        default:        it->timer = GEM_FRAMES; break;
        }
        return;
    }
}

uint8_t items_thrown_count(void)
{
    uint8_t i, n = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        n = (uint8_t)(n + (g_items[i].kind == IT_THROWN || g_items[i].kind == IT_FALLING));
    }
    return n;
}

static void update_thrown(Item *it)
{
    uint8_t cx;
    it->x = (uint16_t)(it->dx < 0 ? it->x - (uint16_t)THROWN_SPEED : it->x + (uint16_t)THROWN_SPEED);
    it->y = (uint16_t)(it->dy < 0 ? it->y - (uint16_t)THROWN_SPEED : it->y + (uint16_t)THROWN_SPEED);

    cx = (uint8_t)(PX(it->x) + 8);
    if (cx < BODY_MIN_CX || cx > BODY_MAX_CX || PX(it->x) >= 0xF0u) {
        it->dx = (int8_t)(cx > 128 ? -1 : 1);
        level_clamp_x(&it->x);
    }
    if (PX(it->y) < THROWN_TOP || PX(it->y) >= WRAP_Y) {
        it->y = TO_POS(THROWN_TOP);
        it->dy = 1;
    } else if (PX(it->y) > THROWN_BOTTOM) {
        it->y = TO_POS(THROWN_BOTTOM);
        it->dy = -1;
    }
    if (--it->timer == 0) {
        it->kind = IT_FALLING;
    }
}

void items_update(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &g_items[i];
        switch (it->kind) {
        case IT_THROWN:
            update_thrown(it);
            break;

        case IT_FALLING:
            if (level_fall(&it->x, &it->y, FX(1, 0))) {
                it->kind = IT_GEM;
                it->timer = GEM_FRAMES;
            }
            break;

        case IT_GEM:
            if (g_player.state == PS_ALIVE && overlap(g_player.x, g_player.y, it->x, it->y, 12, 12)) {
                score_add(g_score, 2, 5);   /* +500 */
                sfx(SFX_GEM);
                it->kind = IT_POPUP;
                it->tile = SPR_PTS500;
                it->timer = POPUP_FRAMES;
            } else if (--it->timer == 0) {
                it->kind = IT_OFF;
            }
            break;

        case IT_POPUP:
            it->y = (uint16_t)(it->y - (uint16_t)POPUP_SPEED);
            if (--it->timer == 0 || PX(it->y) < 4) {
                it->kind = IT_OFF;
            }
            break;

        case IT_BURST:
            if (--it->timer == 0) {
                it->kind = IT_OFF;
            }
            break;

        default:
            break;
        }
    }
}

void items_draw(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        const Item *it = &g_items[i];
        uint8_t tile;
        switch (it->kind) {
        case IT_THROWN:
        case IT_FALLING: tile = (uint8_t)(SPR_E_THROWN0 + ((g_frame >> 2) & 3)); break;
        case IT_GEM:
            if (it->timer < 120 && (it->timer & 4)) {
                continue;   /* about to vanish */
            }
            tile = (uint8_t)(SPR_GEM0 + ((g_frame >> 4) & 1));
            break;
        case IT_BURST:   tile = (uint8_t)(it->timer > BURST_FRAMES / 2 ? SPR_BURST0 : SPR_BURST1); break;
        case IT_POPUP:   tile = it->tile; break;
        default:         continue;
        }
        oam_add(PX(it->x), PX(it->y), tile, 0);
    }
}
