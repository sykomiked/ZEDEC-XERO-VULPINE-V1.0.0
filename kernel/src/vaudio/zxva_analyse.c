/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxva_analyse.c — recordings to ZXVA in integers. See zxva_analyse.h. */
#include "zxva_analyse.h"

#include <stddef.h>

#include "../robin_debanks/sha256.h"

/* ---- the shared stage (zxva_stft_math.h) as C ------------------------- */
typedef int64_t zl;
typedef uint64_t zw;
typedef int32_t zi;
typedef uint32_t zu;
#define ZKC(t, x)       ((t) (x))
#define ZK_FN           static inline
#define ZXVA_K_SRC(...) __VA_ARGS__
#include "zxva_stft_math.h"
#undef ZXVA_K_SRC

/* ---- constants (tools/zxva/wav2zxva.py) -------------------------------- */
#define ONE30      ((int64_t) 1 << 30)
#define US         1000000ull
#define MAXCODE    6144
#define FMAX       ((1u << 31) - 1)
#define PAD        4u         /* zero padding of the partial STFT        */
#define MIN_FRAMES 9u         /* a partial lasts one window (8 hops)     */
#define PHASE_TOL  (1u << 29) /* 0.125 turn, Q32                         */
#define SALIENCE   1200       /* 12 dB, 1/100 dB                         */
#define REL_DIV    31623u     /* 90 dB                                   */
#define NB_NOISE   24u
#define SEED       0x5A58u
#define MAX_GRAINS 4096u
#define BATCH      8u    /* frames per backend call (default)       */
#define BATCH_MAX  1024u /* zxva_backend_t.batch cap                */
#define RES_TMP    (ZXVA_RES_BLOCK * 9 + 16)

/* ---- integer helpers --------------------------------------------------- */
static int64_t sar(int64_t v, int s)
{
    return v >= 0 ? v >> s : ~(~v >> s);
}
static uint64_t uabs(int64_t v)
{
    return v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}
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
static u128 u128of(uint64_t v)
{
    u128 r = {0, v};
    return r;
}
static u128 shr128(u128 a, int s)
{
    if (s == 0) return a;
    if (s >= 64) {
        a.lo = s >= 128 ? 0 : a.hi >> (s - 64);
        a.hi = 0;
        return a;
    }
    a.lo = (a.lo >> s) | (a.hi << (64 - s));
    a.hi >>= s;
    return a;
}
static u128 shl128(u128 a, int s)
{
    if (s == 0) return a;
    if (s >= 64) {
        a.hi = s >= 128 ? 0 : a.lo << (s - 64);
        a.lo = 0;
        return a;
    }
    a.hi = (a.hi << s) | (a.lo >> (64 - s));
    a.lo <<= s;
    return a;
}
static int cmp128(u128 a, u128 b)
{
    if (a.hi != b.hi) return a.hi < b.hi ? -1 : 1;
    if (a.lo != b.lo) return a.lo < b.lo ? -1 : 1;
    return 0;
}
/* n / d for d > 0, the full 128-bit quotient; rem optional */
static u128 div128(u128 n, uint64_t d, uint64_t *rem)
{
    u128 q = {0, 0};
    uint64_t r = 0;
    for (int i = 127; i >= 0; i--) {
        uint64_t bit = i >= 64 ? (n.hi >> (i - 64)) & 1u : (n.lo >> i) & 1u;
        uint64_t carry = r >> 63;
        r = (r << 1) | bit;
        if (carry || r >= d) {
            r -= d;
            if (i >= 64)
                q.hi |= (uint64_t) 1 << (i - 64);
            else
                q.lo |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}
/* floor(a * b / d), saturated to 64 bits */
static uint64_t muldiv(uint64_t a, uint64_t b, uint64_t d)
{
    u128 q = div128(mul64(a, b), d, 0);
    return q.hi ? UINT64_MAX : q.lo;
}
/* floor(a * b / d) for signed a */
static int64_t smuldiv(int64_t a, uint64_t b, uint64_t d)
{
    uint64_t rem;
    u128 q = div128(mul64(uabs(a), b), d, &rem);
    uint64_t m = q.hi || q.lo > (uint64_t) INT64_MAX ? (uint64_t) INT64_MAX : q.lo;
    if (a >= 0) return (int64_t) m;
    return -(int64_t) m - (rem ? 1 : 0);
}
/* (a * b) mod 2^32 * ... : (x mod M) * 2^s / M for phase fractions */
static uint64_t frac_q(uint64_t x, uint64_t M, int s)
{
    u128 n = shl128(u128of(x % M), s);
    return div128(n, M, 0).lo;
}
static uint64_t isqrt128(u128 v)
{
    uint64_t r = 0;
    for (int b = 63; b >= 0; b--) {
        uint64_t t = r | ((uint64_t) 1 << b);
        if (cmp128(mul64(t, t), v) <= 0) r = t;
    }
    return r;
}
static uint64_t isqrt64(uint64_t v)
{
    return isqrt128(u128of(v));
}
/* log2(v), Q32, v >= 1 (0 gives 0) */
static int64_t log2_128(u128 v)
{
    if (!v.hi && !v.lo) return 0;
    int top = v.hi ? 64 + 63 - __builtin_clzll(v.hi) : 63 - __builtin_clzll(v.lo);
    int sh = top - 60;
    uint64_t m = sh > 0 ? shr128(v, sh).lo : v.lo << -sh; /* 2^60 <= m < 2^61 */
    /* zxva_log2_q32(m) = log2(m / 2^30); log2(v) = log2(m) + sh */
    return zxva_log2_q32(m) + (int64_t) (sh + 30) * ((int64_t) 1 << 32);
}
static int64_t log2_64(uint64_t v)
{
    return log2_128(u128of(v));
}
/* 10 log10 of a ratio given as log2 Q32 difference, in 1/100 dB */
static int32_t cdb10(int64_t d_q32)
{
    int64_t v = smuldiv(d_q32, 30103, 100ull << 32);
    return v > 2000000 ? 2000000 : v < -2000000 ? -2000000 : (int32_t) v;
}
/* 20 log10 of a ratio, 1/100 dB */
static int32_t cdb20(int64_t d_q32)
{
    return cdb10(d_q32) * 2;
}
static int32_t cos_q30(uint64_t ph)
{
    return zxva_sin_q30(ph + ((uint64_t) 1 << 62));
}

/* atan2(y, x) in turns, Q32 (CORDIC, 31 steps) */
static const uint32_t ATAN_T[31] = {
    536870912, 316933406, 167458907, 85004756, 42667331, 21354465, 10679838, 5340245,
    2670163,   1335087,   667544,    333772,   166886,   83443,    41722,    20861,
    10430,     5215,      2608,      1304,     652,      326,      163,      81,
    41,        20,        10,        5,        3,        1,        1};
static uint32_t atan2_q32(int64_t y, int64_t x)
{
    if (!x && !y) return 0;
    uint32_t z = 0;
    if (x < 0) {
        x = -x;
        y = -y;
        z = 1u << 31;
    }
    /* keep |x|, |y| < 2^40 so the iterations stay in range */
    while (uabs(x) >= ((uint64_t) 1 << 40) || uabs(y) >= ((uint64_t) 1 << 40)) {
        x = sar(x, 1);
        y = sar(y, 1);
    }
    x *= (int64_t) 1 << 20;
    y *= (int64_t) 1 << 20;
    for (int i = 0; i < 31; i++) {
        int64_t xs = sar(x, i), ys = sar(y, i);
        if (y > 0) {
            x += ys;
            y -= xs;
            z += ATAN_T[i];
        } else {
            x -= ys;
            y += xs;
            z -= ATAN_T[i];
        }
    }
    return z;
}

/* ---- tables ------------------------------------------------------------ */
void zxva_tables_bh(int32_t *w, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++) {
        uint64_t ph = muldiv(k, (uint64_t) 1 << 63, n) << 1; /* k / n turn, Q64 */
        int64_t v = 385204879LL * ONE30 - 524297395LL * cos_q30(ph) +
                    151698245LL * cos_q30(2 * ph) - 12541305LL * cos_q30(3 * ph);
        w[k] = (int32_t) sar(v + (ONE30 >> 1), 30);
    }
}
void zxva_tables_hann(int32_t *w, uint32_t n)
{
    for (uint32_t k = 0; k < n; k++) {
        uint64_t ph = muldiv(k, (uint64_t) 1 << 63, n) << 1;
        w[k] = (int32_t) sar(ONE30 - cos_q30(ph) + 1, 1);
    }
}
void zxva_tables_tw(int32_t *tw, uint32_t N)
{
    for (uint32_t k = 0; k < N / 2; k++) {
        uint64_t ph = muldiv(k, (uint64_t) 1 << 63, N) << 1;
        tw[k] = cos_q30(ph);
        tw[N / 2 + k] = zxva_sin_q30(ph);
    }
}
/* symmetric Hann of length L (numpy.hanning): 0.5 - 0.5 cos(2 pi k / (L - 1)) */
static int32_t hann_sym(uint32_t k, uint32_t L)
{
    if (L < 2) return (int32_t) ONE30;
    uint64_t ph = muldiv(k, (uint64_t) 1 << 63, L - 1) << 1;
    return (int32_t) sar(ONE30 - cos_q30(ph) + 1, 1);
}

/* The Blackman-Harris main lobe, |W(d)| / W(0) at d = i / 64 bins, Q30:
 * the zero-phase window is a0 + a1 cos + a2 cos 2 + a3 cos 3, so
 * W(d) = a0 sinc(d) + sum_k (a_k / 2) (sinc(d - k) + sinc(d + k)), which is
 * the discrete window's transform to O(1/n^2) for n >= 512. */
#define LOBE_N 161               /* 0 .. 2.5 bins */
static int64_t sinc64(int64_t u) /* sinc(u / 64), Q30 */
{
    if (u == 0) return ONE30;
    uint64_t au = uabs(u);
    int64_t s = zxva_sin_q30(au << 57); /* sin(pi u / 64) */
    /* / (pi u / 64) */
    return smuldiv(s, 64ull << 30, au * 3373259426ull);
}
static void lobe_table(int32_t *t)
{
    static const int64_t A[4] = {385204879LL, 524297395LL, 151698245LL, 12541305LL};
    int64_t w0 = 0;
    for (int i = 0; i < LOBE_N; i++) {
        int64_t v = A[0] * sinc64(i);
        for (int k = 1; k < 4; k++) v += A[k] / 2 * (sinc64(i - 64 * k) + sinc64(i + 64 * k));
        v = sar(v, 30); /* Q30 */
        if (i == 0) w0 = v;
        t[i] = (int32_t) smuldiv(v < 0 ? -v : v, (uint64_t) ONE30, (uint64_t) w0);
    }
}
/* lobe at an offset given in padded bins, Q16 */
static int64_t lobe_at(const int32_t *t, int64_t off_q16)
{
    uint64_t idx = uabs(off_q16) * 64 / PAD; /* Q16, 1/64 bin units */
    uint64_t i = idx >> 16, fr = idx & 0xFFFFu;
    if (i >= LOBE_N - 1) return t[LOBE_N - 1];
    return t[i] + (((int64_t) t[i + 1] - t[i]) * (int64_t) fr >> 16);
}

/* ---- the CPU reference of the stage ------------------------------------ */
int zxva_stft_cpu(zxva_backend_t *b, const zxva_stft_plan_t *p, uint64_t k0, uint32_t nf,
                  int32_t *re, int32_t *im, uint32_t *mag, uint32_t *mask, uint32_t *top)
{
    uint32_t N = p->N, H = N / 2 + 1, mw = zxva_mask_words(N);
    if (!b->scratch || b->scratch_words < 2 * N) return -1;
    int32_t *xr = b->scratch, *xi = b->scratch + N;
    for (uint32_t f = 0; f < nf; f++) {
        zl c = zk_centre(k0 + f, p->num, p->den, p->c0);
        for (uint32_t j = 0; j < N; j++) {
            zl s = zk_src_offset(j, p->n, N);
            zi v = 0;
            if (s >= 0) {
                zl idx = c - (zl) (p->n >> 1) + s;
                if (idx >= 0 && (uint64_t) idx < p->len) v = zk_windowed(p->x[idx], p->win[s]);
            }
            uint32_t r = zk_bitrev(j, p->L);
            xr[r] = v;
            xi[r] = 0;
        }
        for (uint32_t s = 1; s <= p->L; s++) {
            uint32_t h = 1u << (s - 1);
            for (uint32_t bb = 0; bb < N / 2; bb++) {
                uint32_t i0 = (bb / h) * 2 * h + bb % h, i1 = i0 + h, t = (bb % h) * (N / (2 * h));
                zi c2 = p->tw[t], s2 = p->tw[N / 2 + t];
                zi vr = zk_twmul_re(xr[i1], xi[i1], c2, s2),
                   vi = zk_twmul_im(xr[i1], xi[i1], c2, s2);
                zi ar = xr[i0], ai = xi[i0];
                xr[i0] = zk_half_sum(ar, vr);
                xi[i0] = zk_half_sum(ai, vi);
                xr[i1] = zk_half_diff(ar, vr);
                xi[i1] = zk_half_diff(ai, vi);
            }
        }
        int32_t *R = re + (size_t) f * H, *I = im + (size_t) f * H;
        uint32_t *M = mag + (size_t) f * H, tp = 0;
        for (uint32_t j = 0; j < H; j++) {
            R[j] = xr[j];
            I[j] = xi[j];
            M[j] = zk_mag(xr[j], xi[j]);
            if (M[j] > tp) tp = M[j];
        }
        top[f] = tp;
        zu thr = zk_threshold(tp, p->floor_mag, p->rel);
        uint32_t *mk = mask + (size_t) f * mw;
        for (uint32_t w = 0; w < mw; w++) {
            uint32_t bits = 0;
            for (uint32_t q = 0; q < 32; q++) {
                uint32_t j = w * 32 + q;
                if (j >= 1 && j + 1 < H && zk_is_peak(M[j - 1], M[j], M[j + 1], thr))
                    bits |= 1u << q;
            }
            mk[w] = bits;
        }
    }
    return 0;
}

void zxva_backend_cpu(zxva_backend_t *b)
{
    b->name = "cpu";
    b->ctx = 0;
    b->stft = zxva_stft_cpu;
    b->batch = 0;
}

void zxva_cid(const uint8_t *d, uint64_t n, uint8_t cid[ZXVA_CID_LEN])
{
    sha256_ctx_t c;
    sha256_init(&c);
    while (n) {
        uint32_t k = n > 0x40000000u ? 0x40000000u : (uint32_t) n;
        sha256_update(&c, d, k);
        d += k;
        n -= k;
    }
    cid[0] = 0x01;
    cid[1] = 0x55;
    cid[2] = 0x12;
    cid[3] = 0x20;
    sha256_final(&c, cid + 4);
}

/* ---- byte writer ------------------------------------------------------- */
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
    do {
        uint32_t b = (uint32_t) (v & 0x7Fu);
        v >>= 7;
        w8(w, v ? b | 0x80u : b);
    } while (v);
}
static void wzz(wr_t *w, int64_t v)
{
    wuv(w, v < 0 ? (((uint64_t) (-(v + 1))) << 1) | 1u : (uint64_t) v << 1);
}

