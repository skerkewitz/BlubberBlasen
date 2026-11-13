/*
 * Headless tests for the game library (no raylib, no window).
 *  - score arithmetic
 *  - level data sanity
 *  - determinism: same seed + same input = same output, frame by frame
 *  - invariants of the output (sprites on screen, valid tile numbers)
 *  - a random "bot" plays for a while and must trap, kill, die and reach
 *    the next round at least once each
 */

#include <stdio.h>
#include <stdlib.h>

#include "game/internal.h"

static int failures;
static unsigned long trace;   /* BB_TRACE=<n>: print state every n frames */

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static void test_score(void)
{
    uint8_t a[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
    uint8_t b[8] = { 0, 0, 0, 0, 1, 0, 0, 0 };
    int i;

    for (i = 0; i < 100; i++) {
        score_add(a, 1, 1);           /* 100 x 10 = 1000 */
    }
    CHECK(score_cmp(a, b) == 0, "100 x 10 points should be 1000");
    score_add(a, 2, 5);
    CHECK(a[5] == 5 && a[4] == 1, "1000 + 500 should be 1500");
    CHECK(score_cmp(a, b) > 0 && score_cmp(b, a) < 0, "compare");

    for (i = 0; i < 8; i++) {
        a[i] = 9;
    }
    score_add(a, 0, 1);
    CHECK(a[0] == 9 && a[7] == 9, "score saturates at 99999999");
}

static void test_levels(void)
{
    uint8_t l;
    uint16_t i;

    CHECK(level_count == 20, "expected 20 levels, got %u", level_count);
    CHECK(level_defs[0].hurry_frames == 40 * 60, "round01.txt sets hurry 40");
    CHECK(level_defs[0].enemies[2].delay == 30, "round01.txt sets delay 30 for enemy 3");
    CHECK(level_defs[1].special == SPECIAL_WATER && level_defs[1].special_frames == 15 * 60,
          "round02.txt sets special water 15");
    for (l = 0; l < level_count; l++) {
        const LevelDef *d = &level_defs[l];
        CHECK(d->enemy_count > 0 && d->enemy_count <= LEVEL_MAX_ENEMIES, "level %u enemy count %u", l + 1, d->enemy_count);
        for (i = 0; i < LEVEL_CELLS; i++) {
            CHECK((d->cells[i] & CELL_AIR_MASK) <= AIR_RIGHT, "level %u bad airflow", l + 1);
        }
        for (i = 0; i < LEVEL_H; i++) {
            CHECK(d->cells[i * LEVEL_W] & CELL_SOLID, "level %u: left wall open at row %u", l + 1, i);
        }
        CHECK(d->tileset < LEVEL_TILESETS, "level %u: tileset %u out of range", l + 1, d->tileset);
        CHECK(d->hurry_frames == 0 || d->hurry_frames >= 600, "level %u: hurry time below 10 s", l + 1);
        /* the player spawns bottom-left and must stand on something */
        CHECK(d->cells[(LEVEL_H - 1) * LEVEL_W + 4] & CELL_SOLID, "level %u: no floor at the spawn point", l + 1);
    }
}

/* Simple deterministic input generator, independent from the game RNG. */
static uint16_t bot_rng = 0x1234;
static uint16_t bot_next(void)
{
    bot_rng = (uint16_t)(bot_rng * 25173u + 13849u);
    return bot_rng;
}

static uint16_t bot_pad(uint32_t frame, uint16_t *dir)
{
    uint16_t pad = 0;
    uint16_t r = bot_next();

    if ((r & 63) == 0) {
        *dir = (uint16_t)((r & 64) ? PAD_LEFT : PAD_RIGHT);
    }
    pad |= *dir;
    if ((frame & 15) < 2) {
        pad |= PAD_Y;                 /* shoot often */
    }
    if (((r >> 8) & 31) == 0) {
        pad |= PAD_B;                 /* jump now and then */
    }
    if ((frame % 97) < 2 && g_state != GS_PLAY && g_state != GS_CONTINUE) {
        pad |= PAD_START;             /* skip title / confirm names */
    }
    return pad;
}

typedef struct {
    uint32_t hash;
    uint32_t sfx_seen;
    uint16_t max_round;
    uint32_t games;
} RunResult;

static uint32_t fnv(uint32_t h, uint32_t v)
{
    return (h ^ v) * 16777619u;
}

static RunResult run(uint16_t seed, uint32_t frames, int check_invariants)
{
    RunResult res = { 2166136261u, 0, 0, 0 };
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    uint16_t dir = PAD_RIGHT;
    uint32_t f;
    GameState last = GS_TITLE;
    int i, l;

    bot_rng = 0x1234;
    for (i = 0; i < SRAM_SIZE; i++) {
        game_sram()[i] = 0;
    }
    game_init(seed);

    for (f = 0; f < frames; f++) {
        const Sprite *oam;
        pad[0] = bot_pad(f, &dir);
        game_tick(pad);

        res.sfx_seen |= game_sfx();
        if (g_round > res.max_round) {
            res.max_round = g_round;
        }
        if (g_state == GS_ROUND_INTRO && last == GS_TITLE) {
            res.games++;
        }
        if (g_state == GS_ROUND_INTRO && last == GS_HISCORES) {
            res.games++;
        }
        last = g_state;

        oam = game_oam();
        for (i = 0; i < MAX_SPRITES; i++) {
            res.hash = fnv(res.hash, (uint32_t)oam[i].x | ((uint32_t)oam[i].y << 8) |
                                     ((uint32_t)oam[i].tile << 16) | ((uint32_t)oam[i].attr << 24));
            if (check_invariants && oam[i].y < SCREEN_H) {
                CHECK(oam[i].tile < 160, "frame %lu: sprite %d uses unknown cell %u", (unsigned long)f, i, oam[i].tile);
            }
        }
        for (l = 0; l < BG_LAYERS; l++) {
            const uint16_t *bg = game_bg((uint8_t)l);
            for (i = 0; i < BG_W * (l == 0 ? BG0_ROWS : BG_H); i += 7) {
                res.hash = fnv(res.hash, bg[i]);
            }
        }
        res.hash = fnv(res.hash, game_sfx());

        if (check_invariants) {
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (g_enemies[i].active && !g_enemies[i].entering) {
                    uint8_t cx = (uint8_t)(PX(g_enemies[i].x) + 8);
                    CHECK(cx >= BODY_MIN_CX && cx <= BODY_MAX_CX, "frame %lu: enemy %d outside walls (cx=%u)",
                          (unsigned long)f, i, cx);
                }
            }
            if (g_players[0].state == PS_ALIVE) {
                uint8_t cx = (uint8_t)(PX(g_players[0].x) + 8);
                CHECK(cx >= BODY_MIN_CX && cx <= BODY_MAX_CX, "frame %lu: player outside walls", (unsigned long)f);
            }
        }
        if (trace && (f % trace) == 0) {
            printf("f=%lu st=%d rd=%u lvl=%u P(st=%u x=%u y=%u vy=%d g=%u) E:", (unsigned long)f, (int)g_state, g_round,
                   g_level, g_players[0].state, PX(g_players[0].x), PX(g_players[0].y), g_players[0].vy, g_players[0].on_ground);
            for (i = 0; i < MAX_ENEMIES; i++) {
                if (g_enemies[i].active) {
                    printf(" (%u,%u%s%s)", PX(g_enemies[i].x), PX(g_enemies[i].y), g_enemies[i].entering ? " in" : "",
                           g_enemies[i].angry ? " A" : "");
                }
            }
            printf(" B:");
            for (i = 0; i < MAX_BUBBLES; i++) {
                if (g_bubbles[i].state == BS_TRAPPED) {
                    printf(" T(%u,%u)", PX(g_bubbles[i].x), PX(g_bubbles[i].y));
                }
            }
            printf("\n");
        }
        if (failures > 20) {
            break;
        }
    }
    return res;
}

