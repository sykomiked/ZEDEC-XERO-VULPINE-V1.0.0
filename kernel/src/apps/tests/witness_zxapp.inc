/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Witness's languages against the C its model runs (witness_rules.h).
 * Slots: @logic.witness_chain_ok, @num.witness_kind_count,
 * @logic.witness_audit_ok. */
#include "witness_rules.h"

APP_TEST(witness, 5, 18)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* chain_ok (WR1): KRON = Sutra = C, a record's links swept, with every edge */
    int64_t last = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = last + wrand(i % 9 ? 1 : 0, 50);        /* some indexes not past the last */
        b[i] = i % 7 == 3 ? last + wrand(1, 3) : last; /* some links pointing elsewhere */
        c[i] = last;
        last = a[i] > last ? a[i] : last;
    }
    a[0] = 1, b[0] = 0, c[0] = 0;                                    /* the first entry */
    a[1] = 0, b[1] = 0, c[1] = 0;                                    /* 0 is not past the start */
    a[2] = 5, b[2] = 5, c[2] = 5;                                    /* not past the last */
    a[3] = 4, b[3] = 5, c[3] = 5;                                    /* back in time */
    a[4] = 9, b[4] = 4, c[4] = 5;                                    /* prev is not the last */
    a[5] = WITNESS_SEQ_MAX, b[5] = WITNESS_SEQ_MAX - 1, c[5] = b[5]; /* max */
    a[6] = WITNESS_SEQ_MAX, b[6] = 0, c[6] = 0;                      /* the widest gap */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(witness_linked(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_TRUE,
          "Witness: the C rule's own edges (WR1)");
    zxapp_arg_t ca[3] = {{"seq", a, WAVE_ROWS}, {"prev", b, WAVE_ROWS}, {"last", c, WAVE_ROWS}};
    CHECK(wave_same(app, "chain_ok", ca, 3, want, WAVE_ROWS, true),
          "Witness: chain_ok, KRON = Sutra = C");
    {
        /* max + 1: 19 digits past Sutra's 9(18), refused there; KRON answers */
        static const int64_t ix[2] = {2, WITNESS_SEQ_MAX + 1}, pv[2] = {1, 1}, ls[2] = {1, 1};
        zxapp_arg_t xa[3] = {{"seq", ix, 2}, {"prev", pv, 2}, {"last", ls, 2}};
        CHECK(wave_lp(app, "chain_ok", xa, 3) == ZXAPP_BOTH,
              "Witness: an index past 18 digits: Sutra refuses, KRON answers: BOTH");
        /* negative: Sutra's unsigned picture refuses it */
        static const int64_t nx[2] = {2, -1};
        zxapp_arg_t na[3] = {{"seq", nx, 2}, {"prev", pv, 2}, {"last", ls, 2}};
        CHECK(wave_lp(app, "chain_ok", na, 3) == ZXAPP_BOTH,
              "Witness: a negative index: Sutra refuses, KRON answers: BOTH");
    }

    /* kind_count (WR2): 64 records, each counted for one kind, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t k[16], wk[1];
        uint32_t n = 2u + r % 15u;
        for (uint32_t i = 0; i < n; i++) k[i] = r == 0 ? 0 : wrand(0, 5);
        wk[0] = r == 1 ? 9 : r == 2 ? -1 : wrand(0, 5); /* a kind nobody has; negative */
        if (r == 3) k[0] = wk[0] = 999999999;           /* large */
        int64_t c1 = witness_kind_count(k, n, wk[0]);
        zxapp_arg_t ka[2] = {{"kind", k, n}, {"want", wk, 1}};
        CHECK(wave_same(app, "kind_count", ka, 2, &c1, 1, false),
              "Witness: kind_count %u, KRON = Trebago = C", r);
    }
    {
        static const int64_t all[4] = {3, 3, 3, 3}, three[1] = {3}, four[1] = {4};
        int64_t w4 = 4, w0 = 0;
        zxapp_arg_t xa[2] = {{"kind", all, 4}, {"want", three, 1}};
        CHECK(wave_same(app, "kind_count", xa, 2, &w4, 1, false) &&
                  witness_kind_count(all, 4, 3) == 4,
              "Witness: every entry of the kind");
        zxapp_arg_t na[2] = {{"kind", all, 4}, {"want", four, 1}};
        CHECK(wave_same(app, "kind_count", na, 2, &w0, 1, false) &&
                  witness_kind_count(NULL, 4, 3) == 0,
              "Witness: none of the kind");
        /* two wants: Trebago's scalar refuses a vector, KRON sums it (+/want) */
        static const int64_t k2[2] = {1, 2}, wants[2] = {1, 1};
        zxapp_arg_t ba[2] = {{"kind", k2, 2}, {"want", wants, 2}};
        CHECK(wave_lp(app, "kind_count", ba, 2) == ZXAPP_BOTH,
              "Witness: two wants: Trebago refuses, KRON answers: BOTH");
    }

    /* audit_ok (WR3): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(1, 999999999);
        b[i] = a[i] - wrand(1, a[i]);
        c[i] = wrand(-12, 12);
        d[i] = wrand(0, 999999999) * 1000;
    }
    a[0] = 1, b[0] = 0, c[0] = 1, d[0] = 0; /* every least */
    a[1] = WITNESS_SEQ_MAX, b[1] = WITNESS_SEQ_MAX - 1, c[1] = 9, d[1] = WITNESS_STAMP_MAX;
    a[2] = WITNESS_SEQ_MAX + 1, b[2] = 0, c[2] = 1, d[2] = 0; /* seq max + 1 */
    a[3] = 5, b[3] = WITNESS_SEQ_MAX + 1, c[3] = 1, d[3] = 0; /* prev max + 1 */
    a[4] = 5, b[4] = 4, c[4] = 10, d[4] = 0;                  /* a kind of two digits */
    a[5] = 5, b[5] = 4, c[5] = 1, d[5] = WITNESS_STAMP_MAX + 1;
    a[6] = -5, b[6] = -6, c[6] = -9, d[6] = -1; /* negative: sign aside, they fit */
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(witness_record_fits(a[i], b[i], c[i], d[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE,
          "Witness: the record's own edges (WR3)");
    zxapp_arg_t ra[4] = {{"seq", a, WAVE_ROWS},
                         {"prev", b, WAVE_ROWS},
                         {"kind", c, WAVE_ROWS},
                         {"stamp", d, WAVE_ROWS}};
    CHECK(wave_same(app, "audit_ok", ra, 4, want, WAVE_ROWS, true), "Witness: audit_ok, MYRA = C");
}
