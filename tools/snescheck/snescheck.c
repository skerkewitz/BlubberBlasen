/*
 * snescheck: plays the game on Rachael (the kit's SNES in C) as the kit's main loop plays it on
 * the SNES (lib/snes/main.c: game_init(0), no save RAM, no debug switches), with a simple bot on
 * pad 1, and writes the session as a recording in the kit's check format: the pad per tick and
 * the checksum of every tick (sdevkit_dbg_hash: the sprite table and the tilemaps).
 *
 *   snescheck [--ticks N] [--cpu] [--trace FILE] [--sound FILE] -o build/snes/check.txt
 *   <home>/dev/replay.sh build/snes/blubberblasen.sfc build/snes/check.txt
 *
 * replay.sh plays the same pads on the ROM in dampf's emulator and names the first tick whose
 * checksum differs: dampf's code and gcc's (or MSVC's), the kit's lib/snes and Rachael must
 * compute the same game. Host code; the game is linked without BB_DEBUG, as on the SNES.
 *
 * The bot only presses buttons, so the ROM, which gets the same pads, plays the same game; it may
 * look at the game's state to decide. On the title it sets the options first (--cpu: player 2 is
 * the CPU partner; co-op rescue stays at its default, on), then starts a game, and it continues
 * after a game over.
 * In play it walks, jumps and shoots, places a vacuum every 5 seconds, and never presses Start,
 * which would pause.
 *
 * --trace FILE writes one line per tick, "tick state round bubbles enemies items vacuums"
 * (GameState, the round, the bubbles, enemies and items in use, bit i: player i's vacuum is on), to put a ROM's measurements per tick in
 * context.
 *
 * --sound FILE writes every write to Rachael's sound chip (the S-DSP) as the kit's desktop
 * frontend writes sound.txt: "init RR VV" for those of game_init, then "aram H" (the hash of the
 * bytes uploaded to the audio RAM), then "T RR VV" for those after tick T. dampf's emulator
 * records the same of the ROM (dampf.emu.ExampleCheck), so the two can be compared line by line.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "rachael.h"
#include "game/internal.h"

static const struct { const char *name; uint16_t bit; } BUTTONS[] = {
    { "b", PAD_B }, { "y", PAD_Y }, { "select", PAD_SELECT }, { "start", PAD_START },
    { "up", PAD_UP }, { "down", PAD_DOWN }, { "left", PAD_LEFT }, { "right", PAD_RIGHT },
    { "a", PAD_A }, { "x", PAD_X }, { "l", PAD_L }, { "r", PAD_R },
};
#define NBUTTONS (sizeof BUTTONS / sizeof BUTTONS[0])

static uint8_t want_cpu;

static FILE *sound;                        /* --sound */
static long sound_tick = -1;               /* the tick a DSP write belongs to; -1: game_init */

static void log_dsp(uint8_t reg, uint8_t value)
{
    if (sound_tick < 0) {
        fprintf(sound, "init %02x %02x\n", reg, value);
    } else {
        fprintf(sound, "%ld %02x %02x\n", sound_tick, reg, value);
    }
}

/* A button pressed for 2 ticks, then released for 4: the menus react to a new press only. */
static uint16_t tap(long t, uint16_t button)
{
    return (uint16_t)((t % 6) < 2 ? button : 0);
}

/* The bot: in play the frontend's --autoplay pattern (with its own random numbers, so that every
 * run is the same), around it the menus. */
static uint16_t rng = 0x1234;
static uint16_t bot_pad(long t)
{
    static uint16_t dir = PAD_RIGHT;
    static uint8_t option_row;       /* where the options screen's cursor is */
    uint16_t pad;
    switch (g_state) {
    case GS_PLAY:
        break;
    case GS_TITLE:
        if (g_opt.p2_cpu != want_cpu) {
            option_row = 0;
            return tap(t, PAD_SELECT);
        }
        return tap(t, PAD_START);
    case GS_OPTIONS:
        if ((t % 6) != 0) {
            return 0;                /* one press every 6 ticks, released in between */
        }
        if (g_opt.p2_cpu != want_cpu) {
            if (option_row == 1) return PAD_RIGHT;
            option_row = (uint8_t)(option_row + 1);
            return PAD_DOWN;
        }
        return PAD_START;
    default:                         /* story, round skip, continue, name entry, high scores */
        return tap(t, PAD_START);
    }
    if ((t % 180) == 0) {
        rng = (uint16_t)(rng * 25173u + 13849u);
        dir = (uint16_t)((rng & 0x100) ? PAD_LEFT : PAD_RIGHT);
    }
    pad = dir;
    if ((t % 20) < 2)   pad |= PAD_Y;
    if ((t % 90) < 10)  pad |= PAD_B;
    if ((t % 300) < 2) pad |= PAD_A;   /* a vacuum */
    return pad;
}

