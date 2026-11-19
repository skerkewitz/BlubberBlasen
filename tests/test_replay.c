/*
 * Recordings (src/replay): a recorded run must replay to the same game, and
 * a replay that drifts apart must be noticed.
 *
 *   test_replay                 unit tests
 *   test_replay <file.bbr>...   replays each recording and fails if one no
 *                               longer matches the game (tests/replays: the
 *                               game changed since it was recorded; record
 *                               it again or delete it)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "replay/replay.h"
#include "sdevkit/snes.h"

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define TMP_FILE "test_replay_tmp.bbr"
#define RUN_TICKS 4000u

static uint32_t lcg = 12345;

static uint16_t random_pad(void)
{
    static const uint16_t moves[] = {
        0, PAD_LEFT, PAD_RIGHT, PAD_LEFT | PAD_B, PAD_RIGHT | PAD_B, PAD_Y, PAD_LEFT | PAD_Y,
        PAD_RIGHT | PAD_Y, PAD_X, PAD_A, PAD_UP | PAD_Y, PAD_B
    };
    lcg = lcg * 1103515245u + 12345u;
    return moves[(lcg >> 16) % (sizeof moves / sizeof moves[0])];
}

/* Plays a Plus game with changing input and developer actions, recording it.
 * Returns the checksum at the end. */
static uint32_t record_run(const char *path)
{
    ReplaySetup s;
    ReplayWriter w;
    uint16_t pad[NUM_PLAYERS] = { 0, 0 };
    uint32_t t;
    uint8_t went = 0;

    memset(&s, 0, sizeof s);
    snprintf(s.version, sizeof s.version, "%s", GAME_VERSION);
    s.seed = 777;
    memcpy(s.sram, sdevkit_sram(), SRAM_SIZE);
    s.round = 3;
    s.skip_story = 1;
    s.set_options = 1;
    s.plus = 1;
    s.ending = -1;
    s.switches[DBG_INFINITE_LIVES] = 1;

    replay_apply_setup(&s);
    CHECK(replay_write_begin(&w, path, &s) == 0, "cannot write %s", path);
    for (t = 0; t < RUN_TICKS; t++) {
        if (t == 1500) {
            game_debug_set(DBG_INVINCIBLE, 1);
            replay_write_event(&w, t, RE_SWITCH, DBG_INVINCIBLE, 1);
        }
        if (t == 1800) {
            game_debug_spawn(10);
            replay_write_event(&w, t, RE_SPAWN, 10, 0);
            replay_write_event(&w, t, RE_MARK, 0, 0);
        }
        if (t >= 2500 && !went && game_debug_goto_round(7)) {
            /* the debug view's round picker: recorded like the other actions */
            replay_write_event(&w, t, RE_ROUND, 7, 0);
            went = 1;
        }
        if (t < 200) {
            pad[0] = (uint16_t)((t % 40) < 2 ? PAD_START : 0);   /* through the title */
        } else if (t % 12 == 0) {
            pad[0] = random_pad();
            pad[1] = (uint16_t)(t % 600 == 0 ? PAD_START : random_pad());   /* player 2 joins too */
        }
        game_tick(pad);
        replay_write_tick(&w, t, pad);
    }
    replay_write_end(&w, RUN_TICKS);
    CHECK(went, "the run never got to go to round 7");
    return replay_hash();
}

