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

/* The body (columns c0..c1+1, rows y-1 and y) can move sideways along
 * row y: inner walls block it, as level_wall_ahead does in the game. */
static int span_free(const MapFile *m, int c0, int c1, int y)
{
    int k, lo = c0 < c1 ? c0 : c1, hi = c0 < c1 ? c1 : c0;
    for (k = lo; k <= hi + 1; k++) {
        if (reach_solid(m, k, y) || reach_solid(m, k, y - 1)) {
            return 0;
        }
    }
    return 1;
}

/* A jump from (c, r) lands on (nc, nr): rise in place (platforms are
 * one-way), move sideways at some height up to 5 rows above r, then drop
 * to nr. The sideways part must not pass through a wall. */
static int jump_path(const MapFile *m, int c, int r, int nc, int nr)
{
    int y, k;
    for (y = r - 5 < 1 ? 1 : r - 5; y <= nr; y++) {
        int clear = 1;
        if (!span_free(m, c, nc, y)) {
            continue;
        }
        for (k = y; k < nr && clear; k++) {   /* the drop at nc down to nr */
            clear = !reach_solid(m, nc, k) && !reach_solid(m, nc + 1, k);
        }
        if (clear) {
            return 1;
        }
    }
    return 0;
}

int reach_moves_ex(const MapFile *m, int c, int r, int out[REACH_MAX_MOVES][2], int flags)
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
            if (reach_stands(m, nc, nr) && n < REACH_MAX_MOVES &&
                (!(flags & REACH_WALLS) || nc == c || jump_path(m, c, r, nc, nr))) {
                out[n][0] = nc;
                out[n][1] = nr;
                n++;
            }
        }
    }
    if (flags & REACH_DASH) {
        /* Plus mode dash: about 7 tiles along the row, through inner walls */
        for (d = 1; d <= REACH_DASH_TILES; d++) {
            if (reach_stands(m, c - d, r) && n < REACH_MAX_MOVES) {
                out[n][0] = c - d;
                out[n][1] = r;
                n++;
            }
            if (reach_stands(m, c + d, r) && n < REACH_MAX_MOVES) {
                out[n][0] = c + d;
                out[n][1] = r;
                n++;
            }
        }
    }
    return n;
}

int reach_moves(const MapFile *m, int c, int r, int out[REACH_MAX_MOVES][2])
{
    return reach_moves_ex(m, c, r, out, 0);
}

void reach_from_start_ex(const MapFile *m, unsigned char seen[LEVEL_W][LEVEL_H], int flags)
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
        n = reach_moves_ex(m, stack[sp][0], stack[sp][1], moves, flags);
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

void reach_from_start(const MapFile *m, unsigned char seen[LEVEL_W][LEVEL_H])
{
    reach_from_start_ex(m, seen, 0);
}
