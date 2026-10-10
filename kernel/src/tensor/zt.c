/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zt.c — fixed point, golden quantisation, Fibonacci-tiled products and the
 * integer nonlinearities. See zt.h T1-T6. */
#include "zt.h"

#define INVPHI_Q64 11400714819323198485ull /* floor(2^64 / phi) = (phi - 1) * 2^64 */
#define INVPHI_Q16 40503                   /* 1/phi * 2^16 */
#define LOG2E_Q16  94548                   /* log2(e) * 2^16 */
#define TILE       21u                     /* F(8) */

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

/* T3: phi^k = F(k) phi + F(k-1) = F(k+1) + F(k)/phi for k > 0, and
 * phi^-n = (-1)^n (F(n+1) - F(n) phi) = (-1)^n (F(n-1) - F(n)/phi). 1/phi is
 * held to 64 fraction bits (INVPHI_Q64), so F(k)/phi is exact to far below
 * a Q16 unit for every k in range and the result is correctly rounded. */
int64_t zt_phi_pow(int32_t k)
{
    if (k < -24 || k > 40) return 0;
    if (k == 0) return ZT_ONE;
    if (k > 0) {
        /* round(F(k) * INVPHI_Q64 / 2^48), the product split in 32-bit halves */
        uint64_t f = fib((uint32_t) k);
        uint64_t hi = f * (INVPHI_Q64 >> 32), lo = f * (INVPHI_Q64 & 0xFFFFFFFFu);
        uint64_t frac = (hi + ((lo + (1ull << 47)) >> 32)) >> 16;
        return (int64_t) ((fib((uint32_t) k + 1) << 16) + frac);
    }
    /* |phi^-n| < 1, so its Q64 value is the low 64 bits of
     * +-(F(n-1) 2^64 - F(n) INVPHI_Q64): the high parts cancel. */
    uint32_t n = (uint32_t) -k;
    uint64_t xl = fib(n) * INVPHI_Q64; /* wraps mod 2^64 on purpose */
    uint64_t d = (n & 1u) ? xl : (uint64_t) 0 - xl;
    return (int64_t) ((d + (1ull << 47)) >> 48);
}

/* |v| without overflow: |INT32_MIN| = 2^31 fits in 32 unsigned bits. */
static uint32_t uabs32(int32_t v)
{
    return v < 0 ? (uint32_t) 0 - (uint32_t) v : (uint32_t) v;
}

void zt_quantize(const zt_fx *x, uint32_t n, zt_q8_t *out, bool golden)
{
    for (uint32_t b = 0; b < n / ZT_BLOCK; b++) {
        const zt_fx *v = x + b * ZT_BLOCK;
        zt_q8_t *o = &out[b];
        uint32_t amax = 0; /* at most 2^31 */
        for (uint32_t i = 0; i < ZT_BLOCK; i++)
            if (uabs32(v[i]) > amax) amax = uabs32(v[i]);
        uint32_t scale = (amax + 126u) / 127u; /* ceil: never clips */
        o->phi_k = 0;
        o->shift = 0;
        if (scale == 0) {
            o->scale = 0;
            for (uint32_t i = 0; i < ZT_BLOCK; i++) o->q[i] = 0;
            continue;
        }
        if (golden) {
            int32_t k = -24;
            while (k < 21 && zt_phi_pow(k) < (int64_t) scale) k++;
            o->phi_k = (int8_t) k;
            scale = (uint32_t) zt_phi_pow(k);
        }
        o->scale = (int32_t) scale;
        for (uint32_t i = 0; i < ZT_BLOCK; i++) {
            uint32_t q = (uabs32(v[i]) + scale / 2u) / scale;
            if (q > 127u) q = 127u;
            o->q[i] = (int8_t) (v[i] < 0 ? -(int32_t) q : (int32_t) q);
        }
    }
}

static zt_fx sat32(int64_t v)
{
    if (v > INT32_MAX) return INT32_MAX;
    if (v < INT32_MIN) return INT32_MIN;
    return (zt_fx) v;
}

