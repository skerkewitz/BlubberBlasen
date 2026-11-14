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
#include <string.h>

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

    CHECK(level_count == 101, "expected 100 rounds and the secret room, got %u", level_count);
    CHECK(level_defs[99].kind == LEVEL_BOSS && level_defs[100].kind == LEVEL_SECRET, "round 100 is the boss, then the secret room");
    CHECK(level_defs[0].hurry_frames == 40 * 60, "round001.txt sets hurry 40");
    CHECK(level_defs[0].enemies[2].delay == 30, "round001.txt sets delay 30 for enemy 3");
    CHECK(level_defs[1].special == SPECIAL_WATER && level_defs[1].special_frames == 15 * 60,
          "round002.txt sets special water 15");
    for (l = 0; l < level_count; l++) {
        const LevelDef *d = &level_defs[l];
        CHECK(d->kind != LEVEL_NORMAL || (d->enemy_count > 0 && d->enemy_count <= LEVEL_MAX_ENEMIES),
              "level %u enemy count %u", l + 1, d->enemy_count);
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
                CHECK(oam[i].tile < 176, "frame %lu: sprite %d uses unknown cell %u", (unsigned long)f, i, oam[i].tile);
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
    memset(&b, 0, sizeof b);   /* new fields default to 0 */
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
    b.vx = 0;
    b.vy = 0;
    b.owner = 0;
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
    player_start(&g_players[0]);   /* resets dash, rescue ... */
    g_players[0].invulnerable = 0;
    g_players[0].energy = 0;
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
    memset(&g_count, 0, sizeof g_count);
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

static int count_shots(uint8_t shot)
{
    int i, n = 0;
    for (i = 0; i < MAX_ITEMS; i++) {
        n += g_items[i].kind == IT_ROCK && g_items[i].tier == shot;
    }
    return n;
}

/* Flutter: changes direction, stays inside the room. */
static void test_flutter(void)
{
    int f, turns = 0;
    uint8_t last;
    Enemy *e;

    setup_room(5);
    enemy_spawn(TO_POS(100), TO_POS(100), ENEMY_TYPE_FLUTTER, 0, 0);
    e = &g_enemies[0];
    last = (uint8_t)(e->facing_left | (e->moving_down << 1));
    for (f = 0; f < 1800; f++) {
        uint8_t now;
        enemies_update();
        now = (uint8_t)(e->facing_left | (e->moving_down << 1));
        turns += now != last;
        last = now;
        CHECK(PX(e->y) >= FIELD_Y && PX(e->y) < FIELD_BOTTOM - 8, "flutter left the room");
        CHECK(PX(e->x) + 8 >= BODY_MIN_CX && PX(e->x) + 8 <= BODY_MAX_CX, "flutter left the room sideways");
        if (failures > 5) {
            return;
        }
    }
    CHECK(turns >= 10, "flutter should change direction often (%d)", turns);
}

/* Hopper: leaves the ground again and again and lands each time. */
static void test_hopper(void)
{
    int f, takeoffs = 0, was_down = 1, top = 255;
    Enemy *e;

    setup_room(0);
    enemy_spawn(TO_POS(100), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_HOPPER, 0, 0);
    e = &g_enemies[0];
    for (f = 0; f < 600; f++) {
        int down;
        enemies_update();
        down = level_on_ground(e->x, e->y);
        takeoffs += was_down && !down;
        was_down = down;
        if (PX(e->y) < top) {
            top = PX(e->y);
        }
    }
    CHECK(takeoffs >= 5, "hopper should keep hopping (%d)", takeoffs);
    CHECK(top < PLAYER_SPAWN_Y - 20, "hopper jumps too low (top %d)", top);
}

/* Spitter: spits at a player in line without stopping. */
/* Spitter: winds up (stands still) for ENEMY_WINDUP frames, then spits
 * the way it faced; afterwards it walks on. */
static void test_spitter(void)
{
    int f;
    uint16_t x0;

    setup_room(0);
    enemy_spawn(TO_POS(180), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_SPITTER, 0, 1);
    g_enemies[0].throw_timer = 0;
    x0 = g_enemies[0].x;
    enemies_update();   /* sees the player: the wind-up starts */
    CHECK(enemy_shot_in(&g_enemies[0]) == ENEMY_WINDUP, "the spitter should start a wind-up (%u)",
          enemy_shot_in(&g_enemies[0]));
    g_players[0].x = TO_POS(120);   /* the player moving does not turn it */
    for (f = 1; f < ENEMY_WINDUP; f++) {
        enemies_update();
        items_update();
    }
    CHECK(count_shots(SHOT_SPIT) == 0, "no shot before the wind-up is over");
    CHECK(g_enemies[0].x == x0, "the spitter stands still while winding up");
    enemies_update();
    CHECK(count_shots(SHOT_SPIT) == 1, "the spit leaves when the wind-up ends");
    CHECK(g_items[0].vx < 0, "the spit flies the way the spitter faced");
    for (f = 0; f < ENEMY_RECOVER + 4; f++) {
        enemies_update();
    }
    CHECK(g_enemies[0].x != x0, "after the shot the spitter walks on");
}

/* Dropper: shoots down at a player below it. */
static void test_dropper(void)
{
    int f;

    setup_room(0);
    /* on the left platform of round 1 (row 20), the player below it */
    enemy_spawn(TO_POS(24), TO_POS(FIELD_Y + 19 * 8 - 8), ENEMY_TYPE_DROPPER, 0, 0);
    g_enemies[0].throw_timer = 0;
    g_players[0].x = TO_POS(28);
    for (f = 0; f < ENEMY_WINDUP && count_shots(SHOT_DOWN) == 0; f++) {
        enemies_update();
    }
    CHECK(count_shots(SHOT_DOWN) == 0, "the dropper winds up before it shoots");
    for (f = 0; f < 10 && count_shots(SHOT_DOWN) == 0; f++) {
        enemies_update();
    }
    CHECK(count_shots(SHOT_DOWN) > 0, "dropper never shot down");
}

/* Title -> three story pages (B turns the page) -> game. */
static void test_story(void)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    int k;

    game_debug_skip_story(0);
    game_init(1);
    pad[0] = PAD_START;
    game_tick(pad);
    CHECK(g_state == GS_STORY, "Start on the title should show the story");
    for (k = 0; k < 3; k++) {
        pad[0] = 0;
        game_tick(pad);
        pad[0] = PAD_B;
        game_tick(pad);
    }
    CHECK(g_state == GS_ROUND_INTRO, "after three pages the game should start (state %d)", (int)g_state);

    game_init(1);
    pad[0] = PAD_START;
    game_tick(pad);
    pad[0] = 0;
    game_tick(pad);
    pad[0] = PAD_START;
    game_tick(pad);
    CHECK(g_state == GS_ROUND_INTRO, "Start should skip the story");
    game_debug_skip_story(1);
}

/* Ticks with no buttons (players kept alive) until a state is reached. */
static int run_until(GameState st, int max_frames)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    int f;
    for (f = 0; f < max_frames && g_state != st; f++) {
        g_players[0].invulnerable = 200;
        g_players[1].invulnerable = 200;
        game_tick(pad);
    }
    return g_state == st;
}

