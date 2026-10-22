/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zxva.c — ZXVA vector audio: oscillator accuracy, rate independence,
 * band limiting, determinism, the lossless residual, FVOICE frame semantics,
 * streaming, fail-closed validation and fuzzing (zxva.h A1..A7). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "zxva.h"
#include "test/zxva_fixtures.inc"

static int fails, checks;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            printf("  FAIL (%s:%d) ", __FILE__, __LINE__);                                         \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

/* a little double maths so the test needs no libm (host test only) */
#define PI 3.14159265358979323846
static double floor_(double x)
{
    double t = (double) (long long) x;
    return t > x ? t - 1 : t;
}
static double sin_(double x)
{
    x -= 2 * PI * floor_(x / (2 * PI) + 0.5);
    double t = x, s = x;
    for (int k = 1; k < 16; k++) {
        t *= -x * x / ((2 * k) * (2 * k + 1));
        s += t;
    }
    return s;
}
static double log10_(double x) /* enough for printing dB */
{
    int e = 0;
    while (x >= 10) {
        x /= 10;
        e++;
    }
    while (x < 1) {
        x *= 10;
        e--;
    }
    double y = (x - 1) / (x + 1), y2 = y * y, s = 0, t = y;
    for (int k = 1; k < 60; k += 2) {
        s += t / k;
        t *= y2;
    }
    return e + 2 * s / 2.302585092994046;
}
static double db(double p)
{
    return 10 * log10_(p);
}

static zxva_work_t W, W2;
#define MAXF 96000
static int32_t A[MAXF * 2], B[MAXF * 2];
static int64_t ACC[MAXF * 2];

static uint32_t fnv(const int32_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 4; k++) h = (h ^ ((uint32_t) p[i] >> (8 * k) & 0xFFu)) * 16777619u;
    return h;
}
static int64_t render_all(zxva_work_t *w, const uint8_t *d, uint32_t n, uint32_t rate,
                          uint32_t bits, uint32_t flags, int32_t *out)
{
    if (zxva_open(w, d, n, rate, bits, flags) < 0) return -1;
    uint64_t f = zxva_frames(w);
    if (f > MAXF) return -1;
    zxva_render(w, 0, out, (uint32_t) f);
    return (int64_t) f;
}

/* ---- oscillator -------------------------------------------------------- */
static void test_oscillator(void)
{
    double se = 0, ss = 0, mx = 0;
    uint64_t p = 0x0123456789ABCDEFull;
    for (int i = 0; i < 400000; i++) {
        p = p * 6364136223846793005ull + 1442695040888963407ull;
        double ideal = sin_(2 * PI * ((double) (p >> 11) / 9007199254740992.0));
        double e = zxva_sin_q30(p) / 1073741824.0 - ideal;
        se += e * e;
        ss += ideal * ideal;
        if (e < 0) e = -e;
        if (e > mx) mx = e;
    }
    double snr = db(ss / se);
    printf("  oscillator: SNR %.1f dB, peak error %.2e (%.1f dB) over 400000 random phases\n", snr,
           mx, db(mx * mx));
    CHECK(snr > 160, "sine SNR %.1f dB", snr);
    CHECK(zxva_sin_q30(0) == 0 && zxva_sin_q30(1ull << 62) == (1 << 30) &&
              zxva_sin_q30(3ull << 62) == -(1 << 30),
          "sine is exact at 0, 1/4 and 3/4 turn");

    /* THD+N of a rendered partial (997 Hz, -6.02 dBFS) at 96 kHz / 32 bit,
     * against the ideal sine computed in double */
    int64_t n = render_all(&W, ZXVA_TONE, sizeof ZXVA_TONE, 96000, 32, 0, A);
    CHECK(n == 9600, "tone frames %lld", (long long) n);
    se = ss = 0;
    for (int64_t i = 0; i < n; i++) {
        double ideal = 0.5 * 2147483648.0 * sin_(2 * PI * 997.0 * (double) i / 96000.0);
        double e = A[i] - ideal;
        se += e * e;
        ss += ideal * ideal;
    }
    double thdn = db(se / ss);
    printf("  rendered partial: THD+N %.1f dB at 96 kHz / 32 bit\n", thdn);
    CHECK(thdn < -120, "THD+N %.1f dB", thdn);
}

