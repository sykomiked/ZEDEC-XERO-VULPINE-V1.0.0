/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxvs.c — ZXVS descriptors (zxvs.h): describe, check, curves, similarity.
 * Integer only, freestanding, no allocation (S1). */
#include "zxvs.h"

#include <stddef.h>

#include "../robin_debanks/sha256.h"

#define TOL_DEF   64u    /* Q16 of range: ~0.1 %                       */
#define MAX_SPAN  65536u /* samples per curve segment, at most        */
#define ENT_MAXW  2048u  /* entropy windows                            */
#define HIL_MAXK  6u
#define MAX_COLS  256u  /* columns examined for numbers               */
#define FOLD_STK  4096u /* nesting tracked for JSON / XML syntax      */
#define NAME_MAX_ 31u

/* ---- tiny libc-free helpers -------------------------------------------- */
static void zset(void *p, int v, size_t n)
{
    uint8_t *d = p;
    while (n--) *d++ = (uint8_t) v;
}
static void zcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
}
static int64_t sar64(int64_t v, int s)
{
    return v >= 0 ? v >> s : ~((~v) >> s);
}
static uint32_t clz64(uint64_t x)
{
    uint32_t n = 0;
    if (!x) return 64;
    while (!(x & ((uint64_t) 1 << 63))) {
        x <<= 1;
        n++;
    }
    return n;
}

/* u128 as hi:lo */
typedef struct {
    uint64_t hi, lo;
} u128;
static u128 mul64(uint64_t a, uint64_t b)
{
    uint64_t a0 = a & 0xFFFFFFFFu, a1 = a >> 32, b0 = b & 0xFFFFFFFFu, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFu) + (p10 & 0xFFFFFFFFu);
    u128 r;
    r.lo = (mid << 32) | (p00 & 0xFFFFFFFFu);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
    return r;
}
static u128 add128(u128 a, u128 b)
{
    u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo);
    return r;
}
/* floor(a / d), d > 0; quotient saturates at 2^64 - 1 */
static uint64_t div128(u128 a, uint64_t d)
{
    if (a.hi >= d) return UINT64_MAX;
    uint64_t q = 0, r = a.hi;
    for (int i = 63; i >= 0; i--) {
        uint64_t top = r >> 63;
        r = (r << 1) | ((a.lo >> i) & 1u);
        if (top || r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    return q;
}
static uint64_t muldiv(uint64_t a, uint64_t b, uint64_t d)
{
    return div128(mul64(a, b), d);
}
static uint64_t isqrt64(uint64_t v)
{
    uint64_t r = 0, b = (uint64_t) 1 << 62;
    while (b > v) b >>= 2;
    while (b) {
        if (v >= r + b) {
            v -= r + b;
            r = (r >> 1) + b;
        } else {
            r >>= 1;
        }
        b >>= 2;
    }
    return r;
}

/* log2(x) in Q16, x >= 1, truncated */
static uint32_t log2_q16(uint64_t x)
{
    if (x <= 1) return 0;
    uint32_t n = 63 - clz64(x);
    uint64_t m = n >= 30 ? x >> (n - 30) : x << (30 - n); /* [2^30, 2^31) */
    uint32_t f = 0;
    for (int i = 15; i >= 0; i--) {
        m = (m * m) >> 30;
        if (m >= ((uint64_t) 1 << 31)) {
            m >>= 1;
            f |= 1u << i;
        }
    }
    return (n << 16) | f;
}

/* Shannon entropy of a histogram of w bytes, in 1/256 bit (0..2048) */
static uint32_t entropy_q8(const uint32_t *h, uint64_t w)
{
    if (w == 0) return 0;
    uint64_t s = 0;
    for (int i = 0; i < 256; i++)
        if (h[i] > 1) s += (uint64_t) h[i] * log2_q16(h[i]);
    uint64_t lw = log2_q16(w);
    uint64_t t = s / w;
    int64_t hq = (int64_t) lw - (int64_t) t;
    if (hq < 0) hq = 0;
    hq >>= 8;
    return hq > 2048 ? 2048u : (uint32_t) hq;
}

/* ---- writer / reader ---------------------------------------------------- */
typedef struct {
    uint8_t *p;
    uint64_t n, cap;
    bool bad;
} wr_t;
static void w8(wr_t *w, uint32_t v)
{
    if (w->n >= w->cap) {
        w->bad = true;
        return;
    }
    w->p[w->n++] = (uint8_t) v;
}
static void wuv(wr_t *w, uint64_t v)
{
    while (v >= 0x80) {
        w8(w, (uint32_t) (v & 0x7F) | 0x80);
        v >>= 7;
    }
    w8(w, (uint32_t) v);
}
static void wzz(wr_t *w, int64_t v)
{
    wuv(w, ((uint64_t) v << 1) ^ (uint64_t) sar64(v, 63));
}
static void wbytes(wr_t *w, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++) w8(w, s[i]);
}
/* start a section: tag + 4-byte length to patch */
static uint64_t sec_begin(wr_t *w, uint32_t tag)
{
    w8(w, tag);
    uint64_t at = w->n;
    for (int i = 0; i < 4; i++) w8(w, 0);
    return at;
}
static void sec_end(wr_t *w, uint64_t at)
{
    if (w->bad) return;
    uint64_t len = w->n - at - 4;
    if (len > 0xFFFFFFFFu) {
        w->bad = true;
        return;
    }
    for (int i = 0; i < 4; i++) w->p[at + i] = (uint8_t) (len >> (8 * i));
}

typedef struct {
    const uint8_t *p, *e;
    bool bad;
} rd_t;
static uint32_t r8(rd_t *r)
{
    if (r->bad || r->p >= r->e) {
        r->bad = true;
        return 0;
    }
    return *r->p++;
}
static uint64_t ruv(rd_t *r)
{
    uint64_t v = 0;
    for (int i = 0; i < 10; i++) {
        uint32_t b = r8(r);
        if (r->bad) return 0;
        if (i == 9 && b > 1) {
            r->bad = true;
            return 0;
        }
        v |= (uint64_t) (b & 0x7F) << (7 * i);
        if (!(b & 0x80)) return v;
    }
    r->bad = true;
    return 0;
}
static uint64_t ruvr(rd_t *r, uint64_t lo, uint64_t hi)
{
    uint64_t v = ruv(r);
    if (v < lo || v > hi) r->bad = true;
    return v;
}
static int64_t rzz(rd_t *r)
{
    uint64_t u = ruv(r);
    return (int64_t) (u >> 1) ^ -(int64_t) (u & 1);
}
static uint32_t r32(rd_t *r)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= r8(r) << (8 * i);
    return v;
}

/* ---- arena -------------------------------------------------------------- */
typedef struct {
    uint8_t *base;
    uint64_t used, cap;
    bool measure, bad;
} arena_t;
static void *take(arena_t *a, uint64_t bytes)
{
    uint64_t at = (a->used + 15) & ~(uint64_t) 15;
    if (at + bytes < at) {
        a->bad = true;
        return 0;
    }
    a->used = at + bytes;
    if (a->measure) return 0;
    if (a->used > a->cap) {
        a->bad = true;
        return 0;
    }
    return a->base + at;
}

/* ---- the curve fitter (shared by series and the entropy curve) ----------- */
static int64_t lerp_q16(int64_t a, int64_t b, int64_t t)
{
    return a + sar64((b - a) * t, 16);
}
static int64_t bez_eval(int64_t p0, int64_t p1, int64_t p2, int64_t p3, int64_t t)
{
    int64_t a = lerp_q16(p0, p1, t), b = lerp_q16(p1, p2, t), c = lerp_q16(p2, p3, t);
    int64_t d = lerp_q16(a, b, t), e = lerp_q16(b, c, t);
    return lerp_q16(d, e, t);
}
static int64_t sdiv_round(int64_t num, int64_t den) /* den > 0, half away */
{
    if (num >= 0) return (num + den / 2) / den;
    return -((-num + den / 2) / den);
}
static int64_t clamp64(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

typedef struct {
    const int32_t *y;
    uint32_t n, tol;
    uint8_t *keep;    /* 1: breakpoint                                    */
    int32_t *c1, *c2; /* per segment start: P1 - P0, P2 - P0              */
    uint32_t *bp;     /* scratch: the RDP breakpoints                     */
    uint32_t maxerr, rmserr, nseg;
} fit_t;

/* max error of the segment a..b with control values p1, p2; worst at *wx */
static uint32_t seg_err(const int32_t *y, uint32_t a, uint32_t b, int64_t p1, int64_t p2,
                        uint32_t *wx, u128 *sq)
{
    uint32_t worst = 0, L = b - a;
    *wx = a;
    for (uint32_t x = a; x <= b; x++) {
        int64_t t = (int64_t) (((uint64_t) (x - a) << 16) / L);
        int64_t v = bez_eval(y[a], p1, p2, y[b], t);
        int64_t e = v - y[x];
        uint64_t ae = (uint64_t) (e < 0 ? -e : e);
        if (ae > worst) {
            worst = ae > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) ae;
            *wx = x;
        }
        if (sq && x < b) *sq = add128(*sq, mul64(ae, ae));
    }
    return worst;
}

static uint32_t next_keep(const uint8_t *k, uint32_t a, uint32_t n)
{
    uint32_t b = a + 1;
    while (b < n - 1 && !k[b]) b++;
    return b;
}

/* the slope (per sample) at sample i, from samples i - h .. i + h */
static int64_t slope_num(const int32_t *y, uint32_t n, uint32_t i, uint32_t h, int64_t *den)
{
    uint32_t lo = i >= h ? i - h : 0, hi = i + h < n ? i + h : n - 1;
    *den = (int64_t) (hi - lo);
    return (int64_t) y[hi] - y[lo];
}

/* try the span a..b: a cubic with tangents from the samples, else the
 * chord; 1 and the control values when it is within the bound */
static int span_fits(const fit_t *f, uint32_t a, uint32_t b, int64_t *p1, int64_t *p2)
{
    const int32_t *y = f->y;
    int64_t L = (int64_t) (b - a), ya = y[a], yb = y[b];
    uint32_t h = (uint32_t) (L / 4);
    if (h < 1) h = 1;
    if (h > 64) h = 64;
    int64_t da, db;
    int64_t na = slope_num(y, f->n, a, h, &da), nb = slope_num(y, f->n, b, h, &db);
    int64_t c1 = ya + (da ? sdiv_round(na * L, 3 * da) : 0);
    int64_t c2 = yb - (db ? sdiv_round(nb * L, 3 * db) : 0);
    c1 = clamp64(c1, -((int64_t) 1 << 26), ((int64_t) 1 << 26));
    c2 = clamp64(c2, -((int64_t) 1 << 26), ((int64_t) 1 << 26));
    uint32_t wx;
    if (seg_err(y, a, b, c1, c2, &wx, 0) <= f->tol) {
        *p1 = c1;
        *p2 = c2;
        return 1;
    }
    c1 = ya + sdiv_round(yb - ya, 3);
    c2 = ya + sdiv_round(2 * (yb - ya), 3);
    if (seg_err(y, a, b, c1, c2, &wx, 0) <= f->tol) {
        *p1 = c1;
        *p2 = c2;
        return 1;
    }
    return 0;
}

/* The fit: Ramer-Douglas-Peucker breakpoints at the bound (vertical
 * distance to the chord, spans capped at MAX_SPAN), then from each segment
 * start the farthest breakpoint one cubic reaches within the bound
 * (exponential then binary search over the breakpoints), falling back to the
 * farthest sample when not even the next breakpoint can be reached. Every
 * accepted span is checked with the exact evaluator, so maxerr <= tol. */