static void test_determinism_and_play(void)
{
    const uint32_t frames = 60u * 60u * 30u;   /* 30 minutes of play */
    RunResult a = run(4711, frames, 1);
    RunResult b = run(4711, frames, 0);
    RunResult c = run(4712, frames, 0);

    CHECK(a.hash == b.hash, "same seed and input gave different output");
    CHECK(a.hash != c.hash, "different seeds gave identical output");

    CHECK(a.sfx_seen & (1u << SFX_SHOOT), "bot never shot");
    CHECK(a.sfx_seen & (1u << SFX_TRAP), "no enemy was ever trapped");
    CHECK(a.sfx_seen & (1u << SFX_KILL), "no trapped enemy was ever popped");
    CHECK(a.sfx_seen & (1u << SFX_GEM), "no food was ever collected");
    CHECK(a.sfx_seen & (1u << SFX_DEATH), "player never died");
    CHECK(a.sfx_seen & (1u << SFX_GAMEOVER), "never reached game over");
    CHECK(a.max_round >= 2, "never cleared a round (max round %u)", a.max_round);

    printf("bot: %lu games, best round %u, sfx mask 0x%04lX\n",
           (unsigned long)a.games, a.max_round, (unsigned long)a.sfx_seen);
}

static Bubble make_trapped(uint8_t x, uint8_t y)
{
    Bubble b;
    b.state = BS_TRAPPED;
    b.trapped = 1;
    b.angry = 0;
    b.anim = 0;
    b.pop_timer = 0;
    b.dir = AIR_NONE;
    b.kind = BK_NORMAL;
    b.letter = 0;
    b.etype = ENEMY_TYPE_WALKER;
    b.shot_frames = 8;
    b.shot_speed = FX(4, 0);
    b.age = 1;
    b.x = TO_POS(x);
    b.y = TO_POS(y);
    return b;
}

