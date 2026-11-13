/*
 * Reading GierzahnEditor level files (*.map), shared by the host tools.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>

#include "common/mapfile.h"

#define MAX_FILE 4096

static long read_be32(const unsigned char *p)
{
    unsigned long v = ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
                      ((unsigned long)p[2] << 8) | (unsigned long)p[3];
    return (v & 0x80000000ul) ? -(long)(0xFFFFFFFFul - v) - 1 : (long)v;
}

int mapfile_load(const char *tool, const char *path, MapFile *lv)
{
    unsigned char buf[MAX_FILE];
    size_t size;
    long count, i;
    FILE *f = fopen(path, "rb");

    if (!f) {
        fprintf(stderr, "%s: %s: cannot open file\n", tool, path);
        return 0;
    }
    size = fread(buf, 1, sizeof buf, f);
    fclose(f);

    if (size < 2 * LEVEL_CELLS) {
        fprintf(stderr, "%s: %s: file too short (%u bytes, need at least %d)\n",
                tool, path, (unsigned)size, 2 * LEVEL_CELLS);
        return 0;
    }

    lv->path = path;
    for (i = 0; i < LEVEL_CELLS; i++) {
        unsigned char solid = buf[i];
        unsigned char air = buf[LEVEL_CELLS + i];
        if (air > AIR_RIGHT) {
            fprintf(stderr, "%s: %s: invalid airflow value %u at x=%ld y=%ld\n",
                    tool, path, air, i % LEVEL_W, i / LEVEL_W);
            return 0;
        }
        lv->cells[i] = (uint8_t)((solid ? CELL_SOLID : 0u) | air);
    }

    lv->enemy_count = 0;
    if (size == 2 * LEVEL_CELLS) {
        return 1;   /* old files without enemy layer */
    }
    if (size < 2 * LEVEL_CELLS + 4) {
        fprintf(stderr, "%s: %s: truncated enemy count\n", tool, path);
        return 0;
    }
    count = read_be32(buf + 2 * LEVEL_CELLS);
    if (count < 0 || count > LEVEL_MAX_ENEMIES) {
        fprintf(stderr, "%s: %s: %ld enemies, allowed are 0..%d\n", tool, path, count, LEVEL_MAX_ENEMIES);
        return 0;
    }
    if (size < (size_t)(2 * LEVEL_CELLS + 4 + count * 16)) {
        fprintf(stderr, "%s: %s: truncated enemy list\n", tool, path);
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *p = buf + 2 * LEVEL_CELLS + 4 + i * 16;
        long x = read_be32(p), y = read_be32(p + 4), type = read_be32(p + 8), left = read_be32(p + 12);
        if (x < 0 || x >= LEVEL_W || y < 0 || y >= LEVEL_H) {
            fprintf(stderr, "%s: %s: enemy %ld outside the map (x=%ld y=%ld)\n", tool, path, i, x, y);
            return 0;
        }
        if (type < ENEMY_TYPE_WALKER || type > ENEMY_TYPE_COUNT) {
            fprintf(stderr, "%s: %s: warning: enemy %ld has unknown type %ld, using walker\n", tool, path, i, type);
            type = ENEMY_TYPE_WALKER;
        }
        if (lv->cells[y * LEVEL_W + x] & CELL_SOLID) {
            fprintf(stderr, "%s: %s: warning: enemy %ld spawns inside a solid tile\n", tool, path, i);
        }
        lv->enemies[i].x = (uint8_t)x;
        lv->enemies[i].y = (uint8_t)y;
        lv->enemies[i].type = (uint8_t)type;
        lv->enemies[i].flags = (uint8_t)(left ? LEVEL_ENEMY_FACE_LEFT : 0u);
        lv->enemies[i].delay = 0;
    }
    lv->enemy_count = (int)count;
    return 1;
}

