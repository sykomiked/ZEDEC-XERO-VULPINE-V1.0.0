/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* test_ct_gate.c — unit tests for the tier-1 surplus gate (ct_gate.h).
 *
 *   gcc -std=c11 -O2 -Wall -Wextra -Werror -Isrc/tensor -Isrc/cotier src/cotier/test_ct_gate.c \
 *       src/cotier/ct_gate.c src/tensor/zt.c src/tensor/zt_isf.c -lm -o /tmp/test_ct_gate
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ct_gate.h"

static int failures = 0, checks = 0;
#define CHECK(c, ...)                                                                              \
    do {                                                                                           \
        checks++;                                                                                  \
        int ok_ = (c) ? 1 : 0;                                                                     \
        if (!ok_) failures++;                                                                      \
        printf(ok_ ? "[PASS] " : "[FAIL] ");                                                       \
        printf(__VA_ARGS__);                                                                       \
        printf("\n");                                                                              \
    } while (0)

static uint64_t rng = 0x1234567;
static uint64_t next(void)
{
    uint64_t z = (rng += 0x9E3779B97F4A7C15ull);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}
static double unif(void)
{
    return (next() >> 11) * (1.0 / 9007199254740992.0) * 2 - 1;
}

#define D 16u

/* rows = base direction (one of three axes, signed) + small noise */
static void groups(zt_fx *x, uint32_t n, const uint32_t *axis, double noise, uint32_t *label)
{
    for (uint32_t t = 0; t < n; t++) {
        uint32_t gidx = (uint32_t) (next() % 3);
        label[t] = gidx;
        for (uint32_t i = 0; i < D; i++) {
            double v = (i == axis[gidx] ? 1.0 : 0.0) + noise * unif();
            x[t * D + i] = (zt_fx) (v * 65536.0);
        }
    }
}

static ct_gate_cfg_t cfg(uint32_t mode, double floor, uint32_t window)
{
    ct_gate_cfg_t c;
    c.mode = mode;
    c.n_clusters = 8;
    c.iters = 2;
    c.N = D;
    c.floor = (zt_fx) (floor * 65536.0);
    c.window = window;
    return c;
}