/* Four touching trapped enemies pop in one chain with doubling food tiers;
 * a bubble that touches none of them stays. */
static void test_chain(void)
{
    uint8_t i, thrown = 0, tiers = 0;
    int f;

    game_init(1);
    level_load(0, 0);
    bubbles_clear();
    items_clear();
    for (i = 0; i < 4; i++) {
        g_bubbles[i] = make_trapped((uint8_t)(60 + i * 13), 100);
    }
    g_bubbles[5] = make_trapped(200, 40);

    bubble_pop(&g_bubbles[0], 1, 0);
    for (f = 0; f < 40; f++) {
        bubbles_update();
    }
    for (i = 0; i < MAX_ITEMS; i++) {
        if (g_items[i].kind == IT_THROWN) {
            thrown++;
            tiers = (uint8_t)(tiers | (1u << g_items[i].tier));
        }
    }
    CHECK(thrown == 4, "chain should defeat 4 enemies, got %u", thrown);
    CHECK(tiers == 0x0F, "chain tiers should be 0,1,2,3 (mask 0x%02X)", tiers);
    CHECK(g_bubbles[5].state == BS_TRAPPED, "a distant bubble must not pop");
}

/* Two bubbles on the same spot drift apart. */
static void test_push(void)
{
    int f;
    int16_t dx, dy;

    game_init(1);
    level_load(0, 0);
    bubbles_clear();
    g_bubbles[0] = make_trapped(100, 100);
    g_bubbles[1] = make_trapped(100, 100);
    g_bubbles[0].state = BS_FLOAT;
    g_bubbles[1].state = BS_FLOAT;
    for (f = 0; f < 30; f++) {
        bubbles_update();
    }
    dx = (int16_t)(PX(g_bubbles[0].x) - PX(g_bubbles[1].x));
    dy = (int16_t)(PX(g_bubbles[0].y) - PX(g_bubbles[1].y));
    if (dx < 0) dx = (int16_t)-dx;
    if (dy < 0) dy = (int16_t)-dy;
    CHECK(dx >= 11 || dy >= 11, "bubbles still overlap (dx=%d dy=%d)", dx, dy);
}

