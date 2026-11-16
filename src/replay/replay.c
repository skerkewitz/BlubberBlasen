/*
 * Recordings of games, see replay.h. File format (text, one entry per line):
 *
 *   BLUBBERBLASEN REPLAY 1
 *   version 1.1.0
 *   seed 4711
 *   sram 0a1b...              save RAM, two hex digits per byte
 *   round 1                   developer options the run started with
 *   skip_story 0
 *   options 1 0 0             plus, rescue, cpu2 (only if set on the command line)
 *   open_options              (only if the run started on the options screen)
 *   ending 1                  (only if the run started with an ending)
 *   switch invincible 1       (one line per switch that was on)
 *   start
 *   0 pad 0000 0000           from here on: <tick> <entry>
 *   0 check 1a2b3c4d
 *   125 pad 8000 0000
 *   300 switch invincible 0
 *   812 spawn 3
 *   900 mark
 *   end 5400
 *
 * Entries of one tick come in the order they happen: developer actions
 * (before the tick), the pads (if changed), the checksum (after the tick).
 */

#include "replay/replay.h"

#include <stdlib.h>
#include <string.h>

#define REPLAY_MAGIC "BLUBBERBLASEN REPLAY 1"

/* Names in the file, so a recording survives new switches in the enum. */
static const char *const switch_names[DBG_COUNT] = {
    "infinite_lives", "invincible", "hitboxes", "air", "cpu_view", "vacuum_walls", "no_sticky"
};

const char *replay_switch_name(uint8_t sw)
{
    return sw < DBG_COUNT ? switch_names[sw] : 0;
}

static int switch_by_name(const char *name)
{
    int i;
    for (i = 0; i < DBG_COUNT; i++) {
        if (strcmp(switch_names[i], name) == 0) {
            return i;
        }
    }
    return -1;
}

void replay_apply_setup(const ReplaySetup *s)
{
    uint8_t k;
    memcpy(game_sram(), s->sram, SRAM_SIZE);
    game_debug_cpu(0);
    game_init(s->seed);
    game_debug_start_round(s->round);
    game_debug_skip_story(s->skip_story);
    for (k = 0; k < DBG_COUNT; k++) {
        game_debug_set(k, s->switches[k]);
    }
    if (s->set_options) {
        game_debug_options(s->plus, s->rescue, s->cpu2);
    }
    if (s->open_options) {
        game_debug_open_options();
    }
    if (s->ending >= 0) {
        game_debug_show_ending((uint8_t)s->ending);
    }
}

/* --- Checksum -------------------------------------------------------------- */
static uint32_t fnv(uint32_t h, uint8_t b)
{
    return (h ^ b) * 16777619u;
}

static uint32_t fnv16(uint32_t h, uint16_t v)
{
    return fnv(fnv(h, (uint8_t)(v & 0xFF)), (uint8_t)(v >> 8));
}

uint32_t replay_hash(void)
{
    uint32_t h = 2166136261u;
    const Sprite *oam = game_oam();
    const ColorWindow *win = game_windows();
    const uint16_t *bg;
    int i, n;

    for (i = 0; i < MAX_SPRITES; i++) {
        h = fnv(fnv(fnv(fnv(h, oam[i].x), oam[i].y), oam[i].tile), oam[i].attr);
    }
    bg = game_bg(0);
    for (i = 0, n = BG_W * BG0_ROWS; i < n; i++) {
        h = fnv16(h, bg[i]);
    }
    bg = game_bg(1);
    for (i = 0, n = BG_W * BG_H; i < n; i++) {
        h = fnv16(h, bg[i]);
    }
    for (n = 0; n < NUM_WINDOWS; n++) {
        if (!(win[n].add_r | win[n].add_g | win[n].add_b)) {
            continue;   /* off: its spans don't matter */
        }
        for (i = 0; i < SCREEN_H; i++) {
            h = fnv(fnv(h, win[n].left[i]), win[n].right[i]);
        }
        h = fnv(fnv(fnv(h, win[n].add_r), win[n].add_g), win[n].add_b);
    }
    h = fnv16(h, game_bg_scroll_y());
    h = fnv(h, game_bg_window_top());
    h = fnv(h, game_music());
    return h;
}