/* ---- arena ------------------------------------------------------------- */
typedef struct {
    uint8_t *p;
    uint64_t used, cap;
    bool measure, bad;
} arena_t;
static void *take(arena_t *a, uint64_t bytes)
{
    bytes = (bytes + 15) & ~(uint64_t) 15;
    uint64_t at = a->used;
    a->used += bytes;
    if (a->measure) return 0;
    if (a->used > a->cap) {
        a->bad = true;
        return 0;
    }
    return a->p + at;
}

/* ---- geometry of the analysis ----------------------------------------- */
/* 2^round(log2(rate * num / den)) */
static uint32_t pow2_round(uint32_t rate, uint64_t num, uint64_t den)
{
    /* x^2 = rate^2 num^2 / den^2 against 2^(2L +- 1) */
    u128 x2 = mul64((uint64_t) rate * rate, num * num);
    uint64_t d2 = den * den;
    uint32_t L = 1;
    while (L < 30) {
        u128 lim = mul64(d2, (uint64_t) 1 << 0);
        lim = shl128(lim, (int) (2 * L + 1));
        if (cmp128(x2, lim) < 0) break;
        L++;
    }
    return 1u << L;
}

typedef struct {
    uint32_t rate, nch, bits, rbits;
    uint64_t frames;
    uint32_t dur_us;
    /* partial STFT */
    uint32_t n, N, L, hop, nfr;
    /* noise and band-error STFT */
    uint32_t nn, nL, nfr_noise;
    uint32_t edges[NB_NOISE + 1];
    uint32_t maxpk, maxtr_ch, ncells;
    uint32_t s_tr;  /* transient step, samples */
    uint32_t batch; /* frames per backend call */
} geo_t;

static void erb_edges(uint32_t top, uint32_t *e);

static int geometry(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg, geo_t *g)
{
    if (!pcm || !pcm->pcm || !cfg || pcm->nch < 1 || pcm->nch > ZXVA_MAX_CH || pcm->frames < 1 ||
        pcm->rate < ZXVA_RATE_MIN || pcm->rate > ZXVA_RATE_MAX || pcm->bits < 4 || pcm->bits > 32)
        return ZXVA_AN_EARG;
    g->rate = pcm->rate;
    g->batch = cfg->backend && cfg->backend->batch ? cfg->backend->batch : BATCH;
    if (g->batch > BATCH_MAX) g->batch = BATCH_MAX;
    g->nch = pcm->nch;
    g->bits = pcm->bits;
    g->rbits = pcm->bits <= 16 ? 16 : pcm->bits <= 24 ? 24 : 32;
    g->frames = pcm->frames;
    uint64_t d = (pcm->frames * US + pcm->rate - 1) / pcm->rate;
    while (d > 1 && (d * pcm->rate + US - 1) / US > pcm->frames) d--;
    if (d >= ((uint64_t) 1 << 32)) return ZXVA_AN_ECAP;
    g->dur_us = (uint32_t) d;
    g->n = pow2_round(pcm->rate, 17, 200); /* 85 ms */
    g->L = 0;
    while ((1u << g->L) < g->n * PAD) g->L++;
    g->N = 1u << g->L;
    g->hop = g->n / 8;
    g->nfr = (uint32_t) (pcm->frames / g->hop + 1);
    g->nn = pow2_round(pcm->rate, 21, 1000); /* 21 ms */
    g->nL = 0;
    while ((1u << g->nL) < g->nn) g->nL++;
    g->nfr_noise = g->dur_us / 10000 + 1;
    uint32_t top = (uint32_t) (pcm->rate * 45ull / 100);
    if (top > 20000) top = 20000;
    erb_edges(top, g->edges);
    g->maxpk = cfg->max_peaks ? cfg->max_peaks : 160;
    uint32_t mt = cfg->max_tracks ? cfg->max_tracks : 1000;
    if (mt > ZXVA_MAX_TRACKS) mt = ZXVA_MAX_TRACKS;
    g->maxtr_ch = mt / g->nch ? mt / g->nch : 1;
    uint32_t step = pcm->rate / 100;
    g->ncells = pcm->frames > g->nn ? (uint32_t) ((pcm->frames - g->nn + step - 1) / step) : 0;
    g->s_tr = pcm->rate / 2000 ? pcm->rate / 2000 : 1;
    return 0;
}

/* ---- the work layout ---------------------------------------------------- */
typedef struct {
    uint32_t f, a, p; /* Q16 Hz, Q30 amplitude, Q32 turns */
    int32_t nxt;
    uint32_t k;
    int32_t sal; /* 1/100 dB */
} pk_t;
typedef struct {
    int32_t head, tail;
    uint32_t len;
    uint64_t energy;
} tr_t;
typedef struct {
    uint32_t df;
    uint32_t ti, pi;
} cand_t;
typedef struct {
    uint32_t t, f, a, p;
} bp_t;
typedef struct {
    uint32_t t0, ch, pan, dur, half, f, code, ph;
} grain_t;
typedef struct {
    uint32_t bin, mag;
    int32_t re, im;
} pc_t; /* a candidate peak before the per-frame cap */

typedef struct {
    zxva_work_t *zw;
    int32_t *xq, *rend, *rbuf, *model;
    uint64_t *esum;
    int32_t *win, *tw, *scratch, *re, *im;
    uint64_t *wpre; /* prefix sums of win */
    uint32_t *mag, *mask, *top;
    int32_t *nwin, *ntw;
    int32_t lobe[LOBE_N];
    pk_t *pool;
    uint32_t *fstart, *fcount;
    pc_t *pc;
    uint32_t *sel;
    cand_t *cand;
    uint32_t *act, *nxt, *usedt, *usedp;
    tr_t *trk;
    uint32_t *order, *gather;
    int32_t *salv;
    bp_t *bp, *bp2;
    grain_t *grains;
    uint16_t *ncode;
    int32_t *cellR, *cellX;
    uint8_t *rtmp;
    int32_t *hwin;   /* periodic Hann of n (the refit's weights)          */
    int32_t *sintab; /* quarter-wave sine, 1025 entries, Q30              */
    uint32_t *ent_pk, *bstart, *bfill;
    int64_t *ent_C;
    int32_t *cs, *cc, *wy;
    int64_t *ry, *ca, *cb;
    bp_t *bpall;
    uint32_t *bpoff, *bpn;
    uint32_t *gt, *gg, *onset; /* sharpening grid (us, Q16 gain), onsets */
    uint32_t gridcap, oncap, bpcap;
    int32_t *gbase; /* grain basis: s then c, Q30 */
    int32_t *gfft;  /* grain spectrum scratch: 2 x 2^gL */
    uint32_t gL;
} lay_t;

static void layout(arena_t *a, const geo_t *g, lay_t *l)
{
    uint64_t F = g->frames, H = g->N / 2 + 1;
    uint32_t pool = g->nfr * g->maxpk;
    l->zw = take(a, sizeof(zxva_work_t));
    l->xq = take(a, F * 4);
    l->rend = take(a, F * g->nch * 4);
    l->rbuf = take(a, F * 4);
    l->model = take(a, F * 4);
    l->esum = take(a, (F / g->s_tr + 3) * 8);
    l->win = take(a, (uint64_t) g->n * 4);
    l->wpre = take(a, ((uint64_t) g->n + 1) * 8);
    l->tw = take(a, (uint64_t) g->N / 2 * 2 * 4);
    l->scratch = take(a, (uint64_t) g->N * 2 * 4);
    l->re = take(a, g->batch * H * 4);
    l->im = take(a, g->batch * H * 4);
    l->mag = take(a, g->batch * H * 4);
    l->mask = take(a, (uint64_t) g->batch * zxva_mask_words(g->N) * 4);
    l->top = take(a, g->batch * 4);
    l->nwin = take(a, (uint64_t) g->nn * 4);
    l->ntw = take(a, (uint64_t) g->nn * 4);
    l->pool = take(a, (uint64_t) pool * sizeof(pk_t));
    l->fstart = take(a, (uint64_t) g->nfr * 4);
    l->fcount = take(a, (uint64_t) g->nfr * 4);
    l->pc = take(a, (H / 2 + 2) * sizeof(pc_t));
    l->sel = take(a, (H / 2 + 2) * 4);
    l->cand = take(a, (uint64_t) g->maxpk * g->maxpk * sizeof(cand_t));
    l->act = take(a, (uint64_t) g->maxpk * 4);
    l->nxt = take(a, (uint64_t) g->maxpk * 2 * 4);
    l->usedt = take(a, (uint64_t) g->maxpk * 4);
    l->usedp = take(a, (uint64_t) g->maxpk * 4);
    l->trk = take(a, (uint64_t) pool * sizeof(tr_t));
    l->order = take(a, (uint64_t) pool * 4);
    l->gather = take(a, (uint64_t) g->nfr * 4);
    l->salv = take(a, (uint64_t) g->nfr * 4);
    l->oncap = (uint32_t) (F / (10 * g->s_tr) + 2);
    l->gridcap = l->oncap * 70;
    l->bpcap = g->nfr + 4 + l->gridcap;
    l->bp = take(a, (uint64_t) l->bpcap * sizeof(bp_t));
    l->bp2 = take(a, (uint64_t) l->bpcap * sizeof(bp_t));
    l->hwin = take(a, (uint64_t) g->n * 4);
    l->sintab = take(a, 1025 * 4);
    l->ent_pk = take(a, (uint64_t) pool * 4);
    l->ent_C = take(a, (uint64_t) pool * 8);
    l->bstart = take(a, ((uint64_t) g->nfr + 1) * 4);
    l->bfill = take(a, ((uint64_t) g->nfr + 1) * 4);
    l->cs = take(a, (uint64_t) g->n * 4);
    l->cc = take(a, (uint64_t) g->n * 4);
    l->wy = take(a, (uint64_t) g->n * 4);
    l->ry = take(a, (uint64_t) g->n * 8);
    l->ca = take(a, (uint64_t) g->maxpk * 8);
    l->cb = take(a, (uint64_t) g->maxpk * 8);
    l->bpall = take(a, ((uint64_t) pool + 2 * (uint64_t) g->maxtr_ch + 4) * sizeof(bp_t));
    l->bpoff = take(a, ((uint64_t) g->maxtr_ch + 1) * 4);
    l->bpn = take(a, ((uint64_t) g->maxtr_ch + 1) * 4);
    l->gt = take(a, (uint64_t) l->gridcap * 4);
    l->gg = take(a, (uint64_t) l->gridcap * 4);
    l->onset = take(a, (uint64_t) l->oncap * 4);
    l->grains = take(a, MAX_GRAINS * sizeof(grain_t));
    l->ncode = take(a, (uint64_t) g->nfr_noise * NB_NOISE * 2);
    l->cellR = take(a, ((uint64_t) g->ncells * g->nch + 1) * NB_NOISE * 4);
    l->cellX = take(a, ((uint64_t) g->ncells * g->nch + 1) * NB_NOISE * 4);
    l->rtmp = take(a, RES_TMP);
    /* grain segments are at most rate / 50 + rate / 2000 samples */
    uint32_t gmax = g->rate / 50 + g->rate / 2000 + 2;
    l->gbase = take(a, (uint64_t) gmax * 2 * 4);
    l->gL = 0;
    while ((1u << l->gL) < 4 * gmax) l->gL++;
    l->gfft = take(a, ((uint64_t) 1 << l->gL) * 2 * 4);
}

uint64_t zxva_analyse_work_size(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg)
{
    geo_t g;
    if (geometry(pcm, cfg, &g) < 0) return 0;
    arena_t a = {0, 0, 0, true, false};
    lay_t l;
    layout(&a, &g, &l);
    return a.used + 16;
}