static void start_at_round(uint8_t round, int two_players)
{
    uint16_t pad[NUM_PLAYERS] = { PAD_START, 0 };
    int i;
    for (i = 0; i < SRAM_SIZE; i++) {
        game_sram()[i] = 0;
    }
    game_init(7);
    game_debug_start_round(round);
    game_tick(pad);
    if (two_players) {
        pad[0] = 0;
        pad[1] = PAD_START;
        game_tick(pad);
    }
    game_debug_start_round(1);
}

/* Round 100: lightning beats the boss, the giant bubble is popped, ending. */
static void boss_fight(int two_players)
{
    int f, k;

    start_at_round(100, two_players);
    CHECK(run_until(GS_PLAY, 600), "the boss round did not start");
    CHECK(boss_fighting(), "no boss in round 100");
    /* a Fire Glove fireball does nothing to it */
    k = boss_hp();
    effect_start_glove((uint16_t)(boss_center_x() - TO_POS(8)), (uint16_t)(boss_center_y() - TO_POS(8)), 1, 0);
    effects_update();
    CHECK(boss_hp() == k, "only lightning may hurt the boss");
    /* lightning does */
    for (f = 0; f < 2000 && boss_fighting(); f++) {
        if ((f % 25) == 0) {
            (void)boss_hit_by_bolt((uint16_t)(boss_center_x() - TO_POS(8)), (uint16_t)(boss_center_y() - TO_POS(8)));
        }
        g_players[0].invulnerable = 200;
        g_players[1].invulnerable = 200;
        game_tick((const uint16_t[NUM_PLAYERS]){ 0, 0 });
    }
    CHECK(!boss_fighting() && !boss_defeated(), "the boss should be trapped in its bubble");
    for (f = 0; f < 200; f++) {   /* walk into the giant bubble */
        g_players[0].x = (uint16_t)(boss_center_x() - TO_POS(8));
        g_players[0].y = (uint16_t)(boss_center_y() - TO_POS(8));
        game_tick((const uint16_t[NUM_PLAYERS]){ 0, 0 });
        if (boss_defeated()) {
            break;
        }
    }
    CHECK(boss_defeated(), "touching the giant bubble should pop it");
    CHECK(run_until(GS_ENDING, 300), "no ending after the boss");
}

static void test_boss_and_endings(void)
{
    boss_fight(0);
    CHECK(g_state == GS_ENDING, "ending");
    {
        int pages = 0;
        uint16_t pad[NUM_PLAYERS] = { 0, 0 };
        while (g_state == GS_ENDING && pages < 10) {
            pad[0] = PAD_B;
            game_tick(pad);
            pad[0] = 0;
            game_tick(pad);
            pages++;
        }
        CHECK(pages == 3, "one player: sad ending with 2 pages and credits (%d)", pages);
        CHECK(g_state == GS_NAME_ENTRY || g_state == GS_HISCORES, "after the ending: high scores");
    }
    boss_fight(1);
    {
        int pages = 0;
        uint16_t pad[NUM_PLAYERS] = { 0, 0 };
        while (g_state == GS_ENDING && pages < 10) {
            pad[0] = PAD_B;
            game_tick(pad);
            pad[0] = 0;
            game_tick(pad);
            pages++;
        }
        CHECK(pages == 4, "two players: happy ending with 3 pages and credits (%d)", pages);
    }
}

/* Round 20 without losing a life: a door to the secret room, then round 21. */
static void test_secret_room(void)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    int f, gems = 0, i;

    start_at_round(20, 0);
    CHECK(run_until(GS_PLAY, 600), "round 20 did not start");
    g_players[0].x = TO_POS(192);
    g_players[0].y = TO_POS(PLAYER_SPAWN_Y);
    pad[0] = PAD_UP;
    game_tick(pad);
    CHECK(g_state == GS_TRANSITION, "Up at the door should leave the round");
    CHECK(run_until(GS_PLAY, 800), "secret room did not start");
    CHECK(level_defs[g_level].kind == LEVEL_SECRET, "not in the secret room");
    for (i = 0; i < MAX_ITEMS; i++) {
        gems += g_items[i].kind == IT_FOOD;
    }
    CHECK(gems >= 6, "the secret room should be full of gems (%d)", gems);
    for (f = 0; f < 1200 && level_defs[g_level].kind == LEVEL_SECRET; f++) {
        pad[0] = 0;
        game_tick(pad);
    }
    CHECK(g_round == 21 && level_defs[g_level].kind == LEVEL_NORMAL, "after the secret room comes round 21 (%u)", g_round);

    /* round 50: the door is a warp to round 70 */
    start_at_round(50, 0);
    CHECK(run_until(GS_PLAY, 600), "round 50 did not start");
    g_players[0].x = TO_POS(192);
    g_players[0].y = TO_POS(PLAYER_SPAWN_Y);
    pad[0] = PAD_UP;
    game_tick(pad);
    CHECK(g_round == 70, "the round 50 door should warp to round 70 (%u)", g_round);

    /* no door after losing a life */
    start_at_round(19, 0);
    player_killed(&g_players[0]);
    CHECK(run_until(GS_PLAY, 300) || 1, "");
    g_skip_rounds = 1;
    run_until(GS_TRANSITION, 60);
    run_until(GS_PLAY, 800);
    g_players[0].x = TO_POS(192);
    g_players[0].y = TO_POS(PLAYER_SPAWN_Y);
    pad[0] = PAD_UP;
    game_tick(pad);
    CHECK(g_round == 20 && g_state == GS_PLAY, "no door in round 20 after a lost life");
}

/* Skipping never goes past round 100. */
static void test_round_limit(void)
{
    start_at_round(99, 0);
    CHECK(run_until(GS_PLAY, 600), "round 99 did not start");
    g_skip_rounds = 3;   /* umbrella */
    run_until(GS_TRANSITION, 30);
    CHECK(g_round == 100 && level_defs[g_level].kind == LEVEL_BOSS, "the umbrella should stop at the boss (%u)", g_round);
}

/* The new items: Soap Star, Steam Whistle, Fire Glove, Bubble Ring, and their counters. */
static void test_special_items(void)
{
    uint8_t tiers;
    int f;

    setup_room(0);
    memset(&g_count, 0, sizeof g_count);
    g_count.food = 10;
    g_count.defeated = 40;
    g_count.water_kills = 6;
    g_count.fire_kills = 4;
    g_count.candies = 3;
    counters_check();
    CHECK(g_count.queued == (POWER_BIT(POWER_STAR) | POWER_BIT(POWER_WAVE) | POWER_BIT(POWER_WHISTLE) |
                             POWER_BIT(POWER_GLOVE) | POWER_BIT(POWER_RING)), "counters should unlock the five items");

    enemy_spawn(TO_POS(120), TO_POS(100), ENEMY_TYPE_WALKER, 0, 0);
    enemy_spawn(TO_POS(160), TO_POS(60), ENEMY_TYPE_BOUNCER, 0, 0);
    item_spawn_powerup(POWER_STAR, g_players[0].x, g_players[0].y);
    items_update();
    CHECK(enemies_count() == 0 && count_thrown(&tiers) == 2 && tiers == (1u << GEM_TIER), "Soap Star: all enemies become gems");

    setup_room(0);
    enemy_spawn(TO_POS(60), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_WALKER, 0, 0);
    item_spawn_powerup(POWER_WHISTLE, g_players[0].x, g_players[0].y);
    items_update();
    CHECK(g_freeze > 0, "Steam Whistle should freeze");
    CHECK(!enemies_touch(g_enemies[0].x, g_enemies[0].y), "frozen enemies are harmless");
    enemies_defeat_at(g_enemies[0].x, g_enemies[0].y, 0);
    CHECK(enemies_count() == 0, "touching a frozen enemy defeats it");
    g_freeze = 0;

    setup_room(0);
    enemy_spawn(TO_POS(150), TO_POS(PLAYER_SPAWN_Y), ENEMY_TYPE_WALKER, 0, 0);
    g_players[0].powers = POWER_BIT(POWER_GLOVE) | POWER_BIT(POWER_RING);
    memset(g_players[0].score, 0, 8);
    g_state = GS_PLAY;
    for (f = 0; f < 60 && enemies_count(); f++) {
        player_update(&g_players[0], f == 1 ? PAD_Y : 0, f == 1 ? PAD_Y : 0);
        effects_update();
    }
    CHECK(enemies_count() == 0 && count_thrown(&tiers) == 1 && tiers == (1u << GEM_TIER), "the glove fireball should defeat the enemy");
    CHECK(g_players[0].score[5] == 1, "Bubble Ring: +100 per shot");
}

