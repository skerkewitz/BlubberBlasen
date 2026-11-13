#ifndef BB_FIXED_H
#define BB_FIXED_H

#include <stdint.h>

/*
 * 8.8 fixed point: high byte = integer pixels, low byte = 1/256 pixel.
 * Range: -128.0 .. +127.996. Use for velocities and sub-pixel offsets.
 * Positions on screen (0..255) are stored as fx8_8 in a uint16_t-sized
 * field interpreted as unsigned 8.8 (see Player in game.c).
 */
typedef int16_t fx8_8;

#define FX(i, frac256)   ((fx8_8)((i) * 256 + (frac256)))   /* constant-folded */
#define FX_INT(v)        ((int16_t)((v) >> 8))
#define FX_FROM_INT(i)   ((fx8_8)((i) * 256))

#endif /* BB_FIXED_H */