/* --- Writing ------------------------------------------------------------- */
int replay_write_begin(ReplayWriter *w, const char *path, const ReplaySetup *s)
{
    int i;
    memset(w, 0, sizeof *w);
    w->f = fopen(path, "w");
    if (!w->f) {
        return -1;
    }
    w->pad[0] = w->pad[1] = 0xFFFF;   /* the first tick always writes its pads */
    fprintf(w->f, "%s\nversion %s\nseed %u\nsram ", REPLAY_MAGIC, s->version, (unsigned)s->seed);
    for (i = 0; i < SRAM_SIZE; i++) {
        fprintf(w->f, "%02x", s->sram[i]);
    }
    fprintf(w->f, "\nround %u\nskip_story %u\n", (unsigned)s->round, (unsigned)s->skip_story);
    if (s->set_options) {
        fprintf(w->f, "options %u %u %u\n", (unsigned)s->plus, (unsigned)s->rescue, (unsigned)s->cpu2);
    }
    if (s->open_options) {
        fprintf(w->f, "open_options\n");
    }
    if (s->ending >= 0) {
        fprintf(w->f, "ending %d\n", (int)s->ending);
    }
    for (i = 0; i < DBG_COUNT; i++) {
        if (s->switches[i]) {
            fprintf(w->f, "switch %s 1\n", switch_names[i]);
        }
    }
    fprintf(w->f, "start\n");
    fflush(w->f);
    return 0;
}

void replay_write_event(ReplayWriter *w, uint32_t tick, uint8_t kind, uint16_t a, uint16_t b)
{
    if (!w->f) {
        return;
    }
    switch (kind) {
    case RE_SWITCH:
        if (a < DBG_COUNT) {
            fprintf(w->f, "%lu switch %s %u\n", (unsigned long)tick, switch_names[a], (unsigned)(b != 0));
        }
        break;
    case RE_SPAWN:
        fprintf(w->f, "%lu spawn %u\n", (unsigned long)tick, (unsigned)a);
        break;
    case RE_CLEAR:
        fprintf(w->f, "%lu clear\n", (unsigned long)tick);
        break;
    case RE_MARK:
        fprintf(w->f, "%lu mark\n", (unsigned long)tick);
        w->marks++;
        fflush(w->f);   /* the moment that matters is on disk, even if the game crashes next */
        break;
    default:
        break;
    }
}

void replay_write_tick(ReplayWriter *w, uint32_t tick, const uint16_t pad[NUM_PLAYERS])
{
    if (!w->f) {
        return;
    }
    if (pad[0] != w->pad[0] || pad[1] != w->pad[1]) {
        fprintf(w->f, "%lu pad %04x %04x\n", (unsigned long)tick, (unsigned)pad[0], (unsigned)pad[1]);
        w->pad[0] = pad[0];
        w->pad[1] = pad[1];
    }
    if (tick % REPLAY_CHECK_EVERY == 0) {
        fprintf(w->f, "%lu check %08lx\n", (unsigned long)tick, (unsigned long)replay_hash());
        fflush(w->f);   /* about once a second: a crash loses at most that */
    }
}

void replay_write_end(ReplayWriter *w, uint32_t ticks)
{
    if (!w->f) {
        return;
    }
    fprintf(w->f, "end %lu\n", (unsigned long)ticks);
    fclose(w->f);
    w->f = 0;
}

/* --- Reading ------------------------------------------------------------- */
static int add_event(Replay *r, const ReplayEvent *e)
{
    if (r->count == r->cap) {
        uint32_t cap = r->cap ? r->cap * 2 : 1024;
        ReplayEvent *ev = (ReplayEvent *)realloc(r->events, cap * sizeof *ev);
        if (!ev) {
            return -1;
        }
        r->events = ev;
        r->cap = cap;
    }
    r->events[r->count++] = *e;
    return 0;
}

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

#define FAIL(...) do { snprintf(err, err_size, __VA_ARGS__); fclose(f); replay_free(r); return -1; } while (0)