int main(int argc, char **argv)
{
    long ticks = 3600, t, start;
    const char *out = NULL, *trace_path = NULL, *sound_path = NULL;
    FILE *trace = NULL;
    uint16_t *pads;
    uint32_t *hashes;
    FILE *f;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            ticks = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--cpu") == 0) {
            want_cpu = 1;
        } else if (strcmp(argv[i], "--sound") == 0 && i + 1 < argc) {
            sound_path = argv[++i];
        } else if (strcmp(argv[i], "--trace") == 0 && i + 1 < argc) {
            trace_path = argv[++i];
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else {
            out = NULL;
            break;
        }
    }
    if (out == NULL || ticks <= 0) {
        fprintf(stderr, "usage: snescheck [--ticks N] [--cpu] [--trace FILE] [--sound FILE] -o check.txt\n");
        return 2;
    }
    pads = calloc((size_t)ticks, sizeof pads[0]);
    hashes = calloc((size_t)ticks, sizeof hashes[0]);
    if (pads == NULL || hashes == NULL) {
        fprintf(stderr, "snescheck: out of memory\n");
        return 1;
    }

    if (trace_path != NULL && (trace = fopen(trace_path, "w")) == NULL) {
        fprintf(stderr, "snescheck: cannot write %s\n", trace_path);
        return 1;
    }
    sdevkit__reset();
    if (sound_path != NULL) {
        sound = fopen(sound_path, "w");
        if (sound == NULL) {
            fprintf(stderr, "snescheck: cannot write %s\n", sound_path);
            return 1;
        }
        sdevkit__sound.log = log_dsp;
    }
    game_init(0);
    if (sound != NULL) {
        fprintf(sound, "aram %08x\n", (unsigned)sdevkit__sound.upload_hash);
    }
    for (t = 0; t < ticks; t++) {
        uint16_t pad[NUM_PLAYERS];
        pad[0] = bot_pad(t);
        pad[1] = 0;
        pads[t] = pad[0];
        game_tick(pad);
        hashes[t] = sdevkit__tick_hash();
        if (trace != NULL) {
            int b = 0, e = 0, it = 0, k;
            for (k = 0; k < MAX_BUBBLES; k++) b += g_bubbles[k].state != BS_OFF;
            for (k = 0; k < MAX_ENEMIES; k++) e += g_enemies[k].active != 0;
            for (k = 0; k < MAX_ITEMS; k++) it += g_items[k].kind != IT_OFF;
            fprintf(trace, "%ld %d %d %d %d %d %d\n", t, (int)g_state, (int)g_round, b, e, it,
                    vacuum_active(0) | (vacuum_active(1) << 1));
        }
        sdevkit__vblank();
        game_vblank();
        sound_tick = t;
        sdevkit__sfx_frame();   /* as lib/snes/main.c: the tick's sound effects start */
        sdevkit__display();
    }

    if (trace != NULL) {
        fclose(trace);
    }
    if (sound != NULL) {
        fclose(sound);
    }
    f = fopen(out, "w");
    if (f == NULL) {
        fprintf(stderr, "snescheck: cannot write %s\n", out);
        return 1;
    }
    fprintf(f, "# Blubber Blasen on Rachael (tools/snescheck): pad 1 and the checksum of every tick\n");
    fprintf(f, "ticks %ld\n", ticks);
    for (start = 0; start < ticks; start = t) {
        uint16_t p = pads[start];
        for (t = start + 1; t < ticks && pads[t] == p; t++) {
        }
        if (p != 0) {
            size_t b;
            fprintf(f, "press %ld %ld", start, t);
            for (b = 0; b < NBUTTONS; b++) {
                if (p & BUTTONS[b].bit) {
                    fprintf(f, " %s", BUTTONS[b].name);
                }
            }
            fprintf(f, "\n");
        }
    }
    for (t = 0; t < ticks; t++) {
        fprintf(f, "hash %ld %08x\n", t, (unsigned)hashes[t]);
    }
    fclose(f);
    printf("snescheck: %ld ticks, %s\n", ticks, out);
    return 0;
}
