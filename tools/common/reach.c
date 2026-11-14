/*
 * Simplified movement model of the game (see reach.h).
 */

#include <string.h>

#include "common/reach.h"

int reach_solid(const MapFile *m, int c, int r)
{
    if (c < 0 || c >= LEVEL_W) {
        return 1;
    }
    if (r < 0 || r >= LEVEL_H) {
        return 0;
    }
    return (m->cells[r * LEVEL_W + c] & CELL_SOLID) != 0;
}

/* Like the game: the top border only counts at the side walls. */
static int ground(const MapFile *m, int c, int r)
{
    if (r == 0 && c >= 2 && c < LEVEL_W - 2) {
        return 0;
    }
    return reach_solid(m, c, r);
}

int reach_stands(const MapFile *m, int c, int r)
{
    if (c < 2 || c > LEVEL_W - 4 || r < 0 || r > LEVEL_H - 2) {
        return 0;
    }
    if (ground(m, c, r) || ground(m, c + 1, r)) {
        return 0;
    }
    return ground(m, c, r + 1) || ground(m, c + 1, r + 1);
}

static int fall(const MapFile *m, int c, int *r)
{
    int n, y = *r;
    for (n = 0; n < 3 * LEVEL_H; n++) {
        if (reach_stands(m, c, y)) {
            *r = y;
            return 1;
        }
        y++;
        if (y > LEVEL_H - 2) {
            if (reach_solid(m, c, LEVEL_H - 1) && reach_solid(m, c + 1, LEVEL_H - 1)) {
                return 0;
            }
            y = 0;   /* fell through a gap: comes back in at the top */
        }
    }
    return 0;
}

int reach_moves(const MapFile *m, int c, int r, int out[REACH_MAX_MOVES][2])
{
    int n = 0, d, nc, nr;
    for (d = -1; d <= 1; d += 2) {
        nc = c + d;
        nr = r;
        if (nc >= 2 && nc <= LEVEL_W - 4 && !reach_solid(m, nc, r) && !reach_solid(m, nc + 1, r) &&
            fall(m, nc, &nr) && n < REACH_MAX_MOVES) {
            out[n][0] = nc;
            out[n][1] = nr;
            n++;
        }
    }
    for (nr = r - 5 < 0 ? 0 : r - 5; nr <= r; nr++) {
        for (nc = c - 4 < 2 ? 2 : c - 4; nc <= c + 4 && nc <= LEVEL_W - 4; nc++) {
            if (reach_stands(m, nc, nr) && n < REACH_MAX_MOVES) {
                out[n][0] = nc;
                out[n][1] = nr;
                n++;
            }
        }
    }
    return n;
}

void reach_from_start(const MapFile *m, unsigned char seen[LEVEL_W][LEVEL_H])
{
    static int stack[LEVEL_W * LEVEL_H][2];
    int moves[REACH_MAX_MOVES][2];
    int sp = 0, i, n;

    memset(seen, 0, LEVEL_W * LEVEL_H);
    if (!reach_stands(m, 3, LEVEL_H - 2)) {
        return;
    }
    seen[3][LEVEL_H - 2] = 1;
    stack[sp][0] = 3;
    stack[sp][1] = LEVEL_H - 2;
    sp++;
    while (sp > 0) {
        sp--;
        n = reach_moves(m, stack[sp][0], stack[sp][1], moves);
        for (i = 0; i < n; i++) {
            int c = moves[i][0], r = moves[i][1];
            if (!seen[c][r]) {
                seen[c][r] = 1;
                stack[sp][0] = c;
                stack[sp][1] = r;
                sp++;
            }
        }
    }
}