int replay_load(const char *path, Replay *r, char *err, size_t err_size)
{
    char line[600];
    FILE *f;
    int in_body = 0;
    unsigned long line_no = 0, last_tick = 0;

    memset(r, 0, sizeof *r);
    r->setup.ending = -1;
    r->setup.round = 1;
    f = fopen(path, "r");
    if (!f) {
        snprintf(err, err_size, "%s: cannot open file", path);
        return -1;
    }
    if (!fgets(line, sizeof line, f) || strncmp(line, REPLAY_MAGIC, strlen(REPLAY_MAGIC)) != 0) {
        FAIL("%s: not a Blubber Blasen recording", path);
    }
    line_no = 1;
    while (fgets(line, sizeof line, f)) {
        char word[32], name[32];
        unsigned long tick, a, b, c;
        line_no++;
        line[strcspn(line, "\r\n")] = 0;
        if (!line[0]) {
            continue;
        }
        if (!in_body) {
            if (sscanf(line, "version %15s", r->setup.version) == 1) {
            } else if (sscanf(line, "seed %lu", &a) == 1) {
                r->setup.seed = (uint16_t)a;
            } else if (strncmp(line, "sram ", 5) == 0) {
                const char *h = line + 5;
                size_t i, n = strlen(h);
                if (n != SRAM_SIZE * 2) {
                    FAIL("%s:%lu: save RAM has %lu bytes, this game has %d", path, line_no, (unsigned long)(n / 2), SRAM_SIZE);
                }
                for (i = 0; i < SRAM_SIZE; i++) {
                    int hi = hex_digit(h[i * 2]), lo = hex_digit(h[i * 2 + 1]);
                    if (hi < 0 || lo < 0) {
                        FAIL("%s:%lu: bad hex digit in the save RAM", path, line_no);
                    }
                    r->setup.sram[i] = (uint8_t)(hi * 16 + lo);
                }
            } else if (sscanf(line, "round %lu", &a) == 1) {
                r->setup.round = (uint8_t)a;
            } else if (sscanf(line, "skip_story %lu", &a) == 1) {
                r->setup.skip_story = (uint8_t)(a != 0);
            } else if (sscanf(line, "options %lu %lu %lu", &a, &b, &c) == 3) {
                r->setup.set_options = 1;
                r->setup.plus = (uint8_t)(a != 0);
                r->setup.rescue = (uint8_t)(b != 0);
                r->setup.cpu2 = (uint8_t)(c != 0);
            } else if (strcmp(line, "open_options") == 0) {
                r->setup.open_options = 1;
            } else if (sscanf(line, "ending %lu", &a) == 1) {
                r->setup.ending = (int8_t)(a != 0);
            } else if (sscanf(line, "switch %31s %lu", name, &a) == 2) {
                int sw = switch_by_name(name);
                if (sw < 0 && strcmp(name, "full_energy") == 0) {
                    continue;   /* switch of the removed energy bar (before 1.4.0) */
                }
                if (sw < 0) {
                    FAIL("%s:%lu: unknown switch '%s'", path, line_no, name);
                }
                r->setup.switches[sw] = (uint8_t)(a != 0);
            } else if (strcmp(line, "start") == 0) {
                in_body = 1;
            } else {
                FAIL("%s:%lu: unknown setting '%s'", path, line_no, line);
            }
            continue;
        }
        if (sscanf(line, "end %lu", &a) == 1) {
            r->end_tick = (uint32_t)a;
            break;
        }
        if (sscanf(line, "%lu %31s", &tick, word) != 2) {
            FAIL("%s:%lu: cannot read '%s'", path, line_no, line);
        }
        if (tick < last_tick) {
            FAIL("%s:%lu: tick %lu comes after tick %lu", path, line_no, tick, last_tick);
        }
        last_tick = tick;
        {
            ReplayEvent e;
            memset(&e, 0, sizeof e);
            e.tick = (uint32_t)tick;
            if (strcmp(word, "pad") == 0 && sscanf(line, "%*u pad %lx %lx", &a, &b) == 2) {
                e.kind = RE_PAD;
                e.a = (uint16_t)a;
                e.b = (uint16_t)b;
            } else if (strcmp(word, "switch") == 0 && sscanf(line, "%*u switch %31s %lu", name, &a) == 2) {
                int sw = switch_by_name(name);
                if (sw < 0 && strcmp(name, "full_energy") == 0) {
                    continue;   /* switch of the removed energy bar (before 1.4.0) */
                }
                if (sw < 0) {
                    FAIL("%s:%lu: unknown switch '%s'", path, line_no, name);
                }
                e.kind = RE_SWITCH;
                e.a = (uint16_t)sw;
                e.b = (uint16_t)(a != 0);
            } else if (strcmp(word, "spawn") == 0 && sscanf(line, "%*u spawn %lu", &a) == 1) {
                e.kind = RE_SPAWN;
                e.a = (uint16_t)a;
            } else if (strcmp(word, "clear") == 0) {
                e.kind = RE_CLEAR;
            } else if (strcmp(word, "mark") == 0) {
                e.kind = RE_MARK;
            } else if (strcmp(word, "check") == 0 && sscanf(line, "%*u check %lx", &a) == 1) {
                e.kind = RE_CHECK;
                e.value = (uint32_t)a;
            } else {
                FAIL("%s:%lu: cannot read '%s'", path, line_no, line);
            }
            if (add_event(r, &e) != 0) {
                FAIL("%s: out of memory", path);
            }
        }
    }
    fclose(f);
    if (!in_body) {
        snprintf(err, err_size, "%s: no 'start' line", path);
        replay_free(r);
        return -1;
    }
    if (!r->end_tick) {
        /* the game ended without closing the file (a crash): play what is there */
        r->end_tick = r->count ? r->events[r->count - 1].tick + 1 : 0;
    }
    return 0;
}

