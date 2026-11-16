/*
 * PNG reader for the host tools (see pngread.h). Includes a small inflate
 * (RFC 1951: stored, fixed and dynamic Huffman blocks), written for clarity,
 * not speed: the asset images are small.
 */

#include "common/pngread.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- Inflate ------------------------------------------------------------------- */
typedef struct {
    const uint8_t *in;
    size_t         in_len, pos;
    uint32_t       bits;
    int            nbits;
    uint8_t       *out;
    size_t         out_len, out_cap;
    int            error;
} Inflate;

static int getbits(Inflate *s, int n)
{
    uint32_t v;
    while (s->nbits < n) {
        if (s->pos >= s->in_len) {
            s->error = 1;
            return 0;
        }
        s->bits |= (uint32_t)s->in[s->pos++] << s->nbits;
        s->nbits += 8;
    }
    v = s->bits & ((1u << n) - 1u);
    s->bits >>= n;
    s->nbits -= n;
    return (int)v;
}

static void put(Inflate *s, uint8_t b)
{
    if (s->out_len == s->out_cap) {
        size_t cap = s->out_cap ? s->out_cap * 2 : 65536;
        uint8_t *n = (uint8_t *)realloc(s->out, cap);
        if (!n) {
            s->error = 1;
            return;
        }
        s->out = n;
        s->out_cap = cap;
    }
    s->out[s->out_len++] = b;
}

typedef struct {
    short count[16];      /* codes per length */
    short symbol[320];    /* symbols ordered by code */
} Huffman;

static void build(Huffman *h, const short *lengths, int n)
{
    short offs[16];
    int i;
    memset(h->count, 0, sizeof h->count);
    for (i = 0; i < n; i++) {
        h->count[lengths[i]]++;
    }
    h->count[0] = 0;
    offs[1] = 0;
    for (i = 1; i < 15; i++) {
        offs[i + 1] = (short)(offs[i] + h->count[i]);
    }
    for (i = 0; i < n; i++) {
        if (lengths[i]) {
            h->symbol[offs[lengths[i]]++] = (short)i;
        }
    }
}

static int decode(Inflate *s, const Huffman *h)
{
    int code = 0, first = 0, index = 0, len;
    for (len = 1; len < 16; len++) {
        code |= getbits(s, 1);
        if (s->error) {
            return -1;
        }
        if (code - h->count[len] < first) {
            return h->symbol[index + (code - first)];
        }
        index += h->count[len];
        first += h->count[len];
        first <<= 1;
        code <<= 1;
    }
    s->error = 1;
    return -1;
}

static const short len_base[29] = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258 };
static const short len_extra[29] = { 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0 };
static const short dist_base[30] = { 1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577 };
static const short dist_extra[30] = { 0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13 };

static void codes(Inflate *s, const Huffman *lit, const Huffman *dist)
{
    for (;;) {
        int sym = decode(s, lit);
        if (s->error) {
            return;
        }
        if (sym < 256) {
            put(s, (uint8_t)sym);
        } else if (sym == 256) {
            return;
        } else {
            int len, d;
            sym -= 257;
            if (sym >= 29) {
                s->error = 1;
                return;
            }
            len = len_base[sym] + getbits(s, len_extra[sym]);
            d = decode(s, dist);
            if (s->error || d >= 30) {
                s->error = 1;
                return;
            }
            d = dist_base[d] + getbits(s, dist_extra[d]);
            if ((size_t)d > s->out_len) {
                s->error = 1;
                return;
            }
            while (len--) {
                put(s, s->out[s->out_len - (size_t)d]);
            }
        }
    }
}

static void fixed_block(Inflate *s)
{
    short lengths[288];
    Huffman lit, dist;
    int i;
    for (i = 0; i < 144; i++) lengths[i] = 8;
    for (; i < 256; i++) lengths[i] = 9;
    for (; i < 280; i++) lengths[i] = 7;
    for (; i < 288; i++) lengths[i] = 8;
    build(&lit, lengths, 288);
    for (i = 0; i < 30; i++) lengths[i] = 5;
    build(&dist, lengths, 30);
    codes(s, &lit, &dist);
}

