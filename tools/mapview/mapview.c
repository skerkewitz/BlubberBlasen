/*
 * mapview - render level files (*.map) into one PNG contact sheet for review,
 * and/or check that every room can be played.
 *
 * Usage: mapview [-o <sheet.png>] [-c <columns>] [-s <scale>] [--check] <level.map>...
 *
 * --check uses a simplified movement model (walk, fall, wrap through gaps in
 * the bottom row, jump up to 5 rows through one-way platforms and 4 columns
 * sideways) and reports rooms where an enemy that walks cannot be reached,
 * an enemy that needs a floor has none, or the player start has no floor.
 * Exit code 1 if any room fails.
 *
 * Each room shows: walls (pink), airflow (grey arrows), enemy spawn points
 * (walker purple, bouncer cyan, thrower orange, flutter yellow, hopper green,
 * spitter red, dropper grey; a white bar marks the side
 * it faces), the player start points (green = 1, white = 2) and the round
 * number taken from the file name (round011.map -> 11).
 *
 * Plain C, no libraries: the PNG is written with uncompressed deflate blocks.
 * This is a host tool: the game-code rules (no malloc, no float) do not apply.
 */

#define _CRT_SECURE_NO_WARNINGS
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/reach.h"

#define ROOM_W   (LEVEL_W * 8)
#define ROOM_H   (LEVEL_H * 8)
#define MARGIN   8
#define MAX_MAPS 120

typedef struct { unsigned char r, g, b; } Rgb;

static unsigned char *img;
static int img_w, img_h, scale = 1;

static const Rgb C_BACK   = { 18, 18, 28 };
static const Rgb C_SHEET  = { 50, 50, 60 };
static const Rgb C_WALL   = { 235, 100, 170 };
static const Rgb C_WALL_D = { 150, 40, 100 };
static const Rgb C_AIR    = { 80, 80, 105 };
static const Rgb C_WHITE  = { 255, 255, 255 };
static const Rgb C_LABEL  = { 255, 230, 60 };
/* enemy types 1..8: walker, bouncer, thrower, flutter, hopper, spitter, dropper, shield */
static const Rgb C_TYPE[ENEMY_TYPE_COUNT + 1] = { { 0, 0, 0 }, { 160, 90, 230 }, { 90, 200, 255 }, { 230, 140, 60 },
                               { 250, 220, 80 }, { 120, 230, 120 }, { 240, 70, 60 }, { 170, 170, 190 },
                               { 240, 170, 100 } };
static const Rgb C_P1 = { 80, 220, 90 };
static const Rgb C_P2 = { 240, 240, 240 };

/* --- drawing (coordinates in room pixels, scaled on output) ----------------- */
static void pixel(int ox, int oy, int x, int y, Rgb c)
{
    int sx, sy;
    for (sy = 0; sy < scale; sy++) {
        for (sx = 0; sx < scale; sx++) {
            int px = ox + x * scale + sx, py = oy + y * scale + sy;
            if (px >= 0 && py >= 0 && px < img_w && py < img_h) {
                unsigned char *p = img + ((size_t)py * img_w + px) * 3;
                p[0] = c.r;
                p[1] = c.g;
                p[2] = c.b;
            }
        }
    }
}

static void rect(int ox, int oy, int x, int y, int w, int h, Rgb c)
{
    int i, j;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            pixel(ox, oy, x + i, y + j, c);
        }
    }
}

static void digit(int ox, int oy, int x, int y, int d, Rgb c)
{
    static const char *const font[10] = {
        "111101101101111", "010110010010111", "111001111100111", "111001111001111", "101101111001001",
        "111100111001111", "111100111101111", "111001010010010", "111101111101111", "111101111001111"
    };
    int i;
    for (i = 0; i < 15; i++) {
        if (font[d][i] == '1') {
            rect(ox, oy, x + (i % 3) * 2, y + (i / 3) * 2, 2, 2, c);
        }
    }
}

