/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Furrow's model against zxvd's real dispatcher (LINK): a sheet is a file
 * in the Hoard, read with files.read and written with files.write. */
#include "furrow_model.h"

static furrow_t FW, FW2;

#define FURROW_TEST_PATH "Budget.furrow"

static bool fw_file_is(const char *want)
{
    static uint8_t buf[FURROW_TEXT_MAX];
    uint32_t got = 0;
    uint64_t size = 0;
    if (zxf_read(FURROW_TEST_PATH, 0, buf, sizeof buf - 1u, true, &got, &size) != 0) return false;
    buf[got] = 0;
    return size == got && !strcmp((const char *) buf, want);
}

APPS_TEST(furrow)
{
    int64_t v = 0;
    int32_t kind = 0;
    rat_t mean;
    char text[48];

    /* the rules the languages say */
    int64_t pos[4] = {1, 2, 3, 4}, lo[4] = {2, 2, 2, 2}, hi[4] = {3, 3, 3, 3},
            cell[4] = {5, 6, 7, 8}, over[2] = {INT64_MAX, 1};
    CHECK(furrow_range_total(pos, lo, hi, cell, 4, &v) && v == 13 &&
              !furrow_range_total(pos, pos, pos, over, 2, &v) &&
              !furrow_range_total(NULL, lo, hi, cell, 1, &v),
          "range total: inside lo..hi only, refused past 64 bits");
    CHECK(furrow_money_cell(12, 34, &v) && v == 1234 && furrow_money_cell(-2, -50, &v) &&
              v == -250 && !furrow_money_cell(1, 100, &v) &&
              !furrow_money_cell(FURROW_UNITS_MAX + 1, 0, &v),
          "money cell: units and hundredths, refused past their pictures");
    CHECK(furrow_formula_ok(0, 0) && furrow_formula_ok(16, 65536) && !furrow_formula_ok(17, 0) &&
              !furrow_formula_ok(0, 65537) && !furrow_formula_ok(-1, 0),
          "formula limits");
    CHECK(furrow_cell_record_ok(1, 1048576, 16384, 3, -999999999999999999) &&
              !furrow_cell_record_ok(10000, 1, 1, 1, 0),
          "cell record");
    CHECK(furrow_money_text(984, text, sizeof text) == 4 && !strcmp(text, "9.84") &&
              furrow_money_text(-250, text, sizeof text) == 5 && !strcmp(text, "-2.50") &&
              furrow_money_text(-1, text, sizeof text) == 5 && !strcmp(text, "-0.01") &&
              furrow_money_text(INT64_MIN, text, sizeof text) == 21 &&
              !strcmp(text, "-92233720368547758.08") && furrow_money_text(5, text, 3) == -1 &&
              !text[0],
          "money as text: %s", text);

    /* naming a sheet */
    CHECK(furrow_init(&FW, &LINK, "", 1) == ZXM_E_ARG &&
              furrow_init(&FW, &LINK, "../x.furrow", 1) == ZXM_E_ARG &&
              furrow_init(&FW, &LINK, FURROW_TEST_PATH, 0) == ZXM_E_ARG &&
              furrow_init(&FW, &LINK, FURROW_TEST_PATH, 10000) == ZXM_E_ARG &&
              furrow_load(&FW) == ZXM_E_ARG,
          "a bad name or sheet number is refused");

    /* nothing is written before the sheet is read */
    CHECK(zxf_use_space(NULL) == 0, "home");
    CHECK(furrow_init(&FW, &LINK, FURROW_TEST_PATH, 1) == ZXM_OK, "init");
    int before = sent;
    CHECK(furrow_set_number(&FW, 1, 1, 10) == ZXM_E_STATE &&
              furrow_clear(&FW, 1, 1) == ZXM_E_STATE && sent == before && !FW.loaded,
          "not loaded: nothing written");
    CHECK(furrow_set_number(&FW, 0, 1, 1) == ZXM_E_ARG &&
              furrow_set_number(&FW, 1, FURROW_COL_MAX + 1, 1) == ZXM_E_ARG &&
              furrow_set_number(&FW, 1, 1, INT64_C(1000000000000000000)) == ZXM_E_ARG &&
              furrow_set_money(&FW, 1, 1, 1, 100) == ZXM_E_ARG &&
              furrow_set_formula(&FW, 1, 1, 3, 1, 1, 1) == ZXM_E_ARG &&
              furrow_set_formula(&FW, 1, 1, FURROW_DOWN, 1, 5, 4) == ZXM_E_ARG &&
              furrow_set_formula(&FW, 1, 1, FURROW_DOWN, 1, 1, 65537) == ZXM_E_ARG &&
              furrow_set_formula(&FW, 1, 1, FURROW_ACROSS, 1, 1, FURROW_COL_MAX + 1) == ZXM_E_ARG &&
              sent == before,
          "bad cells are refused before anything is asked");

    /* a sheet not made yet is empty */
    CHECK(furrow_load(&FW) == ZXM_OK && FW.loaded && FW.n == 0, "no sheet yet: %s", LINK.err);
    CHECK(furrow_value(&FW, 1, 1, &v, &kind) == ZXM_OK && v == 0 && kind == FURROW_NUMBER,
          "an empty cell is the number 0");

    /* numbers down column 1, money down column 2, and their totals */
    CHECK(furrow_set_number(&FW, 3, 1, 30) == ZXM_OK &&
              furrow_set_number(&FW, 1, 1, 10) == ZXM_OK &&
              furrow_set_number(&FW, 2, 1, 20) == ZXM_OK &&
              furrow_set_money(&FW, 1, 2, 12, 34) == ZXM_OK &&
              furrow_set_money(&FW, 2, 2, -2, -50) == ZXM_OK &&
              furrow_set_formula(&FW, 4, 1, FURROW_DOWN, 1, 1, 3) == ZXM_OK &&
              furrow_set_formula(&FW, 3, 2, FURROW_DOWN, 2, 1, 2) == ZXM_OK && FW.n == 7,
          "cells: %s", LINK.err);
    CHECK(fw_file_is("furrow 1\n1 1 1 10\n1 2 2 1234\n2 1 1 20\n2 2 2 -250\n3 1 1 30\n"
                     "3 2 3 1 2 1 2\n4 1 3 1 1 1 3\n"),
          "the sheet in the Hoard, sparse and in order (FM1, FM2)");
    CHECK(furrow_value(&FW, 4, 1, &v, &kind) == ZXM_OK && v == 60 && kind == FURROW_NUMBER,
          "a column's sum: %lld", (long long) v);
    CHECK(furrow_value(&FW, 3, 2, &v, &kind) == ZXM_OK && v == 984 && kind == FURROW_MONEY,
          "a money column's sum: %lld", (long long) v);
    CHECK(furrow_sum(&FW, FURROW_DOWN, 1, 1, 4, &v, &kind) == ZXM_OK && v == 120 &&
              furrow_sum(&FW, FURROW_DOWN, 1, 2, 3, &v, &kind) == ZXM_OK && v == 50 &&
              furrow_sum(&FW, FURROW_DOWN, 1, 5, 9, &v, &kind) == ZXM_OK && v == 0 &&
              kind == FURROW_NUMBER,
          "ranges of a column, a formula inside one evaluated");
    CHECK(furrow_sum(&FW, FURROW_ACROSS, 3, 1, 1, &v, &kind) == ZXM_OK && v == 30 &&
              furrow_sum(&FW, FURROW_ACROSS, 1, 1, 2, &v, &kind) == FURROW_E_MIXED &&
              furrow_sum(&FW, FURROW_ACROSS, 2, 2, 1, &v, &kind) == ZXM_E_ARG,
          "a row's sum; numbers and money are never added together");
    CHECK(furrow_money_total(&FW, &v) == ZXM_OK && v == 984, "the sheet's money (coffer.holdings)");

    /* an exact mean: 10, 21, 30 is 61/3, never a float */
    CHECK(furrow_set_number(&FW, 2, 1, 21) == ZXM_OK && FW.n == 7 &&
              furrow_mean(&FW, FURROW_DOWN, 1, 1, 3, &mean) == ZXM_OK && mean.num == 61 &&
              mean.den == 3 && rat_to_string(mean, text, sizeof text) == 4 && !strcmp(text, "61/3"),
          "mean 61/3: %s", text);
    CHECK(furrow_mean(&FW, FURROW_DOWN, 2, 1, 2, &mean) == ZXM_OK && mean.num == 492 &&
              mean.den == 1 && furrow_mean(&FW, FURROW_DOWN, 9, 1, 9, &mean) == ZXM_E_STATE,
          "a money mean; an empty range has none");

    /* a cycle is refused, never followed for ever (FM5) */
    CHECK(furrow_set_formula(&FW, 5, 3, FURROW_DOWN, 3, 5, 5) == ZXM_OK &&
              furrow_value(&FW, 5, 3, &v, &kind) == FURROW_E_REACH &&
              furrow_sum(&FW, FURROW_DOWN, 3, 1, 9, &v, &kind) == FURROW_E_REACH,
          "a formula over itself");
    CHECK(furrow_clear(&FW, 5, 3) == ZXM_OK && furrow_clear(&FW, 5, 3) == ZXM_OK && FW.n == 7,
          "cleared, and clearing an empty cell");

    /* 16 formulas deep is inside, 17 is past formula_ok */
    bool ok = furrow_set_number(&FW, 18, 4, 7) == ZXM_OK;
    for (int64_t r = 17; ok && r >= 1; r--)
        ok = furrow_set_formula(&FW, r, 4, FURROW_DOWN, 4, r + 1, r + 1) == ZXM_OK;
    CHECK(ok && furrow_value(&FW, 2, 4, &v, &kind) == ZXM_OK && v == 7 &&
              furrow_value(&FW, 1, 4, &v, &kind) == FURROW_E_REACH,
          "depth: %s", LINK.err);

    /* a fan of formulas over formulas passes the references: 10 x 100 x 100 */
    ok = true;
    for (int64_t r = 1; ok && r <= 100; r++)
        ok = furrow_set_number(&FW, r, 7, r) == ZXM_OK &&
             furrow_set_formula(&FW, r, 6, FURROW_DOWN, 7, 1, 100) == ZXM_OK;
    for (int64_t r = 1; ok && r <= 10; r++)
        ok = furrow_set_formula(&FW, r, 5, FURROW_DOWN, 6, 1, 100) == ZXM_OK;
    CHECK(ok && furrow_value(&FW, 1, 6, &v, &kind) == ZXM_OK && v == 5050 &&
              furrow_value(&FW, 1, 5, &v, &kind) == ZXM_OK && v == 505000 &&
              furrow_sum(&FW, FURROW_DOWN, 5, 1, 6, &v, &kind) == ZXM_OK && v == 3030000 &&
              furrow_sum(&FW, FURROW_DOWN, 5, 1, 10, &v, &kind) == FURROW_E_REACH,
          "references: %s", LINK.err);

    /* a second model reads it all back */
    CHECK(furrow_init(&FW2, &LINK, FURROW_TEST_PATH, 1) == ZXM_OK && furrow_load(&FW2) == ZXM_OK &&
              FW2.n == FW.n && !memcmp(FW2.c, FW.c, FW.n * sizeof FW.c[0]),
          "read back: %s", LINK.err);

    /* a full sheet goes in parts (begin, add, end) and refuses more */
    ok = true;
    for (int64_t r = 1; ok && FW.n < FURROW_CELLS; r++)
        ok = furrow_set_money(&FW, r, 9, -FURROW_UNITS_MAX, -99) == ZXM_OK;
    CHECK(ok && FW.n == FURROW_CELLS, "full: %u %s", FW.n, LINK.err);
    before = sent;
    CHECK(furrow_set_number(&FW, 1, 10, 1) == ZXM_E_TOOBIG && sent == before, "no room");
    CHECK(furrow_set_number(&FW, 1, 9, 5) == ZXM_OK && FW.n == FURROW_CELLS,
          "a full sheet still replaces a cell: %s", LINK.err);
    CHECK(furrow_init(&FW2, &LINK, FURROW_TEST_PATH, 1) == ZXM_OK && furrow_load(&FW2) == ZXM_OK &&
              FW2.n == FURROW_CELLS && !memcmp(FW2.c, FW.c, FW.n * sizeof FW.c[0]),
          "a full sheet read back: %s", LINK.err);
    CHECK(furrow_money_total(&FW, &v) == ZXM_E_TOOBIG,
          "money past 64 bits is refused, never wrapped");

    /* a file that is not a sheet is never overwritten (FM3) */
    uint64_t size;
    CHECK(zxf_write(FURROW_TEST_PATH, (const uint8_t *) "furrow 1\n1 1 1 07\n", 18, ZXF_W_REPLACE,
                    false, &size) == 0,
          "a leading zero");
    CHECK(furrow_load(&FW2) == ZXM_E_PROTOCOL && !FW2.loaded &&
              furrow_set_number(&FW2, 1, 1, 1) == ZXM_E_STATE && fw_file_is("furrow 1\n1 1 1 07\n"),
          "not a sheet: refused, left as it was");
    static const char *const bad[] = {"sheet\n",
                                      "furrow 1\n1 1 1 5",
                                      "furrow 1\n1 1 1 -0\n",
                                      "furrow 1\n1 1 4 5\n",
                                      "furrow 1\n0 1 1 5\n",
                                      "furrow 1\n1 16385 1 5\n",
                                      "furrow 1\n2 1 1 5\n1 1 1 5\n",
                                      "furrow 1\n1 1 1 5\n1 1 1 6\n",
                                      "furrow 1\n1 1 1 1000000000000000000\n",
                                      "furrow 1\n1 1 1 9999999999999999999\n",
                                      "furrow 1\n1 1 3 3 1 1 2\n",
                                      "furrow 1\n1 1 3 1 1 2 1\n",
                                      "furrow 1\n1 1 3 1 1 1 70000\n",
                                      "furrow 1\n1 1 3 1 1 1\n"};
    for (uint32_t i = 0; i < sizeof bad / sizeof bad[0]; i++)
        CHECK(zxf_write(FURROW_TEST_PATH, (const uint8_t *) bad[i], (uint32_t) strlen(bad[i]),
                        ZXF_W_REPLACE, false, &size) == 0 &&
                  furrow_load(&FW2) == ZXM_E_PROTOCOL,
              "not a sheet: %u", i);
    CHECK(zxf_write(FURROW_TEST_PATH, (const uint8_t *) "furrow 1\n7 2 2 -999999999999999999\n", 35,
                    ZXF_W_REPLACE, false, &size) == 0 &&
              furrow_load(&FW2) == ZXM_OK && FW2.n == 1 && FW2.c[0].kind == FURROW_MONEY &&
              FW2.c[0].value == -999999999999999999,
          "the widest money read");
}
