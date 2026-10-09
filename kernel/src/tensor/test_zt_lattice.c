/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* test_zt_lattice.c — host tests for the E8 and Leech quantisers (T17).
 * Nearest points are checked against brute-force searches written here, and
 * the E8 codebook's error is compared with scalar quantisers on Gaussian-like
 * data. Build and run from kernel/:
 *   gcc -std=c11 -Wall -Werror -Wextra -O2 -Isrc/tensor src/tensor/test_zt_lattice.c \
 *       src/tensor/zt_lattice.c src/tensor/zt.c -lm -o /tmp/test_zt_lattice && /tmp/test_zt_lattice
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "zt.h"
#include "zt_lattice.h"

static int fails;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (!(c)) {                                                                                \
            fails++;                                                                               \
            if (fails < 50) {                                                                      \
                printf("FAIL %s:%d: ", __FILE__, __LINE__);                                        \
                printf(__VA_ARGS__);                                                               \
                printf("\n");                                                                      \
            }                                                                                      \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x9E3779B97F4A7C15ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static int32_t rnd_fx(int32_t range) /* uniform in [-range, range] */
{
    return (int32_t) (rnd() % (uint64_t) (2 * (int64_t) range + 1)) - range;
}
static zt_fx gauss(void) /* Irwin-Hall: 12 uniforms, mean 0, sigma 1.0 (Q16) */
{
    int64_t s = 0;
    for (int i = 0; i < 12; i++) s += (int64_t) (rnd() & 0xFFFF);
    return (zt_fx) (s - 6 * 65536);
}
static int popc(uint32_t w)
{
    int n = 0;
    for (; w; w &= w - 1) n++;
    return n;
}

/* ---- E8 ---------------------------------------------------------------- */

static int8_t book[ZT_E8_CODEBOOK * 8];

/* Exact squared distance from 2x to v s (doubled Q16 units). 128-bit here
 * (host test only) so that far brute-force candidates cannot overflow. */
typedef __int128 i128;
static i128 e8_dist(const zt_fx x[8], zt_fx s, const int8_t v[8])
{
    i128 d = 0;
    for (int i = 0; i < 8; i++) {
        i128 e = 2 * (int64_t) x[i] - (int64_t) v[i] * s;
        d += e * e;
    }
    return d;
}

/* Brute force: every lattice point with v_i in [T_i - 2, T_i + 3], T_i =
 * floor(2 x_i / s). The covering radius of E8 is 1 (2 doubled), so a nearest
 * point always lies in this box. */
static i128 e8_brute(const zt_fx x[8], zt_fx s)
{
    int64_t lo[8];
    i128 best = (i128) 1 << 120;
    for (int i = 0; i < 8; i++) lo[i] = (int64_t) floor(2.0 * x[i] / s) - 2;
    int8_t v[8];
    for (int p = 0; p < 2; p++) {
        int base[8];
        for (int i = 0; i < 8; i++) base[i] = (int) (lo[i] + (((lo[i] & 1) != p) ? 1 : 0));
        for (int c = 0; c < 6561; c++) { /* 3 values of parity p per coordinate */
            int cc = c;
            for (int i = 0; i < 8; i++) {
                v[i] = (int8_t) (base[i] + 2 * (cc % 3));
                cc /= 3;
            }
            if (!zt_e8_is_point(v)) continue;
            i128 d = e8_dist(x, s, v);
            if (d < best) best = d;
        }
    }
    return best;
}

static void test_e8_codebook(void)
{
    uint32_t n = zt_e8_codebook_build(book);
    CHECK(n == ZT_E8_CODEBOOK, "codebook size %u", n);
    static const uint32_t want[5] = {1, 240, 2160, 6720, 17520};
    uint32_t got[5] = {0}, bad = 0, order = 0;
    for (uint32_t i = 0; i < n; i++) {
        const int8_t *v = book + i * 8;
        bad += !zt_e8_is_point(v);
        int32_t nn = zt_e8_norm2(v);
        if (nn % 8 == 0 && nn <= 32) got[nn / 8]++;
        if (i) {
            const int8_t *u = v - 8;
            int32_t mu = zt_e8_norm2(u);
            int lex = 0;
            for (int j = 0; j < 8 && !lex; j++) lex = (v[j] > u[j]) - (v[j] < u[j]);
            order += !(nn > mu || (nn == mu && lex > 0));
        }
    }
    CHECK(bad == 0, "%u codebook entries are not lattice points", bad);
    CHECK(order == 0, "%u entries out of (shell, lexicographic) order", order);
    for (int k = 0; k < 5; k++) CHECK(got[k] == want[k], "shell %d: %u points", k, got[k]);
    uint32_t rt = 0;
    for (uint32_t i = 0; i < n; i++) {
        int8_t v[8];
        rt += !zt_e8_decode(book, i, v) || zt_e8_encode(book, v) != (int32_t) i;
    }
    CHECK(rt == 0, "%u encode/decode round trips failed", rt);
    int8_t v[8];
    CHECK(!zt_e8_decode(book, ZT_E8_CODEBOOK, v), "decode past the end");
    int8_t far[8] = {6, 2, 0, 0, 0, 0, 0, 0}; /* a lattice point of norm 10 */
    CHECK(zt_e8_is_point(far) && zt_e8_encode(book, far) == -1, "norm-10 point not in book");
    int8_t off[8] = {2, 0, 0, 0, 0, 0, 0, 0}; /* sum 2 mod 4 */
    CHECK(!zt_e8_is_point(off) && zt_e8_encode(book, off) == -1, "non-point rejected");
    int8_t mixed[8] = {1, 1, 1, 1, 1, 1, 1, 0};
    CHECK(!zt_e8_is_point(mixed), "mixed parity rejected");
    printf("  E8 codebook: %u points, shells %u/%u/%u/%u/%u, 15-bit index\n", n, got[0], got[1],
           got[2], got[3], got[4]);
}

static void test_e8_nearest(void)
{
    static const zt_fx scales[] = {3, 7, 1000, 65536, 65537, 1 << 20, ZT_E8_SCALE_MAX};
    int wrong = 0, trials = 0, notpt = 0;
    for (int t = 0; t < 4000; t++) {
        zt_fx s = t % 8 < 7 ? scales[t % 7] : 1 + (zt_fx) (rnd() % (1u << 24));
        int64_t lim = 60 * (int64_t) s;
        if (lim > (1 << 30)) lim = 1 << 30;
        int32_t range = (int32_t) (t % 3 == 0 ? (lim < 3 * s ? lim : 3 * s) : lim);
        zt_fx x[8];
        int8_t v[8];
        for (int i = 0; i < 8; i++) x[i] = rnd_fx(range);
        CHECK(zt_e8_nearest(x, s, v), "nearest returned false");
        notpt += !zt_e8_is_point(v);
        wrong += e8_dist(x, s, v) != e8_brute(x, s);
        trials++;
    }
    CHECK(notpt == 0, "%d outputs not lattice points", notpt);
    CHECK(wrong == 0, "%d of %d nearest points beaten by brute force", wrong, trials);
    /* Lattice points come back unchanged; the origin; saturation. */
    int self = 0;
    for (uint32_t i = 0; i < ZT_E8_CODEBOOK; i += 7) {
        zt_fx x[8];
        int8_t v[8];
        zt_e8_dequantize(book + i * 8, 2 * 4096, x);
        zt_e8_nearest(x, 2 * 4096, v);
        for (int j = 0; j < 8; j++) self += v[j] != book[i * 8 + j];
    }
    CHECK(self == 0, "lattice points not fixed by nearest");
    zt_fx big[8] = {INT32_MAX, INT32_MIN, INT32_MAX, INT32_MIN, 5, 0, -5, 1};
    int8_t v[8];
    zt_e8_nearest(big, 1, v);
    CHECK(zt_e8_is_point(v) && v[0] >= 124 && v[1] <= -124, "saturation");
    CHECK(!zt_e8_nearest(big, 0, v) && zt_e8_norm2(v) == 0, "scale 0");
    printf("  E8 nearest: %d random inputs match brute force\n", trials - wrong);
}

/* Normalised MSE of the E8 codebook with scale = gain * RMS per 32 values. */
static double e8_mse(const zt_fx *x, int n, double gain, double *shrunk)
{
    double se = 0;
    int nshr = 0;
    for (int b = 0; b < n; b += 32) {
        zt_fx rms = zt_e8_rms(x + b, 32);
        zt_fx s = gain > 0 ? (zt_fx) (rms * gain + 0.5) : zt_e8_scale(x + b, 32);
        for (int k = 0; k < 32; k += 8) {
            int8_t v[8], near[8];
            zt_fx y[8];
            uint16_t idx = zt_e8_quantize(book, x + b + k, s);
            zt_e8_decode(book, idx, v);
            zt_e8_nearest(x + b + k, s, near);
            nshr += zt_e8_norm2(near) > 32;
            zt_e8_dequantize(v, s, y);
            for (int i = 0; i < 8; i++) {
                double e = (double) (y[i] - x[b + k + i]) / 65536.0;
                se += e * e;
            }
        }
    }
    if (shrunk) *shrunk = nshr / (double) (n / 8);
    return se / n;
}

/* Uniform 2-bit mid-rise scalar quantiser, levels (q + 1/2) d, q in -2..1. */
static double scalar2_mse(const zt_fx *x, int n, double gain)
{
    double se = 0;
    for (int b = 0; b < n; b += 32) {
        double d = zt_e8_rms(x + b, 32) / 65536.0 * gain;
        for (int i = 0; i < 32; i++) {
            double v = x[b + i] / 65536.0, q = floor(v / d);
            if (q < -2) q = -2;
            if (q > 1) q = 1;
            double e = (q + 0.5) * d - v;
            se += e * e;
        }
    }
    return se / n;
}

static void test_e8_mse(void)
{
    enum { N = 1 << 16 };
    static zt_fx x[N], y[N];
    static zt_q8_t q8[N / 32];
    for (int i = 0; i < N; i++) x[i] = gauss();
    double var = 0;
    for (int i = 0; i < N; i++) var += (x[i] / 65536.0) * (x[i] / 65536.0);
    var /= N;
    printf("  MSE on %d Gaussian-like values (sigma^2 %.4f), one scale per 32:\n", N, var);
    double be = 1e9, bg = 0;
    printf("    E8 gain sweep:");
    for (double g = 0.90; g <= 1.501; g += 0.05) {
        double m = e8_mse(x, N, g, 0) / var;
        printf(" %.2f:%.4f", g, m);
        if (m < be) {
            be = m;
            bg = g;
        }
    }
    printf("\n");
    double bs = 1e9, bsg = 0;
    for (double g = 0.80; g <= 1.201; g += 0.01) {
        double m = scalar2_mse(x, N, g) / var;
        if (m < bs) {
            bs = m;
            bsg = g;
        }
    }
    double shr;
    double de = e8_mse(x, N, 0, &shr) / var;
    zt_quantize(x, N, q8, false);
    zt_dequantize(q8, N / 32, y);
    double s8 = 0;
    for (int i = 0; i < N; i++) {
        double e = (y[i] - x[i]) / 65536.0;
        s8 += e * e;
    }
    s8 /= N * var;
    printf("    E8 codebook, 1.875 bpw + scale, default gain %.2f: NMSE %.4f (%.2f dB SNR),"
           " %.1f%% of blocks shrunk\n",
           ZT_E8_RMS_GAIN / 65536.0, de, -10 * log10(de), 100 * shr);
    printf("    E8 codebook, best swept gain %.2f:            NMSE %.4f (%.2f dB)\n", bg, be,
           -10 * log10(be));
    printf("    uniform 2-bit scalar, 2.0 bpw + scale, best step %.2f rms: NMSE %.4f (%.2f dB)\n",
           bsg, bs, -10 * log10(bs));
    printf("    int8 zt_quantize, 8 bpw + scale (reference):    NMSE %.6f (%.2f dB)\n", s8,
           -10 * log10(s8));
    printf("    Gaussian rate-distortion bound at 1.875 bpw:      NMSE %.4f (%.2f dB)\n",
           pow(2, -2 * 1.875), 2 * 1.875 * 10 * log10(2.0));
    CHECK(de < bs, "E8 (%.4f) should beat 2-bit scalar (%.4f) at fewer bits", de, bs);
    CHECK(de < be * 1.02, "default gain within 2%% of the best swept");
    /* How often is quantize the nearest codebook point? Exhaustive scan. */
    int opt = 0, nb = 1500;
    double exc = 0, tot = 0;
    for (int b = 0; b < nb; b++) {
        const zt_fx *z = x + 8 * b;
        zt_fx s = zt_e8_scale(x + 32 * (b / 4), 32);
        int8_t v[8];
        zt_e8_decode(book, zt_e8_quantize(book, z, s), v);
        i128 dq = e8_dist(z, s, v), dm = dq;
        for (uint32_t i = 0; i < ZT_E8_CODEBOOK; i++) {
            i128 d = e8_dist(z, s, book + i * 8);
            if (d < dm) dm = d;
        }
        opt += dq == dm;
        exc += (double) (dq - dm);
        tot += (double) dm;
    }
    printf("    quantize = nearest codebook point in %.1f%% of %d blocks; excess distortion "
           "%.2f%%\n",
           100.0 * opt / nb, nb, 100 * exc / tot);
    /* every index decodes to an in-ball lattice point */
    int bad = 0;
    for (int i = 0; i < 4096; i++) {
        zt_fx z[8];
        int8_t v[8];
        for (int j = 0; j < 8; j++) z[j] = rnd_fx(20 * 65536);
        uint16_t idx = zt_e8_quantize(book, z, 65536);
        bad += !zt_e8_decode(book, idx, v) || zt_e8_norm2(v) > 32 || !zt_e8_is_point(v);
    }
    CHECK(bad == 0, "quantize left the codebook %d times", bad);
    zt_fx zero[8] = {0};
    CHECK(zt_e8_quantize(book, zero, 65536) == 0 && zt_e8_scale(zero, 8) == 0, "zero block");
}

/* ---- Golay and Leech --------------------------------------------------- */

static void test_golay(void)
{
    int dist[25] = {0}, bad = 0;
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t w = zt_golay_codeword(i);
        dist[popc(w)]++;
        bad += !zt_golay_is_codeword(w) || (w >> 24);
        bad += zt_golay_is_codeword(w ^ (1u << (i % 24)));
        bad += zt_golay_is_codeword(w ^ (1u << (i % 24)) ^ (1u << ((i * 7 + 3) % 24))) &&
               (i % 24) != (i * 7 + 3) % 24;
    }
    CHECK(bad == 0, "golay membership %d", bad);
    CHECK(dist[0] == 1 && dist[8] == 759 && dist[12] == 2576 && dist[16] == 759 && dist[24] == 1 &&
              dist[0] + dist[8] + dist[12] + dist[16] + dist[24] == 4096,
          "golay weights %d %d %d %d %d", dist[0], dist[8], dist[12], dist[16], dist[24]);
    printf("  Golay [24,12,8]: weights 0:%d 8:%d 12:%d 16:%d 24:%d\n", dist[0], dist[8], dist[12],
           dist[16], dist[24]);
}

