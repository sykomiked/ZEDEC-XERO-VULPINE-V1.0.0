/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Kindred's languages against the C its model runs (kindred_rules.h).
 * Slots: @logic.kindred_contact_fields_ok, @num.kindred_name_prefix_hit,
 * @num.kindred_contact_dupes, @logic.kindred_contact_record_ok and
 * @logic.kindred_name_index_ok. */
#include "kindred_rules.h"

/* the code of a C string (KR2) */
static int64_t kd_code(const char *s, int64_t *len)
{
    return kindred_name_code(s, (uint32_t) strlen(s), len);
}

APP_TEST(kindred, 8, 44)
{
    static int64_t a[WAVE_ROWS], b[WAVE_ROWS], c[WAVE_ROWS], d[WAVE_ROWS], e[WAVE_ROWS],
        f[WAVE_ROWS], want[WAVE_ROWS];
    int64_t la = -1, lb = -1, lc = -1, ld = -1;

    /* the C rules' own edges (KR1-KR7) */
    CHECK(kd_code("Ada", &la) == 10401000000000000 && la == 3 &&
              kd_code("a d-a!", &lb) == 10401000000000000 && lb == 3 &&
              kd_code("Zoe9", &lc) == 261505360000000000 && lc == 4 && kd_code("", &ld) == 0 &&
              ld == 0 && kd_code(" .,", NULL) == 0 &&
              kd_code("abcdefghijk", &la) == 10203040506070809 && la == 9,
          "Kindred: name codes (KR2)");
    CHECK(kd_code("\xc3\xa9mile", &la) == 371309120500000000 && la == 5,
          "Kindred: a character past ASCII is one code, 37 (KR2)");
    CHECK(kindred_fields_ok(1, 0, 0, 0) && kindred_fields_ok(128, 15, 254, 1024) &&
              !kindred_fields_ok(0, 0, 0, 0) && !kindred_fields_ok(129, 0, 0, 0) &&
              !kindred_fields_ok(5, 16, 0, 0) && !kindred_fields_ok(5, -1, 0, 0) &&
              !kindred_fields_ok(5, 0, 255, 0) && !kindred_fields_ok(5, 0, 0, 1025) &&
              !kindred_fields_ok(5, 0, 0, -1),
          "Kindred: field limits at their edges (KR1)");
    int64_t adaline = kd_code("Adaline", NULL), ad = kd_code("Ad", NULL);
    int64_t pa_ = kd_code("ada", &la), pb_ = kd_code("adb", &lb);
    CHECK(la == 3 && lb == 3 && kindred_prefix_hit(adaline, pa_, la) == 1 &&
              kindred_prefix_hit(adaline, pb_, lb) == 0 && kindred_prefix_hit(ad, pa_, la) == 0 &&
              kindred_prefix_hit(5, 0, 0) == 1 &&
              kindred_prefix_hit(KINDRED_NAME_CODE_MAX, 0, 0) == 1 &&
              kindred_prefix_hit(1, 1, 10) == KINDRED_REFUSED &&
              kindred_prefix_hit(1, 1, -1) == KINDRED_REFUSED &&
              kindred_prefix_hit(KINDRED_NAME_CODE_MAX + 1, 0, 0) == KINDRED_REFUSED &&
              kindred_prefix_hit(1, -1, 0) == KINDRED_REFUSED,
          "Kindred: the prefix rule (KR3)");
    {
        static const int64_t cd[6] = {5, 7, 5, 0, 0, 0}, nm[6] = {11, 12, 13, 13, 0, 0},
                             bad[2] = {5, -3}, one[2] = {1, 1};
        int64_t out[6];
        CHECK(kindred_dupes(cd, nm, 6, out) == 0 && out[0] == 1 && out[1] == 0 && out[2] == 2 &&
                  out[3] == 1 && out[4] == 0 && out[5] == 0 &&
                  kindred_dupes(bad, one, 2, out) == KINDRED_REFUSED &&
                  kindred_dupes(cd, nm, KINDRED_ROWS_MAX + 1u, out) == KINDRED_REFUSED,
              "Kindred: likely duplicates (KR4)");
    }
    CHECK(kindred_record_ok(1, 0, 0, 1, 1, 1792627200000) &&
              kindred_record_ok(-999999, -1, 2, 3, -9, 0) &&
              !kindred_record_ok(1000000, 0, 0, 0, 0, 0) &&
              !kindred_record_ok(1, 1000000000000000, 0, 0, 0, 0) &&
              !kindred_record_ok(1, 0, 0, 0, 10, 0) && kindred_index_ok(1, 1, 1) &&
              !kindred_index_ok(1, 1, 10000000000000),
          "Kindred: the records' own edges (KR5, KR6)");
    CHECK(kindred_id_code("0000000000000000000000000000000000000000000000000000000000000000") ==
                  1 &&
              kindred_id_code("ffffffffffff0000000000000000000000000000000000000000000000000000") ==
                  INT64_C(281474976710656) &&
              kindred_id_code("abc") == KINDRED_REFUSED &&
              kindred_id_code("FFFFFFFFFFFF0000000000000000000000000000000000000000000000000000") ==
                  KINDRED_REFUSED,
          "Kindred: card and node codes (KR7)");

    /* contact_fields_ok (KR1): Eve = KRON = C, each range at its edges, then swept */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-5, 140);
        b[i] = wrand(-3, 18);
        c[i] = wrand(-5, 270);
        d[i] = wrand(-5, 1100);
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 0;
    a[1] = 128, b[1] = 15, c[1] = 254, d[1] = 1024;
    a[2] = 0, b[2] = 5, c[2] = 0, d[2] = 0;
    a[3] = 129, b[3] = 5, c[3] = 0, d[3] = 0;
    a[4] = 10, b[4] = 16, c[4] = 0, d[4] = 0;
    a[5] = 10, b[5] = -1, c[5] = 0, d[5] = 0;
    a[6] = 10, b[6] = 5, c[6] = 255, d[6] = 0;
    a[7] = 10, b[7] = 5, c[7] = 0, d[7] = 1025;
    a[8] = 10, b[8] = 5, c[8] = -1, d[8] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(kindred_fields_ok(a[i], b[i], c[i], d[i]));
    zxapp_arg_t fa[4] = {{"namelen", a, WAVE_ROWS},
                         {"phone", b, WAVE_ROWS},
                         {"mail", c, WAVE_ROWS},
                         {"note", d, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[7] == ZXAPP_FALSE && want[8] == ZXAPP_FALSE &&
              wave_same(app, "contact_fields_ok", fa, 4, want, WAVE_ROWS, true),
          "Kindred: contact_fields_ok, Eve = KRON = C");

    /* name_prefix_hit (KR3): KRON = Trebago = C, prefixes cut from swept names */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        int64_t name = wrand(0, KINDRED_NAME_CODE_MAX);
        int64_t len = wrand(0, 9);
        int64_t p = kindred_pow100((uint32_t) (9 - len));
        int64_t prefix = name / p * p;
        if (i % 3 == 0) prefix = wrand(0, KINDRED_NAME_CODE_MAX) / p * p; /* mostly a miss */
        a[i] = name, b[i] = prefix, c[i] = len;
        want[i] = kindred_prefix_hit(name, prefix, len);
    }
    a[0] = 0, b[0] = 0, c[0] = 0;
    a[1] = KINDRED_NAME_CODE_MAX, b[1] = 0, c[1] = 0;
    a[2] = KINDRED_NAME_CODE_MAX, b[2] = KINDRED_NAME_CODE_MAX, c[2] = 9;
    a[3] = 102030000000000000, b[3] = 103000000000000000, c[3] = 1;
    a[4] = 102099999999999999, b[4] = 102000000000000000, c[4] = 2;
    a[5] = 102100000000000000, b[5] = 102000000000000000, c[5] = 2;
    for (uint32_t i = 0; i < 6u; i++) want[i] = kindred_prefix_hit(a[i], b[i], c[i]);
    zxapp_arg_t pa[3] = {{"name", a, WAVE_ROWS}, {"prefix", b, WAVE_ROWS}, {"len", c, WAVE_ROWS}};
    CHECK(want[0] == 1 && want[1] == 1 && want[2] == 1 && want[3] == 0 && want[4] == 1 &&
              want[5] == 0 && wave_same(app, "name_prefix_hit", pa, 3, want, WAVE_ROWS, false),
          "Kindred: name_prefix_hit, KRON = Trebago = C");
    {
        /* a len of 10: Trebago refuses, KRON answers 1/100: BOTH */
        static const int64_t nm[2] = {1, 1}, pf[2] = {1, 1}, ln[2] = {1, 10};
        zxapp_arg_t xa[3] = {{"name", nm, 2}, {"prefix", pf, 2}, {"len", ln, 2}};
        CHECK(wave_lp(app, "name_prefix_hit", xa, 3) == ZXAPP_BOTH,
              "Kindred: len 10: Trebago refuses, KRON answers: BOTH");
        /* a name code past 18 digits: Trebago refuses, KRON answers: BOTH */
        static const int64_t big[2] = {1, KINDRED_NAME_CODE_MAX + 1}, zero[2] = {0, 0};
        zxapp_arg_t ya[3] = {{"name", big, 2}, {"prefix", zero, 2}, {"len", zero, 2}};
        CHECK(wave_lp(app, "name_prefix_hit", ya, 3) == ZXAPP_BOTH,
              "Kindred: a 19-digit name code: Trebago refuses, KRON answers: BOTH");
    }

    /* contact_dupes (KR4): KRON = Trebago = C, codes drawn from a few so they repeat */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        e[i] = i % 4 ? wrand(0, 12) : 0;
        if (e[i] == 12) e[i] = KINDRED_CARD_CODE_MAX;
        f[i] = i % 5 ? 100000000000000000 * wrand(0, 9) : wrand(0, KINDRED_NAME_CODE_MAX);
    }
    f[1] = KINDRED_NAME_CODE_MAX;
    CHECK(kindred_dupes(e, f, WAVE_ROWS, want) == 0, "Kindred: the C counts");
    {
        int64_t most = 0;
        for (uint32_t i = 0; i < WAVE_ROWS; i++) most = want[i] > most ? want[i] : most;
        zxapp_arg_t da[2] = {{"card", e, WAVE_ROWS}, {"name", f, WAVE_ROWS}};
        CHECK(most > 1 && wave_same(app, "contact_dupes", da, 2, want, WAVE_ROWS, false),
              "Kindred: contact_dupes, KRON = Trebago = C (most %lld)", (long long) most);
        static const int64_t cd[2] = {5, -3}, nm[2] = {1, 1};
        zxapp_arg_t xa[2] = {{"card", cd, 2}, {"name", nm, 2}};
        CHECK(wave_lp(app, "contact_dupes", xa, 2) == ZXAPP_BOTH,
              "Kindred: a negative card code: Trebago refuses, KRON counts: BOTH");
        static const int64_t cb[2] = {KINDRED_CARD_CODE_MAX + 1, KINDRED_CARD_CODE_MAX + 1},
                             nz[2] = {0, 0};
        zxapp_arg_t ya[2] = {{"card", cb, 2}, {"name", nz, 2}};
        CHECK(wave_lp(app, "contact_dupes", ya, 2) == ZXAPP_BOTH,
              "Kindred: a 16-digit card code: Trebago refuses, KRON counts: BOTH");
    }

    /* contact_record_ok (KR5): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = wrand(-1200000, 1200000);
        b[i] = wrand(-1200000000000000, 1200000000000000);
        c[i] = wrand(-1200000000000000, 1200000000000000);
        d[i] = wrand(-KINDRED_NAME_CODE_MAX, KINDRED_NAME_CODE_MAX);
        e[i] = wrand(-12, 12);
        f[i] = wrand(-12000000000000, 12000000000000);
    }
    a[0] = 1, b[0] = 0, c[0] = 0, d[0] = 10401000000000000, e[0] = 1, f[0] = 1792627200000;
    a[1] = 999999, b[1] = 999999999999999, c[1] = 999999999999999, d[1] = KINDRED_NAME_CODE_MAX,
    e[1] = 9, f[1] = 9999999999999;
    a[2] = 1000000, b[2] = 0, c[2] = 0, d[2] = 0, e[2] = 0, f[2] = 0;
    a[3] = 1, b[3] = 1000000000000000, c[3] = 0, d[3] = 0, e[3] = 0, f[3] = 0;
    a[4] = 1, b[4] = 0, c[4] = -1000000000000000, d[4] = 0, e[4] = 0, f[4] = 0;
    a[5] = 1, b[5] = 0, c[5] = 0, d[5] = 0, e[5] = 10, f[5] = 0;
    a[6] = 1, b[6] = 0, c[6] = 0, d[6] = 0, e[6] = 0, f[6] = 10000000000000;
    a[7] = -999999, b[7] = -1, c[7] = -2, d[7] = -3, e[7] = -9, f[7] = 0;
    for (uint32_t i = 0; i < WAVE_ROWS; i++)
        want[i] = T_(kindred_record_ok(a[i], b[i], c[i], d[i], e[i], f[i]));
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_TRUE && want[2] == ZXAPP_FALSE &&
              want[3] == ZXAPP_FALSE && want[4] == ZXAPP_FALSE && want[5] == ZXAPP_FALSE &&
              want[6] == ZXAPP_FALSE && want[7] == ZXAPP_TRUE,
          "Kindred: the C record's own edges (KR5)");
    zxapp_arg_t ra[6] = {{"contact", a, WAVE_ROWS},  {"card", b, WAVE_ROWS},
                         {"node", c, WAVE_ROWS},     {"name", d, WAVE_ROWS},
                         {"verified", e, WAVE_ROWS}, {"stamp", f, WAVE_ROWS}};
    CHECK(wave_same(app, "contact_record_ok", ra, 6, want, WAVE_ROWS, true),
          "Kindred: a contact makes a ^ZXKINDRED record, MYRA = C");

    /* name_index_ok (KR6): MYRA = C */
    for (uint32_t i = 0; i < WAVE_ROWS; i++) {
        a[i] = i % 2 ? wrand(-KINDRED_NAME_CODE_MAX, KINDRED_NAME_CODE_MAX) : wrand(0, INT64_MAX);
        b[i] = wrand(-1200000, 1200000);
        c[i] = wrand(-12000000000000, 12000000000000);
        want[i] = T_(kindred_index_ok(a[i], b[i], c[i]));
    }
    a[0] = 10401000000000000, b[0] = 1, c[0] = 1792627200000;
    a[1] = KINDRED_NAME_CODE_MAX + 1, b[1] = 1, c[1] = 1;
    for (uint32_t i = 0; i < 2u; i++) want[i] = T_(kindred_index_ok(a[i], b[i], c[i]));
    zxapp_arg_t ia[3] = {
        {"name", a, WAVE_ROWS}, {"contact", b, WAVE_ROWS}, {"stamp", c, WAVE_ROWS}};
    CHECK(want[0] == ZXAPP_TRUE && want[1] == ZXAPP_FALSE &&
              wave_same(app, "name_index_ok", ia, 3, want, WAVE_ROWS, true),
          "Kindred: a name index entry makes a ^ZXKINDREDNAME record, MYRA = C");
}