uint64_t zxva_analyse_out_bound(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg)
{
    geo_t g;
    if (geometry(pcm, cfg, &g) < 0) return 0;
    uint64_t b = 64;
    b += (uint64_t) g.maxtr_ch * g.nch * (((uint64_t) g.nfr + 4) * 21 + 32);
    b += (uint64_t) g.nch * ((uint64_t) g.nfr_noise * NB_NOISE * 5 + 200);
    b += (uint64_t) MAX_GRAINS * 48 + 16;
    b += 128; /* META */
    if (cfg->residual) {
        uint64_t nblk = (g.frames + ZXVA_RES_BLOCK - 1) / ZXVA_RES_BLOCK;
        b += 32 + nblk * g.nch * (5 + 2) + g.frames * g.nch * 9 + 8;
    }
    return b;
}

/* ---- level codes and band edges ---------------------------------------- */
static uint32_t level_code(uint64_t a_q30)
{
    if (a_q30 <= 64) return 0; /* <= 2^-24 */
    int64_t l = zxva_log2_q32(a_q30);
    int64_t c = MAXCODE + sar(256 * l + ((int64_t) 1 << 31), 32);
    return c < 1 ? 1 : c > MAXCODE ? MAXCODE : (uint32_t) c;
}

/* ERB-rate edges (wav2zxva.py erb_edges, 24 bands from 40 Hz): equal steps of
 * log(1 + 0.00437 f), in integer log2 / exp2 */
static void erb_edges(uint32_t top, uint32_t *e)
{
    const uint64_t K = 4692251; /* 0.00437 * 2^30 */
    int64_t a = zxva_log2_q32(ONE30 + 40 * K), b = zxva_log2_q32(ONE30 + (uint64_t) top * K);
    for (uint32_t i = 0; i <= NB_NOISE; i++) {
        int64_t l = a + (b - a) * (int64_t) i / (int64_t) NB_NOISE;
        int64_t y = zxva_exp2_q30(l) - ONE30; /* 0.00437 f, Q30 */
        uint64_t v = (uint64_t) (y < 0 ? 0 : y);
        uint32_t f = (uint32_t) ((v * 1000 + K * 500) / (K * 1000)); /* round(v / K) */
        if (i == 0)
            f = f < 1 ? 1 : f;
        else if (f <= e[i - 1])
            f = e[i - 1] + 1;
        e[i] = f;
    }
}

/* ---- context ----------------------------------------------------------- */
typedef struct {
    const zxva_pcm_t *pcm;
    const zxva_an_cfg_t *cfg;
    geo_t g;
    lay_t l;
    zxva_backend_t cpu, *be;
    wr_t out;
    zxva_an_report_t rep;
    uint32_t ngrain;
    uint64_t sumw; /* window sum, Q30 */
} an_t;

static void load_q23(an_t *A, uint32_t ch)
{
    const zxva_pcm_t *p = A->pcm;
    int s = 24 - (int) p->bits; /* b-bit full scale 2^(b-1) -> 2^23 */
    for (uint64_t i = 0; i < p->frames; i++) {
        int64_t v = p->pcm[i * p->nch + ch];
        A->l.xq[i] =
            (int32_t) (s >= 0 ? v * ((int64_t) 1 << s) : sar(v + ((int64_t) 1 << (-s - 1)), -s));
    }
}

/* ---- 1. peaks ---------------------------------------------------------- */
static int64_t lg(uint32_t m)
{
    return m ? log2_64(m) : -((int64_t) 42 << 32);
}
/* quickselect: the k-th smallest of v[0..n) (n >= 1); afterwards
 * v[0..k) <= v[k] <= v[k+1..n) */
static uint32_t kth(uint32_t *v, uint32_t n, uint32_t k)
{
    uint32_t lo = 0, hi = n - 1;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2, t = v[mid];
        v[mid] = v[hi];
        v[hi] = t;
        uint32_t piv = v[hi], st = lo;
        for (uint32_t i = lo; i < hi; i++)
            if (v[i] < piv) {
                uint32_t u = v[i];
                v[i] = v[st];
                v[st] = u;
                st++;
            }
        t = v[st];
        v[st] = v[hi];
        v[hi] = t;
        if (st == k) break;
        if (k < st)
            hi = st - 1;
        else
            lo = st + 1;
    }
    return v[k];
}
/* 2 x median of mag[a..b) */
static uint64_t median2(an_t *A, const uint32_t *mag, uint32_t a, uint32_t b)
{
    uint32_t n = b - a, *v = A->l.sel;
    for (uint32_t i = 0; i < n; i++) v[i] = mag[a + i];
    if (n & 1u) return 2ull * kth(v, n, n / 2);
    uint32_t hi = kth(v, n, n / 2), lo = 0;
    for (uint32_t i = 0; i < n / 2; i++) lo = v[i] > lo ? v[i] : lo; /* max of the lower half */
    return (uint64_t) lo + hi;
}

static int pick_frame(an_t *A, uint64_t k, const int32_t *re, const int32_t *im,
                      const uint32_t *mag, const uint32_t *mask, uint32_t *np)
{
    const geo_t *g = &A->g;
    uint32_t H = g->N / 2 + 1, cnt = 0;
    /* candidates in bin order */
    for (uint32_t w = 0; w < zxva_mask_words(g->N); w++)
        for (uint32_t m = mask[w]; m; m &= m - 1) {
            uint32_t j = w * 32 + (uint32_t) __builtin_ctz(m);
            A->l.pc[cnt].bin = j;
            A->l.pc[cnt].mag = mag[j];
            A->l.pc[cnt].re = re[j];
            A->l.pc[cnt].im = im[j];
            cnt++;
        }
    pk_t *out = A->l.pool + A->l.fstart[k];
    uint32_t n = 0;
    /* frames that hang over either end of the signal see less window: the
     * peak height scales with the window mass inside (wav2zxva.py fits
     * with the out-of-range samples weighted out, which does the same) */
    int64_t c = (int64_t) k * g->hop, h2 = g->n / 2;
    int64_t s0 = h2 - c, s1 = (int64_t) g->frames - c + h2;
    if (s0 < 0) s0 = 0;
    if (s1 > (int64_t) g->n) s1 = g->n;
    uint64_t wvalid = s1 > s0 ? A->l.wpre[s1] - A->l.wpre[s0] : 0;
    if (!wvalid) {
        *np = 0;
        return 0;
    }
    uint64_t fcap = (uint64_t) 32767 << 16;
    uint64_t fhi = (uint64_t) g->rate * 45 * 65536 / 100;
    if (fhi > fcap) fhi = fcap;
    for (uint32_t ci = 0; ci < cnt; ci++) {
        uint32_t i = A->l.pc[ci].bin;
        int64_t la = lg(mag[i - 1]), lb = lg(mag[i]), lc = lg(mag[i + 1]);
        int64_t den = la - 2 * lb + lc, d = 0;                     /* d: Q16 bins */
        if (den < 0) d = smuldiv(lc - la, 32768, (uint64_t) -den); /* 0.5 (a - c) / den */
        if (d > 32768 || d < -32768) continue;
        /* tonality: the neighbourhood against the main lobe */
        uint32_t j0 = i > 2 * PAD ? i - 2 * PAD : 1,
                 j1 = i + 2 * PAD + 1 < H - 1 ? i + 2 * PAD + 1 : H - 1;
        uint32_t mx = 0;
        for (uint32_t j = j0; j < j1; j++) mx = mag[j] > mx ? mag[j] : mx;
        int sh = 0;
        while ((mx >> sh) >= (1u << 20)) sh++;
        int64_t dot = 0;
        uint64_t rr = 0, mm = 0;
        for (uint32_t j = j0; j < j1; j++) {
            int64_t r = lobe_at(A->l.lobe, ((int64_t) j - i) * 65536 - d) >> 14; /* Q16 */
            int64_t m = mag[j] >> sh;
            dot += r * m;
            rr += (uint64_t) (r * r);
            mm += (uint64_t) (m * m);
        }
        uint64_t nr = isqrt64(rr), nm = isqrt64(mm);
        bool tonal = dot > 0 && cmp128(mul64((uint64_t) dot, 100), mul64(95, nr * nm)) >= 0;
        uint32_t m0 = i > 16 * PAD ? i - 16 * PAD : 1,
                 m1 = i + 16 * PAD + 1 < H ? i + 16 * PAD + 1 : H;
        uint64_t fl2 = median2(A, mag, m0, m1);
        /* far above the local floor: X > fl * 10^(30/20) */
        bool loud = cmp128(mul64((uint64_t) mag[i] * 2, 10000), mul64(fl2, 316228)) >= 0;
        if (!tonal && !loud) continue;
        uint64_t fq = (uint64_t) (((int64_t) i << 16) + d) * g->rate / g->N; /* Q16 Hz */
        if (fq <= (15u << 16) || fq >= fhi) continue;
        int32_t sal = fl2 ? cdb20(log2_64((uint64_t) mag[i] * 2) - log2_64(fl2)) : 32767;
        if (sal > 32767) sal = 32767;
        /* amplitude: X = mag N 4 / sumw, divided by the lobe at the offset */
        u128 xq = mul64((uint64_t) mag[i] * g->N * 4, (uint64_t) 1 << 30);
        uint64_t X = div128(xq, wvalid, 0).lo; /* Q30 */
        uint64_t lob = (uint64_t) lobe_at(A->l.lobe, d);
        uint64_t amp = lob ? muldiv(X, (uint64_t) ONE30, lob) : X;
        if (amp > 0xFFFFFFFFu) amp = 0xFFFFFFFFu;
        uint32_t ph = atan2_q32(A->l.pc[ci].im, A->l.pc[ci].re) + (1u << 30);
        if (n < g->maxpk) {
            out[n].f = (uint32_t) fq;
            out[n].a = (uint32_t) amp;
            out[n].p = ph;
            out[n].sal = sal;
            out[n].nxt = -1;
            out[n].k = (uint32_t) k;
            n++;
        } else {
            /* over the cap: replace the quietest kept peak if this is louder */
            uint32_t q = 0;
            for (uint32_t t = 1; t < n; t++)
                if (out[t].a < out[q].a) q = t;
            if (amp > out[q].a) {
                for (uint32_t t = q; t + 1 < n; t++) out[t] = out[t + 1];
                out[n - 1].f = (uint32_t) fq;
                out[n - 1].a = (uint32_t) amp;
                out[n - 1].p = ph;
                out[n - 1].sal = sal;
                out[n - 1].nxt = -1;
                out[n - 1].k = (uint32_t) k;
            }
        }
    }
    *np = n;
    return 0;
}

static int run_peaks(an_t *A)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    zxva_stft_plan_t p = {g->n,   g->N,  g->L,  (uint32_t) g->hop, 1, 0,
                          l->win, l->tw, l->xq, g->frames,         0, REL_DIV};
    /* -110 dBFS: X = 10^-5.5 -> mag = X sumw / (4 N) */
    p.floor_mag = (uint32_t) muldiv(A->sumw, 316228, (uint64_t) 4 * g->N * 100000000000ull);
    uint32_t H = g->N / 2 + 1, mw = zxva_mask_words(g->N);
    for (uint32_t k = 0; k < g->nfr; k++) l->fstart[k] = k * g->maxpk;
    for (uint64_t k0 = 0; k0 < g->nfr; k0 += g->batch) {
        uint32_t nf = g->nfr - k0 < g->batch ? (uint32_t) (g->nfr - k0) : g->batch;
        if (A->be->stft(A->be, &p, k0, nf, l->re, l->im, l->mag, l->mask, l->top) < 0)
            return ZXVA_AN_EBACK;
        A->rep.stft_frames += nf;
        for (uint32_t f = 0; f < nf; f++)
            pick_frame(A, k0 + f, l->re + (size_t) f * H, l->im + (size_t) f * H,
                       l->mag + (size_t) f * H, l->mask + (size_t) f * mw, &l->fcount[k0 + f]);
    }
    return 0;
}

/* ---- 2. tracking ------------------------------------------------------- */
static uint32_t adv_q32(uint64_t fsum_q16, uint64_t dt_num, uint64_t dt_den)
{
    /* (fsum / 2^16 / 2) Hz * dt_num / dt_den s, in turns Q32 */
    u128 n = mul64(fsum_q16, dt_num);
    n = shl128(n, 15);
    return (uint32_t) div128(n, dt_den, 0).lo;
}
static bool cand_lt(const cand_t *a, const cand_t *b)
{
    if (a->df != b->df) return a->df < b->df;
    if (a->ti != b->ti) return a->ti < b->ti;
    return a->pi < b->pi;
}
static void heap_sort(cand_t *v, uint32_t n)
{
    for (uint32_t s = n / 2; s-- > 0;) {
        uint32_t r = s;
        for (;;) {
            uint32_t c = 2 * r + 1;
            if (c >= n) break;
            if (c + 1 < n && cand_lt(&v[c], &v[c + 1])) c++;
            if (!cand_lt(&v[r], &v[c])) break;
            cand_t t = v[r];
            v[r] = v[c];
            v[c] = t;
            r = c;
        }
    }
    for (uint32_t e = n; e-- > 1;) {
        cand_t t = v[0];
        v[0] = v[e];
        v[e] = t;
        uint32_t r = 0;
        for (;;) {
            uint32_t c = 2 * r + 1;
            if (c >= e) break;
            if (c + 1 < e && cand_lt(&v[c], &v[c + 1])) c++;
            if (!cand_lt(&v[r], &v[c])) break;
            cand_t t2 = v[r];
            v[r] = v[c];
            v[c] = t2;
            r = c;
        }
    }
}

