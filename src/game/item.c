/*
 * Short-lived objects: a defeated enemy flying through the room in an arc
 * and turning into food where it lands; food; bubble bursts; score pop-ups.
 * Ported from ThrownEnemySystem / BonusItemSystem, extended per the design
 * guide (food value from the combo position).
 */

#include "game/internal.h"

#define THROWN_SPEED_X  FX(1, 128)  /* 1.5 px/frame + random up to 0.5 */
#define THROWN_SPEED_Y  FX(3, 0)    /* upward start speed + random up to 0.5 */
#define THROWN_GRAVITY  FX(0, 24)
#define THROWN_MAX_FALL FX(2, 0)
#define THROWN_MIN      40          /* frames in the air before it may land */
#define FOOD_FRAMES     600
#define BURST_FRAMES    16
#define POPUP_FRAMES    60
#define POPUP_SPEED     FX(0, 100)
#define THROWN_TOP      (FIELD_Y + 8)

/* Food values 1000 << tier as (ten-thousands, thousands) digits. */
static const uint8_t food_value[FOOD_TIERS][2] = {
    { 0, 1 }, { 0, 2 }, { 0, 4 }, { 0, 8 }, { 1, 6 }, { 3, 2 }, { 6, 4 }
};

#define BONUS_DELAY    600      /* frames after the round start */
#define BONUS_FRAMES   600

/* Bonus food values as (decimal position, digit) and their pop-ups. */
static const uint8_t bonus_value[BONUS_TYPES][2] = { { 2, 5 }, { 2, 7 }, { 3, 1 }, { 3, 2 } };
static const uint8_t bonus_popup[BONUS_TYPES] = { POPUP_500, POPUP_700, POPUP_FOOD0, POPUP_FOOD0 + 1 };
static uint8_t bonus_placed;

void items_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        g_items[i].kind = IT_OFF;
    }
    bonus_placed = 0;
}

static Item *alloc_item(uint8_t kind, uint16_t x, uint16_t y, uint8_t tier)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        Item *it = &g_items[i];
        if (it->kind == IT_OFF) {
            it->kind = kind;
            it->tier = tier;
            it->x = x;
            it->y = y;
            it->vx = 0;
            it->vy = 0;
            it->timer = 0;
            return it;
        }
    }
    return 0;
}

void item_spawn(uint8_t kind, uint16_t x, uint16_t y, uint8_t tier)
{
    Item *it = alloc_item(kind, x, y, tier);
    if (it) {
        it->timer = (uint16_t)(kind == IT_BURST ? BURST_FRAMES : kind == IT_POPUP ? POPUP_FRAMES : FOOD_FRAMES);
    }
}

void item_spawn_thrown(uint16_t x, uint16_t y, int8_t dir, uint8_t tier)
{
    Item *it = alloc_item(IT_THROWN, x, y, tier);
    if (g_count.defeated < 255) {
        g_count.defeated++;
    }
    if (it) {
        fx8_8 speed = (fx8_8)(THROWN_SPEED_X + (fx8_8)(rng_next() & 127));
        it->vx = (fx8_8)(dir < 0 ? -speed : speed);
        it->vy = (fx8_8)-(THROWN_SPEED_Y + (fx8_8)(rng_next() & 127));
        it->timer = THROWN_MIN;
    }
}

/* The boss's soot balls fly in any direction and pass through platforms. */
void item_spawn_soot(uint16_t x, uint16_t y, fx8_8 vx, fx8_8 vy)
{
    Item *it = alloc_item(IT_ROCK, x, y, SHOT_SOOT);
    if (it) {
        it->vx = vx;
        it->vy = vy;
    }
}

/* Enemy shots: rocks and fireballs fly sideways, the dropper's shot straight down. */
void item_spawn_shot(uint8_t shot, uint16_t x, uint16_t y, int8_t dir)
{
    Item *it = alloc_item(IT_ROCK, x, y, shot);
    if (it) {
        fx8_8 speed = shot == SHOT_SPIT ? SPIT_SPEED : ROCK_SPEED;
        it->vx = shot == SHOT_DOWN ? 0 : (fx8_8)(dir < 0 ? -speed : speed);
        it->vy = shot == SHOT_DOWN ? DOWN_SHOT_SPEED : 0;
    }
}

#define POWERUP_FRAMES 600

void item_spawn_powerup(uint8_t power, uint16_t x, uint16_t y)
{
    Item *it = alloc_item(IT_POWERUP, x, y, power);
    if (it) {
        it->timer = POWERUP_FRAMES;
    }
}

#define WHISTLE_FRAMES 480

uint8_t powerup_tile(uint8_t power)
{
    return (uint8_t)(power < POWER_STAR ? SPR_POWERUP0 + power : SPR_ITEM_STAR + power - POWER_STAR);
}