/* x / 2^sh, rounded half away from zero, saturated to 32 bits. */
static zt_fx shr_round(int64_t x, uint32_t sh)
{
    if (sh >= 64) return 0;
    uint64_t m = x < 0 ? (uint64_t) 0 - (uint64_t) x : (uint64_t) x;
    if (sh) m = (m >> sh) + ((m >> (sh - 1)) & 1u);
    if (m > (uint64_t) INT32_MAX) return x < 0 ? INT32_MIN : INT32_MAX;
    return x < 0 ? -(zt_fx) m : (zt_fx) m;
}

void zt_dequantize(const zt_q8_t *in, uint32_t nblocks, zt_fx *out)
{
    for (uint32_t b = 0; b < nblocks; b++)
        for (uint32_t i = 0; i < ZT_BLOCK; i++)
            out[b * ZT_BLOCK + i] = shr_round((int64_t) in[b].q[i] * in[b].scale, in[b].shift);
}

#define DOT_BLOCK_CAP (1ll << 48) /* Q16: far past int32, so the cap only saturates */
#define DOT_ACC_CAP   (1ll << 62)

/* value = sum(qa qb) * sa * sb / 2^(16 + shifts), each block's term rounded
 * toward zero, split so nothing overflows. A block term beyond 2^48 (Q16) is
 * capped there; the sum saturates to int32 at the end. */