static void fit(fit_t *f)
{
    uint32_t n = f->n;
    const int32_t *y = f->y;
    f->maxerr = f->rmserr = f->nseg = 0;
    if (n < 2) return;
    for (uint32_t i = 0; i < n; i++) f->keep[i] = 0;
    f->keep[0] = f->keep[n - 1] = 1;
    uint32_t a = 0;
    while (a < n - 1) {
        uint32_t b = next_keep(f->keep, a, n);
        if (b - a > MAX_SPAN) {
            f->keep[a + MAX_SPAN] = 1;
            continue;
        }
        uint64_t best = 0;
        uint32_t bm = 0;
        int64_t L = (int64_t) (b - a);
        for (uint32_t i = a + 1; i < b; i++) {
            int64_t num = (int64_t) y[i] * L -
                          ((int64_t) y[a] * (int64_t) (b - i) + (int64_t) y[b] * (int64_t) (i - a));
            uint64_t dv = (uint64_t) (num < 0 ? -num : num);
            if (dv > best) best = dv, bm = i;
        }
        if (bm && best > (uint64_t) f->tol * (uint64_t) L) {
            f->keep[bm] = 1;
            continue;
        }
        a = b;
    }
    /* the breakpoints, in order */
    uint32_t m = 0;
    for (uint32_t i = 0; i < n; i++)
        if (f->keep[i]) f->bp[m++] = i;
    for (uint32_t i = 0; i < n; i++) f->keep[i] = 0;
    f->keep[0] = f->keep[n - 1] = 1;
    u128 sq = {0, 0};
    uint32_t i = 0; /* bp[i] <= a < bp[i + 1] */
    a = 0;
    while (a < n - 1) {
        int64_t p1 = 0, p2 = 0, q1, q2;
        uint32_t b;
        while (i + 1 < m && f->bp[i + 1] <= a) i++;
        if (i + 1 < m && span_fits(f, a, f->bp[i + 1], &p1, &p2)) {
            uint32_t ok = i + 1, step = 1, bad;
            while (ok + step < m && f->bp[ok + step] - a <= MAX_SPAN &&
                   span_fits(f, a, f->bp[ok + step], &q1, &q2)) {
                ok += step;
                p1 = q1, p2 = q2;
                step *= 2;
            }
            bad = ok + step < m ? ok + step : m;
            while (bad - ok > 1) {
                uint32_t mid = ok + (bad - ok) / 2;
                if (f->bp[mid] - a <= MAX_SPAN && span_fits(f, a, f->bp[mid], &q1, &q2)) {
                    ok = mid;
                    p1 = q1, p2 = q2;
                } else {
                    bad = mid;
                }
            }
            b = f->bp[ok];
        } else {
            /* not even the next breakpoint: the farthest sample before it */
            uint32_t lim = i + 1 < m ? f->bp[i + 1] : n - 1, ok = a + 1, bad = lim;
            span_fits(f, a, ok, &p1, &p2); /* a span of 1 is exact */
            while (bad - ok > 1) {
                uint32_t mid = ok + (bad - ok) / 2;
                if (span_fits(f, a, mid, &q1, &q2)) {
                    ok = mid;
                    p1 = q1, p2 = q2;
                } else {
                    bad = mid;
                }
            }
            b = ok;
        }
        uint32_t dummy;
        uint32_t e = seg_err(y, a, b, p1, p2, &dummy, &sq);
        if (e > f->maxerr) f->maxerr = e;
        f->c1[a] = (int32_t) (p1 - y[a]);
        f->c2[a] = (int32_t) (p2 - y[a]);
        f->keep[b] = 1;
        f->nseg++;
        a = b;
    }
    uint64_t ms = div128(sq, n);
    f->rmserr = (uint32_t) isqrt64(ms);
    if (f->rmserr > f->maxerr) f->rmserr = f->maxerr;
}

/* write a curvebody of normalised y[0..n) */
static void write_curvebody(wr_t *w, fit_t *f, const char *name, uint32_t nname, uint32_t dec,
                            int64_t vmin, uint64_t vrange)
{
    if (nname > NAME_MAX_) nname = NAME_MAX_;
    w8(w, nname);
    for (uint32_t i = 0; i < nname; i++) {
        uint8_t c = (uint8_t) name[i];
        w8(w, c >= 0x20 && c < 0x7F ? c : '?');
    }
    w8(w, dec);
    wzz(w, vmin);
    wuv(w, vrange);
    wuv(w, f->n);
    wuv(w, f->tol);
    wuv(w, f->maxerr);
    wuv(w, f->rmserr);
    wuv(w, f->nseg);
    wzz(w, f->n ? f->y[0] : 0);
    if (f->n < 2) return;
    uint32_t a = 0;
    while (a < f->n - 1) {
        uint32_t b = next_keep(f->keep, a, f->n);
        wuv(w, b - a);
        wzz(w, f->c1[a]);
        wzz(w, f->c2[a]);
        wzz(w, (int64_t) f->y[b] - f->y[a]);
        a = b;
    }
}

