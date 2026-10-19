/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_zt_evh.c — the paraconsistent evidence head (zt_evh.h) and its
 * bridge to the paradox engine (paradox.h pdx_classify_answer).
 *
 * The integer sigmoid against a float reference (only the test uses
 * floating point), its symmetry and monotonicity; the para-analyser on the
 * corners, the centre, ties and a full grid against a float reference; the
 * head's dot product against exact arithmetic; a fit on four labelled
 * clusters (TRUE, FALSE, BOTH, NEITHER) measured on held-out samples, with
 * a digest of the fitted weights printed so two machines can be compared;
 * the per-span accumulator and its trend; the generate hook; the answer's
 * paradox class and action; and the lpx-tag grammar against lpres_hex's
 * own names for all 64 tags.
 *
 *   gcc -std=c11 -O1 -g -Wall -Wextra -Werror -fsanitize=address,undefined -DTEST_HOST \
 *       -Iinclude -Isrc/tensor -Isrc/paradox -Isrc/lpres -Isrc/event_space -Isrc/modbind \
 *       -Isrc/surplus src/tensor/test_zt_evh.c src/tensor/zt_evh.c src/tensor/zt_gram.c \
 *       src/tensor/zt_gram_lib.c src/tensor/zt_tok.c src/tensor/zt_gguf.c src/tensor/zt.c \
 *       src/paradox/paradox.c src/lpres/lpres_hex.c src/lpres/lpres.c src/robin_debanks/sha256.c \
 *       -lm -o /tmp/test_zt_evh && /tmp/test_zt_evh
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "zt_evh.h"
#include "zt_gram.h"
#include "paradox.h"

static int pass, fail;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        if (c) {                                                                                   \
            pass++;                                                                                \
        } else {                                                                                   \
            fail++;                                                                                \
            printf("[FAIL] %s:%d: ", __FILE__, __LINE__);                                          \
            printf(__VA_ARGS__);                                                                   \
            printf("\n");                                                                          \
        }                                                                                          \
    } while (0)

static uint64_t rng = 0x2026101200000002ull;
static uint64_t rnd(void)
{
    rng ^= rng << 13;
    rng ^= rng >> 7;
    rng ^= rng << 17;
    return rng;
}
static int32_t rnds(int32_t m) /* -m .. m */
{
    return (int32_t) (rnd() % (uint64_t) (2 * (int64_t) m + 1)) - m;
}

/* FNV-1a over values, little-endian whatever the CPU */
static uint64_t fnv_v(int64_t v, uint32_t bytes, uint64_t h)
{
    for (uint32_t i = 0; i < bytes; i++)
        h = (h ^ (uint8_t) ((uint64_t) v >> (8 * i))) * 0x100000001b3ull;
    return h;
}

static void test_sigmoid(void)
{
    double worst = 0;
    bool mono = true, sym = true;
    zt_fx prev = -1;
    for (int64_t z = -40 * 65536; z <= 40 * 65536; z += 97) {
        zt_fx s = zt_sigmoid((zt_fx) z);
        double ref = 65536.0 / (1.0 + exp(-(double) z / 65536.0));
        double e = fabs(s - ref);
        if (e > worst) worst = e;
        mono = mono && s >= prev;
        prev = s;
        sym = sym && zt_sigmoid((zt_fx) -z) == ZT_ONE - s;
    }
    CHECK(worst <= 8.0, "sigmoid within 8/65536 of exp (%.2f)", worst);
    uint32_t exact = 0, tot = 0;
    for (int64_t z = 0; z <= 30 * 65536; z += 101, tot++) {
        uint64_t d = 65536u + (uint32_t) zt_exp((zt_fx) -z);
        exact += (uint64_t) zt_sigmoid((zt_fx) z) == ((1ull << 32) + d / 2) / d;
    }
    CHECK(exact == tot, "sigmoid == round(2^32 / (2^16 + zt_exp(-z))) exactly (%u/%u)", exact, tot);
    CHECK(mono && sym, "sigmoid monotone and sigmoid(-z) == 1 - sigmoid(z)");
    CHECK(zt_sigmoid(0) == 32768 && zt_sigmoid(INT32_MAX) == ZT_ONE && zt_sigmoid(INT32_MIN) == 0,
          "sigmoid(0) = 1/2, saturates at 0 and 1 (%d %d)", zt_sigmoid(INT32_MAX),
          zt_sigmoid(INT32_MIN));
}

