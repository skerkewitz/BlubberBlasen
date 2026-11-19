/* Bubble separation from Blubber Blasen (src/game/bubble.c, separate()), as a benchmark for
 * dampf's loop optimizations. Floating bubbles push each other apart when they overlap and cling
 * together when close; with 12 to 16 bubbles in a clump this pair loop was the costliest part of
 * the game's tick on the SNES (about a quarter of a frame, measured 2026-11-19).
 *
 * Four variants compute it, each over the same 32 frames of real bubble positions recorded from
 * the game (a clump of 12 to 16 bubbles, Blubber Blasen commit 28f5bd7, bot run, ticks 3061..3092):
 *
 *               struct array (b[i].x, 23 bytes)   one array per field (x[i])
 *   counter     sep_counter_struct  (A, the game) sep_counter_fields  (C)
 *   sorted list sep_list_struct     (B)           sep_list_fields     (D)
 *
 * "counter" walks all slot pairs, skipping the ones not floating (the game today). "sorted list"
 * keeps the floating bubbles' slots sorted by x (an insertion sort, the order kept from frame to
 * frame) and stops a bubble's pairs at the first one STICK_REACH or more to the right: in a clump
 * that leaves about a third of the pairs (measured in the game: 75.5 pairs a tick, 23.0 close on
 * x, 11.9 close on both axes). A and C give the same results, and so do B and D. The list
 * changes the order the pairs are handled in, so A and B may differ; on these 32 frames all four
 * agree, so each variant checks the others.
 *
 * Measured with dampf 0.5.2 (bench_game.c beside this file: a superdevkit game whose tick loads
 * one frame and runs one variant; the work per tick minus that of loading alone, averaged over
 * the 32 frames), in master cycles per frame of data (a frame of the SNES has 357,368):
 *
 *   A counter, struct array (the game)    93,999   0.263 frames
 *   B sorted list, struct array           93,763   0.262 frames
 *   C counter, one array per field       109,381   0.306 frames
 *   D sorted list, one array per field    94,843   0.265 frames
 *
 * So today neither the list (about a third of the pairs) nor one array per field pays: what they
 * save, the code around them costs. What the variants show of dampf (see the game's
 * docs/kit-findings.md, finding 9):
 *   - b[i] with i from a list multiplies i by 23 each time (B against A);
 *   - one array per field indexes without multiplying (C and D), DAMPF_SPLIT's case;
 *   - the counted loop recomputes i * 23 each step instead of adding 23 (induction variables);
 *   - every intermediate value goes through the direct page (register allocation);
 *   - small loops over bytes with 8-bit counters (slow-u8-counter).
 * The goal for those optimizations: D clearly the fastest, and A close to C.
 */
#include <stdint.h>

#define BUBBLES     16
#define FRAMES      32
#define BS_FLOAT    2
#define BS_TRAPPED  3
#define PUSH_REACH  13          /* closer than this: push apart */
#define STICK_REACH 24          /* closer than this: cling together */
#define STICK_PULL  64          /* 0.25 px a frame (8.8) */
#define TOP_LIMIT   22          /* the room's limits for a bubble's top left */
#define BOTTOM_LIMIT 202
#define WRAP_Y      224
#define MIN_CX      22          /* a bubble's centre x stays within MIN_CX..MAX_CX */
#define MAX_CX      234
#define PX(v)       ((v) >> 8)

/* The game's Bubble, field for field: 23 bytes. */
typedef struct {
    uint8_t  state, dir, angry, anim, trapped, etype, kind, letter, shot_frames, owner;
    int16_t  shot_speed, vx, vy;
    uint8_t  pop_timer;
    uint16_t age;
    uint16_t x, y;
} Bubble;