/* See the POWER_* list in internal.h. */
static void collect_powerup(Item *it, Player *p)
{
    uint8_t popup = POPUP_100;
    uint8_t n;

    switch (it->tier) {
    case POWER_UMBRELLA:
        round_skip(3, SKIP_UMBRELLA, p->id);
        score_add(p->score, 2, 2);   /* +200 */
        popup = POPUP_200;
        break;
    case POWER_STAR:
    case POWER_WAVE:
        if (it->tier == POWER_WAVE) {
            uint8_t k;
            for (k = 0; k < 3; k++) {
                effect_start(BK_WATER, TO_POS(40 + k * 80), TO_POS(FIELD_Y + 8), (int8_t)(k & 1 ? -1 : 1), p->id);
            }
        }
        /* every enemy at once: a multikill for whoever picked it up */
        n = enemies_to_gems();
        n = (uint8_t)(n + bubbles_trapped_to_gems());
        multikill_award(p->id, n);
        score_add(p->score, 3, 1);   /* +1000 */
        popup = POPUP_FOOD0;
        break;
    case POWER_WHISTLE:
        g_freeze = WHISTLE_FRAMES;
        score_add(p->score, 3, 1);
        popup = POPUP_FOOD0;
        break;
    default:
        if (it->tier <= POWER_RATE && g_count.candies < 255) {
            g_count.candies++;
        }
        p->powers = (uint16_t)(p->powers | POWER_BIT(it->tier));
        score_add(p->score, 2, 1);   /* +100 */
        break;
    }
    sfx(SFX_EXTRA);
    it->kind = IT_POPUP;
    it->tier = popup;
    it->timer = POPUP_FRAMES;
}

/* One bonus food per round, on the last enemy's spawn point (a platform). */
void bonus_round_tick(void)
{
    const LevelDef *def = &level_defs[g_level];
    const LevelEnemy *slot;
    Item *it;

    if (bonus_placed || g_level_frames != BONUS_DELAY || def->enemy_count == 0) {
        return;
    }
    bonus_placed = 1;
    slot = &def->enemies[def->enemy_count - 1];
    if (slot->type == ENEMY_TYPE_BOUNCER) {
        slot = &def->enemies[0];   /* bouncers may start in mid-air */
    }
    it = alloc_item(IT_BONUS, TO_POS(slot->x << 3), TO_POS(FIELD_Y + (slot->y << 3) - 8),
                    (uint8_t)(g_level & (BONUS_TYPES - 1)));
    if (it) {
        it->timer = BONUS_FRAMES;
    }
}

void items_clear_rocks(void)
{
    uint8_t i;
    for (i = 0; i < MAX_ITEMS; i++) {
        if (g_items[i].kind == IT_ROCK) {
            g_items[i].kind = IT_OFF;
        }
    }
}

/* A sideways shot flies until it hits a wall or a platform; a downward
 * shot passes through platforms and ends at the floor. */
static void update_rock(Item *it)
{
    uint8_t cx;
    if (it->tier == SHOT_SOOT) {
        it->x = (uint16_t)(it->x + (uint16_t)it->vx);
        it->y = (uint16_t)(it->y + (uint16_t)it->vy);
        cx = (uint8_t)(PX(it->x) + 8);
        if (cx < 16 || cx > SCREEN_W - 16 || PX(it->y) >= FIELD_BOTTOM - 16 || PX(it->y) < FIELD_Y) {
            it->kind = IT_OFF;
        }
        return;
    }
    if (it->tier == SHOT_DOWN) {
        it->y = (uint16_t)(it->y + (uint16_t)it->vy);
        if (PX(it->y) >= FIELD_BOTTOM - 8 - 16 || PX(it->y) >= WRAP_Y) {
            it->kind = IT_OFF;
        }
        return;
    }
    it->x = (uint16_t)(it->x + (uint16_t)it->vx);
    cx = (uint8_t)(PX(it->x) + 8);
    if (cx < BODY_MIN_CX || cx > BODY_MAX_CX || PX(it->x) >= 0xF0u ||
        level_solid_px((int16_t)cx, (int16_t)(PX(it->y) + 8))) {
        it->kind = IT_BURST;
        it->timer = 8;
    }
}

static void update_thrown(Item *it)
{
    uint8_t cx;

    /* Horizontal: bounce off the side walls. */
    it->x = (uint16_t)(it->x + (uint16_t)it->vx);
    cx = (uint8_t)(PX(it->x) + 8);
    if (cx < BODY_MIN_CX || cx > BODY_MAX_CX || PX(it->x) >= 0xF0u) {
        it->vx = (fx8_8)-it->vx;
        it->x = TO_POS(it->vx > 0 ? BODY_MIN_CX - 8 : BODY_MAX_CX - 8);
    }

    /* Vertical: an arc; bounce off the ceiling, land after a minimum time. */
    it->vy = (fx8_8)(it->vy + THROWN_GRAVITY);
    if (it->vy > THROWN_MAX_FALL) {
        it->vy = THROWN_MAX_FALL;
    }
    if (it->vy < 0) {
        it->y = (uint16_t)(it->y + (uint16_t)it->vy);
        if (PX(it->y) < THROWN_TOP || PX(it->y) >= WRAP_Y) {
            it->y = TO_POS(THROWN_TOP);
            it->vy = 0;
        }
    } else if (it->timer == 0) {
        if (level_fall(&it->x, &it->y, it->vy)) {
            it->kind = IT_FOOD;
            it->timer = FOOD_FRAMES;
            return;
        }
    } else {
        it->y = (uint16_t)(it->y + (uint16_t)it->vy);
        level_wrap_y(&it->y);
    }
    if (it->timer) {
        it->timer--;
    }
}

