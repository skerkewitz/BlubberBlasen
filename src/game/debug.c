/*
 * Debug switches for development builds (design guide: "Debug switches").
 * The whole file is only compiled with BB_DEBUG (game.h); public builds and
 * the ROM contain none of it, and every DBG() check in the game is then a
 * constant 0. The debug view of superdevkit's desktop reaches the switches and
 * a few buttons through the registrations at the end (ADR-0010).
 * Frame step and slow motion live in the platform layer (they control the tick).
 */

#include "game/internal.h"
#include "sdevkit/snes.h"

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

/* Collision boxes, sized so that a player's box and another object's box
 * overlap exactly when the game counts a touch between them. overlap(w, h)
 * compares the distance of two objects with w and h, so each object of a
 * pair gets its share of that distance: two boxes of width a and b overlap
 * when the distance is below (a + b) / 2.
 *   player  10 x 12  enemy body: overlap(10, 12) with an enemy of the same size
 *   enemy   10 x 12
 *   bubble  14 x 12  touched by a player: overlap(12, 12) -> (10 + 14) / 2 = 12;
 *                    trapping an enemy, overlap(12, 12), comes out the same
 *   item    14 x 12  collected by a player: overlap(12, 12)
 *   rock     6 x  4  hits a player: overlap(8, 8)
 *   boss    30 x 28  touches(20) measures centres: 20 - 10/2 = 15, 20 - 12/2 = 14 */
uint8_t game_debug_boxes(DebugBox *out, uint8_t max)
{
    uint8_t n = 0;
    for (uint8_t i = 0; i < NUM_PLAYERS; i++) {
        if (g_players[i].state == PS_ALIVE) {
            n = add_box(out, n, max, g_players[i].x, g_players[i].y, 10, 12, 0);
        }
    }
    for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
        if (g_enemies[i].active && !g_enemies[i].spawn_delay) {
            n = add_box(out, n, max, g_enemies[i].x, g_enemies[i].y, 10, 12, 1);
        }
    }
    for (uint8_t i = 0; i < MAX_BUBBLES; i++) {
        if (g_bubbles[i].state != BS_OFF) {
            n = add_box(out, n, max, g_bubbles[i].x, g_bubbles[i].y, 14, 12, 2);
        }
    }
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        if (g_items[i].kind != IT_OFF && g_items[i].kind != IT_POPUP && g_items[i].kind != IT_BURST) {
            uint8_t rock = (uint8_t)(g_items[i].kind == IT_ROCK);
            n = add_box(out, n, max, g_items[i].x, g_items[i].y, (uint8_t)(rock ? 6 : 14), (uint8_t)(rock ? 4 : 12), 3);
        }
    }
    if (boss_fighting()) {
        n = add_box(out, n, max, (uint16_t)(boss_center_x() - TO_POS(8)), (uint16_t)(boss_center_y() - TO_POS(8)),
                    30, 28, 4);
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

    if (g_state != GS_PLAY || p->state != PS_ALIVE) {
        return;
    }
    uint16_t x = (uint16_t)(p->facing_left ? p->x - TO_POS(24) : p->x + TO_POS(24));
    level_clamp_x(&x);
    uint16_t y = p->y;
    if (what < POWER_COUNT) {
        item_spawn_powerup(what, x, y);
    } else if (what < POWER_COUNT + 3) {
        bubble_spawn_special((uint8_t)(BK_WATER + what - POWER_COUNT), 0, x, (uint16_t)(y - TO_POS(8)));
    } else if (what < DEBUG_SPAWN_COUNT) {
        bubble_spawn_special(BK_LETTER, (uint8_t)(rng_next() % 6), x, (uint16_t)(y - TO_POS(8)));
    }
}

/* --- Views for the debug panels ------------------------------------------------ */

static uint8_t add_object(DebugObject *out, uint8_t n, uint8_t max, const DebugObject *o)
{
    if (n < max) {
        out[n] = *o;
        n++;
    }
    return n;
}