static void arrow(int ox, int oy, int tx, int ty, int dir)
{
    int cx = tx * 8 + 4, cy = ty * 8 + 4;
    switch (dir) {
    case AIR_UP:    pixel(ox, oy, cx, cy - 2, C_AIR); pixel(ox, oy, cx, cy - 1, C_AIR); pixel(ox, oy, cx, cy, C_AIR);
                    pixel(ox, oy, cx - 1, cy - 1, C_AIR); pixel(ox, oy, cx + 1, cy - 1, C_AIR); break;
    case AIR_DOWN:  pixel(ox, oy, cx, cy + 2, C_AIR); pixel(ox, oy, cx, cy + 1, C_AIR); pixel(ox, oy, cx, cy, C_AIR);
                    pixel(ox, oy, cx - 1, cy + 1, C_AIR); pixel(ox, oy, cx + 1, cy + 1, C_AIR); break;
    case AIR_LEFT:  pixel(ox, oy, cx - 2, cy, C_AIR); pixel(ox, oy, cx - 1, cy, C_AIR); pixel(ox, oy, cx, cy, C_AIR);
                    pixel(ox, oy, cx - 1, cy - 1, C_AIR); pixel(ox, oy, cx - 1, cy + 1, C_AIR); break;
    case AIR_RIGHT: pixel(ox, oy, cx + 2, cy, C_AIR); pixel(ox, oy, cx + 1, cy, C_AIR); pixel(ox, oy, cx, cy, C_AIR);
                    pixel(ox, oy, cx + 1, cy - 1, C_AIR); pixel(ox, oy, cx + 1, cy + 1, C_AIR); break;
    default: break;
    }
}

/* A 16x16 marker with its top-left at room pixel (x, y). */
static void marker(int ox, int oy, int x, int y, Rgb c, int face_left)
{
    rect(ox, oy, x + 1, y + 1, 14, 14, c);
    rect(ox, oy, face_left ? x + 1 : x + 13, y + 4, 2, 6, C_WHITE);
}

static int round_number(const char *path, int fallback)
{
    const char *p = path + strlen(path);
    int n = 0, mul = 1, found = 0;
    while (p > path && (p[-1] < '0' || p[-1] > '9') && p[-1] != '/' && p[-1] != '\\') {
        p--;
    }
    while (p > path && p[-1] >= '0' && p[-1] <= '9') {
        n += (p[-1] - '0') * mul;
        mul *= 10;
        p--;
        found = 1;
    }
    return found ? n : fallback;
}

static void draw_room(const MapFile *m, int ox, int oy, int number)
{
    int x, y, i;

    rect(ox, oy, 0, 0, ROOM_W, ROOM_H, C_BACK);
    for (y = 0; y < LEVEL_H; y++) {
        for (x = 0; x < LEVEL_W; x++) {
            uint8_t c = m->cells[y * LEVEL_W + x];
            if (c & CELL_SOLID) {
                rect(ox, oy, x * 8, y * 8, 8, 8, C_WALL);
                rect(ox, oy, x * 8, y * 8 + 7, 8, 1, C_WALL_D);
                rect(ox, oy, x * 8 + 7, y * 8, 1, 8, C_WALL_D);
            } else {
                arrow(ox, oy, x, y, c & CELL_AIR_MASK);
            }
        }
    }
    /* player starts: feet on row 24, sprites at x 24 and 216 */
    marker(ox, oy, 24, 24 * 8 - 8, C_P1, 0);
    marker(ox, oy, ROOM_W - 40, 24 * 8 - 8, C_P2, 1);
    for (i = 0; i < m->enemy_count; i++) {
        const LevelEnemy *e = &m->enemies[i];
        int t = e->type >= 1 && e->type <= ENEMY_TYPE_COUNT ? e->type : 1;
        marker(ox, oy, e->x * 8, e->y * 8 - 8, C_TYPE[t], (e->flags & LEVEL_ENEMY_FACE_LEFT) != 0);
    }
    /* round number on the top-left wall */
    if (number >= 100) {
        digit(ox, oy, 1, 1, number / 100 % 10, C_LABEL);
        digit(ox, oy, 8, 1, number / 10 % 10, C_LABEL);
        digit(ox, oy, 15, 1, number % 10, C_LABEL);
    } else {
        digit(ox, oy, 1, 1, number / 10 % 10, C_LABEL);
        digit(ox, oy, 8, 1, number % 10, C_LABEL);
    }
}

/* --- PNG output ---------------------------------------------------------------- */
static unsigned long crc_table[256];

