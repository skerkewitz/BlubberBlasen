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
 *   boss    1                 the boss round
 *   secret  1                 the secret room (not part of the normal round order)
 *   # comment
 *
 * Input format: see tools/common/mapfile.h.
 *
 * Output: see src/game/level_data.h. Levels are numbered in argument order.
 *
 * This is a host tool: the game-code rules (no malloc, no float) do not apply.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/reach.h"

#define MAX_LEVELS 120
#define DEFAULT_HURRY_SECONDS 30

typedef struct {
    MapFile     m;
    int         tileset;        /* 0-based */
    long        hurry_frames;
    int         special;
    long        special_frames;
    int         kind;
    NavSeg      nav[255];
    uint8_t     nav_edges[255 * 32];
    int         nav_count;
    int         nav_edge_count;
    char        settings_path[1024];
    int         has_settings;
} Level;

static Level levels[MAX_LEVELS];

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
    size_t len = strlen(lv->m.path);
    int line = 0;
    FILE *f;

    lv->tileset = index % LEVEL_TILESETS;
    lv->hurry_frames = DEFAULT_HURRY_SECONDS * 60;
    lv->has_settings = 0;
    lv->special = SPECIAL_NONE;
    lv->special_frames = 0;
    lv->kind = LEVEL_NORMAL;

    if (len < 4 || len + 1 > sizeof lv->settings_path) {
        return 1;
    }
    memcpy(lv->settings_path, lv->m.path, len - 4);
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
                if (n >= lv->m.enemy_count) {
                    fclose(f);
                    return settings_error(lv, line, "more delay values than enemies in the .map");
                }
                lv->m.enemies[n++].delay = (uint8_t)v;
            }
            if (n != lv->m.enemy_count) {
                fclose(f);
                return settings_error(lv, line, "fewer delay values than enemies in the .map");
            }
        } else if (strcmp(key, "boss") == 0 || strcmp(key, "secret") == 0) {
            tok = strtok(NULL, " \t\r\n");
            if (!tok || !parse_long(tok, &v) || v < 0 || v > 1) {
                fclose(f);
                return settings_error(lv, line, "boss and secret take 0 or 1");
            }
            if (v) {
                lv->kind = strcmp(key, "boss") == 0 ? LEVEL_BOSS : LEVEL_SECRET;
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
            return settings_error(lv, line, "unknown setting (expected hurry, tileset, delay, special, boss or secret)");
        }
        if (strtok(NULL, " \t\r\n") != NULL && strcmp(key, "delay") != 0) {
            fclose(f);
            return settings_error(lv, line, "too many values");
        }
    }
    fclose(f);
    return 1;
}

/* Navigation data for the CPU partner: segments and their edges. */
static int build_nav(Level *lv)
{
    static int seg_of[LEVEL_W][LEVEL_H];
    int moves[REACH_MAX_MOVES][2];
    int c, r, s, i, n;

    lv->nav_count = 0;
    lv->nav_edge_count = 0;
    for (c = 0; c < LEVEL_W; c++) {
        for (r = 0; r < LEVEL_H; r++) {
            seg_of[c][r] = -1;
        }
    }
    for (r = 0; r < LEVEL_H - 1; r++) {
        for (c = 2; c <= LEVEL_W - 4; c++) {
            if (!reach_stands(&lv->m, c, r)) {
                continue;
            }
            if (c > 2 && seg_of[c - 1][r] >= 0) {
                seg_of[c][r] = seg_of[c - 1][r];
                lv->nav[seg_of[c][r]].x1 = (uint8_t)c;
                continue;
            }
            if (lv->nav_count == 255) {
                fprintf(stderr, "map2c: %s: too many platform segments for the navigation data\n", lv->m.path);
                return 0;
            }
            seg_of[c][r] = lv->nav_count;
            lv->nav[lv->nav_count].row = (uint8_t)r;
            lv->nav[lv->nav_count].x0 = (uint8_t)c;
            lv->nav[lv->nav_count].x1 = (uint8_t)c;
            lv->nav_count++;
        }
    }
    for (s = 0; s < lv->nav_count; s++) {
        NavSeg *seg = &lv->nav[s];
        seg->edge_first = (uint16_t)lv->nav_edge_count;
        seg->edge_count = 0;
        for (c = seg->x0; c <= seg->x1; c++) {
            n = reach_moves(&lv->m, c, seg->row, moves);
            for (i = 0; i < n; i++) {
                int t = seg_of[moves[i][0]][moves[i][1]], k, known = 0;
                if (t < 0 || t == s) {
                    continue;
                }
                for (k = 0; k < seg->edge_count; k++) {
                    known |= lv->nav_edges[seg->edge_first + k] == t;
                }
                if (!known && seg->edge_count < 32) {
                    lv->nav_edges[lv->nav_edge_count++] = (uint8_t)t;
                    seg->edge_count++;
                }
            }
        }
    }
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

/* --- SNES output (--snes): the per-level arrays as assembly in ROM ---------------
 * 816-tcc copies every initialised global into RAM at start-up and puts a
 * file's static const data into one ROM section, which must fit a 32 KB
 * bank. So for the SNES the arrays go into level_data.asm, one ROM section
 * per level, as plain bytes (NavSeg: 6 bytes, LevelEnemy: 5 bytes, no gaps;
 * the C file checks those sizes), and the C file only holds level_defs,
 * whose pointers 816-tcc lays out itself. */
static void asm_bytes(FILE *f, const uint8_t *b, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        fprintf(f, "%s$%02X", i % 16 ? "," : (i ? "\n.db " : ".db "), b[i]);
    }
    fprintf(f, "\n");
}