/* Food of tier 4 (5th enemy in a chain) is worth 16,000. */
static void test_food_value(void)
{
    static const uint8_t expect[8] = { 0, 0, 0, 1, 6, 0, 0, 0 };
    uint8_t i;

    game_init(1);
    level_load(0, 0);
    items_clear();
    for (i = 0; i < 8; i++) {
        g_players[0].score[i] = 0;
    }
    g_players[0].state = PS_ALIVE;
    g_players[0].x = TO_POS(100);
    g_players[0].y = TO_POS(100);
    item_spawn(IT_FOOD, g_players[0].x, g_players[0].y, 4);
    items_update();
    CHECK(score_cmp(g_players[0].score, expect) == 0, "tier 4 food should give 16000");
}

static void setup_room(uint8_t level)
{
    game_init(1);
    g_level = level;
    level_load(level, 0);
    enemies_clear();
    bubbles_clear();
    items_clear();
    g_state = GS_PLAY;
    g_players[0].state = PS_ALIVE;
    g_players[0].invulnerable = 0;
    g_players[0].x = TO_POS(PLAYER_SPAWN_X);
    g_players[0].y = TO_POS(PLAYER_SPAWN_Y);
}

/* The bouncer stays inside the room and bounces off floor and ceiling. */
static void test_bouncer(void)
{
    int f, flips = 0;
    uint8_t last_down;
    Enemy *e;

    setup_room(5);
    enemy_spawn(TO_POS(100), TO_POS(100), ENEMY_TYPE_BOUNCER, 0, 0);
    e = &g_enemies[0];
    last_down = e->moving_down;
    for (f = 0; f < 1200; f++) {
        enemies_update();
        CHECK(PX(e->y) >= FIELD_Y && PX(e->y) < FIELD_BOTTOM - 8, "bouncer left the room (y=%u)", PX(e->y));
        if (e->moving_down != last_down) {
            flips++;
            last_down = e->moving_down;
        }
    }
    CHECK(flips >= 2, "bouncer never bounced vertically (%d)", flips);
}

/* A thrower at the player's height throws a rock that can hit the player. */
static void test_thrower(void)
{
    int f, rocks = 0, hit = 0, i;

    setup_room(0);
    enemy_spawn(TO_POS(150), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_THROWER, 0, 1);
    g_enemies[0].throw_timer = 0;
    for (f = 0; f < 120 && !hit; f++) {
        enemies_update();
        items_update();
        for (i = 0; i < MAX_ITEMS; i++) {
            rocks += g_items[i].kind == IT_ROCK;
        }
        for (i = 0; i < MAX_ITEMS; i++) {
            if (g_items[i].kind == IT_ROCK && overlap(g_players[0].x, g_players[0].y, g_items[i].x, g_items[i].y, 8, 8)) {
                hit = 1;
            }
        }
    }
    CHECK(rocks > 0, "thrower never threw");
    CHECK(hit, "rock never reached the player");
}

/* The chaser flies through walls and catches a player who stands still. */
static void test_chaser(void)
{
    int f, caught = 0;

    setup_room(2);
    chaser_spawn();
    for (f = 0; f < 900 && !caught; f++) {
        chaser_update();
        caught = enemies_touch(g_players[0].x, g_players[0].y);
    }
    CHECK(caught, "chaser never reached the player");
}