/* Title music around the title, game music in the rounds. */
static void test_music(void)
{
    uint16_t pad[NUM_PLAYERS] = { PAD_START, 0 };
    game_init(1);
    CHECK(game_music() == MUSIC_TITLE, "title screen should ask for the title music");
    game_tick(pad);
    CHECK(game_music() == MUSIC_GAME, "the round should ask for the game music");
    g_state = GS_GAME_OVER;
    CHECK(game_music() == MUSIC_NONE, "game over is silent");
}

/* Soak test (design guide "CPU partner"): the CPU plays both players.
 * Catches crashes, broken invariants and rounds where nobody gets on. */
static int soak_deaths;

static void soak(uint8_t plus, uint8_t rescue, uint16_t minutes, int *best_round, int *rounds_cleared)
{
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    uint32_t f, frames = (uint32_t)minutes * 3600u;
    uint8_t last_round = 0;
    int i;

    for (i = 0; i < SRAM_SIZE; i++) {
        game_sram()[i] = 0;
    }
    game_init(321);
    g_opt.plus = plus;
    g_opt.rescue = rescue;
    g_opt.p2_cpu = 0;
    game_debug_cpu(3);
    *best_round = 0;
    *rounds_cleared = 0;
    soak_deaths = 0;
    {
        uint16_t *c = (uint16_t *)game_debug_death_causes();
        for (i = 0; i < DEBUG_DEATH_CAUSES; i++) {
            c[i] = 0;
        }
    }
    for (f = 0; f < frames; f++) {
        pad[0] = (uint16_t)((g_state != GS_PLAY && g_state != GS_ROUND_INTRO && g_state != GS_TRANSITION &&
                             (f % 120) < 2) ? PAD_START : 0);
        game_tick(pad);
        soak_deaths += (game_sfx() >> SFX_DEATH) & 1;
        if (getenv("BB_DEATHS") && ((game_sfx() >> SFX_DEATH) & 1) && soak_deaths <= 25 && plus == 0) {
            int k, j;
            DebugBox bx[1];
            const char *act = "";
            (void)game_debug_cpu_view(bx, 0, &act);
            for (k = 0; k < NUM_PLAYERS; k++) {
                if (g_players[k].state == PS_DYING && g_players[k].timer == 0) {
                    printf("death P%d at (%u,%u) ground=%u vx=%d vy=%d act=%s round=%u:", k + 1, PX(g_players[k].x),
                           PX(g_players[k].y), g_players[k].on_ground, g_players[k].vx, g_players[k].vy, act, g_round);
                    for (j = 0; j < MAX_ENEMIES; j++) {
                        const Enemy *e = &g_enemies[j];
                        if (e->active && !e->entering && overlap(g_players[k].x, g_players[k].y, e->x, e->y, 14, 16)) {
                            printf(" E%u(%u,%u) left=%u jump=%u angry=%u", e->type, PX(e->x), PX(e->y), e->facing_left,
                                   e->jump_left, e->angry);
                        }
                    }
                    printf("\n");
                }
            }
        }
        if (g_state == GS_ROUND_INTRO && g_round != last_round) {
            if (last_round && g_round > last_round) {
                (*rounds_cleared)++;
            }
            last_round = g_round;
        }
        if (g_round > *best_round) {
            *best_round = g_round;
        }
        for (i = 0; i < NUM_PLAYERS; i++) {
            if (g_players[i].state == PS_ALIVE) {
                uint8_t cx = (uint8_t)(PX(g_players[i].x) + 8);
                CHECK(cx >= BODY_MIN_CX && cx <= BODY_MAX_CX, "soak: player outside the walls");
            }
        }
        if (failures > 10) {
            break;
        }
    }
    game_debug_cpu(0);
    g_opt.plus = 0;
    g_opt.rescue = 0;
    {
        static const char *const names[DEBUG_DEATH_CAUSES] = {
            "?", "walker", "bouncer", "thrower", "flutter", "hopper", "spitter", "dropper",
            "shot", "chaser", "boss", "rescue ran out"
        };
        const uint16_t *c = game_debug_death_causes();
        printf("  deaths by cause:");
        for (i = 0; i < DEBUG_DEATH_CAUSES; i++) {
            if (c[i]) {
                printf(" %s %u,", names[i], c[i]);
            }
        }
        printf("\n");
    }
    if (getenv("BB_TRACE")) {
        printf("soak end: state %d round %u P1 %d/%u P2 %d/%u\n", (int)g_state, g_round, g_players[0].state,
               g_players[0].lives, g_players[1].state, g_players[1].lives);
    }
}

static void test_cpu_soak(void)
{
    int best, cleared;
    soak(0, 0, 30, &best, &cleared);
    printf("cpu soak (arcade): best round %d, %d rounds cleared, %d deaths\n", best, cleared, soak_deaths);
    CHECK(cleared >= 12, "two CPU players should clear at least 12 rounds in 30 minutes (%d)", cleared);
    CHECK(soak_deaths <= 100, "the CPU players die too often (%d deaths in 30 minutes)", soak_deaths);
    soak(1, 1, 30, &best, &cleared);
    printf("cpu soak (plus + rescue): best round %d, %d rounds cleared, %d deaths\n", best, cleared, soak_deaths);
    CHECK(cleared >= 12, "two CPU players should clear at least 12 rounds in 30 minutes in Plus mode (%d)", cleared);
    CHECK(soak_deaths <= 100, "the CPU players die too often in Plus mode (%d deaths in 30 minutes)", soak_deaths);
}

static void setup_two_players(uint8_t level)
{
    uint8_t rescue = g_opt.rescue;
    setup_room(level);
    g_opt.rescue = rescue;   /* game_init reloads the options */
    g_players[1].id = 1;
    g_players[1].lives = 3;
    player_start(&g_players[1]);
    g_players[1].invulnerable = 0;
    g_players[0].lives = 3;
    g_players[0].energy = 0;
    g_players[1].energy = 0;
}

/* Co-op rescue: trapped instead of killed, freed by the partner;
 * an expired bubble costs a life; shots still kill. */
