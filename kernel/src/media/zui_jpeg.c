/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zui_jpeg.c — the JPEG decoder behind zx_image.h.
 *
 * One path for baseline and progressive: every scan decodes into a full
 * coefficient array (int16 per coefficient), then each component is inverse
 * transformed into a sample plane and the planes are upsampled and converted
 * to RGB row by row. That costs about 2 bytes of coefficients plus 1 byte of
 * plane per component sample, and keeps one decoder for both modes.
 *
 * Arithmetic notes: the IDCT is libjpeg's jidctint "islow" (13-bit constants,
 * 2 extra bits after the column pass) computed in 64-bit so hostile
 * coefficients cannot overflow; YCbCr -> RGB is libjpeg's 16-bit fixed
 * point; 2x chroma uses libjpeg's "fancy" triangle upsampling. Every right
 * shift of a signed value is a floor (asr), never implementation defined.
 */
#include "zx_image_int.h"

#define FAST 9

typedef struct {
    uint16_t fast[1 << FAST]; /* (len << 8) | symbol, 0: longer than FAST */
    int32_t maxcode[18], valoff[18];
    uint8_t vals[256];
    bool defined;
} jhuff_t;

typedef struct {
    uint8_t id, h, v, tq;
    uint32_t bw, bh; /* coefficient array, in blocks (MCU padded)  */
    uint32_t cw, ch; /* real samples of this component              */
    int16_t *coef;
    uint8_t *plane; /* bw*8 x bh*8                                  */
    uint16_t q[64]; /* natural order, latched at its first scan     */
    bool latched;
    int32_t pred;
    uint8_t td, ta;
} jcomp_t;

typedef struct {
    jhuff_t dc[4], ac[4];
    uint16_t qt[4][64];
    bool qdef[4];
    jcomp_t c[3];
    uint32_t nc, w, h, hmax, vmax, mcux, mcuy, ri, scans;
    bool sof, progressive, adobe;
    uint8_t adobe_xf;
    /* entropy decoder */
    const uint8_t *d;
    uint32_t n, pos, nb, eobrun;
    uint64_t buf;
    bool marker;
    int err;
    /* current scan */
    uint32_t ns, ss, se, ah, al;
    jcomp_t *sc[4];
} jdec_t;

