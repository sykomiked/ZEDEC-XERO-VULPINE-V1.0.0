/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Lodestone's languages against the C its model runs (lodestone_rules.h).
 * Slots: @num.lodestone_latency_mean_ms and @logic.lodestone_net_contact_ok,
 * with @num.witness_kind_count (methods per group) and
 * @num.retort_latency_fastest_ms, @num.retort_latency_slowest_ms (the
 * fastest and slowest call), each used as it is. */
#include "lodestone_rules.h"

/* latency_mean_ms over n latencies: one answer, the C's reduced fraction,
 * Trebago and KRON agreeing (TRUE) */
static bool ldt_mean(zxapp_t *a, const int64_t *ms, uint32_t n)
{
    zxapp_arg_t ma = {"ms", ms, n};
    zxapp_num_t o[WAVE_ROWS];
    uint32_t k = 0;
    int32_t lp = -1;
    int64_t num = 0, den = 0;
    int rc = zxapp_call_witnessed(a, &H, "latency_mean_ms", &ma, 1, o, WAVE_ROWS, &k, &lp);
    if (rc != ZXAPP_OK || k != 1 || lp != ZXAPP_TRUE ||
        !lodestone_latency_mean(ms, n, &num, &den)) {
        printf("    latency_mean_ms: rc %d n %u lp %d (%s)\n", rc, k, (int) lp, a->err);
        return false;
    }
    if (o[0].num != num || o[0].den != den) {
        printf("    latency_mean_ms: %lld/%lld, want %lld/%lld\n", (long long) o[0].num,
               (long long) o[0].den, (long long) num, (long long) den);
        return false;
    }
    return true;
}