static int8_t (*minv)[24]; /* the 196560 minimal vectors */
static int nminv;
static zt_leech_work_t lw;

static void build_minimal(int counts[3])
{
    minv = malloc(196560 * sizeof *minv);
    nminv = 0;
    counts[0] = counts[1] = counts[2] = 0;
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t w = zt_golay_codeword(i);
        if (popc(w) != 8) continue;
        for (uint32_t sg = 0; sg < 256; sg++) {
            if (popc(sg) & 1) continue;
            int k = 0;
            for (int j = 0; j < 24; j++) {
                int8_t v = 0;
                if ((w >> j) & 1u) v = ((sg >> k++) & 1u) ? -2 : 2;
                minv[nminv][j] = v;
            }
            nminv++;
            counts[0]++;
        }
    }
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t w = zt_golay_codeword(i);
        for (int p = 0; p < 24; p++) {
            for (int j = 0; j < 24; j++) {
                int v = j == p ? -3 : 1;
                minv[nminv][j] = (int8_t) (((w >> j) & 1u) ? -v : v);
            }
            nminv++;
            counts[1]++;
        }
    }
    for (int a = 0; a < 24; a++)
        for (int b = a + 1; b < 24; b++)
            for (int sg = 0; sg < 4; sg++) {
                for (int j = 0; j < 24; j++) minv[nminv][j] = 0;
                minv[nminv][a] = (sg & 1) ? -4 : 4;
                minv[nminv][b] = (sg & 2) ? -4 : 4;
                nminv++;
                counts[2]++;
            }
}

