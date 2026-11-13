/*
 * Special bubbles and what they release, EXTEND letters, and the hidden
 * counters that unlock power-ups (design guide: Special bubbles, Items).
 *
 *  - Water: a stream runs down and along the platforms and sweeps away
 *    every enemy it touches; all of them count as one combo.
 *  - Fire: a fireball drops onto the platform below and leaves a carpet
 *    of fire that defeats enemies and stuns players.
 *  - Lightning: a bolt flies straight across the room.
 *  - EXTEND: chains of three or more enemies earn letter bubbles; whoever
 *    pops a letter collects it; all six give an extra life and end the round.
 *  - Power-ups: when a counter reaches its limit, the power-up waits for the
 *    next round and appears on a platform a few seconds after the start.
 */

#include "game/internal.h"

#define MAX_EFFECTS      4
#define WATER_SPEED      FX(2, 0)
#define WATER_SEGMENTS   10        /* trail length */
#define WATER_SAMPLE     3         /* trail point every 4 frames (mask) */
#define WATER_FRAMES     420       /* gives up after 7 s (rooms without a bottom gap) */
#define FIRE_FALL        FX(2, 0)
#define FIRE_FRAMES      180       /* how long the carpet burns */
#define FIRE_REACH       4         /* tiles to each side of the landing point */
#define STUN_FRAMES      60
#define BOLT_SPEED       FX(4, 0)
#define LETTER_INTERVAL  240       /* frames between two letter bubbles */
#define POWERUP_DELAY    300       /* frames after the round start */

/* Counter limits (design guide values, the umbrella lowered for 10 rounds). */
#define LIMIT_BLOWN      35
#define LIMIT_POPPED     35
#define LIMIT_JUMPS      35
#define LIMIT_WALKED     3360      /* 15 times across the room */
#define LIMIT_WATER      5

enum { EF_OFF, EF_WATER, EF_FIREBALL, EF_FIRE, EF_BOLT };

typedef struct {
    uint8_t  kind;
    uint8_t  owner;
    uint8_t  combo;          /* enemies defeated by this effect so far */
    int8_t   dir;
    uint16_t timer;
    uint16_t x, y;           /* head / fireball / bolt / carpet row */
    uint8_t  left, right;    /* fire carpet: pixel range */
    uint8_t  trail_x[WATER_SEGMENTS];
    uint8_t  trail_y[WATER_SEGMENTS];
    uint8_t  trail_len;
} Effect;

static Effect   effects[MAX_EFFECTS];
static uint16_t special_timer;
static uint16_t letter_timer;
static uint8_t  letters_queued;
static uint8_t  powerup_placed;   /* at most one power-up per round */

void effects_clear(void)
{
    uint8_t i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        effects[i].kind = EF_OFF;
    }
    special_timer = 0;
    letter_timer = 0;
    powerup_placed = 0;
}

void effect_start(uint8_t kind, uint16_t x, uint16_t y, int8_t dir, uint8_t player)
{
    uint8_t i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        Effect *ef = &effects[i];
        if (ef->kind != EF_OFF) {
            continue;
        }
        ef->kind = (uint8_t)(kind == BK_WATER ? EF_WATER : kind == BK_FIRE ? EF_FIREBALL : EF_BOLT);
        ef->owner = player;
        ef->combo = 0;
        ef->dir = dir;
        ef->timer = 0;
        ef->x = x;
        ef->y = y;
        ef->trail_len = 0;
        return;
    }
}

/* An enemy hit by an effect flies off and becomes food (one combo per effect). */
static void sweep_enemies(Effect *ef, uint16_t x, uint16_t y, uint8_t w, uint8_t h)
{
    uint8_t i;
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        if (e->active && !e->entering && overlap(x, y, e->x, e->y, w, h)) {
            uint8_t tier = (uint8_t)(ef->combo < FOOD_TIERS - 1 ? ef->combo : FOOD_TIERS - 1);
            e->active = 0;
            item_spawn_thrown(e->x, e->y, ef->dir, tier);
            if (ef->combo < 255) {
                ef->combo++;
            }
            sfx(SFX_KILL);
        }
    }
}

