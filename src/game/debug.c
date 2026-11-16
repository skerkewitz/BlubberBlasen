/*
 * Debug switches for development builds (design guide: "Debug switches").
 * The whole file is only compiled with BB_DEBUG; public builds contain none
 * of it, and every DBG() check in the game is then a constant 0.
 * Frame step and slow motion live in the platform layer (they control the tick).
 */

#include "game/internal.h"

#ifdef BB_DEBUG

uint8_t g_dbg[DBG_COUNT];
uint16_t g_dbg_deaths[DEBUG_DEATH_CAUSES];
uint8_t  g_dbg_touch;

const uint16_t *game_debug_death_causes(void)
{
    return g_dbg_deaths;
}

void game_debug_set(uint8_t sw, uint8_t on)
{
    if (sw < DBG_COUNT) {
        g_dbg[sw] = (uint8_t)(on != 0);
    }
}

uint8_t game_debug_get(uint8_t sw)
{
    return sw < DBG_COUNT ? g_dbg[sw] : 0;
}

uint8_t game_debug_room_shown(void)
{
    return (uint8_t)(g_state == GS_ROUND_INTRO || g_state == GS_PLAY || g_state == GS_PAUSE || g_state == GS_ROUND_SKIP ||
                     g_state == GS_CONTINUE || g_state == GS_GAME_OVER);
}

uint8_t game_debug_cell(uint8_t tx, uint8_t ty)
{
    return level_cell(tx, ty);
}

static uint8_t add_box(DebugBox *out, uint8_t n, uint8_t max, uint16_t x, uint16_t y, uint8_t w, uint8_t h, uint8_t kind)
{
    if (n < max) {
        /* centred on the 16x16 sprite at (x, y) */
        out[n].x = (uint8_t)(PX(x) + 8 - (w >> 1));
        out[n].y = (uint8_t)(PX(y) + 8 - (h >> 1));
        out[n].w = w;
        out[n].h = h;
        out[n].kind = kind;
        n++;
    }
    return n;
}

/* Collision boxes as the game checks them (overlap() sizes, doubled). */
uint8_t game_debug_boxes(DebugBox *out, uint8_t max)
{
    uint8_t i, n = 0;
    for (i = 0; i < NUM_PLAYERS; i++) {
        if (g_players[i].state == PS_ALIVE) {
            n = add_box(out, n, max, g_players[i].x, g_players[i].y, 20, 24, 0);
        }
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        if (g_enemies[i].active && !g_enemies[i].spawn_delay) {
            n = add_box(out, n, max, g_enemies[i].x, g_enemies[i].y, 20, 24, 1);
        }
    }
    for (i = 0; i < MAX_BUBBLES; i++) {
        if (g_bubbles[i].state != BS_OFF) {
            n = add_box(out, n, max, g_bubbles[i].x, g_bubbles[i].y, 24, 24, 2);
        }
    }
    for (i = 0; i < MAX_ITEMS; i++) {
        if (g_items[i].kind != IT_OFF && g_items[i].kind != IT_POPUP && g_items[i].kind != IT_BURST) {
            n = add_box(out, n, max, g_items[i].x, g_items[i].y, 16, 16, 3);
        }
    }
    if (boss_fighting()) {
        n = add_box(out, n, max, (uint16_t)(boss_center_x() - TO_POS(8)), (uint16_t)(boss_center_y() - TO_POS(8)),
                    40, 40, 4);
    }
    return n;
}

static const char *const spawn_names[DEBUG_SPAWN_COUNT] = {
    "RANGE CANDY", "SPEED CANDY", "RATE CANDY", "SHOES", "UMBRELLA",
    "SOAP STAR", "RINSE WAVE", "STEAM WHISTLE", "FIRE GLOVE", "BUBBLE RING",
    "WATER BUBBLE", "FIRE BUBBLE", "LIGHTNING BUBBLE", "LETTER BUBBLE"
};

const char *game_debug_spawn_name(uint8_t what)
{
    return what < DEBUG_SPAWN_COUNT ? spawn_names[what] : "";
}

/* Places an item or special bubble a little in front of player 1. */
void game_debug_spawn(uint8_t what)
{
    const Player *p = &g_players[0];
    uint16_t x, y;

    if (g_state != GS_PLAY || p->state != PS_ALIVE) {
        return;
    }
    x = (uint16_t)(p->facing_left ? p->x - TO_POS(24) : p->x + TO_POS(24));
    level_clamp_x(&x);
    y = p->y;
    if (what < POWER_COUNT) {
        item_spawn_powerup(what, x, y);
    } else if (what < POWER_COUNT + 3) {
        bubble_spawn_special((uint8_t)(BK_WATER + what - POWER_COUNT), 0, x, (uint16_t)(y - TO_POS(8)));
    } else if (what < DEBUG_SPAWN_COUNT) {
        bubble_spawn_special(BK_LETTER, (uint8_t)(rng_next() % 6), x, (uint16_t)(y - TO_POS(8)));
    }
}

#endif /* BB_DEBUG */