static uint8_t ref_base(double mu, double la)
{
    double gc = mu - la, gt = mu + la - 1.0;
    if (fabs(gc) > fabs(gt)) return gc > 0 ? ZT_EVH_TRUE : ZT_EVH_FALSE;
    return gt > 0 ? ZT_EVH_BOTH : ZT_EVH_NEITHER;
}

static void test_base(void)
{
    const zt_fx H = ZT_ONE / 2, O = ZT_ONE;
    zt_fx c, t;
    CHECK(zt_evh_base(O, 0, &c, &t) == ZT_EVH_TRUE && c == O && t == 0, "(1,0) is TRUE");
    CHECK(zt_evh_base(0, O, &c, &t) == ZT_EVH_FALSE && c == -O, "(0,1) is FALSE");
    CHECK(zt_evh_base(O, O, &c, &t) == ZT_EVH_BOTH && t == O, "(1,1) is BOTH");
    CHECK(zt_evh_base(0, 0, &c, &t) == ZT_EVH_NEITHER && t == -O, "(0,0) is NEITHER");
    CHECK(zt_evh_base(H, H, 0, 0) == ZT_EVH_NEITHER, "the centre is NEITHER");
    CHECK(zt_evh_base(H + 1, H, &c, &t) == ZT_EVH_BOTH && c == 1 && t == 1 &&
              zt_evh_base(H, H - 1, 0, 0) == ZT_EVH_NEITHER &&
              zt_evh_base(H + 2, H - 1, 0, 0) == ZT_EVH_TRUE &&
              zt_evh_base(H - 2, H + 1, 0, 0) == ZT_EVH_FALSE,
          "one step off the centre: BOTH, NEITHER, TRUE, FALSE");
    CHECK(zt_evh_base(O, H, 0, 0) == ZT_EVH_BOTH && zt_evh_base(H, 0, 0, 0) == ZT_EVH_NEITHER &&
              zt_evh_base(0, H, 0, 0) == ZT_EVH_NEITHER && zt_evh_base(H, O, 0, 0) == ZT_EVH_BOTH,
          "ties go to the contradiction axis, never to TRUE or FALSE");
    uint32_t agree = 0, tot = 0, seen[4] = {0};
    for (zt_fx mu = 0; mu <= O; mu += 257)
        for (zt_fx la = 0; la <= O; la += 263) {
            uint8_t b = zt_evh_base(mu, la, 0, 0);
            agree += b == ref_base(mu / 65536.0, la / 65536.0);
            seen[b]++;
            tot++;
        }
    CHECK(agree == tot && seen[0] && seen[1] && seen[2] && seen[3],
          "grid of %u pairs == the float para-analyser, all four regions", tot);
}