static void update_water(Effect *ef)
{
    uint8_t i;

    if (++ef->timer >= WATER_FRAMES) {
        ef->kind = EF_OFF;
        return;
    }
    if (level_on_ground(ef->x, ef->y)) {
        if (level_wall_ahead(ef->x, ef->y, ef->dir)) {
            ef->dir = (int8_t)-ef->dir;
        }
        ef->x = (uint16_t)(ef->dir < 0 ? ef->x - (uint16_t)WATER_SPEED : ef->x + (uint16_t)WATER_SPEED);
        {
            uint16_t before = ef->x;
            level_clamp_x(&ef->x);
            if (ef->x != before) {
                ef->dir = (int8_t)-ef->dir;
            }
        }
    } else if (PX(ef->y) >= FIELD_BOTTOM - 16 - 2) {
        ef->kind = EF_OFF;   /* poured out through the bottom gap */
        return;
    } else {
        (void)level_fall(&ef->x, &ef->y, WATER_SPEED);
    }

    if ((ef->timer & WATER_SAMPLE) == 0) {
        for (i = (uint8_t)(WATER_SEGMENTS - 1); i > 0; i--) {
            ef->trail_x[i] = ef->trail_x[i - 1];
            ef->trail_y[i] = ef->trail_y[i - 1];
        }
        ef->trail_x[0] = PX(ef->x);
        ef->trail_y[0] = PX(ef->y);
        if (ef->trail_len < WATER_SEGMENTS) {
            ef->trail_len++;
        }
    }

    sweep_enemies(ef, ef->x, ef->y, 14, 14);
    for (i = 0; i < ef->trail_len; i++) {
        sweep_enemies(ef, TO_POS(ef->trail_x[i]), TO_POS(ef->trail_y[i]), 14, 14);
    }
}

/* Fire spreads along the ground the fireball landed on. */
static void light_carpet(Effect *ef)
{
    uint8_t cx = (uint8_t)(PX(ef->x) + 8);
    uint8_t left = (uint8_t)(cx & 0xF8u), right = (uint8_t)(cx & 0xF8u);
    uint8_t k;
    uint16_t probe;

    for (k = 0; k < FIRE_REACH; k++) {
        probe = TO_POS(left - 8 - 4);
        if (left < 24 || !level_on_ground(probe, ef->y)) {
            break;
        }
        left = (uint8_t)(left - 8);
    }
    for (k = 0; k < FIRE_REACH; k++) {
        probe = TO_POS(right + 8 - 4);
        if (right > SCREEN_W - 32 || !level_on_ground(probe, ef->y)) {
            break;
        }
        right = (uint8_t)(right + 8);
    }
    ef->kind = EF_FIRE;
    ef->left = left;
    ef->right = (uint8_t)(right + 8);
    ef->timer = FIRE_FRAMES;
}

static void update_fire(Effect *ef)
{
    uint8_t i;
    if (--ef->timer == 0) {
        ef->kind = EF_OFF;
        return;
    }
    for (i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &g_enemies[i];
        uint8_t cx = (uint8_t)(PX(e->x) + 8);
        if (e->active && !e->entering && cx >= ef->left && cx < ef->right && overlap(e->x, e->y, e->x, ef->y, 1, 10)) {
            sweep_enemies(ef, e->x, e->y, 1, 1);
        }
    }
    for (i = 0; i < NUM_PLAYERS; i++) {
        Player *p = &g_players[i];
        uint8_t cx = (uint8_t)(PX(p->x) + 8);
        if (p->state == PS_ALIVE && !p->stun && !p->invulnerable && cx >= ef->left && cx < ef->right &&
            overlap(p->x, p->y, p->x, ef->y, 1, 10)) {
            p->stun = STUN_FRAMES;
        }
    }
}

static void update_bolt(Effect *ef)
{
    uint8_t cx;
    ef->x = (uint16_t)(ef->dir < 0 ? ef->x - (uint16_t)BOLT_SPEED : ef->x + (uint16_t)BOLT_SPEED);
    cx = (uint8_t)(PX(ef->x) + 8);
    if (cx < BODY_MIN_CX || cx > BODY_MAX_CX || PX(ef->x) >= 0xF0u) {
        ef->kind = EF_OFF;
        return;
    }
    sweep_enemies(ef, ef->x, ef->y, 14, 12);
}

/* Letter bubbles and special bubbles come in through the room's opening. */
static void emit_bubbles(void)
{
    const LevelDef *def = &level_defs[g_level];
    uint16_t x, y;

    if (def->special != SPECIAL_NONE && ++special_timer >= def->special_frames) {
        special_timer = 0;
        level_opening(&x, &y);
        bubble_spawn_special((uint8_t)def->special, 0, x, y);   /* SPECIAL_* == BK_* */
    }
    if (letters_queued && ++letter_timer >= LETTER_INTERVAL) {
        uint8_t want = 0, letter, tries;
        uint8_t k;
        letter_timer = 0;
        letters_queued--;
        /* Prefer a letter some player in the game is still missing. */
        for (k = 0; k < NUM_PLAYERS; k++) {
            if (g_players[k].state != PS_OFF) {
                want = (uint8_t)(want | (EXTEND_ALL & (uint8_t)~g_players[k].letters));
            }
        }
        letter = (uint8_t)(rng_next() & 7);
        for (tries = 0; tries < 8; tries++) {
            letter = (uint8_t)(letter >= 5 ? 0 : letter + 1);
            if (want & (1u << letter)) {
                break;
            }
        }
        level_opening(&x, &y);
        bubble_spawn_special(BK_LETTER, letter, x, y);
    }
}