static void test_round_trip(void)
{
    Replay r;
    char err[256];
    uint32_t end_hash = record_run(TMP_FILE), ticks = 0, desync = 0;
    uint32_t i, marks = 0, checks = 0, rounds = 0;

    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) == 0, "%s", err);
    CHECK(r.end_tick == RUN_TICKS, "end tick %lu", (unsigned long)r.end_tick);
    CHECK(r.setup.seed == 777 && r.setup.round == 3 && r.setup.plus && r.setup.set_options, "setup read back");
    CHECK(r.setup.switches[DBG_INFINITE_LIVES] && !r.setup.switches[DBG_INVINCIBLE], "switches read back");
    CHECK(strcmp(r.setup.version, GAME_VERSION) == 0, "version '%s'", r.setup.version);
    for (i = 0; i < r.count; i++) {
        marks += r.events[i].kind == RE_MARK;
        checks += r.events[i].kind == RE_CHECK;
        rounds += r.events[i].kind == RE_ROUND && r.events[i].a == 7;
    }
    CHECK(marks == 1, "one mark, got %lu", (unsigned long)marks);
    CHECK(rounds == 1, "one jump to round 7, got %lu", (unsigned long)rounds);
    CHECK(checks == (RUN_TICKS + REPLAY_CHECK_EVERY - 1) / REPLAY_CHECK_EVERY, "a checksum every %d ticks, got %lu",
          REPLAY_CHECK_EVERY, (unsigned long)checks);

    /* the same game again, from the file */
    CHECK(replay_run(&r, &ticks, &desync) == 0, "replay drifted apart at tick %lu", (unsigned long)desync);
    CHECK(ticks == RUN_TICKS, "played %lu ticks", (unsigned long)ticks);
    CHECK(replay_hash() == end_hash, "the replay ends on a different picture");

    /* twice in a row works too (nothing left over from the last game) */
    CHECK(replay_run(&r, &ticks, &desync) == 0, "second replay drifted apart at tick %lu", (unsigned long)desync);

    /* change one input in the middle: the replay must notice */
    for (i = 0; i < r.count; i++) {
        if (r.events[i].kind == RE_PAD && r.events[i].tick > 1000) {
            r.events[i].a = (uint16_t)(r.events[i].a == (PAD_LEFT | PAD_B) ? PAD_RIGHT | PAD_B : PAD_LEFT | PAD_B);
            break;
        }
    }
    CHECK(i < r.count, "a pad change after tick 1000");
    CHECK(replay_run(&r, &ticks, &desync) == 1, "a changed input must be noticed");
    CHECK(desync >= r.events[i].tick && desync < r.events[i].tick + 4 * REPLAY_CHECK_EVERY,
          "noticed at tick %lu, input changed at %lu", (unsigned long)desync, (unsigned long)r.events[i].tick);
    CHECK(ticks == desync + 1, "playing stops at the mismatch");
    replay_free(&r);

    /* a different seed is a different game */
    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) == 0, "%s", err);
    r.setup.seed = 778;
    CHECK(replay_run(&r, &ticks, &desync) == 1, "a different seed must be noticed");
    replay_free(&r);
}

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void test_bad_files(void)
{
    Replay r;
    char err[256];

    write_file(TMP_FILE, "hello\n");
    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) != 0 && strstr(err, "not a Blubber Blasen recording"), "magic: %s", err);
    write_file(TMP_FILE, "BLUBBERBLASEN REPLAY 1\nswitch flying 1\nstart\n");
    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) != 0 && strstr(err, ":2: unknown switch 'flying'"), "switch: %s", err);
    write_file(TMP_FILE, "BLUBBERBLASEN REPLAY 1\nsram 00\nstart\n");
    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) != 0 && strstr(err, "save RAM has 1 bytes"), "sram: %s", err);
    write_file(TMP_FILE, "BLUBBERBLASEN REPLAY 1\nstart\n5 pad 0000 0000\n3 pad 0100 0000\n");
    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) != 0 && strstr(err, ":4: tick 3 comes after tick 5"), "order: %s", err);
    write_file(TMP_FILE, "BLUBBERBLASEN REPLAY 1\nstart\n0 pad 0000 0000\n0 check 0\n40 pad 0100 0000\n");
    CHECK(replay_load(TMP_FILE, &r, err, sizeof err) == 0 && r.end_tick == 41, "a file cut off by a crash still plays: %s", err);
    replay_free(&r);
    remove(TMP_FILE);
}

static int check_recordings(int argc, char **argv)
{
    int i;
    for (i = 1; i < argc; i++) {
        Replay r;
        char err[256];
        uint32_t ticks, desync;
        if (replay_load(argv[i], &r, err, sizeof err) != 0) {
            printf("FAIL %s\n", err);
            failures++;
            continue;
        }
        if (replay_run(&r, &ticks, &desync)) {
            printf("FAIL %s (recorded with %s): drifted apart at tick %lu of %lu; the game changed since,"
                   " record it again\n", argv[i], r.setup.version, (unsigned long)desync, (unsigned long)r.end_tick);
            failures++;
        } else {
            printf("ok   %s: %lu ticks\n", argv[i], (unsigned long)ticks);
        }
        replay_free(&r);
    }
    return failures ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc > 1) {
        return check_recordings(argc, argv);
    }
    test_round_trip();
    test_bad_files();
    if (failures) {
        printf("%d check(s) failed\n", failures);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
