/*
 * Compiler benchmark for the SNES (ROADMAP.md, stage 3 spike).
 *
 * The same game code, built once with 816-tcc (PVSnesLib) and once with
 * Calypsi, runs a fixed input script without any hardware: every PVSnesLib
 * call goes to an empty stub (renamed with -D, see build.sh), there is no
 * interrupt and no waiting for the vertical blank. What is left is the
 * time of the compiled game code.
 *
 * Each tick is framed by writes to unused B-bus ports, which the Mesen script
 * (bench.lua) watches:
 *   $21FC <- game state      tick starts (game_tick, then game_vblank)
 *   $21FF <- 0               tick ends
 *   $21FD/$21FE <- hash      three checksums: sprite table, tilemap 0, tilemap 1,
 *                            to check that both builds compute the same game
 *   $21FA <- 0               the tick's checksums are written
 *   $21FB <- 1               benchmark done
 * After the one-player ticks and the micro benchmark, the game starts again
 * with player 2 as CPU partner (the "PLAYER 2: CPU" option) for CPU_TICKS
 * more ticks; those report their state plus CPU_STATE, so report.py lists
 * them apart.
 * Mesen records the master clock at the start and end markers.
 *
 * With BENCH_PC (tools/snesbench/pc) the same program runs on the PC and
 * prints the lines bench.lua writes, with 0 clocks: the reference checksums.
 */

#include "game/game.h"
#include "game/internal.h"
#include "snes/snes.h"

#define TICKS 1800u
#define CPU_TICKS 1800u
#define CPU_STATE 32u   /* added to the state of the CPU-partner ticks */

#ifdef BENCH_PC
#include <stdio.h>
static uint8_t pc_state;
static uint16_t pc_tick;
static void pc_port(uint16_t a, uint8_t v)
{
    static uint8_t lo;
    if (a == 0x21FC) {
        pc_state = v;
    } else if (a == 0x21FF) {
        printf("%u %u 0 ", pc_tick, pc_state);
    } else if (a == 0x21FD) {
        lo = v;
    } else if (a == 0x21FE) {
        printf("%04X", lo + 256u * v);
    } else if (a == 0x21FA) {
        printf("\n");
        pc_tick++;
    }
}
#define PORT_WRITE(a, v) pc_port((a), (v))
#else
#define PORT_WRITE(a, v) (*(volatile uint8_t *)(a) = (uint8_t)(v))
#endif

u8 oamMemory[128 * 4 + 8 * 4];

/* --- Stubs for everything in snes/snes.h the game calls --------------------- */
void setMode(u8 mode, u8 size) { (void)mode; (void)size; }
void setBrightness(u8 level) { (void)level; }
void setScreenOn(void) { }
void setScreenOff(void) { }
void setColorEffect(u8 a, u8 b) { (void)a; (void)b; }
void setColorIntensity(u8 a, u8 b) { (void)a; (void)b; }
void setPaletteColor(u16 e, u16 c) { (void)e; (void)c; }
void dmaCopyVram(const u8 *s, u16 a, u16 n) { (void)s; (void)a; (void)n; }
void dmaCopyCGram(const u8 *s, u16 a, u16 n) { (void)s; (void)a; (void)n; }
void dmaClearVram(void) { }
void bgSetGfxPtr(u8 bg, u16 a) { (void)bg; (void)a; }
void bgSetMapPtr(u8 bg, u16 a, u8 m) { (void)bg; (void)a; (void)m; }
void bgSetScroll(u8 bg, u16 x, u16 y) { (void)bg; (void)x; (void)y; }
void bgSetEnable(u8 bg) { (void)bg; }
void bgSetDisable(u8 bg) { (void)bg; }
void bgSetEnableSub(u8 bg) { (void)bg; }
void bgSetDisableSub(u8 bg) { (void)bg; }
void bgInitTileSet(u8 bg, const u8 *t, const u8 *p, u8 e, u16 ts, u16 ps, u16 m, u16 a)
{
    (void)bg; (void)t; (void)p; (void)e; (void)ts; (void)ps; (void)m; (void)a;
}
void bgInitMapSet(u8 bg, const u8 *m, u16 n, u8 s, u16 a) { (void)bg; (void)m; (void)n; (void)s; (void)a; }
void oamInit(void) { }
void oamInitGfxSet(const u8 *t, u16 ts, const u8 *p, u16 ps, u8 pn, u16 a, u8 s)
{
    (void)t; (void)ts; (void)p; (void)ps; (void)pn; (void)a; (void)s;
}
void oamInitGfxAttr(u16 a, u8 s) { (void)a; (void)s; }
void oamSet(u16 id, u16 x, u16 y, u8 pr, u8 hf, u8 vf, u16 g, u8 p)
{
    (void)id; (void)x; (void)y; (void)pr; (void)hf; (void)vf; (void)g; (void)p;
}
void oamSetXY(u16 id, u16 x, u16 y) { (void)id; (void)x; (void)y; }
void oamSetEx(u16 id, u8 s, u8 h) { (void)id; (void)s; (void)h; }
void oamSetVisible(u16 id, u8 h) { (void)id; (void)h; }
void oamClear(u16 first, u8 n) { (void)first; (void)n; }
void snesRegWrite(u8 reg, u8 value) { (void)reg; (void)value; }
void snesHdmaSet(u8 c, u8 r, u8 t, const u8 *table) { (void)c; (void)r; (void)t; (void)table; }
void snesHdmaEnable(u8 c) { (void)c; }
u8 snesDmaFits(u16 bytes) { (void)bytes; return 1; }