/* ---- primitives -------------------------------------------------------- */
static void test_primitives(void)
{
    CHECK(zxva_code_amp(6144) == (1 << 30) && zxva_code_amp(6144 - 256) == (1 << 29) &&
              zxva_code_amp(0) == 0,
          "level codes: 0 dB, -6.02 dB exact, silence");
    CHECK(zxva_exp2_q30((int64_t) 3 << 32) == (int64_t) 8 << 30, "exp2(3) = 8");
    int64_t r = zxva_pow_q30(1u << 29, 1, 2); /* sqrt(1/2) */
    CHECK(r > 759250120 && r < 759250130, "pow(1/2, 1/2) = %lld", (long long) r);
    CHECK(zxva_log2_q32(1u << 30) == 0 && zxva_log2_q32(1ull << 33) == (int64_t) 3 << 32,
          "log2 exact at powers of two");
    CHECK(zxva_lcg(12345, 1664525, 1013904223) == 12345u * 1664525u + 1013904223u, "LCG step");
    CHECK(zxva_onepole(0, 65536, 16384) == 16384 && zxva_onepole(0, -3, 16384) == -1,
          "one-pole: y + floor((x - y) a / 2^16)");
    CHECK(zxva_decay(1 << 30, (1 << 30) - 1) == (1 << 30) - 1 && zxva_decay(-5, 1 << 29) == -3,
          "Q30 decay is a floor product");
}

/* ---- rate independence (A3) ------------------------------------------- */
static void test_rates(void)
{
    int64_t n48 = render_all(&W, ZXVA_TONE, sizeof ZXVA_TONE, 48000, 32, 0, A);
    int64_t n96 = render_all(&W, ZXVA_TONE, sizeof ZXVA_TONE, 96000, 32, 0, B);
    CHECK(n96 == 2 * n48, "96 kHz has twice the frames");
    int64_t md = 0;
    for (int64_t i = 0; i < n48; i++) {
        int64_t d = (int64_t) B[2 * i] - A[i];
        if (d < 0) d = -d;
        if (d > md) md = d;
    }
    printf("  tone: 96 kHz even samples vs 48 kHz differ by at most %lld LSB of 32 bit\n",
           (long long) md);
    CHECK(md <= 4, "tone 48k vs 96k: %lld", (long long) md);

    /* the chime's closed-form layers: the same continuous signal sampled twice */
    uint32_t L = ZXVA_L_PARTIAL | ZXVA_L_GRAIN | ZXVA_L_VOICE;
    n48 = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, L, A);
    n96 = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 96000, 24, L, B);
    md = 0;
    for (int64_t i = 0; i < n48 * 2; i++) {
        int64_t d = (int64_t) B[(i / 2) * 4 + (i % 2)] - A[i];
        if (d < 0) d = -d;
        if (d > md) md = d;
    }
    printf("  chime partials+grains+voices: 96 kHz vs 48 kHz at most %lld LSB of 24 bit\n",
           (long long) md);
    CHECK(md <= 2, "chime 48k vs 96k: %lld", (long long) md);
    /* noise and frame voices: same content (power), different samples */
    uint32_t Ls[2] = {ZXVA_L_NOISE, ZXVA_L_FVOICE};
    const char *nm[2] = {"noise", "frame voices"};
    for (int k = 0; k < 2; k++) {
        n48 = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, Ls[k], A);
        n96 = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 96000, 24, Ls[k], B);
        double p48 = 0, p96 = 0;
        for (int64_t i = 0; i < n48 * 2; i++) p48 += (double) A[i] * A[i];
        for (int64_t i = 0; i < n96 * 2; i++) p96 += (double) B[i] * B[i];
        double d = db(p96 / (2 * p48));
        printf("  chime %s: power at 96 kHz vs 48 kHz %+.2f dB\n", nm[k], d);
        CHECK(d > -0.5 && d < 0.5, "%s power differs by %.2f dB", nm[k], d);
    }
}

