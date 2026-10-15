/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_image.c — pictures from PNG files, fitted to where they are drawn.
 * See zui_image.h. The inflater follows RFC 1951 (canonical Huffman decoded
 * a bit at a time, a 32 KiB window); the PNG side follows the PNG
 * specification's chunk layout, filters and sample formats. */
#include "zui_image.h"

#define WIN 32768u

typedef struct {
    uint16_t cnt[16], sym[320];
} huff_t;

typedef struct {
    const uint8_t *p;
    uint32_t n;
    uint32_t first_idat, next, left, at; /* the zlib stream: next chunk, bytes
                                          * left in this one, the cursor */
    uint32_t bits, nbits;
    int err;
    /* the picture */
    uint32_t w, h, rowbytes, bpp;
    uint8_t depth, ctype, ch, has_trns;
    uint16_t trns[3];
    uint8_t pal[256][4];
    /* the inflater's output: the window, then the scanlines */
    uint8_t *win;
    uint32_t wpos, total;
    uint8_t *cur, *prev;
    uint32_t fill, y;
    /* what is done with each row */
    int pass;
    uint32_t key;
    int32_t bx0, by0, bx1, by1;
    int32_t dw, dh, cy;
    uint64_t *acc;
    uint32_t *cnt, *out;
    huff_t lt, dt;
} dec_t;

static uint32_t be32(const uint8_t *b)
{
    return (uint32_t) b[0] << 24 | (uint32_t) b[1] << 16 | (uint32_t) b[2] << 8 | b[3];
}

static int is(const uint8_t *t, const char *s)
{
    return t[0] == (uint8_t) s[0] && t[1] == (uint8_t) s[1] && t[2] == (uint8_t) s[2] &&
           t[3] == (uint8_t) s[3];
}