/* --- The input script ---------------------------------------------------------
 * Title for 60 ticks, Start (into round 1; the story is skipped by default),
 * then play: walk right and left in turns, blow a bubble (Y) every 8 ticks,
 * jump (B) every 32. Bit masks, no %: Calypsi 5.18 at -O2 drops the result
 * of a division that is only compared with zero. */
static uint16_t script(uint16_t t, uint8_t state)
{
    uint16_t p = 0;
    if (t == 60u) {
        return PAD_START;
    }
    if (t == 160u || t == 230u) {
        return PAD_B;
    }
    if (state == 5u) {   /* GS_PLAY */
        p = (t & 64u) ? PAD_LEFT : PAD_RIGHT;
        if ((t & 7u) == 0u) {
            p |= PAD_Y;   /* blow a bubble */
        }
        if ((t & 31u) == 0u) {
            p |= PAD_B;   /* jump */
        }
    }
    return p;
}

static uint16_t hash_bytes(uint16_t h, const uint8_t *b, uint16_t n)
{
    while (n--) {
        h = (uint16_t)(((h << 1) | (h >> 15)) ^ *b++);
    }
    return h;
}

static void put_hash(uint16_t h)
{
    PORT_WRITE(0x21FD, (uint8_t)h);
    PORT_WRITE(0x21FE, (uint8_t)(h >> 8));
}

/* Micro benchmark after the ticks, reported as two extra "states": 98 is the
 * bare loop over 1000 positions, 99 the same loop calling level_solid_px
 * (the room of the last tick). The sum of the results goes into the checksum. */
static void micro_run(uint8_t state, uint8_t call)
{
    uint16_t i, sum = 0;
    int16_t sx = -64, sy = -8;
    PORT_WRITE(0x21FC, state);
    for (i = 0; i < 1000u; i++) {
        if (call) {
            sum = (uint16_t)(sum + level_solid_px(sx, sy));
        } else {
            sum = (uint16_t)(sum + (uint16_t)sx);
        }
        sx = (int16_t)(sx + 37);
        if (sx > 320) {
            sx = (int16_t)(sx - 384);
        }
        sy = (int16_t)(sy + 53);
        if (sy > 240) {
            sy = (int16_t)(sy - 256);
        }
    }
    PORT_WRITE(0x21FF, 0);
    put_hash(sum);
    put_hash(0);
    put_hash(0);
    PORT_WRITE(0x21FA, 0);
}

static void micro(void)
{
    uint8_t k;
    for (k = 0; k < 5u; k++) {
        micro_run(98, 0);
        micro_run(99, 1);
    }
}

#ifdef BENCH_TCC
void snes_clear_ram(void);   /* src/platform/snes/glue.asm: 816-tcc's start-up clears only one file's variables */
#endif

/* One run of the game from the title: n ticks of the input script for player 1,
 * each reported as its state plus state_ofs. */
static void run_ticks(uint16_t n, uint8_t state_ofs)
{
    uint16_t pad[NUM_PLAYERS];
    uint16_t t;
    uint16_t h;
    uint8_t state;

    pad[1] = 0;
    for (t = 0; t < n; t++) {
        state = (uint8_t)g_state;
        pad[0] = script(t, state);
        PORT_WRITE(0x21FC, (uint8_t)(state + state_ofs));
        game_tick(pad);
        game_vblank();
        PORT_WRITE(0x21FF, 0);
        h = hash_bytes(0, oamMemory, (uint16_t)sizeof oamMemory);
        put_hash(h);
        h = hash_bytes(0, (const uint8_t *)game_bg(0), (uint16_t)(BG_W * BG0_ROWS * 2u));
        put_hash(h);
        h = hash_bytes(0, (const uint8_t *)game_bg(1), (uint16_t)(BG_W * BG_H * 2u));
        put_hash(h);
        PORT_WRITE(0x21FA, 0);   /* tick written */
    }
}

int main(void)
{
#ifdef BENCH_TCC
    snes_clear_ram();
#endif
#ifndef BENCH_PC
    PORT_WRITE(0x4200, 0);   /* no interrupts */
#endif
#ifdef BENCH_FASTROM
    PORT_WRITE(0x420D, 1);   /* MEMSEL: banks $80-$FF at 3.58 MHz */
#endif
    game_init(0x2B1Du);
    run_ticks(TICKS, 0);
    micro();

    /* Two players, the second one the CPU partner (it joins at Start). */
    game_init(0x2B1Du);
    g_opt.p2_cpu = 1;          /* the release option, not saved: game_init loaded it from SRAM */
    run_ticks(CPU_TICKS, CPU_STATE);
#ifdef BENCH_PC
    return 0;
#else
    PORT_WRITE(0x21FB, 1);
    for (;;) {
    }
    return 0;
#endif
}