static void test_rescue(void)
{
    int f;

    g_opt.rescue = 1;
    setup_two_players(0);
    enemy_spawn(g_players[0].x, g_players[0].y, ENEMY_TYPE_WALKER, 0, 0);
    g_sfx = 0;
    player_update(&g_players[0], 0, 0);
    CHECK(g_players[0].state == PS_RESCUE, "enemy contact should trap player 1 in a rescue bubble");
    CHECK((g_sfx & ((uint32_t)1u << SFX_HELP)) && !(g_sfx & ((uint32_t)1u << SFX_TRAP)),
          "a trapped player calls for help (not the enemy trap sound)");
    enemies_clear();
    for (f = 0; f < 30; f++) {
        player_update(&g_players[0], 0, 0);
    }
    g_players[1].x = g_players[0].x;
    g_players[1].y = g_players[0].y;
    player_update(&g_players[0], 0, 0);
    CHECK(g_players[0].state == PS_ALIVE && g_players[0].lives == 3, "the partner should free player 1 without a life lost");

    setup_two_players(0);
    enemy_spawn(g_players[0].x, g_players[0].y, ENEMY_TYPE_WALKER, 0, 0);
    player_update(&g_players[0], 0, 0);
    enemies_clear();
    {
        int calls = 0, call_at = -1;
        for (f = 0; f < 600 && g_players[0].state == PS_RESCUE; f++) {
            g_sfx = 0;
            player_update(&g_players[0], 0, 0);
            if (g_sfx & ((uint32_t)1u << SFX_HELP)) {
                calls++;
                call_at = f;
            }
        }
        CHECK(calls == 1 && call_at >= 270 && call_at <= 290,
              "a second call for help when 2/3 of the time is over (%d calls, last at frame %d)", calls, call_at);
    }
    CHECK(g_players[0].state == PS_DYING, "an expired rescue bubble costs a life");

    setup_two_players(0);
    item_spawn_shot(SHOT_ROCK, g_players[0].x, g_players[0].y, 1);
    player_update(&g_players[0], 0, 0);
    CHECK(g_players[0].state == PS_DYING, "shots still kill with rescue on");

    setup_room(0);   /* one player alone: no rescue */
    g_opt.rescue = 1;
    g_players[1].state = PS_OFF;
    enemy_spawn(g_players[0].x, g_players[0].y, ENEMY_TYPE_WALKER, 0, 0);
    player_update(&g_players[0], 0, 0);
    CHECK(g_players[0].state == PS_DYING, "rescue needs a partner");
    g_opt.rescue = 0;
}

/* Plus mode: energy from defeats, drain after the hold time, dash, a full bar. */
static void test_plus_mode(void)
{
    int f;
    uint16_t x0;
    uint8_t e;

    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = 0;
    energy_gain(0, ENERGY_PER_ENEMY);
    CHECK(g_players[0].energy == ENERGY_PER_ENEMY, "defeating an enemy gives energy");
    for (f = 0; f < ENERGY_HOLD; f++) {
        energy_tick();
    }
    e = g_players[0].energy;
    CHECK(e == ENERGY_PER_ENEMY, "energy holds for a while after a gain");
    for (f = 0; f < 40; f++) {
        g_frame++;
        energy_tick();
    }
    CHECK(g_players[0].energy < e, "then it drains");

    /* dash: about 30 px, invulnerable in the middle, costs energy */
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = ENERGY_MAX;
    g_players[0].x = TO_POS(60);
    x0 = g_players[0].x;
    player_update(&g_players[0], PAD_R, PAD_R);
    for (f = 0; f < 4; f++) {
        player_update(&g_players[0], 0, 0);
    }
    enemy_spawn(g_players[0].x, g_players[0].y, ENEMY_TYPE_WALKER, 0, 0);
    player_update(&g_players[0], 0, 0);
    CHECK(g_players[0].state == PS_ALIVE, "invulnerable in the middle of a dash");
    enemies_clear();
    for (f = 0; f < 20; f++) {
        player_update(&g_players[0], 0, 0);
    }
    CHECK(PX(g_players[0].x) - PX(x0) >= 48 && PX(g_players[0].x) - PX(x0) <= 64, "a dash covers 6-8 tiles (%d px)",
          PX(g_players[0].x) - PX(x0));
    CHECK(g_players[0].energy == ENERGY_MAX - DASH_COST, "a dash costs energy");
    CHECK(g_players[0].facing_left == 0, "dash keeps the facing");

    /* after the dash: still safe long enough to jump clear */
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = ENERGY_MAX;
    g_players[0].x = TO_POS(60);
    player_update(&g_players[0], PAD_R, PAD_R);
    CHECK(player_dash_invulnerable(&g_players[0]), "invulnerable from the first dash frame");
    for (f = 0; f < DASH_FRAMES - 1; f++) {
        player_update(&g_players[0], 0, 0);
    }
    CHECK(g_players[0].dash == 0 && g_players[0].invulnerable >= DASH_GRACE - 1, "the dash ends with a grace time");
    enemy_spawn(g_players[0].x, g_players[0].y, ENEMY_TYPE_WALKER, 0, 0);
    for (f = 0; f < DASH_GRACE + 5; f++) {   /* jump at once, keep B held */
        player_update(&g_players[0], PAD_B, (uint16_t)(f == 0 ? PAD_B : 0));
    }
    CHECK(g_players[0].state == PS_ALIVE && g_players[0].invulnerable == 0,
          "jumping right after a dash next to an enemy should get clear before the grace time ends");
    enemies_clear();

    /* the dash pops bubbles in a band taller than the body */
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = ENERGY_MAX;
    g_players[0].x = TO_POS(60);
    g_bubbles[0] = make_trapped(100, (uint8_t)(PLAYER_SPAWN_Y - 16));
    g_bubbles[0].state = BS_FLOAT;
    g_bubbles[0].trapped = 0;
    player_update(&g_players[0], PAD_R, PAD_R);
    for (f = 0; f < DASH_FRAMES; f++) {
        player_update(&g_players[0], 0, 0);
    }
    CHECK(g_bubbles[0].state == BS_POPPING, "a dash under a bubble should pop it");

    /* a full bar changes nothing about shooting: Y fires on the press
     * (the special shot that fired on release is gone) */
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = ENERGY_MAX;
    player_update(&g_players[0], PAD_Y, PAD_Y);
    CHECK(g_bubbles[0].state == BS_SHOOT && g_players[0].energy == ENERGY_MAX,
          "with a full bar, Y blows a normal bubble at once and keeps the energy");
    g_opt.plus = 0;
}

/* Options live in SRAM. */
static void test_options(void)
{
    game_init(1);
    g_opt.plus = 1;
    g_opt.rescue = 1;
    g_opt.p2_cpu = 1;
    options_save();
    g_opt.plus = 0;
    g_opt.rescue = 0;
    g_opt.p2_cpu = 0;
    game_init(1);
    CHECK(g_opt.plus && g_opt.rescue && g_opt.p2_cpu, "options should survive in SRAM");
    CHECK(game_sram_dirty(), "saving options marks SRAM dirty");
    g_opt.plus = 0;
    g_opt.rescue = 0;
    g_opt.p2_cpu = 0;
    options_save();
}