/* ---- format detection --------------------------------------------------- */
static bool is_ws(uint8_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}
static bool is_digit(uint8_t c)
{
    return c >= '0' && c <= '9';
}
static bool is_alpha(uint8_t c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
static uint8_t lower(uint8_t c)
{
    return c >= 'A' && c <= 'Z' ? (uint8_t) (c + 32) : c;
}
static bool starts_ci(const uint8_t *p, const uint8_t *e, const char *s)
{
    for (; *s; s++, p++)
        if (p >= e || lower(*p) != (uint8_t) *s) return false;
    return true;
}
static bool is_text(const uint8_t *d, uint64_t n)
{
    uint64_t bad = 0;
    for (uint64_t i = 0; i < n;) {
        uint8_t c = d[i];
        if (c >= 0x20 && c < 0x7F) {
            i++;
            continue;
        }
        if (c == '\t' || c == '\n' || c == '\r' || c == '\f') {
            i++;
            continue;
        }
        if (c >= 0xC2 && c <= 0xF4) { /* a UTF-8 sequence */
            uint32_t k = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1, j = 1;
            for (; j <= k && i + j < n; j++)
                if ((d[i + j] & 0xC0) != 0x80) break;
            if (j > k) {
                i += k + 1;
                continue;
            }
        }
        bad++;
        i++;
    }
    return n > 0 && bad * 100 <= n; /* at most 1 % */
}

static uint32_t media_fmt(const uint8_t *d, uint64_t n)
{
    if (n >= 8 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') return ZXVS_FMT_PNG;
    if (n >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF) return ZXVS_FMT_JPEG;
    if (n >= 6 && d[0] == 'G' && d[1] == 'I' && d[2] == 'F' && d[3] == '8') return ZXVS_FMT_GIF;
    if (n >= 26 && d[0] == 'B' && d[1] == 'M') return ZXVS_FMT_BMP;
    if (n >= 4 && d[0] == 'Z' && d[1] == 'X' && d[2] == 'V' && d[3] == 'G') return ZXVS_FMT_ZXVG;
    if (n >= 4 && d[0] == 'Z' && d[1] == 'X' && d[2] == 'V' && d[3] == 'A') return ZXVS_FMT_ZXVA;
    if (n >= 12 && d[0] == 'R' && d[1] == 'I' && d[2] == 'F' && d[3] == 'F' && d[8] == 'W' &&
        d[9] == 'A' && d[10] == 'V' && d[11] == 'E')
        return ZXVS_FMT_WAV;
    if (n >= 12 && d[0] == 'F' && d[1] == 'O' && d[2] == 'R' && d[3] == 'M' && d[8] == 'A' &&
        d[9] == 'I' && d[10] == 'F')
        return ZXVS_FMT_AIFF;
    if (n >= 4 && d[0] == 'f' && d[1] == 'L' && d[2] == 'a' && d[3] == 'C') return ZXVS_FMT_FLAC;
    if (n >= 4 && d[0] == 'O' && d[1] == 'g' && d[2] == 'g' && d[3] == 'S') return ZXVS_FMT_OGG;
    if (n >= 12 && d[4] == 'f' && d[5] == 't' && d[6] == 'y' && d[7] == 'p') return ZXVS_FMT_MP4;
    if (n >= 3 && d[0] == 'I' && d[1] == 'D' && d[2] == '3') return ZXVS_FMT_MP3;
    if (n >= 2 && d[0] == 0xFF && (d[1] & 0xE0) == 0xE0 && (d[1] & 0x06) != 0) return ZXVS_FMT_MP3;
    return 0;
}

/* ---- numbers ------------------------------------------------------------- */
static const uint64_t P10[19] = {1ull,
                                 10ull,
                                 100ull,
                                 1000ull,
                                 10000ull,
                                 100000ull,
                                 1000000ull,
                                 10000000ull,
                                 100000000ull,
                                 1000000000ull,
                                 10000000000ull,
                                 100000000000ull,
                                 1000000000000ull,
                                 10000000000000ull,
                                 100000000000000ull,
                                 1000000000000000ull,
                                 10000000000000000ull,
                                 100000000000000000ull,
                                 1000000000000000000ull};
#define DEC_MAX 12

/* parse [ws] [+-] digits [. digits] [e [+-] digits] [ws]: value = m / 10^s
 * (s may be negative). false when not exactly a number. */
static bool parse_num(const uint8_t *p, const uint8_t *e, int64_t *m, int32_t *s)
{
    while (p < e && (*p == ' ' || *p == '\t' || *p == '\r')) p++;
    while (e > p && (e[-1] == ' ' || e[-1] == '\t' || e[-1] == '\r')) e--;
    if (p >= e) return false;
    bool neg = false;
    if (*p == '+' || *p == '-') neg = *p++ == '-';
    uint64_t v = 0;
    uint32_t sig = 0, nd = 0;
    int32_t frac = 0;
    while (p < e && is_digit(*p)) {
        if (v || *p != '0') {
            if (++sig > 18) return false;
        }
        v = v * 10 + (uint64_t) (*p - '0');
        p++;
        nd++;
    }
    if (p < e && *p == '.') {
        p++;
        while (p < e && is_digit(*p)) {
            if (v || *p != '0') {
                if (++sig > 18) return false;
            }
            v = v * 10 + (uint64_t) (*p - '0');
            frac++;
            p++;
            nd++;
        }
    }
    if (!nd) return false;
    int32_t ex = 0;
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        bool en = false;
        if (p < e && (*p == '+' || *p == '-')) en = *p++ == '-';
        uint32_t k = 0;
        while (p < e && is_digit(*p)) {
            if (ex < 100000) ex = ex * 10 + (*p - '0');
            p++;
            k++;
        }
        if (!k) return false;
        if (en) ex = -ex;
    }
    if (p != e) return false;
    *m = neg ? -(int64_t) v : (int64_t) v;
    *s = frac - ex;
    return true;
}
/* m / 10^s as an integer at dec decimals; false on overflow (>= 2^62) */
static bool to_fixed(int64_t m, int32_t s, uint32_t dec, int64_t *out)
{
    int32_t k = (int32_t) dec - s; /* multiply by 10^k */
    uint64_t a = (uint64_t) (m < 0 ? -m : m);
    if (k >= 0) {
        if (k > 18) {
            if (a) return false;
        } else {
            u128 p = mul64(a, P10[k]);
            if (p.hi || p.lo >= ((uint64_t) 1 << 62)) return false;
            a = p.lo;
        }
    } else {
        int32_t j = -k;
        if (j > 18) {
            a = 0;
        } else {
            uint64_t q = a / P10[j], r = a % P10[j];
            if (r * 2 >= P10[j]) q++;
            a = q;
        }
    }
    *out = m < 0 ? -(int64_t) a : (int64_t) a;
    return true;
}

/* ---- tables (CSV / TSV) -------------------------------------------------- */
typedef struct {
    const uint8_t *d, *e, *p;
    uint8_t sep;
    bool csv;
    bool eol, eof;
} cells_t;
static void cells_init(cells_t *c, const uint8_t *d, uint64_t n, uint8_t sep)
{
    c->d = c->p = d;
    c->e = d + n;
    c->sep = sep;
    c->csv = sep == ',';
    c->eol = true;
    c->eof = n == 0;
}
/* at a row start: skip comment ('#') and blank lines; false at the end */
static bool row_start(cells_t *c)
{
    for (;;) {
        if (c->p >= c->e) {
            c->eof = true;
            return false;
        }
        const uint8_t *q = c->p;
        if (*q == '#' || *q == '\n' || (*q == '\r' && q + 1 < c->e && q[1] == '\n')) {
            while (c->p < c->e && *c->p != '\n') c->p++;
            if (c->p < c->e) c->p++;
            continue;
        }
        c->eol = false;
        return true;
    }
}
/* the next cell of the row: [*a, *b) (quotes kept for CSV) */
static bool next_cell(cells_t *c, const uint8_t **a, const uint8_t **b)
{
    if (c->eol) return false;
    const uint8_t *p = c->p, *e = c->e;
    *a = p;
    bool q = false;
    while (p < e) {
        uint8_t ch = *p;
        if (c->csv && ch == '"') q = !q;
        if (!q && (ch == c->sep || ch == '\n')) break;
        p++;
    }
    *b = p;
    if (*b > *a && (*b)[-1] == '\r' && (p >= e || *p == '\n')) (*b)--;
    if (p < e && *p == c->sep) {
        c->p = p + 1;
    } else {
        c->p = p < e ? p + 1 : p;
        c->eol = true;
    }
    return true;
}
static void unquote(const uint8_t **a, const uint8_t **b)
{
    while (*a < *b && (**a == ' ')) (*a)++;
    while (*b > *a && ((*b)[-1] == ' ')) (*b)--;
    if (*b - *a >= 2 && **a == '"' && (*b)[-1] == '"') {
        (*a)++;
        (*b)--;
    }
}

typedef struct {
    uint32_t num, other, empty;
    int32_t dec;
    const uint8_t *name;
    uint32_t nname;
} col_t;

/* is it a table with separator sep? fills rows/cols */
static bool table_shape(const uint8_t *d, uint64_t n, uint8_t sep, uint32_t *rows, uint32_t *cols)
{
    cells_t c;
    cells_init(&c, d, n, sep);
    uint32_t r = 0, k0 = 0, odd = 0;
    while (row_start(&c)) {
        uint32_t k = 0;
        const uint8_t *a, *b;
        while (next_cell(&c, &a, &b)) k++;
        if (r == 0)
            k0 = k;
        else if (k != k0)
            odd++;
        r++;
    }
    *rows = r;
    *cols = k0;
    return r >= 2 && k0 >= 2 && odd * 20 <= r;
}

/* ---- the structure tree -------------------------------------------------- */
typedef struct {
    int32_t parent, first, last, next;
    uint32_t nchild, nkept, x, y;
    uint8_t type, nlabel;
    char label[15];
} node_t;

typedef struct {
    node_t *nd;
    uint32_t n, cap;
    uint32_t total;
    uint32_t *order; /* serialisation order (pre-order of the tree)   */
    int32_t *newid;
} tree_t;

/* add a node under parent (-1: none, i.e. the root); returns its index or
 * -1 when it is not kept */
static int32_t tree_add(tree_t *t, int32_t parent, uint32_t type, const uint8_t *lab, uint32_t nl)
{
    t->total++;
    if (parent >= 0) t->nd[parent].nchild++;
    if (t->n >= t->cap || (parent < 0 && t->n > 0)) return -1;
    int32_t i = (int32_t) t->n++;
    node_t *x = &t->nd[i];
    x->parent = parent;
    x->first = x->last = x->next = -1;
    x->nchild = x->nkept = x->x = x->y = 0;
    x->type = (uint8_t) type;
    uint32_t k = 0;
    for (uint32_t j = 0; j < nl && k < 15; j++) {
        uint8_t ch = lab[j];
        if (ch >= 0x21 && ch < 0x7F)
            x->label[k++] = (char) ch;
        else if (ch == ' ' && k)
            x->label[k++] = '_';
    }
    x->nlabel = (uint8_t) k;
    if (parent >= 0) {
        node_t *p = &t->nd[parent];
        if (p->last >= 0)
            t->nd[p->last].next = i;
        else
            p->first = i;
        p->last = i;
        p->nkept++;
        x->y = p->y + 1;
    }
    return i;
}
/* a kept child of parent with this label, else add one */
static int32_t tree_child(tree_t *t, int32_t parent, uint32_t type, const uint8_t *lab, uint32_t nl)
{
    if (parent >= 0) {
        for (int32_t c = t->nd[parent].first; c >= 0; c = t->nd[c].next) {
            node_t *x = &t->nd[c];
            uint32_t k = 0, j = 0;
            bool same = true;
            for (; j < nl && k < 15; j++) {
                uint8_t ch = lab[j];
                char want;
                if (ch >= 0x21 && ch < 0x7F)
                    want = (char) ch;
                else if (ch == ' ' && k)
                    want = '_';
                else
                    continue;
                if (k >= x->nlabel || x->label[k] != want) {
                    same = false;
                    break;
                }
                k++;
            }
            if (same && k == x->nlabel && x->type == type) return c;
        }
    }
    return tree_add(t, parent, type, lab, nl);
}

static void tree_layout(tree_t *t)
{
    if (!t->n) return;
    /* pre-order */
    uint32_t k = 0;
    int32_t c = 0;
    while (c >= 0 && k < t->n) {
        t->order[k++] = (uint32_t) c;
        if (t->nd[c].first >= 0) {
            c = t->nd[c].first;
            continue;
        }
        while (c >= 0 && t->nd[c].next < 0) c = t->nd[c].parent;
        if (c >= 0) c = t->nd[c].next;
    }
    t->n = k;
    for (uint32_t i = 0; i < k; i++) t->newid[t->order[i]] = (int32_t) i;
    uint32_t leaf = 0;
    for (uint32_t i = 0; i < k; i++) {
        node_t *x = &t->nd[t->order[i]];
        if (x->first < 0) x->x = 2 * leaf++;
    }
    for (uint32_t i = k; i-- > 0;) {
        node_t *x = &t->nd[t->order[i]];
        if (x->first >= 0) x->x = (t->nd[x->first].x + t->nd[x->last].x) / 2;
    }
}

/* JSON ---------------------------------------------------------------------- */
static const uint8_t *json_str(const uint8_t *p,
                               const uint8_t *e) /* p after '"'; returns after closing '"' */
{
    while (p < e) {
        uint8_t c = *p++;
        if (c == '"') return p;
        if (c == '\\') {
            if (p >= e) return 0;
            uint8_t x = *p++;
            if (x == 'u') {
                for (int i = 0; i < 4; i++, p++)
                    if (p >= e ||
                        !((*p >= '0' && *p <= '9') || (lower(*p) >= 'a' && lower(*p) <= 'f')))
                        return 0;
            } else if (!(x == '"' || x == '\\' || x == '/' || x == 'b' || x == 'f' || x == 'n' ||
                         x == 'r' || x == 't'))
                return 0;
        } else if (c < 0x20) {
            return 0;
        }
    }
    return 0;
}
/* a JSON number: -?(0|[1-9][0-9]*)(.[0-9]+)?([eE][+-]?[0-9]+)?; returns its end */
static const uint8_t *json_num(const uint8_t *p, const uint8_t *e)
{
    if (p < e && *p == '-') p++;
    if (p >= e || !is_digit(*p)) return 0;
    if (*p == '0')
        p++;
    else
        while (p < e && is_digit(*p)) p++;
    if (p < e && *p == '.') {
        p++;
        if (p >= e || !is_digit(*p)) return 0;
        while (p < e && is_digit(*p)) p++;
    }
    if (p < e && (*p == 'e' || *p == 'E')) {
        p++;
        if (p < e && (*p == '+' || *p == '-')) p++;
        if (p >= e || !is_digit(*p)) return 0;
        while (p < e && is_digit(*p)) p++;
    }
    return p;
}
static bool parse_json(const uint8_t *d, uint64_t n, tree_t *t)
{
    const uint8_t *p = d, *e = d + n;
    /* stack: bit 1 = object; node per level (folded past ZXVS_MAX_DEPTH) */
    uint8_t kind[FOLD_STK];
    int32_t nodeat[ZXVS_MAX_DEPTH + 1];
    uint32_t depth = 0;
    int32_t root = tree_add(t, -1, ZXVS_N_ROOT, (const uint8_t *) "json", 4);
    nodeat[0] = root;
    const uint8_t *key = 0;
    uint32_t nkey = 0;
    enum { VALUE, AFTER, KEY, COLON } st = VALUE;
    for (;;) {
        while (p < e && is_ws(*p)) p++;
        if (p >= e) break;
        uint8_t c = *p;
        int32_t par = nodeat[depth > ZXVS_MAX_DEPTH ? ZXVS_MAX_DEPTH : depth];
        if (st == KEY) {
            if (c == '}' && depth && kind[depth - 1] == 1) {
                /* empty object or trailing: only valid right after '{' (checked by AFTER) */
                return false;
            }
            if (c != '"') return false;
            const uint8_t *q = json_str(p + 1, e);
            if (!q) return false;
            key = p + 1;
            nkey = (uint32_t) (q - p - 2);
            p = q;
            st = COLON;
            continue;
        }
        if (st == COLON) {
            if (c != ':') return false;
            p++;
            st = VALUE;
            continue;
        }
        if (st == AFTER) {
            if (!depth) return false; /* trailing data */
            if (c == ',') {
                p++;
                st = kind[depth - 1] == 1 ? KEY : VALUE;
                continue;
            }
            if ((c == '}' && kind[depth - 1] == 1) || (c == ']' && kind[depth - 1] == 2)) {
                p++;
                depth--;
                st = AFTER;
                if (!depth) {
                    while (p < e && is_ws(*p)) p++;
                    return p == e;
                }
                continue;
            }
            return false;
        }
        /* VALUE */
        bool inobj = depth && kind[depth - 1] == 1;
        const uint8_t *lab = inobj ? key : 0;
        uint32_t nl = inobj ? nkey : 0;
        if (c == '{' || c == '[') {
            if (depth >= FOLD_STK) return false;
            int32_t me = tree_add(t, par, c == '{' ? ZXVS_N_OBJECT : ZXVS_N_ARRAY, lab, nl);
            kind[depth++] = c == '{' ? 1 : 2;
            if (depth <= ZXVS_MAX_DEPTH) nodeat[depth] = me >= 0 ? me : par;
            p++;
            while (p < e && is_ws(*p)) p++;
            if (p < e && ((c == '{' && *p == '}') || (c == '[' && *p == ']'))) {
                p++;
                depth--;
                st = AFTER;
                if (!depth) {
                    while (p < e && is_ws(*p)) p++;
                    return p == e;
                }
                continue;
            }
            st = c == '{' ? KEY : VALUE;
            continue;
        }
        uint32_t type;
        if (c == '"') {
            const uint8_t *q = json_str(p + 1, e);
            if (!q) return false;
            p = q;
            type = ZXVS_N_STRING;
        } else if (c == 't' && e - p >= 4 && p[1] == 'r' && p[2] == 'u' && p[3] == 'e') {
            p += 4;
            type = ZXVS_N_BOOL;
        } else if (c == 'f' && e - p >= 5 && p[1] == 'a' && p[2] == 'l' && p[3] == 's' &&
                   p[4] == 'e') {
            p += 5;
            type = ZXVS_N_BOOL;
        } else if (c == 'n' && e - p >= 4 && p[1] == 'u' && p[2] == 'l' && p[3] == 'l') {
            p += 4;
            type = ZXVS_N_NULL;
        } else if (c == '-' || is_digit(c)) {
            const uint8_t *q = json_num(p, e);
            if (!q) return false;
            p = q;
            type = ZXVS_N_NUMBER;
        } else {
            return false;
        }
        tree_add(t, par, type, lab, nl);
        st = AFTER;
        if (!depth) {
            while (p < e && is_ws(*p)) p++;
            return p == e;
        }
    }
    return false;
}

/* XML / HTML -------------------------------------------------------------------- */
static bool html_void(const uint8_t *nm, uint32_t n)
{
    static const char *const v[] = {"area",  "base",   "br",    "col",  "embed",
                                    "hr",    "img",    "input", "link", "meta",
                                    "param", "source", "track", "wbr",  0};
    for (int i = 0; v[i]; i++) {
        uint32_t k = 0;
        while (v[i][k] && k < n && lower(nm[k]) == (uint8_t) v[i][k]) k++;
        if (!v[i][k] && k == n) return true;
    }
    return false;
}
static bool name_ch(uint8_t c)
{
    return is_alpha(c) || is_digit(c) || c == '-' || c == '.' || c == ':' || c >= 0x80;
}
static bool same_name(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb, bool ci)
{
    if (na != nb) return false;
    for (uint32_t i = 0; i < na; i++)
        if (ci ? lower(a[i]) != lower(b[i]) : a[i] != b[i]) return false;
    return true;
}
static const uint8_t *find_seq(const uint8_t *p, const uint8_t *e, const char *s, bool ci)
{
    uint32_t k = 0;
    while (s[k]) k++;
    for (; p + k <= e; p++) {
        uint32_t j = 0;
        while (j < k && (ci ? lower(p[j]) : p[j]) == (uint8_t) s[j]) j++;
        if (j == k) return p;
    }
    return 0;
}
#define XSTK 256
static bool parse_xml(const uint8_t *d, uint64_t n, tree_t *t, bool html)
{
    const uint8_t *p = d, *e = d + n;
    const uint8_t *snm[XSTK];
    uint32_t snl[XSTK];
    int32_t snode[XSTK];
    uint32_t depth = 0, deep = 0, elems = 0;
    int32_t root =
        tree_add(t, -1, ZXVS_N_ROOT, (const uint8_t *) (html ? "html" : "xml"), html ? 4 : 3);
    while (p < e) {
        int32_t par = depth ? snode[depth - 1] : root;
        if (*p != '<') {
            const uint8_t *q = p;
            bool txt = false;
            while (q < e && *q != '<') {
                if (!is_ws(*q)) txt = true;
                q++;
            }
            if (txt) tree_add(t, par, ZXVS_N_TEXT, 0, 0);
            p = q;
            continue;
        }
        if (e - p >= 4 && p[1] == '!' && p[2] == '-' && p[3] == '-') {
            const uint8_t *q = find_seq(p + 4, e, "-->", false);
            p = q ? q + 3 : e;
            continue;
        }
        if (e - p >= 9 && starts_ci(p, e, "<![cdata[")) {
            const uint8_t *q = find_seq(p + 9, e, "]]>", false);
            tree_add(t, par, ZXVS_N_TEXT, 0, 0);
            p = q ? q + 3 : e;
            continue;
        }
        if (e - p >= 2 && (p[1] == '?' || p[1] == '!')) {
            while (p < e && *p != '>') p++;
            if (p < e) p++;
            continue;
        }
        if (e - p >= 2 && p[1] == '/') {
            const uint8_t *q = p + 2, *nm = q;
            while (q < e && name_ch(*q)) q++;
            uint32_t nl = (uint32_t) (q - nm);
            while (q < e && *q != '>') q++;
            if (q < e) q++;
            p = q;
            if (deep) {
                deep--;
                continue;
            }
            for (uint32_t k = depth; k-- > 0;)
                if (same_name(snm[k], snl[k], nm, nl, html)) {
                    depth = k;
                    break;
                }
            continue;
        }
        const uint8_t *q = p + 1, *nm = q;
        while (q < e && name_ch(*q)) q++;
        uint32_t nl = (uint32_t) (q - nm);
        if (!nl) { /* a lone '<' in text */
            p++;
            continue;
        }
        int32_t me = tree_add(t, par, ZXVS_N_ELEMENT, nm, nl);
        elems++;
        /* attributes */
        bool selfc = false;
        while (q < e && *q != '>') {
            if (*q == '/' && q + 1 < e && q[1] == '>') {
                selfc = true;
                q++;
                break;
            }
            if (name_ch(*q)) {
                const uint8_t *an = q;
                while (q < e && name_ch(*q)) q++;
                tree_add(t, me >= 0 ? me : par, ZXVS_N_ATTR, an, (uint32_t) (q - an));
                while (q < e && is_ws(*q)) q++;
                if (q < e && *q == '=') {
                    q++;
                    while (q < e && is_ws(*q)) q++;
                    if (q < e && (*q == '"' || *q == '\'')) {
                        uint8_t qc = *q++;
                        while (q < e && *q != qc) q++;
                        if (q < e) q++;
                    } else {
                        while (q < e && !is_ws(*q) && *q != '>') q++;
                    }
                }
                continue;
            }
            q++;
        }
        if (q < e) q++;
        p = q;
        if (selfc || (html && html_void(nm, nl))) continue;
        if (html && (same_name(nm, nl, (const uint8_t *) "script", 6, true) ||
                     same_name(nm, nl, (const uint8_t *) "style", 5, true))) {
            const uint8_t *z = find_seq(p, e, nl == 6 ? "</script" : "</style", true);
            if (z && z > p) tree_add(t, me >= 0 ? me : par, ZXVS_N_TEXT, 0, 0);
            p = z ? z : e;
            continue;
        }
        if (depth < XSTK && depth < ZXVS_MAX_DEPTH) {
            snm[depth] = nm;
            snl[depth] = nl;
            snode[depth] = me >= 0 ? me : par;
            depth++;
        } else {
            deep++;
        }
    }
    return elems > 0;
}

/* zxcfg ---------------------------------------------------------------------------- */
static bool parse_zxcfg(const uint8_t *d, uint64_t n, tree_t *t)
{
    const uint8_t *p = d, *e = d + n;
    if (!(e - p >= 7 && starts_ci(p, e, "zxcfg "))) return false;
    int32_t root = tree_add(t, -1, ZXVS_N_ROOT, (const uint8_t *) "zxcfg", 5);
    while (p < e && *p != '\n') p++;
    while (p < e) {
        p++;
        const uint8_t *l = p;
        while (p < e && *p != '\n') p++;
        const uint8_t *le = p;
        while (l < le && (*l == ' ' || *l == '\t')) l++;
        if (l >= le || *l == '#' || *l == '\r') continue;
        if (le - l > 5 && starts_ci(l, le, "lock ")) l += 5;
        const uint8_t *k = l;
        while (l < le && *l != '=' && *l != ' ' && *l != '\t') l++;
        const uint8_t *ke = l;
        while (l < le && (*l == ' ' || *l == '\t')) l++;
        if (l >= le || *l != '=' || ke == k) return false;
        int32_t at = root;
        const uint8_t *s = k;
        for (const uint8_t *q = k; q <= ke; q++) {
            if (q == ke || *q == '.') {
                if (q == s) return false;
                at =
                    tree_child(t, at, q == ke ? ZXVS_N_KEY : ZXVS_N_SECTION, s, (uint32_t) (q - s));
                if (at < 0) break;
                s = q + 1;
            }
        }
    }
    return true;
}

/* C source --------------------------------------------------------------------------- */
static bool parse_csrc(const uint8_t *d, uint64_t n, tree_t *t)
{
    const uint8_t *p = d, *e = d + n;
    int32_t root = tree_add(t, -1, ZXVS_N_ROOT, (const uint8_t *) "c", 1);
    int32_t stk[ZXVS_MAX_DEPTH];
    uint32_t depth = 0, deep = 0, blocks = 0;
    const uint8_t *lastid = 0, *callid = 0;
    uint32_t nlast = 0, ncall = 0;
    uint8_t prev = 0;
    bool bol = true;
    while (p < e) {
        uint8_t c = *p;
        if (c == '\n') {
            bol = true;
            p++;
            continue;
        }
        if (is_ws(c)) {
            p++;
            continue;
        }
        if (bol && c == '#') { /* preprocessor line, with continuations */
            while (p < e && *p != '\n') {
                if (*p == '\\' && p + 1 < e && p[1] == '\n') p++;
                p++;
            }
            continue;
        }
        bol = false;
        if (c == '/' && p + 1 < e && p[1] == '/') {
            while (p < e && *p != '\n') p++;
            continue;
        }
        if (c == '/' && p + 1 < e && p[1] == '*') {
            const uint8_t *q = find_seq(p + 2, e, "*/", false);
            p = q ? q + 2 : e;
            continue;
        }
        if (c == '"' || c == '\'') {
            p++;
            while (p < e && *p != c && *p != '\n') {
                if (*p == '\\') p++;
                p++;
            }
            if (p < e) p++;
            prev = 'v';
            continue;
        }
        if (is_alpha(c)) {
            const uint8_t *q = p;
            while (q < e && (is_alpha(*q) || is_digit(*q))) q++;
            lastid = p;
            nlast = (uint32_t) (q - p);
            p = q;
            prev = 'i';
            continue;
        }
        if (c == '(') {
            if (prev == 'i') {
                callid = lastid;
                ncall = nlast;
            }
        } else if (c == '{') {
            int32_t par = depth ? stk[depth - 1] : root;
            const uint8_t *lab = prev == ')' ? callid : lastid;
            uint32_t nl = prev == ')' ? ncall : nlast;
            int32_t me = tree_add(t, par, ZXVS_N_BLOCK, lab, lab ? nl : 0);
            blocks++;
            if (depth < ZXVS_MAX_DEPTH)
                stk[depth++] = me >= 0 ? me : par;
            else
                deep++;
        } else if (c == '}') {
            if (deep)
                deep--;
            else if (depth)
                depth--;
        }
        prev = c;
        p++;
    }
    return blocks > 0;
}

/* ---- the descriptor ------------------------------------------------------------- */
typedef struct {
    const uint8_t *d;
    uint64_t n;
    const zxvs_cfg_t *cfg;
    uint32_t tol;
    uint32_t fmt, kind;
    uint64_t hist[256];
    uint32_t h32[256];
    /* entropy */
    uint32_t win, nwin;
    int32_t *ent;
    uint8_t *ekeep;
    int32_t *ec1, *ec2;
    uint32_t *ebp;
    /* hilbert */
    uint32_t hk, bpc, ncell;
    uint8_t *cls;
    /* table */
    uint32_t rows, cols;
    uint8_t sep;
    col_t *col;
    uint32_t curve_col[ZXVS_MAX_CURVES], ncurve;
    /* series scratch */
    int64_t *v;
    int32_t *y, *c1, *c2;
    uint32_t *bp;
    uint8_t *keep;
    uint64_t vcap;
    /* tree */
    tree_t tr;
    zxvs_report_t rep;
    uint32_t shape[ZXVS_DIMS];
    /* curve summary for the shape vector */
    uint32_t first_curve[16];
    bool have_curve;
    uint32_t depth_hist[16], maxdepth;
} ctx_t;

static void layout(arena_t *a, ctx_t *c, uint64_t n, uint32_t maxnodes)
{
    uint64_t vmax = n / 2 + 4;
    c->vcap = vmax;
    c->ent = take(a, ENT_MAXW * 4);
    c->ekeep = take(a, ENT_MAXW);
    c->ec1 = take(a, ENT_MAXW * 4);
    c->ec2 = take(a, ENT_MAXW * 4);
    c->ebp = take(a, ENT_MAXW * 4);
    c->cls = take(a, 4096);
    c->col = take(a, MAX_COLS * sizeof(col_t));
    c->v = take(a, vmax * 8);
    c->y = take(a, vmax * 4);
    c->c1 = take(a, vmax * 4);
    c->c2 = take(a, vmax * 4);
    c->bp = take(a, vmax * 4);
    c->keep = take(a, vmax);
    c->tr.nd = take(a, (uint64_t) maxnodes * sizeof(node_t));
    c->tr.order = take(a, (uint64_t) maxnodes * 4);
    c->tr.newid = take(a, (uint64_t) maxnodes * 4);
    c->tr.cap = maxnodes;
}

uint64_t zxvs_work_size(uint64_t len, const zxvs_cfg_t *cfg)
{
    uint32_t mn = cfg && cfg->max_nodes ? cfg->max_nodes : ZXVS_MAX_NODES;
    if (mn > ZXVS_MAX_NODES) mn = ZXVS_MAX_NODES;
    arena_t a = {0, 0, 0, true, false};
    ctx_t c;
    layout(&a, &c, len, mn);
    return a.used + 16;
}

uint64_t zxvs_out_bound(uint64_t len, const zxvs_cfg_t *cfg)
{
    uint32_t mn = cfg && cfg->max_nodes ? cfg->max_nodes : ZXVS_MAX_NODES;
    if (mn > ZXVS_MAX_NODES) mn = ZXVS_MAX_NODES;
    uint64_t b = 64 + ZXVS_CID_LEN + 10 + ZXVS_DIMS * 3;
    b += 5 + 256 * 10;                                                        /* HIST    */
    b += 5 + 10 + 32 + 80 + (uint64_t) ENT_MAXW * 17;                         /* ENTROPY */
    b += 5 + 20 + 4096 * 4 + 10 + (2 * 64 * 65) * 11;                         /* HILBERT */
    b += (uint64_t) ZXVS_MAX_CURVES * (5 + 1 + 32 + 80) + (len / 2 + 8) * 17; /* CURVE */
    b += 5 + 20 + (uint64_t) mn * 36;                                         /* STRUCT  */
    b += 5 + 64;                                                              /* REF     */
    return b;
}

static void hilbert_d2xy(uint32_t k, uint32_t d, uint32_t *x, uint32_t *y)
{
    uint32_t rx, ry, t = d;
    *x = *y = 0;
    for (uint32_t s = 1; s < (1u << k); s <<= 1) {
        rx = 1u & (t / 2);
        ry = 1u & (t ^ rx);
        if (ry == 0) {
            if (rx == 1) {
                *x = s - 1 - *x;
                *y = s - 1 - *y;
            }
            uint32_t tmp = *x;
            *x = *y;
            *y = tmp;
        }
        *x += s * rx;
        *y += s * ry;
        t /= 4;
    }
}

static void bytes_stage(ctx_t *c, wr_t *w)
{
    const uint8_t *d = c->d;
    uint64_t n = c->n;
    /* HIST */
    for (uint64_t i = 0; i < n; i++) c->hist[d[i]]++;
    uint64_t at = sec_begin(w, ZXVS_HIST);
    for (int i = 0; i < 256; i++) wuv(w, c->hist[i]);
    sec_end(w, at);
    /* ENTROPY */
    c->win = n <= 64 ? (uint32_t) (n ? n : 1) : (uint32_t) ((n + ENT_MAXW - 1) / ENT_MAXW);
    if (c->win < 64 && n > 64) c->win = 64;
    c->nwin = n ? (uint32_t) ((n + c->win - 1) / c->win) : 1;
    for (uint32_t k = 0; k < c->nwin; k++) {
        zset(c->h32, 0, sizeof c->h32);
        uint64_t a = (uint64_t) k * c->win, b = a + c->win > n ? n : a + c->win;
        for (uint64_t i = a; i < b; i++) c->h32[d[i]]++;
        c->ent[k] = (int32_t) entropy_q8(c->h32, b - a) * 8192; /* 2048 -> 2^24 */
    }
    fit_t f = {c->ent, c->nwin, c->tol, c->ekeep, c->ec1, c->ec2, c->ebp, 0, 0, 0};
    fit(&f);
    at = sec_begin(w, ZXVS_ENTROPY);
    wuv(w, c->win);
    write_curvebody(w, &f, "entropy", 7, 0, 0, 2048);
    sec_end(w, at);
    c->rep.ent_max_q24 = f.maxerr;
    c->rep.ent_rms_q24 = f.rmserr;
    /* HILBERT */
    uint32_t k = 0;
    while (k < HIL_MAXK && ((uint64_t) 1 << (2 * k)) * 16 < n) k++;
    c->hk = k;
    uint64_t cells = (uint64_t) 1 << (2 * k);
    c->bpc = (uint32_t) ((n + cells - 1) / cells);
    if (!c->bpc) c->bpc = 1;
    c->ncell = n ? (uint32_t) ((n + c->bpc - 1) / c->bpc) : 0;
    for (uint32_t i = 0; i < c->ncell; i++) {
        zset(c->h32, 0, sizeof c->h32);
        uint64_t a = (uint64_t) i * c->bpc, b = a + c->bpc > n ? n : a + c->bpc;
        uint64_t z = 0, txt = 0;
        for (uint64_t j = a; j < b; j++) {
            uint8_t ch = d[j];
            c->h32[ch]++;
            z += ch == 0;
            txt += (ch >= 0x20 && ch < 0x7F) || ch == '\n' || ch == '\r' || ch == '\t';
        }
        uint64_t m = b - a;
        uint32_t cl;
        if (z * 10 >= m * 9)
            cl = 0;
        else if (txt * 10 >= m * 9)
            cl = 1;
        else if (entropy_q8(c->h32, m) >= 7 * 256)
            cl = 2;
        else
            cl = 3;
        c->cls[i] = (uint8_t) cl;
    }
    at = sec_begin(w, ZXVS_HILBERT);
    w8(w, k);
    wuv(w, c->bpc);
    uint32_t nrun = 0;
    for (uint32_t i = 0; i < c->ncell; i++)
        if (i == 0 || c->cls[i] != c->cls[i - 1]) nrun++;
    wuv(w, nrun);
    for (uint32_t i = 0; i < c->ncell;) {
        uint32_t j = i;
        while (j < c->ncell && c->cls[j] == c->cls[i]) j++;
        w8(w, c->cls[i]);
        wuv(w, j - i);
        i = j;
    }
    /* the class grid (4 = empty) and its outline */
    uint32_t S = 1u << k;
    static const uint8_t EMPTY = 4;
    uint8_t g[64 * 64];
    zset(g, EMPTY, sizeof g);
    for (uint32_t i = 0; i < c->ncell; i++) {
        uint32_t x, y;
        hilbert_d2xy(k, i, &x, &y);
        g[y * 64 + x] = c->cls[i];
    }
#define GAT(x, y) ((x) < S && (y) < S ? g[(y) * 64 + (x)] : EMPTY)
    uint32_t nedge = 0;
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 1) wuv(w, nedge);
        /* horizontal: lattice line y = j between rows j-1 and j */
        for (uint32_t j = 0; j <= S; j++) {
            for (uint32_t i = 0; i < S;) {
                uint8_t up = j ? GAT(i, j - 1) : EMPTY, dn = GAT(i, j);
                if (up == dn) {
                    i++;
                    continue;
                }
                uint32_t i0 = i;
                while (i < S) {
                    uint8_t u2 = j ? GAT(i, j - 1) : EMPTY, d2 = GAT(i, j);
                    if (u2 == d2) break;
                    i++;
                }
                if (pass == 0)
                    nedge++;
                else {
                    w8(w, 0);
                    wuv(w, i0);
                    wuv(w, j);
                    wuv(w, i - i0);
                }
            }
        }
        for (uint32_t i = 0; i <= S; i++) {
            for (uint32_t j = 0; j < S;) {
                uint8_t lf = i ? GAT(i - 1, j) : EMPTY, rt = GAT(i, j);
                if (lf == rt) {
                    j++;
                    continue;
                }
                uint32_t j0 = j;
                while (j < S) {
                    uint8_t l2 = i ? GAT(i - 1, j) : EMPTY, r2 = GAT(i, j);
                    if (l2 == r2) break;
                    j++;
                }
                if (pass == 0)
                    nedge++;
                else {
                    w8(w, 1);
                    wuv(w, i);
                    wuv(w, j0);
                    wuv(w, j - j0);
                }
            }
        }
    }