/* ---- band limiting (A4) ----------------------------------------------- */
static double goertzel_amp(const int32_t *x, int64_t n, double f, double rate)
{
    double re = 0, im = 0;
    for (int64_t i = 0; i < n; i++) {
        double ph = 2 * PI * f * (double) i / rate;
        re += x[i] * sin_(ph + PI / 2);
        im += x[i] * sin_(ph);
    }
    re /= 2147483648.0;
    im /= 2147483648.0;
    double m2 = (re * re + im * im) * 4 / ((double) n * n), a = m2, g = 1;
    for (int i = 0; i < 60; i++) g = 0.5 * (g + a / g); /* sqrt */
    return m2 > 0 ? g : 0;
}
static void test_nyquist(void)
{
    int64_t n = render_all(&W, ZXVA_NYQ, sizeof ZXVA_NYQ, 48000, 32, 0, A);
    double a10 = goertzel_amp(A, n, 10000, 48000), a235 = goertzel_amp(A, n, 23500, 48000);
    double a18 = goertzel_amp(A, n, 18000, 48000); /* where 30 kHz would alias */
    printf("  48 kHz: 10 kHz %.6f, 23.5 kHz %.1f dB, 30 kHz alias at 18 kHz %.1f dB\n", a10,
           db(a235 * a235 + 1e-30), db(a18 * a18 + 1e-30));
    CHECK(a10 > 0.2499 && a10 < 0.2501, "10 kHz kept at 0.25 (%f)", a10);
    CHECK(a235 < 1e-7, "23.5 kHz is above 0.48 R and not synthesised");
    CHECK(a18 < 1e-7, "30 kHz does not alias to 18 kHz");
    n = render_all(&W, ZXVA_NYQ, sizeof ZXVA_NYQ, 96000, 32, 0, A);
    double b235 = goertzel_amp(A, n, 23500, 96000), b30 = goertzel_amp(A, n, 30000, 96000);
    CHECK(b235 > 0.2499 && b235 < 0.2501 && b30 > 0.2499 && b30 < 0.2501,
          "at 96 kHz 23.5 and 30 kHz are present (%f %f)", b235, b30);
}

/* ---- determinism (A2) -------------------------------------------------- */
static void test_determinism(void)
{
    uint32_t h[2], h2;
    for (int k = 0; k < 2; k++) {
        int64_t n = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 16, ZXVA_F_DITHER, A);
        h[k] = fnv(A, (size_t) n * 2);
    }
    int64_t n = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 44100, 24, 0, A);
    h2 = fnv(A, (size_t) n * 2);
    printf("  chime hashes: 48k/16 dithered %08x, 44.1k/24 %08x\n", h[0], h2);
    CHECK(h[0] == h[1], "the same render twice is the same");
    CHECK(h[0] == 0x6cb97201u, "A2: 48k/16 dithered hash is the reference (%08x)", h[0]);
    CHECK(h2 == 0x4aba8a3bu, "A2: 44.1k/24 hash is the reference (%08x)", h2);
}

/* ---- the lossless residual -------------------------------------------- */
static void test_residual(void)
{
    zxva_info_t in;
    CHECK(zxva_check(ZXVA_RES, sizeof ZXVA_RES, &in) == 0 && in.res_rate == 48000 &&
              in.res_bits == 16 && in.res_frames == 2400,
          "residual fixture: 48 kHz / 16 bit / 2400 frames");
    int64_t n = render_all(&W, ZXVA_RES, sizeof ZXVA_RES, 48000, 16, ZXVA_F_RESIDUAL, A);
    int same = 0;
    for (int64_t i = 0; i < n; i++) same += A[i] == ZXVA_RES_PCM[i];
    CHECK(n == 2400 && same == 2400, "ZXVA + residual is sample-exact: %d / 2400", same);
    /* in odd blocks, and from the middle */
    zxva_open(&W2, ZXVA_RES, sizeof ZXVA_RES, 48000, 16, ZXVA_F_RESIDUAL);
    same = 0;
    for (uint32_t at = 1500, k = 0; at < 2400; at += 37 + k, k++) {
        uint32_t m = 2400 - at < 37 + k ? 2400 - at : 37 + k;
        zxva_render(&W2, at, B, m);
        for (uint32_t i = 0; i < m; i++) same += B[i] == ZXVA_RES_PCM[at + i];
    }
    zxva_render(&W2, 0, B, 1500);
    for (int i = 0; i < 1500; i++) same += B[i] == ZXVA_RES_PCM[i];
    CHECK(same == 2400, "residual seeks back and forth exactly (%d)", same);
    n = render_all(&W, ZXVA_RES, sizeof ZXVA_RES, 48000, 16, 0, A);
    double se = 0, ss = 0;
    for (int64_t i = 0; i < n; i++) {
        double e = (double) A[i] - ZXVA_RES_PCM[i];
        se += e * e;
        ss += (double) ZXVA_RES_PCM[i] * ZXVA_RES_PCM[i];
    }
    printf("  residual fixture: parametric alone %.1f dB SNR, %u bytes with residual vs %u PCM\n",
           db(ss / se), (unsigned) sizeof ZXVA_RES, 2400u * 2);
    CHECK(zxva_open(&W, ZXVA_RES, sizeof ZXVA_RES, 44100, 16, ZXVA_F_RESIDUAL) == ZXVA_EARG,
          "the residual only applies at its native rate");
    CHECK(zxva_open(&W, ZXVA_RES, sizeof ZXVA_RES, 48000, 24, ZXVA_F_RESIDUAL) == ZXVA_EARG,
          "and at its native depth");
    CHECK(zxva_open(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 16, ZXVA_F_RESIDUAL) == ZXVA_EARG,
          "a file without a residual refuses the flag");
}