/* Two bots: player 2 joins with Start, both play and score. */
static void test_two_players(void)
{
    uint16_t pad[NUM_PLAYERS];
    uint16_t dir0 = PAD_RIGHT, dir1 = PAD_LEFT;
    uint32_t f;
    int p2_joined = 0, both_alive = 0, p2_scored = 0;
    int i;

    bot_rng = 0x4321;
    for (i = 0; i < SRAM_SIZE; i++) {
        game_sram()[i] = 0;
    }
    game_init(99);
    for (f = 0; f < 60u * 60u * 5u; f++) {
        pad[0] = bot_pad(f, &dir0);
        pad[1] = (uint16_t)(bot_pad(f + 7, &dir1) & (uint16_t)~PAD_START);
        if (f == 600 || (g_players[1].state == PS_OFF && (f % 500) == 0)) {
            pad[1] |= PAD_START;
        }
        game_tick(pad);
        if (g_players[1].state != PS_OFF) {
            p2_joined = 1;
        }
        if (g_players[0].state == PS_ALIVE && g_players[1].state == PS_ALIVE) {
            both_alive = 1;
        }
        if (g_players[1].score[7] || g_players[1].score[6] || g_players[1].score[5] || g_players[1].score[4]) {
            p2_scored = 1;
        }
    }
    CHECK(p2_joined, "player 2 never joined");
    CHECK(both_alive, "the two players were never alive at the same time");
    CHECK(p2_scored, "player 2 never scored");
}

static uint8_t count_thrown(uint8_t *tiers)
{
    uint8_t i, n = 0;
    *tiers = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        if (g_items[i].kind == IT_THROWN) {
            n++;
            *tiers = (uint8_t)(*tiers | (1u << g_items[i].tier));
        }
    }
    return n;
}

/* Water poured on the floor sweeps the enemies there as one combo. */
static void test_water(void)
{
    uint8_t tiers, n;
    int f;

    setup_room(0);
    g_players[0].x = TO_POS(200);   /* out of the way */
    enemy_spawn(TO_POS(60), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_WALKER, 0, 0);
    enemy_spawn(TO_POS(90), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_WALKER, 0, 0);
    effect_start(BK_WATER, TO_POS(40), TO_POS(PLAYER_SPAWN_Y - 30), 1, 0);
    for (f = 0; f < 120; f++) {
        effects_update();
    }
    n = count_thrown(&tiers);
    CHECK(n == 2, "water should sweep 2 enemies, got %u", n);
    CHECK(tiers == 0x03, "water kills should be one combo (tiers mask 0x%02X)", tiers);
}

/* A lightning bolt flies across and defeats an enemy in its row. */
static void test_lightning(void)
{
    uint8_t tiers;
    int f;

    setup_room(0);
    enemy_spawn(TO_POS(180), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_WALKER, 0, 0);
    effect_start(BK_LIGHTNING, TO_POS(40), TO_POS(PLAYER_SPAWN_Y), 1, 0);
    for (f = 0; f < 80; f++) {
        effects_update();
    }
    CHECK(count_thrown(&tiers) == 1, "lightning should defeat the enemy");
}

/* Fire lands on the floor, defeats an enemy in it and stuns the player. */
static void test_fire(void)
{
    uint8_t tiers;
    int f, stunned = 0;

    setup_room(0);
    g_players[0].x = TO_POS(84);
    enemy_spawn(TO_POS(104), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_WALKER, 0, 0);
    effect_start(BK_FIRE, TO_POS(96), TO_POS(PLAYER_SPAWN_Y - 40), 1, 0);
    for (f = 0; f < 100; f++) {
        effects_update();
        stunned |= g_players[0].stun != 0;
    }
    CHECK(count_thrown(&tiers) == 1, "fire should defeat the enemy");
    CHECK(stunned, "fire should stun the player");
}

/* Six letters: an extra life and the round ends. */
static void test_extend(void)
{
    uint8_t k, lives;

    setup_room(0);
    g_players[0].lives = 2;
    g_players[0].letters = 0;
    g_skip_rounds = 0;
    lives = g_players[0].lives;
    for (k = 0; k < 6; k++) {
        extend_collect(0, k);
    }
    CHECK(g_players[0].lives == lives + 1, "EXTEND should give an extra life");
    CHECK(g_skip_rounds == 1, "EXTEND should end the round");
    CHECK(g_players[0].letters == 0, "letters start over after EXTEND");
}