#undef GAT
    sec_end(w, at);
    c->rep.hilbert_order = k;
    c->rep.hilbert_edges = nedge;
}

/* pick the format */
static uint32_t detect(ctx_t *c)
{
    const uint8_t *d = c->d;
    uint64_t n = c->n;
    uint32_t m = media_fmt(d, n);
    if (m) return m;
    if (!is_text(d, n)) return ZXVS_FMT_BIN;
    const uint8_t *p = d, *e = d + n;
    if (n >= 3 && d[0] == 0xEF && d[1] == 0xBB && d[2] == 0xBF) p += 3;
    while (p < e && is_ws(*p)) p++;
    if (e - p >= 6 && starts_ci(p, e, "zxcfg ")) return ZXVS_FMT_ZXCFG;
    if (p < e && (*p == '{' || *p == '[')) return ZXVS_FMT_JSON; /* confirmed by the parse */
    if (p < e && *p == '<') {
        if (starts_ci(p, e, "<!doctype html") || starts_ci(p, e, "<html")) return ZXVS_FMT_HTML;
        return ZXVS_FMT_XML;
    }
    uint32_t r, k;
    if (table_shape(d, n, '\t', &r, &k)) {
        c->sep = '\t';
        c->rows = r;
        c->cols = k;
        return ZXVS_FMT_TSV;
    }
    if (table_shape(d, n, ',', &r, &k)) {
        c->sep = ',';
        c->rows = r;
        c->cols = k;
        return ZXVS_FMT_CSV;
    }
    /* one number per line */
    {
        uint32_t lines = 0, nums = 0;
        const uint8_t *q = d;
        while (q < e) {
            const uint8_t *l = q;
            while (q < e && *q != '\n') q++;
            int64_t mm;
            int32_t ss;
            const uint8_t *le = q;
            if (q < e) q++;
            const uint8_t *s = l;
            while (s < le && is_ws(*s)) s++;
            if (s >= le || *s == '#') continue;
            lines++;
            if (parse_num(l, le, &mm, &ss)) nums++;
        }
        if (lines >= 2 && nums * 100 >= lines * 99) {
            c->sep = '\n';
            c->rows = lines;
            c->cols = 1;
            return ZXVS_FMT_NUMTEXT;
        }
    }
    if (find_seq(d, e, "#include", false) ||
        (find_seq(d, e, "{", false) && find_seq(d, e, ";", false) &&
         (find_seq(d, e, "return", false) || find_seq(d, e, "void ", false) ||
          find_seq(d, e, "int ", false))))
        return ZXVS_FMT_CSRC;
    return ZXVS_FMT_TEXT;
}

