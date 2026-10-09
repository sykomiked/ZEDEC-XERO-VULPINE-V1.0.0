/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* zt_lattice.c — E8 and Leech nearest-point quantisers. See zt_lattice.h
 * T17.
 *
 * Both work the same way. A value x at step size s is split exactly into an
 * integer part T = floor(k x / s) and a remainder R = k x - T s in [0, s)
 * (k = 2 for E8, 4 for Leech), using only 32-bit division. Every distance is
 * then a sum of squared residuals (k x - n s)^2 in int64, exact, and the
 * caps on the scale keep those sums below 2^63. */
#include "zt_lattice.h"

#define E8_CLAMP    124   /* doubled units: inputs saturate at +-62 s */
#define LEECH_CLAMP 30000 /* quarter units: inputs saturate at +-7500 s */
#define GOLAY_G     0xC75u

static const uint32_t e8_shell_end[5] = {ZT_E8_SHELL0, ZT_E8_SHELL1, ZT_E8_SHELL2, ZT_E8_SHELL3,
                                         ZT_E8_SHELL4};

/* floor(x / s) and x - s floor(x / s) in [0, s), for s > 0. */
static void floordiv(int32_t x, int32_t s, int64_t *q, int64_t *r)
{
    int32_t qq = x / s, rr = x - qq * s;
    if (rr < 0) {
        qq--;
        rr += s;
    }
    *q = qq;
    *r = rr;
}

/* p / 2^k rounded to nearest, halves away from zero, saturated to int32. */
static zt_fx round_shift(int64_t p, uint32_t k)
{
    uint64_t u = p < 0 ? (uint64_t) 0 - (uint64_t) p : (uint64_t) p;
    u = (u + ((uint64_t) 1 << (k - 1))) >> k;
    if (u > (uint64_t) INT32_MAX) u = (uint64_t) INT32_MAX;
    return p < 0 ? -(zt_fx) u : (zt_fx) u;
}

static int64_t abs64(int64_t v)
{
    return v < 0 ? -v : v;
}

/* ---- E8 ---------------------------------------------------------------- */

/* 2x = T s + R with R in [0, s), T clamped to +-E8_CLAMP. */
static void e8_target(const zt_fx x[8], int32_t s, int32_t T[8], int64_t R[8])
{
    for (uint32_t i = 0; i < 8; i++) {
        int64_t q, r;
        floordiv(x[i], s, &q, &r);
        int64_t t = 2 * q;
        r *= 2;
        if (r >= s) {
            t++;
            r -= s;
        }
        if (t > E8_CLAMP || (t == E8_CLAMP && r > 0)) {
            t = E8_CLAMP;
            r = 0;
        } else if (t < -E8_CLAMP) {
            t = -E8_CLAMP;
            r = 0;
        }
        T[i] = (int32_t) t;
        R[i] = r;
    }
}

/* Nearest point of 2E8 whose coordinates all have parity p, and its squared
 * distance (in doubled Q16 units). Round each coordinate to the nearest value
 * of parity p; if the sum is 2 mod 4, move the coordinate with the largest
 * residual the other way by 2, which costs 4 s^2 - 4 s |d|, the least. */
static int64_t e8_coset(const int32_t T[8], const int64_t R[8], int64_t s, uint32_t p, int8_t v[8])
{
    int64_t D = 0, worst = -1, wd = 0;
    uint32_t wi = 0, sum = 0;
    for (uint32_t i = 0; i < 8; i++) {
        int32_t n = T[i];
        int64_t d = R[i];
        if (((uint32_t) n & 1u) != p) { /* T + 1 is nearer than T - 1 */
            n++;
            d -= s;
        }
        v[i] = (int8_t) n;
        D += d * d;
        sum += (uint32_t) n;
        if (abs64(d) > worst) {
            worst = abs64(d);
            wd = d;
            wi = i;
        }
    }
    if (sum & 3u) {
        v[wi] = (int8_t) (v[wi] + (wd < 0 ? -2 : 2));
        D += 4 * s * s - 4 * s * worst;
    }
    return D;
}

static int32_t e8_cap(zt_fx scale)
{
    return scale > ZT_E8_SCALE_MAX ? ZT_E8_SCALE_MAX : scale;
}

