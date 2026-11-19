/*
 * Text on background layer 1 and decimal score arithmetic.
 * Scores are kept as 8 decimal digits so they can be shown and compared
 * without any division (the 65816 has none).
 */

#include "game/internal.h"

void bg_clear(uint8_t layer)
{
    uint16_t i;
    if (layer == 0) {
        for (i = 0; i < BG_W * BG0_ROWS; i++) {
            g_bg0[i] = 0;
        }
    } else {
        for (i = 0; i < BG_W * BG_H; i++) {
            g_bg1[i] = 0;
        }
    }
    bg_mark_all(layer);
}

static uint16_t char_tile(char c)
{
    uint8_t ch = (uint8_t)c;
    if (ch >= 'a' && ch <= 'z') {
        ch = (uint8_t)(ch - ('a' - 'A'));
    }
    if (ch < 32 || ch > 95) {
        return 0;
    }
    return (uint16_t)(CHR_FONT_BASE + (uint16_t)(ch - 32));
}

/* A row is marked for the upload to VRAM only when an entry really changes:
 * the HUD writes its numbers every frame, and on the SNES translating and
 * copying unchanged rows cost a third of the frame. Pointers, not dst[i]:
 * 816-tcc multiplies for every index into an array of 16-bit entries. */
void text_put(uint8_t col, uint8_t row, const char *s, uint8_t pal)
{
    uint16_t *dst = &g_bg1[((uint16_t)row << 5) + col];
    uint8_t changed = 0;
    while (*s && col < BG_W) {
        uint16_t t = char_tile(*s++);
        uint16_t v = t ? BG_ENTRY(t, pal) : 0u;
        if (*dst != v) {
            *dst = v;
            changed = 1;
        }
        dst++;
        col++;
    }
    if (changed) {
        bg_mark_row(1, row);
    }
}

void text_clear_row(uint8_t row)
{
    uint16_t *dst = &g_bg1[(uint16_t)row << 5];
    uint8_t i, changed = 0;
    for (i = 0; i < BG_W; i++, dst++) {
        if (*dst) {
            *dst = 0;
            changed = 1;
        }
    }
    if (changed) {
        bg_mark_row(1, row);
    }
}

/* Digits without leading zeros; the last digit is always shown. */
void text_digits(uint8_t col, uint8_t row, const uint8_t *digits, uint8_t n, uint8_t pal)
{
    uint16_t *dst = &g_bg1[((uint16_t)row << 5) + col];
    uint8_t leading = 1, changed = 0;
    uint8_t i;

    for (i = 0; i < n; i++, dst++) {
        uint8_t d = digits[i];
        uint16_t v;
        if (d != 0 || i == n - 1) {
            leading = 0;
        }
        v = leading ? 0u : BG_ENTRY(CHR_FONT_BASE + 16u + d, pal);   /* '0' + d */
        if (*dst != v) {
            *dst = v;
            changed = 1;
        }
    }
    if (changed) {
        bg_mark_row(1, row);
    }
}

/* Two digits, or three from 100 on (round 100). */
void text_number(uint8_t col, uint8_t row, uint8_t value, uint8_t pal)
{
    uint8_t d[3];
    d[0] = 0;
    d[1] = 0;
    while (value >= 100) {
        value = (uint8_t)(value - 100);
        d[0]++;
    }
    while (value >= 10) {
        value = (uint8_t)(value - 10);
        d[1]++;
    }
    d[2] = value;
    if (d[0]) {
        text_digits(col, row, d, 3, pal);
    } else {
        text_digits(col, row, d + 1, 2, pal);
    }
}

/* Add `digit` at decimal position `pos` (0 = ones). Saturates at 99999999. */
void score_add(uint8_t *score, uint8_t pos, uint8_t digit)
{
    int8_t i = (int8_t)(7 - pos);

    g_hud_changes++;

    score[i] = (uint8_t)(score[i] + digit);
    while (score[i] > 9) {
        if (i == 0) {
            for (i = 0; i < 8; i++) {
                score[i] = 9;
            }
            return;
        }
        score[i] = (uint8_t)(score[i] - 10);
        i--;
        score[i]++;
    }
}

int8_t score_cmp(const uint8_t *a, const uint8_t *b)
{
    uint8_t i;
    for (i = 0; i < 8; i++) {
        if (a[i] != b[i]) {
            return (int8_t)(a[i] < b[i] ? -1 : 1);
        }
    }
    return 0;
}