static void dynamic_block(Inflate *s)
{
    static const short order[19] = { 16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15 };
    short lengths[320];
    Huffman lit, dist, lencode;
    int nlen = getbits(s, 5) + 257, ndist = getbits(s, 5) + 1, ncode = getbits(s, 4) + 4, i = 0;
    memset(lengths, 0, sizeof lengths);
    for (i = 0; i < ncode; i++) {
        lengths[order[i]] = (short)getbits(s, 3);
    }
    build(&lencode, lengths, 19);
    i = 0;
    while (i < nlen + ndist && !s->error) {
        int sym = decode(s, &lencode), rep = 0;
        short v = 0;
        if (sym < 16) {
            lengths[i++] = (short)sym;
            continue;
        }
        if (sym == 16) {
            if (i == 0) {
                s->error = 1;
                return;
            }
            v = lengths[i - 1];
            rep = 3 + getbits(s, 2);
        } else if (sym == 17) {
            rep = 3 + getbits(s, 3);
        } else {
            rep = 11 + getbits(s, 7);
        }
        if (i + rep > nlen + ndist) {
            s->error = 1;
            return;
        }
        while (rep--) {
            lengths[i++] = v;
        }
    }
    build(&lit, lengths, nlen);
    build(&dist, lengths + nlen, ndist);
    codes(s, &lit, &dist);
}

static int inflate_zlib(const uint8_t *in, size_t len, uint8_t **out, size_t *out_len)
{
    Inflate s;
    int last;
    memset(&s, 0, sizeof s);
    if (len < 2 || (in[0] & 0x0F) != 8) {
        return 0;
    }
    s.in = in + 2;   /* zlib header; the checksum at the end is not checked */
    s.in_len = len - 2;
    do {
        int type;
        last = getbits(&s, 1);
        type = getbits(&s, 2);
        if (type == 0) {
            unsigned n;
            s.bits = 0;
            s.nbits = 0;
            if (s.pos + 4 > s.in_len) {
                s.error = 1;
                break;
            }
            n = (unsigned)(s.in[s.pos] | (s.in[s.pos + 1] << 8));
            s.pos += 4;
            if (s.pos + n > s.in_len) {
                s.error = 1;
                break;
            }
            while (n--) {
                put(&s, s.in[s.pos++]);
            }
        } else if (type == 1) {
            fixed_block(&s);
        } else if (type == 2) {
            dynamic_block(&s);
        } else {
            s.error = 1;
        }
    } while (!last && !s.error);
    if (s.error) {
        free(s.out);
        return 0;
    }
    *out = s.out;
    *out_len = s.out_len;
    return 1;
}