int main(void)
{
    static ct_gate_t g;
    static zt_fx q[4096 * D], k[4096 * D];
    static uint8_t qa[4096], ka[4096], keep[256 * 256];
    uint32_t lq[4096], lk[4096];

    /* arguments */
    ct_gate_cfg_t c = cfg(CT_GATE_SURPLUS, 0, 4);
    c.n_clusters = 0;
    CHECK(ct_gate_build(&g, &c, q, 4, D, k, 4, D, D, qa, ka) == CT_GATE_EARG, "0 clusters refused");
    c.n_clusters = 17;
    CHECK(ct_gate_build(&g, &c, q, 4, D, k, 4, D, D, qa, ka) == CT_GATE_EARG,
          "17 clusters refused");
    c = cfg(CT_GATE_SURPLUS, 0, 4);
    CHECK(ct_gate_build(&g, &c, q, 4, D, k, 4, D, 0, qa, ka) == CT_GATE_EARG, "dim 0 refused");
    CHECK(ct_gate_build(&g, &c, q, 4, D, k, 4, D, 257, qa, ka) == CT_GATE_EARG, "dim 257 refused");
    CHECK(ct_gate_build(&g, &c, q, 4, 8, k, 4, D, D, qa, ka) == CT_GATE_EARG,
          "stride below dim refused");
    c.iters = 5;
    CHECK(ct_gate_build(&g, &c, q, 4, D, k, 4, D, D, qa, ka) == CT_GATE_EARG, "5 passes refused");

    /* clustering: three signed axes with noise are recovered as pure clusters */
    uint32_t axes[3] = {0, 5, 11};
    groups(k, 300, axes, 0.15, lk);
    groups(q, 300, axes, 0.15, lq);
    c = cfg(CT_GATE_SURPLUS, 0, 0);
    c.n_clusters = 3;
    c.iters = 3;
    CHECK(ct_gate_build(&g, &c, q, 300, D, k, 300, D, D, qa, ka) == 0, "build 300 x 300");
    uint32_t impure = 0;
    for (uint32_t t = 0; t < 300; t++)
        for (uint32_t s = 0; s < t; s++)
            if ((lk[t] == lk[s]) != (ka[t] == ka[s])) impure++;
    CHECK(impure == 0, "keys: every group lands in its own cluster (%u impure pairs)", impure);

    /* F(u) on cluster pairs: aligned ~0, orthogonal ~ln N */
    double lnN = log((double) D);
    double fmax = 0, fmin = 99;
    for (uint32_t a = 0; a < 3; a++)
        for (uint32_t b = 0; b < 3; b++) {
            double f = g.score[a][b] / 65536.0;
            if (f > fmax) fmax = f;
            if (f < fmin) fmin = f;
        }
    CHECK(fmin < 0.1 && fmax > 0.9 * lnN,
          "cluster-pair F(u): aligned %.3f, orthogonal %.3f (ln N = %.3f)", fmin, fmax, lnN);

    /* SURPLUS keeps orthogonal clusters and drops aligned ones; ALIGN the reverse */
    c.floor = (zt_fx) (0.5 * lnN * 65536);
    ct_gate_build(&g, &c, q, 300, D, k, 300, D, D, qa, ka);
    uint32_t wrong = 0;
    for (uint32_t i = 0; i < 300; i++)
        for (uint32_t j = 0; j < 300; j++)
            if (ct_gate_keep(&g, i, 1000, j, 0) != (lq[i] != lk[j])) wrong++;
    CHECK(wrong == 0, "surplus: keeps exactly the cross-group edges (%u wrong)", wrong);
    c.mode = CT_GATE_ALIGN;
    c.floor = (zt_fx) (0.5 * 65536);
    ct_gate_build(&g, &c, q, 300, D, k, 300, D, D, qa, ka);
    wrong = 0;
    for (uint32_t i = 0; i < 300; i++)
        for (uint32_t j = 0; j < 300; j++)
            if (ct_gate_keep(&g, i, 1000, j, 0) != (lq[i] == lk[j])) wrong++;
    CHECK(wrong == 0, "align: keeps exactly the same-group edges (%u wrong)", wrong);

    /* never prune everything: an impossible floor keeps window + diagonal */
    for (uint32_t w = 0; w <= 8; w += 4) {
        c = cfg(CT_GATE_SURPLUS, 1000.0, w);
        uint32_t n = 200, bad = 0;
        for (uint32_t t = 0; t < n * D; t++) q[t] = k[t] = (zt_fx) (next() & 0x3FFFF) - 0x20000;
        ct_gate_build(&g, &c, q, n, D, k, n, D, D, qa, ka);
        for (uint32_t i = 0; i < n; i++) {
            uint32_t kept = ct_gate_row(&g, i, i, 0, keep);
            uint32_t want = w == 0 ? 1 : (i + 1 < w ? i + 1 : w);
            if (kept != want || !keep[i]) bad++;
            for (uint32_t j = i + 1; j < n; j++)
                if (keep[j]) bad++;
        }
        CHECK(bad == 0, "window %u: every row keeps exactly the window and its diagonal", w);
    }

    /* floor 0 keeps every causal edge; OFF keeps every causal edge */
    c = cfg(CT_GATE_SURPLUS, 0, 0);
    ct_gate_build(&g, &c, q, 200, D, k, 200, D, D, qa, ka);
    uint32_t total = 0;
    for (uint32_t i = 0; i < 200; i++) total += ct_gate_row(&g, i, i, 0, keep);
    CHECK(total == 200 * 201 / 2, "surplus floor 0 keeps all %u causal edges", total);
    c.mode = CT_GATE_OFF;
    ct_gate_build(&g, &c, q, 200, D, k, 200, D, D, qa, ka);
    total = 0;
    for (uint32_t i = 0; i < 200; i++) total += ct_gate_row(&g, i, i, 0, keep);
    CHECK(total == 200 * 201 / 2, "OFF keeps all causal edges");

    /* keep list = union over queries, against brute force */
    c = cfg(CT_GATE_SURPLUS, 0.995 * lnN, 2);
    for (uint32_t t = 0; t < 128 * D; t++) q[t] = k[t] = (zt_fx) (next() & 0x3FFFF) - 0x20000;
    ct_gate_build(&g, &c, q, 32, D, k, 128, D, D, qa, ka);
    uint32_t list[128], nl = ct_gate_keep_list(&g, 96, 0, list, 128), bf = 0, mism = 0, li = 0;
    for (uint32_t j = 0; j < 128; j++) {
        bool any = false;
        for (uint32_t i = 0; i < 32; i++) any |= ct_gate_keep(&g, i, 96 + i, j, j);
        if (any) {
            bf++;
            if (li >= nl || list[li++] != j) mism++;
        }
    }
    CHECK(nl == bf && mism == 0 && nl >= 32, "keep list: %u of 128 keys, matches brute force", nl);
    CHECK(ct_gate_keep_list(&g, 96, 0, list, 3) == nl, "keep list reports the full count past cap");
    c.floor = 1000 * 65536;
    ct_gate_build(&g, &c, q, 32, D, k, 128, D, D, qa, ka);
    nl = ct_gate_keep_list(&g, 96, 0, list, 128);
    CHECK(nl == 33 && list[0] == 95 && list[32] == 127,
          "keep list with nothing passing the floor: keys 95..127 (window 2) only");
    c.floor = (zt_fx) (0.97 * lnN * 65536);

    /* the hook */
    ct_gate_ctx_t cx;
    ct_gate_ctx_init(&cx, &c, qa, ka, 4096);
    int32_t r = ct_gate_attn(&cx, 0, 0, q, 64, D, 0, k, 64, D, 0, D, keep);
    uint32_t mism2 = 0, kept2 = 0;
    for (uint32_t i = 0; i < 64; i++)
        for (uint32_t j = 0; j < 64; j++) {
            bool e = ct_gate_keep(&cx.g, i, i, j, j);
            kept2 += e;
            if (keep[i * 64 + j] != e) mism2++;
        }
    CHECK(r == (int32_t) kept2 && mism2 == 0 && cx.edges == 64 * 65 / 2 && cx.kept == kept2,
          "ct_gate_attn: keep matrix and counts (%d of %llu edges kept)", r,
          (unsigned long long) cx.edges);
    ct_gate_ctx_t small;
    ct_gate_ctx_init(&small, &c, qa, ka, 16);
    CHECK(ct_gate_attn(&small, 0, 0, q, 64, D, 0, k, 64, D, 0, D, keep) == CT_GATE_ESPACE,
          "ct_gate_attn: too many tokens for the scratch -> keep-all signal");
    /* decode-style call: one query at position 63 against 64 keys */
    r = ct_gate_attn(&cx, 0, 0, q + 63 * D, 1, D, 63, k, 64, D, 0, D, keep);
    CHECK(r >= 2 && keep[63] && keep[62], "single-query decode call keeps diagonal and window");

    /* determinism */
    static uint8_t qa2[4096], ka2[4096];
    static ct_gate_t g2;
    ct_gate_build(&g, &c, q, 128, D, k, 128, D, D, qa, ka);
    ct_gate_build(&g2, &c, q, 128, D, k, 128, D, D, qa2, ka2);
    CHECK(!memcmp(qa, qa2, 128) && !memcmp(ka, ka2, 128) && !memcmp(g.mask, g2.mask, sizeof g.mask),
          "same input, same clusters and mask");

    /* cost at a realistic context length: gate MACs vs attention MACs */
    {
        uint32_t n = 4096, d = 64;
        static zt_fx qq[4096 * 64], kk[4096 * 64];
        for (uint32_t t = 0; t < n * d; t++) {
            qq[t] = (zt_fx) (next() & 0x3FFFF) - 0x20000;
            kk[t] = (zt_fx) (next() & 0x3FFFF) - 0x20000;
        }
        ct_gate_cfg_t cc = cfg(CT_GATE_SURPLUS, 0.97 * log(64.0), 64);
        cc.N = 64;
        ct_gate_ctx_t cxl;
        static uint8_t qal[4096], kal[4096];
        ct_gate_ctx_init(&cxl, &cc, qal, kal, 4096);
        /* one query block of 256 at the end of a 4096 context (prefill chunk) */
        static uint8_t kp[256 * 4096];
        ct_gate_attn(&cxl, 0, 0, qq + (n - 256) * d, 256, d, n - 256, kk, n, d, 0, d, kp);
        double ratio = (double) cxl.gate_macs / cxl.full_macs;
        printf(
            "       cost: 256 queries x 4096 keys, d = 64, 8 clusters, 2 passes: gate %llu MACs, "
            "attention %llu MACs, ratio %.3f\n",
            (unsigned long long) cxl.gate_macs, (unsigned long long) cxl.full_macs, ratio);
        CHECK(ratio < 0.15, "gate costs under 15%% of attention at 4096 context (%.3f)", ratio);
    }

    /* fuzz: random shapes, extreme values, zeros; invariants hold, no crash */
    uint32_t viol = 0;
    for (uint32_t it = 0; it < 3000; it++) {
        uint32_t d = 1 + (uint32_t) (next() % 64), nq = (uint32_t) (next() % 40),
                 nk = (uint32_t) (next() % 40);
        ct_gate_cfg_t fc;
        fc.mode = (uint32_t) (next() % 3);
        fc.n_clusters = 1 + (uint32_t) (next() % 16);
        fc.iters = (uint32_t) (next() % 5);
        fc.N = (uint32_t) (next() % 70);
        fc.floor = (zt_fx) next();
        fc.window = (uint32_t) (next() % 6);
        uint32_t kind = (uint32_t) (next() % 4);
        for (uint32_t t = 0; t < 40 * 64; t++) {
            uint64_t z = next();
            q[t] = kind == 0   ? 0
                   : kind == 1 ? (z & 1 ? INT32_MIN : INT32_MAX)
                   : kind == 2 ? (zt_fx) (z & 3) - 1
                               : (zt_fx) z;
            k[t] = (zt_fx) (z >> 32);
        }
        if (ct_gate_build(&g, &fc, q, nq, d, k, nk, d, d, qa, ka)) {
            viol++;
            continue;
        }
        uint32_t qp0 = (uint32_t) (next() % 50), kp0 = (uint32_t) (next() % 50);
        for (uint32_t i = 0; i < nq; i++) {
            ct_gate_row(&g, i, qp0 + i, kp0, keep);
            for (uint32_t j = 0; j < nk; j++) {
                uint32_t kpos = kp0 + j, qpos = qp0 + i;
                if (kpos > qpos && keep[j]) viol++;
                if (kpos <= qpos && (qpos - kpos < fc.window || kpos == qpos) && !keep[j]) viol++;
            }
        }
        for (uint32_t t = 0; t < nq; t++)
            if (qa[t] >= g.ncq) viol++;
        for (uint32_t t = 0; t < nk; t++)
            if (ka[t] >= g.nck) viol++;
    }
    CHECK(viol == 0, "fuzz: 3000 random gates, causal + window + diagonal invariants hold");

    printf("%d/%d checks passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