/* Player 2 as CPU: joins with player 1, a human takes over with Start. */
static void test_cpu_partner(void)
{
    uint16_t pad[NUM_PLAYERS] = { PAD_START, 0 };
    int f;

    game_init(1);
    g_opt.p2_cpu = 1;
    game_tick(pad);
    CHECK(g_players[1].state != PS_OFF && g_players[1].cpu, "the CPU partner should join with player 1");
    pad[0] = 0;
    for (f = 0; f < 300; f++) {
        game_tick(pad);
    }
    pad[1] = PAD_START;
    game_tick(pad);
    CHECK(!g_players[1].cpu && g_state == GS_PLAY, "Start on controller 2 hands player 2 to a human (no pause)");
    for (f = 0; f < 10; f++) {
        game_tick(pad);          /* a real press holds Start for several frames */
    }
    CHECK(g_state == GS_PLAY, "holding Start after the takeover must not pause (state %d)", g_state);

    /* The CPU partner is out of lives: Start on controller 2 is a human joining. */
    pad[1] = 0;
    game_tick(pad);
    g_players[1].cpu = 1;
    g_players[1].state = PS_OFF;
    g_players[1].lives = 0;
    pad[1] = PAD_START;
    game_tick(pad);
    CHECK(g_players[1].state != PS_OFF && !g_players[1].cpu,
          "Start on controller 2 after the CPU is out lets a human join (state %d, cpu %d)",
          g_players[1].state, g_players[1].cpu);
    g_opt.p2_cpu = 0;
}

/* A round change carries nothing over: a death in progress costs the life,
 * a dash in progress ends, the rescue bubble opens. */
static void test_round_change_resets(void)
{
    setup_two_players(0);
    g_players[0].state = PS_DYING;
    g_players[0].timer = 10;
    g_players[0].powers = 1;
    g_players[1].dash = 5;
    g_players[1].stun = 7;
    player_enter_round(&g_players[0]);
    player_enter_round(&g_players[1]);
    CHECK(g_players[0].lives == 2 && g_players[0].state == PS_ENTERING && g_players[0].powers == 0,
          "a dying player loses the life at a round change (lives %u, state %d)", g_players[0].lives, g_players[0].state);
    CHECK(g_players[1].dash == 0 && g_players[1].stun == 0, "dash and stun end at a round change");

    setup_two_players(0);
    g_players[0].state = PS_DYING;
    g_players[0].lives = 1;
    player_enter_round(&g_players[0]);
    CHECK(g_players[0].state == PS_OFF, "a dying player on the last life is out after a round change");

    setup_two_players(0);
    g_players[0].state = PS_RESCUE;
    g_players[0].rescue = 50;
    player_enter_round(&g_players[0]);
    CHECK(g_players[0].lives == 3 && g_players[0].state == PS_ENTERING,
          "the rescue bubble opens at a round change, no life lost");
}

/* Plus mode push bubble (docs/brainstorm/special-shot_2026-11-14.md). */
static Bubble *push_setup(int bubble_dy, uint8_t kind)
{
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].x = TO_POS(60);
    g_players[0].y = TO_POS(FIELD_Y + 80);
    g_players[0].facing_left = 0;
    bubble_spawn_special(kind, 0, TO_POS(100), (uint16_t)TO_POS(FIELD_Y + 80 + bubble_dy));
    return &g_bubbles[0];
}

static void fly_push(void)
{
    int f;
    player_update(&g_players[0], PAD_X, PAD_X);
    for (f = 0; f < 30 && push_active(0); f++) {
        bubbles_update();
    }
}

static void test_push_bubble(void)
{
    Bubble *b;
    int f, i, food = 0;

    /* where it hits decides the shove */
    b = push_setup(0, BK_NORMAL);
    fly_push();
    CHECK(b->state == BS_FLOAT && b->vx > 0 && b->vy == 0, "a hit in the middle pushes along the shot (vx %d vy %d)",
          b->vx, b->vy);
    for (f = 0; f < 90; f++) {
        bubbles_update();
    }
    CHECK(PX(b->x) >= 130 && b->vx == 0, "the shoved bubble slides about 30+ px and stops (x %u, vx %d)", PX(b->x), b->vx);

    b = push_setup(7, BK_NORMAL);   /* the push bubble meets its upper third */
    fly_push();
    CHECK(b->vy > 0 && b->vx == 0, "a hit in the upper third pushes down (vx %d vy %d)", b->vx, b->vy);
    b = push_setup(-7, BK_NORMAL);  /* lower third */
    fly_push();
    CHECK(b->vy < 0 && b->vx == 0, "a hit in the lower third pushes up (vx %d vy %d)", b->vx, b->vy);

    /* a trapped enemy can be shoved too, and stays trapped */
    b = push_setup(0, BK_NORMAL);
    b->state = BS_TRAPPED;
    b->trapped = 1;
    fly_push();
    CHECK(b->state == BS_TRAPPED && b->vx > 0, "a bubble with an enemy inside is shoved and keeps it");

    /* a shoved empty bubble traps the first enemy it slides into */
    b = push_setup(0, BK_NORMAL);
    enemy_spawn(TO_POS(120), TO_POS(FIELD_Y + 80), ENEMY_TYPE_WALKER, 0, 0);
    fly_push();
    for (f = 0; f < 30 && b->state == BS_FLOAT; f++) {
        bubbles_update();
    }
    CHECK(b->state == BS_TRAPPED && !g_enemies[0].active && b->vx == 0,
          "a shoved empty bubble traps the enemy it hits and stops (state %d)", b->state);

    /* a shoved bubble that already holds an enemy does not take a second */
    b = push_setup(0, BK_NORMAL);
    b->state = BS_TRAPPED;
    b->trapped = 1;
    enemy_spawn(TO_POS(120), TO_POS(FIELD_Y + 80), ENEMY_TYPE_WALKER, 0, 0);
    fly_push();
    for (f = 0; f < 30; f++) {
        bubbles_update();
    }
    CHECK(g_enemies[0].active, "a full bubble slides past an enemy");

    /* The combo shot (found in play): Y, then X shortly after. The faster
     * push bubble reaches the normal one just as it stops flying and shoves
     * it on, so the bubble goes much further. Too quick (X 1-2 frames after
     * Y) fails: the push bubble overtakes the bubble while it still flies and
     * cannot be pushed. Measured window: X 3+ frames after Y. It depends on
     * SHOOT_SPEED/SHOOT_FRAMES against PUSH_SPEED/PUSH_FRAMES in bubble.c;
     * a tuning change must not lose it. Bit k = X pressed k+1 frames after Y. */
    {
        int k, window = 0;
        for (k = 0; k <= 12; k++) {
            uint16_t x_float = 0;
            setup_room(0);
            g_opt.plus = 1;
            g_players[0].x = TO_POS(40);
            g_players[0].facing_left = 0;
            for (f = 0; f < 60; f++) {
                uint16_t pad = (uint16_t)(f == 0 ? PAD_Y : f == k + 1 ? PAD_X : 0);
                player_update(&g_players[0], pad, pad);
                bubbles_update();
                if (!x_float && g_bubbles[0].state == BS_FLOAT) {
                    x_float = PX(g_bubbles[0].x);
                }
            }
            if (x_float && PX(g_bubbles[0].x) >= x_float + 20) {
                window |= 1 << k;
            }
        }
        CHECK((window & 0x01FC) == 0x01FC, "the combo shot works with X 3-9 frames after Y (mask 0x%04X)", window);
        CHECK((window & 0x0003) == 0, "X 1-2 frames after Y overtakes the flying bubble (mask 0x%04X)", window);
    }

    /* a wall stops a sliding bubble */
    b = push_setup(0, BK_NORMAL);
    b->x = TO_POS(220);
    b->vx = FX(3, 0);
    for (f = 0; f < 20; f++) {
        bubbles_update();
    }
    CHECK(b->vx == 0, "a wall stops the slide (vx %d)", b->vx);

    /* special bubbles are set off as if touched */
    b = push_setup(0, BK_WATER);
    fly_push();
    CHECK(b->state != BS_FLOAT, "a special bubble is set off by the push bubble");

    /* enemy shots are destroyed, the bubble behind is not reached */
    b = push_setup(0, BK_NORMAL);
    item_spawn_shot(SHOT_ROCK, TO_POS(84), TO_POS(FIELD_Y + 80), -1);
    fly_push();
    for (i = 0; i < MAX_ITEMS; i++) {
        CHECK(g_items[i].kind != IT_ROCK, "the push bubble destroys an enemy shot");
    }
    CHECK(b->vx == 0, "the push bubble bursts on the shot");

    /* an enemy: the push bubble bursts, nothing happens */
    b = push_setup(0, BK_NORMAL);
    enemy_spawn(TO_POS(84), TO_POS(FIELD_Y + 80), ENEMY_TYPE_WALKER, 0, 0);
    fly_push();
    CHECK(g_enemies[0].active && b->vx == 0 && !push_active(0), "an enemy stops the push bubble, unharmed");

    /* food is not collected, it flies on to the bubble */
    b = push_setup(0, BK_NORMAL);
    item_spawn(IT_FOOD, TO_POS(84), TO_POS(FIELD_Y + 80), 0);
    fly_push();
    for (i = 0; i < MAX_ITEMS; i++) {
        food += g_items[i].kind == IT_FOOD;
    }
    CHECK(food == 1 && b->vx > 0, "the push bubble collects nothing (food %d, vx %d)", food, b->vx);

    /* one at a time; nothing in Arcade mode */
    push_setup(0, BK_NORMAL);
    player_update(&g_players[0], PAD_X, PAD_X);
    CHECK(push_active(0) && !push_shoot(0, g_players[0].x, g_players[0].y, AIR_RIGHT), "only one push bubble at a time");
    push_setup(0, BK_NORMAL);
    g_opt.plus = 0;
    player_update(&g_players[0], PAD_X, PAD_X);
    CHECK(!push_active(0), "no push bubble in Arcade mode");

    /* through the whole game: X during play shows the push bubble */
    {
        uint16_t pad[NUM_PLAYERS] = { PAD_START, 0 };
        const Sprite *oam;
        int drawn = 0;
        for (i = 0; i < SRAM_SIZE; i++) {
            game_sram()[i] = 0;
        }
        game_init(5);
        game_debug_options(1, 0, 0);
        game_tick(pad);
        pad[0] = 0;
        for (f = 0; f < 600 && g_state != GS_PLAY; f++) {
            game_tick(pad);
        }
        g_players[0].invulnerable = 200;
        pad[0] = PAD_X;
        game_tick(pad);
        oam = game_oam();
        for (i = 0; i < MAX_SPRITES; i++) {
            drawn |= oam[i].y < SCREEN_H && oam[i].tile == SPR_PUSH;
        }
        CHECK(g_state == GS_PLAY && push_active(0) && drawn, "X in a Plus game blows a visible push bubble (state %d)",
              g_state);
        game_debug_options(0, 0, 0);
    }
    g_opt.plus = 0;
}