/* ---- the PNG's chunks ------------------------------------------------------ */
static int header(dec_t *d, const uint8_t *p, uint32_t n)
{
    static const uint8_t SIG[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    if (!p || n < 8 + 25) return ZUI_IMG_EFORMAT;
    for (int i = 0; i < 8; i++)
        if (p[i] != SIG[i]) return ZUI_IMG_EFORMAT;
    d->p = p;
    d->n = n;
    uint32_t at = 8;
    int seen_ihdr = 0;
    for (uint32_t i = 0; i < 256; i++) d->pal[i][3] = 255;
    while (at + 12 <= n) {
        uint32_t len = be32(p + at);
        const uint8_t *t = p + at + 4, *b = p + at + 8;
        if (len > n - at - 12) return ZUI_IMG_EFORMAT;
        if (is(t, "IHDR")) {
            if (len < 13) return ZUI_IMG_EFORMAT;
            d->w = be32(b);
            d->h = be32(b + 4);
            d->depth = b[8];
            d->ctype = b[9];
            if (b[10] || b[11]) return ZUI_IMG_EFORMAT;
            if (b[12]) return ZUI_IMG_EUNSUPPORTED; /* interlaced */
            seen_ihdr = 1;
        } else if (!seen_ihdr) {
            return ZUI_IMG_EFORMAT;
        } else if (is(t, "PLTE")) {
            for (uint32_t i = 0; i < len / 3 && i < 256; i++) {
                d->pal[i][0] = b[3 * i];
                d->pal[i][1] = b[3 * i + 1];
                d->pal[i][2] = b[3 * i + 2];
            }
        } else if (is(t, "tRNS")) {
            d->has_trns = 1;
            if (d->ctype == 3)
                for (uint32_t i = 0; i < len && i < 256; i++) d->pal[i][3] = b[i];
            else
                for (uint32_t i = 0; i < 3 && 2 * i + 1 < len; i++)
                    d->trns[i] = (uint16_t) (b[2 * i] << 8 | b[2 * i + 1]);
        } else if (is(t, "IDAT")) {
            if (!d->first_idat) d->first_idat = at;
        } else if (is(t, "IEND")) {
            break;
        }
        at += 12 + len;
    }
    if (!seen_ihdr || !d->first_idat || !d->w || !d->h || d->w > 32768 || d->h > 32768)
        return ZUI_IMG_EFORMAT;
    static const uint8_t CH[7] = {1, 0, 3, 1, 2, 0, 4};
    if (d->ctype > 6 || !CH[d->ctype]) return ZUI_IMG_EFORMAT;
    d->ch = CH[d->ctype];
    uint8_t dp = d->depth;
    int ok = dp == 8 || dp == 16 ||
             ((d->ctype == 0 || d->ctype == 3) && (dp == 1 || dp == 2 || dp == 4));
    if (!ok || (d->ctype == 3 && dp == 16)) return ZUI_IMG_EFORMAT;
    d->rowbytes = (d->w * d->ch * dp + 7u) / 8u;
    d->bpp = d->ch * dp / 8u;
    if (!d->bpp) d->bpp = 1;
    return ZUI_IMG_OK;
}

/* ---- reading the zlib stream across IDAT chunks ------------------------- */
static uint32_t rd_byte(dec_t *d)
{
    while (!d->left) {
        uint32_t c = d->next;
        if (d->err || c + 12 > d->n || !is(d->p + c + 4, "IDAT")) {
            d->err = 1; /* the stream ran out */
            return 0;
        }
        d->left = be32(d->p + c);
        d->at = c + 8;
        d->next = c + 12 + d->left;
    }
    d->left--;
    return d->p[d->at++];
}

static uint32_t bit(dec_t *d)
{
    if (!d->nbits) {
        d->bits = rd_byte(d);
        d->nbits = 8;
    }
    uint32_t b = d->bits & 1u;
    d->bits >>= 1;
    d->nbits--;
    return b;
}

static uint32_t bits(dec_t *d, uint32_t n)
{
    uint32_t v = 0;
    for (uint32_t i = 0; i < n; i++) v |= bit(d) << i;
    return v;
}

/* ---- Huffman ------------------------------------------------------------- */
static int build(huff_t *h, const uint8_t *len, uint32_t n)
{
    uint16_t off[16];
    for (int i = 0; i < 16; i++) h->cnt[i] = 0;
    for (uint32_t i = 0; i < n; i++) h->cnt[len[i]]++;
    h->cnt[0] = 0;
    int left = 1;
    for (int i = 1; i < 16; i++) {
        left = left * 2 - h->cnt[i];
        if (left < 0) return -1; /* over-subscribed */
    }
    off[1] = 0;
    for (int i = 1; i < 15; i++) off[i + 1] = (uint16_t) (off[i] + h->cnt[i]);
    for (uint32_t i = 0; i < n; i++)
        if (len[i]) h->sym[off[len[i]]++] = (uint16_t) i;
    return 0;
}

static int decode(dec_t *d, const huff_t *h)
{
    int code = 0, first = 0, index = 0;
    for (int len = 1; len < 16; len++) {
        code |= (int) bit(d);
        int count = h->cnt[len];
        if (code - first < count) return h->sym[index + code - first];
        index += count;
        first = (first + count) << 1;
        code <<= 1;
        if (d->err) return -1;
    }
    return -1;
}

/* ---- rows ------------------------------------------------------------------ */
static uint32_t sample(const dec_t *d, const uint8_t *row, uint32_t idx)
{
    if (d->depth == 8) return row[idx];
    if (d->depth == 16) return (uint32_t) row[2 * idx] << 8 | row[2 * idx + 1];
    uint32_t bitpos = idx * d->depth;
    return (uint32_t) (row[bitpos >> 3] >> (8u - d->depth - (bitpos & 7u))) &
           ((1u << d->depth) - 1u);
}

static uint32_t to8(const dec_t *d, uint32_t v)
{
    if (d->depth == 16) return v >> 8;
    if (d->depth < 8) return v * 255u / ((1u << d->depth) - 1u);
    return v;
}

static uint32_t udist(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

/* pixel x of a row as straight R, G, B, A */
static void pixel(const dec_t *d, const uint8_t *row, uint32_t x, uint32_t *r, uint32_t *g,
                  uint32_t *b, uint32_t *a)
{
    uint32_t base = x * d->ch;
    switch (d->ctype) {
    case 0: {
        uint32_t v = sample(d, row, base);
        *r = *g = *b = to8(d, v);
        *a = d->has_trns && v == d->trns[0] ? 0u : 255u;
        break;
    }
    case 2: {
        uint32_t R = sample(d, row, base), G = sample(d, row, base + 1),
                 B = sample(d, row, base + 2);
        *r = to8(d, R);
        *g = to8(d, G);
        *b = to8(d, B);
        *a = d->has_trns && R == d->trns[0] && G == d->trns[1] && B == d->trns[2] ? 0u : 255u;
        break;
    }
    case 3: {
        const uint8_t *e = d->pal[sample(d, row, base) & 255u];
        *r = e[0];
        *g = e[1];
        *b = e[2];
        *a = e[3];
        break;
    }
    case 4:
        *r = *g = *b = to8(d, sample(d, row, base));
        *a = to8(d, sample(d, row, base + 1));
        break;
    default:
        *r = to8(d, sample(d, row, base));
        *g = to8(d, sample(d, row, base + 1));
        *b = to8(d, sample(d, row, base + 2));
        *a = to8(d, sample(d, row, base + 3));
        break;
    }
    if (d->key != ZUI_IMG_NO_KEY && !d->has_trns && d->ctype != 4 && d->ctype != 6) {
        /* IM2: drawn on the ground colour: clear near it, fading in */
        uint32_t dist = udist(*r, (d->key >> 16) & 255u) + udist(*g, (d->key >> 8) & 255u) +
                        udist(*b, d->key & 255u);
        uint32_t k = dist > 30u ? (dist - 30u) * 3u : 0u;
        *a = k > 255u ? 255u : k;
    }
}

static void flush(dec_t *d)
{
    if (d->cy < 0) return;
    for (int32_t x = 0; x < d->dw; x++) {
        uint32_t c = d->cnt[x];
        uint64_t *s = &d->acc[4 * x];
        uint32_t v = 0;
        if (c)
            v = (uint32_t) (s[0] / c) << 24 | (uint32_t) (s[1] / (255u * c)) << 16 |
                (uint32_t) (s[2] / (255u * c)) << 8 | (uint32_t) (s[3] / (255u * c));
        d->out[d->cy * d->dw + x] = v;
        s[0] = s[1] = s[2] = s[3] = 0;
        d->cnt[x] = 0;
    }
}

static void row_done(dec_t *d)
{
    uint8_t *row = d->cur + 1, *up = d->prev + 1;
    uint32_t bpp = d->bpp, n = d->rowbytes;
    switch (d->cur[0]) {
    case 0:
        break;
    case 1:
        for (uint32_t i = bpp; i < n; i++) row[i] = (uint8_t) (row[i] + row[i - bpp]);
        break;
    case 2:
        for (uint32_t i = 0; i < n; i++) row[i] = (uint8_t) (row[i] + up[i]);
        break;
    case 3:
        for (uint32_t i = 0; i < n; i++)
            row[i] = (uint8_t) (row[i] + (((i >= bpp ? row[i - bpp] : 0u) + up[i]) >> 1));
        break;
    case 4:
        for (uint32_t i = 0; i < n; i++) {
            int a = i >= bpp ? row[i - bpp] : 0, b = up[i], c = i >= bpp ? up[i - bpp] : 0;
            int p = a + b - c, pa = p > a ? p - a : a - p, pb = p > b ? p - b : b - p,
                pc = p > c ? p - c : c - p;
            row[i] = (uint8_t) (row[i] + (pa <= pb && pa <= pc ? a : pb <= pc ? b : c));
        }
        break;
    default:
        d->err = 2;
        return;
    }
    int32_t y = (int32_t) d->y;
    /* the common files straight from the bytes: 8-bit RGBA (the logos) and
     * 8-bit RGB keyed off the ground (the icons); everything else through
     * pixel() */
    int rgba8 = d->depth == 8 && d->ctype == 6;
    int rgb8 = d->depth == 8 && d->ctype == 2 && !d->has_trns;
    uint32_t kr = (d->key >> 16) & 255u, kg = (d->key >> 8) & 255u, kb = d->key & 255u;
    int keyed = d->key != ZUI_IMG_NO_KEY;
#define PX(x)                                                                                      \
    do {                                                                                           \
        if (rgba8) {                                                                               \
            const uint8_t *q = row + 4u * (uint32_t) (x);                                          \
            r = q[0], g = q[1], b = q[2], a = q[3];                                                \
        } else if (rgb8) {                                                                         \
            const uint8_t *q = row + 3u * (uint32_t) (x);                                          \
            r = q[0], g = q[1], b = q[2], a = 255u;                                                \
            if (keyed) {                                                                           \
                uint32_t dist = udist(r, kr) + udist(g, kg) + udist(b, kb);                        \
                uint32_t k = dist > 30u ? (dist - 30u) * 3u : 0u;                                  \
                a = k > 255u ? 255u : k;                                                           \
            }                                                                                      \
        } else {                                                                                   \
            pixel(d, row, (uint32_t) (x), &r, &g, &b, &a);                                         \
        }                                                                                          \
    } while (0)
    uint32_t r, g, b, a;
    if (d->pass == 1) { /* IM3: where is the drawing? */
        int32_t lo = -1, hi = -1;
        for (uint32_t x = 0; x < d->w; x++) {
            if (rgba8) {
                a = row[4u * x + 3u];
            } else {
                PX(x);
            }
            if (!a) continue;
            if (lo < 0) lo = (int32_t) x;
            hi = (int32_t) x;
        }
        if (lo >= 0) {
            if (lo < d->bx0) d->bx0 = lo;
            if (hi > d->bx1) d->bx1 = hi;
            if (y < d->by0) d->by0 = y;
            if (y > d->by1) d->by1 = y;
        }
    } else if (y >= d->by0 && y <= d->by1) { /* IM3: fit it, averaging */
        int32_t bw = d->bx1 - d->bx0 + 1, bh = d->by1 - d->by0 + 1;
        int32_t dy = (int32_t) ((int64_t) (y - d->by0) * d->dh / bh);
        if (dy != d->cy) {
            flush(d);
            d->cy = dy;
        }
        int32_t dx = 0, num = 0; /* dx = (x - bx0) * dw / bw, stepped */
        for (int32_t x = d->bx0; x <= d->bx1; x++) {
            PX(x);
            uint64_t *s = &d->acc[4 * dx];
            s[0] += a;
            s[1] += (uint64_t) r * a;
            s[2] += (uint64_t) g * a;
            s[3] += (uint64_t) b * a;
            d->cnt[dx]++;
            num += d->dw;
            if (num >= bw) num -= bw, dx++;
        }
    }
#undef PX
    uint8_t *t = d->prev;
    d->prev = d->cur;
    d->cur = t;
    d->fill = 0;
    d->y++;
}

static void put(dec_t *d, uint8_t b)
{
    d->win[d->wpos++ & (WIN - 1u)] = b;
    d->total++;
    if (d->y >= d->h) return;
    d->cur[d->fill++] = b;
    if (d->fill == d->rowbytes + 1u) row_done(d);
}

/* ---- inflate ---------------------------------------------------------------- */
static const uint16_t LBASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXTRA[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                   2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
                                   33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
                                   1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXTRA[30] = {0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
                                   6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static int dynamic(dec_t *d)
{
    static const uint8_t ORDER[19] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};
    uint8_t len[320];
    uint32_t hlit = bits(d, 5) + 257u, hdist = bits(d, 5) + 1u, hclen = bits(d, 4) + 4u;
    if (hlit > 286 || hdist > 30) return -1;
    for (int i = 0; i < 19; i++) len[i] = 0;
    for (uint32_t i = 0; i < hclen; i++) len[ORDER[i]] = (uint8_t) bits(d, 3);
    huff_t cl;
    if (build(&cl, len, 19)) return -1;
    for (uint32_t i = 0; i < hlit + hdist;) {
        int s = decode(d, &cl);
        if (s < 0) return -1;
        if (s < 16) {
            len[i++] = (uint8_t) s;
            continue;
        }
        uint32_t rep, v = 0;
        if (s == 16) {
            if (!i) return -1;
            v = len[i - 1];
            rep = 3u + bits(d, 2);
        } else if (s == 17) {
            rep = 3u + bits(d, 3);
        } else {
            rep = 11u + bits(d, 7);
        }
        if (i + rep > hlit + hdist) return -1;
        while (rep--) len[i++] = (uint8_t) v;
    }
    if (!len[256]) return -1;
    if (build(&d->lt, len, hlit) || build(&d->dt, len + hlit, hdist)) return -1;
    return 0;
}

static void fixed(dec_t *d)
{
    uint8_t len[320];
    uint32_t i = 0;
    for (; i < 144; i++) len[i] = 8;
    for (; i < 256; i++) len[i] = 9;
    for (; i < 280; i++) len[i] = 7;
    for (; i < 288; i++) len[i] = 8;
    build(&d->lt, len, 288);
    for (i = 0; i < 30; i++) len[i] = 5;
    build(&d->dt, len, 30);
}

static int inflate(dec_t *d)
{
    uint32_t cmf = rd_byte(d), flg = rd_byte(d);
    if (d->err || (cmf & 15u) != 8u || (cmf * 256u + flg) % 31u || (flg & 32u)) return -1;
    uint32_t final;
    do {
        final = bit(d);
        uint32_t type = bits(d, 2);
        if (type == 0) {
            d->nbits = 0; /* to the byte */
            uint32_t len = rd_byte(d), nlen;
            len |= rd_byte(d) << 8;
            nlen = rd_byte(d);
            nlen |= rd_byte(d) << 8;
            if ((len ^ 0xFFFFu) != nlen) return -1;
            while (len-- && !d->err) put(d, (uint8_t) rd_byte(d));
        } else if (type == 3) {
            return -1;
        } else {
            if (type == 1)
                fixed(d);
            else if (dynamic(d))
                return -1;
            for (;;) {
                int s = decode(d, &d->lt);
                if (s < 0 || d->err) return -1;
                if (s < 256) {
                    put(d, (uint8_t) s);
                } else if (s == 256) {
                    break;
                } else {
                    s -= 257;
                    if (s >= 29) return -1;
                    uint32_t len = LBASE[s] + bits(d, LEXTRA[s]);
                    int ds = decode(d, &d->dt);
                    if (ds < 0 || ds >= 30) return -1;
                    uint32_t dist = DBASE[ds] + bits(d, DEXTRA[ds]);
                    if (dist > d->total || dist > WIN) return -1;
                    while (len--) put(d, d->win[(d->wpos - dist) & (WIN - 1u)]);
                }
                if (d->y >= d->h) return 0; /* every row is in: the rest is not needed */
            }
        }
        if (d->err == 2) return -2;
        if (d->y >= d->h) return 0;
    } while (!final && !d->err);
    return d->y >= d->h ? 0 : -1;
}

/* one pass over the picture's rows */
static int pass(dec_t *d, int which)
{
    uint32_t rb = d->rowbytes + 1u;
    for (uint32_t i = 0; i < rb; i++) d->cur[i] = d->prev[i] = 0;
    d->pass = which;
    d->next = d->first_idat;
    d->left = d->nbits = d->bits = 0;
    d->err = 0;
    d->wpos = d->total = d->fill = d->y = 0;
    int r = inflate(d);
    if (d->err == 2) return ZUI_IMG_EUNSUPPORTED;
    return r ? ZUI_IMG_ECORRUPT : ZUI_IMG_OK;
}

/* ---- public ------------------------------------------------------------------ */
static uint32_t align8(uint32_t v)
{
    return (v + 7u) & ~7u;
}

static uint32_t need(const dec_t *d, int32_t max_w)
{
    return align8(sizeof(dec_t)) + WIN + 2u * align8(d->rowbytes + 1u) +
           (uint32_t) max_w * (4u * 8u + 4u);
}

uint32_t zui_image_work_size(const uint8_t *png, uint32_t n, int32_t max_w)
{
    dec_t d;
    uint8_t *b = (uint8_t *) &d;
    for (uint32_t i = 0; i < sizeof d; i++) b[i] = 0;
    if (max_w < 1 || header(&d, png, n) != ZUI_IMG_OK) return 0;
    return need(&d, max_w);
}

int zui_image_load(zui_image_t *img, uint32_t *px, int32_t max_w, int32_t max_h, const uint8_t *png,
                   uint32_t n, uint32_t key, uint8_t *work, uint32_t work_cap)
{
    if (!img || !px || !work || max_w < 1 || max_h < 1) return ZUI_IMG_EFORMAT;
    img->px = px;
    img->w = img->h = 0;
    uint8_t *aligned = (uint8_t *) (((uintptr_t) work + 7u) & ~(uintptr_t) 7u);
    uint32_t slack = (uint32_t) (aligned - work);
    if (work_cap < slack + sizeof(dec_t)) return ZUI_IMG_ESPACE;
    dec_t *d = (dec_t *) aligned;
    uint8_t *b = (uint8_t *) d;
    for (uint32_t i = 0; i < sizeof *d; i++) b[i] = 0;
    int r = header(d, png, n);
    if (r) return r;
    if (work_cap < slack + need(d, max_w)) return ZUI_IMG_ESPACE;
    d->key = key;
    uint8_t *m = aligned + align8(sizeof(dec_t));
    d->win = m;
    m += WIN;
    d->cur = m;
    m += align8(d->rowbytes + 1u);
    d->prev = m;
    m += align8(d->rowbytes + 1u);
    d->acc = (uint64_t *) m;
    m += (uint32_t) max_w * 32u;
    d->cnt = (uint32_t *) m;
    d->out = px;

    d->bx0 = d->by0 = 0x7FFFFFFF;
    d->bx1 = d->by1 = -1;
    if ((r = pass(d, 1))) return r;
    if (d->bx1 < 0) return ZUI_IMG_EEMPTY;
    int32_t bw = d->bx1 - d->bx0 + 1, bh = d->by1 - d->by0 + 1;
    /* fit inside the box, aspect kept, never enlarged */
    if ((int64_t) bw * max_h >= (int64_t) bh * max_w) {
        d->dw = bw < max_w ? bw : max_w;
        d->dh = (int32_t) ((int64_t) bh * d->dw / bw);
    } else {
        d->dh = bh < max_h ? bh : max_h;
        d->dw = (int32_t) ((int64_t) bw * d->dh / bh);
    }
    if (d->dw < 1) d->dw = 1;
    if (d->dh < 1) d->dh = 1;
    for (int32_t i = 0; i < d->dw; i++) {
        d->acc[4 * i] = d->acc[4 * i + 1] = d->acc[4 * i + 2] = d->acc[4 * i + 3] = 0;
        d->cnt[i] = 0;
    }
    d->cy = -1;
    if ((r = pass(d, 2))) return r;
    flush(d);
    img->w = d->dw;
    img->h = d->dh;
    return ZUI_IMG_OK;
}

void zui_image_draw(zui_surface_t *s, const zui_image_t *img, zui_rect_t box, uint32_t strength,
                    uint32_t tint_rgb)
{
    if (!s || !img || !img->px || img->w < 1 || img->h < 1 || !strength || box.w < 1 || box.h < 1)
        return;
    if (strength > 256u) strength = 256u;
    /* fitted inside the box, aspect kept, never enlarged */
    int32_t dw = img->w, dh = img->h;
    if (dw > box.w || dh > box.h) {
        if ((int64_t) img->w * box.h >= (int64_t) img->h * box.w) {
            dw = box.w;
            dh = (int32_t) ((int64_t) img->h * box.w / img->w);
        } else {
            dh = box.h;
            dw = (int32_t) ((int64_t) img->w * box.h / img->h);
        }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;
    }
    int32_t x0 = box.x + (box.w - dw) / 2, y0 = box.y + (box.h - dh) / 2;
    zui_rect_t c = s->clip;
    uint32_t tr = (tint_rgb >> 16) & 255u, tg = (tint_rgb >> 8) & 255u, tb = tint_rgb & 255u;
    uint32_t tmax = tr > tg ? (tr > tb ? tr : tb) : (tg > tb ? tg : tb);
    if (!tmax) tmax = 1;
    for (int32_t y = 0; y < dh; y++) {
        int32_t sy = y0 + y;
        if (sy < c.y || sy >= c.y + c.h) continue;
        uint32_t *dst = s->px + (int64_t) sy * s->stride;
        const uint32_t *src = img->px + (int64_t) y * img->h / dh * img->w;
        for (int32_t x = 0; x < dw; x++) {
            int32_t sx = x0 + x;
            if (sx < c.x || sx >= c.x + c.w) continue;
            uint32_t p = src[(int64_t) x * img->w / dw], a = p >> 24;
            if (!a) continue;
            uint32_t r = (p >> 16) & 255u, g = (p >> 8) & 255u, b = p & 255u;
            if (tint_rgb != ZUI_IMG_NO_KEY) { /* held to one colour: its light and shade */
                uint32_t l = r > g ? (r > b ? r : b) : (g > b ? g : b);
                l = l * 255u / tmax;
                if (l > a) l = a;
                r = tr * l / 255u;
                g = tg * l / 255u;
                b = tb * l / 255u;
            }
            a = a * strength >> 8;
            r = r * strength >> 8;
            g = g * strength >> 8;
            b = b * strength >> 8;
            uint32_t q = dst[sx], ia = 255u - a;
            uint32_t R = r + ((q >> 16) & 255u) * ia / 255u, G = g + ((q >> 8) & 255u) * ia / 255u,
                     B = b + (q & 255u) * ia / 255u;
            dst[sx] =
                (R > 255u ? 255u : R) << 16 | (G > 255u ? 255u : G) << 8 | (B > 255u ? 255u : B);
        }
    }
}
