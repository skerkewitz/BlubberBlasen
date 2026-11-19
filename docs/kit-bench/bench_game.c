/* The benchmark as a superdevkit game, for measuring: tick t loads frame (t / 5) % 32 and runs
 * case t % 5 on it: 0 only the loading, 1 to 4 the variants A to D. No checksums (test_main
 * checks the results); the work per tick of a case minus that of case 0 is the variant's. */
#include "sdevkit/loop.h"
#include "../separate.c"

static uint16_t tick;

void game_init(uint16_t seed)
{
    (void)seed;
}

void game_tick(const uint16_t pad[SDEVKIT_PADS])
{
    uint8_t c = (uint8_t)(tick % 5);
    uint8_t f = (uint8_t)((tick / 5) & (FRAMES - 1));
    (void)pad;
    if (c == 0 || c == 1 || c == 2) {
        load_struct(f);
    } else {
        load_fields(f);
    }
    switch (c) {
    case 1: sep_counter_struct(); break;
    case 2: sep_list_struct(); break;
    case 3: sep_counter_fields(); break;
    case 4: sep_list_fields(); break;
    default: break;
    }
    tick++;
}

void game_vblank(void)
{
}
