/*
 * High score table, kept in battery-backed save RAM (SRAM) like on a
 * cartridge. The platform loads the bytes before game_init() and saves
 * them when game_sram_dirty() reports a change.
 *
 * Layout: 4 bytes magic, then 5 entries of 12 bytes:
 *   name[3] (ASCII), round, score[8] (decimal digits, most significant first)
 */

#include "game/internal.h"

#define ENTRY_SIZE 12
#define TABLE_OFS  4
#define OPTIONS_OFS 64   /* 4 bytes magic "OPT1", then plus, rescue, p2_cpu */

Options g_opt;

static uint8_t sram[SRAM_SIZE];
static uint8_t sram_dirty;

static const uint8_t magic[4] = { 'B', 'B', 'H', '1' };

static uint8_t *entry(uint8_t rank)
{
    return &sram[TABLE_OFS + (rank << 3) + (rank << 2)];   /* rank * ENTRY_SIZE */
}

uint8_t *game_sram(void)
{
    return sram;
}

uint8_t game_sram_dirty(void)
{
    uint8_t d = sram_dirty;
    sram_dirty = 0;
    return d;
}

void hiscore_init(void)
{
    static const char names[HISCORE_COUNT][3] = {
        { 'B', 'L', 'U' }, { 'B', 'B', 'E' }, { 'R', 'B', 'L' }, { 'A', 'S', 'E' }, { 'N', '!', '!' }
    };
    uint8_t i;

    for (i = 0; i < 4; i++) {
        if (sram[i] != magic[i]) {
            break;
        }
    }
    if (i == 4) {
        return;   /* valid table */
    }

    for (i = 0; i < 4; i++) {
        sram[i] = magic[i];
    }
    for (i = 0; i < HISCORE_COUNT; i++) {
        uint8_t *e = entry(i);
        e[0] = (uint8_t)names[i][0];
        e[1] = (uint8_t)names[i][1];
        e[2] = (uint8_t)names[i][2];
        e[3] = (uint8_t)(HISCORE_COUNT - i);
        for (uint8_t k = 0; k < 8; k++) {
            e[4 + k] = 0;
        }
        e[4 + 3] = (uint8_t)(HISCORE_COUNT - i);   /* 50000, 40000, ... */
    }
    g_hud_changes++;
}

static const uint8_t opt_magic[4] = { 'O', 'P', 'T', '1' };

void options_load(void)
{
    for (uint8_t i = 0; i < 4; i++) {
        if (sram[OPTIONS_OFS + i] != opt_magic[i]) {
            g_opt.plus = 0;
            g_opt.rescue = 0;
            g_opt.p2_cpu = 0;
            return;
        }
    }
    g_opt.plus = (uint8_t)(sram[OPTIONS_OFS + 4] != 0);
    g_opt.rescue = (uint8_t)(sram[OPTIONS_OFS + 5] != 0);
    g_opt.p2_cpu = (uint8_t)(sram[OPTIONS_OFS + 6] != 0);
}

void options_save(void)
{
    for (uint8_t i = 0; i < 4; i++) {
        sram[OPTIONS_OFS + i] = opt_magic[i];
    }
    sram[OPTIONS_OFS + 4] = g_opt.plus;
    sram[OPTIONS_OFS + 5] = g_opt.rescue;
    sram[OPTIONS_OFS + 6] = g_opt.p2_cpu;
    sram_dirty = 1;
}

const uint8_t *hiscore_best(void)
{
    return entry(0) + 4;
}

uint8_t hiscore_rank(const uint8_t *score)
{
    for (uint8_t i = 0; i < HISCORE_COUNT; i++) {
        if (score_cmp(score, entry(i) + 4) > 0) {
            return i;
        }
    }
    return HISCORE_COUNT;
}

void hiscore_insert(uint8_t rank, const char *name, const uint8_t *score, uint8_t round)
{
    if (rank >= HISCORE_COUNT) {
        return;
    }
    for (uint8_t i = HISCORE_COUNT - 1; i > rank; i--) {
        uint8_t *dst = entry(i);
        const uint8_t *src = entry((uint8_t)(i - 1));
        for (uint8_t k = 0; k < ENTRY_SIZE; k++) {
            dst[k] = src[k];
        }
    }
    {
        uint8_t *e = entry(rank);
        e[0] = (uint8_t)name[0];
        e[1] = (uint8_t)name[1];
        e[2] = (uint8_t)name[2];
        e[3] = round;
        for (uint8_t k = 0; k < 8; k++) {
            e[4 + k] = score[k];
        }
    }
    g_hud_changes++;
    sram_dirty = 1;
}

void hiscore_draw_table(uint8_t first_row, uint8_t highlight)
{
    static const char *const ranks[HISCORE_COUNT] = { "1ST", "2ND", "3RD", "4TH", "5TH" };

    text_put(4, first_row, "RANK   SCORE  RD  NAME", PAL_CYAN);
    for (uint8_t i = 0; i < HISCORE_COUNT; i++) {
        const uint8_t *e = entry(i);
        uint8_t row = (uint8_t)(first_row + 2 + (i << 1));
        uint8_t pal = (uint8_t)(i == highlight ? PAL_YELLOW : PAL_NONE);
        char name[4];

        name[0] = (char)e[0];
        name[1] = (char)e[1];
        name[2] = (char)e[2];
        name[3] = 0;
        text_put(4, row, ranks[i], pal);
        text_digits(9, row, e + 4, 8, pal);
        text_number(19, row, e[3], pal);
        text_put(23, row, name, pal);
    }
}
