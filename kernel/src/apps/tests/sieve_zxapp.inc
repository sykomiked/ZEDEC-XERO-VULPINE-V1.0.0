/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Sieve's languages against the C its model runs (sieve_rules.h).
 * Slots: @logic.sieve_suggestion_apply_ok, @num.sieve_suggestion_apply_n,
 * @num.sieve_held_suggestion_count, @logic.sieve_suggestion_hold_ok,
 * @logic.sieve_suggestion_index_ok, and, used as they are,
 * @num.witness_kind_count and @logic.ramble_consent_covers. */
#include "ramble_rules.h"
#include "sieve_rules.h"
#include "witness_rules.h"

APP_TEST(sieve, 16, 63)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];

    /* the C rules' own edges */
    CHECK(
        sieve_seconds_left(1000, 300, 1000) == 300 && sieve_seconds_left(1000, 300, 1299) == 1 &&
            sieve_seconds_left(1000, 300, 1300) == 0 && sieve_seconds_left(1000, 300, 9999) == 0 &&
            sieve_seconds_left(1000, 0, 1000) == SIEVE_REFUSED &&
            sieve_seconds_left(1000, SIEVE_HOLD_MAX + 1, 1000) == SIEVE_REFUSED &&
            sieve_seconds_left(-1, 300, 1000) == SIEVE_REFUSED &&
            sieve_seconds_left(1000, 300, SIEVE_NOW_MAX + 1) == SIEVE_REFUSED &&
            sieve_seconds_left(SIEVE_NOW_MAX, SIEVE_HOLD_MAX, 0) == SIEVE_NOW_MAX + SIEVE_HOLD_MAX,
        "Sieve: seconds left of a hold (SR6)");
    CHECK(sieve_drift(7, 7) == 0 && sieve_drift(8, 7) == 1 && sieve_drift(0, 1) == -1 &&
              sieve_drift(-1, 0) == INT64_MIN &&
              sieve_drift(0, SIEVE_VERSION_MAX + 1) == INT64_MIN &&
              sieve_drift(SIEVE_VERSION_MAX, 0) == SIEVE_VERSION_MAX,
          "Sieve: a target's drift (SR7)");

    /* suggestion_apply_ok and suggestion_apply_n (SR1, SR2): every edge, then a sweep */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t k = wrand(0, 3);
        a[i] = wrand(0, 1);
        b[i] = k == 0 ? wrand(-SIEVE_HOLD_MAX, 0) : wrand(1, SIEVE_HOLD_MAX);
        c[i] = k == 1 ? wrand(-999999999999999, 999999999999999) : 0;
    }
    a[0] = 1, b[0] = 1, c[0] = 0;               /* the least that applies */
    a[1] = 1, b[1] = SIEVE_HOLD_MAX, c[1] = 0;  /* the most */
    a[2] = 0, b[2] = 600, c[2] = 0;             /* not accepted */
    a[3] = 1, b[3] = 0, c[3] = 0;               /* lapsing now */
    a[4] = 1, b[4] = -5, c[4] = 0;              /* lapsed */
    a[5] = 1, b[5] = 600, c[5] = 1;             /* the target moved on */
    a[6] = 1, b[6] = 600, c[6] = -1;            /* or back */
    a[7] = 0, b[7] = -SIEVE_HOLD_MAX, c[7] = 5; /* everything wrong */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(sieve_apply_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Sieve: the C apply rule's own edges (SR1)");
    zxapp_arg_t aa[3] = {
        {"accepted", a, WAVE_ROWS}, {"secondsleft", b, WAVE_ROWS}, {"drift", c, WAVE_ROWS}};
    CHECK(wave_same(app, "suggestion_apply_ok", aa, 3, want, WAVE_ROWS, true),
          "Sieve: suggestion_apply_ok, Eve = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = sieve_apply_n(a[i], b[i], c[i]);
    CHECK(want[0] == 1 && want[1] == 1 && want[2] == 0 && want[5] == 0 && want[7] == 0,
          "Sieve: the C apply number's own edges (SR2)");
    CHECK(wave_same(app, "suggestion_apply_n", aa, 3, want, WAVE_ROWS, false),
          "Sieve: suggestion_apply_n, C++ (wasm) = KRON = C");
    {
        /* an accept that is no flag (2, -1), or a hold longer than a day (86401) */
        static const int64_t acc[3][2] = {{1, 2}, {1, -1}, {1, 1}};
        static const int64_t left[3][2] = {{600, 600}, {600, 600}, {600, SIEVE_HOLD_MAX + 1}};
        static const int64_t none[2] = {0, 0};
        for (uint32_t k = 0; k < 3; k++) {
            zxapp_arg_t xa[3] = {
                {"accepted", acc[k], 2}, {"secondsleft", left[k], 2}, {"drift", none, 2}};
            /* KRON reads -1 as no, as Eve does: only 2 and 86401 are read apart */
            int32_t want_lp = k == 1 ? ZXAPP_TRUE : ZXAPP_BOTH;
            CHECK(wave_lp(app, "suggestion_apply_ok", xa, 3) == want_lp &&
                      !sieve_apply_ok(acc[k][1], left[k][1], 0),
                  "Sieve: suggestion_apply_ok case %u: Eve no, KRON %s", k,
                  k == 1 ? "no too: agreed" : "yes: BOTH");
            CHECK(wave_lp(app, "suggestion_apply_n", xa, 3) == ZXAPP_BOTH &&
                      sieve_apply_n(acc[k][1], left[k][1], 0) == SIEVE_REFUSED,
                  "Sieve: suggestion_apply_n case %u: C++ refuses, KRON answers: BOTH", k);
        }
    }

    /* held_suggestion_count (SR3): 64 apps' counts over up to 64 suggestions */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t ap[WAVE_ROWS], pe[WAVE_ROWS];
        uint32_t n = r < 2 ? 2u : (uint32_t) wrand(2, WAVE_ROWS);
        int64_t w = r == 2 ? SIEVE_APP_FIELD : wrand(1, 8);
        for (uint32_t i = 0; i < n; i++) {
            ap[i] = r == 2 ? SIEVE_APP_FIELD : wrand(1, 8);
            pe[i] = r == 0 ? 0 : r == 1 ? 1 : wrand(0, 1);
        }
        if (r == 1) ap[0] = ap[1] = w; /* both held for the app asked about */
        int64_t got = sieve_held_count(ap, pe, n, w);
        /* want is one value: KRON makes it a scalar (+/want), Trebago reads one */
        zxapp_arg_t ha[3] = {{"app", ap, n}, {"pending", pe, n}, {"want", &w, 1}};
        CHECK(wave_same(app, "held_suggestion_count", ha, 3, &got, 1, false),
              "Sieve: held_suggestion_count %u of %u, KRON = Trebago = C (%lld)", r, n,
              (long long) got);
    }
    {
        /* a flag past 1: KRON holds it at 1, Trebago adds it as it is */
        static const int64_t ap[2] = {5, 5}, two[2] = {1, 2}, minus[2] = {1, -1}, w[1] = {5};
        zxapp_arg_t t1[3] = {{"app", ap, 2}, {"pending", two, 2}, {"want", w, 1}};
        zxapp_arg_t t2[3] = {{"app", ap, 2}, {"pending", minus, 2}, {"want", w, 1}};
        CHECK(wave_lp(app, "held_suggestion_count", t1, 3) == ZXAPP_BOTH &&
                  wave_lp(app, "held_suggestion_count", t2, 3) == ZXAPP_BOTH,
              "Sieve: a pending flag that is no flag: KRON and Trebago disagree: BOTH");
        CHECK(sieve_held_count(ap, two, 2, 5) == SIEVE_REFUSED &&
                  sieve_held_count(ap, minus, 2, 5) == SIEVE_REFUSED &&
                  sieve_held_count(NULL, NULL, 0, 5) == 0 &&
                  sieve_held_count(NULL, two, 2, 5) == SIEVE_REFUSED,
              "Sieve: the C refuses it");
    }

    /* suggestion_hold_ok (SR4): Eve = C, each field at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, 4);
        b[i] = i % 3 ? wrand(1, SIEVE_HOLD_MAX) : wrand(-10, SIEVE_HOLD_MAX + 10);
        c[i] = i % 4 ? wrand(1600000000, 2000000000) : wrand(-10, SIEVE_NOW_MAX + 10);
    }
    a[0] = 0, b[0] = 1, c[0] = 0;                                      /* every least */
    a[1] = SIEVE_SRC_MAX, b[1] = SIEVE_HOLD_MAX, c[1] = SIEVE_NOW_MAX; /* every most */
    a[2] = SIEVE_SRC_MAX + 1, b[2] = 600, c[2] = 0;                    /* no such source */
    a[3] = -1, b[3] = 600, c[3] = 0;
    a[4] = 0, b[4] = 0, c[4] = 0;                   /* held no time */
    a[5] = 0, b[5] = SIEVE_HOLD_MAX + 1, c[5] = 0;  /* longer than a day */
    a[6] = 0, b[6] = 600, c[6] = -1;                /* before 1970 */
    a[7] = 0, b[7] = 600, c[7] = SIEVE_NOW_MAX + 1; /* past 9999 */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(sieve_hold_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Sieve: the C hold limits' own edges (SR4)");
    zxapp_arg_t la[3] = {{"source", a, WAVE_ROWS}, {"ttl", b, WAVE_ROWS}, {"made", c, WAVE_ROWS}};
    CHECK(wave_same(app, "suggestion_hold_ok", la, 3, want, WAVE_ROWS, true),
          "Sieve: suggestion_hold_ok, Eve = C");

    /* suggestion_index_ok (SR5): MYRA = C, each piece at its edges */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(1, UINT32_MAX) : wrand(-11000000000, 11000000000);
        b[i] = wrand(-1100, 1100);
        c[i] = wrand(-11, 11);
        d[i] = wrand(-11, 11);
        e[i] = i % 4 ? wrand(1600000000, 2000000000) : wrand(-1100000000000, 1100000000000);
        f[i] = i % 3 ? wrand(0, 999999999) : wrand(-SIEVE_VERSION_MAX, SIEVE_VERSION_MAX);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0, f[0] = 0; /* every least */
    a[1] = SIEVE_ID_FIELD, b[1] = SIEVE_APP_FIELD, c[1] = 9, d[1] = 9;
    e[1] = SIEVE_EXPIRES_FIELD, f[1] = SIEVE_VERSION_FIELD;                       /* every most */
    a[2] = SIEVE_ID_FIELD + 1, b[2] = 1, c[2] = 0, d[2] = 1, e[2] = 0, f[2] = 0;  /* id past i10 */
    a[3] = 1, b[3] = SIEVE_APP_FIELD + 1, c[3] = 0, d[3] = 1, e[3] = 0, f[3] = 0; /* app past i3 */
    a[4] = 1, b[4] = 1, c[4] = 10, d[4] = 1, e[4] = 0, f[4] = 0; /* source past i1 */
    a[5] = 1, b[5] = 1, c[5] = 0, d[5] = 10, e[5] = 0, f[5] = 0; /* state past i1 */
    a[6] = 1, b[6] = 1, c[6] = 0, d[6] = 1, e[6] = SIEVE_EXPIRES_FIELD + 1, f[6] = 0;
    a[7] = 1, b[7] = 1, c[7] = 0, d[7] = 1, e[7] = 0, f[7] = SIEVE_VERSION_FIELD + 1;
    a[8] = -1, b[8] = -999, c[8] = -9, d[8] = -9, e[8] = -1, f[8] = -SIEVE_VERSION_FIELD;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(sieve_record_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE && want[8] == ZXAPP_TRUE,
          "Sieve: the C record's own edges (SR5)");
    zxapp_arg_t ra[6] = {{"id", a, WAVE_ROWS},      {"app", b, WAVE_ROWS},
                         {"source", c, WAVE_ROWS},  {"state", d, WAVE_ROWS},
                         {"expires", e, WAVE_ROWS}, {"version", f, WAVE_ROWS}};
    CHECK(wave_same(app, "suggestion_index_ok", ra, 6, want, WAVE_ROWS, true),
          "Sieve: suggestion_index_ok, MYRA = C");

    /* witness.kind_count over the pending suggestions' sources (SM6) */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t k[WAVE_ROWS], wv[1];
        uint32_t n = (uint32_t) wrand(2, WAVE_ROWS);
        for (uint32_t i = 0; i < n; i++) k[i] = wrand(0, SIEVE_SRC_MAX);
        wv[0] = (int64_t) (r % 3u);
        int64_t got = witness_kind_count(k, n, wv[0]);
        zxapp_arg_t ka[2] = {{"kind", k, n}, {"want", wv, 1}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &got, 1, false),
              "Sieve: held by source %lld through witness.kind_count, KRON = Trebago = C",
              (long long) wv[0]);
    }

    /* ramble.consent_covers for an envelope's accept: its own scope and amount */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t need = wrand(0, RAMBLE_SCOPE_ALL);
        a[i] = ramble_scope_gap(need, i % 5 ? need : 0);
        b[i] = 0;
        c[i] = wrand(-10, SIEVE_HOLD_MAX);
        want[i] = T_(ramble_consent_covers(a[i], b[i], c[i]));
    }
    zxapp_arg_t ca[3] = {
        {"scopegap", a, WAVE_ROWS}, {"ceilingleft", b, WAVE_ROWS}, {"secondsleft", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ramble.consent_covers", ca, 3, want, WAVE_ROWS, true),
          "Sieve: an envelope's grant through ramble.consent_covers, Eve = Sutra = C");
}