static int write_asm(const char *asm_path, int n)
{
    int l, i;
    FILE *f = fopen(asm_path, "w");
    if (!f) {
        fprintf(stderr, "map2c: %s: cannot write file\n", asm_path);
        return 0;
    }
    fprintf(f, "; Generated by tools/map2c (--snes). Do not edit; edit the .map files instead.\n");
    fprintf(f, "; The level arrays for the SNES build, one ROM section per level.\n\n.include \"hdr.asm\"\n\n");
    for (l = 0; l < n; l++) {
        const Level *lv = &levels[l];
        static uint8_t buf[6 * 255 + LEVEL_MAX_ENEMIES * 5 + 8];
        int k = 0;
        fprintf(f, "; %s\n.SECTION \".level%02d\" SUPERFREE\n", base_name(lv->m.path), l + 1);
        fprintf(f, "level%02d_cells:\n", l + 1);
        asm_bytes(f, lv->m.cells, LEVEL_CELLS);
        for (i = 0; i < lv->nav_count; i++) {
            buf[k++] = lv->nav[i].row;
            buf[k++] = lv->nav[i].x0;
            buf[k++] = lv->nav[i].x1;
            buf[k++] = lv->nav[i].edge_count;
            buf[k++] = (uint8_t)(lv->nav[i].edge_first & 0xFF);
            buf[k++] = (uint8_t)(lv->nav[i].edge_first >> 8);
        }
        if (lv->nav_count == 0) {
            memset(buf, 0, 6);
            k = 6;
        }
        fprintf(f, "level%02d_nav:\n", l + 1);
        asm_bytes(f, buf, k);
        fprintf(f, "level%02d_nav_edges:\n", l + 1);
        if (lv->nav_edge_count > 0) {
            asm_bytes(f, lv->nav_edges, lv->nav_edge_count);
        } else {
            fprintf(f, ".db $00\n");
        }
        if (lv->m.enemy_count > 0) {
            k = 0;
            for (i = 0; i < lv->m.enemy_count; i++) {
                buf[k++] = lv->m.enemies[i].x;
                buf[k++] = lv->m.enemies[i].y;
                buf[k++] = lv->m.enemies[i].type;
                buf[k++] = lv->m.enemies[i].flags;
                buf[k++] = lv->m.enemies[i].delay;
            }
            fprintf(f, "level%02d_enemies:\n", l + 1);
            asm_bytes(f, buf, k);
        }
        fprintf(f, ".ENDS\n\n");
    }
    if (fclose(f) != 0) {
        fprintf(stderr, "map2c: %s: write error\n", asm_path);
        return 0;
    }
    return 1;
}

/* The C file. With snes set, the arrays are only declared (they are in the
 * assembly file) and made global, so the assembler's labels are found. */