static void collect_food(Item *it, Player *p)
{
    if (g_count.food < 255) {
        g_count.food++;
    }
    score_add(p->score, 4, food_value[it->tier][0]);
    score_add(p->score, 3, food_value[it->tier][1]);
    sfx(SFX_GEM);
    it->kind = IT_POPUP;
    it->tier = (uint8_t)(POPUP_FOOD0 + it->tier);
    it->timer = POPUP_FRAMES;
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

        case IT_ROCK:
            update_rock(it);
            break;

        case IT_BONUS: {
            uint8_t k;
            for (k = 0; k < NUM_PLAYERS && it->kind == IT_BONUS; k++) {
                Player *p = &g_players[k];
                if (p->state == PS_ALIVE && overlap(p->x, p->y, it->x, it->y, 12, 12)) {
                    score_add(p->score, bonus_value[it->tier][0], bonus_value[it->tier][1]);
                    sfx(SFX_GEM);
                    it->kind = IT_POPUP;
                    it->tier = bonus_popup[it->tier];
                    it->timer = POPUP_FRAMES;
                }
            }
            if (it->kind == IT_BONUS && --it->timer == 0) {
                it->kind = IT_OFF;
            }
            break;
        }

        case IT_POWERUP: {
            uint8_t k;
            for (k = 0; k < NUM_PLAYERS && it->kind == IT_POWERUP; k++) {
                Player *p = &g_players[k];
                if (p->state == PS_ALIVE && overlap(p->x, p->y, it->x, it->y, 12, 12)) {
                    collect_powerup(it, p);
                }
            }
            if (it->kind == IT_POWERUP && --it->timer == 0) {
                it->kind = IT_OFF;
            }
            break;
        }

        case IT_FOOD: {
            uint8_t k;
            for (k = 0; k < NUM_PLAYERS && it->kind == IT_FOOD; k++) {
                Player *p = &g_players[k];
                if (p->state == PS_ALIVE && overlap(p->x, p->y, it->x, it->y, 12, 12)) {
                    collect_food(it, p);
                }
            }
            if (it->kind == IT_FOOD && --it->timer == 0) {
                it->kind = IT_OFF;
            }
            break;
        }

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
        switch (it->kind) {
        case IT_THROWN:
            oam_add(PX(it->x), PX(it->y), (uint8_t)(SPR_E_THROWN0 + ((g_frame >> 2) & 3)), 0);
            break;
        case IT_FOOD:
            if (it->timer >= 120 || !(it->timer & 4)) {   /* blink before it vanishes */
                oam_add(PX(it->x), PX(it->y), (uint8_t)(SPR_FOOD0 + it->tier), 0);
            }
            break;
        case IT_BURST:
            oam_add(PX(it->x), PX(it->y), (uint8_t)(it->timer > BURST_FRAMES / 2 ? SPR_BURST0 : SPR_BURST1), 0);
            break;
        case IT_ROCK:
            if (it->tier == SHOT_SPIT) {
                oam_add(PX(it->x), PX(it->y), SPR_SPIT, (uint8_t)(it->vx < 0 ? SPR_ATTR_FLIP_X : 0u));
            } else if (it->tier == SHOT_DOWN) {
                oam_add(PX(it->x), PX(it->y), SPR_DOWN_SHOT, 0);
            } else if (it->tier == SHOT_SOOT) {
                oam_add(PX(it->x), PX(it->y), SPR_SOOT, 0);
            } else {
                oam_add(PX(it->x), PX(it->y), (uint8_t)(SPR_ROCK0 + ((g_frame >> 2) & 1)), 0);
            }
            break;
        case IT_BONUS:
            if (it->timer >= 120 || !(it->timer & 4)) {
                oam_add(PX(it->x), PX(it->y), (uint8_t)(SPR_BONUS0 + it->tier), 0);
            }
            break;
        case IT_POWERUP:
            if (it->timer >= 120 || !(it->timer & 4)) {
                oam_add(PX(it->x), PX(it->y), powerup_tile(it->tier), 0);
            }
            break;
        case IT_POPUP: {
            /* 32 px wide, centred over the 16 px object */
            uint8_t x = (uint8_t)(PX(it->x) >= 8 ? PX(it->x) - 8 : 0);
            uint8_t tile = (uint8_t)(SPR_POPUP0 + (it->tier << 1));
            if (x > SCREEN_W - 32) {
                x = SCREEN_W - 32;
            }
            oam_add(x, PX(it->y), tile, 0);
            oam_add((uint8_t)(x + 16), PX(it->y), (uint8_t)(tile + 1), 0);
            break;
        }
        default:
            break;
        }
    }
}