static const uint8_t ZZ[64] = {0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,
                               12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6,  7,  14, 21, 28,
                               35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
                               58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

/* The tables of ITU T.81 K.3, used when a file has none (Motion-JPEG). */
static const uint8_t STD_DC_BITS[2][16] = {{0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0},
                                           {0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0}};
static const uint8_t STD_DC_VALS[12] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
static const uint8_t STD_AC_BITS[2][16] = {{0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 0x7d},
                                           {0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 0x77}};
static const uint8_t STD_AC_VALS[2][162] = {
    {0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06, 0x13, 0x51, 0x61,
     0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52,
     0xd1, 0xf0, 0x24, 0x33, 0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25,
     0x26, 0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45,
     0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64,
     0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83,
     0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99,
     0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6,
     0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3,
     0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8,
     0xe9, 0xea, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa},
    {0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61,
     0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
     0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25, 0xf1, 0x17, 0x18,
     0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43, 0x44,
     0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63,
     0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
     0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
     0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
     0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca,
     0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
     0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa}};

/* ---- integer helpers ---------------------------------------------------- */
static int64_t asr(int64_t v, int s) /* floor(v / 2^s) */
{
    return v >= 0 ? v >> s : ~(~v >> s);
}
static int16_t c16(int64_t v)
{
    return (int16_t) (v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}
static uint8_t c8(int64_t v)
{
    return (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v);
}

/* ---- Huffman tables ----------------------------------------------------- */
static int build(jhuff_t *t, const uint8_t *bits, const uint8_t *vals, uint32_t nvals)
{
    uint32_t total = 0;
    for (int i = 0; i < 16; i++) total += bits[i];
    if (total > 256 || total > nvals) return -1;
    for (uint32_t i = 0; i < total; i++) t->vals[i] = vals[i];
    for (int i = 0; i < (1 << FAST); i++) t->fast[i] = 0;
    int32_t code = 0, k = 0;
    for (int l = 1; l <= 16; l++) {
        t->valoff[l] = k - code;
        for (int i = 0; i < bits[l - 1]; i++, k++, code++) {
            if (code > (1 << l) - 2) return -1; /* overfull, or the reserved all-ones code */
            if (l <= FAST) {
                int32_t b = code << (FAST - l);
                for (int32_t j = 0; j < (1 << (FAST - l)); j++)
                    t->fast[b + j] = (uint16_t) (l << 8 | t->vals[k]);
            }
        }
        t->maxcode[l] = bits[l - 1] ? code - 1 : -1;
        code <<= 1;
    }
    t->maxcode[17] = 0x7FFFFFFF;
    t->defined = true;
    return 0;
}

/* ---- the entropy-coded bit stream --------------------------------------- */
static void fill(jdec_t *j)
{
    while (j->nb <= 56) {
        uint32_t b = 0;
        if (!j->marker && j->pos < j->n) {
            b = j->d[j->pos];
            if (b == 0xFF) {
                if (j->pos + 1 < j->n && j->d[j->pos + 1] == 0) {
                    j->pos += 2;
                } else { /* a marker: feed zeros, leave it for the parser */
                    j->marker = true;
                    b = 0;
                }
            } else {
                j->pos++;
            }
        }
        j->buf |= (uint64_t) b << (56 - j->nb);
        j->nb += 8;
    }
}

static uint32_t bits(jdec_t *j, uint32_t n) /* n <= 16 */
{
    if (!n) return 0;
    if (j->nb < n) fill(j);
    uint32_t v = (uint32_t) (j->buf >> (64 - n));
    j->buf <<= n;
    j->nb -= n;
    return v;
}

static int32_t extend(jdec_t *j, uint32_t s) /* s <= 16 */
{
    if (!s) return 0;
    int32_t v = (int32_t) bits(j, s);
    return v < (1 << (s - 1)) ? v - (1 << s) + 1 : v;
}

static int huff(jdec_t *j, const jhuff_t *t)
{
    if (j->nb < 16) fill(j);
    uint32_t e = t->fast[j->buf >> (64 - FAST)];
    if (e) {
        j->buf <<= e >> 8;
        j->nb -= e >> 8;
        return (int) (e & 255);
    }
    for (int l = FAST + 1; l <= 16; l++) {
        int32_t code = (int32_t) (j->buf >> (64 - l));
        if (code <= t->maxcode[l]) {
            j->buf <<= l;
            j->nb -= (uint32_t) l;
            return t->vals[code + t->valoff[l]];
        }
    }
    j->err = ZX_IMG_E_DATA;
    return 0;
}

/* ---- block decoders ----------------------------------------------------- */
static void blk_seq(jdec_t *j, jcomp_t *c, int16_t *b)
{
    uint32_t t = (uint32_t) huff(j, &j->dc[c->td]);
    if (t > 15) {
        j->err = ZX_IMG_E_DATA;
        return;
    }
    int64_t p = (int64_t) c->pred + extend(j, t);
    c->pred = c16(p);
    b[0] = (int16_t) c->pred;
    for (uint32_t k = 1; k < 64;) {
        uint32_t rs = (uint32_t) huff(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
        if (s) {
            k += r;
            if (k > 63) {
                j->err = ZX_IMG_E_DATA;
                return;
            }
            b[ZZ[k++]] = c16(extend(j, s));
        } else if (r == 15) {
            k += 16;
        } else {
            break;
        }
    }
}

static void blk_dc_first(jdec_t *j, jcomp_t *c, int16_t *b)
{
    uint32_t t = (uint32_t) huff(j, &j->dc[c->td]);
    if (t > 15) {
        j->err = ZX_IMG_E_DATA;
        return;
    }
    c->pred = c16((int64_t) c->pred + extend(j, t));
    b[0] = c16((int64_t) c->pred * ((int64_t) 1 << j->al));
}

static void blk_dc_refine(jdec_t *j, int16_t *b)
{
    if (bits(j, 1)) b[0] = (int16_t) (b[0] | (1 << j->al));
}

static void blk_ac_first(jdec_t *j, jcomp_t *c, int16_t *b)
{
    if (j->eobrun) {
        j->eobrun--;
        return;
    }
    for (uint32_t k = j->ss; k <= j->se; k++) {
        uint32_t rs = (uint32_t) huff(j, &j->ac[c->ta]), r = rs >> 4, s = rs & 15;
        if (s) {
            k += r;
            if (k > 63) {
                j->err = ZX_IMG_E_DATA;
                return;
            }
            b[ZZ[k]] = c16((int64_t) extend(j, s) * ((int64_t) 1 << j->al));
        } else if (r < 15) {
            j->eobrun = (1u << r) - 1;
            if (r) j->eobrun += bits(j, r);
            return;
        } else {
            k += 15;
        }
    }
}

static void refine(jdec_t *j, int16_t *v, int32_t p1)
{
    if (bits(j, 1) && (*v & p1) == 0) *v = c16(*v >= 0 ? *v + p1 : *v - p1);
}

static void blk_ac_refine(jdec_t *j, jcomp_t *c, int16_t *b)
{
    int32_t p1 = 1 << j->al;
    uint32_t k = j->ss;
    if (!j->eobrun) {
        for (; k <= j->se; k++) {
            uint32_t rs = (uint32_t) huff(j, &j->ac[c->ta]);
            int32_t r = (int32_t) (rs >> 4), s = (int32_t) (rs & 15), val = 0;
            if (j->err) return;
            if (s) {
                if (s != 1) {
                    j->err = ZX_IMG_E_DATA;
                    return;
                }
                val = bits(j, 1) ? p1 : -p1;
            } else if (r != 15) {
                j->eobrun = 1u << r;
                if (r) j->eobrun += bits(j, (uint32_t) r);
                break;
            }
            for (; k <= j->se; k++) {
                int16_t *z = &b[ZZ[k]];
                if (*z) {
                    refine(j, z, p1);
                } else {
                    if (r == 0) break;
                    r--;
                }
            }
            if (val && k <= j->se) b[ZZ[k]] = (int16_t) val;
        }
    }
    if (j->eobrun) {
        for (; k <= j->se; k++)
            if (b[ZZ[k]]) refine(j, &b[ZZ[k]], p1);
        j->eobrun--;
    }
}

static void block(jdec_t *j, jcomp_t *c, int16_t *b)
{
    if (!j->progressive)
        blk_seq(j, c, b);
    else if (j->ss == 0)
        j->ah ? blk_dc_refine(j, b) : blk_dc_first(j, c, b);
    else
        j->ah ? blk_ac_refine(j, c, b) : blk_ac_first(j, c, b);
}

static int restart(jdec_t *j)
{
    j->buf = 0;
    j->nb = 0;
    j->marker = false;
    j->eobrun = 0;
    for (uint32_t i = 0; i < j->nc; i++) j->c[i].pred = 0;
    while (j->pos + 1 < j->n && j->d[j->pos] == 0xFF && j->d[j->pos + 1] == 0xFF) j->pos++;
    if (j->pos + 1 < j->n && j->d[j->pos] == 0xFF && (j->d[j->pos + 1] & 0xF8) == 0xD0) {
        j->pos += 2;
        return 0;
    }
    return j->pos + 1 < j->n ? ZX_IMG_E_DATA : ZX_IMG_E_TRUNC;
}

static int scan(jdec_t *j)
{
    j->buf = 0;
    j->nb = 0;
    j->marker = false;
    j->eobrun = 0;
    j->err = 0;
    for (uint32_t i = 0; i < j->nc; i++) j->c[i].pred = 0;
    uint32_t done = 0;
    if (j->ns == 1) {
        jcomp_t *c = j->sc[0];
        uint32_t bw = (c->cw + 7) / 8, bh = (c->ch + 7) / 8;
        for (uint32_t y = 0; y < bh; y++)
            for (uint32_t x = 0; x < bw; x++) {
                if (j->ri && done && done % j->ri == 0) {
                    int rc = restart(j);
                    if (rc) return rc;
                }
                block(j, c, c->coef + ((size_t) y * c->bw + x) * 64);
                if (j->err) return j->err;
                done++;
            }
        return 0;
    }
    for (uint32_t my = 0; my < j->mcuy; my++)
        for (uint32_t mx = 0; mx < j->mcux; mx++) {
            if (j->ri && done && done % j->ri == 0) {
                int rc = restart(j);
                if (rc) return rc;
            }
            for (uint32_t i = 0; i < j->ns; i++) {
                jcomp_t *c = j->sc[i];
                for (uint32_t v = 0; v < c->v; v++)
                    for (uint32_t h = 0; h < c->h; h++) {
                        size_t bi = (size_t) (my * c->v + v) * c->bw + mx * c->h + h;
                        block(j, c, c->coef + bi * 64);
                        if (j->err) return j->err;
                    }
            }
            done++;
        }
    return 0;
}

/* ---- inverse DCT (libjpeg jidctint "islow") ----------------------------- */
#define F0298 2446
#define F0390 3196
#define F0541 4433
#define F0765 6270
#define F0899 7373
#define F1175 9633
#define F1501 12299
#define F1847 15137
#define F1961 16069
#define F2053 16819
#define F2562 20995
#define F3072 25172

static void idct(const int16_t *in, const uint16_t *q, uint8_t *out, uint32_t stride)
{
    int64_t ws[64];
    for (int c = 0; c < 8; c++) {
        const int16_t *i = in + c;
        const uint16_t *k = q + c;
        int64_t *w = ws + c;
        if (!i[8] && !i[16] && !i[24] && !i[32] && !i[40] && !i[48] && !i[56]) {
            int64_t dc = (int64_t) i[0] * k[0] * 4;
            for (int r = 0; r < 8; r++) w[8 * r] = dc;
            continue;
        }
        int64_t z2 = (int64_t) i[16] * k[16], z3 = (int64_t) i[48] * k[48];
        int64_t z1 = (z2 + z3) * F0541;
        int64_t t2 = z1 - z3 * F1847, t3 = z1 + z2 * F0765;
        z2 = (int64_t) i[0] * k[0];
        z3 = (int64_t) i[32] * k[32];
        int64_t t0 = (z2 + z3) * 8192, t1 = (z2 - z3) * 8192;
        int64_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
        t0 = (int64_t) i[56] * k[56];
        t1 = (int64_t) i[40] * k[40];
        t2 = (int64_t) i[24] * k[24];
        t3 = (int64_t) i[8] * k[8];
        z1 = t0 + t3;
        z2 = t1 + t2;
        z3 = t0 + t2;
        int64_t z4 = t1 + t3, z5 = (z3 + z4) * F1175;
        t0 *= F0298;
        t1 *= F2053;
        t2 *= F3072;
        t3 *= F1501;
        z1 *= -F0899;
        z2 *= -F2562;
        z3 = z3 * -F1961 + z5;
        z4 = z4 * -F0390 + z5;
        t0 += z1 + z3;
        t1 += z2 + z4;
        t2 += z2 + z3;
        t3 += z1 + z4;
        w[0] = asr(t10 + t3 + 1024, 11);
        w[56] = asr(t10 - t3 + 1024, 11);
        w[8] = asr(t11 + t2 + 1024, 11);
        w[48] = asr(t11 - t2 + 1024, 11);
        w[16] = asr(t12 + t1 + 1024, 11);
        w[40] = asr(t12 - t1 + 1024, 11);
        w[24] = asr(t13 + t0 + 1024, 11);
        w[32] = asr(t13 - t0 + 1024, 11);
    }
    for (int r = 0; r < 8; r++) {
        const int64_t *w = ws + 8 * r;
        uint8_t *o = out + (size_t) r * stride;
        if (!w[1] && !w[2] && !w[3] && !w[4] && !w[5] && !w[6] && !w[7]) {
            uint8_t v = c8(asr(w[0] + 16, 5) + 128);
            for (int x = 0; x < 8; x++) o[x] = v;
            continue;
        }
        int64_t z2 = w[2], z3 = w[6];
        int64_t z1 = (z2 + z3) * F0541;
        int64_t t2 = z1 - z3 * F1847, t3 = z1 + z2 * F0765;
        int64_t t0 = (w[0] + w[4]) * 8192, t1 = (w[0] - w[4]) * 8192;
        int64_t t10 = t0 + t3, t13 = t0 - t3, t11 = t1 + t2, t12 = t1 - t2;
        t0 = w[7];
        t1 = w[5];
        t2 = w[3];
        t3 = w[1];
        z1 = t0 + t3;
        z2 = t1 + t2;
        z3 = t0 + t2;
        int64_t z4 = t1 + t3, z5 = (z3 + z4) * F1175;
        t0 *= F0298;
        t1 *= F2053;
        t2 *= F3072;
        t3 *= F1501;
        z1 *= -F0899;
        z2 *= -F2562;
        z3 = z3 * -F1961 + z5;
        z4 = z4 * -F0390 + z5;
        t0 += z1 + z3;
        t1 += z2 + z4;
        t2 += z2 + z3;
        t3 += z1 + z4;
        const int64_t R = 1 << 17;
        o[0] = c8(asr(t10 + t3 + R, 18) + 128);
        o[7] = c8(asr(t10 - t3 + R, 18) + 128);
        o[1] = c8(asr(t11 + t2 + R, 18) + 128);
        o[6] = c8(asr(t11 - t2 + R, 18) + 128);
        o[2] = c8(asr(t12 + t1 + R, 18) + 128);
        o[5] = c8(asr(t12 - t1 + R, 18) + 128);
        o[3] = c8(asr(t13 + t0 + R, 18) + 128);
        o[4] = c8(asr(t13 - t0 + R, 18) + 128);
    }
}

/* ---- headers ------------------------------------------------------------ */
typedef struct {
    uint32_t w, h, nc, hmax, vmax;
    uint8_t hs[3], vs[3];
    bool progressive;
    uint8_t orient;
} jhdr_t;

static int sof(jhdr_t *f, const uint8_t *s, uint32_t len, zx_image_info_t *info)
{
    if (len < 6) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad SOF");
    if (s[0] != 8) return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: only 8-bit samples");
    f->h = zi_be16(s + 1);
    f->w = zi_be16(s + 3);
    f->nc = s[5];
    if (f->h == 0) return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: DNL (height in trailer)");
    if (f->nc != 1 && f->nc != 3)
        return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: only 1 or 3 components (no CMYK)");
    if (len != 6 + 3 * f->nc) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad SOF length");
    f->hmax = f->vmax = 1;
    for (uint32_t i = 0; i < f->nc; i++) {
        uint32_t hv = s[7 + 3 * i];
        f->hs[i] = (uint8_t) (hv >> 4);
        f->vs[i] = (uint8_t) (hv & 15);
        if (f->hs[i] < 1 || f->hs[i] > 4 || f->vs[i] < 1 || f->vs[i] > 4 || s[8 + 3 * i] > 3)
            return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad sampling factor or table");
        for (uint32_t k = 0; k < i; k++)
            if (s[6 + 3 * k] == s[6 + 3 * i])
                return zi_fail(info, ZX_IMG_E_DATA, "JPEG: duplicate component");
        if (f->hs[i] > f->hmax) f->hmax = f->hs[i];
        if (f->vs[i] > f->vmax) f->vmax = f->vs[i];
    }
    for (uint32_t i = 0; i < f->nc; i++)
        if (f->hmax % f->hs[i] || f->vmax % f->vs[i])
            return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: fractional sampling ratio");
    return zi_dims(info, f->w, f->h);
}

static bool sof_kind(uint32_t m, zx_image_info_t *info, int *rc)
{
    if (m == 0xC0 || m == 0xC1 || m == 0xC2) return true;
    if (m == 0xC3)
        *rc = zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: lossless (SOF3) not supported");
    else if (m >= 0xC9)
        *rc = zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: arithmetic coding not supported");
    else
        *rc = zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: hierarchical (SOF5-7) not supported");
    return false;
}

static bool is_sof(uint32_t m)
{
    return m >= 0xC0 && m <= 0xCF && m != 0xC4 && m != 0xC8 && m != 0xCC;
}

/* Bytes of work for a header: the state, coefficients, planes, rows. */
static uint64_t need(const jhdr_t *f)
{
    uint64_t mcux = (f->w + 8 * f->hmax - 1) / (8 * f->hmax),
             mcuy = (f->h + 8 * f->vmax - 1) / (8 * f->vmax);
    uint64_t t = (sizeof(jdec_t) + 7) & ~(uint64_t) 7;
    for (uint32_t i = 0; i < f->nc; i++) t += mcux * f->hs[i] * mcuy * f->vs[i] * (128 + 64);
    return t + 3 * (uint64_t) f->w;
}

uint32_t zui_jpeg_work_max(uint32_t w, uint32_t h)
{
    uint64_t b = (uint64_t) ((w + 7) / 8 + 4) * ((h + 7) / 8 + 4);
    uint64_t t = ((sizeof(jdec_t) + 7) & ~(uint64_t) 7) + 3 * b * (128 + 64) + 3 * (uint64_t) w;
    return t > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) t;
}

/* Segment walker: calls back for each marker with a length. Returns the
 * position after the segment, or 0 on a structural error (rc set). */
static uint32_t segment(const uint8_t *d, uint32_t n, uint32_t pos, uint32_t *m, uint32_t *len,
                        int *rc, zx_image_info_t *info)
{
    if (pos >= n || d[pos] != 0xFF) {
        *rc = zi_fail(info, pos >= n ? ZX_IMG_E_TRUNC : ZX_IMG_E_DATA, "JPEG: marker expected");
        return 0;
    }
    while (pos < n && d[pos] == 0xFF) pos++;
    if (pos >= n) {
        *rc = zi_fail(info, ZX_IMG_E_TRUNC, "JPEG: ends inside a marker");
        return 0;
    }
    *m = d[pos++];
    *len = 0;
    if (*m == 0xD8 || *m == 0xD9 || *m == 0x01 || (*m >= 0xD0 && *m <= 0xD7)) return pos;
    if (*m == 0) {
        *rc = zi_fail(info, ZX_IMG_E_DATA, "JPEG: stray FF00");
        return 0;
    }
    if (n - pos < 2) {
        *rc = zi_fail(info, ZX_IMG_E_TRUNC, "JPEG: segment cut short");
        return 0;
    }
    *len = zi_be16(d + pos);
    if (*len < 2) {
        *rc = zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad segment length");
        return 0;
    }
    if (n - pos < *len) {
        *rc = zi_fail(info, ZX_IMG_E_TRUNC, "JPEG: segment cut short");
        return 0;
    }
    return pos;
}

static void app(const uint8_t *s, uint32_t len, uint32_t m, uint8_t *orient, bool *adobe,
                uint8_t *xf)
{
    if (m == 0xE1 && len >= 6 && s[0] == 'E' && s[1] == 'x' && s[2] == 'i' && s[3] == 'f' &&
        !s[4] && !s[5])
        *orient = zi_exif_orientation(s + 6, len - 6);
    if (m == 0xEE && len >= 12 && s[0] == 'A' && s[1] == 'd' && s[2] == 'o' && s[3] == 'b' &&
        s[4] == 'e') {
        *adobe = true;
        *xf = s[11];
    }
}

int zi_jpeg_probe(const uint8_t *d, uint32_t n, zx_image_info_t *info)
{
    if (n < 2 || d[0] != 0xFF || d[1] != 0xD8) return zi_fail(info, ZX_IMG_E_FORMAT, "not a JPEG");
    jhdr_t f;
    zi_zero(&f, sizeof f);
    f.orient = 1;
    bool adobe = false;
    uint8_t xf = 0;
    uint32_t pos = 2;
    for (;;) {
        uint32_t m, len;
        int rc = 0;
        pos = segment(d, n, pos, &m, &len, &rc, info);
        if (!pos) return rc;
        if (m == 0xD9 || m == 0xDA) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: no frame header");
        if (m == 0xD8) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: second SOI");
        const uint8_t *s = d + pos + 2;
        if (len) app(s, len - 2, m, &f.orient, &adobe, &xf);
        if (is_sof(m)) {
            if (!sof_kind(m, info, &rc)) return rc;
            rc = sof(&f, s, len - 2, info);
            if (rc) return rc;
            f.progressive = m == 0xC2;
            break;
        }
        pos += len;
    }
    uint64_t w = need(&f);
    if (w > 0xFFFFF000u) return zi_fail(info, ZX_IMG_E_SIZE, "JPEG: too large");
    info->format = ZX_IMG_JPEG;
    info->orientation = f.orient;
    info->w = f.orient >= 5 ? f.h : f.w;
    info->h = f.orient >= 5 ? f.w : f.h;
    info->work = (uint32_t) w;
    info->progressive = f.progressive;
    return ZX_IMG_OK;
}

static int dht(jdec_t *j, const uint8_t *s, uint32_t len, zx_image_info_t *info)
{
    while (len) {
        if (len < 17) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad DHT");
        uint32_t tc = s[0] >> 4, th = s[0] & 15, total = 0;
        for (int i = 0; i < 16; i++) total += s[1 + i];
        if (tc > 1 || th > 3 || len < 17 + total)
            return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad DHT");
        if (build(tc ? &j->ac[th] : &j->dc[th], s + 1, s + 17, total))
            return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad Huffman table");
        s += 17 + total;
        len -= 17 + total;
    }
    return 0;
}

static int dqt(jdec_t *j, const uint8_t *s, uint32_t len, zx_image_info_t *info)
{
    while (len) {
        uint32_t pq = s[0] >> 4, tq = s[0] & 15, sz = 1 + 64 * (pq + 1);
        if (pq > 1 || tq > 3 || len < sz) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad DQT");
        for (int k = 0; k < 64; k++)
            j->qt[tq][ZZ[k]] = (uint16_t) (pq ? zi_be16(s + 1 + 2 * k) : s[1 + k]);
        j->qdef[tq] = true;
        s += sz;
        len -= sz;
    }
    return 0;
}

static int sos(jdec_t *j, const uint8_t *s, uint32_t len, zx_image_info_t *info)
{
    if (!j->sof) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: scan before frame");
    if (len < 1) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad SOS");
    j->ns = s[0];
    if (j->ns < 1 || j->ns > j->nc || len != 4 + 2 * j->ns)
        return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad SOS");
    uint32_t blocks = 0;
    for (uint32_t i = 0; i < j->ns; i++) {
        jcomp_t *c = 0;
        for (uint32_t k = 0; k < j->nc; k++)
            if (j->c[k].id == s[1 + 2 * i]) c = &j->c[k];
        for (uint32_t k = 0; k < i; k++)
            if (j->sc[k] == c) c = 0;
        if (!c) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad scan component");
        c->td = s[2 + 2 * i] >> 4;
        c->ta = s[2 + 2 * i] & 15;
        if (c->td > 3 || c->ta > 3) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad table selector");
        j->sc[i] = c;
        blocks += (uint32_t) c->h * c->v;
    }
    const uint8_t *p = s + 1 + 2 * j->ns;
    j->ss = p[0];
    j->se = p[1];
    j->ah = p[2] >> 4;
    j->al = p[2] & 15;
    if (j->ns > 1 && blocks > 10)
        return zi_fail(info, ZX_IMG_E_DATA, "JPEG: too many blocks per MCU");
    if (j->progressive) {
        bool ok = j->ss <= j->se && j->se <= 63 && j->al <= 13 && j->ah <= 13 &&
                  (j->ss == 0 ? j->se == 0 : j->ns == 1);
        if (!ok) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad progressive scan");
    } else if (j->ss != 0 || j->se != 63 || j->ah || j->al) {
        return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad sequential scan");
    }
    for (uint32_t i = 0; i < j->ns; i++) {
        jcomp_t *c = j->sc[i];
        bool wdc = !j->progressive || (j->ss == 0 && j->ah == 0),
             wac = !j->progressive || j->ss > 0;
        jhuff_t *t[2] = {wdc ? &j->dc[c->td] : 0, wac ? &j->ac[c->ta] : 0};
        uint32_t sel[2] = {c->td, c->ta};
        for (int k = 0; k < 2; k++) {
            if (!t[k] || t[k]->defined) continue;
            if (sel[k] > 1) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: Huffman table missing");
            if (k == 0)
                build(t[k], STD_DC_BITS[sel[k]], STD_DC_VALS, 12);
            else
                build(t[k], STD_AC_BITS[sel[k]], STD_AC_VALS[sel[k]], 162);
        }
        if (!c->latched) {
            if (!j->qdef[c->tq])
                return zi_fail(info, ZX_IMG_E_DATA, "JPEG: quantisation table missing");
            for (int k = 0; k < 64; k++) c->q[k] = j->qt[c->tq][k];
            c->latched = true;
        }
    }
    return 0;
}

static void comp_row(const jdec_t *j, const jcomp_t *c, uint32_t y, uint8_t *o)
{
    uint32_t fh = j->hmax / c->h, fv = j->vmax / c->v, W = j->w, cw = c->cw, st = c->bw * 8;
    const uint8_t *P = c->plane;
    if (fv == 2 && fh <= 2) {
        uint32_t iy = y / 2, far = (y & 1) ? iy + 1 : (iy ? iy - 1 : 0);
        if (far > c->ch - 1) far = c->ch - 1;
        const uint8_t *a = P + (size_t) iy * st, *b = P + (size_t) far * st;
        if (fh == 1) {
            for (uint32_t x = 0; x < W; x++)
                o[x] = (uint8_t) ((3u * a[x] + b[x] + ((y & 1) ? 2u : 1u)) >> 2);
            return;
        }
        for (uint32_t x = 0; x < W; x++) {
            uint32_t ix = x / 2, cs = 3u * a[ix] + b[ix];
            if (!(x & 1))
                o[x] = (uint8_t) (ix == 0 ? (cs * 4 + 8) >> 4
                                          : (cs * 3 + 3u * a[ix - 1] + b[ix - 1] + 8) >> 4);
            else
                o[x] = (uint8_t) (ix + 1 >= cw ? (cs * 4 + 7) >> 4
                                               : (cs * 3 + 3u * a[ix + 1] + b[ix + 1] + 7) >> 4);
        }
        return;
    }
    const uint8_t *r = P + (size_t) (y / fv) * st;
    if (fh == 1) {
        for (uint32_t x = 0; x < W; x++) o[x] = r[x];
    } else if (fh == 2 && fv == 1) {
        for (uint32_t x = 0; x < W; x++) {
            uint32_t ix = x / 2;
            if (!(x & 1))
                o[x] = (uint8_t) (ix == 0 ? r[0] : (3u * r[ix] + r[ix - 1] + 1) >> 2);
            else
                o[x] = (uint8_t) (ix + 1 >= cw ? r[ix] : (3u * r[ix] + r[ix + 1] + 2) >> 2);
        }
    } else {
        for (uint32_t x = 0; x < W; x++) o[x] = r[x / fh];
    }
}

int zi_jpeg_decode(const uint8_t *d, uint32_t n, uint8_t *work, uint32_t worklen, uint32_t *px,
                   zx_image_info_t *info)
{
    int rc = zi_jpeg_probe(d, n, info);
    if (rc) return rc;
    if (worklen < info->work) return zi_fail(info, ZX_IMG_E_SPACE, "JPEG: work too small");
    jdec_t *j = (jdec_t *) (void *) work;
    zi_zero(j, sizeof *j);
    j->d = d;
    j->n = n;
    uint8_t *mem = work + ((sizeof(jdec_t) + 7) & ~(size_t) 7);
    uint8_t orient = 1;
    bool eoi = false;
    for (uint32_t pos = 2; !eoi;) {
        uint32_t m, len;
        pos = segment(d, n, pos, &m, &len, &rc, info);
        if (!pos) return rc;
        const uint8_t *s = d + pos + 2;
        uint32_t sl = len ? len - 2 : 0;
        if (m == 0xD9) {
            eoi = true;
        } else if (m == 0xD8) {
            return zi_fail(info, ZX_IMG_E_DATA, "JPEG: second SOI");
        } else if (is_sof(m)) {
            jhdr_t f;
            zi_zero(&f, sizeof f);
            if (j->sof) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: second frame header");
            if (!sof_kind(m, info, &rc)) return rc;
            if ((rc = sof(&f, s, sl, info))) return rc;
            j->sof = true;
            j->progressive = m == 0xC2;
            j->w = f.w, j->h = f.h, j->nc = f.nc, j->hmax = f.hmax, j->vmax = f.vmax;
            j->mcux = (f.w + 8 * f.hmax - 1) / (8 * f.hmax);
            j->mcuy = (f.h + 8 * f.vmax - 1) / (8 * f.vmax);
            if (need(&f) > worklen) return zi_fail(info, ZX_IMG_E_SPACE, "JPEG: work too small");
            for (uint32_t i = 0; i < f.nc; i++) {
                jcomp_t *c = &j->c[i];
                c->id = s[6 + 3 * i];
                c->h = f.hs[i];
                c->v = f.vs[i];
                c->tq = s[8 + 3 * i];
                c->bw = j->mcux * c->h;
                c->bh = j->mcuy * c->v;
                c->cw = (f.w * c->h + f.hmax - 1) / f.hmax;
                c->ch = (f.h * c->v + f.vmax - 1) / f.vmax;
                size_t nb = (size_t) c->bw * c->bh;
                c->coef = (int16_t *) (void *) mem;
                for (size_t k = 0; k < nb * 64; k++) c->coef[k] = 0;
                mem += nb * 128;
                c->plane = mem;
                mem += nb * 64;
            }
        } else if (m == 0xC4) {
            if ((rc = dht(j, s, sl, info))) return rc;
        } else if (m == 0xDB) {
            if ((rc = dqt(j, s, sl, info))) return rc;
        } else if (m == 0xDD) {
            if (sl != 2) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: bad DRI");
            j->ri = zi_be16(s);
        } else if (m == 0xCC) {
            return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: arithmetic coding not supported");
        } else if (m == 0xDC) {
            return zi_fail(info, ZX_IMG_E_UNSUPPORTED, "JPEG: DNL not supported");
        } else if (m >= 0xE0 && m <= 0xEF) {
            app(s, sl, m, &orient, &j->adobe, &j->adobe_xf);
        } else if (m == 0xDA) {
            if ((rc = sos(j, s, sl, info))) return rc;
            if (++j->scans > ZUI_JPEG_MAX_SCANS)
                return zi_fail(info, ZX_IMG_E_DATA, "JPEG: too many scans");
            j->pos = pos + len;
            rc = scan(j);
            if (rc == ZX_IMG_E_TRUNC) return zi_fail(info, rc, "JPEG: cut short inside a scan");
            if (rc) return zi_fail(info, rc, "JPEG: corrupt entropy-coded data");
            /* skip to the next marker (tolerating stray bytes, as libjpeg does) */
            pos = j->pos;
            while (pos + 1 < n && !(d[pos] == 0xFF && d[pos + 1] != 0 && d[pos + 1] != 0xFF &&
                                    !(d[pos + 1] >= 0xD0 && d[pos + 1] <= 0xD7)))
                pos++;
            if (pos + 1 >= n) return zi_fail(info, ZX_IMG_E_TRUNC, "JPEG: ends without EOI");
            continue;
        }
        pos += len;
    }
    if (!j->sof || !j->scans) return zi_fail(info, ZX_IMG_E_DATA, "JPEG: no image data");
    for (uint32_t i = 0; i < j->nc; i++)
        if (!j->c[i].latched)
            return zi_fail(info, ZX_IMG_E_DATA, "JPEG: a component was never coded");

    for (uint32_t i = 0; i < j->nc; i++) {
        jcomp_t *c = &j->c[i];
        for (uint32_t by = 0; by < c->bh; by++)
            for (uint32_t bx = 0; bx < c->bw; bx++)
                idct(c->coef + ((size_t) by * c->bw + bx) * 64, c->q,
                     c->plane + (size_t) by * 8 * c->bw * 8 + bx * 8, c->bw * 8);
    }
    bool rgb = j->nc == 3 && ((j->adobe && j->adobe_xf == 0) ||
                              (j->c[0].id == 'R' && j->c[1].id == 'G' && j->c[2].id == 'B'));
    uint8_t *row[3] = {mem, mem + j->w, mem + 2 * (size_t) j->w};
    zi_out_t o = {px, j->w, j->h, info->orientation};
    for (uint32_t y = 0; y < j->h; y++) {
        for (uint32_t i = 0; i < j->nc; i++) comp_row(j, &j->c[i], y, row[i]);
        int64_t base, step;
        zi_row(&o, y, &base, &step);
        for (uint32_t x = 0; x < j->w; x++) {
            uint32_t c;
            int32_t Y = row[0][x];
            if (j->nc == 1) {
                c = zi_argb(255, (uint32_t) Y, (uint32_t) Y, (uint32_t) Y);
            } else if (rgb) {
                c = zi_argb(255, (uint32_t) Y, row[1][x], row[2][x]);
            } else {
                int64_t cb = (int64_t) row[1][x] - 128, cr = (int64_t) row[2][x] - 128;
                int64_t R = Y + asr(91881 * cr + 32768, 16);
                int64_t G = Y + asr(-22554 * cb + 32768 - 46802 * cr, 16);
                int64_t B = Y + asr(116130 * cb + 32768, 16);
                c = zi_argb(255, c8(R), c8(G), c8(B));
            }
            px[base + (int64_t) x * step] = c;
        }
    }
    return ZX_IMG_OK;
}