bool zt_e8_nearest(const zt_fx x[8], zt_fx scale, int8_t out2[8])
{
    int32_t T[8];
    int64_t R[8];
    int8_t odd[8];
    if (scale <= 0) {
        for (uint32_t i = 0; i < 8; i++) out2[i] = 0;
        return false;
    }
    int32_t s = e8_cap(scale);
    e8_target(x, s, T, R);
    int64_t de = e8_coset(T, R, s, 0, out2);
    int64_t dodd = e8_coset(T, R, s, 1, odd);
    if (dodd < de)
        for (uint32_t i = 0; i < 8; i++) out2[i] = odd[i];
    return true;
}

bool zt_e8_is_point(const int8_t v2[8])
{
    uint32_t p = (uint32_t) v2[0] & 1u, sum = 0;
    for (uint32_t i = 0; i < 8; i++) {
        if (((uint32_t) v2[i] & 1u) != p) return false;
        sum += (uint32_t) v2[i];
    }
    return (sum & 3u) == 0;
}

int32_t zt_e8_norm2(const int8_t v2[8])
{
    int32_t n = 0;
    for (uint32_t i = 0; i < 8; i++) n += (int32_t) v2[i] * v2[i];
    return n;
}

void zt_e8_dequantize(const int8_t v2[8], zt_fx scale, zt_fx out[8])
{
    for (uint32_t i = 0; i < 8; i++) out[i] = round_shift((int64_t) v2[i] * scale, 1);
}

/* Depth-first enumeration of the points of doubled norm `target`, in
 * lexicographic order (coordinate 0 most significant, -5 first). */
static void e8_gen(int8_t *table, uint32_t *count, int8_t cur[8], uint32_t depth, int32_t norm,
                   int32_t target, uint32_t sum)
{
    if (depth == 8) {
        if (norm == target && (sum & 3u) == 0) {
            for (uint32_t i = 0; i < 8; i++) table[*count * 8 + i] = cur[i];
            (*count)++;
        }
        return;
    }
    for (int32_t v = -5; v <= 5; v++) {
        if (depth && (((uint32_t) v ^ (uint32_t) cur[0]) & 1u)) continue;
        /* every later coordinate costs at least 1 if odd */
        int32_t rest = (int32_t) (7 - depth) * (int32_t) ((uint32_t) v & 1u);
        if (norm + v * v + rest > target) continue;
        cur[depth] = (int8_t) v;
        e8_gen(table, count, cur, depth + 1, norm + v * v, target, sum + (uint32_t) v);
    }
}

uint32_t zt_e8_codebook_build(int8_t *table)
{
    int8_t cur[8];
    uint32_t count = 0;
    for (int32_t sh = 0; sh < 5; sh++) e8_gen(table, &count, cur, 0, 0, 8 * sh, 0);
    return count;
}

/* Lexicographic order of a codebook point is numeric order of this key. */
static uint32_t e8_key(const int8_t v[8])
{
    uint32_t k = 0;
    for (uint32_t i = 0; i < 8; i++) k = (k << 4) | (uint32_t) (v[i] + 8);
    return k;
}

int32_t zt_e8_encode(const int8_t *table, const int8_t v2[8])
{
    if (!zt_e8_is_point(v2)) return -1;
    int32_t n = zt_e8_norm2(v2); /* a multiple of 8 for a lattice point */
    if (n > 32) return -1;
    uint32_t sh = (uint32_t) n >> 3;
    uint32_t lo = sh ? e8_shell_end[sh - 1] : 0, hi = e8_shell_end[sh];
    uint32_t key = e8_key(v2);
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2, k = e8_key(table + mid * 8);
        if (k == key) return (int32_t) mid;
        if (k < key)
            lo = mid + 1;
        else
            hi = mid;
    }
    return -1;
}

bool zt_e8_decode(const int8_t *table, uint32_t index, int8_t v2[8])
{
    if (index >= ZT_E8_CODEBOOK) return false;
    for (uint32_t i = 0; i < 8; i++) v2[i] = table[index * 8 + i];
    return true;
}

/* Distance to X up to terms that do not depend on v, divided by s:
 * |X - v s|^2 = |X|^2 + s (s |v|^2 - 2 X.v). Small enough for any input. */
