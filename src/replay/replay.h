#ifndef BB_REPLAY_H
#define BB_REPLAY_H

/*
 * Recording and replaying games (development builds, PC only).
 *
 * The game is deterministic: the same start (seed, save RAM, developer
 * options) and the same joypad words give the same game. A recording stores
 * exactly that, nothing of the game's state:
 *   - a header with the game version, the seed, the save RAM and the
 *     developer options the run started with;
 *   - per tick, the two joypad words, written only when they change;
 *   - developer actions that change the game (F2 invincible, F10 spawn, ...)
 *     at the tick they happened;
 *   - "mark" lines: the player pressed the mark key ("that was weird!");
 *   - every CHECK_EVERY ticks a checksum of the game's output (sprite table,
 *     tilemaps, scroll, windows), so a replay that drifts apart, because the
 *     game changed since the recording, is noticed at once.
 *
 * The file is plain text, one line per entry (see replay.c), so it can be
 * read and diffed. This module is host code: it uses stdio and malloc and is
 * never part of the SNES game.
 */

#include <stdint.h>
#include <stdio.h>

#include "game/game.h"

#define REPLAY_CHECK_EVERY 60   /* ticks between checksums */

/* How a run starts: everything besides the joypads that shapes the game. */
typedef struct {
    char     version[16];       /* GAME_VERSION of the recording */
    uint16_t seed;
    uint8_t  sram[SRAM_SIZE];
    uint8_t  round;             /* game_debug_start_round */
    uint8_t  skip_story;
    uint8_t  set_options, plus, rescue, cpu2;   /* game_debug_options, if set_options */
    uint8_t  open_options;
    int8_t   ending;            /* -1 none, 0 sad, 1 happy */
    uint8_t  switches[DBG_COUNT];
} ReplaySetup;

enum {
    RE_PAD,        /* a = pad 1, b = pad 2, from this tick on */
    RE_SWITCH,     /* a = debug switch, b = on */
    RE_SPAWN,      /* a = what (game_debug_spawn) */
    RE_CLEAR,      /* game_debug_clear_round */
    RE_MARK,       /* the player marked this moment */
    RE_CHECK       /* value = replay_hash() after this tick */
};

typedef struct {
    uint32_t tick;
    uint8_t  kind;
    uint16_t a, b;
    uint32_t value;
} ReplayEvent;

typedef struct {
    ReplaySetup  setup;
    ReplayEvent *events;
    uint32_t     count, cap;
    uint32_t     end_tick;      /* ticks recorded; 0 if the file has no end line */
} Replay;

/* Starts a game as described (fills the save RAM, game_init, options). */
void     replay_apply_setup(const ReplaySetup *s);

/* Checksum of what the game shows now (FNV-1a over its output). */
uint32_t replay_hash(void);

/* --- Writing ------------------------------------------------------------- */
typedef struct {
    FILE    *f;
    uint16_t pad[NUM_PLAYERS];
    uint32_t marks;
} ReplayWriter;

int  replay_write_begin(ReplayWriter *w, const char *path, const ReplaySetup *s);   /* 0 = ok */
/* Before game_tick(tick): a developer action (RE_SWITCH, RE_SPAWN, RE_CLEAR, RE_MARK). */
void replay_write_event(ReplayWriter *w, uint32_t tick, uint8_t kind, uint16_t a, uint16_t b);
/* After game_tick(tick) ran with these pads. */
void replay_write_tick(ReplayWriter *w, uint32_t tick, const uint16_t pad[NUM_PLAYERS]);
void replay_write_end(ReplayWriter *w, uint32_t ticks);

/* --- Reading and playing ------------------------------------------------- */
/* Returns 0 on success; otherwise writes a message into err. */
int  replay_load(const char *path, Replay *r, char *err, size_t err_size);
void replay_free(Replay *r);

typedef struct {
    const Replay *r;
    uint32_t next;              /* index of the next event */
    uint16_t pad[NUM_PLAYERS];  /* current joypad words */
    uint32_t desync_tick;       /* first tick whose checksum did not match (if desynced) */
    uint8_t  desynced;
    uint8_t  marked;            /* set by replay_before_tick when this tick has a mark */
} ReplayPlayer;

void replay_play_begin(ReplayPlayer *p, const Replay *r);   /* also applies the setup */
/* Applies the developer actions of this tick and returns the pads for it. */
void replay_before_tick(ReplayPlayer *p, uint32_t tick, uint16_t pad[NUM_PLAYERS]);
/* After game_tick(tick): compares the checksum. Returns 0 if it matched (or
 * there was none), 1 on the first mismatch. */
int  replay_after_tick(ReplayPlayer *p, uint32_t tick);

/* Plays a whole recording without a window. Returns 1 if it drifted apart
 * (*desync_tick: the first tick whose checksum did not match), else 0.
 * *ticks: the ticks played. */
int      replay_run(const Replay *r, uint32_t *ticks, uint32_t *desync_tick);

/* Name of a debug switch in the file ("invincible"), or 0. */
const char *replay_switch_name(uint8_t sw);

#endif /* BB_REPLAY_H */