void replay_free(Replay *r)
{
    free(r->events);
    r->events = 0;
    r->count = r->cap = 0;
}

/* --- Playing ------------------------------------------------------------- */
void replay_play_begin(ReplayPlayer *p, const Replay *r)
{
    memset(p, 0, sizeof *p);
    p->r = r;
    replay_apply_setup(&r->setup);
}

void replay_before_tick(ReplayPlayer *p, uint32_t tick, uint16_t pad[NUM_PLAYERS])
{
    const Replay *r = p->r;
    p->marked = 0;
    while (p->next < r->count && r->events[p->next].tick <= tick && r->events[p->next].kind != RE_CHECK) {
        const ReplayEvent *e = &r->events[p->next++];
        switch (e->kind) {
        case RE_PAD:    p->pad[0] = e->a; p->pad[1] = e->b; break;
        case RE_SWITCH: game_debug_set((uint8_t)e->a, (uint8_t)e->b); break;
        case RE_SPAWN:  game_debug_spawn((uint8_t)e->a); break;
        case RE_CLEAR:  game_debug_clear_round(); break;
        case RE_MARK:   p->marked = 1; break;
        default: break;
        }
    }
    pad[0] = p->pad[0];
    pad[1] = p->pad[1];
}

int replay_after_tick(ReplayPlayer *p, uint32_t tick)
{
    const Replay *r = p->r;
    int bad = 0;
    while (p->next < r->count && r->events[p->next].tick <= tick && r->events[p->next].kind == RE_CHECK) {
        if (r->events[p->next].value != replay_hash() && !p->desynced) {
            p->desynced = 1;
            p->desync_tick = tick;
            bad = 1;
        }
        p->next++;
    }
    return bad;
}

int replay_run(const Replay *r, uint32_t *ticks, uint32_t *desync_tick)
{
    ReplayPlayer p;
    uint32_t t;
    replay_play_begin(&p, r);
    for (t = 0; t < r->end_tick; t++) {
        uint16_t pad[NUM_PLAYERS];
        replay_before_tick(&p, t, pad);
        game_tick(pad);
        if (replay_after_tick(&p, t)) {
            t++;   /* this tick was played */
            break;
        }
    }
    *ticks = t;
    *desync_tick = p.desync_tick;
    return p.desynced;
}