static int write_output(const char *out_path, int n, int snes)
{
    int l, i;
    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "map2c: %s: cannot write file\n", out_path);
        return 0;
    }

    fprintf(f, "/* Generated by tools/map2c. Do not edit; edit the .map files instead.\n");
    for (l = 0; l < n; l++) {
        fprintf(f, " *   level %2d: %s%s\n", l + 1, base_name(levels[l].m.path),
                levels[l].has_settings ? " + settings" : "");
    }
    fprintf(f, " */\n\n#include \"game/level_data.h\"\n\n");

    if (snes) {
        fprintf(f, "/* SNES build: the arrays are in level_data.asm (ROM); their layout there\n"
                   " * assumes these sizes. */\n");
        fprintf(f, "typedef char navseg_is_6_bytes[sizeof(NavSeg) == 6 ? 1 : -1];\n");
        fprintf(f, "typedef char level_enemy_is_5_bytes[sizeof(LevelEnemy) == 5 ? 1 : -1];\n\n");
        for (l = 0; l < n; l++) {
            fprintf(f, "extern const uint8_t level%02d_cells[];\nextern const NavSeg level%02d_nav[];\n"
                       "extern const uint8_t level%02d_nav_edges[];\n", l + 1, l + 1, l + 1);
            if (levels[l].m.enemy_count > 0) {
                fprintf(f, "extern const LevelEnemy level%02d_enemies[];\n", l + 1);
            }
        }
        fprintf(f, "\n");
    }
    for (l = 0; l < n && !snes; l++) {
        const Level *lv = &levels[l];
        fprintf(f, "/* %s */\nstatic const uint8_t level%02d_cells[LEVEL_CELLS] = {\n", base_name(lv->m.path), l + 1);
        for (i = 0; i < LEVEL_CELLS; i++) {
            if (i % LEVEL_W == 0) {
                fprintf(f, "    ");
            }
            fprintf(f, "0x%02X,", lv->m.cells[i]);
            fprintf(f, (i % LEVEL_W == LEVEL_W - 1) ? "\n" : "");
        }
        fprintf(f, "};\n");
        fprintf(f, "static const NavSeg level%02d_nav[%d] = {\n", l + 1, lv->nav_count > 0 ? lv->nav_count : 1);
        for (i = 0; i < lv->nav_count; i++) {
            fprintf(f, "    { %2u, %2u, %2u, %2u, %4u },\n", lv->nav[i].row, lv->nav[i].x0, lv->nav[i].x1,
                    lv->nav[i].edge_count, lv->nav[i].edge_first);
        }
        if (lv->nav_count == 0) {
            fprintf(f, "    { 0, 0, 0, 0, 0 },\n");
        }
        fprintf(f, "};\nstatic const uint8_t level%02d_nav_edges[%d] = {", l + 1, lv->nav_edge_count > 0 ? lv->nav_edge_count : 1);
        for (i = 0; i < lv->nav_edge_count; i++) {
            fprintf(f, "%s%u,", (i % 24) == 0 ? "\n    " : " ", lv->nav_edges[i]);
        }
        fprintf(f, "%s\n};\n", lv->nav_edge_count == 0 ? "\n    0," : "");
        if (lv->m.enemy_count > 0) {
            fprintf(f, "static const LevelEnemy level%02d_enemies[%d] = {\n", l + 1, lv->m.enemy_count);
            for (i = 0; i < lv->m.enemy_count; i++) {
                fprintf(f, "    { %2u, %2u, %u, 0x%02X, %3u },\n", lv->m.enemies[i].x, lv->m.enemies[i].y,
                        lv->m.enemies[i].type, lv->m.enemies[i].flags, lv->m.enemies[i].delay);
            }
            fprintf(f, "};\n");
        }
        fprintf(f, "\n");
    }

    fprintf(f, "const LevelDef level_defs[%d] = {\n", n);
    for (l = 0; l < n; l++) {
        if (levels[l].m.enemy_count > 0) {
            fprintf(f, "    { level%02d_cells, level%02d_enemies, %d, %d, %ld, %d, %ld, %d, level%02d_nav, level%02d_nav_edges, %d },\n",
                    l + 1, l + 1, levels[l].m.enemy_count, levels[l].tileset, levels[l].hurry_frames,
                    levels[l].special, levels[l].special_frames, levels[l].kind, l + 1, l + 1, levels[l].nav_count);
        } else {
            fprintf(f, "    { level%02d_cells, 0, 0, %d, %ld, %d, %ld, %d, level%02d_nav, level%02d_nav_edges, %d },\n",
                    l + 1, levels[l].tileset, levels[l].hurry_frames, levels[l].special, levels[l].special_frames,
                    levels[l].kind, l + 1, l + 1, levels[l].nav_count);
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
    fprintf(stderr, "usage: map2c -o <output.c> [--snes <output.asm>] <level.map>...\n");
}

int main(int argc, char **argv)
{
    const char *out_path = NULL, *asm_path = NULL;
    int n = 0, i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--snes") == 0 && i + 1 < argc) {
            asm_path = argv[++i];
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else {
            if (n == MAX_LEVELS) {
                fprintf(stderr, "map2c: too many levels (max %d)\n", MAX_LEVELS);
                return 1;
            }
            if (!mapfile_load("map2c", argv[i], &levels[n].m) || !load_settings(&levels[n], n) || !build_nav(&levels[n])) {
                return 1;
            }
            n++;
        }
    }
    if (!out_path || n == 0) {
        usage();
        return 2;
    }
    if (!write_output(out_path, n, asm_path != NULL) || (asm_path && !write_asm(asm_path, n))) {
        return 1;
    }
    printf("map2c: wrote %d level(s) to %s\n", n, out_path);
    return 0;
}