void effects_update(void)
{
    uint8_t i;
    for (i = 0; i < MAX_EFFECTS; i++) {
        Effect *ef = &effects[i];
        switch (ef->kind) {
        case EF_WATER:
            update_water(ef);
            break;
        case EF_FIREBALL:
            if (level_fall(&ef->x, &ef->y, FIRE_FALL) || PX(ef->y) >= FIELD_BOTTOM - 16 - 2) {
                light_carpet(ef);
            }
            break;
        case EF_FIRE:
            update_fire(ef);
            break;
        case EF_BOLT:
            update_bolt(ef);
            break;
        default:
            break;
        }
    }
    emit_bubbles();
}

void effects_draw(void)
{
    uint8_t i, k;
    for (i = 0; i < MAX_EFFECTS; i++) {
        const Effect *ef = &effects[i];
        uint8_t anim = (uint8_t)((g_frame >> 3) & 1);
        switch (ef->kind) {
        case EF_WATER:
            oam_add(PX(ef->x), PX(ef->y), (uint8_t)(SPR_WATER0 + anim), 0);
            for (k = 0; k < ef->trail_len; k++) {
                oam_add(ef->trail_x[k], ef->trail_y[k], (uint8_t)(SPR_WATER0 + ((anim + k) & 1)), 0);
            }
            break;
        case EF_FIREBALL:
            oam_add(PX(ef->x), PX(ef->y), SPR_FIREBALL, 0);
            break;
        case EF_FIRE:
            for (k = ef->left; k < ef->right; k = (uint8_t)(k + 16)) {
                uint8_t x = (uint8_t)(k + 16 > ef->right ? ef->right - 16 : k);
                oam_add(x, PX(ef->y), (uint8_t)(SPR_FIRE0 + ((anim + (k >> 4)) & 1)), 0);
            }
            break;
        case EF_BOLT:
            oam_add(PX(ef->x), PX(ef->y), (uint8_t)(SPR_BOLT0 + anim), (uint8_t)(ef->dir < 0 ? SPR_ATTR_FLIP_X : 0u));
            break;
        default:
            break;
        }
    }
}

/* --- EXTEND ----------------------------------------------------------------- */
void extend_letters_earned(uint8_t count)
{
    letters_queued = (uint8_t)(letters_queued + count > 6 ? 6 : letters_queued + count);
}

void extend_collect(uint8_t player, uint8_t letter)
{
    Player *p = &g_players[player];
    p->letters = (uint8_t)(p->letters | (1u << letter));
    sfx(SFX_GEM);
    if (p->letters == EXTEND_ALL) {
        p->letters = 0;
        if (p->lives < 9) {
            p->lives++;
        }
        sfx(SFX_EXTRA);
        g_skip_rounds = 1;   /* the round ends at once */
    }
}

/* --- Power-ups -------------------------------------------------------------- */
static void unlock(uint8_t power)
{
    g_count.queued = (uint8_t)(g_count.queued | (1u << power));
}

void counters_check(void)
{
    if (g_count.blown >= LIMIT_BLOWN)   { g_count.blown = 0;  unlock(POWER_RANGE); }
    if (g_count.popped >= LIMIT_POPPED) { g_count.popped = 0; unlock(POWER_SPEED); }
    if (g_count.jumps >= LIMIT_JUMPS)   { g_count.jumps = 0;  unlock(POWER_RATE); }
    if (g_count.walked >= LIMIT_WALKED) { g_count.walked = 0; unlock(POWER_SHOES); }
    if (g_count.water >= LIMIT_WATER)   { g_count.water = 0;  unlock(POWER_UMBRELLA); }
}

/* Call once per frame of play: places at most one waiting power-up per
 * round, on the first enemy spawn point (always on a platform). */
void powerup_round_tick(void)
{
    const LevelDef *def = &level_defs[g_level];
    uint8_t power;

    if (powerup_placed || g_level_frames != POWERUP_DELAY || !g_count.queued || def->enemy_count == 0) {
        return;
    }
    for (power = 0; power < POWER_COUNT; power++) {
        if (g_count.queued & (1u << power)) {
            g_count.queued = (uint8_t)(g_count.queued & (0xFFu ^ (1u << power)));
            powerup_placed = 1;
            item_spawn_powerup(power, TO_POS(def->enemies[0].x << 3),
                               TO_POS(FIELD_Y + (def->enemies[0].y << 3) - 8));
            return;
        }
    }
}