uint8_t game_debug_objects(DebugObject *out, uint8_t max)
{
    DebugObject o;
    uint8_t n = 0;

    for (uint8_t i = 0; i < NUM_PLAYERS; i++) {
        const Player *p = &g_players[i];
        o.kind = DOBJ_PLAYER;
        o.slot = i;
        o.type = p->lives;
        o.state = p->state;
        o.x = p->x;
        o.y = p->y;
        o.vx = p->vx;
        o.vy = p->vy;
        o.timer = p->invulnerable;
        o.flags = (uint8_t)((p->on_ground ? DOBJ_ON_GROUND : 0u) | (p->facing_left ? DOBJ_FACING_LEFT : 0u) |
                            (p->cpu ? DOBJ_CPU : 0u) | (p->stun ? DOBJ_STUNNED : 0u));
        n = add_object(out, n, max, &o);
    }
    for (uint8_t i = 0; i < MAX_ENEMIES; i++) {
        const Enemy *e = &g_enemies[i];
        if (!e->active) {
            continue;
        }
        o.kind = DOBJ_ENEMY;
        o.slot = i;
        o.type = e->type;
        o.state = 0;
        o.x = e->x;
        o.y = e->y;
        o.vx = 0;
        o.vy = e->vy;
        o.timer = e->throw_timer;
        o.flags = (uint8_t)((e->angry ? DOBJ_ANGRY : 0u) | (e->facing_left ? DOBJ_FACING_LEFT : 0u) |
                            (e->stun ? DOBJ_STUNNED : 0u) | (e->entering || e->spawn_delay ? DOBJ_ENTERING : 0u));
        n = add_object(out, n, max, &o);
    }
    for (uint8_t i = 0; i < MAX_BUBBLES; i++) {
        const Bubble *b = &g_bubbles[i];
        if (b->state == BS_OFF) {
            continue;
        }
        o.kind = DOBJ_BUBBLE;
        o.slot = i;
        o.type = b->kind;
        o.state = b->state;
        o.x = b->x;
        o.y = b->y;
        o.vx = b->vx;
        o.vy = b->vy;
        o.timer = b->age;
        o.flags = (uint8_t)((b->angry ? DOBJ_ANGRY : 0u) | (b->trapped ? DOBJ_TRAPPED : 0u));
        n = add_object(out, n, max, &o);
    }
    for (uint8_t i = 0; i < MAX_ITEMS; i++) {
        const Item *it = &g_items[i];
        if (it->kind == IT_OFF) {
            continue;
        }
        o.kind = DOBJ_ITEM;
        o.slot = i;
        o.type = it->kind;
        o.state = 0;
        o.x = it->x;
        o.y = it->y;
        o.vx = it->vx;
        o.vy = it->vy;
        o.timer = it->timer;
        o.flags = 0;
        n = add_object(out, n, max, &o);
    }
    return n;
}

static const char *const state_names[] = {
    "TITLE", "STORY", "OPTIONS", "TRANSITION", "ROUND INTRO", "PLAY", "ROUND SKIP", "PAUSE",
    "CONTINUE", "GAME OVER", "ENDING", "NAME ENTRY", "HISCORES"
};

void game_debug_status(DebugStatus *out)
{
    out->state = (uint8_t)g_state < sizeof state_names / sizeof state_names[0] ? state_names[g_state] : "?";
    out->round = g_round;
    out->round_frames = g_level_frames;
    out->frame = g_frame;
    out->hurry = g_hurry;
    out->freeze = g_freeze;
    out->plus = g_opt.plus;
    for (uint8_t i = 0; i < NUM_PLAYERS; i++) {
        out->lives[i] = g_players[i].lives;
        out->letters[i] = g_players[i].letters;
        for (uint8_t k = 0; k < 8; k++) {
            out->score[i][k] = g_players[i].score[k];
        }
    }
}

static const char *const enemy_names[ENEMY_TYPE_COUNT + 1] = {
    "?", "WALKER", "BOUNCER", "THROWER", "FLUTTER", "HOPPER", "SPITTER", "DROPPER", "SHIELD"
};
static const char *const bubble_kinds[] = { "NORMAL", "WATER", "FIRE", "LIGHTNING", "LETTER" };
static const char *const item_kinds[] = { "OFF", "THROWN", "FOOD", "BURST", "POPUP", "ROCK", "POWERUP", "BONUS" };
static const char *const player_states[] = { "OFF", "ALIVE", "DYING", "ENTERING", "RESCUE" };
static const char *const bubble_states[] = { "OFF", "SHOOT", "FLOAT", "TRAPPED", "POPPING" };

#define PICK(table, i) ((i) < sizeof(table) / sizeof((table)[0]) ? (table)[i] : "?")

const char *game_debug_type_name(uint8_t kind, uint8_t type)
{
    switch (kind) {
    case DOBJ_ENEMY:  return PICK(enemy_names, type);
    case DOBJ_BUBBLE: return PICK(bubble_kinds, type);
    case DOBJ_ITEM:   return PICK(item_kinds, type);
    default:          return "";
    }
}