/* The 16 slots in each of the 32 frames: state, x low, x high, y low, y high (x, y in 8.8). */
static const uint8_t frames[FRAMES][BUBBLES][5] = {
    {{2,247,37,155,68},{2,251,24,255,69},{2,239,166,183,84},{2,243,153,147,77},{2,63,130,112,26},{2,193,131,98,39},{2,178,142,116,39},{2,21,88,79,36},{2,219,82,91,47},{2,0,14,135,122},{2,242,110,212,138},{2,0,14,12,135},{2,160,108,200,195},{3,199,70,111,197},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,65,68},{2,251,24,165,69},{2,239,166,93,84},{2,243,153,57,77},{2,229,129,112,26},{2,161,131,188,39},{2,120,142,116,39},{2,111,88,47,36},{2,219,82,33,47},{2,0,14,29,122},{2,152,110,212,138},{2,0,14,194,134},{2,160,108,110,195},{3,199,70,34,197},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,231,67},{2,251,24,75,69},{2,239,166,3,84},{2,243,153,223,76},{2,139,129,176,26},{2,129,131,214,39},{2,62,142,116,39},{2,201,88,255,35},{2,219,82,247,46},{2,0,14,195,121},{2,62,110,212,138},{2,0,14,104,134},{2,160,108,20,195},{3,199,70,213,196},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,141,67},{2,251,24,241,68},{2,239,166,169,83},{2,243,153,133,76},{2,49,129,240,26},{2,81,131,240,39},{2,20,142,116,39},{2,35,89,223,35},{2,219,82,189,46},{2,0,14,105,121},{2,228,109,212,138},{2,0,14,14,134},{2,160,108,186,194},{3,199,70,136,196},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,51,67},{2,251,24,151,68},{2,239,166,79,83},{2,243,153,43,76},{2,215,128,48,27},{2,33,131,10,40},{2,234,141,116,39},{2,125,89,191,35},{2,219,82,131,46},{2,0,14,255,120},{2,138,109,212,138},{2,0,14,196,133},{2,160,108,96,194},{3,199,70,59,196},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,217,66},{2,251,24,61,68},{2,239,166,245,82},{2,243,153,209,75},{2,125,128,48,27},{2,241,130,100,40},{2,192,141,116,39},{2,215,89,159,35},{2,219,82,73,46},{2,0,14,165,120},{2,48,109,212,138},{2,0,14,106,133},{2,160,108,6,194},{3,199,70,238,195},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,127,66},{2,251,24,227,67},{2,239,166,155,82},{2,243,153,119,75},{2,35,128,48,27},{2,209,130,190,40},{2,134,141,116,39},{2,49,90,111,35},{2,219,82,31,46},{2,0,14,75,120},{2,214,108,212,138},{2,0,14,16,133},{2,160,108,172,193},{3,199,70,161,195},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,37,66},{2,251,24,137,67},{2,239,166,65,82},{2,243,153,29,75},{2,9,128,112,27},{2,161,130,216,40},{2,28,141,116,39},{2,139,90,63,35},{2,219,82,245,45},{2,0,14,241,119},{2,124,108,212,138},{2,0,14,182,132},{2,160,108,82,193},{3,199,70,84,195},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,203,65},{2,251,24,47,67},{2,239,166,231,81},{2,243,153,195,74},{2,175,127,176,27},{2,113,130,242,40},{2,242,140,116,39},{2,229,90,15,35},{2,219,82,203,45},{2,0,14,151,119},{2,34,108,212,138},{2,0,14,92,132},{2,160,108,248,192},{3,199,70,7,195},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,113,65},{2,251,24,213,66},{2,239,166,141,81},{2,243,153,105,74},{2,85,127,240,27},{2,65,130,12,41},{2,200,140,116,39},{2,63,91,223,34},{2,219,82,161,45},{2,0,14,61,119},{2,200,107,212,138},{2,0,14,2,132},{2,160,108,158,192},{3,199,70,186,194},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,23,65},{2,251,24,123,66},{2,239,166,51,81},{2,243,153,15,74},{2,175,127,48,28},{2,17,130,38,41},{2,158,140,116,39},{2,153,91,191,34},{2,219,82,103,45},{2,0,14,227,118},{2,110,107,212,138},{2,0,14,168,131},{2,160,108,68,192},{3,199,70,109,194},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,189,64},{2,251,24,33,66},{2,239,166,217,80},{2,243,153,181,73},{2,249,127,48,28},{2,225,129,128,41},{2,132,140,116,39},{2,243,91,159,34},{2,219,82,45,45},{2,0,14,137,118},{2,20,107,212,138},{2,0,14,78,131},{2,160,108,234,191},{3,199,70,32,194},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,99,64},{2,251,24,199,65},{2,239,166,127,80},{2,243,153,91,73},{2,67,128,48,28},{2,193,129,218,41},{2,90,140,116,39},{2,125,92,159,34},{2,171,82,211,44},{2,0,14,31,118},{2,186,106,212,138},{2,0,14,4,131},{2,160,108,144,191},{3,199,70,211,193},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,9,64},{2,251,24,109,65},{2,239,166,37,80},{2,243,153,1,73},{2,141,128,112,28},{2,161,129,244,41},{2,48,140,116,39},{2,7,93,159,34},{2,123,82,121,44},{2,0,14,197,117},{2,96,106,212,138},{2,0,14,170,130},{2,160,108,54,191},{3,199,70,134,193},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,175,63},{2,251,24,19,65},{2,239,166,203,79},{2,243,153,167,72},{2,199,128,112,28},{2,113,129,154,41},{2,38,140,116,39},{2,129,93,159,34},{2,91,82,31,44},{2,0,14,107,117},{2,6,106,212,138},{2,0,14,80,130},{2,160,108,220,190},{3,199,70,57,193},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,85,63},{2,251,24,185,64},{2,239,166,113,79},{2,243,153,77,72},{2,33,129,80,28},{2,65,129,64,41},{2,252,139,148,39},{2,251,93,159,34},{2,59,82,197,43},{2,0,14,1,117},{2,172,105,212,138},{2,0,14,6,130},{2,160,108,130,190},{3,199,70,236,192},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,251,62},{2,251,24,95,64},{2,239,166,23,79},{2,243,153,243,71},{2,123,129,32,28},{2,17,129,246,40},{2,210,139,180,39},{2,101,94,159,34},{2,43,82,107,43},{2,0,14,167,116},{2,82,105,212,138},{2,0,14,172,129},{2,160,108,40,190},{3,199,70,159,192},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,161,62},{2,251,24,5,64},{2,239,166,189,78},{2,243,153,153,71},{2,213,129,240,27},{2,225,128,172,40},{2,168,139,212,39},{2,207,94,159,34},{2,27,82,17,43},{2,0,14,77,116},{2,248,104,212,138},{2,0,14,82,129},{2,160,108,206,189},{3,199,70,82,192},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,71,62},{2,251,24,171,63},{2,239,166,99,78},{2,243,153,63,71},{2,47,130,224,27},{2,193,128,82,40},{2,110,139,228,39},{2,41,95,159,34},{2,27,82,183,42},{2,0,14,243,115},{2,158,104,212,138},{2,0,14,248,128},{2,160,108,116,189},{3,199,70,5,192},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,237,61},{2,251,24,81,63},{2,239,166,9,78},{2,243,153,229,70},{2,137,130,192,27},{2,161,128,8,40},{2,52,139,244,39},{2,131,95,159,34},{2,27,82,93,42},{2,0,14,153,115},{2,68,104,212,138},{2,0,14,158,128},{2,160,108,26,189},{3,199,70,184,191},{0,0,0,0,0},{0,0,0,0,0}},
    {{2,247,37,147,61},{2,251,24,247,62},{2,239,166,175,77},{2,243,153,139,70},{2,227,130,160,27},{2,113,128,190,39},{2,10,139,4,40},{2,221,95,159,34},{2,27,82,3,42},{2,0,14,63,115},{2,234,103,212,138},{2,0,14,68,128},{2,160,108,192,188},{3,199,70,107,191},{1,6,58,175,173},{0,0,0,0,0}},
    {{2,247,37,57,61},{2,251,24,157,62},{2,239,166,85,77},{2,243,153,49,70},{2,61,131,144,27},{2,65,128,116,39},{2,224,138,4,40},{2,247,95,159,34},{2,91,82,169,41},{2,0,14,229,114},{2,144,103,212,138},{2,0,14,234,127},{2,160,108,102,188},{3,199,70,30,191},{1,6,54,175,173},{0,0,0,0,0}},
    {{2,247,37,223,60},{2,251,24,67,62},{2,239,166,251,76},{2,243,153,215,69},{2,151,131,128,27},{2,17,128,42,39},{2,182,138,4,40},{2,17,96,159,34},{2,155,82,79,41},{2,0,14,139,114},{2,54,103,212,138},{2,0,14,144,127},{2,160,108,12,188},{3,199,70,209,190},{1,6,50,175,173},{0,0,0,0,0}},
    {{2,247,37,133,60},{2,251,24,233,61},{2,239,166,161,76},{2,243,153,125,69},{2,241,131,96,27},{2,225,127,240,38},{2,140,138,4,40},{2,43,96,159,34},{2,219,82,245,40},{2,0,14,49,114},{2,220,102,212,138},{2,0,14,54,127},{2,160,108,178,187},{3,199,70,132,190},{1,6,46,175,173},{0,0,0,0,0}},
    {{2,247,37,43,60},{2,251,24,143,61},{2,239,166,71,76},{2,243,153,35,69},{2,75,132,64,27},{2,193,127,182,38},{2,82,138,4,40},{2,69,96,159,34},{2,27,83,155,40},{2,0,14,215,113},{2,130,102,212,138},{2,0,14,220,126},{2,160,108,88,187},{3,199,70,55,190},{1,6,42,175,173},{0,0,0,0,0}},
    {{2,247,37,209,59},{2,251,24,53,61},{2,239,166,237,75},{2,243,153,201,68},{2,165,132,32,27},{2,145,127,124,38},{2,40,138,4,40},{2,159,96,159,34},{2,27,83,65,40},{2,0,14,125,113},{2,40,102,212,138},{2,0,14,130,126},{2,160,108,254,186},{3,199,70,234,189},{1,6,38,175,173},{0,0,0,0,0}},
    {{2,247,37,119,59},{2,251,24,219,60},{2,239,166,147,75},{2,243,153,111,68},{2,255,132,0,27},{2,97,127,66,38},{2,254,137,4,40},{2,249,96,159,34},{2,27,83,231,39},{2,0,14,35,113},{2,206,101,212,138},{2,0,14,40,126},{2,160,108,164,186},{0,199,70,234,189},{1,6,34,175,173},{0,0,0,0,0}},
    {{2,247,37,29,59},{2,251,24,129,60},{2,239,166,57,75},{2,243,153,21,68},{2,89,133,16,27},{2,49,127,24,38},{2,212,137,196,39},{2,19,97,159,34},{2,91,83,141,39},{2,0,14,201,112},{2,116,101,212,138},{2,0,14,206,125},{2,160,108,74,186},{0,199,70,234,189},{1,6,30,175,173},{0,0,0,0,0}},
    {{2,247,37,195,58},{2,251,24,39,60},{2,239,166,223,74},{2,243,153,187,67},{2,179,133,224,26},{2,1,127,238,37},{2,170,137,196,39},{2,45,97,159,34},{2,155,83,51,39},{2,0,14,111,112},{2,26,101,212,138},{2,0,14,116,125},{2,160,108,240,185},{0,199,70,234,189},{2,6,26,175,173},{0,0,0,0,0}},
    {{2,247,37,105,58},{2,251,24,205,59},{2,239,166,133,74},{2,243,153,97,67},{2,13,134,192,26},{2,209,126,180,37},{2,128,137,196,39},{2,71,97,159,34},{2,219,83,217,38},{2,0,14,21,112},{2,192,100,212,138},{2,0,14,26,125},{2,160,108,150,185},{0,199,70,234,189},{2,6,26,85,173},{0,0,0,0,0}},
    {{2,247,37,15,58},{2,251,24,115,59},{2,239,166,43,74},{2,243,153,7,67},{2,103,134,160,26},{2,251,126,212,37},{2,86,137,196,39},{2,97,97,159,34},{2,27,84,127,38},{2,0,14,187,111},{2,102,100,212,138},{2,0,14,192,124},{2,160,108,60,185},{0,199,70,234,189},{2,6,26,251,172},{0,0,0,0,0}},
    {{2,247,37,181,57},{2,251,24,25,59},{2,239,166,209,73},{2,243,153,173,66},{2,193,134,128,26},{2,21,127,244,37},{2,60,137,196,39},{2,187,97,159,34},{2,27,84,37,38},{2,0,14,97,111},{2,12,100,212,138},{2,0,14,102,124},{2,160,108,226,184},{0,199,70,234,189},{2,6,26,161,172},{0,0,0,0,0}},
};

