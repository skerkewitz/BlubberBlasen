/*
 * Platform layer for the SNES (ROADMAP.md, stage 3): the counterpart of
 * src/platform/platform_raylib.c, built with PVSnesLib (816-tcc). See
 * build.sh.
 *
 * The frame rhythm is the console's:
 *   - game_tick() runs while the picture is drawn. It only prepares: the
 *     sprite shadow, HDMA tables, what VRAM and CGRAM need (src/game/video.c).
 *   - Then the main loop waits for the vertical blank. Its interrupt copies
 *     the sprite shadow into OAM (PVSnesLib) and calls vblank() below, which
 *     calls game_vblank() for the uploads, but only once per finished tick:
 *     when a tick takes longer than a frame, the vertical blank in between
 *     finds no new frame and leaves everything as it is.
 * No sound yet (ADR-0005, point 6).
 *
 * CPU meter (METER, on for the stage 3 spike): the picture is drawn darker
 * while game_tick() runs and at full brightness again when it is done, so
 * the dark band at the top shows the share of the frame the game logic
 * takes. A picture that stays dark and a game that stutters: a tick takes
 * longer than a frame.
 */

#include <snes.h>

#include "game/game.h"

void snes_clear_ram(void);   /* glue.asm */
void consoleVblank(void);    /* PVSnesLib's own vertical blank routine (joypads) */

#define METER 1

static u8 frame_ready;

static void vblank(void)
{
    if (frame_ready) {
        frame_ready = 0;
        game_vblank();
    }
    consoleVblank();
}

int main(void)
{
    u16 pad[NUM_PLAYERS];

    snes_clear_ram();   /* first: C's zeroes for every variable (see glue.asm) */
    consoleInit();
    game_init(0x2B1Du);
    nmiSet(vblank);
    for (;;) {
        pad[0] = padsCurrent(0);
        pad[1] = padsCurrent(1);
#if METER
        setBrightness(8);
#endif
        game_tick(pad);
#if METER
        setBrightness(15);
#endif
        frame_ready = 1;
        WaitForVBlank();
    }
    return 0;
}
