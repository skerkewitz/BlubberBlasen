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

    CHECK(level_count > 0, "no levels compiled in");
    for (l = 0; l < level_count; l++) {
        const LevelDef *d = &level_defs[l];
        CHECK(d->enemy_count > 0 && d->enemy_count <= LEVEL_MAX_ENEMIES, "level %u enemy count %u", l + 1, d->enemy_count);
        for (i = 0; i < LEVEL_CELLS; i++) {
            CHECK((d->cells[i] & CELL_AIR_MASK) <= AIR_RIGHT, "level %u bad airflow", l + 1);
        }
        for (i = 0; i < LEVEL_H; i++) {
            CHECK(d->cells[i * LEVEL_W] & CELL_SOLID, "level %u: left wall open at row %u", l + 1, i);
        }
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
    if ((frame % 97) < 2 && g_state != GS_PLAY) {
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
                CHECK(oam[i].tile < 64, "frame %lu: sprite %d uses unknown cell %u", (unsigned long)f, i, oam[i].tile);
            }
        }
        for (l = 0; l < BG_LAYERS; l++) {
            const uint16_t *bg = game_bg((uint8_t)l);
            for (i = 0; i < BG_W * BG_H; i += 7) {
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
            if (g_player.state == PS_ALIVE) {
                uint8_t cx = (uint8_t)(PX(g_player.x) + 8);
                CHECK(cx >= BODY_MIN_CX && cx <= BODY_MAX_CX, "frame %lu: player outside walls", (unsigned long)f);
            }
        }
        if (trace && (f % trace) == 0) {
            printf("f=%lu st=%d rd=%u lvl=%u P(st=%u x=%u y=%u vy=%d g=%u) E:", (unsigned long)f, (int)g_state, g_round,
                   g_level, g_player.state, PX(g_player.x), PX(g_player.y), g_player.vy, g_player.on_ground);
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
    CHECK(a.sfx_seen & (1u << SFX_DEATH), "player never died");
    CHECK(a.sfx_seen & (1u << SFX_GAMEOVER), "never reached game over");
    CHECK(a.max_round >= 2, "never cleared a round (max round %u)", a.max_round);

    printf("bot: %lu games, best round %u, sfx mask 0x%04lX\n",
           (unsigned long)a.games, a.max_round, (unsigned long)a.sfx_seen);
}

int main(void)
{
    const char *t = getenv("BB_TRACE");
    trace = t ? strtoul(t, 0, 10) : 0;
    test_score();
    test_levels();
    test_determinism_and_play();

    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
