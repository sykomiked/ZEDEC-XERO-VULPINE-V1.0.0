/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt.c — fixed point, golden quantisation, Fibonacci-tiled products and the
 * integer nonlinearities. See zt.h T1-T6. */
#include "zt.h"

#define PHI_Q32    6949403065ull /* phi * 2^32 */
#define INVPHI_Q16 40503         /* 1/phi * 2^16 */
#define LOG2E_Q16  94548         /* log2(e) * 2^16 */
#define TILE       21u           /* F(8) */

uint64_t zt_udiv64(uint64_t n, uint64_t d, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    if (d == 0) {
        if (rem) *rem = 0;
        return 0;
    }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) {
            r -= d;
            q |= (uint64_t) 1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}

static int64_t sdiv64(int64_t n, int64_t d)
{
    bool neg = (n < 0) != (d < 0);
    uint64_t un = n < 0 ? (uint64_t) 0 - (uint64_t) n : (uint64_t) n;
    uint64_t ud = d < 0 ? (uint64_t) 0 - (uint64_t) d : (uint64_t) d;
    uint64_t q = zt_udiv64(un, ud, 0);
    return neg ? -(int64_t) q : (int64_t) q;
}

static uint64_t fib(uint32_t n)
{
    uint64_t a = 0, b = 1;
    for (uint32_t i = 0; i < n; i++) {
        uint64_t t = a + b;
        a = b;
        b = t;
    }
    return a;
}

/* T3: phi^k = F(k) phi + F(k-1), and phi^-n = (-1)^n (F(n+1) - F(n) phi).
 * Worked in Q32 and rounded to Q16. */
int64_t zt_phi_pow(int32_t k)
{
    if (k < -24 || k > 40) return 0;
    uint64_t q32;
    if (k >= 0) {
        uint64_t fk = fib((uint32_t) k), fk1 = k ? fib((uint32_t) k - 1) : 1;
        q32 = fk * PHI_Q32 + (fk1 << 32);
        if (k == 0) q32 = (uint64_t) 1 << 32;
    } else {
        uint32_t n = (uint32_t) -k;
        int64_t v = (int64_t) (fib(n + 1) << 32) - (int64_t) (fib(n) * PHI_Q32);
        if (n & 1u) v = -v;
        q32 = (uint64_t) v;
    }
    return (int64_t) ((q32 + 0x8000u) >> 16);
}

static int32_t iabs32(int32_t v)
{
    return v < 0 ? -v : v;
}

void zt_quantize(const zt_fx *x, uint32_t n, zt_q8_t *out, bool golden)
{
    for (uint32_t b = 0; b < n / ZT_BLOCK; b++) {
        const zt_fx *v = x + b * ZT_BLOCK;
        zt_q8_t *o = &out[b];
        int32_t amax = 0;
        for (uint32_t i = 0; i < ZT_BLOCK; i++)
            if (iabs32(v[i]) > amax) amax = iabs32(v[i]);
        int32_t scale = (amax + 126) / 127; /* ceil: never clips */
        o->phi_k = 0;
        o->shift = 0;
        if (scale == 0) {
            o->scale = 0;
            for (uint32_t i = 0; i < ZT_BLOCK; i++) o->q[i] = 0;
            continue;
        }
        if (golden) {
            int32_t k = -24;
            while (k < 21 && zt_phi_pow(k) < scale) k++;
            o->phi_k = (int8_t) k;
            scale = (int32_t) zt_phi_pow(k);
        }
        o->scale = scale;
        for (uint32_t i = 0; i < ZT_BLOCK; i++) {
            int32_t a = iabs32(v[i]);
            int32_t q = (a + scale / 2) / scale;
            if (q > 127) q = 127;
            o->q[i] = (int8_t) (v[i] < 0 ? -q : q);
        }
    }
}

/* x / 2^sh, rounded half away from zero. */
static zt_fx shr_round(int64_t x, uint32_t sh)
{
    if (sh == 0) return (zt_fx) x;
    uint64_t m = (uint64_t) (x < 0 ? -x : x);
    m = (m + (1ull << (sh - 1))) >> sh;
    return (zt_fx) (x < 0 ? -(int64_t) m : (int64_t) m);
}

void zt_dequantize(const zt_q8_t *in, uint32_t nblocks, zt_fx *out)
{
    for (uint32_t b = 0; b < nblocks; b++)
        for (uint32_t i = 0; i < ZT_BLOCK; i++)
            out[b * ZT_BLOCK + i] = shr_round((int64_t) in[b].q[i] * in[b].scale, in[b].shift);
}

static zt_fx sat32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (zt_fx) v;
}

