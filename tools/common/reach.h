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
 */

#include "common/mapfile.h"

#define REACH_MAX_MOVES 64

int  reach_solid(const MapFile *m, int c, int r);
int  reach_stands(const MapFile *m, int c, int r);
int  reach_moves(const MapFile *m, int c, int r, int out[REACH_MAX_MOVES][2]);
void reach_from_start(const MapFile *m, unsigned char seen[LEVEL_W][LEVEL_H]);

#endif /* BB_TOOLS_REACH_H */
