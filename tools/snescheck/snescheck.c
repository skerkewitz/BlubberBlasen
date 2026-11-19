/*
 * snescheck: plays the game on Rachael (the kit's SNES in C) as the kit's main loop plays it on
 * the SNES (lib/snes/main.c: game_init(0), no save RAM, no debug switches), with a simple bot on
 * pad 1, and writes the session as a recording in the kit's check format: the pad per tick and
 * the checksum of every tick (sdevkit_dbg_hash: the sprite table and the tilemaps).
 *
 *   snescheck [--ticks N] -o build/snes/check.txt
 *   <home>/dev/replay.sh build/snes/blubberblasen.sfc build/snes/check.txt
 *
 * replay.sh plays the same pads on the ROM in dampf's emulator and names the first tick whose
 * checksum differs: dampf's code and gcc's (or MSVC's), the kit's lib/snes and Rachael must
 * compute the same game. Host code; the game is linked without BB_DEBUG, as on the SNES.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/game.h"
#include "rachael.h"

static const struct { const char *name; uint16_t bit; } BUTTONS[] = {
    { "b", PAD_B }, { "y", PAD_Y }, { "select", PAD_SELECT }, { "start", PAD_START },
    { "up", PAD_UP }, { "down", PAD_DOWN }, { "left", PAD_LEFT }, { "right", PAD_RIGHT },
    { "a", PAD_A }, { "x", PAD_X }, { "l", PAD_L }, { "r", PAD_R },
};
#define NBUTTONS (sizeof BUTTONS / sizeof BUTTONS[0])

/* The frontend's --autoplay bot, with its own random numbers so that every run is the same. */
static uint16_t rng = 0x1234;
static uint16_t bot_pad(long t)
{
    static uint16_t dir = PAD_RIGHT;
    uint16_t pad;
    if ((t % 180) == 0) {
        rng = (uint16_t)(rng * 25173u + 13849u);
        dir = (uint16_t)((rng & 0x100) ? PAD_LEFT : PAD_RIGHT);
    }
    pad = dir;
    if ((t % 20) < 2)   pad |= PAD_Y;
    if ((t % 90) < 10)  pad |= PAD_B;
    if ((t % 1200) < 2) pad |= PAD_START;   /* title, name entry */
    return pad;
}

int main(int argc, char **argv)
{
    long ticks = 3600, t, start;
    const char *out = NULL;
    uint16_t *pads;
    uint32_t *hashes;
    FILE *f;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            ticks = strtol(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else {
            out = NULL;
            break;
        }
    }
    if (out == NULL || ticks <= 0) {
        fprintf(stderr, "usage: snescheck [--ticks N] -o check.txt\n");
        return 2;
    }
    pads = calloc((size_t)ticks, sizeof pads[0]);
    hashes = calloc((size_t)ticks, sizeof hashes[0]);
    if (pads == NULL || hashes == NULL) {
        fprintf(stderr, "snescheck: out of memory\n");
        return 1;
    }

    sdevkit__reset();
    game_init(0);
    for (t = 0; t < ticks; t++) {
        uint16_t pad[NUM_PLAYERS];
        pad[0] = bot_pad(t);
        pad[1] = 0;
        pads[t] = pad[0];
        game_tick(pad);
        hashes[t] = sdevkit__tick_hash();
        sdevkit__vblank();
        game_vblank();
        sdevkit__display();
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