/* value = sum(qa qb) * sa * sb / 2^16, split so nothing overflows. */
zt_fx zt_dot(const zt_q8_t *a, const zt_q8_t *b, uint32_t nblocks)
{
    int64_t acc = 0;
    for (uint32_t k = 0; k < nblocks; k++) {
        int32_t s = 0;
        for (uint32_t i = 0; i < ZT_BLOCK; i++) s += (int32_t) a[k].q[i] * b[k].q[i];
        bool neg = s < 0;
        uint64_t p = (uint64_t) (neg ? -s : s) * (uint32_t) a[k].scale;
        uint64_t sb = (uint32_t) b[k].scale;
        uint32_t sh = 16u + a[k].shift + b[k].shift;
        /* floor(p * sb / 2^sh), p < 2^52 and sb < 2^31, without 128 bits */
        uint64_t hi = (p >> 32) * sb, lo = (p & 0xFFFFFFFFu) * sb, t;
        if (sh >= 96)
            t = 0;
        else if (sh >= 32)
            t = (hi + (lo >> 32)) >> (sh - 32);
        else
            t = (hi << (32 - sh)) + (lo >> sh);
        acc += neg ? -(int64_t) t : (int64_t) t;
    }
    return sat32(acc);
}

void zt_matvec(const zt_q8_t *w, uint32_t rows, const zt_q8_t *x, uint32_t nblocks, zt_fx *y)
{
    for (uint32_t r = 0; r < rows; r++) y[r] = zt_dot(w + (uint64_t) r * nblocks, x, nblocks);
}

/* T4: split the longer side at 1/phi until a tile fits in TILE x TILE. */
static void mm_rec(const zt_q8_t *a, uint32_t i0, uint32_t m, const zt_q8_t *b, uint32_t j0,
                   uint32_t n, uint32_t ldc, uint32_t nb, zt_fx *c)
{
    if (m <= TILE && n <= TILE) {
        for (uint32_t i = i0; i < i0 + m; i++)
            for (uint32_t j = j0; j < j0 + n; j++)
                c[(uint64_t) i * ldc + j] =
                    zt_dot(a + (uint64_t) i * nb, b + (uint64_t) j * nb, nb);
        return;
    }
    if (m >= n) {
        uint32_t h = (uint32_t) (((uint64_t) m * INVPHI_Q16) >> 16);
        if (h == 0) h = 1;
        if (h >= m) h = m - 1;
        mm_rec(a, i0, h, b, j0, n, ldc, nb, c);
        mm_rec(a, i0 + h, m - h, b, j0, n, ldc, nb, c);
    } else {
        uint32_t h = (uint32_t) (((uint64_t) n * INVPHI_Q16) >> 16);
        if (h == 0) h = 1;
        if (h >= n) h = n - 1;
        mm_rec(a, i0, m, b, j0, h, ldc, nb, c);
        mm_rec(a, i0, m, b, j0 + h, n - h, ldc, nb, c);
    }
}

void zt_matmul_fib(const zt_q8_t *a, uint32_t m, const zt_q8_t *b, uint32_t n, uint32_t nblocks,
                   zt_fx *c)
{
    if (m && n) mm_rec(a, 0, m, b, 0, n, n, nblocks, c);
}

uint32_t zt_isqrt64(uint64_t v)
{
    uint64_t r = 0, bit = (uint64_t) 1 << 62;
    while (bit > v) bit >>= 2;
    while (bit) {
        if (v >= r + bit) {
            v -= r + bit;
            r = (r >> 1) + bit;
        } else {
            r >>= 1;
        }
        bit >>= 2;
    }
    return (uint32_t) r;
}

void zt_rmsnorm(const zt_fx *x, const zt_fx *gain, uint32_t n, zt_fx *y)
{
    uint64_t ss = 0;
    for (uint32_t i = 0; i < n; i++) ss += ((uint64_t) ((int64_t) x[i] * x[i])) >> 16;
    uint64_t mean = n ? zt_udiv64(ss, n, 0) : 0; /* Q16 */
    uint32_t rms = zt_isqrt64(mean << 16);       /* Q16 */
    if (rms == 0) rms = 1;
    uint64_t inv = zt_udiv64((uint64_t) 1 << 32, rms, 0); /* Q16 of 1/rms */
    for (uint32_t i = 0; i < n; i++) {
        int64_t t = ((int64_t) x[i] * (int64_t) inv) >> 16;
        y[i] = sat32((t * gain[i]) >> 16);
    }
}

