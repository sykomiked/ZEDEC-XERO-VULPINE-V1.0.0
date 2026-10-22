/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zxva.c — ZXVA vector audio reader and renderer. See zxva.h and ZXVA.md. */
#include "zxva.h"

#define ONE30     ((int64_t) 1 << 30)
#define MAXCODE   6144u /* level code of 0 dBFS (1/256 octave)    */
#define FQ        60    /* frequency fraction bits, turns/sample  */
#define GOLD      0x9E3779B97F4A7C15ull
#define LN2_Q32   2977044472ull /* ln 2, Q32                              */
#define INV2PI_28 42722830      /* 2^28 / (2 pi)                          */
#define SQRT3_30  1859775393    /* sqrt 3, Q30                            */
#define GRAIN_MAX (1u << 20)    /* longest grain, us                      */
#define US        1000000ull

static const int64_t FADE_LO = (int64_t) ((9ull << FQ) / 20);  /* 0.45 turn */
static const int64_t FADE_HI = (int64_t) ((12ull << FQ) / 25); /* 0.48 turn */

/* ---- integer helpers (A2) ---------------------------------------------- */
static int64_t sar(int64_t v, int s) /* floor(v / 2^s) */
{
    return v >= 0 ? v >> s : ~(~v >> s);
}
static int64_t iabs(int64_t v)
{
    return v < 0 ? -v : v;
}
static uint64_t uabs(int64_t v)
{
    return v < 0 ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
}
static int64_t clamp(int64_t v, int64_t lo, int64_t hi)
{
    return v < lo ? lo : v > hi ? hi : v;
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
/* floor(n / d); UINT64_MAX when the quotient does not fit */
static uint64_t div128(u128 n, uint64_t d, uint64_t *rem)
{
    if (n.hi >= d) {
        if (rem) *rem = 0;
        return UINT64_MAX;
    }
    uint64_t r = n.hi, q = 0;
    for (int i = 63; i >= 0; i--) {
        uint64_t carry = r >> 63;
        r = (r << 1) | ((n.lo >> i) & 1u);
        q <<= 1;
        if (carry || r >= d) {
            r -= d;
            q |= 1u;
        }
    }
    if (rem) *rem = r;
    return q;
}
static uint64_t muldiv_u(uint64_t a, uint64_t b, uint64_t d) /* floor(a*b/d) */
{
    return div128(mul64(a, b), d, 0);
}
static int64_t muldiv_s(int64_t a, uint64_t b, uint64_t d) /* floor(a*b/d), saturating */
{
    uint64_t rem, q = div128(mul64(uabs(a), b), d, &rem);
    if (a >= 0) return q > (uint64_t) INT64_MAX ? INT64_MAX : (int64_t) q;
    if (q >= (uint64_t) INT64_MAX) return INT64_MIN + 1;
    return -(int64_t) q - (rem ? 1 : 0);
}
static int64_t mulsh(int64_t a, int64_t b, int s) /* floor(a*b / 2^s), 0<s<64, saturating */
{
    u128 p = mul64(uabs(a), uabs(b));
    uint64_t q = (p.lo >> s) | (p.hi << (64 - s)), fr = p.lo & (((uint64_t) 1 << s) - 1);
    if (p.hi >> s) q = UINT64_MAX;
    if ((a < 0) == (b < 0)) return q > (uint64_t) INT64_MAX ? INT64_MAX : (int64_t) q;
    if (q >= (uint64_t) INT64_MAX) return INT64_MIN + 1;
    return -(int64_t) q - (fr ? 1 : 0);
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
static uint64_t mix64(uint64_t x) /* splitmix64 finaliser */
{
    x += GOLD;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
static uint64_t hkey(uint64_t a, uint64_t b, uint64_t c)
{
    return mix64(mix64(mix64(a) ^ b) ^ c);
}

/* ---- primitives -------------------------------------------------------- */
/* sin(2 pi p / 2^64), Q30. Quarter-wave Taylor series to x^15 (truncation
 * < 7e-12), Horner in Q30 with rounding. */
int32_t zxva_sin_q30(uint64_t p)
{
    uint32_t q = (uint32_t) (p >> 62);
    uint64_t r = p & (((uint64_t) 1 << 62) - 1);
    if (q & 1u) r = ((uint64_t) 1 << 62) - r;
    int64_t x = (int64_t) (((r >> 31) * 3373259426ull + ((uint64_t) 1 << 31)) >> 32); /* Q30 rad */
    int64_t x2 = (x * x + (1 << 29)) >> 30, s = ONE30;
#define ZXVA_SIN_STEP(k) s = ONE30 - (((x2 * s + (1 << 29)) >> 30) + (k) / 2) / (k)
    ZXVA_SIN_STEP(210);
    ZXVA_SIN_STEP(156);
    ZXVA_SIN_STEP(110);
    ZXVA_SIN_STEP(72);
    ZXVA_SIN_STEP(42);
    ZXVA_SIN_STEP(20);
    ZXVA_SIN_STEP(6);
#undef ZXVA_SIN_STEP
    s = (x * s + (1 << 29)) >> 30;
    if (s > ONE30) s = ONE30;
    return (int32_t) (q >= 2 ? -s : s);
}
/* 2^(-i/256), Q30, rounded (generated: round(2^30 * 2^(-i/256))) */
static const int32_t EXP2T[256] = {
    1073741824, 1070838486, 1067942999, 1065055341, 1062175491, 1059303428, 1056439131, 1053582579,
    1050733751, 1047892626, 1045059183, 1042233401, 1039415261, 1036604740, 1033801819, 1031006477,
    1028218693, 1025438448, 1022665720, 1019900489, 1017142735, 1014392438, 1011649578, 1008914134,
    1006186087, 1003465416, 1000752102, 998046124,  995347464,  992656100,  989972014,  987295185,
    984625594,  981963222,  979308048,  976660054,  974019220,  971385527,  968758955,  966139485,
    963527098,  960921775,  958323496,  955732243,  953147997,  950570738,  948000448,  945437108,
    942880699,  940331203,  937788600,  935252872,  932724001,  930201967,  927686753,  925178340,
    922676710,  920181844,  917693724,  915212331,  912737649,  910269657,  907808339,  905353676,
    902905651,  900464244,  898029440,  895601218,  893179563,  890764456,  888355878,  885953814,
    883558244,  881169153,  878786521,  876410331,  874040567,  871677210,  869320244,  866969651,
    864625413,  862287515,  859955938,  857630665,  855311680,  852998965,  850692504,  848392279,
    846098274,  843810471,  841528855,  839253408,  836984114,  834720956,  832463917,  830212982,
    827968132,  825729353,  823496627,  821269938,  819049271,  816834607,  814625932,  812423229,
    810226483,  808035676,  805850792,  803671817,  801498734,  799331526,  797170178,  795014675,
    792865000,  790721137,  788583072,  786450787,  784324269,  782203500,  780088465,  777979150,
    775875538,  773777614,  771685363,  769598769,  767517817,  765442492,  763372778,  761308661,
    759250125,  757197155,  755149737,  753107854,  751071493,  749040637,  747015274,  744995386,
    742980960,  740971982,  738968435,  736970306,  734977579,  732990241,  731008277,  729031671,
    727060411,  725094480,  723133865,  721178552,  719228525,  717283772,  715344277,  713410026,
    711481005,  709557200,  707638598,  705725183,  703816941,  701913860,  700015924,  698123120,
    696235434,  694352853,  692475362,  690602947,  688735596,  686873293,  685016026,  683163781,
    681316545,  679474303,  677637043,  675804750,  673977412,  672155015,  670337545,  668524990,
    666717336,  664914570,  663116678,  661323648,  659535466,  657752119,  655973594,  654199878,
    652430958,  650666822,  648907455,  647152846,  645402981,  643657847,  641917433,  640181724,
    638450708,  636724373,  635002706,  633285695,  631573326,  629865587,  628162466,  626463950,
    624770026,  623080683,  621395908,  619715688,  618040012,  616368866,  614702239,  613040119,
    611382493,  609729349,  608080675,  606436459,  604796689,  603161352,  601530438,  599903933,
    598281827,  596664106,  595050760,  593441776,  591837143,  590236848,  588640881,  587049229,
    585461881,  583878825,  582300049,  580725543,  579155293,  577589290,  576027521,  574469975,
    572916640,  571367506,  569822560,  568281792,  566745190,  565212742,  563684439,  562160268,
    560640218,  559124278,  557612438,  556104685,  554601009,  553101399,  551605844,  550114332,
    548626854,  547143398,  545663953,  544188508,  542717053,  541249576,  539786068,  538326517,
};
/* 2^(-e / 2^32), Q30, e >= 0: table on the top 8 fraction bits, cubic on the
 * rest (|r ln 2| < 0.0028, truncation < 3e-12) */
static int64_t exp2n(uint64_t e)
{
    uint64_t ip = e >> 32;
    if (ip >= 40) return 0;
    uint32_t fr = (uint32_t) e;
    int64_t y = (int64_t) (((uint64_t) (fr & 0xFFFFFFu) * LN2_Q32) >> 34); /* Q30 */
    int64_t y2 = (y * y) >> 30, y3 = (y2 * y) >> 30;
    int64_t p = ONE30 - y + y2 / 2 - y3 / 6;
    return ((EXP2T[fr >> 24] * p + (1 << 29)) >> 30) >> ip;
}
int64_t zxva_exp2_q30(int64_t e)
{
    if (e <= 0) return exp2n(uabs(e));
    uint64_t ip = (uint64_t) e >> 32, fr = (uint64_t) e & 0xFFFFFFFFu;
    if (ip >= 32) return (int64_t) 1 << 62;
    if (!fr) return ONE30 << ip;
    return exp2n(((uint64_t) 1 << 32) - fr) << (ip + 1);
}
int64_t zxva_log2_q32(uint64_t x)
{
    if (!x) return INT64_MIN + 1;
    int b = 63;
    while (!((x >> b) & 1u)) b--;
    uint64_t m = b >= 30 ? x >> (b - 30) : x << (30 - b);
    int64_t f = 0;
    for (int i = 1; i <= 32; i++) {
        m = (m * m) >> 30;
        if (m >= ((uint64_t) 1 << 31)) {
            m >>= 1;
            f |= (int64_t) 1 << (32 - i);
        }
    }
    return (int64_t) (b - 30) * ((int64_t) 1 << 32) + f;
}
int64_t zxva_pow_q30(uint64_t x, uint32_t num, uint32_t den)
{
    if (!x || !den) return 0;
    if (num == den) return (int64_t) x;
    return zxva_exp2_q30(muldiv_s(zxva_log2_q32(x), num, den));
}
uint32_t zxva_lcg(uint32_t s, uint32_t mul, uint32_t add)
{
    return s * mul + add;
}
int64_t zxva_onepole(int64_t y, int64_t x, int64_t a)
{
    return y + sar((x - y) * a, 16);
}
int64_t zxva_decay(int64_t e, int64_t m)
{
    return mulsh(e, m, 30);
}
int64_t zxva_code_amp(uint32_t c)
{
    if (!c) return 0;
    if (c >= MAXCODE) return ONE30;
    return exp2n((uint64_t) (MAXCODE - c) << 24);
}
/* constant-power pan, Q30; exact 1/0 at the ends */
static void pan_gains(uint32_t nch, uint32_t pan, int32_t g[2])
{
    if (nch == 1 || pan == 0) {
        g[0] = (int32_t) ONE30;
        g[1] = 0;
    } else if (pan >= 65536) {
        g[0] = 0;
        g[1] = (int32_t) ONE30;
    } else {
        uint64_t p = (uint64_t) pan << 46;
        g[0] = zxva_sin_q30(((uint64_t) 1 << 62) - p);
        g[1] = zxva_sin_q30(p);
    }
}

/* ---- reader (A7) ------------------------------------------------------- */
typedef struct {
    const uint8_t *p, *e;
    bool bad;
} rd_t;

static uint32_t rd_u8(rd_t *r)
{
    if (r->bad || r->p >= r->e) {
        r->bad = true;
        return 0;
    }
    return *r->p++;
}
static uint32_t rd_u16(rd_t *r)
{
    uint32_t a = rd_u8(r);
    return a | (rd_u8(r) << 8);
}
static uint64_t rd_uv64(rd_t *r) /* < 2^64, at most 10 bytes */
{
    uint64_t v = 0;
    for (int i = 0; i < 10; i++) {
        uint64_t b = rd_u8(r);
        if (r->bad) return 0;
        if (i == 9 && (b & 0xFEu)) {
            r->bad = true;
            return 0;
        }
        v |= (b & 0x7Fu) << (7 * i);
        if (!(b & 0x80u)) return v;
    }
    r->bad = true;
    return 0;
}
static uint32_t rd_uv(rd_t *r) /* < 2^32, at most 5 bytes */
{
    uint64_t v = 0;
    for (int i = 0; i < 5; i++) {
        uint32_t b = rd_u8(r);
        if (r->bad) return 0;
        if (i == 4 && (b & 0xF0u)) {
            r->bad = true;
            return 0;
        }
        v |= (uint64_t) (b & 0x7Fu) << (7 * i);
        if (!(b & 0x80u)) return (uint32_t) v;
    }
    r->bad = true;
    return 0;
}
static int32_t rd_zz(rd_t *r)
{
    uint32_t u = rd_uv(r);
    return (u & 1u) ? -(int32_t) (u >> 1) - 1 : (int32_t) (u >> 1);
}
static int64_t rd_zz64(rd_t *r)
{
    uint64_t u = rd_uv64(r);
    return (u & 1u) ? -(int64_t) (u >> 1) - 1 : (int64_t) (u >> 1);
}
/* range-checked reads: mark bad when out of [lo, hi] */
static uint32_t rd_uvr(rd_t *r, uint32_t lo, uint32_t hi)
{
    uint32_t v = rd_uv(r);
    if (v < lo || v > hi) r->bad = true;
    return v;
}
static uint32_t rd_u8r(rd_t *r, uint32_t lo, uint32_t hi)
{
    uint32_t v = rd_u8(r);
    if (v < lo || v > hi) r->bad = true;
    return v;
}

/* output placement: channel ch (and ch+1 when the second gain is used) */
static void chk_out(rd_t *r, uint32_t nch, uint32_t ch, bool second)
{
    if (ch >= nch || (nch == 1 && second) || (second && ch + 1 >= nch)) r->bad = true;
}

/* ---- chunk walkers: validate, and (with w) index ----------------------- */
#define FMAX ((1u << 31) - 1) /* frequencies are < 2^31 in Q16 Hz (32 kHz) */

static void v_partial(rd_t *r, const zxva_info_t *in)
{
    uint32_t ch = rd_u8(r), pan = rd_uvr(r, 0, 65536), lock = rd_u8r(r, 0, 1);
    chk_out(r, in->nch, ch, pan > 0);
    uint64_t t = rd_uv(r);
    uint32_t nbp = rd_uvr(r, 2, 65535);
    int64_t f = rd_uvr(r, 0, FMAX), c = rd_uvr(r, 0, MAXCODE);
    rd_u16(r);
    for (uint32_t i = 1; i < nbp && !r->bad; i++) {
        t += rd_uvr(r, 100, 1u << 22);
        f += rd_zz(r);
        c += rd_zz(r);
        if (lock) {
            int32_t dp = rd_zz(r);
            if (dp < -32768 || dp > 32767) r->bad = true;
        }
        if (f < 0 || f > FMAX || c < 0 || c > MAXCODE) r->bad = true;
    }
    if (t > in->dur_us) r->bad = true;
    (void) lock;
}
static void v_noise(rd_t *r, const zxva_info_t *in)
{
    uint32_t ch = rd_u8(r), pan = rd_uvr(r, 0, 65536);
    chk_out(r, in->nch, ch, pan > 0);
    uint64_t t0 = rd_uv(r), hop = rd_uvr(r, 500, 1u << 20), nfr = rd_uvr(r, 2, 1u << 20);
    uint32_t nb = rd_u8r(r, 1, ZXVA_MAX_BANDS), prev = 0;
    for (uint32_t b = 0; b <= nb && !r->bad; b++) {
        uint32_t e = rd_uvr(r, 1, 65535);
        if (e <= prev) r->bad = true;
        prev = e;
    }
    int32_t c[ZXVA_MAX_BANDS];
    for (uint32_t b = 0; b < nb && !r->bad; b++) c[b] = (int32_t) rd_uvr(r, 0, MAXCODE);
    for (uint64_t f = 1; f < nfr && !r->bad; f++)
        for (uint32_t b = 0; b < nb && !r->bad; b++) {
            c[b] += rd_zz(r);
            if (c[b] < 0 || c[b] > (int32_t) MAXCODE) r->bad = true;
        }
    if (t0 + (nfr - 1) * hop > in->dur_us) r->bad = true;
}
typedef struct {
    uint32_t t0, kind, ch, pan, dur, att, half, f, bw, code, ph;
} grain_t;
static void rd_grain(rd_t *r, grain_t *g, uint32_t nch)
{
    uint64_t t = (uint64_t) g->t0 + rd_uv(r);
    if (t > UINT32_MAX) r->bad = true;
    g->t0 = (uint32_t) t;
    g->kind = rd_u8r(r, 0, 1);
    g->ch = rd_u8(r);
    g->pan = rd_uvr(r, 0, 65536);
    chk_out(r, nch, g->ch, g->pan > 0);
    g->dur = rd_uvr(r, 100, GRAIN_MAX);
    g->att = rd_uvr(r, 0, g->dur);
    g->half = rd_uvr(r, 0, 1u << 24);
    g->f = rd_uvr(r, 0, FMAX);
    g->bw = rd_uvr(r, 0, g->kind ? FMAX : 0);
    if (g->bw / 2 > g->f || (uint64_t) g->f + g->bw / 2 > FMAX) r->bad = true;
    g->code = rd_uvr(r, 0, MAXCODE);
    g->ph = rd_u16(r);
}
static uint32_t v_grains(rd_t *r, const zxva_info_t *in)
{
    uint32_t n = rd_uvr(r, 1, ZXVA_MAX_GRAINS);
    grain_t g = {0};
    for (uint32_t i = 0; i < n && !r->bad; i++) {
        rd_grain(r, &g, in->nch);
        if ((uint64_t) g.t0 + g.dur > in->dur_us) r->bad = true;
    }
    return n;
}
static void v_voice(rd_t *r, const zxva_info_t *in)
{
    uint32_t kind = rd_u8r(r, 0, 2), ch = rd_u8(r), pan = rd_uvr(r, 0, 65536);
    chk_out(r, in->nch, ch, pan > 0);
    uint64_t t0 = rd_uv(r), f = rd_uvr(r, 1, FMAX), T = 0;
    uint32_t ne = rd_u8r(r, 2, ZXVA_MAX_ENV);
    for (uint32_t i = 0; i < ne && !r->bad; i++) {
        uint32_t dt = rd_uv(r);
        if ((i == 0 && dt != 0) || (i > 0 && (dt < 1 || dt > (1u << 22)))) r->bad = true;
        T += dt;
        rd_uvr(r, 0, MAXCODE);
    }
    if (T > (1u << 26) || t0 + T > in->dur_us) r->bad = true;
    if (kind == ZXVA_V_MODAL) {
        uint32_t np = rd_u8r(r, 1, ZXVA_MAX_MODAL);
        for (uint32_t i = 0; i < np && !r->bad; i++) {
            uint64_t ra = rd_uvr(r, 1, 1u << 24);
            rd_uvr(r, 0, MAXCODE);
            uint32_t h = rd_uvr(r, 0, 1u << 26);
            if ((h && h < 4096) || ((f * ra) >> 16) > FMAX) r->bad = true;
        }
    } else if (kind == ZXVA_V_FM) {
        uint64_t ra = rd_uvr(r, 1, 1u << 24);
        rd_uvr(r, 0, 1u << 20);
        rd_uvr(r, 0, 1u << 20);
        if (((f * ra) >> 16) > FMAX) r->bad = true;
    } else {
        rd_uvr(r, 4096, 1u << 26);
        rd_uvr(r, 0, 65536);
        rd_uvr(r, 1, 32768);
    }
}
static void v_fvoice(rd_t *r, const zxva_info_t *in)
{
    uint64_t rate = rd_uvr(r, ZXVA_RATE_MIN, ZXVA_RATE_MAX), start = rd_uvr(r, 0, 1u << 30);
    uint64_t len = rd_uvr(r, 1, 1u << 26);
    uint32_t kind = rd_u8r(r, 0, 2), ch = rd_u8(r);
    int64_t g0 = rd_zz64(r), g1 = rd_zz64(r);
    const int64_t GM = (int64_t) 1 << 40;
    if (g0 > GM || g0 < -GM || g1 > GM || g1 < -GM) r->bad = true;
    chk_out(r, in->nch, ch, g1 != 0);
    uint32_t env = rd_u8r(r, 0, 3), A = rd_uv(r), B = rd_uv(r);
    if (env == ZXVA_ENV_NONE || env == ZXVA_ENV_HALFSINE) {
        if (A || B) r->bad = true;
    } else if (A > len || (env == ZXVA_ENV_ASR && B > len) || (env == ZXVA_ENV_AD && B > ONE30)) {
        r->bad = true;
    }
    uint64_t ne = rd_u8r(r, 0, ZXVA_FV_ECHO), delay = 0;
    if (ne) {
        delay = rd_uvr(r, 1, 1u << 26);
        rd_uvr(r, 0, (uint32_t) ONE30);
        rd_u8r(r, 0, 1);
    }
    uint64_t fmax = 500 * rate - 1; /* below the native Nyquist, mHz */
    if (kind == ZXVA_FV_TONE) {
        uint32_t np = rd_u8r(r, 1, ZXVA_FV_PARTIALS);
        for (uint32_t i = 0; i < np && !r->bad; i++) {
            if (rd_uvr(r, 1, FMAX) > fmax) r->bad = true;
            rd_uvr(r, 0, 1u << 16);
            if (rd_uv(r) > len) r->bad = true;
            rd_uvr(r, 0, (uint32_t) ONE30);
        }
    } else if (kind == ZXVA_FV_NOISE) {
        rd_uv(r);
        rd_uv(r);
        rd_uv(r);
        rd_uvr(r, 1, 65536);
        rd_uvr(r, 0, 65536);
        rd_uvr(r, 0, 1u << 16);
    } else {
        if (rd_uvr(r, 1, FMAX) > fmax) r->bad = true;
        rd_uvr(r, 1, 1u << 31);
        rd_uvr(r, 0, 1u << 16);
    }
    if ((start + len + ne * delay) * US > (uint64_t) in->dur_us * rate) r->bad = true;
}

/* residual bit reader, MSB first */
typedef struct {
    const uint8_t *p, *e;
    uint32_t bit; /* bits used in *p */
    bool bad;
} br_t;
static uint64_t br_get(br_t *b, int n)
{
    uint64_t v = 0;
    while (n--) {
        if (b->p >= b->e) {
            b->bad = true;
            return 0;
        }
        v = (v << 1) | ((*b->p >> (7 - b->bit)) & 1u);
        if (++b->bit == 8) {
            b->bit = 0;
            b->p++;
        }
    }
    return v;
}
/* decode one block-channel of `cnt` samples into out[i * stride]; false if
 * malformed (A7: every bit accounted for) */
static bool res_block(const uint8_t *p, uint32_t nbytes, uint32_t cnt, int32_t *out,
                      uint32_t stride)
{
    br_t b = {p, p + nbytes, 0, false};
    uint32_t order = (uint32_t) br_get(&b, 2), k = (uint32_t) br_get(&b, 6);
    if (k > 40) return false;
    int64_t s1 = 0, s2 = 0, s3 = 0;
    for (uint32_t i = 0; i < cnt && !b.bad; i++) {
        uint32_t q = 0;
        while (q < 32 && br_get(&b, 1)) q++;
        uint64_t u = q == 32 ? br_get(&b, 40) : (((uint64_t) q << k) | br_get(&b, (int) k));
        int64_t e = (u & 1u) ? -(int64_t) (u >> 1) - 1 : (int64_t) (u >> 1);
        uint32_t o = i < order ? i : order;
        int64_t pr = o == 0 ? 0 : o == 1 ? s1 : o == 2 ? 2 * s1 - s2 : 3 * s1 - 3 * s2 + s3;
        int64_t s = pr + e;
        if (s < INT32_MIN || s > INT32_MAX) return false;
        out[(uint64_t) i * stride] = (int32_t) s;
        s3 = s2;
        s2 = s1;
        s1 = s;
    }
    if (b.bad) return false;
    /* the rest of the last byte must be zero padding, and nothing after it */
    if (b.bit) {
        if (*b.p & (0xFFu >> b.bit)) return false;
        b.p++;
    }
    return b.p == b.e;
}
static void v_residual(rd_t *r, const zxva_info_t *in, zxva_info_t *out)
{
    uint64_t rate = rd_uvr(r, ZXVA_RATE_MIN, ZXVA_RATE_MAX);
    uint32_t bits = rd_u8(r);
    if (bits != 16 && bits != 24 && bits != 32) r->bad = true;
    uint64_t nf = rd_uv64(r);
    if (r->bad || nf != ((uint64_t) in->dur_us * rate + US - 1) / US) {
        r->bad = true;
        return;
    }
    int32_t tmp[1];
    uint64_t nblk = (nf + ZXVA_RES_BLOCK - 1) / ZXVA_RES_BLOCK;
    for (uint64_t b = 0; b < nblk && !r->bad; b++) {
        uint32_t cnt = (uint32_t) (b + 1 < nblk ? ZXVA_RES_BLOCK : nf - b * ZXVA_RES_BLOCK);
        for (uint32_t c = 0; c < in->nch && !r->bad; c++) {
            uint32_t nb = rd_uvr(r, 1, UINT32_MAX);
            if (r->bad || nb > (uint64_t) (r->e - r->p)) {
                r->bad = true;
                return;
            }
            /* decode with stride 0 into one slot: only the checks matter */
            if (!res_block(r->p, nb, cnt, tmp, 0)) r->bad = true;
            r->p += nb;
        }
    }
    out->res_rate = (uint32_t) rate;
    out->res_bits = bits;
    out->res_frames = nf;
}

/* META body (len bytes). Version 1 must parse to exactly len bytes; a
 * higher version is accepted unread (forward compatible: it is skipped by
 * its length). 0 = version 1 (m filled when non-null), 1 = newer, < 0 bad. */
static int v_meta(const uint8_t *p, uint32_t len, zxva_meta_t *m)
{
    rd_t r = {p, p + len, false};
    zxva_meta_t t = {0};
    t.mver = rd_u8(&r);
    if (r.bad || t.mver == 0) return ZXVA_EFORMAT;
    if (t.mver > 1) return 1;
    static const uint8_t pre[4] = {0x01, 0x55, 0x12, 0x20};
    for (uint32_t i = 0; i < ZXVA_CID_LEN; i++) {
        t.cid[i] = (uint8_t) rd_u8(&r);
        if (i < 4 && t.cid[i] != pre[i]) r.bad = true;
    }
    t.src_bytes = rd_uv64(&r);
    t.src_rate = rd_uv(&r);
    t.src_bits = rd_u8(&r);
    if (t.src_bits > 64) r.bad = true;
    t.src_kind = rd_u8r(&r, 0, ZXVA_SRC_OTHER);
    t.analyser = rd_u8r(&r, 0, ZXVA_AN_AUTHOR);
    t.snr_cdb = rd_zz(&r);
    t.band_err_cdb = rd_uv(&r);
    t.exact = rd_u8r(&r, 0, ZXVA_EXACT_UNVERIFIED);
    if (r.bad || r.p != r.e) return ZXVA_EFORMAT;
    if (m) *m = t;
    return 0;
}

int zxva_meta(const uint8_t *d, uint32_t n, zxva_meta_t *m)
{
    zxva_info_t z;
    int rc = zxva_check(d, n, &z);
    if (rc < 0 && rc != ZXVA_ECAP) return rc;
    if (!z.has_meta) return 1;
    /* walk the chunks again to find it: the file is valid, so every chunk
     * is skipped with the walkers' own reads */
    rd_t r = {d + 6, d + n, false};
    zxva_info_t q = {0};
    q.nch = rd_uv(&r);
    q.dur_us = rd_uv(&r);
    rd_uv(&r);
    for (;;) {
        uint32_t tag = rd_u8(&r);
        if (r.bad || tag == ZXVA_END) return 1;
        switch (tag) {
        case ZXVA_PARTIAL:
            v_partial(&r, &q);
            break;
        case ZXVA_NOISE:
            v_noise(&r, &q);
            break;
        case ZXVA_GRAINS:
            v_grains(&r, &q);
            break;
        case ZXVA_VOICE:
            v_voice(&r, &q);
            break;
        case ZXVA_FVOICE:
            v_fvoice(&r, &q);
            break;
        case ZXVA_NORM:
            rd_uv64(&r);
            rd_uv64(&r);
            break;
        case ZXVA_META: {
            uint32_t len = rd_uv(&r);
            return v_meta(r.p, len, m);
        }
        default:
            return 1; /* RESIDUAL is last; META comes before it */
        }
    }
}

static int parse(const uint8_t *d, uint32_t n, zxva_info_t *inf, zxva_work_t *w)
{
    zxva_info_t z = {0};
    if (!d || n < 9 || d[0] != 'Z' || d[1] != 'X' || d[2] != 'V' || d[3] != 'A')
        return ZXVA_EFORMAT;
    rd_t r = {d + 4, d + n, false};
    if (rd_u8(&r) != 1 || rd_u8(&r) != 0) return ZXVA_EFORMAT;
    z.nch = rd_uvr(&r, 1, ZXVA_MAX_CH);
    z.dur_us = rd_uvr(&r, 1, UINT32_MAX);
    z.seed = rd_uv(&r);
    bool cap = false, res = false;
    for (;;) {
        if (r.bad) return ZXVA_EFORMAT;
        const uint8_t *at = r.p;
        uint32_t tag = rd_u8(&r);
        if (r.bad) return ZXVA_EFORMAT;
        if (tag == ZXVA_END) break;
        if (res) return ZXVA_EFORMAT; /* RESIDUAL must be the last chunk */
        switch (tag) {
        case ZXVA_PARTIAL:
            if (w && z.npartial < ZXVA_MAX_TRACKS) w->trk[z.npartial].hdr = at + 1;
            v_partial(&r, &z);
            if (++z.npartial > ZXVA_MAX_TRACKS) cap = true;
            break;
        case ZXVA_NOISE:
            if (w && z.nnoise < ZXVA_MAX_NOISE) w->noi[z.nnoise].hdr = at + 1;
            v_noise(&r, &z);
            if (++z.nnoise > ZXVA_MAX_NOISE) cap = true;
            break;
        case ZXVA_GRAINS:
            if (z.ngrain) return ZXVA_EFORMAT;
            {
                const uint8_t *g0 = r.p;
                z.ngrain = v_grains(&r, &z);
                if (w) {
                    rd_t q = {g0, d + n, false};
                    rd_uv(&q);
                    w->grains = q.p;
                }
            }
            break;
        case ZXVA_VOICE:
            if (w && z.nvoice < ZXVA_MAX_VOICES) w->voice[z.nvoice] = at + 1;
            v_voice(&r, &z);
            if (++z.nvoice > ZXVA_MAX_VOICES) cap = true;
            break;
        case ZXVA_FVOICE:
            if (w && z.nfvoice < ZXVA_MAX_FVOICES) w->fv[z.nfvoice].p = at + 1;
            v_fvoice(&r, &z);
            if (++z.nfvoice > ZXVA_MAX_FVOICES) cap = true;
            break;
        case ZXVA_NORM: {
            if (z.has_norm) return ZXVA_EFORMAT;
            uint64_t pk = rd_uv64(&r), tg = rd_uv64(&r);
            if (pk < 1 || tg < 1 || tg > ((uint64_t) 1 << 62)) return ZXVA_EFORMAT;
            z.has_norm = true;
            if (w) {
                uint64_t g = muldiv_u(tg, (uint64_t) 1 << 32, pk);
                w->norm_g = g > (uint64_t) INT64_MAX ? (uint64_t) INT64_MAX : g;
            }
            break;
        }
        case ZXVA_META: {
            if (z.has_meta) return ZXVA_EFORMAT;
            uint32_t len = rd_uv(&r);
            if (r.bad || len < 1 || len > (uint64_t) (r.e - r.p)) return ZXVA_EFORMAT;
            if (v_meta(r.p, len, 0) < 0) return ZXVA_EFORMAT;
            r.p += len;
            z.has_meta = true;
            break;
        }
        case ZXVA_RESIDUAL:
            if (w) w->res = at + 1;
            v_residual(&r, &z, &z);
            res = true;
            break;
        default:
            return ZXVA_EFORMAT;
        }
    }
    if (r.p != r.e) return ZXVA_EFORMAT; /* nothing may follow END */
    if (inf) *inf = z;
    return cap ? ZXVA_ECAP : 0;
}

int zxva_check(const uint8_t *d, uint32_t n, zxva_info_t *info)
{
    return parse(d, n, info, 0);
}

/* ---- time helpers ------------------------------------------------------ */
static uint64_t nceil(const zxva_work_t *w, uint64_t t_us) /* first frame at or after t */
{
    return (t_us * w->rate + US - 1) / US;
}
static uint32_t dfrac(const zxva_work_t *w, uint64_t n, uint64_t t_us) /* n - t*R/1e6, Q32 */
{
    return (uint32_t) (((n * US - t_us * w->rate) << 32) / US);
}
static uint64_t dus(const zxva_work_t *w, uint32_t d32) /* that fraction in us, Q32 */
{
    return (uint64_t) d32 * US / w->rate;
}
static int64_t fq(const zxva_work_t *w, uint64_t f16) /* Q16 Hz -> Q60 turns/sample */
{
    return (int64_t) muldiv_u(f16, (uint64_t) 1 << (FQ - 16), w->rate);
}
static int64_t fade(int64_t f) /* Q30 gain for |f| (Q60 turns/sample), A4 */
{
    f = iabs(f);
    if (f <= FADE_LO) return ONE30;
    if (f >= FADE_HI) return 0;
    return ((FADE_HI - f) >> 30) * ONE30 / ((FADE_HI - FADE_LO) >> 30);
}
static void put(zxva_work_t *w, uint32_t fi, uint32_t ch, const int32_t g[2], int64_t m30)
{
    int64_t *a = &w->acc[fi * w->info.nch + ch];
    a[0] += mulsh(m30, g[0], 20);
    if (g[1]) a[1] += mulsh(m30, g[1], 20);
}

/* ---- PARTIAL ----------------------------------------------------------- */
static void pt_reset(zxva_work_t *w, zxva_ptrack_t *t)
{
    rd_t r = {t->hdr, w->d + w->n, false};
    t->ch = (uint8_t) rd_u8(&r);
    uint32_t pan = rd_uv(&r);
    pan_gains(w->info.nch, pan, t->g);
    t->locked = (uint8_t) rd_u8(&r);
    t->tb = rd_uv(&r);
    t->left = rd_uv(&r);
    t->fb = rd_uv(&r);
    t->cb = rd_uv(&r);
    t->pha = (uint64_t) rd_u16(&r) << 48;
    t->left--;
    t->p = r.p;
    t->have = 0;
    t->n1 = 0;
}
/* read the next breakpoint: segment (a = old b, b = new) */
static void pt_next(zxva_work_t *w, zxva_ptrack_t *t)
{
    rd_t r = {t->p, w->d + w->n, false};
    if (t->have) { /* advance the phase state over the finished segment */
        const uint64_t M = 2 * US * 65536;
        uint64_t prod = ((uint64_t) t->fa + t->fb) * (t->tb - t->ta);
        u128 nn = {prod % M, 0};
        t->pha += div128(nn, M, 0) + ((uint64_t) (uint32_t) t->dph << 48);
    }
    t->ta = t->tb;
    t->fa = t->fb;
    t->ca = t->cb;
    t->tb = t->ta + rd_uv(&r);
    t->fb = (uint32_t) ((int64_t) t->fa + rd_zz(&r));
    t->cb = (uint32_t) ((int64_t) t->ca + rd_zz(&r));
    t->dph = t->locked ? rd_zz(&r) : 0;
    t->p = r.p;
    t->left--;
    t->have = 1;
    /* coefficients at this rate (A3) */
    uint64_t D = t->tb - t->ta, DR = D * w->rate;
    t->n0 = nceil(w, t->ta);
    t->n1 = nceil(w, t->tb);
    if (t->n1 <= t->n0) return;
    uint32_t d32 = dfrac(w, t->n0, t->ta);
    int64_t F =
        fq(w, t->fa) + muldiv_s((int64_t) t->dph * (int64_t) US, (uint64_t) 1 << (FQ - 16), DR);
    int64_t X = muldiv_s((int64_t) t->fb - t->fa, (uint64_t) 1 << (FQ - 16), w->rate);
    int64_t G = muldiv_s(X, US, DR); /* Q60 turns/sample^2 */
    int sh = 16;
    while (sh > 0 && (iabs(G) >> (47 - sh)) != 0) sh--;
    t->Gh = muldiv_s(X, US << sh, 2 * DR); /* G/2, Q(60+sh) */
    t->sh = sh;
    t->G = G;
    int64_t Gd = mulsh(G, d32, 32);
    t->A = t->pha + ((uint64_t) (mulsh(F, d32, 32) + sar(mulsh(Gd, d32, 32), 1)) << (64 - FQ));
    t->B = F + Gd;
    int64_t aa = zxva_code_amp(t->ca), ab = zxva_code_amp(t->cb);
    t->Sa = muldiv_s((ab - aa) * (int64_t) US, 1u << 16, DR);
    t->Aa = aa * 65536 + mulsh(t->Sa, d32, 32);
    int64_t fs = t->B, fe = t->B + G * (int64_t) (t->n1 - 1 - t->n0);
    if (((fs < 0) == (fe < 0)) && iabs(fs) >= FADE_HI && iabs(fe) >= FADE_HI)
        t->bl = 2;
    else if (iabs(fs) <= FADE_LO && iabs(fe) <= FADE_LO)
        t->bl = 0;
    else
        t->bl = 1;
}
static void pt_span(zxva_work_t *w, zxva_ptrack_t *t, uint64_t N, uint64_t a, uint64_t b)
{
    if (t->bl == 2 || (!t->Aa && !t->Sa)) return;
    uint64_t mask = ((uint64_t) 1 << t->sh) - 1;
    for (uint64_t n = a; n < b; n++) {
        uint64_t u = n - t->n0, W = u * u;
        /* all phase arithmetic wraps mod 2^64 (unsigned): only the phase mod 1
         * turn matters */
        uint64_t quad =
            (uint64_t) t->Gh * (W >> t->sh) + (uint64_t) sar(t->Gh * (int64_t) (W & mask), t->sh);
        uint64_t ph = t->A + (((uint64_t) t->B * u + quad) << (64 - FQ));
        int64_t amp = sar(t->Aa + t->Sa * (int64_t) u, 16);
        if (t->bl) amp = sar(amp * fade(t->B + t->G * (int64_t) u), 30);
        put(w, (uint32_t) (n - N), t->ch, t->g, sar(amp * zxva_sin_q30(ph), 30));
    }
}
static void r_partials(zxva_work_t *w, uint64_t N, uint32_t L)
{
    for (uint32_t i = 0; i < w->ntrack; i++) {
        zxva_ptrack_t *t = &w->trk[i];
        if (nceil(w, t->tend) <= N || nceil(w, t->tstart) >= N + L) continue;
        if (t->have && N < t->n0 && t->ta != t->tstart) pt_reset(w, t);
        if (!t->have) pt_next(w, t);
        for (;;) {
            if (t->n1 > N && t->n0 < N + L) {
                uint64_t a = t->n0 > N ? t->n0 : N, b = t->n1 < N + L ? t->n1 : N + L;
                pt_span(w, t, N, a, b);
            }
            if (t->n1 >= N + L || !t->left) break;
            pt_next(w, t);
        }
    }
}

/* ---- NOISE ------------------------------------------------------------- */
static void nt_frame0(zxva_work_t *w, zxva_ntrack_t *t)
{
    rd_t r = {t->hdr, w->d + w->n, false};
    rd_u8(&r);
    rd_uv(&r);
    rd_uv(&r);
    rd_uv(&r);
    rd_uv(&r);
    rd_u8(&r);
    for (uint32_t b = 0; b <= t->nb; b++) rd_uv(&r);
    for (uint32_t b = 0; b < t->nb; b++) t->cc[b] = (int32_t) rd_uv(&r);
    for (uint32_t b = 0; b < t->nb; b++) t->nc[b] = t->cc[b] + rd_zz(&r);
    for (uint32_t b = 0; b < t->nb; b++) {
        t->ca[b] = zxva_code_amp((uint32_t) t->cc[b]);
        t->na[b] = zxva_code_amp((uint32_t) t->nc[b]);
    }
    t->fr = r.p;
    t->fi = 0;
}
static void nt_advance(zxva_work_t *w, zxva_ntrack_t *t)
{
    rd_t r = {t->fr, w->d + w->n, false};
    for (uint32_t b = 0; b < t->nb; b++) {
        t->cc[b] = t->nc[b];
        t->ca[b] = t->na[b];
    }
    t->fi++;
    if (t->fi + 1 < t->nfr)
        for (uint32_t b = 0; b < t->nb; b++) {
            t->nc[b] = t->cc[b] + rd_zz(&r);
            t->na[b] = zxva_code_amp((uint32_t) t->nc[b]);
        }
    t->fr = r.p;
}
static void nt_open(zxva_work_t *w, zxva_ntrack_t *t, uint32_t idx)
{
    rd_t r = {t->hdr, w->d + w->n, false};
    t->ch = (uint8_t) rd_u8(&r);
    pan_gains(w->info.nch, rd_uv(&r), t->g);
    t->t0 = rd_uv(&r);
    t->hop = rd_uv(&r);
    t->nfr = rd_uv(&r);
    t->nb = (uint8_t) rd_u8(&r);
    for (uint32_t b = 0; b <= t->nb; b++) t->edge[b] = (uint16_t) rd_uv(&r);
    uint64_t top = (uint64_t) w->rate * 9 / 20; /* 0.45 R, Hz */
    for (uint32_t b = 0; b < t->nb; b++) {
        uint64_t lo = t->edge[b], hi = t->edge[b + 1], hc = hi < top ? hi : top;
        t->nrm[b] = 0;
        if (lo >= hc) continue;
        uint64_t f0 = isqrt64((lo * hc) << 32); /* Q16 Hz */
        uint64_t bw = (hc - lo) << 16;
        int64_t sn = zxva_sin_q30(muldiv_u(f0, (uint64_t) 1 << 48, w->rate));
        int64_t cs = zxva_sin_q30(((uint64_t) 1 << 62) - muldiv_u(f0, (uint64_t) 1 << 48, w->rate));
        int64_t al = (int64_t) muldiv_u((uint64_t) sn, bw, 2 * f0); /* sin w0 / 2Q, Q30 */
        int64_t den = ONE30 + al;
        t->b0[b] = muldiv_s(al, (uint64_t) ONE30, (uint64_t) den);
        t->a1[b] = -muldiv_s(2 * cs, (uint64_t) ONE30, (uint64_t) den);
        t->a2[b] = muldiv_s(ONE30 - al, (uint64_t) ONE30, (uint64_t) den);
        /* each band's power = (amp^2 / 2) * kept fraction; the filter's noise
         * gain is exactly b0 */
        uint64_t keep = isqrt64(muldiv_u(hc - lo, (uint64_t) 1 << 60, hi - lo)); /* Q30 */
        uint64_t sq = isqrt64((uint64_t) (2 * t->b0[b]) << 30); /* sqrt(2 b0), Q30 */
        t->nrm[b] = sq ? (int64_t) muldiv_u(keep, (uint64_t) ONE30, sq) : 0;
    }
    t->n0 = nceil(w, t->t0);
    t->pos = t->n0;
    for (uint32_t b = 0; b < ZXVA_MAX_BANDS; b++)
        for (int k = 0; k < 5; k++) t->st[b][k] = 0;
    nt_frame0(w, t);
    (void) idx;
}
static int64_t nt_filter(zxva_work_t *w, zxva_ntrack_t *t, uint32_t idx, uint32_t b, uint64_t n)
{
    uint64_t h = mix64(hkey(w->info.seed, 0x4E00u + idx, b) + n * GOLD);
    int64_t x = sar(((int64_t) (h >> 39) - ((int64_t) 1 << 24)) * SQRT3_30, 30); /* unit var, Q24 */
    int64_t *s = t->st[b];
    int64_t acc = t->b0[b] * (x - s[1]) - t->a1[b] * s[2] - t->a2[b] * s[3] + s[4];
    int64_t y = sar(acc, 30);
    s[4] = acc - y * ONE30; /* error feedback: no DC drift from rounding */
    s[1] = s[0];
    s[0] = x;
    s[3] = s[2];
    s[2] = y;
    return y;
}
static void nt_run(zxva_work_t *w, zxva_ntrack_t *t, uint32_t idx, uint64_t from, uint64_t to)
{
    for (uint64_t n = from; n < to; n++)
        for (uint32_t b = 0; b < t->nb; b++)
            if (t->nrm[b]) nt_filter(w, t, idx, b, n);
}
static void r_noise(zxva_work_t *w, uint64_t N, uint32_t L)
{
    uint64_t pre = w->rate / 20; /* 50 ms */
    for (uint32_t i = 0; i < w->nnoise; i++) {
        zxva_ntrack_t *t = &w->noi[i];
        uint64_t ne = nceil(w, (uint64_t) t->t0 + (uint64_t) (t->nfr - 1) * t->hop);
        uint64_t a = N > t->n0 ? N : t->n0, e = N + L < ne ? N + L : ne;
        if (a >= e) continue;
        if (a != t->pos) {
            if (a > t->pos && a - t->pos <= pre) {
                nt_run(w, t, i, t->pos, a);
            } else {
                for (uint32_t b = 0; b < ZXVA_MAX_BANDS; b++)
                    for (int k = 0; k < 5; k++) t->st[b][k] = 0;
                uint64_t p0 = a - t->n0 <= pre ? t->n0 : a - pre;
                nt_run(w, t, i, p0, a);
            }
        }
        uint32_t d32 = dfrac(w, t->n0, t->t0);
        uint64_t du = dus(w, d32), hq = (uint64_t) t->hop << 32;
        for (uint64_t n = a; n < e; n++) {
            uint64_t tau = du + (n - t->n0) * w->inc_us, j = tau / hq;
            if (j > t->nfr - 2) j = t->nfr - 2;
            int64_t fr = (int64_t) ((tau - j * hq) / ((uint64_t) t->hop * 4)); /* Q30 */
            if (j < t->fi) nt_frame0(w, t);
            while (t->fi < j) nt_advance(w, t);
            int64_t m = 0;
            for (uint32_t b = 0; b < t->nb; b++) {
                if (!t->nrm[b]) continue;
                int64_t y = nt_filter(w, t, i, b, n);
                int64_t amp = t->ca[b] + sar((t->na[b] - t->ca[b]) * fr, 30);
                int64_t g = mulsh(amp, t->nrm[b], 30); /* nrm holds the 1/sqrt 2 */
                m += mulsh(y, g, 24);                  /* Q24 * Q30 -> Q30 */
            }
            put(w, (uint32_t) (n - N), t->ch, t->g, m);
        }
        t->pos = e;
    }
}

/* ---- GRAINS ------------------------------------------------------------ */
static int64_t sin2q(uint64_t r32) /* sin^2(pi/2 * r), r Q32 in [0,1], Q30 */
{
    int64_t s = zxva_sin_q30(r32 << 30);
    return (s * s) >> 30;
}
static void grain_span(zxva_work_t *w, const grain_t *g, uint32_t idx, uint64_t N, uint32_t L)
{
    uint64_t n0 = nceil(w, g->t0), n1 = nceil(w, (uint64_t) g->t0 + g->dur);
    uint64_t a = n0 > N ? n0 : N, b = n1 < N + L ? n1 : N + L;
    if (a >= b || !g->code) return;
    int32_t pg[2];
    pan_gains(w->info.nch, g->pan, pg);
    uint32_t d32 = dfrac(w, n0, g->t0);
    uint64_t du = dus(w, d32), fl = g->dur / 8, tend = (uint64_t) g->dur << 32;
    int64_t amp0 = zxva_code_amp(g->code);
    int K = g->kind ? ZXVA_NOISE_K : 1;
    int64_t F[ZXVA_NOISE_K], Fd[ZXVA_NOISE_K], gn[ZXVA_NOISE_K];
    uint64_t P[ZXVA_NOISE_K];
    for (int k = 0; k < K; k++) {
        uint64_t f = g->f, ph = (uint64_t) g->ph << 48;
        if (g->kind) {
            uint64_t h = hkey(w->info.seed, 0x6700000000ull + idx, (uint64_t) k);
            f = g->f - g->bw / 2 + (((uint64_t) g->bw * (h >> 32)) >> 32);
            ph = mix64(h);
        }
        F[k] = fq(w, f);
        Fd[k] = mulsh(F[k], d32, 32);
        P[k] = ph;
        gn[k] = fade(F[k]);
        if (g->kind) gn[k] >>= 2; /* 16 components: each amp / 4 */
    }
    for (uint64_t n = a; n < b; n++) {
        uint64_t u = n - n0, tau = du + u * w->inc_us;
        int64_t env = amp0;
        if (tau < ((uint64_t) g->att << 32)) env = (env * sin2q(tau / g->att)) >> 30;
        if (g->half) {
            uint64_t ta = (uint64_t) g->att << 32, x = tau > ta ? tau - ta : 0;
            env = (env * exp2n(x / g->half)) >> 30;
        }
        if (fl && tau + (fl << 32) > tend) {
            uint64_t rem = tau < tend ? tend - tau : 0;
            env = (env * sin2q(rem / fl)) >> 30;
        }
        int64_t m = 0;
        for (int k = 0; k < K; k++) {
            if (!gn[k]) continue;
            uint64_t ph = P[k] + (((uint64_t) F[k] * u + (uint64_t) Fd[k]) << (64 - FQ));
            m += sar(((env * gn[k]) >> 30) * zxva_sin_q30(ph), 30);
        }
        put(w, (uint32_t) (n - N), g->ch, pg, m);
    }
}
static void r_grains(zxva_work_t *w, uint64_t N, uint32_t L)
{
    if (!w->grains) return;
    uint64_t tb = N * US / w->rate, te = (N + L) * US / w->rate + 1;
    rd_t r = {w->gcur, w->d + w->n, false};
    grain_t g;
    if (w->gcur_i && tb < (uint64_t) w->gcur_t0 + GRAIN_MAX) { /* may have gone back */
        w->gcur = w->grains;
        w->gcur_i = 0;
        w->gcur_t0 = 0;
        r.p = w->grains;
    }
    /* advance past grains that ended before this block (dur <= GRAIN_MAX) */
    for (;;) {
        if (w->gcur_i >= w->ngrain) return;
        g.t0 = w->gcur_t0;
        rd_t q = r;
        rd_grain(&q, &g, w->info.nch);
        if ((uint64_t) g.t0 + GRAIN_MAX > tb) break;
        r = q;
        w->gcur = q.p;
        w->gcur_i++;
        w->gcur_t0 = g.t0; /* t0 of the last grain skipped */
    }
    uint32_t i = w->gcur_i, t0 = w->gcur_t0;
    for (; i < w->ngrain; i++) {
        g.t0 = t0;
        rd_grain(&r, &g, w->info.nch);
        t0 = g.t0;
        if (g.t0 > te) break;
        grain_span(w, &g, i, N, L);
    }
}

/* ---- VOICE ------------------------------------------------------------- */
static void r_voice(zxva_work_t *w, const uint8_t *p, uint64_t N, uint32_t L)
{
    rd_t r = {p, w->d + w->n, false};
    uint32_t kind = rd_u8(&r), ch = rd_u8(&r);
    int32_t pg[2];
    pan_gains(w->info.nch, rd_uv(&r), pg);
    uint64_t t0 = rd_uv(&r), f = rd_uv(&r);
    uint32_t ne = rd_u8(&r), T[ZXVA_MAX_ENV];
    int64_t EA[ZXVA_MAX_ENV];
    uint32_t tt = 0;
    for (uint32_t i = 0; i < ne; i++) {
        tt += rd_uv(&r);
        T[i] = tt;
        EA[i] = zxva_code_amp(rd_uv(&r));
    }
    uint64_t n0 = nceil(w, t0), n1 = nceil(w, t0 + tt);
    uint64_t a = n0 > N ? n0 : N, b = n1 < N + L ? n1 : N + L;
    if (a >= b) return;
    uint32_t d32 = dfrac(w, n0, t0);
    uint64_t du = dus(w, d32);
    int np = 0;
    int64_t F[ZXVA_PLUCK_HARM] = {0}, Fd[ZXVA_PLUCK_HARM] = {0}, A[ZXVA_PLUCK_HARM] = {0};
    uint64_t RK[ZXVA_PLUCK_HARM] = {0};
    int64_t i0 = 0, i1 = 0, Fm = 0, Fmd = 0, imax = 0;
    if (kind == ZXVA_V_MODAL || kind == ZXVA_V_PLUCK) {
        uint32_t cnt = kind == ZXVA_V_MODAL ? rd_u8(&r) : ZXVA_PLUCK_HARM;
        uint32_t h1 = 0, damp = 0, pos = 0;
        if (kind == ZXVA_V_PLUCK) {
            h1 = rd_uv(&r);
            damp = rd_uv(&r);
            pos = rd_uv(&r);
        }
        for (uint32_t k = 0; k < cnt; k++) {
            uint64_t ratio, half;
            int64_t amp;
            if (kind == ZXVA_V_MODAL) {
                ratio = rd_uv(&r);
                amp = zxva_code_amp(rd_uv(&r));
                half = rd_uv(&r);
            } else {
                ratio = (uint64_t) (k + 1) << 16;
                amp = iabs(zxva_sin_q30((uint64_t) (k + 1) * pos << 47)) / (k + 1);
                half = (uint64_t) h1 * 65536 / (65536 + (uint64_t) damp * k);
                if (half < 1) half = 1;
            }
            uint64_t fk = (f * ratio) >> 16;
            if (fk > FMAX) continue;
            int64_t Fk = fq(w, fk), gk = fade(Fk);
            if (!gk || !amp) continue;
            F[np] = Fk;
            Fd[np] = mulsh(Fk, d32, 32);
            A[np] = (amp * gk) >> 30;
            RK[np] = half ? ((uint64_t) 1 << 40) / half : 0;
            np++;
        }
    } else {
        uint64_t ratio = rd_uv(&r);
        i0 = rd_uv(&r);
        i1 = rd_uv(&r);
        F[0] = fq(w, f);
        Fd[0] = mulsh(F[0], d32, 32);
        A[0] = fade(F[0]);
        RK[0] = 0;
        Fm = fq(w, (f * ratio) >> 16);
        Fmd = mulsh(Fm, d32, 32);
        imax = FADE_LO > F[0] && Fm ? muldiv_s(FADE_LO - F[0], 65536, (uint64_t) Fm) - 65536 : 0;
        if (imax < 0) imax = 0;
        np = A[0] ? 1 : 0;
    }
    uint32_t seg = 0;
    for (uint64_t n = a; n < b; n++) {
        uint64_t u = n - n0, tau = du + u * w->inc_us, t8 = tau >> 24;
        while (seg + 2 < ne && t8 >= (uint64_t) T[seg + 1] << 8) seg++;
        int64_t env = EA[seg];
        uint64_t s0 = (uint64_t) T[seg] << 8, s1 = (uint64_t) T[seg + 1] << 8;
        if (t8 >= s1)
            env = EA[seg + 1];
        else if (t8 > s0)
            env += (EA[seg + 1] - EA[seg]) * (int64_t) (t8 - s0) / (int64_t) (s1 - s0);
        int64_t m = 0;
        if (kind == ZXVA_V_FM) {
            if (np) {
                int64_t idx = i0 + (i1 - i0) * (int64_t) t8 / (int64_t) ((uint64_t) tt << 8);
                if (idx > imax) idx = imax;
                uint64_t pm = ((uint64_t) Fm * u + (uint64_t) Fmd) << (64 - FQ);
                int64_t rad = sar(idx * zxva_sin_q30(pm), 16); /* Q30 radians */
                uint64_t pc = (((uint64_t) F[0] * u + (uint64_t) Fd[0]) << (64 - FQ)) +
                              ((uint64_t) (rad * INV2PI_28) << 6);
                m = sar(((env * A[0]) >> 30) * zxva_sin_q30(pc), 30);
            }
        } else {
            for (int k = 0; k < np; k++) {
                int64_t ak = (env * A[k]) >> 30;
                if (RK[k]) ak = (ak * exp2n((t8 * RK[k]) >> 16)) >> 30;
                if (!ak) continue;
                uint64_t ph = ((uint64_t) F[k] * u + (uint64_t) Fd[k]) << (64 - FQ);
                m += sar(ak * zxva_sin_q30(ph), 30);
            }
        }
        put(w, (uint32_t) (n - N), ch, pg, m);
    }
}

/* ---- FVOICE (frame recipes) ------------------------------------------- */
static uint64_t fv_frames(const zxva_work_t *w, const zxva_fvoice_t *v, uint64_t n)
{
    return w->rate == v->rate ? n : (n * w->rate + v->rate / 2) / v->rate;
}
static int64_t fv_mult(const zxva_work_t *w, const zxva_fvoice_t *v, int64_t m)
{
    return w->rate == v->rate ? m : zxva_pow_q30((uint64_t) m, v->rate, w->rate);
}
static int64_t fv_alpha(const zxva_work_t *w, const zxva_fvoice_t *v, int64_t a)
{
    if (w->rate == v->rate || !a) return a;
    int64_t p = zxva_pow_q30((uint64_t) (65536 - a) << 14, v->rate, w->rate);
    int64_t r = sar(ONE30 - p, 14);
    return clamp(r, 1, 65536);
}
/* floor(mHz * 2^64 / (1000 R)), Q64 turns per frame; saturates */
static uint64_t fv_inc(const zxva_work_t *w, uint64_t mhz)
{
    u128 n = {mhz, 0};
    return div128(n, 1000ull * w->rate, 0);
}
static void fv_open(zxva_work_t *w, zxva_fvoice_t *v)
{
    rd_t r = {v->p, w->d + w->n, false};
    v->rate = rd_uv(&r);
    v->start = rd_uv(&r);
    v->len = rd_uv(&r);
    v->kind = rd_u8(&r);
    v->ch = rd_u8(&r);
    v->g[0] = rd_zz64(&r);
    v->g[1] = rd_zz64(&r);
    v->env = rd_u8(&r);
    v->envA = rd_uv(&r);
    v->envB = rd_uv(&r);
    v->necho = rd_u8(&r);
    v->delay = v->egain = v->mirror = 0;
    if (v->necho) {
        v->delay = rd_uv(&r);
        v->egain = rd_uv(&r);
        v->mirror = rd_u8(&r);
    }
    v->start_r = fv_frames(w, v, v->start);
    v->len_r = fv_frames(w, v, v->len);
    if (!v->len_r) v->len_r = 1;
    v->delay_r = fv_frames(w, v, v->delay);
    v->A_r = fv_frames(w, v, v->envA);
    v->R_r = v->env == ZXVA_ENV_ASR ? fv_frames(w, v, v->envB) : 0;
    v->dec_r = v->env == ZXVA_ENV_AD ? fv_mult(w, v, v->envB) : 0;
    v->hsq = ((uint64_t) 1 << 63) / v->len_r;
    v->hsr = ((uint64_t) 1 << 63) % v->len_r;
    if (v->kind == ZXVA_FV_TONE) {
        v->np = rd_u8(&r);
        for (uint32_t k = 0; k < v->np; k++) {
            v->pinc[k] = fv_inc(w, rd_uv(&r));
            v->pamp[k] = rd_uv(&r);
            /* at a lower rate than native, drop what would alias (A4) */
            if (w->rate != v->rate &&
                v->pinc[k] >= (uint64_t) (((unsigned long long) FADE_HI) << 4))
                v->pamp[k] = 0;
            v->pA[k] = fv_frames(w, v, rd_uv(&r));
            v->pdec[k] = fv_mult(w, v, rd_uv(&r));
        }
    } else if (v->kind == ZXVA_FV_NOISE) {
        v->seed = rd_uv(&r);
        v->mul = rd_uv(&r);
        v->add = rd_uv(&r);
        v->alp_r = fv_alpha(w, v, rd_uv(&r));
        v->ahp_r = fv_alpha(w, v, rd_uv(&r));
        v->amp = rd_uv(&r);
    } else {
        v->inc0 = fv_inc(w, rd_uv(&r));
        v->glide_r = fv_mult(w, v, rd_uv(&r));
        v->amp = rd_uv(&r);
    }
    for (uint32_t j = 0; j <= ZXVA_FV_ECHO; j++) v->inst[j].i = UINT64_MAX;
}
static void fv_reset(const zxva_fvoice_t *v, zxva_fvstate_t *s)
{
    for (uint32_t k = 0; k < ZXVA_FV_PARTIALS; k++) {
        s->ph[k] = 0;
        s->inc[k] = v->kind == ZXVA_FV_SWEEP ? v->inc0 : v->pinc[k];
        s->e[k] = (k < v->np && v->pA[k]) ? 0 : ONE30;
    }
    s->lp = s->hp = 0;
    s->E = v->A_r ? 0 : ONE30;
    s->hq = s->hr = 0;
    s->s = v->seed;
    s->i = 0;
}
static int64_t fv_env(const zxva_fvoice_t *v, const zxva_fvstate_t *s)
{
    uint64_t i = s->i;
    switch (v->env) {
    case ZXVA_ENV_ASR: {
        int64_t e = i < v->A_r ? (int64_t) (i * (uint64_t) ONE30 / v->A_r) : ONE30;
        if (v->R_r && i + v->R_r >= v->len_r) {
            int64_t rr = (int64_t) ((v->len_r - i) * (uint64_t) ONE30 / v->R_r);
            if (rr < e) e = rr;
        }
        return e;
    }
    case ZXVA_ENV_HALFSINE:
        return zxva_sin_q30(s->hq); /* = floor(i * 2^63 / len) */
    case ZXVA_ENV_AD:
        return s->E;
    default:
        return ONE30;
    }
}
/* one frame: returns v(i) (Q30 full scale) and steps the state to i + 1 */
static int64_t fv_step(const zxva_fvoice_t *v, zxva_fvstate_t *s)
{
    int64_t x = 0;
    if (v->kind == ZXVA_FV_TONE) {
        for (uint32_t k = 0; k < v->np; k++) {
            int64_t xk = sar((int64_t) zxva_sin_q30(s->ph[k]) * v->pamp[k], 14);
            x += mulsh(xk, s->e[k], 30);
            s->ph[k] += s->inc[k];
            uint64_t i1 = s->i + 1;
            if (i1 < v->pA[k])
                s->e[k] = (int64_t) (i1 * (uint64_t) ONE30 / v->pA[k]);
            else if (i1 == v->pA[k])
                s->e[k] = ONE30;
            else
                s->e[k] = zxva_decay(s->e[k], v->pdec[k]);
        }
    } else if (v->kind == ZXVA_FV_NOISE) {
        s->s = zxva_lcg(s->s, v->mul, v->add);
        int64_t xs = (int64_t) s->s - ((s->s & 0x80000000u) ? ((int64_t) 1 << 32) : 0);
        s->lp = zxva_onepole(s->lp, sar(xs, 1), v->alp_r);
        int64_t wv = s->lp;
        if (v->ahp_r) {
            s->hp = zxva_onepole(s->hp, wv, v->ahp_r);
            wv -= s->hp;
        }
        x = sar(wv * v->amp, 14);
    } else {
        x = sar((int64_t) zxva_sin_q30(s->ph[0]) * v->amp, 14);
        s->ph[0] += s->inc[0];
        u128 p = mul64(s->inc[0], (uint64_t) v->glide_r);
        uint64_t ni = (p.hi >> 30) ? UINT64_MAX : (p.lo >> 30) | (p.hi << 34);
        s->inc[0] = ni > ((uint64_t) 1 << 63) ? (uint64_t) 1 << 63 : ni;
    }
    x = mulsh(x, fv_env(v, s), 30);
    if (v->env == ZXVA_ENV_AD) {
        uint64_t i1 = s->i + 1;
        if (i1 < v->A_r)
            s->E = (int64_t) (i1 * (uint64_t) ONE30 / v->A_r);
        else if (i1 == v->A_r)
            s->E = ONE30;
        else
            s->E = zxva_decay(s->E, v->dec_r);
    }
    s->hr += v->hsr; /* step floor(i 2^63 / len) exactly */
    s->hq += v->hsq + (s->hr >= v->len_r ? 1 : 0);
    if (s->hr >= v->len_r) s->hr -= v->len_r;
    s->i++;
    return x;
}
static void r_fvoices(zxva_work_t *w, uint64_t N, uint32_t L)
{
    for (uint32_t vi = 0; vi < w->nfv; vi++) {
        zxva_fvoice_t *v = &w->fv[vi];
        int64_t gj = ONE30;
        for (uint32_t j = 0; j <= v->necho; j++) {
            if (j) gj = mulsh(gj, v->egain, 30);
            uint64_t st = v->start_r + j * v->delay_r;
            if (st >= N + L || st + v->len_r <= N) continue;
            uint64_t ib = N > st ? N - st : 0, ie = N + L - st < v->len_r ? N + L - st : v->len_r;
            zxva_fvstate_t *s = &v->inst[j];
            if (s->i > ib) fv_reset(v, s);
            while (s->i < ib) fv_step(v, s);
            int64_t G0 = mulsh(v->g[0], gj, 30), G1 = mulsh(v->g[1], gj, 30);
            if (v->mirror && (j & 1u)) {
                int64_t t = G0;
                G0 = G1;
                G1 = t;
            }
            if (w->info.nch == 1) G1 = 0;
            for (uint64_t i = ib; i < ie; i++) {
                int64_t x = fv_step(v, s);
                int64_t *a = &w->acc[(st + i - N) * w->info.nch + v->ch];
                a[0] += mulsh(x, G0, 20);
                if (G1) a[1] += mulsh(x, G1, 20);
            }
        }
    }
}

/* ---- residual ---------------------------------------------------------- */
static void res_rewind(zxva_work_t *w)
{
    rd_t r = {w->res, w->d + w->n, false};
    rd_uv(&r);
    rd_u8(&r);
    rd_uv64(&r);
    w->rcur = r.p;
    w->rnext = 0;
}
static void res_load(zxva_work_t *w, uint64_t blk)
{
    if (w->rblk == blk) return;
    if (blk < w->rnext) res_rewind(w);
    uint64_t nf = w->info.res_frames, nblk = (nf + ZXVA_RES_BLOCK - 1) / ZXVA_RES_BLOCK;
    rd_t r = {w->rcur, w->d + w->n, false};
    for (; w->rnext < blk; w->rnext++)
        for (uint32_t c = 0; c < w->info.nch; c++) {
            uint32_t nb = rd_uv(&r);
            r.p += nb;
        }
    uint32_t cnt = (uint32_t) (blk + 1 < nblk ? ZXVA_RES_BLOCK : nf - blk * ZXVA_RES_BLOCK);
    for (uint32_t c = 0; c < w->info.nch; c++) {
        uint32_t nb = rd_uv(&r);
        res_block(r.p, nb, cnt, &w->rbuf[c], w->info.nch);
        r.p += nb;
    }
    w->rcur = r.p;
    w->rnext = blk + 1;
    w->rblk = blk;
}

/* ---- top level --------------------------------------------------------- */
int zxva_open(zxva_work_t *w, const uint8_t *d, uint32_t n, uint32_t rate, uint32_t bits,
              uint32_t flags)
{
    if (!w || !d) return ZXVA_EARG;
    if (rate < ZXVA_RATE_MIN || rate > ZXVA_RATE_MAX || (bits != 16 && bits != 24 && bits != 32))
        return ZXVA_EARG;
    if (!(flags & ZXVA_L_ALL)) flags |= ZXVA_L_ALL;
    w->grains = 0;
    w->res = 0;
    w->norm_g = 0;
    int rc = parse(d, n, &w->info, w);
    if (rc < 0) return rc;
    if ((flags & ZXVA_F_RESIDUAL) &&
        (!w->res || w->info.res_rate != rate || w->info.res_bits != bits))
        return ZXVA_EARG;
    if (flags & ZXVA_F_NONORM) w->norm_g = 0;
    w->d = d;
    w->n = n;
    w->rate = rate;
    w->bits = bits;
    w->flags = flags;
    w->frames = ((uint64_t) w->info.dur_us * rate + US - 1) / US;
    w->inc_us = (US << 32) / rate;
    w->ntrack = w->info.npartial;
    w->nnoise = w->info.nnoise;
    w->nvoice = w->info.nvoice;
    w->nfv = w->info.nfvoice;
    for (uint32_t i = 0; i < w->ntrack; i++) {
        zxva_ptrack_t *t = &w->trk[i];
        pt_reset(w, t);
        t->tstart = t->tb;
        rd_t r = {t->p, d + n, false};
        uint64_t te = t->tb;
        for (uint32_t k = 0; k < t->left; k++) {
            te += rd_uv(&r);
            rd_zz(&r);
            rd_zz(&r);
            if (t->locked) rd_zz(&r);
        }
        t->tend = (uint32_t) te;
    }
    for (uint32_t i = 0; i < w->nnoise; i++) nt_open(w, &w->noi[i], i);
    for (uint32_t i = 0; i < w->nfv; i++) fv_open(w, &w->fv[i]);
    w->ngrain = w->info.ngrain;
    w->gcur = w->grains;
    w->gcur_i = 0;
    w->gcur_t0 = 0;
    w->rblk = UINT64_MAX;
    if (w->res) res_rewind(w);
    return 0;
}

uint64_t zxva_frames(const zxva_work_t *w)
{
    return w ? w->frames : 0;
}

static void mix_block(zxva_work_t *w, uint64_t N, uint32_t L)
{
    uint32_t nc = w->info.nch;
    for (uint32_t i = 0; i < L * nc; i++) w->acc[i] = 0;
    if (w->flags & ZXVA_L_PARTIAL) r_partials(w, N, L);
    if (w->flags & ZXVA_L_NOISE) r_noise(w, N, L);
    if (w->flags & ZXVA_L_GRAIN) r_grains(w, N, L);
    if (w->flags & ZXVA_L_VOICE)
        for (uint32_t i = 0; i < w->nvoice; i++) r_voice(w, w->voice[i], N, L);
    if (w->flags & ZXVA_L_FVOICE) r_fvoices(w, N, L);
    if (w->norm_g)
        for (uint32_t i = 0; i < L * nc; i++) w->acc[i] = mulsh(w->acc[i], (int64_t) w->norm_g, 32);
}

void zxva_quantise(const int64_t *acc, int32_t *out, uint32_t nframes, uint32_t nch, uint32_t bits,
                   bool dither, uint32_t seed, uint64_t frame)
{
    int s = 41 - (int) bits;
    int64_t hi = ((int64_t) 1 << (bits - 1)) - 1, lo = -hi - 1, half = (int64_t) 1 << (s - 1);
    uint64_t key = hkey(seed, 0xD1u, bits), msk = ((uint64_t) 1 << s) - 1;
    for (uint32_t i = 0; i < nframes * nch; i++) {
        int64_t x = clamp(acc[i], -((int64_t) 1 << 61), (int64_t) 1 << 61) + half;
        if (dither) {
            uint64_t h = mix64(key + ((frame + i / nch) * nch + i % nch) * GOLD);
            x += (int64_t) (h & msk) + (int64_t) ((h >> 32) & msk) - ((int64_t) 1 << s);
        }
        out[i] = (int32_t) clamp(sar(x, s), lo, hi);
    }
}

int32_t zxva_render_acc(zxva_work_t *w, uint64_t frame, int64_t *acc, uint32_t nframes)
{
    if (!w || !w->d || !acc || nframes > INT32_MAX) return ZXVA_EARG;
    uint32_t nc = w->info.nch;
    for (uint32_t done = 0; done < nframes;) {
        uint32_t L = nframes - done < ZXVA_BLOCK ? nframes - done : ZXVA_BLOCK;
        mix_block(w, frame + done, L);
        for (uint32_t i = 0; i < L * nc; i++) acc[(uint64_t) done * nc + i] += w->acc[i];
        done += L;
    }
    return (int32_t) nframes;
}

int32_t zxva_render(zxva_work_t *w, uint64_t frame, int32_t *out, uint32_t nframes)
{
    if (!w || !w->d || !out || nframes > INT32_MAX) return ZXVA_EARG;
    uint32_t nc = w->info.nch;
    bool res = (w->flags & ZXVA_F_RESIDUAL) != 0;
    bool dith = (w->flags & ZXVA_F_DITHER) && !res;
    for (uint32_t done = 0; done < nframes;) {
        uint32_t L = nframes - done < ZXVA_BLOCK ? nframes - done : ZXVA_BLOCK;
        uint64_t N = frame + done;
        mix_block(w, N, L);
        int32_t *o = out + (uint64_t) done * nc;
        zxva_quantise(w->acc, o, L, nc, w->bits, dith, w->info.seed, N);
        if (res) {
            int64_t hi = ((int64_t) 1 << (w->bits - 1)) - 1;
            for (uint32_t f = 0; f < L; f++) {
                uint64_t n = N + f;
                if (n >= w->info.res_frames) break;
                res_load(w, n / ZXVA_RES_BLOCK);
                for (uint32_t c = 0; c < nc; c++) {
                    int64_t v = (int64_t) o[f * nc + c] + w->rbuf[(n % ZXVA_RES_BLOCK) * nc + c];
                    o[f * nc + c] = (int32_t) clamp(v, -hi - 1, hi);
                }
            }
        }
        done += L;
    }
    return (int32_t) nframes;
}