static void test_apply(void)
{
    enum { N = 300 };
    static int16_t wm[N], wl[N];
    static zt_fx h[N];
    uint32_t ok = 0;
    for (uint32_t it = 0; it < 500; it++) {
        for (uint32_t i = 0; i < N; i++) {
            wm[i] = (int16_t) rnds(32767);
            wl[i] = (int16_t) rnds(32767);
            h[i] = it % 50 == 0 ? (rnd() & 1 ? INT32_MAX : INT32_MIN)
                   : it % 2     ? rnds(40 * 65536)
                                : rnds(64);
        }
        uint32_t s1 = it % 2 ? (uint32_t) (rnd() % 41) : (uint32_t) (rnd() % 4);
        uint32_t s2 = it % 2 ? (uint32_t) (rnd() % 41) : (uint32_t) (rnd() % 4);
        zt_evh_t e = {N, wm, wl, s1, s2, rnds(4 * 65536), rnds(4 * 65536)};
        zt_evh_out_t o;
        if (zt_evh_apply(&e, h, N, &o)) continue;
        /* |z| < 300 * 2^15 * 2^31 < 2^55: exact in 64 bits; round by division */
        int64_t z = 0;
        for (uint32_t i = 0; i < N; i++) z += (int64_t) wm[i] * h[i];
        int64_t a = z < 0 ? -z : z, d = (int64_t) 1 << e.sh_mu;
        a = a / d + (2 * (a % d) >= d);
        int64_t q = (z < 0 ? -a : a) + e.b_mu;
        zt_fx qs = q > INT32_MAX ? INT32_MAX : q < INT32_MIN ? INT32_MIN : (zt_fx) q;
        int64_t zl = 0;
        for (uint32_t i = 0; i < N; i++) zl += (int64_t) wl[i] * h[i];
        int64_t al = zl < 0 ? -zl : zl, dl = (int64_t) 1 << e.sh_la;
        al = al / dl + (2 * (al % dl) >= dl);
        int64_t ql = (zl < 0 ? -al : al) + e.b_la;
        zt_fx qls = ql > INT32_MAX ? INT32_MAX : ql < INT32_MIN ? INT32_MIN : (zt_fx) ql;
        zt_fx c2, t2;
        uint8_t b2 = zt_evh_base(o.mu, o.la, &c2, &t2);
        ok += o.z_mu == qs && o.z_la == qls && o.mu == zt_sigmoid(qs) && o.la == zt_sigmoid(qls) &&
              o.base == b2 && o.cert == c2 && o.contr == t2;
    }
    CHECK(ok == 500, "the head's dot product == exact arithmetic, extremes included (%u)", ok);
    zt_evh_t e = {N, wm, wl, 0, 0, 0, 0};
    zt_evh_out_t o;
    CHECK(zt_evh_apply(&e, h, N - 1, &o) == ZT_EVH_EARG && zt_evh_apply(0, h, N, &o) == ZT_EVH_EARG,
          "width mismatch and null refused");
    e.sh_mu = 41;
    CHECK(zt_evh_apply(&e, h, N, &o) == ZT_EVH_EARG, "shift beyond 40 refused");
    e.sh_mu = 40;
    e.sh_la = 41;
    CHECK(zt_evh_apply(&e, h, N, &o) == ZT_EVH_EARG, "lambda's shift beyond 40 refused");
    e.sh_la = 40;
    CHECK(zt_evh_apply(&e, h, N, &o) == 0, "shift 40 accepted");
    e.w_la = 0;
    CHECK(zt_evh_apply(&e, h, N, &o) == ZT_EVH_EARG, "missing weights refused");
    static int16_t big[65537];
    static zt_fx hb[65537];
    zt_evh_t eb = {65536, big, big, 0, 0, 0, 0};
    CHECK(zt_evh_apply(&eb, hb, 65536, &o) == 0 && o.base == ZT_EVH_NEITHER,
          "width 65536 accepted (zero weights: the centre, NEITHER)");
    eb.n = 65537;
    CHECK(zt_evh_apply(&eb, hb, 65537, &o) == ZT_EVH_EARG, "width 65537 refused");
}

/* four clusters: feature 0 carries "for", feature 1 "against", the rest noise */
static void make_set(zt_fx *H, uint8_t *ym, uint8_t *yl, uint32_t ns, uint32_t n)
{
    for (uint32_t i = 0; i < ns; i++) {
        uint32_t cls = (uint32_t) (rnd() % 4);
        ym[i] = cls == ZT_EVH_TRUE || cls == ZT_EVH_BOTH;
        yl[i] = cls == ZT_EVH_FALSE || cls == ZT_EVH_BOTH;
        zt_fx *h = H + (uint64_t) i * n;
        for (uint32_t j = 0; j < n; j++) h[j] = rnds(65536);
        h[0] += (ym[i] ? 3 : -3) * 65536;
        h[1] += (yl[i] ? 3 : -3) * 65536;
        h[2] += (ym[i] ? 1 : -1) * 65536 + (yl[i] ? -1 : 1) * 32768;
    }
}