/* Counters unlock power-ups; candies stay with the player, the umbrella skips rounds. */
static void test_powerups(void)
{
    setup_room(0);
    g_count.queued = 0;
    g_count.blown = 35;
    counters_check();
    CHECK(g_count.queued == (1u << POWER_RANGE), "35 bubbles blown should unlock the range candy");

    g_players[0].powers = 0;
    item_spawn_powerup(POWER_RATE, g_players[0].x, g_players[0].y);
    items_update();
    CHECK(g_players[0].powers & (1u << POWER_RATE), "picking up the candy should give the power");

    g_skip_rounds = 0;
    item_spawn_powerup(POWER_UMBRELLA, g_players[0].x, g_players[0].y);
    items_update();
    CHECK(g_skip_rounds == 3, "the umbrella should skip 3 rounds");
}

/* Round 2 releases a water bubble every 15 s through its bottom gap. */
static void test_special_emit(void)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    int f, i, seen = 0;

    for (i = 0; i < SRAM_SIZE; i++) {
        game_sram()[i] = 0;
    }
    game_init(5);
    game_debug_start_round(2);
    pad[0] = PAD_START;
    game_tick(pad);
    pad[0] = 0;
    for (f = 0; f < 60 * 25 && !seen; f++) {
        g_players[0].invulnerable = 200;   /* survive while standing still */
        game_tick(pad);
        for (i = 0; i < MAX_BUBBLES; i++) {
            if (g_bubbles[i].state == BS_FLOAT && g_bubbles[i].kind == BK_WATER) {
                seen = f;
            }
        }
    }
    if (seen) {
        const Sprite *oam = game_oam();
        int drawn = 0;
        for (i = 0; i < MAX_SPRITES; i++) {
            drawn |= oam[i].y < SCREEN_H && oam[i].tile == SPR_SPECIAL_WATER;
        }
        CHECK(drawn, "the water bubble is not in the sprite table");
    }
    game_debug_start_round(1);

    /* With all bubble slots full, a special bubble replaces the oldest plain one. */
    {
        uint8_t k, found = 0;
        setup_room(1);
        for (k = 0; k < MAX_BUBBLES; k++) {
            g_bubbles[k] = make_trapped((uint8_t)(30 + k * 12), 100);
            g_bubbles[k].state = BS_FLOAT;
            g_bubbles[k].age = (uint16_t)(10 + k);
        }
        bubble_spawn_special(BK_FIRE, 0, TO_POS(100), TO_POS(150));
        for (k = 0; k < MAX_BUBBLES; k++) {
            found = (uint8_t)(found | (g_bubbles[k].kind == BK_FIRE));
        }
        CHECK(found, "special bubble was not placed when all slots were full");
        if (!found) {
            for (k = 0; k < MAX_BUBBLES; k++) {
                printf("  slot %u: state %u kind %u age %u\n", k, g_bubbles[k].state, g_bubbles[k].kind, g_bubbles[k].age);
            }
        }
    }
    CHECK(seen, "no water bubble appeared in round 2 within 25 s (state %d, round %u)", (int)g_state, g_round);
    if (seen) {
        printf("water bubble after %d frames\n", seen);
    }
}

/* Out of lives: the continue countdown; Start brings the player back
 * with fresh lives and the same score, waiting it out ends the game. */