/* table pass 1: column stats and the header */
static void table_scan(ctx_t *c, bool *header)
{
    uint32_t nc = c->cols < MAX_COLS ? c->cols : MAX_COLS;
    zset(c->col, 0, sizeof(col_t) * MAX_COLS);
    cells_t it;
    cells_init(&it, c->d, c->n, c->sep == '\n' ? 0 : c->sep);
    /* is row 0 a header? (no numeric cells, and another row exists) */
    *header = false;
    if (c->sep != '\n' && row_start(&it)) {
        const uint8_t *a, *b;
        uint32_t k = 0, nums = 0;
        while (next_cell(&it, &a, &b)) {
            unquote(&a, &b);
            int64_t m;
            int32_t s;
            if (parse_num(a, b, &m, &s)) nums++;
            if (k < nc) {
                c->col[k].name = a;
                c->col[k].nname = (uint32_t) (b - a);
            }
            k++;
        }
        *header = nums == 0 && c->rows >= 2;
    }
    cells_init(&it, c->d, c->n, c->sep == '\n' ? 0 : c->sep);
    bool first = true;
    while (row_start(&it)) {
        const uint8_t *a, *b;
        uint32_t k = 0;
        bool skip = first && *header;
        first = false;
        while (next_cell(&it, &a, &b)) {
            if (k < nc && !skip) {
                unquote(&a, &b);
                col_t *cl = &c->col[k];
                int64_t m;
                int32_t s;
                if (a == b)
                    cl->empty++;
                else if (parse_num(a, b, &m, &s)) {
                    int64_t tmp;
                    int32_t dd = s < 0 ? 0 : s > DEC_MAX ? DEC_MAX : s;
                    if (to_fixed(m, s, (uint32_t) dd, &tmp)) {
                        cl->num++;
                        if (dd > cl->dec) cl->dec = dd;
                    } else {
                        cl->other++;
                    }
                } else
                    cl->other++;
            }
            k++;
        }
    }
    if (!*header)
        for (uint32_t k = 0; k < nc; k++) c->col[k].nname = 0;
}