/* ---- FVOICE: the documented frame semantics, re-implemented ----------- */
typedef struct {
    uint8_t b[2048];
    uint32_t n;
} bld_t;
static void b8(bld_t *b, uint32_t v)
{
    b->b[b->n++] = (uint8_t) v;
}
static void buv(bld_t *b, uint64_t v)
{
    while (v >= 0x80) {
        b8(b, (uint32_t) (v & 0x7F) | 0x80);
        v >>= 7;
    }
    b8(b, (uint32_t) v);
}
static void bzz(bld_t *b, int64_t v)
{
    buv(b, v < 0 ? ((uint64_t) (-(v + 1)) << 1) | 1 : (uint64_t) v << 1);
}
static void bhead(bld_t *b, uint32_t nch, uint32_t dur)
{
    b->n = 0;
    b8(b, 'Z');
    b8(b, 'X');
    b8(b, 'V');
    b8(b, 'A');
    b8(b, 1);
    b8(b, 0);
    buv(b, nch);
    buv(b, dur);
    buv(b, 77);
}

#ifdef __SIZEOF_INT128__
typedef __int128 i128;
static int64_t flr(i128 v, int s) /* floor(v / 2^s) */
{
    i128 d = (i128) 1 << s;
    i128 q = v / d;
    if (v % d != 0 && v < 0) q--;
    return (int64_t) q;
}
typedef struct {
    int kind, env, A, Rl, len, start, necho, delay, mirror, ch, np;
    int64_t dec, g0, g1, egain;
    uint32_t f[2], amp[2], pa[2];
    int64_t pdec[2];
    uint32_t seed, mul, add, alp, ahp, nam;
    uint32_t f0, glide;
} fvref_t;
/* v(i) for i = 0..len-1, straight from the zxva.h text */
static void fv_ref(const fvref_t *p, int64_t *v)
{
    uint64_t ph[2] = {0, 0}, inc[2];
    int64_t e[2], E = p->A ? 0 : (1 << 30), lp = 0, hp = 0;
    uint32_t s = p->seed;
    for (int k = 0; k < 2; k++) {
        uint32_t f = p->kind == 2 ? p->f0 : p->f[k];
        inc[k] = (uint64_t) (((unsigned __int128) f << 64) / (1000u * 48000u));
        e[k] = p->pa[k] ? 0 : (1 << 30);
    }
    for (int i = 0; i < p->len; i++) {
        int64_t x = 0;
        if (p->kind == 0) {
            for (int k = 0; k < p->np; k++) {
                int64_t xk = flr((i128) zxva_sin_q30(ph[k]) * p->amp[k], 14);
                x += flr((i128) xk * e[k], 30);
                ph[k] += inc[k];
                if (i + 1 < (int) p->pa[k])
                    e[k] = (int64_t) (((uint64_t) (i + 1) << 30) / p->pa[k]);
                else if (i + 1 == (int) p->pa[k])
                    e[k] = 1 << 30;
                else
                    e[k] = flr((i128) e[k] * p->pdec[k], 30);
            }
        } else if (p->kind == 1) {
            s = s * p->mul + p->add;
            int64_t xs = (int64_t) s - ((s >> 31) ? ((int64_t) 1 << 32) : 0);
            lp += flr((i128) (flr(xs, 1) - lp) * p->alp, 16);
            int64_t w = lp;
            if (p->ahp) {
                hp += flr((i128) (w - hp) * p->ahp, 16);
                w -= hp;
            }
            x = flr((i128) w * p->nam, 14);
        } else {
            x = flr((i128) zxva_sin_q30(ph[0]) * p->nam, 14);
            ph[0] += inc[0];
            unsigned __int128 ni = ((unsigned __int128) inc[0] * p->glide) >> 30;
            inc[0] = ni > ((unsigned __int128) 1 << 63) ? (uint64_t) 1 << 63 : (uint64_t) ni;
        }
        int64_t env = 1 << 30;
        if (p->env == ZXVA_ENV_ASR) {
            env = i < p->A ? (int64_t) (((uint64_t) i << 30) / (uint64_t) p->A) : (1 << 30);
            if (p->Rl && i >= p->len - p->Rl) {
                int64_t rr = (int64_t) (((uint64_t) (p->len - i) << 30) / (uint64_t) p->Rl);
                if (rr < env) env = rr;
            }
        } else if (p->env == ZXVA_ENV_HALFSINE) {
            env = zxva_sin_q30((uint64_t) (((unsigned __int128) i << 63) / (unsigned) p->len));
        } else if (p->env == ZXVA_ENV_AD) {
            env = E;
        }
        v[i] = flr((i128) x * env, 30);
        if (p->env == ZXVA_ENV_AD) {
            if (i + 1 < p->A)
                E = (int64_t) (((uint64_t) (i + 1) << 30) / (uint64_t) p->A);
            else if (i + 1 == p->A)
                E = 1 << 30;
            else
                E = flr((i128) E * p->dec, 30);
        }
    }
}
static void fv_emit(bld_t *b, const fvref_t *p)
{
    b8(b, ZXVA_FVOICE);
    buv(b, 48000);
    buv(b, (uint64_t) p->start);
    buv(b, (uint64_t) p->len);
    b8(b, (uint32_t) p->kind);
    b8(b, (uint32_t) p->ch);
    bzz(b, p->g0);
    bzz(b, p->g1);
    b8(b, (uint32_t) p->env);
    buv(b, p->env == ZXVA_ENV_ASR || p->env == ZXVA_ENV_AD ? (uint64_t) p->A : 0);
    buv(b, p->env == ZXVA_ENV_ASR  ? (uint64_t) p->Rl
           : p->env == ZXVA_ENV_AD ? (uint64_t) p->dec
                                   : 0);
    b8(b, (uint32_t) p->necho);
    if (p->necho) {
        buv(b, (uint64_t) p->delay);
        buv(b, (uint64_t) p->egain);
        b8(b, (uint32_t) p->mirror);
    }
    if (p->kind == 0) {
        b8(b, (uint32_t) p->np);
        for (int k = 0; k < p->np; k++) {
            buv(b, p->f[k]);
            buv(b, p->amp[k]);
            buv(b, p->pa[k]);
            buv(b, (uint64_t) p->pdec[k]);
        }
    } else if (p->kind == 1) {
        buv(b, p->seed);
        buv(b, p->mul);
        buv(b, p->add);
        buv(b, p->alp);
        buv(b, p->ahp);
        buv(b, p->nam);
    } else {
        buv(b, p->f0);
        buv(b, p->glide);
        buv(b, p->nam);
    }
}
static void test_fvoice(void)
{
    fvref_t V[3];
    memset(V, 0, sizeof V);
    /* tone: two partials, ASR, two mirrored echoes */
    V[0].kind = 0;
    V[0].env = ZXVA_ENV_ASR;
    V[0].A = 96;
    V[0].Rl = 1200;
    V[0].len = 4800;
    V[0].start = 100;
    V[0].necho = 2;
    V[0].delay = 1500;
    V[0].egain = 600000000;
    V[0].mirror = 1;
    V[0].g0 = 700000000;
    V[0].g1 = 300000000;
    V[0].np = 2;
    V[0].f[0] = 880000;
    V[0].amp[0] = 9000;
    V[0].pa[0] = 48;
    V[0].pdec[0] = 1073700000;
    V[0].f[1] = 2637000;
    V[0].amp[1] = 4000;
    V[0].pa[1] = 0;
    V[0].pdec[1] = 1073000000;
    /* noise: LCG, low-pass, high-pass, attack + Q30 decay */
    V[1].kind = 1;
    V[1].env = ZXVA_ENV_AD;
    V[1].A = 30;
    V[1].dec = 1073400000;
    V[1].len = 3000;
    V[1].start = 2000;
    V[1].g0 = 400000000;
    V[1].g1 = 500000000;
    V[1].seed = 99;
    V[1].mul = 1103515245;
    V[1].add = 12345;
    V[1].alp = 20000;
    V[1].ahp = 2500;
    V[1].nam = 12000;
    /* sweep: exponential glide under a half sine, mono gain on ch 1 */
    V[2].kind = 2;
    V[2].env = ZXVA_ENV_HALFSINE;
    V[2].len = 6000;
    V[2].start = 3000;
    V[2].ch = 1;
    V[2].g0 = (int64_t) 1 << 30;
    V[2].f0 = 200000;
    V[2].glide = 1073800000;
    V[2].nam = 10000;
    static bld_t b;
    bhead(&b, 2, 250000);
    for (int k = 0; k < 3; k++) fv_emit(&b, &V[k]);
    b8(&b, ZXVA_END);
    CHECK(zxva_check(b.b, b.n, 0) == 0, "FVOICE file is valid");
    int64_t n = render_all(&W, b.b, b.n, 48000, 16, 0, A);
    static int64_t bus[12000 * 2], v[8000];
    memset(bus, 0, sizeof bus);
    for (int k = 0; k < 3; k++) {
        fv_ref(&V[k], v);
        int64_t gj = 1 << 30;
        for (int j = 0; j <= V[k].necho; j++) {
            if (j) gj = flr((i128) gj * V[k].egain, 30);
            int64_t G0 = flr((i128) V[k].g0 * gj, 30), G1 = flr((i128) V[k].g1 * gj, 30);
            if (V[k].mirror && (j & 1)) {
                int64_t t = G0;
                G0 = G1;
                G1 = t;
            }
            for (int i = 0; i < V[k].len; i++) {
                int64_t f = V[k].start + j * V[k].delay + i;
                bus[f * 2 + V[k].ch] += flr((i128) v[i] * G0, 20);
                if (G1) bus[f * 2 + V[k].ch + 1] += flr((i128) v[i] * G1, 20);
            }
        }
    }
    int same = 0;
    for (int64_t i = 0; i < n * 2; i++) {
        int64_t y = flr((i128) bus[i] + (1 << 24), 25);
        if (y > 32767) y = 32767;
        if (y < -32768) y = -32768;
        same += A[i] == y;
    }
    CHECK(n == 12000 && same == 24000, "FVOICE is bit-exact to its documented semantics (%d/24000)",
          same);
    /* at 96 kHz: converted in integer maths, same content */
    int64_t n96 = render_all(&W, b.b, b.n, 96000, 16, 0, B);
    double p48 = 0, p96 = 0;
    for (int64_t i = 0; i < n * 2; i++) p48 += (double) A[i] * A[i];
    for (int64_t i = 0; i < n96 * 2; i++) p96 += (double) B[i] * B[i];
    printf("  FVOICE: bit-exact at 48 kHz; at 96 kHz power %+.2f dB\n", db(p96 / (2 * p48)));
    CHECK(n96 == 24000 && db(p96 / (2 * p48)) > -0.5 && db(p96 / (2 * p48)) < 0.5,
          "FVOICE at another rate keeps its power");
}
#else
static void test_fvoice(void)
{
    printf("  FVOICE reference skipped: no 128-bit integer type on this compiler\n");
}
#endif