/* --- the struct array (variants A and B) ------------------------------------------------ */
Bubble bub[BUBBLES];

static uint8_t floating_s(uint8_t i)
{
    return (uint8_t)(bub[i].state == BS_FLOAT || bub[i].state == BS_TRAPPED);
}

static int16_t abs16(int16_t v)
{
    return (int16_t)(v < 0 ? -v : v);
}

static void clamp_s(uint8_t i)
{
    uint8_t cx = (uint8_t)(PX(bub[i].x) + 8);
    if (cx < MIN_CX) {
        bub[i].x = (uint16_t)((MIN_CX - 8) << 8);
    } else if (cx > MAX_CX) {
        bub[i].x = (uint16_t)((MAX_CX - 8) << 8);
    }
    if (PX(bub[i].y) < TOP_LIMIT || PX(bub[i].y) >= WRAP_Y) {
        bub[i].y = (uint16_t)(TOP_LIMIT << 8);
    } else if (PX(bub[i].y) > BOTTOM_LIMIT) {
        bub[i].y = (uint16_t)(BOTTOM_LIMIT << 8);
    }
}

/* One pair, slots i and j (the game's code, as a function as in the list variant tried in the
 * game): cling when close, push apart when overlapping. */
static void pair_s(uint8_t i, uint8_t j)
{
    int16_t dx = (int16_t)((int16_t)PX(bub[i].x) - (int16_t)PX(bub[j].x));
    if ((uint16_t)(dx + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
        return;
    }
    int16_t dy = (int16_t)((int16_t)PX(bub[i].y) - (int16_t)PX(bub[j].y));
    if ((uint16_t)(dy + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
        return;
    }
    int16_t adx = abs16(dx);
    int16_t ady = abs16(dy);
    if (adx >= PUSH_REACH || ady >= PUSH_REACH) {
        if (adx > PUSH_REACH) {
            bub[i].x = (uint16_t)(dx > 0 ? bub[i].x - STICK_PULL : bub[i].x + STICK_PULL);
            bub[j].x = (uint16_t)(dx > 0 ? bub[j].x + STICK_PULL : bub[j].x - STICK_PULL);
        }
        if (ady > PUSH_REACH) {
            bub[i].y = (uint16_t)(dy > 0 ? bub[i].y - STICK_PULL : bub[i].y + STICK_PULL);
            bub[j].y = (uint16_t)(dy > 0 ? bub[j].y + STICK_PULL : bub[j].y - STICK_PULL);
        }
        clamp_s(i);
        clamp_s(j);
        return;
    }
    if (adx >= ady) {
        uint16_t push = (uint16_t)((PUSH_REACH - adx) << 4);
        if (dx > 0 || (dx == 0 && (i & 1))) {
            bub[i].x = (uint16_t)(bub[i].x + push);
            bub[j].x = (uint16_t)(bub[j].x - push);
        } else {
            bub[i].x = (uint16_t)(bub[i].x - push);
            bub[j].x = (uint16_t)(bub[j].x + push);
        }
    } else {
        uint16_t push = (uint16_t)((PUSH_REACH - ady) << 4);
        if (dy > 0) {
            bub[i].y = (uint16_t)(bub[i].y + push);
            bub[j].y = (uint16_t)(bub[j].y - push);
        } else {
            bub[i].y = (uint16_t)(bub[i].y - push);
            bub[j].y = (uint16_t)(bub[j].y + push);
        }
    }
    clamp_s(i);
    clamp_s(j);
}

/* A: the game today (bubble.c at commit 28f5bd7), every slot pair with a counter, the pair's
 * code inline in the loop through the pointers a and b. */
void sep_counter_struct(void)
{
    for (uint8_t i = 0; i < BUBBLES; i++) {
        Bubble *a = &bub[i];
        if (!(a->state == BS_FLOAT || a->state == BS_TRAPPED)) {
            continue;
        }
        for (uint8_t j = (uint8_t)(i + 1); j < BUBBLES; j++) {
            Bubble *b = &bub[j];
            if (!(b->state == BS_FLOAT || b->state == BS_TRAPPED)) {
                continue;
            }
            int16_t dx = (int16_t)((int16_t)PX(a->x) - (int16_t)PX(b->x));
            if ((uint16_t)(dx + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
                continue;
            }
            int16_t dy = (int16_t)((int16_t)PX(a->y) - (int16_t)PX(b->y));
            if ((uint16_t)(dy + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
                continue;
            }
            int16_t adx = abs16(dx);
            int16_t ady = abs16(dy);
            if (adx >= PUSH_REACH || ady >= PUSH_REACH) {
                if (adx > PUSH_REACH) {
                    a->x = (uint16_t)(dx > 0 ? a->x - STICK_PULL : a->x + STICK_PULL);
                    b->x = (uint16_t)(dx > 0 ? b->x + STICK_PULL : b->x - STICK_PULL);
                }
                if (ady > PUSH_REACH) {
                    a->y = (uint16_t)(dy > 0 ? a->y - STICK_PULL : a->y + STICK_PULL);
                    b->y = (uint16_t)(dy > 0 ? b->y + STICK_PULL : b->y - STICK_PULL);
                }
                clamp_s(i);
                clamp_s(j);
                continue;
            }
            if (adx >= ady) {
                uint16_t push = (uint16_t)((PUSH_REACH - adx) << 4);
                if (dx > 0 || (dx == 0 && (i & 1))) {
                    a->x = (uint16_t)(a->x + push);
                    b->x = (uint16_t)(b->x - push);
                } else {
                    a->x = (uint16_t)(a->x - push);
                    b->x = (uint16_t)(b->x + push);
                }
            } else {
                uint16_t push = (uint16_t)((PUSH_REACH - ady) << 4);
                if (dy > 0) {
                    a->y = (uint16_t)(a->y + push);
                    b->y = (uint16_t)(b->y - push);
                } else {
                    a->y = (uint16_t)(a->y - push);
                    b->y = (uint16_t)(b->y + push);
                }
            }
            clamp_s(i);
            clamp_s(j);
        }
    }
}

/* B: the floating bubbles sorted by x, pairs only within STICK_REACH on x. */
static uint8_t list_s_slot[BUBBLES], list_s_x[BUBBLES], list_s_n;

void sep_list_struct(void)
{
    uint8_t listed[BUBBLES];
    uint8_t n = 0;
    for (uint8_t s = 0; s < BUBBLES; s++) {
        listed[s] = 0;
    }
    for (uint8_t k = 0; k < list_s_n; k++) {      /* last frame's order, without the gone */
        uint8_t s = list_s_slot[k];
        if (floating_s(s)) {
            list_s_slot[n] = s;
            list_s_x[n] = (uint8_t)PX(bub[s].x);
            n++;
            listed[s] = 1;
        }
    }
    for (uint8_t s = 0; s < BUBBLES; s++) {        /* then the new ones */
        if (!listed[s] && floating_s(s)) {
            list_s_slot[n] = s;
            list_s_x[n] = (uint8_t)PX(bub[s].x);
            n++;
        }
    }
    list_s_n = n;
    for (uint8_t k = 1; k < n; k++) {              /* insertion sort by x */
        uint8_t x = list_s_x[k], s = list_s_slot[k];
        uint8_t m = k;
        while (m > 0 && list_s_x[m - 1] > x) {
            list_s_x[m] = list_s_x[m - 1];
            list_s_slot[m] = list_s_slot[m - 1];
            m--;
        }
        list_s_x[m] = x;
        list_s_slot[m] = s;
    }
    for (uint8_t k = 0; k < n; k++) {
        uint8_t ax = list_s_x[k];
        for (uint8_t l = (uint8_t)(k + 1); l < n; l++) {
            if ((uint8_t)(list_s_x[l] - ax) >= STICK_REACH) {
                break;
            }
            pair_s(list_s_slot[k], list_s_slot[l]);
        }
    }
}

/* --- one array per field (variants C and D) ---------------------------------------------- */
uint8_t  fld_state[BUBBLES];
uint16_t fld_x[BUBBLES], fld_y[BUBBLES];

static uint8_t floating_f(uint8_t i)
{
    return (uint8_t)(fld_state[i] == BS_FLOAT || fld_state[i] == BS_TRAPPED);
}

static void clamp_f(uint8_t i)
{
    uint8_t cx = (uint8_t)(PX(fld_x[i]) + 8);
    if (cx < MIN_CX) {
        fld_x[i] = (uint16_t)((MIN_CX - 8) << 8);
    } else if (cx > MAX_CX) {
        fld_x[i] = (uint16_t)((MAX_CX - 8) << 8);
    }
    if (PX(fld_y[i]) < TOP_LIMIT || PX(fld_y[i]) >= WRAP_Y) {
        fld_y[i] = (uint16_t)(TOP_LIMIT << 8);
    } else if (PX(fld_y[i]) > BOTTOM_LIMIT) {
        fld_y[i] = (uint16_t)(BOTTOM_LIMIT << 8);
    }
}

static void pair_f(uint8_t i, uint8_t j)
{
    int16_t dx = (int16_t)((int16_t)PX(fld_x[i]) - (int16_t)PX(fld_x[j]));
    if ((uint16_t)(dx + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
        return;
    }
    int16_t dy = (int16_t)((int16_t)PX(fld_y[i]) - (int16_t)PX(fld_y[j]));
    if ((uint16_t)(dy + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
        return;
    }
    int16_t adx = abs16(dx);
    int16_t ady = abs16(dy);
    if (adx >= PUSH_REACH || ady >= PUSH_REACH) {
        if (adx > PUSH_REACH) {
            fld_x[i] = (uint16_t)(dx > 0 ? fld_x[i] - STICK_PULL : fld_x[i] + STICK_PULL);
            fld_x[j] = (uint16_t)(dx > 0 ? fld_x[j] + STICK_PULL : fld_x[j] - STICK_PULL);
        }
        if (ady > PUSH_REACH) {
            fld_y[i] = (uint16_t)(dy > 0 ? fld_y[i] - STICK_PULL : fld_y[i] + STICK_PULL);
            fld_y[j] = (uint16_t)(dy > 0 ? fld_y[j] + STICK_PULL : fld_y[j] - STICK_PULL);
        }
        clamp_f(i);
        clamp_f(j);
        return;
    }
    if (adx >= ady) {
        uint16_t push = (uint16_t)((PUSH_REACH - adx) << 4);
        if (dx > 0 || (dx == 0 && (i & 1))) {
            fld_x[i] = (uint16_t)(fld_x[i] + push);
            fld_x[j] = (uint16_t)(fld_x[j] - push);
        } else {
            fld_x[i] = (uint16_t)(fld_x[i] - push);
            fld_x[j] = (uint16_t)(fld_x[j] + push);
        }
    } else {
        uint16_t push = (uint16_t)((PUSH_REACH - ady) << 4);
        if (dy > 0) {
            fld_y[i] = (uint16_t)(fld_y[i] + push);
            fld_y[j] = (uint16_t)(fld_y[j] - push);
        } else {
            fld_y[i] = (uint16_t)(fld_y[i] - push);
            fld_y[j] = (uint16_t)(fld_y[j] + push);
        }
    }
    clamp_f(i);
    clamp_f(j);
}

/* C: as A, with one array per field (the pair's code inline). */
void sep_counter_fields(void)
{
    for (uint8_t i = 0; i < BUBBLES; i++) {
        if (!(fld_state[i] == BS_FLOAT || fld_state[i] == BS_TRAPPED)) {
            continue;
        }
        for (uint8_t j = (uint8_t)(i + 1); j < BUBBLES; j++) {
            if (!(fld_state[j] == BS_FLOAT || fld_state[j] == BS_TRAPPED)) {
                continue;
            }
            int16_t dx = (int16_t)((int16_t)PX(fld_x[i]) - (int16_t)PX(fld_x[j]));
            if ((uint16_t)(dx + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
                continue;
            }
            int16_t dy = (int16_t)((int16_t)PX(fld_y[i]) - (int16_t)PX(fld_y[j]));
            if ((uint16_t)(dy + (STICK_REACH - 1)) > 2 * (STICK_REACH - 1)) {
                continue;
            }
            int16_t adx = abs16(dx);
            int16_t ady = abs16(dy);
            if (adx >= PUSH_REACH || ady >= PUSH_REACH) {
                if (adx > PUSH_REACH) {
                    fld_x[i] = (uint16_t)(dx > 0 ? fld_x[i] - STICK_PULL : fld_x[i] + STICK_PULL);
                    fld_x[j] = (uint16_t)(dx > 0 ? fld_x[j] + STICK_PULL : fld_x[j] - STICK_PULL);
                }
                if (ady > PUSH_REACH) {
                    fld_y[i] = (uint16_t)(dy > 0 ? fld_y[i] - STICK_PULL : fld_y[i] + STICK_PULL);
                    fld_y[j] = (uint16_t)(dy > 0 ? fld_y[j] + STICK_PULL : fld_y[j] - STICK_PULL);
                }
                clamp_f(i);
                clamp_f(j);
                continue;
            }
            if (adx >= ady) {
                uint16_t push = (uint16_t)((PUSH_REACH - adx) << 4);
                if (dx > 0 || (dx == 0 && (i & 1))) {
                    fld_x[i] = (uint16_t)(fld_x[i] + push);
                    fld_x[j] = (uint16_t)(fld_x[j] - push);
                } else {
                    fld_x[i] = (uint16_t)(fld_x[i] - push);
                    fld_x[j] = (uint16_t)(fld_x[j] + push);
                }
            } else {
                uint16_t push = (uint16_t)((PUSH_REACH - ady) << 4);
                if (dy > 0) {
                    fld_y[i] = (uint16_t)(fld_y[i] + push);
                    fld_y[j] = (uint16_t)(fld_y[j] - push);
                } else {
                    fld_y[i] = (uint16_t)(fld_y[i] - push);
                    fld_y[j] = (uint16_t)(fld_y[j] + push);
                }
            }
            clamp_f(i);
            clamp_f(j);
        }
    }
}

/* D: as B, with one array per field. */
static uint8_t list_f_slot[BUBBLES], list_f_x[BUBBLES], list_f_n;

void sep_list_fields(void)
{
    uint8_t listed[BUBBLES];
    uint8_t n = 0;
    for (uint8_t s = 0; s < BUBBLES; s++) {
        listed[s] = 0;
    }
    for (uint8_t k = 0; k < list_f_n; k++) {
        uint8_t s = list_f_slot[k];
        if (floating_f(s)) {
            list_f_slot[n] = s;
            list_f_x[n] = (uint8_t)PX(fld_x[s]);
            n++;
            listed[s] = 1;
        }
    }
    for (uint8_t s = 0; s < BUBBLES; s++) {
        if (!listed[s] && floating_f(s)) {
            list_f_slot[n] = s;
            list_f_x[n] = (uint8_t)PX(fld_x[s]);
            n++;
        }
    }
    list_f_n = n;
    for (uint8_t k = 1; k < n; k++) {
        uint8_t x = list_f_x[k], s = list_f_slot[k];
        uint8_t m = k;
        while (m > 0 && list_f_x[m - 1] > x) {
            list_f_x[m] = list_f_x[m - 1];
            list_f_slot[m] = list_f_slot[m - 1];
            m--;
        }
        list_f_x[m] = x;
        list_f_slot[m] = s;
    }
    for (uint8_t k = 0; k < n; k++) {
        uint8_t ax = list_f_x[k];
        for (uint8_t l = (uint8_t)(k + 1); l < n; l++) {
            if ((uint8_t)(list_f_x[l] - ax) >= STICK_REACH) {
                break;
            }
            pair_f(list_f_slot[k], list_f_slot[l]);
        }
    }
}

/* --- the run ------------------------------------------------------------------------------ */
uint32_t check_a, check_b, check_c, check_d;   /* FNV-1a of the positions after each frame */
uint8_t  a_equals_c, b_equals_d;               /* must both be 1 */

static uint32_t fnv16(uint32_t h, uint16_t v)
{
    h = (h ^ (uint8_t)v) * 16777619u;
    return (h ^ (uint8_t)(v >> 8)) * 16777619u;
}

static void load_struct(uint8_t f)
{
    for (uint8_t i = 0; i < BUBBLES; i++) {
        bub[i].state = frames[f][i][0];
        bub[i].x = (uint16_t)(frames[f][i][1] | (frames[f][i][2] << 8));
        bub[i].y = (uint16_t)(frames[f][i][3] | (frames[f][i][4] << 8));
    }
}

static void load_fields(uint8_t f)
{
    for (uint8_t i = 0; i < BUBBLES; i++) {
        fld_state[i] = frames[f][i][0];
        fld_x[i] = (uint16_t)(frames[f][i][1] | (frames[f][i][2] << 8));
        fld_y[i] = (uint16_t)(frames[f][i][3] | (frames[f][i][4] << 8));
    }
}

static uint32_t hash_struct(uint32_t h)
{
    for (uint8_t i = 0; i < BUBBLES; i++) {
        h = fnv16(fnv16(h, bub[i].x), bub[i].y);
    }
    return h;
}

static uint32_t hash_fields(uint32_t h)
{
    for (uint8_t i = 0; i < BUBBLES; i++) {
        h = fnv16(fnv16(h, fld_x[i]), fld_y[i]);
    }
    return h;
}

/* One frame of the data through all four variants. */
void bench_frame(uint8_t f)
{
    load_struct(f);
    sep_counter_struct();
    check_a = hash_struct(check_a);
    load_struct(f);
    sep_list_struct();
    check_b = hash_struct(check_b);
    load_fields(f);
    sep_counter_fields();
    check_c = hash_fields(check_c);
    load_fields(f);
    sep_list_fields();
    check_d = hash_fields(check_d);
}

void test_main(void)
{
    check_a = check_b = check_c = check_d = 2166136261u;
    for (uint8_t f = 0; f < FRAMES; f++) {
        bench_frame(f);
    }
    a_equals_c = (uint8_t)(check_a == check_c);
    b_equals_d = (uint8_t)(check_b == check_d);
}