static int64_t leech_dist(const zt_fx x[24], zt_fx s, const int16_t v[24])
{
    int64_t d = 0;
    for (int i = 0; i < 24; i++) {
        int64_t e = 4 * (int64_t) x[i] - (int64_t) v[i] * s;
        d += e * e;
    }
    return d;
}

/* Independent slow reference: every coset, every coordinate searched
 * directly, every single +-4 fix tried. Returns the best distance. */
static int64_t leech_ref(const zt_fx x[24], zt_fx s)
{
    int64_t best = INT64_MAX;
    for (uint32_t i = 0; i < 4096; i++) {
        uint32_t w = zt_golay_codeword(i);
        for (int m = 0; m < 2; m++) {
            int64_t nv[24], cost = 0, sum = 0;
            for (int j = 0; j < 24; j++) {
                int c = m + 2 * (int) ((w >> j) & 1u);
                int64_t t = (int64_t) floor(4.0 * x[j] / s), bn = 0, bc = INT64_MAX;
                for (int64_t n = t - 4; n <= t + 4; n++) {
                    if ((((n % 4) + 4) % 4) != c) continue;
                    int64_t e = 4 * (int64_t) x[j] - n * s;
                    if (e * e < bc) {
                        bc = e * e;
                        bn = n;
                    }
                }
                nv[j] = bn;
                cost += bc;
                sum += bn;
            }
            if ((((sum % 8) + 8) % 8) != 4 * m) {
                int64_t bf = INT64_MAX;
                for (int j = 0; j < 24; j++)
                    for (int sg = -1; sg <= 1; sg += 2) {
                        int64_t e0 = 4 * (int64_t) x[j] - nv[j] * s, e1 = e0 - 4 * sg * s;
                        if (e1 * e1 - e0 * e0 < bf) bf = e1 * e1 - e0 * e0;
                    }
                cost += bf;
            }
            if (cost < best) best = cost;
        }
    }
    return best;
}