static int64_t e8_metric(const int32_t T[8], const int64_t R[8], int64_t s, const int8_t v[8])
{
    int64_t m = s * zt_e8_norm2(v);
    for (uint32_t i = 0; i < 8; i++) m -= 2 * ((int64_t) T[i] * s + R[i]) * v[i];
    return m;
}

/* Root r of E8 (r < 240) in doubled coordinates: r < 112 is +-2 at a pair
 * of places (28 pairs, 4 signs), the rest (+-1)^8 with an even number of
 * minus signs (the 128 bytes of even weight, in order). */
static void e8_root(uint32_t r, int8_t e[8])
{
    for (uint32_t i = 0; i < 8; i++) e[i] = 0;
    if (r < 112) {
        uint32_t pair = r >> 2, a = 0, b = 1;
        for (uint32_t k = 0; k < pair; k++)
            if (++b == 8) {
                a++;
                b = a + 1;
            }
        e[a] = (int8_t) ((r & 1u) ? -2 : 2);
        e[b] = (int8_t) ((r & 2u) ? -2 : 2);
        return;
    }
    uint32_t k = r - 112, m = 0; /* the k-th byte of even weight */
    for (;; m++) {
        uint32_t w = m;
        w ^= w >> 4;
        w ^= w >> 2;
        w ^= w >> 1;
        if (!(w & 1u) && k-- == 0) break;
    }
    for (uint32_t i = 0; i < 8; i++) e[i] = (int8_t) (((m >> i) & 1u) ? -1 : 1);
}

uint16_t zt_e8_quantize(const int8_t *table, const zt_fx x[8], zt_fx scale)
{
    int8_t v[8], best[8];
    zt_fx xs[8];
    int32_t T[8];
    int64_t R[8];
    if (!zt_e8_nearest(x, scale, v)) return 0; /* index 0 is the origin */
    if (zt_e8_norm2(v) <= 32) return (uint16_t) zt_e8_encode(table, v);
    int32_t s = e8_cap(scale);
    e8_target(x, s, T, R);
    int64_t bm = 0; /* the origin: always inside */
    for (uint32_t i = 0; i < 8; i++) best[i] = 0;
    uint32_t lo = 0, hi = 65536;
    for (uint32_t it = 0; it < 16; it++) {
        uint32_t mid = (lo + hi) >> 1;
        for (uint32_t i = 0; i < 8; i++) xs[i] = round_shift((int64_t) x[i] * mid, 16);
        zt_e8_nearest(xs, s, v);
        if (zt_e8_norm2(v) > 32) {
            hi = mid;
            continue;
        }
        lo = mid;
        int64_t m = e8_metric(T, R, s, v);
        if (m < bm) {
            bm = m;
            for (uint32_t i = 0; i < 8; i++) best[i] = v[i];
        }
    }
    /* Local search: step by any of the 240 roots while that stays in the ball
     * and lowers the distance to x, until no root does (at most 64 steps). */
    for (uint32_t it = 0; it < 64; it++) {
        int8_t bestr[8];
        int64_t bd = 0;
        for (uint32_t r = 0; r < 240; r++) {
            int8_t e[8], t[8];
            e8_root(r, e);
            for (uint32_t i = 0; i < 8; i++) t[i] = (int8_t) (best[i] + e[i]);
            if (zt_e8_norm2(t) > 32) continue;
            int64_t d = e8_metric(T, R, s, t) - bm;
            if (d < bd) {
                bd = d;
                for (uint32_t i = 0; i < 8; i++) bestr[i] = t[i];
            }
        }
        if (bd == 0) break;
        bm += bd;
        for (uint32_t i = 0; i < 8; i++) best[i] = bestr[i];
    }
    return (uint16_t) zt_e8_encode(table, best);
}

zt_fx zt_e8_rms(const zt_fx *x, uint32_t n)
{
    uint64_t ss = 0;
    if (n == 0) return 0;
    for (uint32_t i = 0; i < n; i++) ss += (uint64_t) ((int64_t) x[i] * x[i]) >> 16;
    uint64_t mean = zt_udiv64(ss, n, 0); /* Q16, below 2^46 */
    return (zt_fx) zt_isqrt64(mean << 16);
}