static uint32_t track(an_t *A)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    uint32_t nact = 0, ntr = 0;
    for (uint32_t k = 0; k < g->nfr; k++) {
        pk_t *P = l->pool + l->fstart[k];
        uint32_t np = l->fcount[k], nc = 0;
        for (uint32_t ti = 0; ti < nact; ti++) {
            const pk_t *t = &l->pool[l->trk[l->act[ti]].tail];
            for (uint32_t pi = 0; pi < np; pi++) {
                if (P[pi].a <= 1073) continue; /* 1e-6 */
                uint32_t df = P[pi].f > t->f ? P[pi].f - t->f : t->f - P[pi].f;
                uint64_t tol = (uint64_t) 3 * t->f > 15ull * 65536 * 100 ? (uint64_t) 3 * t->f
                                                                         : 15ull * 65536 * 100;
                if ((uint64_t) df * 100 >= tol) continue;
                uint32_t adv = adv_q32((uint64_t) P[pi].f + t->f, g->hop, g->rate);
                int32_t dp = (int32_t) (P[pi].p - t->p - adv);
                if (uabs(dp) >= PHASE_TOL) continue;
                l->cand[nc].df = df;
                l->cand[nc].ti = ti;
                l->cand[nc].pi = pi;
                nc++;
            }
        }
        heap_sort(l->cand, nc);
        for (uint32_t i = 0; i < nact; i++) l->usedt[i] = 0;
        for (uint32_t i = 0; i < np; i++) l->usedp[i] = 0;
        uint32_t nn = 0;
        for (uint32_t c = 0; c < nc; c++) {
            cand_t *cd = &l->cand[c];
            if (l->usedt[cd->ti] || l->usedp[cd->pi]) continue;
            l->usedt[cd->ti] = l->usedp[cd->pi] = 1;
            tr_t *t = &l->trk[l->act[cd->ti]];
            int32_t pidx = (int32_t) (l->fstart[k] + cd->pi);
            l->pool[t->tail].nxt = pidx;
            t->tail = pidx;
            t->len++;
            l->nxt[nn++] = l->act[cd->ti];
        }
        /* tracks that did not continue are finished (they stay in trk) */
        for (uint32_t pi = 0; pi < np; pi++) {
            if (l->usedp[pi] || P[pi].a <= 1073) continue;
            tr_t *t = &l->trk[ntr];
            t->head = t->tail = (int32_t) (l->fstart[k] + pi);
            t->len = 1;
            l->nxt[nn++] = ntr++;
        }
        for (uint32_t i = 0; i < nn; i++) l->act[i] = l->nxt[i];
        nact = nn;
    }
    return ntr;
}

/* Python's order: tracks are listed when they finish, then those still
 * active at the end; the kept ones are sorted by energy (stable). Here the
 * order is by finish frame, then position, which the energy sort makes moot
 * except for exact energy ties (resolved by birth order). */
static bool keep_track(an_t *A, const tr_t *t)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    if (t->len < MIN_FRAMES) return false;
    uint32_t n = 0;
    for (int32_t i = t->head; i >= 0; i = l->pool[i].nxt) l->gather[n++] = (uint32_t) i;
    /* coherence across one window */
    uint64_t se = 0;
    uint32_t cnt = 0;
    for (uint32_t j = 0; j + 8 < n; j++) {
        uint32_t ph = l->pool[l->gather[j]].p;
        for (uint32_t m = j; m < j + 8; m++)
            ph += adv_q32((uint64_t) l->pool[l->gather[m]].f + l->pool[l->gather[m + 1]].f, g->hop,
                          g->rate);
        int32_t e = (int32_t) (l->pool[l->gather[j + 8]].p - ph);
        int64_t e16 = sar(e, 16);
        se += (uint64_t) (e16 * e16);
        cnt++;
    }
    if (!cnt || se * 625 >= (uint64_t) cnt * 16 * ((uint64_t) 1 << 32)) return false;
    /* salience: median >= 12 dB */
    for (uint32_t j = 0; j < n; j++) l->salv[j] = l->pool[l->gather[j]].sal + 65536;
    uint32_t *v = (uint32_t *) l->salv;
    int64_t med2;
    if (n & 1u) {
        med2 = 2 * ((int64_t) kth(v, n, n / 2) - 65536);
    } else {
        uint32_t hi = kth(v, n, n / 2), lo = 0;
        for (uint32_t j = 0; j < n / 2; j++) lo = v[j] > lo ? v[j] : lo;
        med2 = (int64_t) hi + lo - 2 * 65536;
    }
    return med2 >= 2 * SALIENCE;
}

static bool tr_before(const tr_t *a, uint32_t ia, const tr_t *b, uint32_t ib)
{
    if (a->energy != b->energy) return a->energy > b->energy;
    return ia < ib;
}

static void sift(lay_t *l, uint32_t r, uint32_t n)
{
    uint32_t *o = l->order;
    for (;;) {
        uint32_t c = 2 * r + 1;
        if (c >= n) break;
        if (c + 1 < n && tr_before(&l->trk[o[c]], o[c], &l->trk[o[c + 1]], o[c + 1])) c++;
        if (!tr_before(&l->trk[o[r]], o[r], &l->trk[o[c]], o[c])) break;
        uint32_t t = o[r];
        o[r] = o[c];
        o[c] = t;
        r = c;
    }
}
/* heap sort of order[0..n) so that tr_before holds front to back */
static void sort_tracks(lay_t *l, uint32_t n)
{
    for (uint32_t s = n / 2; s-- > 0;) sift(l, s, n);
    for (uint32_t e = n; e-- > 1;) {
        uint32_t t = l->order[0];
        l->order[0] = l->order[e];
        l->order[e] = t;
        sift(l, 0, e);
    }
}

/* simplify (wav2zxva.py): can segment i..j replace the breakpoints between? */
static bool seg_ok(const bp_t *B, uint32_t i, uint32_t j, uint64_t tol)
{
    uint64_t d = B[j].t - B[i].t;
    if (d > (1u << 22)) return false;
    if (j == i + 1) return true;
    const uint64_t M = 2 * US * 65536;
    uint32_t nat = (uint32_t) frac_q(((uint64_t) B[i].f + B[j].f) * d, M, 32);
    int32_t corr = (int32_t) (B[j].p - B[i].p - nat);
    int64_t dF = (int64_t) B[j].f - (int64_t) B[i].f;
    int64_t tol20 = (int64_t) (tol >> 10);
    u128 tol2 = mul64((uint64_t) tol20, (uint64_t) tol20);
    for (uint32_t m = i + 1; m < j; m++) {
        uint64_t tau = B[m].t - B[i].t;
        uint32_t ph = B[i].p + (uint32_t) frac_q((uint64_t) B[i].f * tau, US * 65536, 32);
        /* dF tau^2 / (2 d) / 2^16 / 1e6 turns, Q32: dF tau^2 2^15 / (d 1e6) */
        u128 q = mul64(uabs(dF) * tau, tau);
        q = shl128(q, 15);
        uint32_t chirp = (uint32_t) div128(q, d * US, 0).lo;
        ph += dF < 0 ? (uint32_t) 0 - chirp : chirp;
        ph += (uint32_t) smuldiv(corr, tau, d);
        int32_t dp = (int32_t) (ph - B[m].p);
        int64_t am = B[m].a;
        int64_t a_ = (int64_t) B[i].a + smuldiv((int64_t) B[j].a - (int64_t) B[i].a, tau, d);
        int64_t ea = (a_ - am) >> 10; /* Q20 */
        int64_t ep =
            sar(sar((int64_t) dp * (am >> 10), 32) * 6746518852LL, 30); /* 2 pi dp am, Q20 */
        u128 e2 = add128(mul64(uabs(ea), uabs(ea)), mul64(uabs(ep), uabs(ep)));
        if (cmp128(e2, tol2) > 0) return false;
    }
    return true;
}
static uint32_t simplify(const bp_t *B, uint32_t nb, bp_t *out, uint64_t tol)
{
    uint32_t no = 0, i = 0, last = nb - 1;
    out[no++] = B[0];
    while (i < last) {
        uint32_t step = 1, good = i + 1;
        while (good + step <= last && seg_ok(B, i, good + step, tol)) {
            good += step;
            step *= 2;
        }
        uint32_t lo = good, hi = good + step < last ? good + step : last;
        while (hi - lo > 1) {
            uint32_t mid = (lo + hi) / 2;
            if (seg_ok(B, i, mid, tol))
                lo = mid;
            else
                hi = mid;
        }
        out[no++] = B[lo];
        i = lo;
    }
    return no;
}

static void write_partial(an_t *A, const bp_t *B, uint32_t nb, uint32_t ch, uint32_t pan)
{
    wr_t *w = &A->out;
    if (nb > 65535) nb = 65535;
    uint32_t fa = B[0].f > FMAX ? FMAX : B[0].f, ca = level_code(B[0].a);
    uint32_t ph0 = ((B[0].p + (1u << 15)) >> 16) & 0xFFFFu;
    w8(w, ZXVA_PARTIAL);
    w8(w, ch);
    wuv(w, pan);
    w8(w, 1);
    wuv(w, B[0].t);
    wuv(w, nb);
    wuv(w, fa);
    wuv(w, ca);
    w8(w, ph0 & 0xFFu);
    w8(w, ph0 >> 8);
    uint64_t phi = (uint64_t) ph0 << 48;
    uint32_t ta = B[0].t;
    const uint64_t M = 2 * US * 65536;
    for (uint32_t i = 1; i < nb; i++) {
        uint32_t fb = B[i].f > FMAX ? FMAX : B[i].f, cb = level_code(B[i].a);
        uint32_t d = B[i].t - ta;
        wuv(w, d);
        wzz(w, (int64_t) fb - fa);
        wzz(w, (int64_t) cb - ca);
        uint64_t nat = frac_q(((uint64_t) fa + fb) * d, M, 64);
        uint64_t pred = phi + nat;
        uint64_t tgt = (uint64_t) B[i].p << 32;
        int64_t diff = (int64_t) (tgt - pred);
        int64_t dph = sar(diff + ((int64_t) 1 << 47), 48);
        if (dph > 32767) dph = 32767;
        if (dph < -32768) dph = -32768;
        wzz(w, dph);
        phi = pred + ((uint64_t) dph << 48);
        ta = B[i].t;
        fa = fb;
        ca = cb;
    }
    A->rep.nbp += nb;
}

static void place(uint32_t nch, uint32_t c, uint32_t *ch, uint32_t *pan)
{
    if (nch == 1) {
        *ch = 0;
        *pan = 0;
    } else if (nch == 2) {
        *ch = 0;
        *pan = c == 0 ? 0 : 65536;
    } else {
        *ch = c;
        *pan = 0;
    }
}

/* frame index -> breakpoint (time rounded to whole us, phase moved with it) */
static bp_t mkbp(const geo_t *g, uint64_t k, uint32_t f, uint32_t a, uint32_t p)
{
    bp_t b;
    uint64_t num = k * g->hop * US;
    uint64_t tu = (2 * num + g->rate) / (2 * g->rate);
    int64_t diff = (int64_t) (tu * g->rate) - (int64_t) num; /* |diff| <= rate / 2 */
    b.t = (uint32_t) tu;
    b.f = f;
    b.a = a;
    b.p = p + (uint32_t) smuldiv(diff * 65536, f, US * g->rate);
    return b;
}

static bool transient_test(uint64_t S, uint64_t ls, uint64_t Lg, uint64_t ll)
{
    /* short > 10 long  and  short > 1e-6 (energy Q38: 1e-6 = 274877.9) */
    if (cmp128(mul64(S, ll), mul64(10 * Lg, ls)) <= 0) return false;
    return cmp128(mul64(S, 10), mul64(2748779, ls)) > 0;
}

static int render_now(an_t *A, uint32_t layers, uint32_t bits, uint32_t flags);

/* ---- the chirp refit (wav2zxva.py chirp_refit) -------------------------
 * Every frame's amplitudes and phases are refitted with each partial as a
 * linear chirp across the window (slope from its track's neighbours), by
 * weighted least squares (periodic Hann weights, samples outside the signal
 * weighted out). wav2zxva.py solves all partials of a frame jointly; here a
 * block Gauss-Seidel sweep (each partial's 2x2 fit against the residual of
 * the others, REFIT_SWEEPS times) converges to the same solution for
 * partials that the window separates. */
#define REFIT_SWEEPS 3
static int32_t fsin(const int32_t *t, uint64_t ph) /* sin(2 pi ph / 2^64), Q30, table */
{
    uint32_t q = (uint32_t) (ph >> 62);
    uint64_t r = ph & (((uint64_t) 1 << 62) - 1);
    if (q & 1u) r = ((uint64_t) 1 << 62) - r;
    uint32_t i = (uint32_t) (r >> 52), fr = (uint32_t) ((r >> 32) & 0xFFFFFu);
    int64_t v = i >= 1024 ? t[1024] : t[i] + (((int64_t) t[i + 1] - t[i]) * fr >> 20);
    return (int32_t) (q >= 2 ? -v : v);
}
/* solve [g11 g12; g12 g22] c = [b1 b2] with basis Q15 and data Q23: c Q30 */
static void solve2(int64_t g11, int64_t g12, int64_t g22, int64_t b1, int64_t b2, int64_t *c1,
                   int64_t *c2)
{
    *c1 = *c2 = 0;
    int sh = 0;
    while ((uint64_t) (g11 | g22) >> sh >= (1u << 30) || uabs(g12) >> sh >= (1u << 30)) sh++;
    int64_t G11 = g11 >> sh, G22 = g22 >> sh, G12 = sar(g12, sh);
    int64_t det = G11 * G22 - G12 * G12;
    if (det <= 0) return;
    int bs = 0;
    while (uabs(b1) >> bs >= (1u << 30) || uabs(b2) >> bs >= (1u << 30)) bs++;
    int64_t B1 = sar(b1, bs), B2 = sar(b2, bs);
    int64_t n1 = G22 * B1 - G12 * B2, n2 = G11 * B2 - G12 * B1;
    int e = 22 + bs - sh;
    if (e >= 0) {
        if (e > 40) e = 40;
        *c1 = smuldiv(n1, (uint64_t) 1 << e, (uint64_t) det);
        *c2 = smuldiv(n2, (uint64_t) 1 << e, (uint64_t) det);
    } else {
        int f = -e > 20 ? 20 : -e;
        *c1 = smuldiv(n1, 1, (uint64_t) det << f);
        *c2 = smuldiv(n2, 1, (uint64_t) det << f);
    }
}

