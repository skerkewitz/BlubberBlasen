/*
 * int16check: records the value range of each instrumented expression and
 * writes it to int16_ranges.txt when the program ends (see int16check.sh).
 */
#include <stdio.h>
#include <stdlib.h>

#define SITES 4096

static long          lo[SITES], hi[SITES];
static unsigned long ulo[SITES], uhi[SITES];
static unsigned char seen[SITES], is_unsigned[SITES];

static void report(void)
{
    FILE *f = fopen("int16_ranges.txt", "w");
    int i;
    if (!f) {
        return;
    }
    for (i = 0; i < SITES; i++) {
        if (!seen[i]) continue;
        if (is_unsigned[i]) fprintf(f, "%d U %lu %lu\n", i, ulo[i], uhi[i]);
        else fprintf(f, "%d S %ld %ld\n", i, lo[i], hi[i]);
    }
    fclose(f);
}

static void start(void)
{
    static int done;
    if (!done) {
        done = 1;
        atexit(report);
    }
}

int i16s(int id, long v)
{
    start();
    if (!seen[id]) { seen[id] = 1; lo[id] = hi[id] = v; }
    if (v < lo[id]) lo[id] = v;
    if (v > hi[id]) hi[id] = v;
    return (int)v;
}

unsigned int i16u(int id, unsigned long v)
{
    start();
    is_unsigned[id] = 1;
    if (!seen[id]) { seen[id] = 1; ulo[id] = uhi[id] = v; }
    if (v < ulo[id]) ulo[id] = v;
    if (v > uhi[id]) uhi[id] = v;
    return (unsigned int)v;
}