/* round(2^(i/256) * 65536), i = 0..256 */
static const uint32_t EXP2[257] = {
    65536,  65714,  65892,  66071,  66250,  66429,  66609,  66790,  66971,  67153,  67335,  67517,
    67700,  67884,  68068,  68252,  68438,  68623,  68809,  68996,  69183,  69370,  69558,  69747,
    69936,  70126,  70316,  70507,  70698,  70889,  71082,  71274,  71468,  71661,  71856,  72050,
    72246,  72442,  72638,  72835,  73032,  73230,  73429,  73628,  73828,  74028,  74229,  74430,
    74632,  74834,  75037,  75240,  75444,  75649,  75854,  76060,  76266,  76473,  76680,  76888,
    77096,  77305,  77515,  77725,  77936,  78147,  78359,  78572,  78785,  78998,  79212,  79427,
    79642,  79858,  80075,  80292,  80510,  80728,  80947,  81166,  81386,  81607,  81828,  82050,
    82273,  82496,  82719,  82944,  83169,  83394,  83620,  83847,  84074,  84302,  84531,  84760,
    84990,  85220,  85451,  85683,  85915,  86148,  86382,  86616,  86851,  87086,  87322,  87559,
    87796,  88034,  88273,  88513,  88752,  88993,  89234,  89476,  89719,  89962,  90206,  90451,
    90696,  90942,  91188,  91436,  91684,  91932,  92181,  92431,  92682,  92933,  93185,  93438,
    93691,  93945,  94200,  94455,  94711,  94968,  95226,  95484,  95743,  96002,  96263,  96524,
    96785,  97048,  97311,  97575,  97839,  98104,  98370,  98637,  98905,  99173,  99442,  99711,
    99982,  100253, 100524, 100797, 101070, 101344, 101619, 101895, 102171, 102448, 102726, 103004,
    103283, 103564, 103844, 104126, 104408, 104691, 104975, 105260, 105545, 105831, 106118, 106406,
    106694, 106984, 107274, 107565, 107856, 108149, 108442, 108736, 109031, 109326, 109623, 109920,
    110218, 110517, 110816, 111117, 111418, 111720, 112023, 112327, 112631, 112937, 113243, 113550,
    113858, 114167, 114476, 114787, 115098, 115410, 115723, 116036, 116351, 116667, 116983, 117300,
    117618, 117937, 118257, 118577, 118899, 119221, 119544, 119869, 120194, 120519, 120846, 121174,
    121502, 121832, 122162, 122493, 122825, 123158, 123492, 123827, 124163, 124500, 124837, 125176,
    125515, 125855, 126197, 126539, 126882, 127226, 127571, 127917, 128263, 128611, 128960, 129310,
    129660, 130012, 130364, 130718, 131072,
};

/* 2^t for t <= 0 in Q16. */
static zt_fx exp2_neg(int64_t t)
{
    int64_t ip = t >> 16;                     /* floor */
    uint32_t f = (uint32_t) (t - ip * 65536); /* 0..65535 */
    uint32_t hi = f >> 8, lo = f & 0xFFu;
    uint32_t m = EXP2[hi] + (((EXP2[hi + 1] - EXP2[hi]) * lo + 128u) >> 8);
    int64_t sh = -ip;
    if (sh >= 32) return 0;
    if (sh <= 0) return (zt_fx) m;
    return (zt_fx) ((m + ((uint32_t) 1 << (sh - 1))) >> sh);
}

zt_fx zt_exp(zt_fx x)
{
    if (x > 0) x = 0;
    return exp2_neg(((int64_t) x * LOG2E_Q16) >> 16);
}

void zt_softmax(zt_fx *x, uint32_t n)
{
    if (!n) return;
    zt_fx mx = x[0];
    for (uint32_t i = 1; i < n; i++)
        if (x[i] > mx) mx = x[i];
    uint64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        int64_t d = (int64_t) x[i] - mx;
        if (d < INT32_MIN) d = INT32_MIN;
        x[i] = zt_exp((zt_fx) d);
        sum += (uint32_t) x[i];
    }
    for (uint32_t i = 0; i < n; i++)
        x[i] = (zt_fx) zt_udiv64((uint64_t) (uint32_t) x[i] << 16, sum, 0);
}

zt_fx zt_silu(zt_fx x)
{
    /* sigmoid(x) = 1 / (1 + e^-|x|) for x >= 0, and 1 - that for x < 0 */
    int64_t e = zt_exp(x < 0 ? x : -x);
    int64_t sig = (int64_t) zt_udiv64((uint64_t) 1 << 32, (uint64_t) (65536 + e), 0);
    if (x < 0) sig = 65536 - sig;
    return sat32(((int64_t) x * sig) >> 16);
}

/* T6: Knuth's multiplier 2^64 / phi. */
uint32_t zt_fib_hash(uint64_t key, uint32_t bits)
{
    if (bits == 0) return 0;
    if (bits > 32) bits = 32;
    return (uint32_t) ((key * 11400714819323198485ull) >> (64u - bits));
}

/* Exposed to zt_coil.c and zt_isf.c. */
int64_t zt__sdiv64(int64_t n, int64_t d)
{
    return sdiv64(n, d);
}

uint64_t zt__fib(uint32_t n)
{
    return fib(n);
}