#define FIT_GOLDEN 0x9221850a1dcfc6c6ull

static uint64_t head_digest(const zt_evh_t *e, uint64_t d)
{
    for (uint32_t j = 0; j < e->n; j++) d = fnv_v(e->w_la[j], 2, fnv_v(e->w_mu[j], 2, d));
    d = fnv_v(e->sh_mu, 4, d);
    d = fnv_v(e->sh_la, 4, d);
    d = fnv_v(e->b_mu, 4, d);
    return fnv_v(e->b_la, 4, d);
}

static void test_fit(void)
{
    enum { N = 24, NT = 1600, NH = 400 };
    zt_fx *H = malloc(sizeof(zt_fx) * N * (NT + NH));
    uint8_t ym[NT + NH], yl[NT + NH];
    make_set(H, ym, yl, NT + NH, N);
    int16_t wm[N], wl[N];
    zt_evh_t e;
    zt_evh_fit_params_t p = {4, 10};
    uint64_t wb = zt_evh_fit_work_bytes(N);
    void *work = malloc(wb + 8);
    CHECK(zt_evh_fit(H, NT, N, ym, yl, &p, wm, wl, &e, work, wb - 1) == ZT_EVH_ESPACE,
          "short fit work refused");
    CHECK(zt_evh_fit(H, NT, N, ym, yl, &p, wm, wl, &e, (uint8_t *) work + 1, wb) == 0,
          "fit runs (misaligned work)");
    uint32_t conf[4][4];
    int32_t right = zt_evh_score(&e, H + (uint64_t) NT * N, NH, ym + NT, yl + NT, conf);
    printf("held-out confusion (rows truth F N B T, columns predicted):\n");
    for (uint32_t a = 0; a < 4; a++)
        printf("  %4u %4u %4u %4u\n", conf[a][0], conf[a][1], conf[a][2], conf[a][3]);
    CHECK(right >= NH * 95 / 100, "held-out accuracy %d/%d >= 95%%", right, NH);
    bool all4 = true;
    for (uint32_t a = 0; a < 4; a++) all4 = all4 && conf[a][a] > 0;
    CHECK(all4, "each of TRUE, FALSE, BOTH and NEITHER is predicted correctly somewhere");
    CHECK(wm[0] > 0 && wl[1] > 0 && wm[0] > abs(wm[5]) * 4 && wl[1] > abs(wl[5]) * 4,
          "mu reads feature 0 and lambda feature 1 (%d %d, noise %d %d)", wm[0], wl[1], wm[5],
          wl[5]);
    uint64_t d = head_digest(&e, 0xcbf29ce484222325ull);
    int16_t wm2[N], wl2[N];
    zt_evh_t e2;
    zt_evh_fit(H, NT, N, ym, yl, &p, wm2, wl2, &e2, work, wb);
    CHECK(!memcmp(wm, wm2, sizeof wm) && !memcmp(wl, wl2, sizeof wl) && e.b_mu == e2.b_mu,
          "a second fit gives the same bits");
    /* the same data scaled up 2^13 with no learning-rate shift: weights hit
     * the +-128 clamp; scaled down with a tiny rate: weights below 2^-9,
     * stored with shifts past 24 */
    zt_fx *Hb = malloc(sizeof(zt_fx) * N * NT);
    for (uint64_t i = 0; i < (uint64_t) N * NT; i++) {
        int64_t v = (int64_t) H[i] * 8192;
        Hb[i] = v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (zt_fx) v;
    }
    zt_evh_fit_params_t pb = {2, 0};
    CHECK(zt_evh_fit(Hb, NT, N, ym, yl, &pb, wm2, wl2, &e2, work, wb) == 0, "large inputs fit");
    d = head_digest(&e2, d);
    bool clamp = false;
    for (uint32_t j = 0; j < N; j++) clamp = clamp || wm2[j] == 32767 || wm2[j] == -32767;
    CHECK(clamp || e2.sh_mu <= 8, "large inputs: weights near the clamp (shift %u)", e2.sh_mu);
    for (uint64_t i = 0; i < (uint64_t) N * NT; i++) Hb[i] = H[i] / 64;
    zt_evh_fit_params_t pt = {1, 24};
    CHECK(zt_evh_fit(Hb, NT, N, ym, yl, &pt, wm2, wl2, &e2, work, wb) == 0 && e2.sh_mu > 24 &&
              e2.sh_la > 24,
          "tiny steps: weights stored with shifts past 24 (%u, %u)", e2.sh_mu, e2.sh_la);
    d = head_digest(&e2, d);
    zt_evh_fit_params_t p1 = {1, 10};
    zt_evh_fit(H, NT, N, ym, yl, &p1, wm2, wl2, &e2, work, wb);
    d = head_digest(&e2, d);
    CHECK(memcmp(wm, wm2, sizeof wm) != 0, "one epoch differs from four");
    free(Hb);
    printf("fit digest %016llx\n", (unsigned long long) d);
    CHECK(d == FIT_GOLDEN, "fitted weights == the golden digest every CPU gives (%016llx)",
          (unsigned long long) d);
    /* argument checks */
    zt_evh_fit_params_t p0 = {0, 10}, pl = {1, 41};
    int32_t bad = 0;
    bad += zt_evh_fit(0, NT, N, ym, yl, &p, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, 0, N, ym, yl, &p, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, 0, ym, yl, &p, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, 65537, ym, yl, &p, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, 0, yl, &p, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, 0, &p, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, 0, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, &p0, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, &pl, wm, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, &p, 0, wl, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, &p, wm, 0, &e, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, &p, wm, wl, 0, work, wb) == ZT_EVH_EARG;
    bad += zt_evh_fit(H, NT, N, ym, yl, &p, wm, wl, &e, 0, wb) == ZT_EVH_ESPACE;
    CHECK(bad == 13, "fit refuses bad arguments (%d/13)", bad);
    zt_evh_fit_params_t p40 = {1, 40};
    CHECK(zt_evh_fit(H, 4, N, ym, yl, &p40, wm2, wl2, &e2, work, wb) == 0,
          "rate shift 40 is allowed");
    CHECK(zt_evh_fit_work_bytes(N) == N * 8u + 8u && zt_evh_fit_work_bytes(0) == 8u,
          "fit work: 8 bytes a weight plus alignment");
    for (uint32_t a = 0; a < 4; a++)
        for (uint32_t b = 0; b < 4; b++) conf[a][b] = 77;
    bad = 0;
    bad += zt_evh_score(0, H, NH, ym, yl, conf) == ZT_EVH_EARG;
    bad += zt_evh_score(&e, 0, NH, ym, yl, conf) == ZT_EVH_EARG;
    bad += zt_evh_score(&e, H, NH, 0, yl, conf) == ZT_EVH_EARG;
    bad += zt_evh_score(&e, H, NH, ym, 0, conf) == ZT_EVH_EARG;
    bad += zt_evh_score(&e, H, NH, ym, yl, 0) == ZT_EVH_EARG;
    bad += zt_evh_score(&e, H, 0x80000000u, ym, yl, conf) == ZT_EVH_EARG;
    CHECK(bad == 6, "score refuses bad arguments (%d/6)", bad);
    right = zt_evh_score(&e, H + (uint64_t) NT * N, NH, ym + NT, yl + NT, conf);
    uint32_t sum = 0, diag = 0;
    for (uint32_t a = 0; a < 4; a++)
        for (uint32_t b = 0; b < 4; b++) sum += conf[a][b], diag += a == b ? conf[a][b] : 0;
    CHECK(sum == NH && (int32_t) diag == right, "score clears the matrix; diagonal == correct");
    zt_evh_t ebad = e;
    ebad.sh_mu = 99;
    CHECK(zt_evh_score(&ebad, H, NH, ym, yl, conf) == ZT_EVH_EARG, "score passes on a bad head");
    /* all-zero labels: lambda's bias goes negative, the head says FALSE never */
    memset(ym, 0, sizeof ym);
    memset(yl, 0, sizeof yl);
    zt_evh_fit(H, NT, N, ym, yl, &p, wm, wl, &e, work, wb);
    right = zt_evh_score(&e, H + (uint64_t) NT * N, NH, ym + NT, yl + NT, conf);
    CHECK(right == NH, "no evidence either way in the labels: NEITHER on every held-out sample");
    free(work);
    free(H);
}