/* Plus mode vacuum (docs/brainstorm/vacuum_2026-11-14.md). */
static void vacuum_setup(void)
{
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].x = TO_POS(100);
    g_players[0].y = TO_POS(PLAYER_SPAWN_Y);
}

static void test_vacuum(void)
{
    Bubble *near_b, *far_b;
    uint16_t cx, cy, x0;
    int f, r, tx;
    fx8_8 step_far, step_near;

    /* A places it where the player stands; bubbles inside the circle gather
     * at its centre, bubbles outside keep following the room's air */
    vacuum_setup();
    player_update(&g_players[0], PAD_A, PAD_A);
    cx = g_players[0].x;
    cy = g_players[0].y;
    CHECK(vacuum_active(0), "A starts a vacuum in Plus mode");
    bubble_spawn_special(BK_NORMAL, 0, (uint16_t)(cx + TO_POS(30)), (uint16_t)(cy - TO_POS(20)));
    bubble_spawn_special(BK_NORMAL, 0, (uint16_t)(cx - TO_POS(80)), cy);
    near_b = &g_bubbles[0];
    far_b = &g_bubbles[1];
    g_players[0].x = TO_POS(30);   /* the vacuum stays where it was placed */
    for (f = 0; f < 60; f++) {
        bubbles_update();
    }
    CHECK(PX(near_b->x) == PX(cx) && PX(near_b->y) == PX(cy), "a bubble in the circle reaches the centre (%u,%u -> %u,%u)",
          PX(near_b->x), PX(near_b->y), PX(cx), PX(cy));
    CHECK(PX(cx) - PX(far_b->x) >= 60, "a bubble outside the circle is not drawn in (%d px away)", PX(cx) - PX(far_b->x));
    for (f = 0; f < 29; f++) {
        bubbles_update();
    }
    CHECK(vacuum_active(0), "the vacuum lasts 90 ticks");
    bubbles_update();
    CHECK(!vacuum_active(0), "the vacuum ends after 1.5 s");

    /* the circle is shown as a colour window, and what it shows is exactly
     * what pulls: 39 px to the side is inside, 42 px is not */
    {
        static ColorWindow w[NUM_WINDOWS];
        int y, cxp, cyp, empty_above = 1;
        for (f = 0; f < NUM_WINDOWS; f++) {
            w[f].add_r = w[f].add_g = w[f].add_b = 0;
            for (y = 0; y < SCREEN_H; y++) {
                w[f].left[y] = 255;
                w[f].right[y] = 0;
            }
        }
        vacuum_setup();
        g_players[0].y = TO_POS(FIELD_Y + 100);
        vacuum_start(0, g_players[0].x, g_players[0].y);
        vacuums_windows(w);
        cxp = PX(g_players[0].x) + 8;
        cyp = PX(g_players[0].y) + 8;
        for (y = 0; y < cyp - 40; y++) {
            empty_above &= w[0].left[y] > w[0].right[y];
        }
        CHECK(w[0].left[cyp] == cxp - 40 && w[0].right[cyp] == cxp + 40, "the window spans 40 px each side on the middle line");
        CHECK(w[0].left[cyp - 40] == cxp && w[0].right[cyp - 40] == cxp, "the window closes to a point at the top");
        CHECK(empty_above && w[1].add_b == 0, "nothing outside the circle, no second window");
        CHECK(w[0].add_b > 0, "the window adds a colour");

        bubble_spawn_special(BK_NORMAL, 0, (uint16_t)(g_players[0].x + TO_POS(39)), g_players[0].y);
        bubble_spawn_special(BK_NORMAL, 0, (uint16_t)(g_players[0].x - TO_POS(42)), g_players[0].y);
        x0 = g_bubbles[1].x;
        cx = g_bubbles[0].x;
        bubbles_update();
        CHECK(g_bubbles[0].x < cx, "39 px away, inside the shown circle: pulled");
        CHECK(g_bubbles[1].x - x0 < TO_POS(1), "42 px away, outside the shown circle: not pulled");
    }

    /* through the whole game: A in a Plus game reports a window */
    {
        uint16_t pad[NUM_PLAYERS] = { PAD_START, 0 };
        for (f = 0; f < SRAM_SIZE; f++) {
            game_sram()[f] = 0;
        }
        game_init(5);
        game_debug_options(1, 0, 0);
        game_tick(pad);
        pad[0] = 0;
        for (f = 0; f < 600 && g_state != GS_PLAY; f++) {
            game_tick(pad);
        }
        g_players[0].invulnerable = 200;
        pad[0] = PAD_A;
        game_tick(pad);
        CHECK(game_windows()[0].add_b > 0, "A in a Plus game shows the vacuum window");
        CHECK(game_sfx() & (1u << SFX_VACUUM), "placing a vacuum plays its sound");
        pad[0] = 0;
        for (f = 0; f < 100; f++) {
            g_players[0].invulnerable = 200;
            game_tick(pad);
        }
        CHECK(game_windows()[0].add_b == 0, "the window is gone when the vacuum ends");
        game_debug_options(0, 0, 0);
    }

    /* faster the closer to the centre */
    vacuum_setup();
    vacuum_start(0, g_players[0].x, g_players[0].y);
    bubble_spawn_special(BK_NORMAL, 0, (uint16_t)(g_players[0].x + TO_POS(38)), g_players[0].y);
    x0 = g_bubbles[0].x;
    bubbles_update();
    step_far = (fx8_8)(x0 - g_bubbles[0].x);
    g_bubbles[0].x = (uint16_t)(g_players[0].x + TO_POS(12));
    x0 = g_bubbles[0].x;
    bubbles_update();
    step_near = (fx8_8)(x0 - g_bubbles[0].x);
    CHECK(step_near > step_far && step_far > 0, "bubbles speed up near the centre (%d at 38 px, %d at 12 px)", step_far,
          step_near);

    /* a bubble sliding from a push keeps its push */
    vacuum_setup();
    vacuum_start(0, g_players[0].x, g_players[0].y);
    bubble_spawn_special(BK_NORMAL, 0, (uint16_t)(g_players[0].x + TO_POS(20)), g_players[0].y);
    g_bubbles[0].vx = FX(3, 0);
    x0 = g_bubbles[0].x;
    bubbles_update();
    CHECK(g_bubbles[0].x > x0, "a pushed bubble slides on through a vacuum");

    /* nothing in Arcade mode */
    vacuum_setup();
    g_opt.plus = 0;
    player_update(&g_players[0], PAD_A, PAD_A);
    CHECK(!vacuum_active(0), "no vacuum in Arcade mode");

    /* a partner's rescue bubble is drawn in, and freed on reaching the player */
    g_opt.rescue = 1;
    setup_two_players(0);
    g_opt.plus = 1;
    g_players[0].x = TO_POS(100);
    g_players[0].y = TO_POS(PLAYER_SPAWN_Y);
    g_players[1].state = PS_RESCUE;
    g_players[1].rescue = 200;
    g_players[1].x = TO_POS(130);
    g_players[1].y = TO_POS(PLAYER_SPAWN_Y - 20);
    player_update(&g_players[0], PAD_A, PAD_A);
    for (f = 0; f < 60 && g_players[1].state == PS_RESCUE; f++) {
        bubbles_update();
        player_update(&g_players[0], 0, 0);
        player_update(&g_players[1], 0, 0);
    }
    CHECK(g_players[1].state == PS_ALIVE, "the vacuum draws the partner's rescue bubble in (state %d)", g_players[1].state);
    g_opt.rescue = 0;

    /* walls: pass-through by default; the developer switch makes them block.
     * Find a one-tile platform with free space 3 rows above and 2 below. */
    for (r = 5, tx = -1; r < LEVEL_H - 6 && tx < 0; r++) {
        int c;
        for (c = 4; c < LEVEL_W - 4 && tx < 0; c++) {
            int px = c * 8 + 4, k, ok = level_solid_px((int16_t)px, (int16_t)(FIELD_Y + r * 8 + 4));
            for (k = -3; k <= 2 && ok; k++) {
                ok = k == 0 || !level_solid_px((int16_t)px, (int16_t)(FIELD_Y + (r + k) * 8 + 4));
            }
            if (ok) {
                tx = c;
            }
        }
    }
    r--;
    CHECK(tx >= 0, "room 1 should have a thin platform for the wall test");
    if (tx >= 0) {
        uint16_t bx = TO_POS(tx * 8 - 4);                      /* bubble centre on that column */
        uint16_t above = TO_POS(FIELD_Y + (r - 3) * 8 - 4);   /* centre in the row 3 above */
        uint16_t below = TO_POS(FIELD_Y + (r + 2) * 8 - 4);   /* vacuum 2 rows below */
        int blocked_mode;
        for (blocked_mode = 0; blocked_mode < 2; blocked_mode++) {
            vacuum_setup();
            game_debug_set(DBG_VACUUM_WALLS, (uint8_t)blocked_mode);
            vacuum_start(0, bx, below);
            bubble_spawn_special(BK_NORMAL, 0, bx, above);
            for (f = 0; f < 60; f++) {
                bubbles_update();
            }
            if (blocked_mode) {
                CHECK(PX(g_bubbles[0].y) + 8 < FIELD_Y + r * 8, "with the switch, the platform stops the bubble (y %u)",
                      PX(g_bubbles[0].y));
            } else {
                CHECK(g_bubbles[0].y == below, "by default the bubble passes the platform (y %u)", PX(g_bubbles[0].y));
            }
        }
        game_debug_set(DBG_VACUUM_WALLS, 0);
    }
    g_opt.plus = 0;
}

