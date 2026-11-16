#ifndef BB_TOOLS_PNGREAD_H
#define BB_TOOLS_PNGREAD_H

/*
 * Reading PNG files for the host tools, without libraries: 8-bit RGB,
 * RGBA or palette images, not interlaced (what the asset PNGs are).
 * Pixels come back as 0xRRGGBB; alpha below 128 counts as transparent
 * and becomes PNG_TRANSPARENT, like the magenta key colour.
 */

#include <stdint.h>

#define PNG_MAGENTA     0xFF00FFu   /* the assets' transparent colour */
#define PNG_TRANSPARENT PNG_MAGENTA

typedef struct {
    int       w, h;
    uint32_t *px;          /* w * h pixels, row by row; free with png_free */
} PngImage;

/* Returns 1 on success; on failure prints "<tool>: <path>: <message>" to stderr. */
int  png_load(const char *tool, const char *path, PngImage *img);
void png_free(PngImage *img);

#endif /* BB_TOOLS_PNGREAD_H */