static void test_acc(void)
{
    zt_evh_acc_t a;
    zt_evh_sum_t s;
    zt_evh_acc_reset(&a);
    zt_evh_summary(&a, &s);
    CHECK(s.n == 0 && s.base == ZT_EVH_NEITHER && !s.saturated, "empty span: NEITHER");
    /* 4 tokens for, then 8 tokens both */
    for (uint32_t i = 0; i < 12; i++) {
        zt_evh_out_t o;
        o.mu = 60000;
        o.la = i < 4 ? 4000 : 60000;
        o.base = zt_evh_base(o.mu, o.la, &o.cert, &o.contr);
        zt_evh_acc_add(&a, &o, 0);
    }
    zt_evh_summary(&a, &s);
    CHECK(s.n == 12 && s.cnt[ZT_EVH_TRUE] == 4 && s.cnt[ZT_EVH_BOTH] == 8,
          "per-base token counts (%u %u)", s.cnt[ZT_EVH_TRUE], s.cnt[ZT_EVH_BOTH]);
    CHECK(s.pos0 == 8 && s.neg0 == 4 && s.pos1 == 12 && s.neg1 == 8,
          "trend: counts at 8 tokens (%u, %u) and at 12 (%u, %u)", s.pos0, s.neg0, s.pos1, s.neg1);
    CHECK(s.mu == 60000 && s.la == (4 * 4000 + 8 * 60000 + 6) / 12 && s.base == ZT_EVH_BOTH,
          "mean pair (%d, %d) is BOTH", s.mu, s.la);
    zt_evh_out_t o = {0, 0, 30000, 30000, 0, 0, ZT_EVH_NEITHER};
    zt_evh_acc_add(&a, &o, 3);
    zt_evh_summary(&a, &s);
    CHECK(s.saturated && a.sat == 3, "a saturation count marks the span");
    zt_evh_acc_add(&a, &o, 2);
    CHECK(a.sat == 3, "the largest saturation count is kept");
    /* marks at 1, 2, 4, 8 tokens; exactly 1/2 counts as evidence */
    zt_evh_acc_reset(&a);
    uint32_t want0[10] = {0, 1, 2, 2, 4, 4, 4, 4, 8, 8}, got_ok = 0;
    for (uint32_t i = 0; i < 10; i++) {
        zt_evh_out_t x = {0, 0, ZT_ONE / 2, ZT_ONE / 2 - 1, 0, 0, ZT_EVH_BOTH};
        zt_evh_acc_add(&a, &x, 0);
        got_ok += a.pos0 == want0[i] && a.neg0 == 0 && a.pos == i + 1 && a.neg == 0;
    }
    CHECK(got_ok == 10,
          "trend marks at each power of two; mu == 1/2 counts, lambda just under does not");
    zt_evh_acc_reset(&a);
    zt_evh_out_t x1 = {0, 0, 1, 2, 0, 0, ZT_EVH_NEITHER}, x2 = {0, 0, 2, 2, 0, 0, ZT_EVH_NEITHER};
    zt_evh_acc_add(&a, &x1, 0);
    zt_evh_acc_add(&a, &x2, 0);
    zt_evh_summary(&a, &s);
    CHECK(s.mu == 2 && s.la == 2, "means round half up (%d, %d)", s.mu, s.la);
    zt_evh_acc_add(&a, &x1, 0);
    zt_evh_summary(&a, &s);
    CHECK(s.mu == 1 && s.la == 2 && s.cnt[ZT_EVH_NEITHER] == 3, "three tokens: mean 4/3 -> 1");
    zt_evh_acc_t full = a;
    full.n = UINT32_MAX;
    zt_evh_acc_add(&full, &x1, 0);
    CHECK(full.n == UINT32_MAX && full.cnt[ZT_EVH_NEITHER] == 3, "a full counter stops counting");
    zt_evh_acc_reset(0);
    zt_evh_acc_add(0, &x1, 0);
    zt_evh_acc_add(&a, 0, 0);
    zt_evh_summary(0, 0);
    zt_evh_summary(0, &s);
    CHECK(s.n == 0 && s.base == ZT_EVH_NEITHER, "null accumulator: an empty NEITHER summary");
    memset(&s, 0x5A, sizeof s);
    zt_evh_acc_reset(&a);
    zt_evh_summary(&a, &s);
    CHECK(s.n == 0 && s.mu == 0 && s.cnt[0] == 0 && s.pos0 == 0 && !s.saturated,
          "summary clears every field");
    zt_evh_hook(0, 0, 0, 0, 0);

    /* the generate hook */
    int16_t w[4] = {1000, 0, 0, 0}, z[4] = {0, 0, 0, 0};
    zt_evh_t e = {4, w, z, 0, 0, 0, -10 * 65536};
    zt_evh_acc_reset(&a);
    zt_evh_hook_t hk = {&e, &a, 0};
    zt_fx h[4] = {65536, 0, 0, 0};
    zt_evh_hook(&hk, 7, h, 4, 0);
    zt_evh_hook(&hk, 8, h, 3, 0);
    zt_evh_summary(&a, &s);
    CHECK(s.n == 1 && hk.skipped == 1 && s.base == ZT_EVH_TRUE,
          "hook: one token added (TRUE), a wrong width skipped and counted");
}