zt_fx zt_dot(const zt_q8_t *a, const zt_q8_t *b, uint32_t nblocks)
{
    int64_t acc = 0;
    for (uint32_t k = 0; k < nblocks; k++) {
        int32_t s = 0;
        for (uint32_t i = 0; i < ZT_BLOCK; i++) s += (int32_t) a[k].q[i] * b[k].q[i];
        bool neg = ((s < 0) != (a[k].scale < 0)) != (b[k].scale < 0);
        uint64_t p = (uint64_t) uabs32(s) * uabs32(a[k].scale); /* < 2^50 */
        uint64_t sb = uabs32(b[k].scale);                       /* <= 2^31 */
        uint32_t sh = 16u + a[k].shift + b[k].shift;
        /* floor(p * sb / 2^sh) without 128 bits: p sb = hi 2^32 + lo */
        uint64_t hi = (p >> 32) * sb, lo = (p & 0xFFFFFFFFu) * sb, t;
        if (sh >= 96)
            t = 0;
        else if (sh >= 32)
            t = (hi + (lo >> 32)) >> (sh - 32);
        else if (hi >> (sh + 16)) /* hi 2^(32 - sh) >= 2^48 */
            t = (uint64_t) DOT_BLOCK_CAP;
        else
            t = (hi << (32 - sh)) + (lo >> sh);
        if (t > (uint64_t) DOT_BLOCK_CAP) t = (uint64_t) DOT_BLOCK_CAP;
        acc += neg ? -(int64_t) t : (int64_t) t;
        if (acc > DOT_ACC_CAP) acc = DOT_ACC_CAP;
        if (acc < -DOT_ACC_CAP) acc = -DOT_ACC_CAP;
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

/* high 64 bits of a 64 x 64 product, from 32-bit limbs */
static uint64_t mulhi64(uint64_t a, uint64_t b)
{
    uint64_t a0 = (uint32_t) a, a1 = a >> 32, b0 = (uint32_t) b, b1 = b >> 32;
    uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    uint64_t mid = (p00 >> 32) + (uint32_t) p01 + (uint32_t) p10;
    return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

static uint32_t bitlen64(uint64_t v)
{
    uint32_t n = 0;
    while (v) n++, v >>= 1;
    return n;
}

/* floor(m / d) for d > 0 given inv = floor((2^64 - 1) / d): the high half of
 * m * inv is at most two short of the quotient. Sets *rem. */
static uint64_t div_by_inv(uint64_t m, uint64_t d, uint64_t inv, uint64_t *rem)
{
    uint64_t q = mulhi64(m, inv), r = m - q * d;
    while (r >= d) q++, r -= d;
    *rem = r;
    return q;
}

/* T5: the RMS of n values as R * 2^-F in Q16, with R < 2^31 and F <= 31
 * as large as fits, so the result keeps about 31 significant bits at every
 * scale. The squares are summed in full (Q32), pre-shifted only as far as
 * n * max^2 needs to fit 64 bits, and the sum is scaled up before the
 * division by n, so small activations keep their precision. */
static uint64_t rms_fixed(const zt_fx *x, uint32_t n, uint32_t *F)
{
    *F = 0;
    if (n == 0) return 0;
    uint32_t amax = 0;
    for (uint32_t i = 0; i < n; i++)
        if (uabs32(x[i]) > amax) amax = uabs32(x[i]);
    if (amax == 0) return 0;
    uint64_t top = (uint64_t) amax * amax; /* <= 2^62 */
    uint32_t need = bitlen64(top) + bitlen64(n), sh = need > 64 ? need - 64 : 0;
    uint64_t ss = 0; /* sum of squares, Q32, divided by 2^sh */
    for (uint32_t i = 0; i < n; i++) ss += ((uint64_t) uabs32(x[i]) * uabs32(x[i])) >> sh;
    /* M0 = floor(mean * 2^e), mean in Q32, from the most bits that fit */
    uint32_t L = 64 - bitlen64(ss);
    uint64_t M0 = zt_udiv64(ss << L, n, 0), M;
    int32_t e = (int32_t) L - (int32_t) sh;
    if (e <= 0) {
        M = M0 << -e; /* = floor(mean), <= max^2 < 2^62 */
    } else {
        /* M = M0 / 2^r: fewer than 63 bits, and e - r = 2F even, F <= 31 */
        int32_t r = bitlen64(M0) > 62 ? (int32_t) bitlen64(M0) - 62 : 0;
        if ((e - r) & 1) r++;
        if (e - r > 62) r = e - 62;
        M = M0 >> r;
        *F = (uint32_t) (e - r) / 2u;
    }
    uint64_t R = zt_isqrt64(M);
    if (M - R * R > R) R++; /* round: sqrt(M) >= R + 1/2 */
    return R;
}

/* RMS in Q16, rounded to nearest. Can exceed INT32_MAX (2^31 for
 * all-INT32_MIN input); callers saturate. Exposed to zt_lattice.c. */
uint32_t zt__rms_q16(const zt_fx *x, uint32_t n)
{
    uint32_t F;
    uint64_t R = rms_fixed(x, n, &F);
    return (uint32_t) (F ? (R + (1ull << (F - 1))) >> F : R);
}

/* y = x * gain / rms, rounded half away from zero, saturated. With
 * rms = R / 2^F (Q16) this is P 2^F / R for P = x * gain (Q32), done as
 * two exact divisions by R through one reciprocal. */
void zt_rmsnorm(const zt_fx *x, const zt_fx *gain, uint32_t n, zt_fx *y)
{
    uint32_t F;
    uint64_t R = rms_fixed(x, n, &F);
    if (R == 0) {
        for (uint32_t i = 0; i < n; i++) y[i] = 0;
        return;
    }
    uint64_t inv = zt_udiv64(~0ull, R, 0);
    for (uint32_t i = 0; i < n; i++) {
        int64_t P = (int64_t) x[i] * gain[i]; /* |P| <= 2^62 */
        uint64_t m = P < 0 ? (uint64_t) 0 - (uint64_t) P : (uint64_t) P, r1, r2;
        uint64_t q1 = div_by_inv(m, R, inv, &r1);
        int64_t v;
        if (q1 >> (40 - F)) {
            v = INT64_MAX; /* far past int32: saturates below */
        } else {
            uint64_t q2 = div_by_inv(r1 << F, R, inv, &r2); /* r1 < 2^31 */
            uint64_t q = (q1 << F) + q2 + (2 * r2 >= R);
            v = (int64_t) q;
        }
        y[i] = sat32(P < 0 ? -v : v);
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
