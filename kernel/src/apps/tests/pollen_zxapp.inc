/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pollen's languages against the C its model runs (pollen_rules.h).
 * Slots: @num.pollen_freight_count, @num.pollen_freight_packets,
 * @logic.pollen_share_limits_ok, @logic.pollen_share_record_ok, and, used
 * as they are, Witness's @num.witness_kind_count, @num.used_x100, Ferry's
 * @logic.ferry_target_fits and Hedgerow's @num.hedgerow_addr_scope. */
#include "pollen_rules.h"

APP_TEST(pollen, 27, 31)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        want[WAVE_ROWS];
    const int64_t S = POLLEN_SIZE_MAX;

    /* freight_count (PR1) and freight_packets (PR2) on the same rows: edges, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 3 ? wrand(0, POLLEN_FILE_MAX) : wrand(0, S);
        b[i] = wrand(1, POLLEN_K_MAX);
    }
    a[0] = 0, b[0] = 1;                          /* an empty file */
    a[1] = 1, b[1] = 1;                          /* one byte */
    a[2] = 20, b[2] = 1;                         /* one row, full */
    a[3] = 21, b[3] = 1;                         /* one byte past it */
    a[4] = 3360, b[4] = POLLEN_K_MAX;            /* one freight, full */
    a[5] = 3361, b[5] = POLLEN_K_MAX;            /* one byte past it */
    a[6] = S, b[6] = 1;                          /* the most freights */
    a[7] = S, b[7] = POLLEN_K_MAX;               /* the largest size, the fewest */
    a[8] = POLLEN_FILE_MAX, b[8] = POLLEN_K_MAX; /* the largest file an offer takes */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = pollen_freights(a[i], b[i]);
    CHECK(want[0] == 0 && want[1] == 1 && want[2] == 1 && want[3] == 2 && want[4] == 1 &&
              want[5] == 2 && want[6] == INT64_C(50000000000) && want[7] == INT64_C(297619048) &&
              want[8] == 4994 && pollen_freights(-1, 1) == POLLEN_REFUSED &&
              pollen_freights(S + 1, 1) == POLLEN_REFUSED &&
              pollen_freights(1, 0) == POLLEN_REFUSED && pollen_freights(1, 169) == POLLEN_REFUSED,
          "Pollen: the C freights' own edges (PR1)");
    zxapp_arg_t fa[2] = {{"size", a, WAVE_ROWS}, {"k", b, WAVE_ROWS}};
    CHECK(wave_same(app, "freight_count", fa, 2, want, WAVE_ROWS, false),
          "Pollen: freight_count, Trebago = KRON = C");
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = pollen_packets(a[i], b[i]);
    CHECK(want[0] == 0 && want[3] == 2 && want[5] == 336 && want[6] == INT64_C(50000000000) &&
              want[8] == 4994 * 168 && pollen_packets(100, 5) == 5 &&
              pollen_packets(101, 5) == 10 && pollen_packets(1, 169) == POLLEN_REFUSED,
          "Pollen: the C packets' own edges (PR2)");
    CHECK(wave_same(app, "freight_packets", fa, 2, want, WAVE_ROWS, false),
          "Pollen: freight_packets, Trebago = KRON = C");
    {
        /* a k past freight.pack's 168 rows, a size past 12 digits: Trebago refuses, KRON answers */
        static const int64_t sz[2] = {100, 100}, kk[2] = {5, 169};
        static const int64_t big[2] = {100, POLLEN_SIZE_MAX + 1}, k5[2] = {5, 5};
        zxapp_arg_t xa[2] = {{"size", sz, 2}, {"k", kk, 2}};
        zxapp_arg_t ya[2] = {{"size", big, 2}, {"k", k5, 2}};
        CHECK(wave_lp(app, "freight_count", xa, 2) == ZXAPP_BOTH,
              "Pollen: freights at k 169: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "freight_packets", xa, 2) == ZXAPP_BOTH,
              "Pollen: packets at k 169: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "freight_count", ya, 2) == ZXAPP_BOTH,
              "Pollen: a size past 12 digits: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "freight_packets", ya, 2) == ZXAPP_BOTH,
              "Pollen: packets for a size past 12 digits: BOTH");
    }

    /* share_limits_ok (PR3): Eve = C, every edge, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-2, POLLEN_FILE_MAX + 2);
        b[i] = wrand(-1, POLLEN_FILES_MAX + 2);
        c[i] = wrand(-1, POLLEN_OPEN_MAX + 1);
    }
    a[0] = 1, b[0] = 1, c[0] = 0;                                                /* least */
    a[1] = POLLEN_FILE_MAX, b[1] = POLLEN_FILES_MAX, c[1] = POLLEN_OPEN_MAX - 1; /* most */
    a[2] = 0, b[2] = 1, c[2] = 0;                                                /* empty */
    a[3] = POLLEN_FILE_MAX + 1, b[3] = 1, c[3] = 0;                              /* a file + 1 */
    a[4] = 1, b[4] = 0, c[4] = 0;                                                /* no files */
    a[5] = 1, b[5] = POLLEN_FILES_MAX + 1, c[5] = 0;                             /* files + 1 */
    a[6] = 1, b[6] = 1, c[6] = POLLEN_OPEN_MAX;                                  /* all open */
    a[7] = 1, b[7] = 1, c[7] = -1;                                               /* negative */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) want[i] = T_(pollen_share_limits_ok(a[i], b[i], c[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
          "Pollen: the C limits' own edges (PR3)");
    zxapp_arg_t la[3] = {{"bytes", a, WAVE_ROWS}, {"files", b, WAVE_ROWS}, {"open", c, WAVE_ROWS}};
    CHECK(wave_same(app, "share_limits_ok", la, 3, want, WAVE_ROWS, true),
          "Pollen: share_limits_ok, Eve = C");

    /* share_record_ok (PR4): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-12000, 12000);
        b[i] = wrand(-120, 120);
        c[i] = wrand(-1200000000, 1200000000);
        d[i] = wrand(-12, 12);
        e[i] = wrand(-1200000000000000, 1200000000000000);
    }
    a[0] = 0, b[0] = 0, c[0] = 0, d[0] = 0, e[0] = 0;
    a[1] = 9999, b[1] = 99, c[1] = 999999999, d[1] = 9, e[1] = POLLEN_STAMP_MAX;
    a[2] = 10000, b[2] = 1, c[2] = 1, d[2] = 1, e[2] = 1;
    a[3] = 1, b[3] = 100, c[3] = 1, d[3] = 1, e[3] = 1;
    a[4] = 1, b[4] = 1, c[4] = 1000000000, d[4] = 1, e[4] = 1;
    a[5] = 1, b[5] = 1, c[5] = 1, d[5] = 10, e[5] = 1;
    a[6] = 1, b[6] = 1, c[6] = 1, d[6] = 1, e[6] = POLLEN_STAMP_MAX + 1;
    a[7] = -9999, b[7] = -99, c[7] = -999999999, d[7] = -9, e[7] = -POLLEN_STAMP_MAX;
    a[8] = 3, b[8] = 2, c[8] = 4096, d[8] = POLLEN_OPEN, e[8] = INT64_C(1760000000000);
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(pollen_record_fits(a[i], b[i], c[i], d[i], e[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE && want[8] == ZXAPP_TRUE,
          "Pollen: the record's own edges (PR4)");
    zxapp_arg_t ra[5] = {{"offer", a, WAVE_ROWS},
                         {"files", b, WAVE_ROWS},
                         {"bytes", c, WAVE_ROWS},
                         {"state", d, WAVE_ROWS},
                         {"stamp", e, WAVE_ROWS}};
    CHECK(wave_same(app, "share_record_ok", ra, 5, want, WAVE_ROWS, true),
          "Pollen: an offer makes a ^ZXPOLLEN record, MYRA = C");

    /* ferry.target_fits: does a file fit on the receiver, the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(0, INT64_C(1) << 36);
        b[i] = wrand(1, POLLEN_FILE_MAX);
        c[i] = i % 2 ? POLLEN_SPARE : wrand(0, a[i]);
        want[i] = T_(ferry_fits(a[i], b[i], c[i]));
    }
    a[0] = POLLEN_SPARE + 4096, b[0] = 4096, c[0] = POLLEN_SPARE; /* exactly full */
    a[1] = POLLEN_SPARE + 4095, b[1] = 4096, c[1] = POLLEN_SPARE; /* one byte short */
    want[0] = T_(ferry_fits(a[0], b[0], c[0]));
    want[1] = T_(ferry_fits(a[1], b[1], c[1]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE, "Pollen: the receiver's own edges");
    zxapp_arg_t ta[3] = {{"avail", a, WAVE_ROWS}, {"need", b, WAVE_ROWS}, {"spare", c, WAVE_ROWS}};
    CHECK(wave_same(app, "ferry.target_fits", ta, 3, want, WAVE_ROWS, true),
          "Pollen: fits on the receiver through ferry.target_fits, Sutra = KRON = C");

    /* hedgerow.addr_scope: which peers are nearby, the model's C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = 4;
        b[i] = i < 8 ? (int64_t[]){127, 10, 172, 192, 169, 8, 0, 224}[i] : wrand(0, 255);
        c[i] = i < 8 ? (int64_t[]){0, 1, 16, 168, 254, 8, 0, 0}[i] : wrand(0, 255);
        d[i] = wrand(0, 255);
        e[i] = wrand(0, 255);
        want[i] = hedgerow_addr_scope(a[i], b[i], c[i], d[i], e[i]);
    }
    CHECK(pollen_nearby_scope(want[0]) && pollen_nearby_scope(want[1]) &&
              pollen_nearby_scope(want[2]) && pollen_nearby_scope(want[3]) &&
              pollen_nearby_scope(want[4]) && !pollen_nearby_scope(want[5]) &&
              !pollen_nearby_scope(want[6]) && !pollen_nearby_scope(want[7]),
          "Pollen: loopback, private and link-local are nearby; public and none are not");
    zxapp_arg_t sa[5] = {{"fam", a, WAVE_ROWS},
                         {"a", b, WAVE_ROWS},
                         {"b", c, WAVE_ROWS},
                         {"c", d, WAVE_ROWS},
                         {"d", e, WAVE_ROWS}};
    CHECK(wave_same(app, "hedgerow.addr_scope", sa, 5, want, WAVE_ROWS, false),
          "Pollen: each peer's scope through hedgerow.addr_scope");

    /* witness.kind_count and used_share.used_x100 over offers and carries: the model's C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        int64_t st[16], one[1], tot[2], left[2], prog[2];
        uint32_t n = 1u + r % 16u;
        for (uint32_t i = 0; i < n; i++) st[i] = wrand(POLLEN_DRAFTED, POLLEN_WITHDRAWN);
        one[0] = r == 1 ? 9 : wrand(POLLEN_DRAFTED, POLLEN_WITHDRAWN);
        int64_t count = witness_kind_count(st, n, one[0]);
        int64_t k = wrand(1, POLLEN_K_MAX), size = wrand(1, POLLEN_FILE_MAX);
        tot[0] = tot[1] = pollen_packets(size, k);
        left[0] = k * wrand(0, pollen_freights(size, k));
        left[1] = r == 2 ? tot[1] : 0;
        for (uint32_t i = 0; i < 2; i++) prog[i] = ferry_progress_x100(tot[i], left[i]);
        zxapp_arg_t ka[2] = {{"kind", st, n}, {"want", one, 1}};
        zxapp_arg_t ua[2] = {{"total", tot, 2}, {"left", left, 2}};
        CHECK(wave_same(app, "witness.kind_count", ka, 2, &count, 1, false),
              "Pollen: %u offers, those in state %lld through witness.kind_count", n,
              (long long) one[0]);
        CHECK(prog[1] == (r == 2 ? 0 : 10000) &&
                  wave_same(app, "used_share.used_x100", ua, 2, prog, 2, false),
              "Pollen: carry %u, progress over packets through used_share.used_x100", r);
    }
    {
        static const int64_t k2[2] = {1, 2}, w2[2] = {1, 1}, tt[2] = {1000, 1000},
                             lf[2] = {0, 1001};
        zxapp_arg_t ka[2] = {{"kind", k2, 2}, {"want", w2, 2}};
        CHECK(wave_lp(app, "witness.kind_count", ka, 2) == ZXAPP_BOTH,
              "Pollen: two states at once: Trebago refuses, KRON sums them: BOTH");
        zxapp_arg_t ua[2] = {{"total", tt, 2}, {"left", lf, 2}};
        CHECK(wave_lp(app, "used_share.used_x100", ua, 2) == ZXAPP_BOTH &&
                  ferry_progress_x100(1000, 1001) == FERRY_REFUSED,
              "Pollen: more packets left than needed: Sutra refuses, KRON answers: BOTH");
        static const int64_t av[2] = {1000, FERRY_BYTES_MAX + 1}, nd[2] = {600, 0},
                             sp[2] = {400, 0};
        zxapp_arg_t xa[3] = {{"avail", av, 2}, {"need", nd, 2}, {"spare", sp, 2}};
        CHECK(wave_lp(app, "ferry.target_fits", xa, 3) == ZXAPP_BOTH,
              "Pollen: a receiver past 9(15) bytes: Sutra refuses, KRON answers: BOTH");
    }
}