static void test_paradox(void)
{
    pdx_evidence_t grow = {2, 2, 9, 2, false}, shrink = {9, 2, 6, 5, false},
                   flat = {3, 1, 3, 1, false};
    pdx_evidence_t stale = {2, 2, 9, 2, true};
    pdx_class_t c = pdx_classify_answer(LPX_B_BOTH, 12, &grow, true, 0, 0);
    CHECK(c.kind == PDX_KIND_CONSTRUCTIVE && lpx_phase(c.tag) == LPX_PH_MANIFEST &&
              lpx_action(c.tag) == LPX_ACT_WAIT,
          "BOTH with a growing margin: constructive, manifest, wait (%s)",
          lpx_action_name(lpx_action(c.tag)));
    c = pdx_classify_answer(LPX_B_BOTH, 12, &shrink, true, 0, 0);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE && lpx_action(c.tag) == LPX_ACT_ESCALATE,
          "BOTH with a shrinking margin: destructive, escalate");
    c = pdx_classify_answer(LPX_B_BOTH, 12, &stale, true, 0, 0);
    CHECK(c.kind == PDX_KIND_DESTRUCTIVE, "BOTH from a saturated engine (stale): destructive");
    c = pdx_classify_answer(LPX_B_BOTH, 3, &flat, false, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL && lpx_phase(c.tag) == LPX_PH_EMERGING,
          "BOTH, steady, still being written: neutral, emerging");
    c = pdx_classify_answer(LPX_B_NEITHER, 0, 0, false, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL && lpx_phase(c.tag) == LPX_PH_POTENTIAL &&
              lpx_action(c.tag) == LPX_ACT_WAIT,
          "no tokens: NEITHER, potential, wait");
    c = pdx_classify_answer(LPX_B_TRUE, 12, &grow, true, 0, 0);
    CHECK(c.kind == PDX_KIND_NONE && lpx_action(c.tag) == LPX_ACT_PROCEED,
          "TRUE, firming up: not a paradox, proceed");
    c = pdx_classify_answer(LPX_B_TRUE, 12, &stale, true, 0, 0);
    CHECK(c.kind == PDX_KIND_NONE && lpx_action(c.tag) == LPX_ACT_WAIT,
          "TRUE from a saturated engine: wait");
    c = pdx_classify_answer(LPX_B_FALSE, 12, &grow, true, 0, 0);
    CHECK(c.kind == PDX_KIND_NONE && lpx_action(c.tag) == LPX_ACT_REFUSE, "FALSE: refuse");
    c = pdx_classify_answer(LPX_B_BOTH, 12, &flat, true, 0, 0);
    CHECK(c.kind == PDX_KIND_NEUTRAL && !c.forbidden, "never forbidden from evidence alone");
    /* a registered signature overrides the trend */
    pdx_sig_t ent[2];
    pdx_registry_t reg;
    pdx_reg_init(&reg, ent, 2);
    uint8_t sig[PDX_HASH];
    pdx_signature((const uint8_t *) "two jurisdictions", 17, sig);
    pdx_reg_add(&reg, sig, PDX_KIND_NEUTRAL, 1, 0, PDX_SRC_SEED);
    c = pdx_classify_answer(LPX_B_BOTH, 12, &shrink, true, &reg, sig);
    CHECK(c.kind == PDX_KIND_NEUTRAL && c.reg_index == 0,
          "a known contradiction (registered signature) is held, not escalated");
    CHECK((int) ZT_EVH_FALSE == (int) LPX_B_FALSE && (int) ZT_EVH_NEITHER == (int) LPX_B_NEITHER &&
              (int) ZT_EVH_BOTH == (int) LPX_B_BOTH && (int) ZT_EVH_TRUE == (int) LPX_B_TRUE,
          "the head's base codes are lpres_hex's");
}

