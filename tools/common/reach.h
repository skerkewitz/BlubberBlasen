#ifndef BB_TOOLS_REACH_H
#define BB_TOOLS_REACH_H

/*
 * Simplified movement model of the game, shared by mapview (--check) and
 * map2c (navigation data for the CPU partner).
 *
 * A position (c, r) means: the 16 px sprite's left edge is at column c (the
 * body covers columns c and c+1), the feet are in row r. Moves: walk one
 * column and fall until standing (wrapping through gaps in the bottom row),
 * or jump up to 5 rows (through one-way platforms) and 4 columns sideways.
 *
 * The plain model is lenient: a sideways jump may pass through an inner
 * wall. The CPU partner's navigation data and the playability check use it.
 * REACH_WALLS makes walls block jumps as in the game, REACH_DASH adds the
 * Plus mode dash (about 7 tiles along a row, through inner walls); mapview
 * uses both to report enemies that only a dash reaches.
 */

#include "common/mapfile.h"

#define REACH_MAX_MOVES 64

int  reach_solid(const MapFile *m, int c, int r);
int  reach_stands(const MapFile *m, int c, int r);
#define REACH_WALLS      1   /* inner walls block sideways jumps */
#define REACH_DASH       2   /* add the Plus mode dash */
#define REACH_DASH_TILES 7

int  reach_moves(const MapFile *m, int c, int r, int out[REACH_MAX_MOVES][2]);
int  reach_moves_ex(const MapFile *m, int c, int r, int out[REACH_MAX_MOVES][2], int flags);
void reach_from_start(const MapFile *m, unsigned char seen[LEVEL_W][LEVEL_H]);
void reach_from_start_ex(const MapFile *m, unsigned char seen[LEVEL_W][LEVEL_H], int flags);

#endif /* BB_TOOLS_REACH_H */