zt_fx zt_e8_scale(const zt_fx *x, uint32_t n)
{
    int64_t rms = zt_e8_rms(x, n);
    bool any = false;
    for (uint32_t i = 0; i < n; i++) any |= x[i] != 0;
    if (!any) return 0;
    int64_t s = (rms * ZT_E8_RMS_GAIN + 32768) >> 16;
    if (s < 1) s = 1;
    if (s > ZT_E8_SCALE_MAX) s = ZT_E8_SCALE_MAX;
    return (zt_fx) s;
}

/* ---- Golay ------------------------------------------------------------- */

static uint32_t parity32(uint32_t w)
{
    w ^= w >> 16;
    w ^= w >> 8;
    w ^= w >> 4;
    w ^= w >> 2;
    w ^= w >> 1;
    return w & 1u;
}

uint32_t zt_golay_codeword(uint32_t i)
{
    uint32_t w = 0;
    for (uint32_t b = 0; b < 12; b++)
        if ((i >> b) & 1u) w ^= GOLAY_G << b;
    return w | (parity32(w) << 23);
}

bool zt_golay_is_codeword(uint32_t w)
{
    if ((w >> 24) || parity32(w)) return false;
    uint32_t r = w & 0x7FFFFFu; /* divisible by g(x)? */
    for (uint32_t b = 22; b >= 11; b--)
        if ((r >> b) & 1u) r ^= GOLAY_G << (b - 11);
    return r == 0;
}

/* ---- Leech ------------------------------------------------------------- */

void zt_leech_init(zt_leech_work_t *w)
{
    for (uint32_t i = 0; i < 4096; i++) w->golay[i] = zt_golay_codeword(i);
}

/* Per coordinate and residue class c mod 4: the nearest value n = c (mod 4),
 * its residual d = 4x - n s, and the extra cost of the next value of the
 * class on the other side, (4 s - |d|)^2 - d^2 = 16 s^2 - 8 s |d|. */
static void leech_classes(zt_leech_work_t *w, const zt_fx x[24], int64_t s)
{
    for (uint32_t i = 0; i < 24; i++) {
        int64_t q, r;
        floordiv(x[i], (int32_t) s, &q, &r);
        int64_t t = 4 * q;
        r *= 4;
        while (r >= s) {
            t++;
            r -= s;
        }
        if (t > LEECH_CLAMP || (t == LEECH_CLAMP && r > 0)) {
            t = LEECH_CLAMP;
            r = 0;
        } else if (t < -LEECH_CLAMP) {
            t = -LEECH_CLAMP;
            r = 0;
        }
        for (uint32_t c = 0; c < 4; c++) {
            uint32_t k = (c - (uint32_t) t) & 3u; /* steps up to class c */
            int64_t n = k == 3 ? t - 1 : t + k;
            int64_t d = r - (n - t) * s;
            w->near[i][c] = (int32_t) n;
            w->res[i][c] = d;
            w->delta[i][c] = 16 * s * s - 8 * s * abs64(d);
        }
    }
}

/* For each byte of the codeword and each parity m, the cost, the sum mod 8
 * and the cheapest fix of the 8 coordinates it covers. Coordinate j takes
 * class m + 2 * bit_j. Cost and sum are built from the pattern with its
 * lowest bit cleared, so each entry is one addition. */
static void leech_tables(zt_leech_work_t *w)
{
    for (uint32_t g = 0; g < 3; g++)
        for (uint32_t m = 0; m < 2; m++) {
            const uint32_t base = 8 * g;
            int64_t c0 = 0;
            uint32_t s0 = 0;
            for (uint32_t j = 0; j < 8; j++) {
                c0 += w->res[base + j][m] * w->res[base + j][m];
                s0 += (uint32_t) w->near[base + j][m];
            }
            w->cost[g][m][0] = c0;
            w->sum8[g][m][0] = (uint8_t) (s0 & 7u);
            for (uint32_t b = 1; b < 256; b++) {
                uint32_t j = 0;
                while (!((b >> j) & 1u)) j++;
                uint32_t prev = b & (b - 1u), i = base + j;
                int64_t on = w->res[i][m + 2], off = w->res[i][m];
                w->cost[g][m][b] = w->cost[g][m][prev] + on * on - off * off;
                w->sum8[g][m][b] = (uint8_t) ((w->sum8[g][m][prev] + (uint32_t) w->near[i][m + 2] -
                                               (uint32_t) w->near[i][m]) &
                                              7u);
            }
            /* The cheapest fix is the smaller of the two nibbles' cheapest. */
            int64_t lo[16], hi[16];
            for (uint32_t b = 0; b < 16; b++) {
                lo[b] = hi[b] = INT64_MAX;
                for (uint32_t j = 0; j < 4; j++) {
                    int64_t dl = w->delta[base + j][m + 2 * ((b >> j) & 1u)];
                    int64_t dh = w->delta[base + 4 + j][m + 2 * ((b >> j) & 1u)];
                    if (dl < lo[b]) lo[b] = dl;
                    if (dh < hi[b]) hi[b] = dh;
                }
            }
            for (uint32_t b = 0; b < 256; b++) {
                int64_t l = lo[b & 15u], h = hi[b >> 4];
                w->fix[g][m][b] = l < h ? l : h;
            }
        }
}

