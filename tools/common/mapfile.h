#ifndef BB_TOOLS_MAPFILE_H
#define BB_TOOLS_MAPFILE_H

/*
 * Reading GierzahnEditor level files (*.map), shared by the host tools.
 *
 * Format (big endian, as written by the old Java editor):
 *   832 bytes  static layer, 32 x 26, row-major; 0 = empty, non-zero = solid
 *   832 bytes  airflow layer; 0 = none, 1 = up, 2 = down, 3 = left, 4 = right
 *   optional:  int32 enemy count, then per enemy int32 x, y, type, lookingLeft
 */

#include "game/level_data.h"

typedef struct {
    const char *path;
    uint8_t     cells[LEVEL_CELLS];        /* CELL_SOLID | airflow, as in the game */
    LevelEnemy  enemies[LEVEL_MAX_ENEMIES];
    int         enemy_count;
} MapFile;

/* Returns 1 on success; on failure prints "<tool>: <path>: <message>" to stderr. */
int mapfile_load(const char *tool, const char *path, MapFile *m);

#endif /* BB_TOOLS_MAPFILE_H */