static void crc_init(void)
{
    unsigned long c;
    int n, k;
    for (n = 0; n < 256; n++) {
        c = (unsigned long)n;
        for (k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320ul ^ (c >> 1) : c >> 1;
        }
        crc_table[n] = c;
    }
}

static unsigned long crc_update(unsigned long crc, const unsigned char *buf, size_t len)
{
    size_t i;
    for (i = 0; i < len; i++) {
        crc = crc_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc;
}

static void put32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)(v >> 24);
    p[1] = (unsigned char)(v >> 16);
    p[2] = (unsigned char)(v >> 8);
    p[3] = (unsigned char)v;
}

static int write_chunk(FILE *f, const char *type, const unsigned char *data, size_t len)
{
    unsigned char hdr[8], crcb[4];
    unsigned long crc;
    put32(hdr, (unsigned long)len);
    memcpy(hdr + 4, type, 4);
    crc = crc_update(0xFFFFFFFFul, hdr + 4, 4);
    crc = crc_update(crc, data, len) ^ 0xFFFFFFFFul;
    put32(crcb, crc);
    return fwrite(hdr, 1, 8, f) == 8 && (len == 0 || fwrite(data, 1, len, f) == len) && fwrite(crcb, 1, 4, f) == 4;
}

static int write_png(const char *path)
{
    static const unsigned char sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    size_t row = (size_t)img_w * 3 + 1, raw_len = row * img_h;
    size_t blocks = (raw_len + 65534) / 65535;
    size_t z_len = 2 + raw_len + blocks * 5 + 4;
    unsigned char *raw = malloc(raw_len), *z = malloc(z_len), ihdr[13];
    unsigned long a = 1, b = 0;
    size_t i, pos = 0, done = 0;
    int y, ok;
    FILE *f;

    if (!raw || !z) {
        fprintf(stderr, "mapview: out of memory\n");
        return 0;
    }
    for (y = 0; y < img_h; y++) {
        raw[row * y] = 0;
        memcpy(raw + row * y + 1, img + (size_t)y * img_w * 3, (size_t)img_w * 3);
    }
    z[pos++] = 0x78;
    z[pos++] = 0x01;
    while (done < raw_len) {
        size_t n = raw_len - done > 65535 ? 65535 : raw_len - done;
        z[pos++] = (unsigned char)(done + n == raw_len ? 1 : 0);
        z[pos++] = (unsigned char)(n & 0xFF);
        z[pos++] = (unsigned char)(n >> 8);
        z[pos++] = (unsigned char)(~n & 0xFF);
        z[pos++] = (unsigned char)((~n >> 8) & 0xFF);
        memcpy(z + pos, raw + done, n);
        pos += n;
        done += n;
    }
    for (i = 0; i < raw_len; i++) {
        a = (a + raw[i]) % 65521ul;
        b = (b + a) % 65521ul;
    }
    put32(z + pos, (b << 16) | a);
    pos += 4;

    put32(ihdr, (unsigned long)img_w);
    put32(ihdr + 4, (unsigned long)img_h);
    ihdr[8] = 8;    /* bit depth */
    ihdr[9] = 2;    /* RGB */
    ihdr[10] = ihdr[11] = ihdr[12] = 0;

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "mapview: %s: cannot write file\n", path);
        free(raw);
        free(z);
        return 0;
    }
    crc_init();
    ok = fwrite(sig, 1, 8, f) == 8 && write_chunk(f, "IHDR", ihdr, 13) &&
         write_chunk(f, "IDAT", z, pos) && write_chunk(f, "IEND", NULL, 0);
    ok = (fclose(f) == 0) && ok;
    free(raw);
    free(z);
    if (!ok) {
        fprintf(stderr, "mapview: %s: write error\n", path);
    }
    return ok;
}

/* --- reachability check (movement model in common/reach.c) ------------------------ */
static int enemy_needs_floor(int type)
{
    return type != ENEMY_TYPE_BOUNCER && type != ENEMY_TYPE_FLUTTER;
}

static int enemy_walks(int type)
{
    return enemy_needs_floor(type) && type != ENEMY_TYPE_HOPPER;
}

static int seen_near(unsigned char seen[LEVEL_W][LEVEL_H], int x, int y)
{
    return (x - 1 >= 0 && seen[x - 1][y]) || seen[x][y] || (x + 1 < LEVEL_W && seen[x + 1][y]);
}