static void test_leech(void)
{
    int counts[3];
    clock_t t0 = clock();
    zt_leech_init(&lw);
    build_minimal(counts);
    CHECK(counts[0] == 97152 && counts[1] == 98304 && counts[2] == 1104 && nminv == 196560,
          "minimal vector counts %d %d %d", counts[0], counts[1], counts[2]);
    /* Every minimal vector: norm 32, a lattice point, decoded to itself both
     * exactly and after noise below half the minimal distance. */
    const zt_fx S = 4 * 65536; /* x[i] = v[i] in Q16 */
    int notpt = 0, badn = 0, notself = 0, noisy = 0, nnoisy = 0;
    for (int k = 0; k < nminv; k++) {
        int16_t v[24], o[24];
        zt_fx x[24];
        int n2 = 0;
        for (int j = 0; j < 24; j++) {
            v[j] = minv[k][j];
            n2 += v[j] * v[j];
        }
        badn += n2 != 32;
        notpt += !zt_leech_is_point(v);
        zt_leech_dequantize(v, S, x);
        zt_leech_nearest(&lw, x, S, o);
        for (int j = 0; j < 24; j++) notself += o[j] != v[j];
        if (k % 16 == 0) { /* |noise| < 0.3 per coordinate: norm < 2.2 < 8 */
            for (int j = 0; j < 24; j++) x[j] += rnd_fx(19660);
            zt_leech_nearest(&lw, x, S, o);
            for (int j = 0; j < 24; j++) noisy += o[j] != v[j];
            nnoisy++;
        }
    }
    CHECK(badn == 0, "%d minimal vectors of wrong norm", badn);
    CHECK(notpt == 0, "%d minimal vectors not recognised", notpt);
    CHECK(notself == 0, "%d minimal vector coordinates moved by decoding", notself);
    CHECK(noisy == 0, "%d coordinates wrong after noise", noisy);
    printf("  Leech minimal vectors: %d (97152 + 98304 + 1104), all points, all decode to "
           "themselves; %d noisy copies decoded (%.2f s)\n",
           nminv, nnoisy, (double) (clock() - t0) / CLOCKS_PER_SEC);
    /* Non-points */
    int16_t z[24] = {0}, o[24];
    CHECK(zt_leech_is_point(z), "0 is a point");
    z[0] = 1;
    CHECK(!zt_leech_is_point(z), "parity");
    z[0] = 2;
    CHECK(!zt_leech_is_point(z), "single 2 is not a codeword");
    z[0] = 4;
    CHECK(!zt_leech_is_point(z), "sum 4 mod 8 with m = 0");
    z[0] = 8;
    CHECK(zt_leech_is_point(z), "8 e_0");
    zt_fx zx[24] = {0};
    zt_leech_nearest(&lw, zx, S, o);
    int nz = 0;
    for (int j = 0; j < 24; j++) nz += o[j] != 0;
    CHECK(nz == 0, "0 decodes to 0");
    CHECK(!zt_leech_nearest(&lw, zx, 0, o), "scale 0");
    /* Random inputs: output is a point, no minimal-vector neighbour is
     * closer, and the distance equals the slow reference. */
    t0 = clock();
    int notp = 0, beaten = 0, refbad = 0, pairbad = 0, nrand = 600;
    for (int t = 0; t < nrand; t++) {
        zt_fx s = t % 3 == 0 ? S : 16 + (zt_fx) (rnd() % (1u << 20));
        zt_fx x[24];
        int32_t range = (int32_t) (t % 2 ? 2 * (int64_t) s : 12 * (int64_t) s);
        for (int j = 0; j < 24; j++) x[j] = rnd_fx(range);
        zt_leech_nearest(&lw, x, s, o);
        notp += !zt_leech_is_point(o);
        int64_t d[24];
        for (int j = 0; j < 24; j++) d[j] = 4 * (int64_t) x[j] - (int64_t) o[j] * s;
        /* pair moves (+-4, +-4) on every pair */
        for (int a = 0; a < 24; a++)
            for (int b = a + 1; b < 24; b++)
                for (int sg = 0; sg < 4; sg++) {
                    int64_t ea = (sg & 1) ? -4 : 4, eb = (sg & 2) ? -4 : 4;
                    pairbad += (d[a] * ea + d[b] * eb) > 16 * (int64_t) s;
                }
        /* all minimal vectors e: |d - e s|^2 >= |d|^2 iff d.e <= 16 s */
        if (t < 80)
            for (int k = 0; k < nminv; k++) {
                int64_t dot = 0;
                for (int j = 0; j < 24; j++) dot += d[j] * minv[k][j];
                beaten += dot > 16 * (int64_t) s;
            }
        if (t < 200) refbad += leech_dist(x, s, o) != leech_ref(x, s);
    }
    CHECK(notp == 0, "%d random outputs not lattice points", notp);
    CHECK(pairbad == 0, "%d pair moves closer", pairbad);
    CHECK(beaten == 0, "%d minimal-vector neighbours closer", beaten);
    CHECK(refbad == 0, "%d differ from the slow reference", refbad);
    printf("  Leech random: %d points; pair moves checked on all, 196560 neighbours on 80, "
           "slow reference on 200 (%.2f s)\n",
           nrand, (double) (clock() - t0) / CLOCKS_PER_SEC);
    /* Throughput */
    t0 = clock();
    zt_fx x[24];
    for (int j = 0; j < 24; j++) x[j] = rnd_fx(3 * 65536);
    for (int t = 0; t < 2000; t++) {
        x[t % 24] ^= 1;
        zt_leech_nearest(&lw, x, 65536, o);
    }
    printf("  Leech decode: %.1f us per 24 values (E8: see total)\n",
           1e6 * (double) (clock() - t0) / CLOCKS_PER_SEC / 2000);
    free(minv);
}

int main(void)
{
    clock_t t0 = clock();
    test_e8_codebook();
    test_e8_nearest();
    test_e8_mse();
    test_golay();
    test_leech();
    {
        zt_fx x[8];
        int8_t v[8];
        clock_t t1 = clock();
        for (int i = 0; i < 8; i++) x[i] = rnd_fx(3 * 65536);
        for (int t = 0; t < 200000; t++) {
            x[t % 8] ^= 1;
            zt_e8_nearest(x, 65536, v);
        }
        printf("  E8 nearest: %.3f us per 8 values\n",
               1e6 * (double) (clock() - t1) / CLOCKS_PER_SEC / 200000);
    }
    printf("  total %.2f s\n", (double) (clock() - t0) / CLOCKS_PER_SEC);
    if (fails) {
        printf("test_zt_lattice: %d failures\n", fails);
        return 1;
    }
    printf("test_zt_lattice: all checks passed\n");
    return 0;
}