/* Plus mode: shots stop at walls, Up aims both shots, and only a push gets
 * a bubble through a platform. Played out in round 16's start shaft (a
 * pillar to the right, a ledge above, the thrower's ledge the way out). */
static void test_plus_walls_and_up(void)
{
    Player *p = &g_players[0];
    int16_t cx, cy, wall_x, ceil_y, ledge_top;
    int f, i, bursts = 0, live = 0;

    setup_room(15);
    cx = (int16_t)(PX(p->x) + 8);
    cy = (int16_t)(PX(p->y) + 8);
    for (wall_x = cx; wall_x < 240 && !level_solid_px(wall_x, cy); wall_x++) {
    }
    for (ceil_y = cy; ceil_y > FIELD_Y && !level_solid_px(cx, ceil_y); ceil_y--) {
    }
    for (ledge_top = ceil_y; ledge_top > FIELD_Y && level_solid_px(cx, ledge_top); ledge_top--) {
    }
    CHECK(wall_x < 240 && ceil_y > FIELD_Y + 40, "round 16 start: a pillar to the right and a ledge above (%d, %d)",
          wall_x, ceil_y);

    /* sideways: Plus stops in front of the pillar, Arcade passes it */
    for (i = 0; i < 2; i++) {
        setup_room(15);
        g_opt.plus = (uint8_t)(i == 0);
        p->x = TO_POS(wall_x - 8 - 20);
        p->facing_left = 0;
        player_update(p, PAD_Y, PAD_Y);
        for (f = 0; f < 15; f++) {
            bubbles_update();
        }
        if (i == 0) {
            CHECK(g_bubbles[0].state == BS_FLOAT && PX(g_bubbles[0].x) + 8 + 6 <= wall_x,
                  "Plus: a shot stops in front of the wall (x %u, wall %d)", PX(g_bubbles[0].x), wall_x);
        } else {
            CHECK(PX(g_bubbles[0].x) + 8 > wall_x, "Arcade: a shot still passes the wall (x %u)", PX(g_bubbles[0].x));
        }
    }

    /* point-blank into the wall: it pops at once */
    setup_room(15);
    g_opt.plus = 1;
    p->x = TO_POS(wall_x - 8 - 6);
    p->facing_left = 0;
    player_update(p, PAD_Y, PAD_Y);
    for (i = 0; i < MAX_BUBBLES; i++) {
        live += g_bubbles[i].state != BS_OFF;
    }
    for (i = 0; i < MAX_ITEMS; i++) {
        bursts += g_items[i].kind == IT_BURST;
    }
    CHECK(live == 0 && bursts == 1, "Plus: a bubble blown into the wall pops at once (%d bubbles, %d bursts)", live, bursts);

    /* Arcade: Up does not aim */
    setup_room(15);
    g_opt.plus = 0;
    p->facing_left = 0;
    player_update(p, PAD_UP | PAD_Y, PAD_Y);
    CHECK(g_bubbles[0].state == BS_SHOOT && g_bubbles[0].dir == AIR_RIGHT, "Arcade: Up + Y still shoots sideways");

    /* Up + Y: the shot flies up and stops under the ledge */
    setup_room(15);
    g_opt.plus = 1;
    enemy_spawn(p->x, TO_POS(ledge_top - 15), ENEMY_TYPE_WALKER, 0, 0);   /* on the ledge above */
    player_update(p, PAD_UP | PAD_Y, PAD_Y);
    for (f = 0; f < 15 && g_bubbles[0].state == BS_SHOOT; f++) {
        bubbles_update();
    }
    CHECK(g_bubbles[0].state == BS_FLOAT && PX(g_bubbles[0].y) + 8 - 6 > ceil_y,
          "Plus: an upward shot stops under the ledge (y %u, ledge bottom %d)", PX(g_bubbles[0].y), ceil_y);
    CHECK(g_enemies[0].active, "the enemy on the ledge is out of reach of the shot");

    /* ... and Up + push shoves it up through the ledge, into the enemy */
    player_update(p, PAD_UP | PAD_X, PAD_X);
    for (f = 0; f < 40 && g_enemies[0].active; f++) {
        bubbles_update();
    }
    CHECK(!g_enemies[0].active && g_bubbles[0].state == BS_TRAPPED,
          "Plus: shoot up, push up: the bubble goes through the ledge and traps the enemy (y %u)", PX(g_bubbles[0].y));
    g_opt.plus = 0;
}