/* Fails a room if the lenient model cannot reach an enemy. With walls
 * blocking jumps as in the game, it also reports (without failing) enemies
 * that only a dash reaches, and those that need more than walking, jumping
 * and dashing (a bubble bounce, or the enemy walking out on its own). */
static int check_room(const MapFile *m)
{
    static unsigned char seen[LEVEL_W][LEVEL_H], strict[LEVEL_W][LEVEL_H], dash_seen[LEVEL_W][LEVEL_H];
    int ok = 1, i;

    if (!reach_stands(m, 3, LEVEL_H - 2)) {
        fprintf(stderr, "mapview: %s: no floor at the player 1 start\n", m->path);
        return 0;
    }
    reach_from_start(m, seen);
    reach_from_start_ex(m, strict, REACH_WALLS);
    reach_from_start_ex(m, dash_seen, REACH_WALLS | REACH_DASH);
    for (i = 0; i < m->enemy_count; i++) {
        const LevelEnemy *e = &m->enemies[i];
        int x = e->x, y = e->y;
        if (enemy_needs_floor(e->type) && !reach_stands(m, x, y) && !reach_stands(m, x - 1, y)) {
            fprintf(stderr, "mapview: %s: enemy %d (x=%d y=%d) has no floor to stand on\n", m->path, i, x, y);
            ok = 0;
        } else if (enemy_walks(e->type) && !seen_near(seen, x, y)) {
            fprintf(stderr, "mapview: %s: enemy %d (x=%d y=%d) cannot be reached\n", m->path, i, x, y);
            ok = 0;
        } else if (enemy_walks(e->type) && !seen_near(strict, x, y)) {
            printf("mapview: %s: enemy %d (x=%d y=%d) %s\n", m->path, i, x, y,
                   seen_near(dash_seen, x, y) ? "is behind a wall: Plus needs a dash"
                                              : "needs more than walk, jump and dash");
        }
    }
    return ok;
}

static void usage(void)
{
    fprintf(stderr, "usage: mapview [-o <sheet.png>] [-c <columns>] [-s <scale>] [--check] <level.map>...\n");
}

int main(int argc, char **argv)
{
    static MapFile maps[MAX_MAPS];
    const char *out = NULL;
    int n = 0, cols = 5, i, rows, check = 0;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out = argv[++i];
        } else if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            cols = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-s") == 0 && i + 1 < argc) {
            scale = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--check") == 0) {
            check = 1;
        } else if (argv[i][0] == '-') {
            usage();
            return 2;
        } else {
            if (n == MAX_MAPS) {
                fprintf(stderr, "mapview: too many maps (max %d)\n", MAX_MAPS);
                return 1;
            }
            if (!mapfile_load("mapview", argv[i], &maps[n])) {
                return 1;
            }
            n++;
        }
    }
    if ((!out && !check) || n == 0 || cols < 1 || scale < 1 || scale > 4) {
        usage();
        return 2;
    }
    if (check) {
        int failed = 0;
        for (i = 0; i < n; i++) {
            failed += !check_room(&maps[i]);
        }
        printf("mapview: checked %d room(s), %d with problems\n", n, failed);
        if (failed) {
            return 1;
        }
        if (!out) {
            return 0;
        }
    }
    if (cols > n) {
        cols = n;
    }
    rows = (n + cols - 1) / cols;
    img_w = MARGIN + cols * (ROOM_W * scale + MARGIN);
    img_h = MARGIN + rows * (ROOM_H * scale + MARGIN);
    img = malloc((size_t)img_w * img_h * 3);
    if (!img) {
        fprintf(stderr, "mapview: out of memory\n");
        return 1;
    }
    {
        int saved = scale;
        scale = 1;
        rect(0, 0, 0, 0, img_w, img_h, C_SHEET);
        scale = saved;
    }
    for (i = 0; i < n; i++) {
        int ox = MARGIN + (i % cols) * (ROOM_W * scale + MARGIN);
        int oy = MARGIN + (i / cols) * (ROOM_H * scale + MARGIN);
        draw_room(&maps[i], ox, oy, round_number(maps[i].path, i + 1));
    }
    if (!write_png(out)) {
        free(img);
        return 1;
    }
    free(img);
    printf("mapview: wrote %d room(s) to %s\n", n, out);
    return 0;
}
