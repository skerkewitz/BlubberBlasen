/*
 * map2c - convert GierzahnEditor level files (*.map) plus their optional
 * per-round settings files (*.txt) into a C source file with const level
 * data ("ROM") for the game.
 *
 * Usage: map2c -o <output.c> <level1.map> [<level2.map> ...]
 *
 * For every <name>.map, map2c reads <name>.txt if it exists:
 *   hurry   <seconds>         time until HURRY UP, 0 = never (default 30)
 *   tileset <1..8>            room graphics (default: round number, repeating)
 *   delay   <frames> ...      spawn delay per enemy in .map order, 0..255
 *   special <type> <seconds>  special bubbles: water, fire or lightning, every n seconds
 *   # comment
 *
 * Input format (big endian, as written by the old Java editor):
 *   832 bytes  static layer, 32 x 26, row-major; 0 = empty, non-zero = solid
 *   832 bytes  airflow layer; 0 = none, 1 = up, 2 = down, 3 = left, 4 = right
 *   optional:  int32 enemy count, then per enemy int32 x, y, type, lookingLeft
 *
 * Output: see src/game/level_data.h. Levels are numbered in argument order.
 *
 * This is a host tool: the game-code rules (no malloc, no float) do not apply.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "game/level_data.h"

#define MAX_LEVELS 99
#define MAX_FILE   4096
#define DEFAULT_HURRY_SECONDS 30

typedef struct {
    const char *path;
    uint8_t     cells[LEVEL_CELLS];
    LevelEnemy  enemies[LEVEL_MAX_ENEMIES];
    int         enemy_count;
    int         tileset;        /* 0-based */
    long        hurry_frames;
    int         special;
    long        special_frames;
    char        settings_path[1024];
    int         has_settings;
} Level;

static Level levels[MAX_LEVELS];

static long read_be32(const unsigned char *p)
{
    unsigned long v = ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
                      ((unsigned long)p[2] << 8) | (unsigned long)p[3];
    return (v & 0x80000000ul) ? -(long)(0xFFFFFFFFul - v) - 1 : (long)v;
}