/* table pass 2: one column's values (as integers at its decimals) */
static uint32_t table_column(ctx_t *c, uint32_t col, bool header)
{
    cells_t it;
    cells_init(&it, c->d, c->n, c->sep == '\n' ? 0 : c->sep);
    uint32_t nv = 0;
    bool first = true;
    uint32_t dec = (uint32_t) c->col[col].dec;
    while (row_start(&it)) {
        const uint8_t *a, *b;
        uint32_t k = 0;
        bool skip = first && header;
        first = false;
        while (next_cell(&it, &a, &b)) {
            if (k == col && !skip) {
                unquote(&a, &b);
                int64_t m, v;
                int32_t s;
                if (a != b && parse_num(a, b, &m, &s) && to_fixed(m, s, dec, &v) && nv < c->vcap)
                    c->v[nv++] = v;
            }
            k++;
        }
    }
    return nv;
}

static void series_stage(ctx_t *c, wr_t *w, bool header)
{
    uint32_t nc = c->cols < MAX_COLS ? c->cols : MAX_COLS;
    c->ncurve = 0;
    for (uint32_t k = 0; k < nc && c->ncurve < ZXVS_MAX_CURVES; k++) {
        col_t *cl = &c->col[k];
        uint32_t ne = cl->num + cl->other;
        if (cl->num >= 2 && cl->other * 100 <= ne) c->curve_col[c->ncurve++] = k;
    }
    for (uint32_t ci = 0; ci < c->ncurve; ci++) {
        uint32_t k = c->curve_col[ci];
        uint32_t nv = table_column(c, k, header);
        int64_t vmin = INT64_MAX, vmax = INT64_MIN;
        for (uint32_t i = 0; i < nv; i++) {
            if (c->v[i] < vmin) vmin = c->v[i];
            if (c->v[i] > vmax) vmax = c->v[i];
        }
        if (!nv) vmin = vmax = 0;
        uint64_t range = (uint64_t) vmax - (uint64_t) vmin;
        for (uint32_t i = 0; i < nv; i++) {
            uint64_t dv = (uint64_t) c->v[i] - (uint64_t) vmin;
            c->y[i] = range
                          ? (int32_t) div128(
                                add128(mul64(dv, (uint64_t) 1 << 24), (u128){0, range / 2}), range)
                          : 0;
        }
        fit_t f = {c->y, nv, c->tol, c->keep, c->c1, c->c2, c->bp, 0, 0, 0};
        fit(&f);
        uint64_t at = sec_begin(w, ZXVS_CURVE);
        w8(w, k);
        write_curvebody(w, &f, (const char *) c->col[k].name, c->col[k].nname,
                        (uint32_t) c->col[k].dec, vmin, range);
        sec_end(w, at);
        c->rep.ncurves++;
        c->rep.nseg += f.nseg;
        c->rep.npts += nv;
        if (f.maxerr > c->rep.curve_max_q24) c->rep.curve_max_q24 = f.maxerr;
        if (f.rmserr > c->rep.curve_rms_q24) c->rep.curve_rms_q24 = f.rmserr;
        if (ci == 0 && nv) {
            c->have_curve = true;
            for (int i = 0; i < 16; i++) {
                uint32_t x = nv > 1 ? (uint32_t) ((uint64_t) i * (nv - 1) / 15) : 0;
                int32_t v = c->y[x];
                c->first_curve[i] = (uint32_t) (v < 0 ? 0 : v) >> 8;
            }
        }
    }
}

static void tree_table(ctx_t *c)
{
    int32_t root =
        tree_add(&c->tr, -1, ZXVS_N_TABLE, (const uint8_t *) (c->sep == ',' ? "csv" : "tsv"), 3);
    uint32_t nc = c->cols;
    for (uint32_t k = 0; k < nc; k++) {
        const uint8_t *nm = k < MAX_COLS ? c->col[k].name : 0;
        uint32_t nl = k < MAX_COLS ? c->col[k].nname : 0;
        int32_t me = tree_add(&c->tr, root, ZXVS_N_COLUMN, nm, nm ? nl : 0);
        if (me >= 0)
            c->tr.nd[me].nchild =
                k < MAX_COLS ? c->col[k].num + c->col[k].other + c->col[k].empty : 0;
    }
}

static void struct_write(ctx_t *c, wr_t *w, uint32_t grammar)
{
    tree_t *t = &c->tr;
    tree_layout(t);
    uint64_t at = sec_begin(w, ZXVS_STRUCT);
    w8(w, grammar);
    wuv(w, t->total);
    wuv(w, t->n);
    for (uint32_t i = 0; i < t->n; i++) {
        node_t *x = &t->nd[t->order[i]];
        uint32_t up = x->parent >= 0 ? i - (uint32_t) t->newid[x->parent] : 0;
        wuv(w, up);
        w8(w, x->type);
        wuv(w, x->nchild);
        wuv(w, x->x);
        wuv(w, x->y);
        w8(w, x->nlabel);
        wbytes(w, x->label, x->nlabel);
        uint32_t dpt = x->y < 15 ? x->y : 15;
        c->depth_hist[dpt]++;
        if (x->y > c->maxdepth) c->maxdepth = x->y;
    }
    sec_end(w, at);
    c->rep.nodes = t->n;
    c->rep.total_nodes = t->total;
}

static uint32_t rd16le(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8);
}
static uint32_t rd32le(const uint8_t *p)
{
    return rd16le(p) | (rd16le(p + 2) << 16);
}
static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void ref_stage(ctx_t *c, wr_t *w, uint32_t *ma, uint32_t *mb, uint64_t *mf)
{
    const uint8_t *d = c->d;
    uint64_t n = c->n;
    uint32_t a = 0, b = 0;
    uint64_t fr = 0;
    switch (c->fmt) {
    case ZXVS_FMT_PNG:
        if (n >= 24) a = rd32be(d + 16), b = rd32be(d + 20);
        break;
    case ZXVS_FMT_GIF:
        if (n >= 10) a = rd16le(d + 6), b = rd16le(d + 8);
        break;
    case ZXVS_FMT_BMP:
        if (n >= 26) a = rd32le(d + 18), b = rd32le(d + 22);
        if (b & 0x80000000u) b = 0u - b;
        break;
    case ZXVS_FMT_JPEG:
        for (uint64_t i = 2; i + 9 < n;) {
            if (d[i] != 0xFF) break;
            uint32_t mk = d[i + 1], L = ((uint32_t) d[i + 2] << 8) | d[i + 3];
            if ((mk >= 0xC0 && mk <= 0xCF) && mk != 0xC4 && mk != 0xC8 && mk != 0xCC) {
                b = ((uint32_t) d[i + 5] << 8) | d[i + 6];
                a = ((uint32_t) d[i + 7] << 8) | d[i + 8];
                break;
            }
            i += 2 + L;
        }
        break;
    case ZXVS_FMT_WAV:
        for (uint64_t i = 12; i + 8 <= n;) {
            uint32_t L = rd32le(d + i + 4);
            if (d[i] == 'f' && d[i + 1] == 'm' && d[i + 2] == 't' && i + 24 <= n) {
                b = rd16le(d + i + 10);
                a = rd32le(d + i + 12);
            }
            if (d[i] == 'd' && d[i + 1] == 'a' && d[i + 2] == 't' && d[i + 3] == 'a') {
                uint32_t blk = b ? b * 2 : 0; /* 16-bit assumed for the frame count; 0 if unknown */
                if (i + 22 <= n && d[12] == 'f') {
                    uint32_t bits = rd16le(d + 12 + 22);
                    blk = b * ((bits + 7) / 8);
                }
                fr = blk ? L / blk : 0;
                break;
            }
            i += 8 + (uint64_t) L + (L & 1);
        }
        break;
    case ZXVS_FMT_FLAC:
        if (n >= 26) {
            const uint8_t *s = d + 8; /* STREAMINFO body */
            a = ((uint32_t) s[10] << 12) | ((uint32_t) s[11] << 4) | (s[12] >> 4);
            b = ((s[12] >> 1) & 7) + 1;
            fr = ((uint64_t) (s[13] & 15) << 32) | rd32be(s + 14);
        }
        break;
    default:
        break;
    }
    uint64_t at = sec_begin(w, ZXVS_REF);
    w8(w, c->fmt);
    bool hv = c->cfg && c->cfg->ref_cid;
    w8(w, hv);
    if (hv) {
        wbytes(w, c->cfg->ref_cid, ZXVS_CID_LEN);
        w8(w, c->cfg->ref_kind == 2 ? 2 : 1);
    }
    wuv(w, a);
    wuv(w, b);
    wuv(w, fr);
    bool fk = c->cfg && c->cfg->ref_fid_known;
    w8(w, fk);
    wzz(w, fk ? c->cfg->ref_fid_cdb : 0);
    sec_end(w, at);
    *ma = a;
    *mb = b;
    *mf = fr;
}

static uint32_t q16frac(uint64_t num, uint64_t den)
{
    if (!den) return 0;
    uint64_t v = muldiv(num, 65536, den);
    return v > 65536 ? 65536 : (uint32_t) v;
}