static void chirp_refit(an_t *A, uint32_t nk)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    uint32_t n = g->n;
    for (uint32_t k = 0; k <= g->nfr; k++) l->bstart[k] = 0;
    for (uint32_t q = 0; q < nk; q++)
        for (int32_t i = l->trk[l->order[q]].head; i >= 0; i = l->pool[i].nxt)
            l->bstart[l->pool[i].k + 1]++;
    for (uint32_t k = 0; k < g->nfr; k++) l->bstart[k + 1] += l->bstart[k];
    for (uint32_t k = 0; k <= g->nfr; k++) l->bfill[k] = l->bstart[k];
    for (uint32_t q = 0; q < nk; q++) {
        uint32_t m = 0;
        for (int32_t i = l->trk[l->order[q]].head; i >= 0; i = l->pool[i].nxt)
            l->gather[m++] = (uint32_t) i;
        for (uint32_t j = 0; j < m; j++) {
            const pk_t *pp = &l->pool[l->gather[j > 0 ? j - 1 : 0]];
            const pk_t *pn = &l->pool[l->gather[j + 1 < m ? j + 1 : m - 1]];
            uint64_t dk = pn->k > pp->k ? pn->k - pp->k : 1;
            /* C = 0.5 slope / rate^2 turns per sample^2, Q64:
             * slope = df / (dk hop / rate) -> C = df_q16 2^47 / (rate dk hop) */
            int64_t C = smuldiv((int64_t) pn->f - (int64_t) pp->f, (uint64_t) 1 << 47,
                                (uint64_t) g->rate * dk * g->hop);
            uint32_t e = l->bfill[l->pool[l->gather[j]].k]++;
            l->ent_pk[e] = l->gather[j];
            l->ent_C[e] = C;
        }
    }
    for (uint32_t k = 0; k < g->nfr; k++) {
        uint32_t e0 = l->bstart[k], e1 = l->bstart[k + 1];
        if (e0 == e1) continue;
        int64_t c0 = (int64_t) k * g->hop - n / 2;
        for (uint32_t i = 0; i < n; i++) {
            int64_t idx = c0 + i;
            bool ok = idx >= 0 && (uint64_t) idx < g->frames;
            l->wy[i] = ok ? l->hwin[i] >> 15 : 0; /* Q15 */
            l->ry[i] = ok ? l->xq[idx] : 0;
        }
        int64_t *ca = l->ca, *cb = l->cb;
        uint32_t m = e1 - e0; /* <= the frame's peaks <= maxpk */
        for (uint32_t j = 0; j < m; j++) ca[j] = cb[j] = 0;
        for (int sw = 0; sw < REFIT_SWEEPS; sw++) {
            for (uint32_t j = 0; j < m; j++) {
                const pk_t *pp = &l->pool[l->ent_pk[e0 + j]];
                uint64_t B = div128(shl128(u128of(pp->f), 48), g->rate, 0).lo; /* f / rate, Q64 */
                uint64_t C = (uint64_t) l->ent_C[e0 + j];
                int64_t i0 = -(int64_t) (n / 2);
                uint64_t ph = B * (uint64_t) i0 + C * (uint64_t) (i0 * i0);
                uint64_t inc = B + C * (uint64_t) (2 * i0 + 1);
                int64_t g11 = 0, g12 = 0, g22 = 0, b1 = 0, b2 = 0;
                for (uint32_t i = 0; i < n; i++) {
                    int32_t sv = fsin(l->sintab, ph),
                            cv = fsin(l->sintab, ph + ((uint64_t) 1 << 62));
                    l->cs[i] = sv;
                    l->cc[i] = cv;
                    ph += inc;
                    inc += 2 * C;
                    int64_t w = l->wy[i];
                    if (!w) continue;
                    int64_t us = (w * (sv >> 15)) >> 15, uc = (w * (cv >> 15)) >> 15;
                    int64_t wr = (w * l->ry[i]) >> 15;
                    g11 += us * us;
                    g12 += us * uc;
                    g22 += uc * uc;
                    b1 += wr * us;
                    b2 += wr * uc;
                }
                int64_t d1, d2;
                solve2(g11, g12, g22, b1, b2, &d1, &d2);
                ca[j] += d1;
                cb[j] += d2;
                for (uint32_t i = 0; i < n; i++)
                    if (l->wy[i]) l->ry[i] -= sar(d1 * l->cs[i] + d2 * l->cc[i], 37);
            }
        }
        for (uint32_t j = 0; j < m; j++) {
            pk_t *pp = &l->pool[l->ent_pk[e0 + j]];
            u128 a2 = add128(mul64(uabs(ca[j]), uabs(ca[j])), mul64(uabs(cb[j]), uabs(cb[j])));
            uint64_t amp = isqrt128(a2);
            pp->a = amp > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) amp;
            pp->p = atan2_q32(cb[j], ca[j]);
        }
    }
}

/* ---- onset sharpening (wav2zxva.py sharpen_onsets) ---------------------
 * At each onset, every partial is scaled by one shared gain curve g(t) on a
 * 0.5 ms grid, the least-squares gain of the partial model against the
 * source over 2 ms (0..1.5), so the model's short-time energy follows a
 * fast attack the analysis hop cannot resolve. The model is the kernel
 * renderer's own output of the unsimplified partials. */
static uint32_t detect_onsets(an_t *A, const int32_t *x, uint32_t *out, uint32_t cap)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    uint64_t len = g->frames, s = g->s_tr, nm = (len + s - 1) / s, acc = 0;
    l->esum[0] = 0;
    for (uint64_t m = 1; m <= nm; m++) {
        uint64_t e0 = (m - 1) * s, e1 = m * s < len ? m * s : len;
        for (uint64_t i = e0; i < e1; i++) {
            int64_t v = x[i] >> 4;
            acc += (uint64_t) (v * v);
        }
        l->esum[m] = acc;
    }
    uint32_t no = 0;
    int64_t last = -1000000000;
    for (uint64_t n = 0; n < len; n += s) {
        int64_t a = (int64_t) n, b = (int64_t) (n + 2 * s);
        if (b > (int64_t) len) b = (int64_t) len;
        uint64_t ma = (uint64_t) a / s, mb = (uint64_t) b >= len ? nm : (uint64_t) b / s;
        uint64_t S = l->esum[mb] - l->esum[ma], ls = (uint64_t) (b - a);
        int64_t la = (int64_t) n - 20 * (int64_t) s, lb = (int64_t) n;
        if (la < 0) la = 0;
        uint64_t Lg = l->esum[(uint64_t) lb / s] - l->esum[(uint64_t) la / s];
        uint64_t ll = (uint64_t) (lb - la) ? (uint64_t) (lb - la) : 1;
        if (!ls || !transient_test(S, ls, Lg, ll)) continue;
        if ((int64_t) n - last <= 10 * (int64_t) s) continue;
        last = (int64_t) n;
        if (no < cap) out[no++] = (uint32_t) n;
    }
    return no;
}

/* gain at t (Q16): 1 outside onset regions or across gaps > 600 us */
static uint32_t gain_at(const lay_t *l, uint32_t ng, uint32_t t)
{
    uint32_t lo = 0, hi = ng; /* first index with gt >= t */
    while (lo < hi) {
        uint32_t mid = (lo + hi) / 2;
        if (l->gt[mid] < t)
            lo = mid + 1;
        else
            hi = mid;
    }
    uint32_t i = lo;
    if (i < ng && l->gt[i] == t) return l->gg[i];
    if (i == 0 || i == ng || l->gt[i] - l->gt[i - 1] > 600) return 65536;
    uint64_t f = ((uint64_t) (t - l->gt[i - 1]) << 16) / (l->gt[i] - l->gt[i - 1]);
    return (uint32_t) (((uint64_t) l->gg[i - 1] * (65536 - f) + (uint64_t) l->gg[i] * f) >> 16);
}

static uint32_t sharpen_grid(an_t *A, uint32_t c, const int32_t *x)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    uint32_t no = detect_onsets(A, x, l->onset, l->oncap), ng = 0;
    uint64_t step = g->s_tr, len = g->frames;
    for (uint32_t o = 0; o < no; o++) {
        uint64_t on = l->onset[o];
        uint64_t a = on > 8 * step ? on - 8 * step : 0,
                 b = on + 60 * step < len ? on + 60 * step : len;
        for (uint64_t cc = a; cc < b; cc += step) {
            uint64_t lo = cc > 2 * step ? cc - 2 * step : 0,
                     hi = cc + 2 * step < len ? cc + 2 * step : len;
            int64_t xm = 0;
            uint64_t em = 0;
            for (uint64_t i = lo; i < hi; i++) {
                int64_t m = sar(sar((int64_t) l->rend[i * g->nch + c] + 128, 8), 4),
                        xv = sar(x[i], 4);
                em += (uint64_t) (m * m);
                xm += xv * m;
            }
            uint32_t gq = 65536;
            if (em > 0) { /* em > 1e-12 in Q38 units is em >= 1 */
                int64_t q = smuldiv(xm, 65536, em);
                gq = q < 0 ? 0 : q > 98304 ? 98304 : (uint32_t) q;
            }
            uint32_t t = (uint32_t) ((2 * cc * US + g->rate) / (2 * g->rate));
            /* later onsets overwrite the same instant (a dict in wav2zxva.py) */
            bool done = false;
            for (uint32_t j = ng; j-- > 0 && j + 200 >= ng;)
                if (l->gt[j] == t) {
                    l->gg[j] = gq;
                    done = true;
                    break;
                }
            if (!done && ng < l->gridcap) {
                l->gt[ng] = t;
                l->gg[ng] = gq;
                ng++;
            }
        }
    }
    /* sort by time (insertion: nearly sorted already) */
    for (uint32_t i = 1; i < ng; i++) {
        uint32_t t = l->gt[i], v = l->gg[i], j = i;
        while (j > 0 && l->gt[j - 1] > t) {
            l->gt[j] = l->gt[j - 1];
            l->gg[j] = l->gg[j - 1];
            j--;
        }
        l->gt[j] = t;
        l->gg[j] = v;
    }
    return ng;
}

/* the renderer's model inside segment a..b at time t (us): f, a, phase */
static bp_t interp_bp(const bp_t *a, const bp_t *b, uint32_t t)
{
    bp_t r;
    uint64_t d = b->t - a->t, tau = t - a->t;
    const uint64_t M = 2 * US * 65536;
    uint32_t nat = (uint32_t) frac_q(((uint64_t) a->f + b->f) * d, M, 32);
    int32_t corr = (int32_t) (b->p - a->p - nat);
    int64_t dF = (int64_t) b->f - (int64_t) a->f;
    uint32_t ph = a->p + (uint32_t) frac_q((uint64_t) a->f * tau, US * 65536, 32);
    u128 q = shl128(mul64(uabs(dF) * tau, tau), 15);
    uint32_t chirp = (uint32_t) div128(q, d * US, 0).lo;
    ph += dF < 0 ? (uint32_t) 0 - chirp : chirp;
    ph += (uint32_t) smuldiv(corr, tau, d);
    r.t = t;
    r.f = (uint32_t) ((int64_t) a->f + smuldiv(dF, tau, d));
    r.a = (uint32_t) ((int64_t) a->a + smuldiv((int64_t) b->a - (int64_t) a->a, tau, d));
    r.p = ph;
    return r;
}