static int load_level(const char *path, Level *lv)
{
    unsigned char buf[MAX_FILE];
    size_t size;
    long count, i;
    FILE *f = fopen(path, "rb");

    if (!f) {
        fprintf(stderr, "map2c: %s: cannot open file\n", path);
        return 0;
    }
    size = fread(buf, 1, sizeof buf, f);
    fclose(f);

    if (size < 2 * LEVEL_CELLS) {
        fprintf(stderr, "map2c: %s: file too short (%u bytes, need at least %d)\n",
                path, (unsigned)size, 2 * LEVEL_CELLS);
        return 0;
    }

    lv->path = path;
    for (i = 0; i < LEVEL_CELLS; i++) {
        unsigned char solid = buf[i];
        unsigned char air = buf[LEVEL_CELLS + i];
        if (air > AIR_RIGHT) {
            fprintf(stderr, "map2c: %s: invalid airflow value %u at x=%ld y=%ld\n",
                    path, air, i % LEVEL_W, i / LEVEL_W);
            return 0;
        }
        lv->cells[i] = (uint8_t)((solid ? CELL_SOLID : 0u) | air);
    }

    lv->enemy_count = 0;
    if (size == 2 * LEVEL_CELLS) {
        return 1;   /* old files without enemy layer */
    }
    if (size < 2 * LEVEL_CELLS + 4) {
        fprintf(stderr, "map2c: %s: truncated enemy count\n", path);
        return 0;
    }
    count = read_be32(buf + 2 * LEVEL_CELLS);
    if (count < 0 || count > LEVEL_MAX_ENEMIES) {
        fprintf(stderr, "map2c: %s: %ld enemies, allowed are 0..%d\n", path, count, LEVEL_MAX_ENEMIES);
        return 0;
    }
    if (size < (size_t)(2 * LEVEL_CELLS + 4 + count * 16)) {
        fprintf(stderr, "map2c: %s: truncated enemy list\n", path);
        return 0;
    }
    for (i = 0; i < count; i++) {
        const unsigned char *p = buf + 2 * LEVEL_CELLS + 4 + i * 16;
        long x = read_be32(p), y = read_be32(p + 4), type = read_be32(p + 8), left = read_be32(p + 12);
        if (x < 0 || x >= LEVEL_W || y < 0 || y >= LEVEL_H) {
            fprintf(stderr, "map2c: %s: enemy %ld outside the map (x=%ld y=%ld)\n", path, i, x, y);
            return 0;
        }
        if (type < ENEMY_TYPE_WALKER || type > ENEMY_TYPE_COUNT) {
            fprintf(stderr, "map2c: %s: warning: enemy %ld has unknown type %ld, using walker\n", path, i, type);
            type = ENEMY_TYPE_WALKER;
        }
        if (lv->cells[y * LEVEL_W + x] & CELL_SOLID) {
            fprintf(stderr, "map2c: %s: warning: enemy %ld spawns inside a solid tile\n", path, i);
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

/* Parse a whole number; returns 0 if the token is not one. */
static int parse_long(const char *tok, long *out)
{
    char *end;
    long v = strtol(tok, &end, 10);
    if (end == tok || *end != 0) {
        return 0;
    }
    *out = v;
    return 1;
}

static int settings_error(const Level *lv, int line, const char *msg)
{
    fprintf(stderr, "map2c: %s:%d: %s\n", lv->settings_path, line, msg);
    return 0;
}

/* Read <name>.txt next to <name>.map. A missing file means defaults. */
static int load_settings(Level *lv, int index)
{
    char buf[256];
    size_t len = strlen(lv->path);
    int line = 0;
    FILE *f;

    lv->tileset = index % LEVEL_TILESETS;
    lv->hurry_frames = DEFAULT_HURRY_SECONDS * 60;
    lv->has_settings = 0;
    lv->special = SPECIAL_NONE;
    lv->special_frames = 0;

    if (len < 4 || len + 1 > sizeof lv->settings_path) {
        return 1;
    }
    memcpy(lv->settings_path, lv->path, len - 4);
    strcpy(lv->settings_path + len - 4, ".txt");
    f = fopen(lv->settings_path, "r");
    if (!f) {
        return 1;
    }
    lv->has_settings = 1;

    while (fgets(buf, sizeof buf, f)) {
        char *hash = strchr(buf, '#');
        char *key, *tok;
        long v;

        line++;
        if (hash) {
            *hash = 0;
        }
        key = strtok(buf, " \t\r\n");
        if (!key) {
            continue;
        }
        if (strcmp(key, "hurry") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (!tok || !parse_long(tok, &v) || v < 0 || v > 999) {
                fclose(f);
                return settings_error(lv, line, "hurry needs seconds 0..999");
            }
            lv->hurry_frames = v * 60;
        } else if (strcmp(key, "tileset") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (!tok || !parse_long(tok, &v) || v < 1 || v > LEVEL_TILESETS) {
                fclose(f);
                return settings_error(lv, line, "tileset needs a number 1..8");
            }
            lv->tileset = (int)(v - 1);
        } else if (strcmp(key, "delay") == 0) {
            int n = 0;
            while ((tok = strtok(NULL, " \t\r\n")) != NULL) {
                if (!parse_long(tok, &v) || v < 0 || v > 255) {
                    fclose(f);
                    return settings_error(lv, line, "delay values must be frames 0..255");
                }
                if (n >= lv->enemy_count) {
                    fclose(f);
                    return settings_error(lv, line, "more delay values than enemies in the .map");
                }
                lv->enemies[n++].delay = (uint8_t)v;
            }
            if (n != lv->enemy_count) {
                fclose(f);
                return settings_error(lv, line, "fewer delay values than enemies in the .map");
            }
        } else if (strcmp(key, "special") == 0) {
            static const char *const names[] = { "none", "water", "fire", "lightning" };
            int t;
            tok = strtok(NULL, " \t\r\n");
            lv->special = -1;
            for (t = 0; tok && t < 4; t++) {
                if (strcmp(tok, names[t]) == 0) {
                    lv->special = t;
                }
            }
            if (lv->special < 0) {
                fclose(f);
                return settings_error(lv, line, "special needs none, water, fire or lightning");
            }
            tok = strtok(NULL, " \t\r\n");
            if (lv->special != SPECIAL_NONE && (!tok || !parse_long(tok, &v) || v < 1 || v > 999)) {
                fclose(f);
                return settings_error(lv, line, "special needs an interval in seconds 1..999");
            }
            lv->special_frames = lv->special != SPECIAL_NONE ? v * 60 : 0;
            if (strtok(NULL, " \t\r\n") != NULL) {
                fclose(f);
                return settings_error(lv, line, "too many values");
            }
            continue;
        } else {
            fclose(f);
            return settings_error(lv, line, "unknown setting (expected hurry, tileset, delay or special)");
        }
        if (strtok(NULL, " \t\r\n") != NULL && strcmp(key, "delay") != 0) {
            fclose(f);
            return settings_error(lv, line, "too many values");
        }
    }
    fclose(f);
    return 1;
}

static const char *base_name(const char *path)
{
    const char *b = path, *p;
    for (p = path; *p; p++) {
        if (*p == '/' || *p == '\\') {
            b = p + 1;
        }
    }
    return b;
}

static int write_output(const char *out_path, int n)
{
    int l, i;
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "map2c: %s: cannot write file\n", out_path);
        return 0;
    }

    fprintf(f, "/* Generated by tools/map2c. Do not edit; edit the .map files instead.\n");
    for (l = 0; l < n; l++) {
        fprintf(f, " *   level %2d: %s%s\n", l + 1, base_name(levels[l].path),
                levels[l].has_settings ? " + settings" : "");
    }
    fprintf(f, " */\n\n#include \"game/level_data.h\"\n\n");

    for (l = 0; l < n; l++) {
        const Level *lv = &levels[l];
        fprintf(f, "/* %s */\nstatic const uint8_t level%02d_cells[LEVEL_CELLS] = {\n", base_name(lv->path), l + 1);
        for (i = 0; i < LEVEL_CELLS; i++) {
            if (i % LEVEL_W == 0) {
                fprintf(f, "    ");
            }
            fprintf(f, "0x%02X,", lv->cells[i]);
            fprintf(f, (i % LEVEL_W == LEVEL_W - 1) ? "\n" : "");
        }
        fprintf(f, "};\n");
        if (lv->enemy_count > 0) {
            fprintf(f, "static const LevelEnemy level%02d_enemies[%d] = {\n", l + 1, lv->enemy_count);
            for (i = 0; i < lv->enemy_count; i++) {
                fprintf(f, "    { %2u, %2u, %u, 0x%02X, %3u },\n", lv->enemies[i].x, lv->enemies[i].y,
                        lv->enemies[i].type, lv->enemies[i].flags, lv->enemies[i].delay);
            }
            fprintf(f, "};\n");
        }
        fprintf(f, "\n");
    }

    fprintf(f, "const LevelDef level_defs[%d] = {\n", n);
    for (l = 0; l < n; l++) {
        if (levels[l].enemy_count > 0) {
            fprintf(f, "    { level%02d_cells, level%02d_enemies, %d, %d, %ld, %d, %ld },\n", l + 1, l + 1,
                    levels[l].enemy_count, levels[l].tileset, levels[l].hurry_frames,
                    levels[l].special, levels[l].special_frames);
        } else {
            fprintf(f, "    { level%02d_cells, 0, 0, %d, %ld, %d, %ld },\n", l + 1, levels[l].tileset,
                    levels[l].hurry_frames, levels[l].special, levels[l].special_frames);
        }
    }
    fprintf(f, "};\n\nconst uint8_t level_count = %d;\n", n);

    if (fclose(f) != 0) {
        fprintf(stderr, "map2c: %s: write error\n", out_path);
        return 0;
    }
    return 1;
}

static void usage(void)
{
    fprintf(stderr, "usage: map2c -o <output.c> <level.map>...\n");
}

int main(int argc, char **argv)
{
    const char *out_path = NULL;
    int n = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else {
            if (n == MAX_LEVELS) {
                fprintf(stderr, "map2c: too many levels (max %d)\n", MAX_LEVELS);
                return 1;
            }
            if (!load_level(argv[i], &levels[n]) || !load_settings(&levels[n], n)) {
                return 1;
            }
            n++;
        }
    }
    if (!out_path || n == 0) {
        usage();
        return 2;
    }
    if (!write_output(out_path, n)) {
        return 1;
    }
    printf("map2c: wrote %d level(s) to %s\n", n, out_path);
    return 0;
}