/* ---- streaming (A5) ---------------------------------------------------- */
static void test_stream(void)
{
    int64_t n = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, ZXVA_F_DITHER, A);
    static const uint32_t SZ[] = {1, 7, 128, 333, 1000, 4097};
    for (int s = 0; s < 6; s++) {
        zxva_open(&W2, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, ZXVA_F_DITHER);
        for (int64_t at = 0; at < n; at += SZ[s]) {
            uint32_t m = n - at < SZ[s] ? (uint32_t) (n - at) : SZ[s];
            zxva_render(&W2, (uint64_t) at, B + at * 2, m);
        }
        CHECK(!memcmp(A, B, (size_t) n * 2 * sizeof A[0]), "blocks of %u equal one render", SZ[s]);
    }
    zxva_open(&W2, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, ZXVA_F_DITHER);
    uint32_t x = 1;
    for (int64_t at = 0; at < n;) {
        x = x * 1103515245u + 12345u;
        uint32_t m = 1 + (x >> 16) % 900;
        if (m > n - at) m = (uint32_t) (n - at);
        zxva_render(&W2, (uint64_t) at, B + at * 2, m);
        at += m;
    }
    CHECK(!memcmp(A, B, (size_t) n * 2 * sizeof A[0]), "random block sizes equal one render");
    /* seeks: closed-form layers and frame voices are exact anywhere */
    uint32_t L = ZXVA_L_ALL & ~ZXVA_L_NOISE;
    n = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, L, A);
    zxva_open(&W2, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, L);
    zxva_render(&W2, 9000, B, 3000);
    zxva_render(&W2, 2000, B + 3000 * 2, 4000);
    zxva_render(&W2, 6000, B + 7000 * 2, 500);
    CHECK(!memcmp(B, A + 9000 * 2, 3000 * 8) && !memcmp(B + 6000, A + 2000 * 2, 4000 * 8) &&
              !memcmp(B + 14000, A + 6000 * 2, 500 * 8),
          "seeking backwards and forwards is exact without noise");
    /* noise continues exactly over a forward jump within its pre-roll */
    n = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, ZXVA_L_NOISE, A);
    zxva_open(&W2, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, ZXVA_L_NOISE);
    zxva_render(&W2, 0, B, 1000);
    zxva_render(&W2, 2000, B + 2000 * 2, 3000);
    CHECK(!memcmp(B + 4000, A + 4000, 3000 * 8), "noise is exact over a short forward jump");
}