static int partials(an_t *A, uint32_t c)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    uint32_t ntr = track(A), nk = 0;
    for (uint32_t t = 0; t < ntr; t++)
        if (keep_track(A, &l->trk[t])) l->order[nk++] = t;
    chirp_refit(A, nk);
    for (uint32_t q = 0; q < nk; q++) {
        uint64_t e = 0;
        for (int32_t i = l->trk[l->order[q]].head; i >= 0; i = l->pool[i].nxt) {
            uint64_t a = l->pool[i].a >> 10;
            e += a * a;
        }
        l->trk[l->order[q]].energy = e;
    }
    sort_tracks(l, nk);
    if (nk > g->maxtr_ch) nk = g->maxtr_ch;
    uint64_t aref = 1;
    for (uint32_t q = 0; q < nk; q++)
        for (int32_t i = l->trk[l->order[q]].head; i >= 0; i = l->pool[i].nxt)
            if (l->pool[i].a > aref) aref = l->pool[i].a;
    uint64_t tol = aref / 1000; /* -60 dB */
    uint32_t ch, pan;
    place(g->nch, c, &ch, &pan);
    /* unsimplified breakpoints of every kept track */
    uint32_t at = 0;
    for (uint32_t q = 0; q < nk; q++) {
        const tr_t *t = &l->trk[l->order[q]];
        const pk_t *h = &l->pool[t->head], *e = &l->pool[t->tail];
        bp_t *bp = l->bpall + at;
        uint32_t nb = 0;
        if (h->k > 0)
            bp[nb++] = mkbp(g, h->k - 1, h->f, 0, h->p - adv_q32(2ull * h->f, g->hop, g->rate));
        for (int32_t i = t->head; i >= 0; i = l->pool[i].nxt) {
            const pk_t *pp = &l->pool[i];
            bp[nb++] = mkbp(g, pp->k, pp->f, pp->a, pp->p);
        }
        if ((uint64_t) (e->k + 1) * g->hop * US <= (uint64_t) g->dur_us * g->rate) {
            bp[nb++] = mkbp(g, e->k + 1, e->f, 0, e->p + adv_q32(2ull * e->f, g->hop, g->rate));
        } else if (g->dur_us >= bp[nb - 1].t + 100) {
            bp_t b = bp[nb - 1];
            uint32_t dt = g->dur_us - b.t;
            b.p += adv_q32(2ull * e->f, dt, US);
            b.t = g->dur_us;
            b.f = e->f;
            b.a = e->a;
            bp[nb++] = b;
        }
        /* a breakpoint past the end (frame times round up) is clamped out */
        while (nb > 1 && bp[nb - 1].t > g->dur_us) nb--;
        l->bpoff[q] = at;
        l->bpn[q] = nb;
        at += nb;
    }
    /* model of these partials by the renderer (other channels' partials
     * land on other outputs), then the shared gain grid at onsets */
    uint64_t mark = A->out.n;
    uint32_t ng = 0, nbp_keep = A->rep.nbp;
    if (nk) {
        for (uint32_t q = 0; q < nk; q++)
            if (l->bpn[q] >= 2) write_partial(A, l->bpall + l->bpoff[q], l->bpn[q], ch, pan);
        if (A->out.bad) return ZXVA_AN_EOUT;
        int rc = render_now(A, ZXVA_L_PARTIAL, 32, 0);
        if (rc < 0) return rc;
        A->out.n = mark;
        A->rep.nbp = nbp_keep;
        ng = sharpen_grid(A, c, l->xq);
    }
    for (uint32_t q = 0; q < nk; q++) {
        const bp_t *src = l->bpall + l->bpoff[q];
        uint32_t nb = l->bpn[q], nm = 0;
        if (nb < 2) continue;
        uint32_t t0 = src[0].t, t1 = src[nb - 1].t, j = 0;
        /* merge the grid instants inside the track */
        for (uint32_t i = 0; i < nb; i++) l->bp[i] = src[i];
        nm = nb;
        for (uint32_t gi = 0; gi < ng && nm < l->bpcap; gi++) {
            uint32_t t = l->gt[gi];
            if (!(t0 < t && t < t1)) continue;
            while (j + 1 < nm && l->bp[j + 1].t <= t) j++;
            if (t - l->bp[j].t < 100 || l->bp[j + 1].t - t < 100) continue;
            bp_t ins = interp_bp(&l->bp[j], &l->bp[j + 1], t);
            for (uint32_t m = nm; m > j + 1; m--) l->bp[m] = l->bp[m - 1];
            l->bp[j + 1] = ins;
            nm++;
            j++;
        }
        if (ng)
            for (uint32_t i = 0; i < nm; i++)
                l->bp[i].a =
                    (uint32_t) (((uint64_t) l->bp[i].a * gain_at(l, ng, l->bp[i].t)) >> 16);
        uint32_t ns = simplify(l->bp, nm, l->bp2, tol);
        if (ns >= 2) {
            write_partial(A, l->bp2, ns, ch, pan);
            A->rep.ntracks++;
        }
    }
    return 0;
}

/* ---- rendering helpers ------------------------------------------------- */
/* render the file built so far (+ END) into rend (32-bit) with `layers` */
static int render_buf(an_t *A, const uint8_t *d, uint32_t n, uint32_t layers, uint32_t bits,
                      uint32_t flags)
{
    int rc = zxva_open(A->l.zw, d, n, A->g.rate, bits, layers | flags);
    if (rc < 0) return ZXVA_AN_EVERIFY;
    uint64_t F = A->g.frames, nf = zxva_frames(A->l.zw);
    if (nf > F) nf = F;
    for (uint64_t f = 0; f < nf; f += 4096) {
        uint32_t cnt = nf - f < 4096 ? (uint32_t) (nf - f) : 4096;
        if (zxva_render(A->l.zw, f, A->l.rend + f * A->g.nch, cnt) < 0) return ZXVA_AN_EVERIFY;
    }
    for (uint64_t f = nf; f < F; f++)
        for (uint32_t c = 0; c < A->g.nch; c++) A->l.rend[f * A->g.nch + c] = 0;
    return 0;
}
static int render_now(an_t *A, uint32_t layers, uint32_t bits, uint32_t flags)
{
    wr_t *w = &A->out;
    if (w->n >= w->cap) return ZXVA_AN_EOUT;
    w->p[w->n] = ZXVA_END;
    return render_buf(A, w->p, (uint32_t) (w->n + 1), layers, bits, flags);
}

/* ---- 3. grains --------------------------------------------------------- */
typedef struct {
    uint32_t f, dur, half; /* Q16 Hz, us, us */
} gpar_t;

/* basis s, c (Q30) for samples 0..L-1 of a grain (wav2zxva.py grain_env) */
static void grain_basis(an_t *A, uint32_t L, gpar_t gp)
{
    const uint32_t att = 50, rate = A->g.rate;
    uint32_t fl = gp.dur / 8;
    int32_t *S = A->l.gbase, *C = A->l.gbase + L;
    for (uint32_t i = 0; i < L; i++) {
        uint64_t tn = (uint64_t) i * US; /* t = tn / rate us */
        int64_t env = ONE30;
        if (tn >= (uint64_t) gp.dur * rate) {
            S[i] = C[i] = 0;
            continue;
        }
        if (tn < (uint64_t) att * rate) {
            int64_t s = zxva_sin_q30(muldiv(tn, (uint64_t) 1 << 62, (uint64_t) att * rate));
            env = sar(s * s, 30);
        }
        if (gp.half && tn > (uint64_t) att * rate) {
            uint64_t e =
                muldiv(tn - (uint64_t) att * rate, (uint64_t) 1 << 32, (uint64_t) rate * gp.half);
            env = sar(env * zxva_exp2_q30(-(int64_t) e), 30);
        }
        if (fl && tn > (uint64_t) (gp.dur - fl) * rate) {
            uint64_t num = (uint64_t) gp.dur * rate - tn;
            int64_t s = zxva_sin_q30(muldiv(num, (uint64_t) 1 << 62, (uint64_t) fl * rate));
            env = sar(env * sar(s * s, 30), 30);
        }
        uint64_t ph = frac_q((uint64_t) gp.f * i, (uint64_t) rate << 16, 64);
        S[i] = (int32_t) sar(env * zxva_sin_q30(ph), 30);
        C[i] = (int32_t) sar(env * cos_q30(ph), 30);
    }
}

/* least squares of seg on (s, c): coefficients Q30, residual energy (Q46) */
static void grain_lsq(an_t *A, const int32_t *seg, uint32_t L, int64_t *c1, int64_t *c2,
                      uint64_t *res)
{
    const int32_t *S = A->l.gbase, *C = A->l.gbase + L;
    int64_t g11 = 0, g12 = 0, g22 = 0, b1 = 0, b2 = 0;
    uint64_t E = 0;
    for (uint32_t i = 0; i < L; i++) {
        int64_t s = S[i] >> 15, c = C[i] >> 15, y = seg[i];
        g11 += s * s;
        g12 += s * c;
        g22 += c * c;
        b1 += y * s;
        b2 += y * c;
        E += (uint64_t) (y * y);
    }
    /* det = g11 g22 - g12^2 >= 0 (Cauchy-Schwarz); solve in scaled 64-bit */
    int sh = 0;
    while ((uint64_t) (g11 | g22) >> sh >= (1u << 30) || uabs(g12) >> sh >= (1u << 30)) sh++;
    int64_t G11 = g11 >> sh, G22 = g22 >> sh, G12 = sar(g12, sh);
    int64_t det = G11 * G22 - G12 * G12;
    *c1 = *c2 = 0;
    *res = E;
    if (det <= 0) return;
    int bs = 0;
    while (uabs(b1) >> bs >= (1u << 30) || uabs(b2) >> bs >= (1u << 30)) bs++;
    int64_t B1 = sar(b1, bs), B2 = sar(b2, bs);
    int64_t n1 = G22 * B1 - G12 * B2, n2 = G11 * B2 - G12 * B1; /* < 2^61 */
    /* c (Q8 per unit) = n / det * 2^(bs - sh); Q30: * 2^22 */
    int e1 = 22 + bs - sh;
    int64_t q1, q2;
    if (e1 >= 0) {
        q1 = smuldiv(n1, (uint64_t) 1 << (e1 > 40 ? 40 : e1), (uint64_t) det);
        q2 = smuldiv(n2, (uint64_t) 1 << (e1 > 40 ? 40 : e1), (uint64_t) det);
    } else {
        q1 = smuldiv(n1, 1, (uint64_t) det << (-e1 > 20 ? 20 : -e1));
        q2 = smuldiv(n2, 1, (uint64_t) det << (-e1 > 20 ? 20 : -e1));
    }
    *c1 = q1;
    *c2 = q2;
    /* residual of the quantised coefficients, exactly */
    uint64_t r = 0;
    for (uint32_t i = 0; i < L; i++) {
        int64_t m = sar(q1 * S[i] + q2 * C[i], 37); /* Q23 */
        int64_t d = seg[i] - m;
        r += (uint64_t) (d * d);
    }
    *res = r;
}

/* the segment's spectral centroid and peak, Q16 Hz */
static void grain_spectrum(an_t *A, const int32_t *seg, uint32_t L, uint32_t *cen, uint32_t *pk)
{
    uint32_t M = 1;
    int lgM = 0;
    while (M < 4 * L) {
        M <<= 1;
        lgM++;
    }
    int32_t *xr = A->l.gfft, *xi = A->l.gfft + M;
    for (uint32_t j = 0; j < M; j++) {
        int32_t v = j < L ? zk_windowed(seg[j], hann_sym(j, L)) : 0;
        uint32_t r = zk_bitrev(j, (uint32_t) lgM);
        xr[r] = v;
        xi[r] = 0;
    }
    for (int s = 1; s <= lgM; s++) {
        uint32_t h = 1u << (s - 1);
        for (uint32_t b = 0; b < M / 2; b++) {
            uint32_t i0 = (b / h) * 2 * h + b % h, i1 = i0 + h;
            uint64_t ph = muldiv((uint64_t) (b % h), (uint64_t) 1 << 63, h); /* (b%h)/(2h) turn */
            zi c = cos_q30(ph), sn = zxva_sin_q30(ph);
            zi vr = zk_twmul_re(xr[i1], xi[i1], c, sn), vi = zk_twmul_im(xr[i1], xi[i1], c, sn);
            zi ar = xr[i0], ai = xi[i0];
            xr[i0] = zk_half_sum(ar, vr);
            xi[i0] = zk_half_sum(ai, vi);
            xr[i1] = zk_half_diff(ar, vr);
            xi[i1] = zk_half_diff(ai, vi);
        }
    }
    u128 num = {0, 0}, den = {0, 0};
    uint64_t best = 0;
    uint32_t bi = 0;
    for (uint32_t j = 0; j <= M / 2; j++) {
        uint64_t p = (uint64_t) ((int64_t) xr[j] * xr[j] + (int64_t) xi[j] * xi[j]);
        num = add128(num, mul64(p, j));
        den = add128(den, u128of(p));
        if (p > best) {
            best = p;
            bi = j;
        }
    }
    *pk = (uint32_t) ((uint64_t) bi * A->g.rate * 65536 / M);
    if (!den.hi && !den.lo) {
        *cen = 0;
        return;
    }
    int s = 0;
    while (shr128(den, s).hi || shr128(den, s).lo >= ((uint64_t) 1 << 40)) s++;
    uint64_t d = shr128(den, s).lo;
    u128 n2 = shr128(num, s); /* < 2^56 */
    u128 t = mul64(n2.lo, (uint64_t) A->g.rate * 65536);
    *cen = (uint32_t) div128(div128(t, d, 0), M, 0).lo;
}