/* The CPU partner climbs to a platform five rows up (a full jump): it
 * must keep jump held while rising, like a player who wants the full height. */
static void test_cpu_climbs(void)
{
    int f, reached = 0;
    uint16_t last = 0, pad;

    setup_two_players(0);
    g_players[1].cpu = 1;
    g_players[0].x = TO_POS(24);   /* round 1: left platform on row 20 */
    g_players[0].y = TO_POS(FIELD_Y + 20 * 8 - 16);
    g_players[1].x = TO_POS(120);  /* on the floor */
    g_players[1].y = TO_POS(PLAYER_SPAWN_Y);
    cpu_reset();
    for (f = 0; f < 600 && !reached; f++) {
        g_frame++;
        player_update(&g_players[0], 0, 0);
        pad = cpu_pad(&g_players[1]);
        player_update(&g_players[1], pad, (uint16_t)(pad & ~last));
        last = pad;
        reached = g_players[1].on_ground && PX(g_players[1].y) == FIELD_Y + 20 * 8 - 16;
    }
    CHECK(reached, "the CPU partner should jump up to the platform next to player 1 (P2 at %u,%u)",
          PX(g_players[1].x), PX(g_players[1].y));
}

/* Debug switches. */
static void test_debug_switches(void)
{
    setup_room(0);
    game_debug_set(DBG_INVINCIBLE, 1);
    enemy_spawn(g_players[0].x, g_players[0].y, ENEMY_TYPE_WALKER, 0, 0);
    player_update(&g_players[0], 0, 0);
    CHECK(g_players[0].state == PS_ALIVE, "invincible: enemies do no harm");
    game_debug_set(DBG_INVINCIBLE, 0);
    game_debug_set(DBG_INFINITE_LIVES, 1);
    g_players[0].lives = 2;
    player_killed(&g_players[0]);
    CHECK(g_players[0].lives == 2, "infinite lives");
    game_debug_set(DBG_INFINITE_LIVES, 0);
    {
        DebugBox boxes[32];
        CHECK(game_debug_boxes(boxes, 32) >= 2, "hitboxes of the player and the enemy");
    }
}

/* Dash details: through inner walls (never ending inside one), the input
 * buffer and the single dash key. */
static void test_dash_rules(void)
{
    int f;
    uint8_t c, r;

    /* round 13 has a tower (columns 15-16) rising from the floor */
    setup_room(12);
    g_opt.plus = 1;
    g_players[0].energy = ENERGY_MAX;
    g_players[0].x = TO_POS(76);
    player_update(&g_players[0], PAD_R, PAD_R);
    for (f = 0; f < 14; f++) {
        player_update(&g_players[0], 0, 0);
    }
    CHECK(PX(g_players[0].x) + 2 >= 136, "the dash should pass through the tower (x=%u)", PX(g_players[0].x));
    for (c = 0; c < 2; c++) {
        for (r = 0; r < 2; r++) {
            CHECK(!level_solid_px((int16_t)(PX(g_players[0].x) + 2 + c * 11), (int16_t)(PX(g_players[0].y) + 2 + r * 13)),
                  "a dash must not end inside a wall");
        }
    }

    /* input buffer: pressed while not possible, done when it becomes possible */
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = 0;
    g_players[0].x = TO_POS(60);
    player_update(&g_players[0], PAD_R, PAD_R);
    CHECK(g_players[0].dash == 0, "no dash without energy");
    g_players[0].energy = ENERGY_MAX;
    for (f = 0; f < 3; f++) {
        player_update(&g_players[0], 0, 0);
    }
    CHECK(g_players[0].dash > 0, "a dash pressed a few frames early should still happen");

    /* single dash key: L and R together = facing direction when no direction is held */
    setup_room(0);
    g_opt.plus = 1;
    g_players[0].energy = ENERGY_MAX;
    g_players[0].x = TO_POS(120);
    g_players[0].facing_left = 1;
    player_update(&g_players[0], PAD_L | PAD_R, PAD_L | PAD_R);
    CHECK(g_players[0].dash > 0 && g_players[0].dash_dir < 0, "the dash key dashes the way the player faces");
    CHECK(g_players[0].facing_left == 1, "and keeps the facing");
    g_opt.plus = 0;
}

int main(void)
{
    game_debug_skip_story(1);
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
    test_flutter();
    test_hopper();
    test_spitter();
    test_dropper();
    test_story();
    test_music();
    test_special_items();
    test_round_limit();
    test_secret_room();
    test_boss_and_endings();
    test_rescue();
    test_plus_mode();
    test_dash_rules();
    test_options();
    test_cpu_partner();
    test_round_change_resets();
    test_push_bubble();
    test_vacuum();
    test_plus_walls_and_up();
    test_cpu_climbs();
    test_debug_switches();
    test_cpu_soak();
    test_determinism_and_play();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