/* --- PNG --------------------------------------------------------------------- */
static uint32_t be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static int paeth(int a, int b, int c)
{
    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
    return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

#define FAIL(msg) do { fprintf(stderr, "%s: %s: %s\n", tool, path, msg); goto fail; } while (0)

int png_load(const char *tool, const char *path, PngImage *img)
{
    static const uint8_t sig[8] = { 137, 80, 78, 71, 13, 10, 26, 10 };
    FILE *f = fopen(path, "rb");
    uint8_t *file = NULL, *idat = NULL, *raw = NULL;
    size_t file_len = 0, idat_len = 0, raw_len = 0, pos;
    uint8_t ctype = 0, plte[256 * 3], trns[256];
    int bpp, stride, x, y, have_trns = 0;
    long size;

    memset(img, 0, sizeof *img);
    memset(trns, 255, sizeof trns);
    if (!f) {
        fprintf(stderr, "%s: %s: cannot open file\n", tool, path);
        return 0;
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    file = (uint8_t *)malloc((size_t)size);
    if (!file || fread(file, 1, (size_t)size, f) != (size_t)size) {
        fclose(f);
        FAIL("cannot read file");
    }
    fclose(f);
    file_len = (size_t)size;
    if (file_len < 8 || memcmp(file, sig, 8) != 0) {
        FAIL("not a PNG file");
    }
    idat = (uint8_t *)malloc(file_len);
    if (!idat) {
        FAIL("out of memory");
    }
    for (pos = 8; pos + 12 <= file_len;) {
        uint32_t n = be32(file + pos);
        const uint8_t *type = file + pos + 4, *data = file + pos + 8;
        if (pos + 12 + n > file_len) {
            FAIL("broken chunk");
        }
        if (memcmp(type, "IHDR", 4) == 0) {
            img->w = (int)be32(data);
            img->h = (int)be32(data + 4);
            ctype = data[9];
            if (data[8] != 8 || data[12] != 0 || (ctype != 2 && ctype != 3 && ctype != 6)) {
                FAIL("only 8-bit RGB, RGBA or palette PNGs without interlace are supported");
            }
        } else if (memcmp(type, "PLTE", 4) == 0) {
            memcpy(plte, data, n < sizeof plte ? n : sizeof plte);
        } else if (memcmp(type, "tRNS", 4) == 0 && ctype == 3) {
            memcpy(trns, data, n < sizeof trns ? n : sizeof trns);
            have_trns = 1;
        } else if (memcmp(type, "IDAT", 4) == 0) {
            memcpy(idat + idat_len, data, n);
            idat_len += n;
        }
        pos += 12 + n;
    }
    (void)have_trns;
    if (img->w <= 0 || img->h <= 0 || !inflate_zlib(idat, idat_len, &raw, &raw_len)) {
        FAIL("cannot decompress the image data");
    }
    bpp = ctype == 2 ? 3 : ctype == 6 ? 4 : 1;
    stride = img->w * bpp;
    if (raw_len < (size_t)(stride + 1) * (size_t)img->h) {
        FAIL("image data too short");
    }
    img->px = (uint32_t *)malloc(sizeof(uint32_t) * (size_t)img->w * (size_t)img->h);
    if (!img->px) {
        FAIL("out of memory");
    }
    for (y = 0; y < img->h; y++) {
        uint8_t *row = raw + (size_t)y * (size_t)(stride + 1);
        uint8_t filter = row[0], *cur = row + 1, *prev = y ? raw + (size_t)(y - 1) * (size_t)(stride + 1) + 1 : NULL;
        for (x = 0; x < stride; x++) {
            int a = x >= bpp ? cur[x - bpp] : 0, b = prev ? prev[x] : 0, c = (prev && x >= bpp) ? prev[x - bpp] : 0;
            switch (filter) {
            case 0: break;
            case 1: cur[x] = (uint8_t)(cur[x] + a); break;
            case 2: cur[x] = (uint8_t)(cur[x] + b); break;
            case 3: cur[x] = (uint8_t)(cur[x] + ((a + b) >> 1)); break;
            case 4: cur[x] = (uint8_t)(cur[x] + paeth(a, b, c)); break;
            default: FAIL("unknown row filter");
            }
        }
        for (x = 0; x < img->w; x++) {
            uint32_t rgb;
            int alpha = 255;
            if (ctype == 3) {
                const uint8_t *p = plte + cur[x] * 3;
                rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                alpha = trns[cur[x]];
            } else {
                const uint8_t *p = cur + x * bpp;
                rgb = ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
                if (ctype == 6) {
                    alpha = p[3];
                }
            }
            img->px[(size_t)y * (size_t)img->w + (size_t)x] = alpha < 128 ? PNG_TRANSPARENT : rgb;
        }
    }
    free(file);
    free(idat);
    free(raw);
    return 1;
fail:
    free(file);
    free(idat);
    free(raw);
    png_free(img);
    return 0;
}

void png_free(PngImage *img)
{
    free(img->px);
    img->px = NULL;
}