static int grains(an_t *A, uint32_t c)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    const int32_t *x = l->rbuf;
    int32_t *model = l->model;
    uint64_t len = g->frames, s = g->s_tr;
    for (uint64_t i = 0; i < len; i++) model[i] = 0;
    /* prefix energies at multiples of s, the last at len */
    uint64_t nm = (len + s - 1) / s, acc = 0;
    l->esum[0] = 0;
    for (uint64_t m = 1; m <= nm; m++) {
        uint64_t e0 = (m - 1) * s, e1 = m * s < len ? m * s : len;
        for (uint64_t i = e0; i < e1; i++) {
            int64_t v = x[i] >> 4;
            acc += (uint64_t) (v * v);
        }
        l->esum[m] = acc;
    }
    uint32_t ch, pan;
    place(g->nch, c, &ch, &pan);
    int64_t last = -1000000000;
    for (uint64_t n = 0; n < len; n += s) {
        int64_t a = (int64_t) n, b = (int64_t) (n + 2 * s);
        if (b > (int64_t) len) b = (int64_t) len;
        uint64_t ma = (uint64_t) a / s, mb = (uint64_t) b >= len ? nm : (uint64_t) b / s;
        uint64_t S = l->esum[mb] - l->esum[ma], ls = (uint64_t) (b - a) ? (uint64_t) (b - a) : 1;
        int64_t la = (int64_t) n - 20 * (int64_t) s, lb = (int64_t) n;
        if (la < 0) la = 0;
        uint64_t Lg = l->esum[(uint64_t) lb / s] - l->esum[(uint64_t) la / s];
        uint64_t ll = (uint64_t) (lb - la) ? (uint64_t) (lb - la) : 1;
        if (!transient_test(S, ls, Lg, ll)) continue;
        int64_t p = (int64_t) n;
        if (p - last <= 10 * (int64_t) s) continue;
        last = p;
        /* fit a tone grain at this onset */
        uint64_t ga = p > (int64_t) (g->rate / 2000) ? (uint64_t) p - g->rate / 2000 : 0;
        uint64_t gb = (uint64_t) p + g->rate / 50 < len ? (uint64_t) p + g->rate / 50 : len;
        if (gb - ga < 16) continue;
        int64_t mx = 0;
        for (uint64_t i = ga; i < gb; i++) {
            int64_t v = (int64_t) x[i] - model[i];
            mx = uabs(v) > (uint64_t) mx ? (int64_t) uabs(v) : mx;
        }
        uint64_t first = 0;
        for (uint64_t i = ga; i < gb; i++) {
            int64_t v = (int64_t) x[i] - model[i];
            if ((int64_t) uabs(v) * 10 > mx) {
                first = i - ga;
                break;
            }
        }
        ga += first > 2 ? first - 2 : 0;
        uint32_t L = (uint32_t) (gb - ga);
        if (L < 16) continue;
        /* segment kept in the second half of gbase's room: use rend scratch */
        int32_t *sg = l->xq; /* xq is free here (partials are done) */
        uint64_t energy = 0;
        for (uint32_t i = 0; i < L; i++) {
            sg[i] = x[ga + i] - model[ga + i];
            energy += (uint64_t) ((int64_t) sg[i] * sg[i]);
        }
        uint32_t cen, pk;
        grain_spectrum(A, sg, L, &cen, &pk);
        uint32_t fs[5] = {cen, pk, cen / 2, (uint32_t) ((uint64_t) cen * 3 / 4),
                          (uint32_t) ((uint64_t) cen * 3 / 2)};
        bool have = false;
        uint64_t bres = 0;
        gpar_t bp = {0, 0, 0};
        int64_t bc1 = 0, bc2 = 0;
        static const uint32_t DURS[4] = {1000, 3000, 8000, 20000}, HALFS[4] = {100, 400, 1500, 0};
        uint32_t maxdur = (uint32_t) ((uint64_t) L * US / g->rate);
        for (int fi = 0; fi < 5; fi++) {
            bool dup = false;
            for (int q = 0; q < fi; q++) dup |= fs[q] == fs[fi];
            if (dup) continue;
            uint64_t f = fs[fi];
            if (f <= (20u << 16) || f * 100 >= (uint64_t) g->rate * 45 * 65536) continue;
            for (int di = 0; di < 4; di++) {
                uint32_t dur = DURS[di] < maxdur ? DURS[di] : maxdur;
                if (dur < 100) continue;
                for (int hi = 0; hi < 4; hi++) {
                    gpar_t gp = {(uint32_t) f, dur, HALFS[hi]};
                    grain_basis(A, L, gp);
                    int64_t c1, c2;
                    uint64_t r;
                    grain_lsq(A, sg, L, &c1, &c2, &r);
                    if (!have || r < bres) {
                        have = true;
                        bres = r;
                        bp = gp;
                        bc1 = c1;
                        bc2 = c2;
                    }
                }
            }
        }
        if (!have || bres * 10 > energy * 8) continue; /* must remove > 1 dB */
        u128 a2 = add128(mul64(uabs(bc1), uabs(bc1)), mul64(uabs(bc2), uabs(bc2)));
        uint64_t amp = isqrt128(a2);
        if (amp > (uint64_t) ONE30) continue;
        uint32_t t0 = (uint32_t) ((2 * ga * US + g->rate) / (2 * g->rate));
        if ((uint64_t) t0 + bp.dur > g->dur_us) continue;
        if (A->ngrain >= MAX_GRAINS) break;
        uint32_t ph = atan2_q32(bc2, bc1);
        grain_t *gr = &l->grains[A->ngrain++];
        gr->t0 = t0;
        gr->ch = ch;
        gr->pan = pan;
        gr->dur = bp.dur;
        gr->half = bp.half;
        gr->f = bp.f > FMAX ? FMAX : bp.f;
        gr->code = level_code(amp);
        gr->ph = ((ph + (1u << 15)) >> 16) & 0xFFFFu;
        grain_basis(A, L, bp);
        for (uint32_t i = 0; i < L; i++)
            model[ga + i] += (int32_t) sar(bc1 * l->gbase[i] + bc2 * l->gbase[L + i], 37);
        A->rep.ngrains++;
    }
    return 0;
}

static void write_grains(an_t *A)
{
    lay_t *l = &A->l;
    uint32_t n = A->ngrain;
    if (!n) return;
    /* stable insertion sort by start (wav2zxva.py sorts the collected list) */
    for (uint32_t i = 1; i < n; i++) {
        grain_t t = l->grains[i];
        uint32_t j = i;
        while (j > 0 && l->grains[j - 1].t0 > t.t0) {
            l->grains[j] = l->grains[j - 1];
            j--;
        }
        l->grains[j] = t;
    }
    wr_t *w = &A->out;
    w8(w, ZXVA_GRAINS);
    wuv(w, n);
    uint32_t prev = 0;
    for (uint32_t i = 0; i < n; i++) {
        const grain_t *g = &l->grains[i];
        wuv(w, g->t0 - prev);
        w8(w, 0);
        w8(w, g->ch);
        wuv(w, g->pan);
        wuv(w, g->dur);
        wuv(w, 50);
        wuv(w, g->half);
        wuv(w, g->f);
        wuv(w, 0);
        wuv(w, g->code);
        w8(w, g->ph & 0xFFu);
        w8(w, g->ph >> 8);
        prev = g->t0;
    }
}

/* ---- 4. noise ---------------------------------------------------------- */
/* band powers of frames centred at c0 + k num / den over x, mags in rbuf */
static int band_frames(an_t *A, const int32_t *x, uint32_t num, uint32_t den, int64_t c0,
                       uint32_t nfr, u128 *bands /* nfr x 24, or NULL */, uint32_t *codes,
                       int32_t *cells)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    uint32_t n = g->nn, H = n / 2 + 1, mw = zxva_mask_words(n);
    zxva_stft_plan_t p = {n, n, g->nL, num, den, c0, l->nwin, l->ntw, x, g->frames, 0xFFFFFFFFu, 1};
    /* sw2 = sum w^2 (Q60) >> 30 */
    uint64_t sw2 = 0;
    for (uint32_t j = 0; j < n; j++) sw2 += (uint64_t) ((int64_t) l->nwin[j] * l->nwin[j]) >> 30;
    (void) bands;
    for (uint64_t k0 = 0; k0 < nfr; k0 += g->batch) {
        uint32_t nf = nfr - k0 < g->batch ? (uint32_t) (nfr - k0) : g->batch;
        if (A->be->stft(A->be, &p, k0, nf, l->re, l->im, l->mag, l->mask, l->top) < 0)
            return ZXVA_AN_EBACK;
        A->rep.stft_frames += nf;
        for (uint32_t f = 0; f < nf; f++) {
            const uint32_t *M = l->mag + (size_t) f * H;
            for (uint32_t b = 0; b < NB_NOISE; b++) {
                u128 S = {0, 0};
                for (uint32_t j = 0; j < H; j++) {
                    uint64_t fr = (uint64_t) j * g->rate; /* fr = j rate / n */
                    if (fr >= (uint64_t) g->edges[b] * n && fr < (uint64_t) g->edges[b + 1] * n)
                        S = add128(S, mul64(M[j], M[j]));
                }
                if (codes) {
                    /* a = sqrt(16 n S 2^30 / sw2), Q30 */
                    u128 m16 = add128(mul64(S.lo, 16ull * n), shl128(u128of(S.hi * 16ull * n), 64));
                    u128 q = div128(shl128(m16, 30), sw2 ? sw2 : 1, 0);
                    uint64_t a = isqrt128(q);
                    codes[(k0 + f) * NB_NOISE + b] = a > 32212 ? level_code(a) : 0;
                }
                if (cells) cells[(k0 + f) * NB_NOISE + b] = cdb10(log2_128(add128(S, u128of(1))));
            }
        }
    }
    (void) mw;
    return 0;
}

static int noise(an_t *A, uint32_t c)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    /* a NOISE chunk needs two 10 ms frames (zxva.c v_noise): shorter sounds
     * have no noise layer */
    if (g->nfr_noise < 2) return 0;
    /* what partials and grains leave */
    for (uint64_t i = 0; i < g->frames; i++) l->rbuf[i] -= l->model[i];
    uint32_t *codes = (uint32_t *) l->xq; /* free scratch: nfr_noise x 24 u32 */
    int rc = band_frames(A, l->rbuf, g->rate, 100, 0, g->nfr_noise, 0, codes, 0);
    if (rc < 0) return rc;
    bool any = false;
    for (uint64_t i = 0; i < (uint64_t) g->nfr_noise * NB_NOISE; i++) any |= codes[i] != 0;
    if (!any) return 0;
    uint32_t ch, pan;
    place(g->nch, c, &ch, &pan);
    wr_t *w = &A->out;
    w8(w, ZXVA_NOISE);
    w8(w, ch);
    wuv(w, pan);
    wuv(w, 0);
    wuv(w, 10000);
    wuv(w, g->nfr_noise);
    w8(w, NB_NOISE);
    for (uint32_t b = 0; b <= NB_NOISE; b++) wuv(w, g->edges[b]);
    for (uint32_t k = 0; k < g->nfr_noise; k++)
        for (uint32_t b = 0; b < NB_NOISE; b++) {
            uint32_t v = codes[k * NB_NOISE + b];
            if (k == 0)
                wuv(w, v);
            else
                wzz(w, (int64_t) v - codes[(k - 1) * NB_NOISE + b]);
        }
    A->rep.nnoise++;
    return 0;
}

/* ---- 5. residual ------------------------------------------------------- */
typedef struct {
    uint8_t *p;
    uint32_t n, cap;
    uint64_t acc;
    int nb;
} bw_t;
static void bput(bw_t *b, uint64_t v, int n) /* n <= 40 */
{
    for (int i = n - 1; i >= 0; i--) {
        b->acc = (b->acc << 1) | ((v >> i) & 1u);
        if (++b->nb == 8) {
            if (b->n < b->cap) b->p[b->n] = (uint8_t) b->acc;
            b->n++;
            b->nb = 0;
            b->acc = 0;
        }
    }
}
static uint64_t zzu(int64_t e)
{
    return e < 0 ? ((uint64_t) (-(e + 1)) << 1) | 1u : (uint64_t) e << 1;
}
static int64_t pred_err(const int32_t *x, uint32_t i, uint32_t order, uint32_t st)
{
    uint32_t o = i < order ? i : order;
    int64_t s = x[(size_t) i * st], p = 0;
    if (o == 1)
        p = x[(size_t) (i - 1) * st];
    else if (o == 2)
        p = 2 * (int64_t) x[(size_t) (i - 1) * st] - x[(size_t) (i - 2) * st];
    else if (o == 3)
        p = 3 * (int64_t) x[(size_t) (i - 1) * st] - 3 * (int64_t) x[(size_t) (i - 2) * st] +
            x[(size_t) (i - 3) * st];
    return s - p;
}
/* encode one block-channel exactly as tools/zxva/zxvafmt.py encode_block */
static uint32_t encode_block(const int32_t *x, uint32_t cnt, uint32_t st, uint8_t *out,
                             uint32_t cap)
{
    uint64_t bestc = UINT64_MAX;
    uint32_t bo = 0, bk = 0;
    for (uint32_t order = 0; order < 4; order++) {
        uint64_t sum = 0;
        for (uint32_t i = 0; i < cnt; i++) sum += zzu(pred_err(x, i, order, st));
        uint32_t k0 = 0;
        if (sum) { /* floor(log2(mean + 1)): 2^k cnt <= sum + cnt */
            while (k0 < 40 && cmp128(mul64((uint64_t) 1 << (k0 + 1), cnt), u128of(sum + cnt)) <= 0)
                k0++;
        }
        uint32_t ka = k0 >= 2 ? k0 - 2 : 0, kb = k0 + 2 > 40 ? 40 : k0 + 2;
        for (uint32_t k = ka; k <= kb; k++) {
            uint64_t c = 0;
            for (uint32_t i = 0; i < cnt; i++) {
                uint64_t u = zzu(pred_err(x, i, order, st));
                c += (u >> k) >= 32 ? 72 : (u >> k) + 1 + k;
            }
            if (c < bestc) {
                bestc = c;
                bo = order;
                bk = k;
            }
        }
    }
    bw_t b = {out, 0, cap, 0, 0};
    bput(&b, bo, 2);
    bput(&b, bk, 6);
    for (uint32_t i = 0; i < cnt; i++) {
        uint64_t u = zzu(pred_err(x, i, bo, st)), q = u >> bk;
        if (q >= 32) {
            bput(&b, 0xFFFFFFFFu, 32);
            bput(&b, u, 40);
        } else {
            for (uint64_t j = 0; j < q; j++) bput(&b, 1, 1);
            bput(&b, 0, 1);
            if (bk) bput(&b, u, (int) bk);
        }
    }
    if (b.nb) bput(&b, 0, 8 - b.nb);
    return b.n <= cap ? b.n : 0;
}