static void make_shape(ctx_t *c, uint32_t ma, uint32_t mb, uint64_t mf)
{
    uint32_t *s = c->shape;
    zset(s, 0, sizeof c->shape);
    uint64_t n = c->n;
    for (int i = 0; i < 16; i++) {
        uint64_t t = 0;
        for (int j = 0; j < 16; j++) t += c->hist[i * 16 + j];
        s[i] = q16frac(t, n);
    }
    uint64_t esum = 0;
    for (uint32_t k = 0; k < c->nwin; k++) esum += (uint64_t) c->ent[k];
    for (int i = 0; i < 16; i++) {
        uint32_t x = c->nwin > 1 ? (uint32_t) ((uint64_t) i * (c->nwin - 1) / 15) : 0;
        s[16 + i] = (uint32_t) c->ent[x] >> 8;
    }
    if (c->kind == ZXVS_K_SERIES && c->have_curve) {
        for (int i = 0; i < 16; i++)
            s[32 + i] = c->first_curve[i] > 65536 ? 65536 : c->first_curve[i];
    } else if (c->kind == ZXVS_K_STRUCT || (c->kind == ZXVS_K_SERIES && c->tr.n)) {
        for (int i = 0; i < 16; i++) s[32 + i] = q16frac(c->depth_hist[i], c->tr.n);
    } else if (c->kind == ZXVS_K_MEDIA) {
        s[32] = ma ? (log2_q16(ma) >> 4 > 65536 ? 65536 : log2_q16(ma) >> 4) : 0;
        s[33] = mb ? (log2_q16(mb) >> 4 > 65536 ? 65536 : log2_q16(mb) >> 4) : 0;
        s[34] = ma && mb ? q16frac(ma < mb ? ma : mb, ma > mb ? ma : mb) : 0;
        s[35] = mf ? (log2_q16(mf) >> 5 > 65536 ? 65536 : log2_q16(mf) >> 5) : 0;
        s[36] = c->fmt >= ZXVS_FMT_WAV ? 65536 : 0;
        s[37] = c->fmt < ZXVS_FMT_WAV ? 65536 : 0;
    } else {
        uint32_t cnt[4] = {0, 0, 0, 0}, q[4][3];
        zset(q, 0, sizeof q);
        for (uint32_t i = 0; i < c->ncell; i++) {
            cnt[c->cls[i]]++;
            uint32_t part = (uint32_t) ((uint64_t) i * 4 / (c->ncell ? c->ncell : 1));
            if (c->cls[i] < 3) q[part][c->cls[i]]++;
        }
        for (int i = 0; i < 4; i++) s[32 + i] = q16frac(cnt[i], c->ncell);
        uint32_t per = c->ncell / 4 ? c->ncell / 4 : 1;
        for (int p = 0; p < 4; p++)
            for (int k = 0; k < 3; k++) s[36 + p * 3 + k] = q16frac(q[p][k], per);
    }
    /* global features */
    s[48] = q16frac(log2_q16(n + 1), (uint64_t) 32 << 16);
    s[49] = c->nwin ? (uint32_t) ((esum / c->nwin) >> 8) : 0;
    uint64_t txt = 0, digits = 0, letters = 0, punct = 0, high = 0, nl = 0;
    for (int i = 0; i < 256; i++) {
        uint64_t h = c->hist[i];
        if ((i >= 0x20 && i < 0x7F) || i == '\n' || i == '\r' || i == '\t') txt += h;
        if (i >= '0' && i <= '9') digits += h;
        if ((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z')) letters += h;
        if (i >= 0x21 && i < 0x7F && !(i >= '0' && i <= '9') &&
            !((i >= 'a' && i <= 'z') || (i >= 'A' && i <= 'Z')))
            punct += h;
        if (i >= 0x80) high += h;
        if (i == '\n') nl += h;
    }
    s[50] = q16frac(txt, n);
    s[51] = q16frac(c->hist[0], n);
    s[52 + (c->kind - 1)] = 65536;
    s[56] = q16frac(nl * 16 > n ? n : nl * 16, n);
    s[57] = q16frac(digits, n);
    s[58] = q16frac(letters, n);
    s[59] = q16frac(punct, n);
    s[60] = q16frac(high, n);
    s[61] = c->tr.n ? q16frac(log2_q16(c->tr.n), (uint64_t) 12 << 16)
                    : q16frac(c->ncurve, ZXVS_MAX_CURVES);
    s[62] = c->rep.npts ? q16frac(c->rep.nseg, c->rep.npts) : 0;
    s[63] = q16frac(c->maxdepth, ZXVS_MAX_DEPTH);
}

static void cid_of(const uint8_t *d, uint64_t n, uint8_t cid[ZXVS_CID_LEN])
{
    sha256_ctx_t h;
    sha256_init(&h);
    while (n) {
        uint32_t k = n > 0x40000000u ? 0x40000000u : (uint32_t) n;
        sha256_update(&h, d, k);
        d += k;
        n -= k;
    }
    cid[0] = 0x01;
    cid[1] = 0x55;
    cid[2] = 0x12;
    cid[3] = 0x20;
    sha256_final(&h, cid + 4);
}

int64_t zxvs_describe(const uint8_t *d, uint64_t len, void *work, uint64_t work_size, uint8_t *out,
                      uint64_t out_cap, const zxvs_cfg_t *cfg, zxvs_report_t *rep)
{
    if ((!d && len) || !work || !out) return ZXVS_EARG;
    if (len >= ((uint64_t) 1 << 32)) return ZXVS_ECAP;
    uint32_t mn = cfg && cfg->max_nodes ? cfg->max_nodes : ZXVS_MAX_NODES;
    if (mn > ZXVS_MAX_NODES) mn = ZXVS_MAX_NODES;
    uintptr_t wp = ((uintptr_t) work + 15) & ~(uintptr_t) 15;
    uint64_t skip = wp - (uintptr_t) work;
    if (work_size < skip) return ZXVS_EWORK;
    ctx_t C0, *c = &C0;
    zset(c, 0, sizeof *c);
    arena_t ar = {(uint8_t *) wp, 0, work_size - skip, false, false};
    layout(&ar, c, len, mn);
    if (ar.bad) return ZXVS_EWORK;
    static const uint8_t none = 0;
    c->d = len ? d : &none;
    c->n = len;
    c->cfg = cfg;
    c->tol = (cfg && cfg->tol_q16 ? cfg->tol_q16 : TOL_DEF);
    if (c->tol > 65536) c->tol = 65536;
    c->tol *= 256; /* Q16 of range -> 2^-24 of range */
    c->fmt = detect(c);
    /* structure / series first (they may change the format on failure) */
    bool header = false;
    switch (c->fmt) {
    case ZXVS_FMT_JSON:
        c->kind = ZXVS_K_STRUCT;
        if (!parse_json(c->d, len, &c->tr)) {
            c->tr.n = c->tr.total = 0;
            c->fmt = ZXVS_FMT_TEXT;
        }
        break;
    case ZXVS_FMT_XML:
    case ZXVS_FMT_HTML:
        c->kind = ZXVS_K_STRUCT;
        if (!parse_xml(c->d, len, &c->tr, c->fmt == ZXVS_FMT_HTML)) {
            c->tr.n = c->tr.total = 0;
            c->fmt = ZXVS_FMT_TEXT;
        }
        break;
    case ZXVS_FMT_ZXCFG:
        c->kind = ZXVS_K_STRUCT;
        if (!parse_zxcfg(c->d, len, &c->tr)) {
            c->tr.n = c->tr.total = 0;
            c->fmt = ZXVS_FMT_TEXT;
        }
        break;
    case ZXVS_FMT_CSRC:
        c->kind = ZXVS_K_STRUCT;
        if (!parse_csrc(c->d, len, &c->tr)) {
            c->tr.n = c->tr.total = 0;
            c->fmt = ZXVS_FMT_TEXT;
        }
        break;
    case ZXVS_FMT_CSV:
    case ZXVS_FMT_TSV:
    case ZXVS_FMT_NUMTEXT:
        table_scan(c, &header);
        c->kind = ZXVS_K_STRUCT;
        break;
    default:
        break;
    }
    if (c->fmt == ZXVS_FMT_TEXT || c->fmt == ZXVS_FMT_BIN) c->kind = ZXVS_K_BYTES;
    if (c->fmt >= ZXVS_FMT_PNG) c->kind = ZXVS_K_MEDIA;
    if (c->fmt == ZXVS_FMT_CSV || c->fmt == ZXVS_FMT_TSV || c->fmt == ZXVS_FMT_NUMTEXT) {
        uint32_t nc = c->cols < MAX_COLS ? c->cols : MAX_COLS;
        for (uint32_t k = 0; k < nc; k++) {
            col_t *cl = &c->col[k];
            if (cl->num >= 2 && cl->other * 100 <= cl->num + cl->other) c->kind = ZXVS_K_SERIES;
        }
        if (c->fmt != ZXVS_FMT_NUMTEXT) tree_table(c);
    }
    /* header */
    wr_t w = {out, 0, out_cap, false};
    wbytes(&w, "ZXVS", 4);
    w8(&w, 1);
    w8(&w, c->kind);
    w8(&w, c->fmt);
    w8(&w, 0);
    uint8_t cid[ZXVS_CID_LEN];
    cid_of(c->d, len, cid);
    wbytes(&w, cid, ZXVS_CID_LEN);
    wuv(&w, len);
    uint64_t shape_at = w.n;
    for (int i = 0; i < ZXVS_DIMS; i++)
        wuv(&w, 65536); /* placeholder, rewritten in place (3 bytes each) */
    if (w.bad) return ZXVS_EOUT;
    bytes_stage(c, &w);
    if (c->kind == ZXVS_K_SERIES) series_stage(c, &w, header);
    if (c->tr.n) struct_write(c, &w, c->fmt);
    uint32_t ma = 0, mb = 0;
    uint64_t mf = 0;
    if (c->kind == ZXVS_K_MEDIA) ref_stage(c, &w, &ma, &mb, &mf);
    w8(&w, ZXVS_END);
    if (w.bad) return ZXVS_EOUT;
    /* the shape vector: written as fixed 3-byte varints so it can be patched */
    make_shape(c, ma, mb, mf);
    for (int i = 0; i < ZXVS_DIMS; i++) {
        uint32_t v = c->shape[i] > 65536 ? 65536 : c->shape[i];
        uint8_t *p = out + shape_at + 3 * i;
        p[0] = (uint8_t) ((v & 0x7F) | 0x80);
        p[1] = (uint8_t) (((v >> 7) & 0x7F) | 0x80);
        p[2] = (uint8_t) (v >> 14);
    }
    c->rep.kind = c->kind;
    c->rep.fmt = c->fmt;
    c->rep.bytes = (uint32_t) w.n;
    if (zxvs_check(out, (uint32_t) w.n, 0) != 0) return ZXVS_EFORMAT;
    if (rep) *rep = c->rep;
    return (int64_t) w.n;
}

/* ---- reading -------------------------------------------------------------- */
/* parse a curvebody; fills cv when not NULL. Validates everything. */
static bool parse_curvebody(rd_t *r, zxvs_curve_t *cv)
{
    zxvs_curve_t c;
    zset(&c, 0, sizeof c);
    uint32_t nn = r8(r);
    if (nn > NAME_MAX_) r->bad = true;
    for (uint32_t i = 0; i < nn && !r->bad; i++) {
        uint32_t ch = r8(r);
        if (ch < 0x20 || ch >= 0x7F) r->bad = true;
        c.name[i] = (char) ch;
    }
    c.dec = r8(r);
    if (c.dec > 18) r->bad = true;
    c.vmin = rzz(r);
    c.vrange = ruv(r);
    c.npts = (uint32_t) ruvr(r, 1, 0xFFFFFFFFu);
    c.tol = (uint32_t) ruvr(r, 0, (uint64_t) 1 << 24);
    c.maxerr = (uint32_t) ruvr(r, 0, c.tol);
    c.rmserr = (uint32_t) ruvr(r, 0, c.maxerr);
    c.nseg = (uint32_t) ruvr(r, 0, c.npts - 1);
    c.y0 = rzz(r);
    if (c.y0 < -((int64_t) 1 << 26) || c.y0 > ((int64_t) 1 << 26)) r->bad = true;
    c.seg = r->p;
    uint64_t sum = 0;
    int64_t y = c.y0;
    for (uint32_t s = 0; s < c.nseg && !r->bad; s++) {
        uint64_t dx = ruvr(r, 1, MAX_SPAN);
        int64_t d1 = rzz(r), d2 = rzz(r), d3 = rzz(r);
        const int64_t B = (int64_t) 1 << 28;
        if (d1 < -B || d1 > B || d2 < -B || d2 > B || d3 < -B || d3 > B) r->bad = true;
        y += d3;
        if (y < -((int64_t) 1 << 26) || y > ((int64_t) 1 << 26)) r->bad = true;
        sum += dx;
    }
    if (!r->bad && sum != (uint64_t) c.npts - 1) r->bad = true;
    c.end = r->p;
    if (cv && !r->bad) *cv = c;
    return !r->bad;
}

typedef struct {
    rd_t r;
    zxvs_info_t in;
} walk_t;

static int walk(const uint8_t *d, uint32_t n, zxvs_info_t *info, uint32_t want_curve,
                zxvs_curve_t *cv, const uint8_t **ref_at)
{
    if (!d || n < 4 + 4 + ZXVS_CID_LEN + 1 + ZXVS_DIMS + 1) return ZXVS_EFORMAT;
    if (d[0] != 'Z' || d[1] != 'X' || d[2] != 'V' || d[3] != 'S' || d[4] != 1 || d[7] != 0)
        return ZXVS_EFORMAT;
    zxvs_info_t in;
    zset(&in, 0, sizeof in);
    in.kind = d[5];
    in.fmt = d[6];
    if (in.kind < 1 || in.kind > 4 || in.fmt > ZXVS_FMT_MAX) return ZXVS_EFORMAT;
    zcpy(in.cid, d + 8, ZXVS_CID_LEN);
    if (in.cid[0] != 1 || in.cid[1] != 0x55 || in.cid[2] != 0x12 || in.cid[3] != 0x20)
        return ZXVS_EFORMAT;
    rd_t r = {d + 8 + ZXVS_CID_LEN, d + n, false};
    in.src_bytes = (uint32_t) ruvr(&r, 0, 0xFFFFFFFFu);
    for (int i = 0; i < ZXVS_DIMS; i++) in.shape[i] = (uint32_t) ruvr(&r, 0, 65536);
    uint32_t last = 0, ncurve = 0;
    bool seen[8] = {false};
    bool found = false;
    for (;;) {
        uint32_t tag = r8(&r);
        if (r.bad) return ZXVS_EFORMAT;
        if (tag == ZXVS_END) break;
        if (tag > ZXVS_REF || tag < last || (tag == last && tag != ZXVS_CURVE) ||
            (seen[tag] && tag != ZXVS_CURVE))
            return ZXVS_EFORMAT;
        seen[tag] = true;
        last = tag;
        uint32_t len = r32(&r);
        if (r.bad || len > (uint64_t) (r.e - r.p)) return ZXVS_EFORMAT;
        rd_t b = {r.p, r.p + len, false};
        switch (tag) {
        case ZXVS_HIST: {
            uint64_t sum = 0;
            for (int i = 0; i < 256; i++) sum += ruvr(&b, 0, 0xFFFFFFFFu);
            if (sum != in.src_bytes) b.bad = true;
            break;
        }
        case ZXVS_ENTROPY: {
            ruvr(&b, 1, 0xFFFFFFFFu);
            zxvs_curve_t c;
            if (parse_curvebody(&b, &c) && want_curve == ZXVS_CURVE_ENTROPY && cv) {
                *cv = c;
                cv->column = 0;
                found = true;
            }
            break;
        }
        case ZXVS_HILBERT: {
            uint32_t k = r8(&b);
            if (k > HIL_MAXK) b.bad = true;
            uint64_t bpc = ruvr(&b, 1, 0xFFFFFFFFu);
            uint64_t nrun = ruvr(&b, 0, 4096), cells = 0;
            for (uint64_t i = 0; i < nrun && !b.bad; i++) {
                if (r8(&b) > 3) b.bad = true;
                cells += ruvr(&b, 1, 4096);
            }
            if (b.bad) break;
            uint64_t want = in.src_bytes ? (in.src_bytes + bpc - 1) / bpc : 0;
            if (cells != want || cells > ((uint64_t) 1 << (2 * k))) b.bad = true;
            uint64_t ne = ruvr(&b, 0, 2 * 65 * 64), S = (uint64_t) 1 << k;
            for (uint64_t i = 0; i < ne && !b.bad; i++) {
                uint32_t dir = r8(&b);
                uint64_t x = ruv(&b), y = ruv(&b), L = ruvr(&b, 1, S);
                if (dir > 1) b.bad = true;
                if (dir == 0 && (y > S || x + L > S)) b.bad = true;
                if (dir == 1 && (x > S || y + L > S)) b.bad = true;
            }
            break;
        }
        case ZXVS_CURVE: {
            if (ncurve >= ZXVS_MAX_CURVES) return ZXVS_EFORMAT;
            uint32_t col = r8(&b);
            zxvs_curve_t c;
            if (parse_curvebody(&b, &c) && want_curve == ncurve && cv) {
                *cv = c;
                cv->column = col;
                found = true;
            }
            ncurve++;
            break;
        }
        case ZXVS_STRUCT: {
            uint32_t g = r8(&b);
            if (g > ZXVS_FMT_MAX) b.bad = true;
            uint64_t total = ruv(&b), nk = ruvr(&b, 0, ZXVS_MAX_NODES);
            if (nk > total || total > 0xFFFFFFFFu) b.bad = true;
            for (uint64_t i = 0; i < nk && !b.bad; i++) {
                uint64_t up = ruv(&b);
                if ((i == 0 && up != 0) || (i > 0 && (up < 1 || up > i))) b.bad = true;
                if (r8(&b) > ZXVS_N_MAX) b.bad = true;
                ruvr(&b, 0, 0xFFFFFFFFu);
                ruvr(&b, 0, 0xFFFFFFFFu);
                ruvr(&b, 0, 0xFFFFFFFFu);
                uint32_t nl = r8(&b);
                if (nl > 15) b.bad = true;
                for (uint32_t j = 0; j < nl && !b.bad; j++) {
                    uint32_t ch = r8(&b);
                    if (ch < 0x21 || ch >= 0x7F) b.bad = true;
                }
            }
            in.nnodes = (uint32_t) nk;
            in.total_nodes = (uint32_t) total;
            break;
        }
        case ZXVS_REF: {
            if (ref_at) *ref_at = b.p;
            uint32_t m = r8(&b);
            if (m < ZXVS_FMT_PNG || m > ZXVS_FMT_MAX) b.bad = true;
            uint32_t hv = r8(&b);
            if (hv > 1) b.bad = true;
            if (hv) {
                for (int i = 0; i < ZXVS_CID_LEN; i++) {
                    uint32_t ch = r8(&b);
                    if (i == 0 && ch != 1) b.bad = true;
                }
                uint32_t vk = r8(&b);
                if (vk < 1 || vk > 2) b.bad = true;
            }
            ruvr(&b, 0, 0xFFFFFFFFu);
            ruvr(&b, 0, 0xFFFFFFFFu);
            ruv(&b);
            if (r8(&b) > 1) b.bad = true;
            rzz(&b);
            in.has_ref = true;
            break;
        }
        default:
            return ZXVS_EFORMAT;
        }
        if (b.bad || b.p != b.e) return ZXVS_EFORMAT;
        r.p += len;
    }
    if (r.p != r.e) return ZXVS_EFORMAT;
    /* every kind has the byte sections; the kind's own section is present */
    if (!seen[ZXVS_HIST] || !seen[ZXVS_ENTROPY] || !seen[ZXVS_HILBERT]) return ZXVS_EFORMAT;
    if (in.kind == ZXVS_K_SERIES && !ncurve) return ZXVS_EFORMAT;
    if (in.kind == ZXVS_K_STRUCT && !seen[ZXVS_STRUCT]) return ZXVS_EFORMAT;
    if (in.kind == ZXVS_K_MEDIA && !seen[ZXVS_REF]) return ZXVS_EFORMAT;
    in.ncurves = ncurve;
    if (info) *info = in;
    if (cv && !found) return ZXVS_EARG;
    return 0;
}

int zxvs_check(const uint8_t *d, uint32_t n, zxvs_info_t *info)
{
    return walk(d, n, info, 0xFFFFFFFEu, 0, 0) == 0 ? 0 : ZXVS_EFORMAT;
}

int zxvs_curve(const uint8_t *d, uint32_t n, uint32_t which, zxvs_curve_t *c)
{
    if (!c) return ZXVS_EARG;
    return walk(d, n, 0, which, c, 0);
}

int zxvs_curve_eval(const zxvs_curve_t *c, uint32_t x, int64_t *yn)
{
    if (!c || !yn || x >= c->npts) return ZXVS_EARG;
    rd_t r = {c->seg, c->end, false};
    int64_t y = c->y0;
    uint32_t x0 = 0;
    if (c->nseg == 0 || x == 0) {
        *yn = y;
        if (c->nseg == 0) return 0;
    }
    for (uint32_t s = 0; s < c->nseg; s++) {
        uint32_t dx = (uint32_t) ruv(&r);
        int64_t d1 = rzz(&r), d2 = rzz(&r), d3 = rzz(&r);
        if (r.bad) return ZXVS_EFORMAT;
        if (x <= x0 + dx) {
            int64_t t = (int64_t) (((uint64_t) (x - x0) << 16) / dx);
            *yn = bez_eval(y, y + d1, y + d2, y + d3, t);
            return 0;
        }
        y += d3;
        x0 += dx;
    }
    return ZXVS_EFORMAT;
}

int zxvs_section(const uint8_t *d, uint32_t n, uint32_t tag, uint32_t index, const uint8_t **body,
                 uint32_t *len)
{
    if (!d || !body || !len || n < 8 + ZXVS_CID_LEN) return ZXVS_EARG;
    rd_t r = {d + 8 + ZXVS_CID_LEN, d + n, false};
    ruv(&r);
    for (int i = 0; i < ZXVS_DIMS; i++) ruv(&r);
    uint32_t k = 0;
    while (!r.bad) {
        uint32_t t = r8(&r);
        if (r.bad || t == ZXVS_END) break;
        uint32_t L = r32(&r);
        if (r.bad || L > (uint64_t) (r.e - r.p)) break;
        if (t == tag && k++ == index) {
            *body = r.p;
            *len = L;
            return 0;
        }
        r.p += L;
    }
    return ZXVS_EARG;
}

uint32_t zxvs_similarity_vec(const uint32_t a[ZXVS_DIMS], const uint32_t b[ZXVS_DIMS])
{
    uint64_t s = 0;
    for (int i = 0; i < ZXVS_DIMS; i++) s += a[i] > b[i] ? a[i] - b[i] : b[i] - a[i];
    s /= ZXVS_DIMS;
    return s >= 65536 ? 0 : 65536u - (uint32_t) s;
}

int32_t zxvs_similarity(const uint8_t *a, uint32_t na, const uint8_t *b, uint32_t nb)
{
    zxvs_info_t ia, ib;
    if (zxvs_check(a, na, &ia) || zxvs_check(b, nb, &ib)) return ZXVS_EFORMAT;
    return (int32_t) zxvs_similarity_vec(ia.shape, ib.shape);
}

int zxvs_register(const uint8_t *desc, uint32_t len, uint64_t source_word, zxvs_register_fn fn,
                  void *ctx)
{
    zxvs_info_t in;
    if (!fn) return ZXVS_EARG;
    if (zxvs_check(desc, len, &in)) return ZXVS_EFORMAT;
    int rc = fn(ctx, zxvs_gc_rebase(source_word, ZXVS_GC_DOMAIN_DATA), in.cid, desc, len);
    if (rc) return rc;
    return fn(ctx, zxvs_gc_rebase(source_word, ZXVS_GC_DOMAIN_EMBED), in.cid, desc, len);
}

int zxvs_alxv_row(const uint8_t *desc, uint32_t len, int8_t row[ZXVS_DIMS], uint32_t *scale_q16)
{
    zxvs_info_t in;
    if (!row || !scale_q16 || zxvs_check(desc, len, &in)) return ZXVS_EFORMAT;
    uint32_t mx = 0;
    for (uint32_t i = 0; i < ZXVS_DIMS; i++)
        if (in.shape[i] > mx) mx = in.shape[i];
    uint32_t sc = (mx + 126u) / 127u;
    if (sc == 0) sc = 1;
    for (uint32_t i = 0; i < ZXVS_DIMS; i++) row[i] = (int8_t) ((in.shape[i] + sc / 2u) / sc);
    *scale_q16 = sc;
    return 0;
}

const char *zxvs_kind_name(uint32_t kind)
{
    switch (kind) {
    case ZXVS_K_BYTES:
        return "bytes";
    case ZXVS_K_SERIES:
        return "series";
    case ZXVS_K_STRUCT:
        return "struct";
    case ZXVS_K_MEDIA:
        return "media";
    default:
        return "unknown";
    }
}

const char *zxvs_fmt_name(uint32_t fmt)
{
    switch (fmt) {
    case ZXVS_FMT_BIN:
        return "bin";
    case ZXVS_FMT_TEXT:
        return "text";
    case ZXVS_FMT_CSV:
        return "csv";
    case ZXVS_FMT_TSV:
        return "tsv";
    case ZXVS_FMT_JSON:
        return "json";
    case ZXVS_FMT_XML:
        return "xml";
    case ZXVS_FMT_HTML:
        return "html";
    case ZXVS_FMT_ZXCFG:
        return "zxcfg";
    case ZXVS_FMT_CSRC:
        return "c";
    case ZXVS_FMT_NUMTEXT:
        return "numbers";
    case ZXVS_FMT_PNG:
        return "png";
    case ZXVS_FMT_JPEG:
        return "jpeg";
    case ZXVS_FMT_GIF:
        return "gif";
    case ZXVS_FMT_BMP:
        return "bmp";
    case ZXVS_FMT_ZXVG:
        return "zxvg";
    case ZXVS_FMT_WAV:
        return "wav";
    case ZXVS_FMT_AIFF:
        return "aiff";
    case ZXVS_FMT_FLAC:
        return "flac";
    case ZXVS_FMT_ZXVA:
        return "zxva";
    case ZXVS_FMT_MP3:
        return "mp3";
    case ZXVS_FMT_OGG:
        return "ogg";
    case ZXVS_FMT_MP4:
        return "mp4";
    default:
        return "unknown";
    }
}