static int64_t min3(int64_t a, int64_t b, int64_t c)
{
    int64_t m = a < b ? a : b;
    return m < c ? m : c;
}

bool zt_leech_nearest(zt_leech_work_t *w, const zt_fx x[24], zt_fx scale, int16_t out[24])
{
    if (scale <= 0) {
        for (uint32_t i = 0; i < 24; i++) out[i] = 0;
        return false;
    }
    int64_t s = scale > ZT_LEECH_SCALE_MAX ? ZT_LEECH_SCALE_MAX : scale;
    leech_classes(w, x, s);
    leech_tables(w);
    /* Every coset: rounding cost; if the sum is wrong mod 8 add the cheapest
     * single +-4 move (the D24 fix-up). Fixes are never negative, so a coset
     * whose rounding cost already reaches the best is skipped. */
    int64_t best = INT64_MAX;
    uint32_t bk = 0, bm = 0;
    for (uint32_t k = 0; k < 4096 && best; k++) {
        uint32_t cw = w->golay[k], b0 = cw & 255u, b1 = (cw >> 8) & 255u, b2 = cw >> 16;
        for (uint32_t m = 0; m < 2; m++) {
            int64_t c = w->cost[0][m][b0] + w->cost[1][m][b1] + w->cost[2][m][b2];
            if (c >= best) continue;
            uint32_t s8 = (uint32_t) (w->sum8[0][m][b0] + w->sum8[1][m][b1] + w->sum8[2][m][b2]);
            if ((s8 & 7u) != 4 * m) c += min3(w->fix[0][m][b0], w->fix[1][m][b1], w->fix[2][m][b2]);
            if (c < best) {
                best = c;
                bk = k;
                bm = m;
            }
        }
    }
    /* Rebuild the winner, fixing the first cheapest coordinate. */
    uint32_t cw = w->golay[bk], sum = 0, fi = 0;
    int64_t f = INT64_MAX;
    for (uint32_t i = 0; i < 24; i++) {
        uint32_t c = bm + 2 * ((cw >> i) & 1u);
        out[i] = (int16_t) w->near[i][c];
        sum += (uint32_t) w->near[i][c];
        if (w->delta[i][c] < f) {
            f = w->delta[i][c];
            fi = i;
        }
    }
    if ((sum & 7u) != 4 * bm) {
        uint32_t c = bm + 2 * ((cw >> fi) & 1u);
        out[fi] = (int16_t) (out[fi] + (w->res[fi][c] < 0 ? -4 : 4));
    }
    return true;
}

bool zt_leech_is_point(const int16_t v[24])
{
    uint32_t m = (uint32_t) v[0] & 1u, mask = 0, sum = 0;
    for (uint32_t i = 0; i < 24; i++) {
        uint32_t u = (uint32_t) v[i];
        if ((u & 1u) != m) return false;
        if ((u & 3u) == 2 + m) mask |= 1u << i;
        sum += u;
    }
    return zt_golay_is_codeword(mask) && (sum & 7u) == 4 * m;
}

void zt_leech_dequantize(const int16_t v[24], zt_fx scale, zt_fx out[24])
{
    for (uint32_t i = 0; i < 24; i++) out[i] = round_shift((int64_t) v[i] * scale, 2);
}