/* ---- loudness normalisation and mixing -------------------------------- */
static void test_norm_mix(void)
{
    int64_t n = render_all(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, 0, A);
    render_all(&W2, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, ZXVA_F_NONORM, B);
    int bad = 0;
    for (int64_t i = 0; i < n * 2; i++) {
        int64_t want = ((int64_t) B[i] * 3) / 4, d = A[i] - want;
        if (d > 1 || d < -1) bad++;
    }
    CHECK(bad == 0, "NORM scales the mix by target/peak = 0.75 (%d off)", bad);
    memset(ACC, 0, (size_t) n * 2 * sizeof ACC[0]);
    zxva_open(&W, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, 0);
    zxva_render_acc(&W, 0, ACC, (uint32_t) n);
    zxva_quantise(ACC, B, (uint32_t) n, 2, 24, false, 0, 0);
    CHECK(!memcmp(A, B, (size_t) n * 2 * sizeof A[0]), "render_acc + quantise equals render");
    /* two cues on one bus */
    zxva_open(&W2, ZXVA_CHIME, sizeof ZXVA_CHIME, 48000, 24, 0);
    zxva_render_acc(&W2, 0, ACC, (uint32_t) n);
    zxva_quantise(ACC, B, (uint32_t) n, 2, 24, false, 0, 0);
    int ok = 1;
    for (int64_t i = 0; i < n * 2; i++) {
        int64_t d = (int64_t) B[i] - 2 * (int64_t) A[i];
        if ((d > 1 || d < -1) && B[i] != 8388607 && B[i] != -8388608) ok = 0;
    }
    CHECK(ok, "two cues mixed on the Q40 bus sum before one quantisation");
}

