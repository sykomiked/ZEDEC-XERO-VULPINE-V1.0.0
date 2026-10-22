/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Pawprint's languages against the C its model runs (pawprint_rules.h).
 * Slots: @logic.pawprint_may_attach, @num.pawprint_breakpoint_hit,
 * @num.pawprint_stepped_insns, @logic.pawprint_debug_limits_ok,
 * @logic.pawprint_break_record_ok. */
#include "pawprint_rules.h"

APP_TEST(pawprint, 7, 122)
{
    static int64_t a[PAW_BREAK_MAX + 1], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], want[WAVE_ROWS];

    /* may_attach (PR1): flags 0 or 1, every row, Eve = KRON = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i < 8 ? (int64_t) (i & 1u) : wrand(0, 1);
        b[i] = i < 8 ? (int64_t) ((i >> 1) & 1u) : wrand(0, 1);
        c[i] = i < 8 ? (int64_t) ((i >> 2) & 1u) : wrand(0, 1);
        want[i] = T_(pawprint_may_attach(a[i], b[i], c[i]));
    }
    CHECK(want[3] == ZXAPP_TRUE && want[7] == ZXAPP_FALSE && want[1] == ZXAPP_FALSE &&
              want[2] == ZXAPP_FALSE && want[0] == ZXAPP_FALSE && !pawprint_may_attach(2, 1, 0) &&
              !pawprint_may_attach(1, 1, -1),
          "Pawprint: the C attach rule, all eight flag rows and two that are no flags");
    {
        zxapp_arg_t ma[3] = {
            {"own", a, WAVE_ROWS}, {"approved", b, WAVE_ROWS}, {"system", c, WAVE_ROWS}};
        CHECK(wave_same(app, "may_attach", ma, 3, want, WAVE_ROWS, true),
              "Pawprint: may_attach, Eve = KRON = C");
        /* a flag that is no flag: Eve does not fit it, KRON reads it as yes: held */
        static const int64_t one[2] = {1, 1}, two[2] = {1, 2}, zero[2] = {0, 0}, neg[2] = {0, -1};
        zxapp_arg_t xa[3] = {{"own", two, 2}, {"approved", one, 2}, {"system", zero, 2}};
        zxapp_arg_t ya[3] = {{"own", one, 2}, {"approved", one, 2}, {"system", neg, 2}};
        zxapp_arg_t za[3] = {{"own", one, 2}, {"approved", two, 2}, {"system", zero, 2}};
        CHECK(wave_lp(app, "may_attach", xa, 3) == ZXAPP_BOTH,
              "Pawprint: own 2: Eve FALSE, KRON TRUE: BOTH");
        CHECK(wave_lp(app, "may_attach", ya, 3) == ZXAPP_BOTH,
              "Pawprint: system -1: Eve FALSE, KRON TRUE: BOTH");
        CHECK(wave_lp(app, "may_attach", za, 3) == ZXAPP_BOTH,
              "Pawprint: approved 2: Eve FALSE, KRON TRUE: BOTH");
    }

    /* breakpoint_hit (PR2): 64 files' breakpoints, sorted and each once,
     * the line one of them or not, KRON = Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = r < 4 ? r + 1u : (uint32_t) wrand(1, r < 8 ? PAW_BREAK_MAX : 64);
        int64_t at = 0, hit = -1, line;
        for (uint32_t i = 0; i < n; i++) {
            at += wrand(1, 5000);
            a[i] = at;
        }
        line = r % 2 ? a[wrand(0, n - 1)] : wrand(1, at + 10);
        if (r == 2) line = 1; /* the first line of a file */
        CHECK(pawprint_breakpoint_hit(a, n, line, &hit) && (hit == 0 || hit == 1),
              "Pawprint: the C finds line %lld among %u breakpoints", (long long) line, n);
        zxapp_arg_t ha[2] = {{"stops", a, n}, {"line", &line, 1}};
        CHECK(wave_same(app, "breakpoint_hit", ha, 2, &hit, 1, false),
              "Pawprint: breakpoint_hit file %u (%u stops, line %lld: %lld), KRON = Trebago = C", r,
              n, (long long) line, (long long) hit);
    }
    {
        /* the edges: one stop, the first and the last line, 256 stops */
        static const int64_t first[1] = {1}, last[2] = {1, PAW_LINE_MAX}, l1 = 1,
                             lmax = PAW_LINE_MAX, l2 = 2, one = 1, none = 0;
        int64_t hit = -1;
        zxapp_arg_t e1[2] = {{"stops", first, 1}, {"line", &l1, 1}};
        zxapp_arg_t e2[2] = {{"stops", last, 2}, {"line", &lmax, 1}};
        zxapp_arg_t e3[2] = {{"stops", last, 2}, {"line", &l2, 1}};
        CHECK(pawprint_breakpoint_hit(first, 1, 1, &hit) && hit == 1 &&
                  wave_same(app, "breakpoint_hit", e1, 2, &one, 1, false),
              "Pawprint: a lone breakpoint on line 1 is hit");
        CHECK(pawprint_breakpoint_hit(last, 2, PAW_LINE_MAX, &hit) && hit == 1 &&
                  wave_same(app, "breakpoint_hit", e2, 2, &one, 1, false),
              "Pawprint: the last line there is is hit");
        CHECK(pawprint_breakpoint_hit(last, 2, 2, &hit) && hit == 0 &&
                  wave_same(app, "breakpoint_hit", e3, 2, &none, 1, false),
              "Pawprint: a line between two breakpoints is not");
        for (uint32_t i = 0; i <= (uint32_t) PAW_BREAK_MAX; i++) a[i] = 2 * (int64_t) i + 1;
        int64_t on = 511, off = 510;
        zxapp_arg_t f1[2] = {{"stops", a, PAW_BREAK_MAX}, {"line", &on, 1}};
        zxapp_arg_t f2[2] = {{"stops", a, PAW_BREAK_MAX}, {"line", &off, 1}};
        CHECK(pawprint_breakpoint_hit(a, PAW_BREAK_MAX, on, &hit) && hit == 1 &&
                  wave_same(app, "breakpoint_hit", f1, 2, &one, 1, false) &&
                  wave_same(app, "breakpoint_hit", f2, 2, &none, 1, false),
              "Pawprint: 256 breakpoints, the last one hit, the line before it not");
        /* what PR2 refuses: Trebago refuses it, KRON answers: held */
        static const int64_t unsorted[2] = {10, 3}, twice[2] = {3, 3}, low[2] = {0, 3},
                             high[2] = {3, PAW_LINE_MAX + 1}, ten = 10, three = 3, zero = 0,
                             past = PAW_LINE_MAX + 1, lines2[2] = {3, 10};
        const int64_t *vs[4] = {unsorted, twice, low, high};
        const int64_t ls[4] = {ten, three, three, three};
        static const char *const why[4] = {"out of order", "a line twice", "a stop on line 0",
                                           "a stop past the last line"};
        for (uint32_t k = 0; k < 4; k++) {
            zxapp_arg_t ba[2] = {{"stops", vs[k], 2}, {"line", &ls[k], 1}};
            CHECK(!pawprint_breakpoint_hit(vs[k], 2, ls[k], &hit) &&
                      wave_lp(app, "breakpoint_hit", ba, 2) == ZXAPP_BOTH,
                  "Pawprint: %s: the C and Trebago refuse, KRON answers: BOTH", why[k]);
        }
        zxapp_arg_t z1[2] = {{"stops", lines2, 2}, {"line", &zero, 1}};
        zxapp_arg_t z2[2] = {{"stops", lines2, 2}, {"line", &past, 1}};
        zxapp_arg_t z3[2] = {{"stops", lines2, 2}, {"line", lines2, 2}};
        zxapp_arg_t z4[2] = {{"stops", a, PAW_BREAK_MAX + 1}, {"line", &on, 1}};
        CHECK(!pawprint_breakpoint_hit(lines2, 2, 0, &hit) &&
                  !pawprint_breakpoint_hit(lines2, 2, PAW_LINE_MAX + 1, &hit) &&
                  !pawprint_breakpoint_hit(a, PAW_BREAK_MAX + 1, on, &hit),
              "Pawprint: the C refuses line 0, a line past the last and 257 breakpoints");
        CHECK(wave_lp(app, "breakpoint_hit", z1, 2) == ZXAPP_BOTH &&
                  wave_lp(app, "breakpoint_hit", z2, 2) == ZXAPP_BOTH,
              "Pawprint: line 0 and a line past the last: Trebago refuses, KRON answers: BOTH");
        CHECK(wave_lp(app, "breakpoint_hit", z3, 2) == ZXAPP_BOTH,
              "Pawprint: two lines for one call: Trebago refuses, KRON sums them: BOTH");
        CHECK(wave_lp(app, "breakpoint_hit", z4, 2) == ZXAPP_BOTH,
              "Pawprint: 257 breakpoints: Trebago refuses, KRON answers: BOTH");
        CHECK(pawprint_breakpoint_hit(a, 0, 5, &hit) && hit == 0,
              "Pawprint: no breakpoints hit nothing");
    }

    /* stepped_insns (PR3): 64 sessions, each an exact sum, Trebago = C */
    for (uint32_t r = 0; r < WAVE_ROWS; r++) {
        uint32_t n = (uint32_t) wrand(1, WAVE_ROWS);
        int64_t total = -1;
        for (uint32_t i = 0; i < n; i++)
            a[i] = r == 0 ? 0 : r == 1 ? PAW_COUNT_MAX : wrand(0, r < 32 ? 50 : PAW_COUNT_MAX);
        CHECK(pawprint_stepped_insns(a, n, &total) && total >= 0, "Pawprint: the C sums session %u",
              r);
        zxapp_arg_t sa = {"insns", a, n};
        CHECK(wave_same(app, "stepped_insns", &sa, 1, &total, 1, false),
              "Pawprint: stepped_insns session %u (%lld), Trebago = C", r, (long long) total);
    }
    {
        static const int64_t top[2] = {INT64_MAX - 5, 5}, tw = INT64_MAX, over[2] = {INT64_MAX, 1},
                             neg[3] = {5, -1, 7};
        int64_t total = 0;
        zxapp_arg_t ta = {"insns", top, 2}, oa = {"insns", over, 2}, na = {"insns", neg, 3};
        CHECK(pawprint_stepped_insns(top, 2, &total) && total == INT64_MAX &&
                  wave_same(app, "stepped_insns", &ta, 1, &tw, 1, false),
              "Pawprint: a session of exactly INT64_MAX instructions");
        CHECK(!pawprint_stepped_insns(over, 2, &total) && !pawprint_stepped_insns(neg, 3, &total) &&
                  wave_lp(app, "stepped_insns", &oa, 1) != ZXAPP_TRUE &&
                  wave_lp(app, "stepped_insns", &na, 1) != ZXAPP_TRUE,
              "Pawprint: past int64 and a negative count: refused, never wrapped");
    }

    /* debug_limits_ok (PR4): Eve's ranges at and past every edge, and a sweep */
    {
        static const int64_t eb[8] = {0, 256, 257, -1, 10, 10, 256, 0},
                             ef[8] = {0, 64, 0, 0, 65, -1, 64, 65};
        for (uint32_t i = 0; i < 8; i++) want[i] = T_(pawprint_limits_ok(eb[i], ef[i]));
        CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
                  want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[7] == ZXAPP_FALSE,
              "Pawprint: debug_limits_ok, the C's own edges");
        zxapp_arg_t la[2] = {{"breakpoints", eb, 8}, {"frames", ef, 8}};
        CHECK(wave_same(app, "debug_limits_ok", la, 2, want, 8, true),
              "Pawprint: debug_limits_ok edges (Eve = C)");
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            b[i] = wrand(-3, 260);
            c[i] = wrand(-3, 68);
            want[i] = T_(pawprint_limits_ok(b[i], c[i]));
        }
        zxapp_arg_t sa[2] = {{"breakpoints", b, WAVE_ROWS}, {"frames", c, WAVE_ROWS}};
        CHECK(wave_same(app, "debug_limits_ok", sa, 2, want, WAVE_ROWS, true),
              "Pawprint: debug_limits_ok sweep (Eve = C)");
    }

    /* break_record_ok (PR5): MYRA's digit widths, i2 i7 i9 i9 */
    {
        for (uint32_t i = 0; i < WAVE_ROWS; i++) {
            a[i] = wrand(-199, 199);
            b[i] = wrand(-19999999, 19999999);
            c[i] = wrand(-1999999999, 1999999999);
            d[i] = wrand(-1999999999, 1999999999);
            want[i] = T_(pawprint_record_ok(a[i], b[i], c[i], d[i]));
        }
        zxapp_arg_t ra[4] = {{"file", a, WAVE_ROWS},
                             {"line", b, WAVE_ROWS},
                             {"condition", c, WAVE_ROWS},
                             {"hits", d, WAVE_ROWS}};
        CHECK(wave_same(app, "break_record_ok", ra, 4, want, WAVE_ROWS, true),
              "Pawprint: break_record_ok sweep (MYRA = C)");
        static const int64_t ea[7] = {0, 99, 100, -99, 1, 1, 1},
                             eb[7] = {1, PAW_LINE_MAX, 1, -1, PAW_LINE_MAX + 1, 1, 1},
                             ec[7] = {0, PAW_COUNT_MAX, 0, 0, 0, PAW_COUNT_MAX + 1, 0},
                             ed[7] = {0, PAW_COUNT_MAX, 0, 0, 0, 0, PAW_COUNT_MAX + 1};
        static const int64_t ew[7] = {ZXAPP_TRUE,  ZXAPP_TRUE,  ZXAPP_FALSE, ZXAPP_TRUE,
                                      ZXAPP_FALSE, ZXAPP_FALSE, ZXAPP_FALSE};
        for (uint32_t i = 0; i < 7; i++)
            CHECK(T_(pawprint_record_ok(ea[i], eb[i], ec[i], ed[i])) == ew[i],
                  "Pawprint: the C record rule at edge %u", i);
        zxapp_arg_t xa[4] = {
            {"file", ea, 7}, {"line", eb, 7}, {"condition", ec, 7}, {"hits", ed, 7}};
        CHECK(wave_same(app, "break_record_ok", xa, 4, ew, 7, true),
              "Pawprint: break_record_ok at 0, the widest values either side, and one past "
              "each");
    }
}