APP_TEST(lodestone, 17, 23)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], g[WAVE_ROWS], want[WAVE_ROWS];

    /* latency_mean_ms (LR2): Trebago = KRON = C, exact fractions */
    for (uint32_t r = 0; r < 16u; r++) {
        uint32_t n = 2u + (r * 13u) % (WAVE_ROWS - 1u);
        for (uint32_t i = 0; i < n; i++) a[i] = wrand(0, LODESTONE_LATENCY_MAX);
        if (r == 0)
            for (uint32_t i = 0; i < n; i++) a[i] = 0; /* every call instant */
        if (r == 1)
            for (uint32_t i = 0; i < n; i++) a[i] = LODESTONE_LATENCY_MAX; /* every one at max */
        if (r == 2) a[0] = 0, a[1] = LODESTONE_LATENCY_MAX; /* both edges in one round */
        CHECK(ldt_mean(app, a, n), "Lodestone: latency_mean_ms round %u of %u, Trebago = KRON = C",
              r, n);
    }
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        a[i] = WAVE_ROWS * wrand(0, LODESTONE_LATENCY_MAX / 64);
    {
        int64_t num = 0, den = 0;
        CHECK(lodestone_latency_mean(a, WAVE_ROWS, &num, &den) && den == 1,
              "Lodestone: 64 latencies, each a multiple of 64: a whole mean");
        CHECK(wave_same(app, "latency_mean_ms", &(zxapp_arg_t){"ms", a, WAVE_ROWS}, 1, &num, 1,
                        false),
              "Lodestone: a whole mean of 64 rows, Trebago = KRON = C");
        static const int64_t m3[3] = {1, 2, 2}, m2[2] = {120, 121};
        CHECK(lodestone_latency_mean(m3, 3, &num, &den) && num == 5 && den == 3 &&
                  lodestone_latency_mean(m2, 2, &num, &den) && num == 241 && den == 2 &&
                  !lodestone_latency_mean(m3, 0, &num, &den) &&
                  !lodestone_latency_mean(NULL, 3, &num, &den),
              "Lodestone: the C rule's own edges (LR2)");
        CHECK(ldt_mean(app, m3, 3) && ldt_mean(app, m2, 2), "Lodestone: 5/3 and 241/2 ms");
        /* max + 1 and a negative: Trebago refuses, KRON adds it */
        static const int64_t past[2] = {5, LODESTONE_LATENCY_MAX + 1}, neg[2] = {-1, 5};
        CHECK(!lodestone_latency_mean(past, 2, &num, &den) &&
                  !lodestone_latency_mean(neg, 2, &num, &den),
              "Lodestone: the C refuses a latency past an hour or negative");
        zxapp_arg_t pa = {"ms", past, 2}, na = {"ms", neg, 2};
        CHECK(wave_lp(app, "latency_mean_ms", &pa, 1) == ZXAPP_BOTH,
              "Lodestone: a latency past an hour: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "latency_mean_ms", &na, 1) == ZXAPP_BOTH,
              "Lodestone: a negative latency: Trebago refuses, KRON answers: BOTH");
    }

    /* net_contact_ok (LR4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = (int64_t) i, b[i] = wrand(0, 255), c[i] = wrand(0, 1);
        d[i] = wrand(0, 999999999999999), e[i] = wrand(0, 1), f[i] = wrand(0, 1);
        g[i] = wrand(0, 999999999);
    }
    a[1] = 999, b[1] = 999, c[1] = 9, d[1] = 999999999999999, e[1] = 9, f[1] = 9, g[1] = 999999999;
    a[2] = 1000;             /* contact max + 1 */
    b[3] = 1000;             /* bucket max + 1 */
    c[4] = 10;               /* ubh max + 1 */
    d[5] = 1000000000000000; /* seen past 15 digits */
    e[6] = 10;               /* book max + 1 */
    f[7] = 10;               /* member max + 1 */
    g[8] = 1000000000;       /* pending past 9 digits */
    g[9] = -999999999;       /* negative: sign aside */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(lodestone_contact_record_fits(a[i], b[i], c[i], d[i], e[i], f[i], g[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              want[9] == ZXAPP_TRUE,
          "Lodestone: the record's own edges (LR4)");
    zxapp_arg_t ka[7] = {{"contact", a, WAVE_ROWS}, {"bucket", b, WAVE_ROWS},
                         {"ubh", c, WAVE_ROWS},     {"seen", d, WAVE_ROWS},
                         {"book", e, WAVE_ROWS},    {"member", f, WAVE_ROWS},
                         {"pending", g, WAVE_ROWS}};
    CHECK(wave_same(app, "net_contact_ok", ka, 7, want, WAVE_ROWS, true),
          "Lodestone: net_contact_ok, MYRA = C");

    /* witness.kind_count (LR1): methods per group, used as it is */
    for (uint32_t r = 0; r < 8u; r++) {
        int64_t grp = (int64_t) r + 1, w = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) a[i] = wrand(1, 8);
        a[0] = grp; /* the group asked for holds at least one */
        w = lodestone_group_count(a, WAVE_ROWS, grp);
        zxapp_arg_t ga[2] = {{"kind", a, WAVE_ROWS}, {"want", &grp, 1}};
        CHECK(w >= 1 && wave_same(app, "witness.kind_count", ga, 2, &w, 1, false),
              "Lodestone: group %u's methods through witness.kind_count = C", r + 1);
    }

    /* retort.latency_fastest_ms and latency_slowest_ms (LR3), used as they are:
     * the fold over a round of calls, each step witnessed */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) b[i] = wrand(0, LODESTONE_LATENCY_MAX);
    b[5] = 0, b[9] = LODESTONE_LATENCY_MAX;
    {
        int64_t lo = LODESTONE_LATENCY_MAX, hi = 0, flo = 0, fhi = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = lo, c[i] = hi;
            lo = retort_latency_fastest(lo, b[i]);
            hi = retort_latency_slowest(hi, b[i]);
            want[i] = lo, d[i] = hi;
        }
        CHECK(lodestone_latency_span(b, WAVE_ROWS, &flo, &fhi) && flo == 0 &&
                  fhi == LODESTONE_LATENCY_MAX && flo == want[WAVE_ROWS - 1] &&
                  fhi == d[WAVE_ROWS - 1],
              "Lodestone: the span of 64 calls is the fold's last step (LR3)");
        zxapp_arg_t fa[2] = {{"fastest", a, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
        zxapp_arg_t sa[2] = {{"slowest", c, WAVE_ROWS}, {"ms", b, WAVE_ROWS}};
        CHECK(wave_same(app, "retort.latency_fastest_ms", fa, 2, want, WAVE_ROWS, false),
              "Lodestone: the fastest call through retort.latency_fastest_ms = C");
        CHECK(wave_same(app, "retort.latency_slowest_ms", sa, 2, d, WAVE_ROWS, false),
              "Lodestone: the slowest call through retort.latency_slowest_ms = C");
        static const int64_t one[2] = {7, 7}, bad[2] = {5, -1};
        CHECK(!lodestone_latency_span(bad, 2, &flo, &fhi) &&
                  !lodestone_latency_span(one, 0, &flo, &fhi) &&
                  lodestone_latency_span(one, 2, &flo, &fhi) && flo == 7 && fhi == 7,
              "Lodestone: the span's own edges (LR3)");
    }
}