/* ---- fail closed (A7) and fuzzing ------------------------------------- */
static uint8_t FZ[16384];
static void test_fuzz(void)
{
    const struct {
        const uint8_t *d;
        uint32_t n;
        const char *nm;
    } fx[] = {{ZXVA_CHIME, sizeof ZXVA_CHIME, "chime"},
              {ZXVA_TONE, sizeof ZXVA_TONE, "tone"},
              {ZXVA_NYQ, sizeof ZXVA_NYQ, "nyq"},
              {ZXVA_RES, sizeof ZXVA_RES, "res"}};
    for (int f = 0; f < 4; f++) {
        CHECK(zxva_check(fx[f].d, fx[f].n, 0) == 0, "%s fixture is valid", fx[f].nm);
        int bad = 0;
        uint32_t tried = 0;
        for (uint32_t k = 0; k < fx[f].n; k++) {
            /* every length near both ends; every 7th in the middle of big files */
            if (k > 1024 && k + 1024 < fx[f].n && k % 7) continue;
            tried++;
            if (zxva_check(fx[f].d, k, 0) >= 0) bad++;
        }
        CHECK(bad == 0, "%s: %u truncations are refused (%d accepted)", fx[f].nm, tried, bad);
        memcpy(FZ, fx[f].d, fx[f].n);
        FZ[fx[f].n] = 0;
        CHECK(zxva_check(FZ, fx[f].n + 1, 0) < 0, "%s: a trailing byte is refused", fx[f].nm);
    }
    /* random byte flips: either refused, or a valid file that renders */
    uint32_t x = 2463534242u, refused = 0, rendered = 0;
    for (int it = 0; it < 6000; it++) {
        int f = it % 4 == 3 && it % 8 != 7 ? 0 : it % 4;
        if (f == 3 && it % 40 != 3) f = 0; /* the residual fixture is big: fewer runs */
        memcpy(FZ, fx[f].d, fx[f].n);
        int nflip = 1 + it % 4;
        for (int k = 0; k < nflip; k++) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            FZ[x % fx[f].n] ^= (uint8_t) (1u << ((x >> 8) % 8));
        }
        if (zxva_check(FZ, fx[f].n, 0) < 0) {
            refused++;
            continue;
        }
        static const uint32_t RATES[] = {8000, 44100, 48000, 96000, 192000, 384000, 768000};
        uint32_t rate = RATES[(x >> 3) % 7], bits = (x >> 7) % 3 == 0   ? 16
                                                    : (x >> 7) % 3 == 1 ? 24
                                                                        : 32;
        if (zxva_open(&W, FZ, fx[f].n, rate, bits, ZXVA_F_DITHER) == 0) {
            uint64_t fr = zxva_frames(&W);
            zxva_render(&W, (x >> 5) % (fr + 1), A, 1024);
            rendered++;
        }
    }
    printf("  fuzz: 6000 mutated files, %u refused, %u valid and rendered\n", refused, rendered);
    CHECK(refused + rendered == 6000, "every mutation is refused or renders");
    /* hostile headers */
    static const uint8_t bad1[] = {'Z', 'X', 'V', 'A', 2, 0, 1, 1, 0, 0};
    static const uint8_t bad2[] = {'Z', 'X', 'V', 'A', 1, 0, 9, 1, 0, 0};
    static const uint8_t bad3[] = {'Z', 'X', 'V', 'A', 1, 0, 1, 0, 0, 0};
    static const uint8_t bad4[] = {'Z', 'X', 'V', 'A', 1, 0, 1, 1, 0, 0x7F};
    static const uint8_t ok1[] = {'Z', 'X', 'V', 'A', 1, 0, 1, 1, 0, 0};
    CHECK(zxva_check(bad1, sizeof bad1, 0) < 0 && zxva_check(bad2, sizeof bad2, 0) < 0 &&
              zxva_check(bad3, sizeof bad3, 0) < 0 && zxva_check(bad4, sizeof bad4, 0) < 0,
          "bad version, channel count, duration and chunk tag are refused");
    CHECK(zxva_check(ok1, sizeof ok1, 0) == 0, "an empty sound is valid");
    CHECK(zxva_check(0, 10, 0) < 0, "NULL is refused");
}