/* ---- metrics ----------------------------------------------------------- */
static int32_t snr_cdb(an_t *A)
{
    u128 s = {0, 0}, e = {0, 0};
    const zxva_pcm_t *p = A->pcm;
    int sh = 24 - (int) p->bits;
    for (uint64_t i = 0; i < p->frames * p->nch; i++) {
        int64_t v = p->pcm[i], src = sh >= 0 ? v * ((int64_t) 1 << sh) : sar(v, -sh);
        int64_t r = sar(A->l.rend[i], 8);
        s = add128(s, mul64(uabs(src), uabs(src)));
        e = add128(e, mul64(uabs(src - r), uabs(src - r)));
    }
    if (!e.hi && !e.lo) return 2000000;
    if (!s.hi && !s.lo) return -2000000;
    return cdb10(log2_128(s) - log2_128(e));
}

/* fidelity of d (a complete file) against the source: SNR of the tonal
 * layers and of all layers, and the band error. With `write_end`, d[n - 1]
 * is set to END first (the file being built). rend keeps the full render. */
static int measure(an_t *A, uint8_t *dw, uint32_t n, bool write_end)
{
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    int rc;
    if (write_end) dw[n - 1] = ZXVA_END;
    const uint8_t *d = dw;
    /* measure: tonal layers, then all layers (rend keeps the full render) */
    if ((rc = render_buf(A, d, n, ZXVA_L_PARTIAL | ZXVA_L_GRAIN, 32, 0)) < 0) return rc;
    A->rep.snr_tonal_cdb = snr_cdb(A);
    if ((rc = render_buf(A, d, n, ZXVA_L_ALL, 32, 0)) < 0) return rc;
    A->rep.snr_cdb = snr_cdb(A);
    /* band error: 10 ms cells x 24 ERB bands, cells within 60 dB of the
     * loudest reference cell, RMS of the level difference */
    {
        uint64_t cnt = 0, sq = 0;
        int32_t rmax = INT32_MIN;
        uint32_t nc = g->ncells;
        for (uint32_t c = 0; c < g->nch && nc; c++) {
            load_q23(A, c);
            for (uint64_t i = 0; i < g->frames; i++)
                l->rbuf[i] = (int32_t) sar((int64_t) l->rend[i * g->nch + c] + 128, 8);
            if ((rc = band_frames(A, l->xq, g->rate / 100, 1, g->nn / 2, nc, 0, 0,
                                  l->cellR + (size_t) c * nc * NB_NOISE)) < 0)
                return rc;
            if ((rc = band_frames(A, l->rbuf, g->rate / 100, 1, g->nn / 2, nc, 0, 0,
                                  l->cellX + (size_t) c * nc * NB_NOISE)) < 0)
                return rc;
        }
        uint64_t tot = (uint64_t) nc * g->nch * NB_NOISE;
        for (uint64_t i = 0; i < tot; i++) rmax = l->cellR[i] > rmax ? l->cellR[i] : rmax;
        for (uint64_t i = 0; i < tot; i++) {
            if (l->cellR[i] <= rmax - 6000) continue;
            int64_t d = (int64_t) l->cellR[i] - l->cellX[i];
            sq += (uint64_t) (d * d);
            cnt++;
        }
        A->rep.band_err_cdb = cnt ? (uint32_t) isqrt64(sq / cnt) : 0;
    }
    return 0;
}

/* ---- top level --------------------------------------------------------- */
static int setup(an_t *A, const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg, void *work,
                 uint64_t work_size)
{
    zxva_an_report_t z = {0};
    A->rep = z;
    int rc = geometry(pcm, cfg, &A->g);
    if (rc < 0) return rc;
    if (!work) return ZXVA_AN_EARG;
    A->pcm = pcm;
    A->cfg = cfg;
    A->ngrain = 0;
    uintptr_t wp = ((uintptr_t) work + 15) & ~(uintptr_t) 15;
    uint64_t skip = wp - (uintptr_t) work;
    if (work_size < skip) return ZXVA_AN_EWORK;
    arena_t ar = {(uint8_t *) wp, 0, work_size - skip, false, false};
    layout(&ar, &A->g, &A->l);
    if (ar.bad || ar.used > ar.cap) return ZXVA_AN_EWORK;
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    zxva_backend_cpu(&A->cpu);
    A->be = cfg->backend ? cfg->backend : &A->cpu;
    A->be->scratch = l->scratch;
    A->be->scratch_words = 2 * g->N;
    zxva_tables_bh(l->win, g->n);
    zxva_tables_tw(l->tw, g->N);
    zxva_tables_hann(l->nwin, g->nn);
    zxva_tables_tw(l->ntw, g->nn);
    lobe_table(l->lobe);
    zxva_tables_hann(l->hwin, g->n);
    for (uint32_t i = 0; i <= 1024; i++) l->sintab[i] = zxva_sin_q30((uint64_t) i << 52);
    A->sumw = 0;
    l->wpre[0] = 0;
    for (uint32_t j = 0; j < g->n; j++) {
        A->sumw += (uint64_t) l->win[j];
        l->wpre[j + 1] = A->sumw;
    }
    return 0;
}

int zxva_measure(const zxva_pcm_t *pcm, const uint8_t *d, uint32_t n, void *work,
                 uint64_t work_size, zxva_an_report_t *rep)
{
    an_t A0, *A = &A0;
    zxva_an_cfg_t cfg = {0};
    int rc = setup(A, pcm, &cfg, work, work_size);
    if (rc < 0) return rc;
    zxva_info_t in;
    if (!d || zxva_check(d, n, &in) != 0 || in.nch != pcm->nch) return ZXVA_AN_EARG;
    if ((rc = measure(A, (uint8_t *) (uintptr_t) d, n, false)) < 0) return rc;
    if (rep) *rep = A->rep;
    return 0;
}

int64_t zxva_analyse(const zxva_pcm_t *pcm, const zxva_an_cfg_t *cfg, void *work,
                     uint64_t work_size, uint8_t *out, uint64_t out_cap, zxva_an_report_t *rep)
{
    an_t A0, *A = &A0;
    if (!out) return ZXVA_AN_EARG;
    int rc = setup(A, pcm, cfg, work, work_size);
    if (rc < 0) return rc;
    const geo_t *g = &A->g;
    lay_t *l = &A->l;
    if (out_cap > 0xFFFFFFF0u) out_cap = 0xFFFFFFF0u; /* zxva files are < 4 GB */
    A->out.p = out;
    A->out.n = 0;
    A->out.cap = out_cap;
    A->out.bad = false;
    /* header */
    wr_t *w = &A->out;
    w8(w, 'Z');
    w8(w, 'X');
    w8(w, 'V');
    w8(w, 'A');
    w8(w, 1);
    w8(w, 0);
    wuv(w, g->nch);
    wuv(w, g->dur_us);
    wuv(w, SEED);
    uint64_t hdr_end = w->n;
    /* 1-2. partials, channel by channel */
    for (uint32_t c = 0; c < g->nch; c++) {
        load_q23(A, c);
        if ((rc = run_peaks(A)) < 0) return rc;
        if ((rc = partials(A, c)) < 0) return rc;
        if (w->bad) return ZXVA_AN_EOUT;
    }
    /* 3-4. grains and noise on what the partials leave */
    if (!cfg->no_grains || !cfg->no_noise) {
        if ((rc = render_now(A, ZXVA_L_ALL, 32, 0)) < 0) return rc;
        for (uint32_t c = 0; c < g->nch; c++) {
            load_q23(A, c);
            for (uint64_t i = 0; i < g->frames; i++)
                l->rbuf[i] = l->xq[i] - (int32_t) sar((int64_t) l->rend[i * g->nch + c] + 128, 8);
            for (uint64_t i = 0; i < g->frames; i++) l->model[i] = 0;
            if (!cfg->no_grains && (rc = grains(A, c)) < 0) return rc;
            if (!cfg->no_noise && (rc = noise(A, c)) < 0) return rc;
            if (w->bad) return ZXVA_AN_EOUT;
        }
    }
    write_grains(A);
    if (w->bad) return ZXVA_AN_EOUT;
    uint64_t param_end = w->n;
    A->rep.param_bytes = (uint32_t) param_end + 1;
    if (w->n >= w->cap) return ZXVA_AN_EOUT;
    if ((rc = measure(A, out, (uint32_t) w->n + 1, true)) < 0) return rc;
    /* The residual is int32 and the renderer clamps after adding it: when the
     * parametric render strays more than 2^31 from a 32-bit source (full
     * scale noise, square waves) no residual can repair it, so the layers are
     * dropped and the residual carries the whole signal. Rare; reported by
     * ntracks = ngrains = nnoise = 0 and the SNR of an empty render. */
    if (cfg->residual && g->rbits == 32) {
        if ((rc = render_now(A, ZXVA_L_ALL, 32, 0)) < 0) return rc;
        bool over = false;
        for (uint64_t i = 0; i < g->frames * g->nch && !over; i++) {
            int64_t dlt = (int64_t) pcm->pcm[i] - l->rend[i];
            over = dlt > INT32_MAX || dlt < INT32_MIN;
        }
        if (over) {
            w->n = hdr_end;
            A->rep.ntracks = A->rep.nbp = A->rep.ngrains = A->rep.nnoise = 0;
            A->rep.param_bytes = (uint32_t) hdr_end + 1;
            if ((rc = measure(A, out, (uint32_t) w->n + 1, true)) < 0) return rc;
        }
    }
    /* 6. META */
    {
        uint8_t cid[ZXVA_CID_LEN];
        if (cfg->cid) {
            for (int i = 0; i < ZXVA_CID_LEN; i++) cid[i] = cfg->cid[i];
        } else {
            /* the PCM bytes, int32 little endian, hashed in place on LE hosts
             * would be host-dependent: hash them byte by byte */
            sha256_ctx_t hc;
            sha256_init(&hc);
            uint64_t tot = pcm->frames * pcm->nch;
            for (uint64_t i = 0; i < tot; i++) {
                uint32_t v = (uint32_t) pcm->pcm[i];
                uint8_t b4[4] = {(uint8_t) v, (uint8_t) (v >> 8), (uint8_t) (v >> 16),
                                 (uint8_t) (v >> 24)};
                sha256_update(&hc, b4, 4);
            }
            cid[0] = 0x01;
            cid[1] = 0x55;
            cid[2] = 0x12;
            cid[3] = 0x20;
            sha256_final(&hc, cid + 4);
        }
        uint8_t body[96];
        wr_t mb = {body, 0, sizeof body, false};
        w8(&mb, 1);
        for (int i = 0; i < ZXVA_CID_LEN; i++) w8(&mb, cid[i]);
        wuv(&mb, cfg->src_bytes ? cfg->src_bytes : pcm->frames * pcm->nch * 4);
        wuv(&mb, pcm->rate);
        w8(&mb, cfg->src_bits ? cfg->src_bits : pcm->bits);
        w8(&mb, cfg->src_kind <= ZXVA_SRC_OTHER ? cfg->src_kind : ZXVA_SRC_OTHER);
        w8(&mb, ZXVA_AN_KERNEL);
        wzz(&mb, A->rep.snr_cdb);
        wuv(&mb, A->rep.band_err_cdb);
        uint64_t exact_at = mb.n;
        w8(&mb, cfg->residual ? ZXVA_EXACT_UNVERIFIED : ZXVA_EXACT_NONE);
        w8(w, ZXVA_META);
        wuv(w, mb.n);
        uint64_t meta_exact = w->n + exact_at;
        for (uint64_t i = 0; i < mb.n; i++) w8(w, body[i]);
        /* 5. residual */
        if (cfg->residual) {
            uint32_t rb = g->rbits;
            if ((rc = render_now(A, ZXVA_L_ALL, rb, 0)) < 0) return rc;
            int sh = (int) rb - (int) g->bits;
            uint64_t F = g->frames;
            for (uint64_t i = 0; i < F * g->nch; i++)
                l->rend[i] = (int32_t) (((int64_t) pcm->pcm[i] * ((int64_t) 1 << sh)) - l->rend[i]);
            w8(w, ZXVA_RESIDUAL);
            wuv(w, g->rate);
            w8(w, rb);
            wuv(w, F);
            for (uint64_t b0 = 0; b0 < F; b0 += ZXVA_RES_BLOCK) {
                uint32_t cnt = F - b0 < ZXVA_RES_BLOCK ? (uint32_t) (F - b0) : ZXVA_RES_BLOCK;
                for (uint32_t c = 0; c < g->nch; c++) {
                    uint32_t nb =
                        encode_block(l->rend + b0 * g->nch + c, cnt, g->nch, l->rtmp, RES_TMP);
                    if (!nb) return ZXVA_AN_EVERIFY;
                    wuv(w, nb);
                    for (uint32_t i = 0; i < nb; i++) w8(w, l->rtmp[i]);
                }
            }
            if (w->bad) return ZXVA_AN_EOUT;
            /* verify: the file reproduces the source exactly */
            if ((rc = render_now(A, ZXVA_L_ALL, rb, ZXVA_F_RESIDUAL)) < 0) return rc;
            bool same = true;
            for (uint64_t i = 0; i < F * g->nch && same; i++)
                same = l->rend[i] == (int32_t) ((int64_t) pcm->pcm[i] * ((int64_t) 1 << sh));
            if (!same) return ZXVA_AN_EVERIFY;
            A->rep.exact = true;
            A->rep.res_bits = rb;
            out[meta_exact] = ZXVA_EXACT_VERIFIED;
        }
    }
    w8(w, ZXVA_END);
    if (w->bad) return ZXVA_AN_EOUT;
    if (zxva_check(out, (uint32_t) w->n, 0) != 0) return ZXVA_AN_EVERIFY;
    A->rep.total_bytes = (uint32_t) w->n;
    if (rep) *rep = A->rep;
    return (int64_t) w->n;
}