static void test_tag_grammar(void)
{
    uint32_t len = 0;
    const char *src = zt_gram_lib("lpx-tag", &len);
    zt_gram_err_t err;
    uint64_t need = zt_gram_arena_bytes((const uint8_t *) src, len, 0, &err);
    void *arena = malloc(need);
    zt_gram_t g;
    zt_gram_compile(&g, (const uint8_t *) src, len, 0, arena, need, &err);
    uint64_t sb = zt_gram_state_bytes(16, 8);
    void *mem = malloc(sb);
    zt_gram_state_t st;
    zt_gram_state_init(&st, &g, 16, 8, mem, sb);
    uint32_t ok = 0;
    for (uint32_t t = 0; t < LPX_STATES; t++) {
        char buf[64];
        int n = snprintf(buf, sizeof buf, "%s/%s/%s", lpx_base_name(lpx_base((lpx_t) t)),
                         lpx_pol_name(lpx_pol((lpx_t) t)), lpx_phase_name(lpx_phase((lpx_t) t)));
        zt_gram_reset(&st);
        ok +=
            zt_gram_accept(&st, (const uint8_t *) buf, (uint32_t) n) == 0 && zt_gram_finished(&st);
    }
    CHECK(ok == LPX_STATES, "lpx-tag grammar admits all 64 of lpres_hex's names (%u)", ok);
    zt_gram_reset(&st);
    CHECK(zt_gram_accept(&st, (const uint8_t *) "true/maybe", 10) == ZT_GRAM_EREJECT,
          "and refuses others");
    free(mem);
    free(arena);
}

int main(void)
{
    test_sigmoid();
    test_base();
    test_apply();
    test_fit();
    test_acc();
    test_paradox();
    test_tag_grammar();
    printf("%d passed, %d failed\n", pass, fail);
    return fail ? 1 : 0;
}