static void test_continue(void)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    int f;

    setup_room(0);
    g_players[0].lives = 1;
    g_players[0].score[4] = 7;
    player_killed(&g_players[0]);
    CHECK(g_state == GS_CONTINUE, "losing the last life should offer a continue");

    pad[0] = PAD_START;
    game_tick(pad);
    CHECK(g_state == GS_PLAY && g_players[0].state == PS_ALIVE && g_players[0].lives == 3,
          "Start should continue with 3 lives");
    CHECK(g_players[0].score[4] == 7, "continuing keeps the score");

    g_players[0].lives = 1;
    player_killed(&g_players[0]);
    pad[0] = 0;
    for (f = 0; f < 60 * 11 && g_state == GS_CONTINUE; f++) {
        game_tick(pad);
    }
    CHECK(g_state == GS_GAME_OVER, "the countdown should end in game over (state %d)", (int)g_state);
}

/* The round's bonus food appears after 10 s and is worth its value. */
static void test_bonus_food(void)
{
    static const uint8_t expect[8] = { 0, 0, 0, 0, 0, 7, 0, 0 };   /* apple: 700 */
    uint8_t i, found = 0;

    setup_room(1);
    for (i = 0; i < 8; i++) {
        g_players[0].score[i] = 0;
    }
    g_level_frames = 600;
    bonus_round_tick();
    for (i = 0; i < MAX_ITEMS; i++) {
        if (g_items[i].kind == IT_BONUS) {
            found = 1;
            g_players[0].x = g_items[i].x;
            g_players[0].y = g_items[i].y;
        }
    }
    CHECK(found, "no bonus food placed");
    items_update();
    CHECK(score_cmp(g_players[0].score, expect) == 0, "round 2 bonus food should be worth 700");
}

/* A water stream picks up a player and lets go when it ends. */
static void test_water_carries(void)
{
    int f, carried = 0;

    setup_room(0);
    g_players[0].x = TO_POS(60);
    effect_start(BK_WATER, TO_POS(40), TO_POS(PLAYER_SPAWN_Y), 1, 0);
    for (f = 0; f < 500; f++) {
        effects_update();
        carried |= g_players[0].carried;
    }
    CHECK(carried, "water should carry the player");
    CHECK(!g_players[0].carried, "the player should be released when the water ends");
    CHECK(g_players[0].state == PS_ALIVE, "water must not hurt players");
}

/* Clearing a round scrolls the next one in: layer 0 moves by one page. */
static void test_transition(void)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    int f, i, saw_transition = 0, scroll_moved = 0;

    for (i = 0; i < SRAM_SIZE; i++) {
        game_sram()[i] = 0;
    }
    game_init(3);
    pad[0] = PAD_START;
    game_tick(pad);
    pad[0] = 0;
    for (f = 0; f < 60 * 20 && g_round == 1; f++) {
        g_players[0].invulnerable = 200;
        if (g_state == GS_PLAY) {
            enemies_clear();          /* "defeat" everybody */
        }
        game_tick(pad);
    }
    for (f = 0; f < 600 && g_state != GS_ROUND_INTRO; f++) {
        saw_transition |= g_state == GS_TRANSITION;
        scroll_moved |= game_bg_scroll_y() != 0;
        game_tick(pad);
    }
    CHECK(g_round == 2, "round 1 was not left (round %u)", g_round);
    CHECK(saw_transition && scroll_moved, "no scrolling transition seen");
    CHECK(g_state == GS_ROUND_INTRO && game_bg_scroll_y() == PAGE_PX, "transition should end on page 1 (scroll %u)",
          game_bg_scroll_y());
    CHECK(game_bg_window_top() == FIELD_Y, "the level must not show behind the HUD");
}

int main(void)
{
    const char *t = getenv("BB_TRACE");
    trace = t ? strtoul(t, 0, 10) : 0;
    test_score();
    test_levels();
    test_chain();
    test_push();
    test_food_value();
    test_bouncer();
    test_thrower();
    test_chaser();
    test_two_players();
    test_water();
    test_lightning();
    test_fire();
    test_extend();
    test_powerups();
    test_special_emit();
    test_continue();
    test_bonus_food();
    test_water_carries();
    test_transition();
    test_determinism_and_play();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