/* ---- API and caps ------------------------------------------------------ */
static void test_api(void)
{
    CHECK(zxva_open(&W, ZXVA_TONE, sizeof ZXVA_TONE, 7999, 16, 0) == ZXVA_EARG &&
              zxva_open(&W, ZXVA_TONE, sizeof ZXVA_TONE, 768001, 16, 0) == ZXVA_EARG &&
              zxva_open(&W, ZXVA_TONE, sizeof ZXVA_TONE, 48000, 20, 0) == ZXVA_EARG,
          "rates outside 8k..768k and depths other than 16/24/32 are refused");
    static const uint32_t RATES[] = {44100, 48000, 96000, 192000, 384000};
    int ok = 1;
    for (int r = 0; r < 5; r++) {
        if (zxva_open(&W, ZXVA_TONE, sizeof ZXVA_TONE, RATES[r], 24, 0) < 0 ||
            zxva_frames(&W) != (uint64_t) RATES[r] / 10)
            ok = 0;
    }
    CHECK(ok, "the same file opens at 44.1k, 48k, 96k, 192k and 384k");
    static uint8_t big[(ZXVA_MAX_TRACKS + 1) * 17 + 64];
    uint32_t n = 0;
    const uint8_t head[] = {'Z', 'X', 'V', 'A', 1, 0, 1, 0xA0, 0x8D, 0x06, 0}; /* 1 ch, 100 ms */
    memcpy(big, head, sizeof head);
    n = sizeof head;
    for (int i = 0; i <= ZXVA_MAX_TRACKS; i++) {
        /* PARTIAL ch 0, pan 0, free, t0 0, 2 bps, f 1000 Hz, code 0, ph 0,
         * dt 1000 us, df 0, dc 0 */
        const uint8_t p[] = {1, 0, 0, 0, 0, 2, 0x80, 0x80, 0xA0, 0x1F, 0, 0, 0, 0xE8, 0x07, 0, 0};
        memcpy(big + n, p, sizeof p);
        n += sizeof p;
    }
    big[n++] = 0;
    CHECK(zxva_check(big, n, 0) == ZXVA_ECAP, "more tracks than the build's cap: ZXVA_ECAP");
    CHECK(zxva_check(big, n - 9, 0) < 0, "and a cut file is still malformed");
}

int main(void)
{
    test_oscillator();
    test_primitives();
    test_rates();
    test_nyquist();
    test_determinism();
    test_residual();
    test_fvoice();
    test_stream();
    test_norm_mix();
    test_fuzz();
    test_api();
    printf("%s test_zxva: %d/%d checks\n", fails ? "[FAIL]" : "[PASS]", checks - fails, checks);
    return fails ? 1 : 0;
}