const char *game_debug_state_name(uint8_t kind, uint8_t state)
{
    switch (kind) {
    case DOBJ_PLAYER: return PICK(player_states, state);
    case DOBJ_BUBBLE: return PICK(bubble_states, state);
    default:          return "";
    }
}

/* --- superdevkit's debug view (F1 on the kit's desktop, ADR-0010) --------------------------- */
/* The view sets these; the next tick does what they ask and clears the buttons. On the SNES the
 * registrations are empty (and the ROM has no BB_DEBUG anyway). */
static int16_t view_spawn;            /* what the spawn button places: 0..DEBUG_SPAWN_COUNT-1 */
static int16_t view_round = 1;        /* where "go to round" goes */
static uint8_t view_spawn_now, view_goto_now, view_clear_now;

void debug_register(uint8_t rounds)
{
    sdevkit_dbg_toggle("invincible", &g_dbg[DBG_INVINCIBLE]);
    sdevkit_dbg_toggle("infinite lives", &g_dbg[DBG_INFINITE_LIVES]);
    sdevkit_dbg_toggle("hit boxes", &g_dbg[DBG_HITBOXES]);
    sdevkit_dbg_toggle("air currents", &g_dbg[DBG_AIR]);
    sdevkit_dbg_toggle("cpu view", &g_dbg[DBG_CPU_VIEW]);
    sdevkit_dbg_toggle("vacuum walls", &g_dbg[DBG_VACUUM_WALLS]);
    sdevkit_dbg_toggle("no sticky bubbles", &g_dbg[DBG_NO_STICKY]);
    sdevkit_dbg_range("spawn what", &view_spawn, 0, DEBUG_SPAWN_COUNT - 1);
    sdevkit_dbg_action("spawn", &view_spawn_now);
    sdevkit_dbg_range("round", &view_round, 1, (int16_t)rounds);
    sdevkit_dbg_action("go to round", &view_goto_now);
    sdevkit_dbg_action("clear round", &view_clear_now);
}

void debug_actions(void)
{
    if (view_spawn_now) {
        view_spawn_now = 0;
        game_debug_spawn((uint8_t)view_spawn);
    }
    if (view_goto_now) {
        view_goto_now = 0;
        (void)game_debug_goto_round((uint8_t)view_round);
    }
    if (view_clear_now) {
        view_clear_now = 0;
        game_debug_clear_round();
    }
}

/* The view's colours (kind 0..7): red, green, blue, yellow, magenta, cyan, orange, white. */
static const uint8_t box_kind[5] = { 1, 0, 2, 3, 4 };   /* player green, enemy red, bubble blue, item yellow, boss magenta */

void debug_boxes(void)
{
    DebugBox boxes[64];
    uint8_t n;

    if (g_dbg[DBG_AIR] && game_debug_room_shown()) {
        /* a box over each run of cells with the same current in a row, its colour the direction
         * (up cyan, down orange, left white, right magenta): the view draws no arrows */
        static const uint8_t air_kind[5] = { 0, 5, 6, 7, 4 };
        for (uint8_t ty = 0; ty < 26; ty++) {
            uint8_t tx = 0;
            while (tx < 32) {
                uint8_t dir = (uint8_t)(level_cell(tx, ty) & CELL_AIR_MASK);
                uint8_t end = (uint8_t)(tx + 1);
                while (end < 32 && (level_cell(end, ty) & CELL_AIR_MASK) == dir) {
                    end++;
                }
                if (dir >= 1 && dir <= 4) {
                    sdevkit_dbg_box((int16_t)(tx * 8 + 1), (int16_t)(16 + ty * 8 + 1), (uint8_t)((end - tx) * 8 - 2), 6,
                                    air_kind[dir]);
                }
                tx = end;
            }
        }
    }
    if (g_dbg[DBG_HITBOXES]) {
        n = game_debug_boxes(boxes, 64);
        for (uint8_t i = 0; i < n; i++) {
            sdevkit_dbg_box(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, box_kind[boxes[i].kind % 5]);
        }
    }
    if (g_dbg[DBG_CPU_VIEW]) {
        const char *action;
        n = game_debug_cpu_view(boxes, 64, &action);
        for (uint8_t i = 0; i < n; i++) {
            sdevkit_dbg_box(boxes[i].x, boxes[i].y, boxes[i].w, boxes[i].h, 0);   /* red: predicted danger */
        }
    }
}

#endif /* BB_DEBUG */
