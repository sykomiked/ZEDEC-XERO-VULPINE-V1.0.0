/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Tally's model against zxvd's real dispatcher (LINK). The books are a
 * scratch file in the test's own folder, never real books: currencies are
 * enabled and accounts opened through Coffer's model, read back by Tally's
 * (ledger.read only), and the scratch file removed at the end. */
#include "coffer_model.h"
#include "tally_model.h"

static tally_t TL;
static coffer_t TLC;

APPS_TEST(tally)
{
    char s[TALLY_TEXT_MAX];

    /* TM1, TM3: the running value, exact; 1/3 stays 1/3 */
    tally_init(&TL, &LINK);
    CHECK(tally_enter(&TL, "1") == ZXM_OK && tally_apply(&TL, TALLY_DIV, "3") == ZXM_OK &&
              TL.acc.num == 1 && TL.acc.den == 3,
          "1 / 3 is 1/3");
    tally_show(TL.acc, s, sizeof s);
    CHECK(!strcmp(s, "1/3"), "shown as 1/3: %s", s);
    CHECK(tally_apply(&TL, TALLY_MUL, "3") == ZXM_OK && TL.acc.num == 1 && TL.acc.den == 1,
          "1/3 * 3 is exactly 1");
    CHECK(tally_enter(&TL, "0.1") == ZXM_OK && tally_apply(&TL, TALLY_ADD, "0.2") == ZXM_OK &&
              TL.acc.num == 3 && TL.acc.den == 10,
          "0.1 + 0.2 is exactly 0.3");
    tally_show(TL.acc, s, sizeof s);
    CHECK(!strcmp(s, "0.3"), "shown as 0.3: %s", s);
    CHECK(tally_apply(&TL, TALLY_SUB, "3/4") == ZXM_OK && TL.acc.num == -9 && TL.acc.den == 20,
          "0.3 - 3/4 is -9/20");
    uint32_t before = TL.ntape;
    rat_t was = TL.acc;
    CHECK(tally_apply(&TL, TALLY_DIV, "0") == ZXM_E_ARG && tally_apply(&TL, 9, "1") == ZXM_E_ARG &&
              tally_apply(&TL, TALLY_ADD, "x") == ZXM_E_ARG &&
              tally_apply(&TL, TALLY_ADD, "1000000000000000000") == ZXM_E_ARG &&
              tally_enter(&TL, "") == ZXM_E_ARG && TL.ntape == before && rat_eq(TL.acc, was),
          "a refusal changes nothing");
    CHECK(tally_reduced_by(6, 9) == 3 && tally_reduced_by(-84, 36) == 12 &&
              tally_reduced_by(0, 7) == 7 && tally_reduced_by(1, 0) == -1,
          "reduced by the gcd");
    tally_show((rat_t){0, 0, false}, s, sizeof s);
    CHECK(!strcmp(s, "refused"), "an invalid value is shown as refused");

    /* TM2: the tape is bounded; a full tape refuses before anything changes */
    tally_clear(&TL);
    CHECK(TL.ntape == 0 && rat_is_zero(TL.acc), "cleared");
    for (uint32_t i = 0; i < TALLY_TAPE; i++) CHECK(tally_enter(&TL, "2") == ZXM_OK, "line %u", i);
    CHECK(tally_enter(&TL, "5") == ZXM_E_TOOBIG && TL.acc.num == 2 && TL.ntape == TALLY_TAPE,
          "a full tape refuses");
    tally_clear(&TL);
    for (uint32_t i = 0; i + 1 < TALLY_TAPE; i++) CHECK(tally_enter(&TL, "2") == ZXM_OK, "line");
    CHECK(tally_apply(&TL, TALLY_ADD, "1") == ZXM_E_TOOBIG && TL.acc.num == 2,
          "an operation needs two lines");
    tally_clear(&TL);

    /* TM6: units by name */
    rat_t u;
    CHECK(tally_unit(1, "mi", "km", &u) == ZXM_OK && u.num == 25146 && u.den == 15625,
          "a mile is 25146/15625 km");
    CHECK(tally_unit(5, "lb", "g", &u) == ZXM_OK && u.num == 45359237 && u.den == 20000,
          "5 lb in g");
    CHECK(tally_unit(1, "m", "g", &u) == ZXM_E_ARG &&
              tally_unit(1, "furlong", "m", &u) == ZXM_E_ARG,
          "two dimensions, or no such unit, refused");

    /* TM7: net present value through pce_fin, exact */
    const int64_t cf[3] = {-100, 0, 121}, cf2[2] = {-300, 100};
    CHECK(tally_npv(cf, 3, 1, 10, &u) == ZXM_OK && rat_is_zero(u), "npv at 1/10 is 0");
    CHECK(tally_npv(cf2, 2, 1, 3, &u) == ZXM_OK && u.num == -225 && u.den == 1,
          "npv of -300, 100 at 1/3 is -225");
    CHECK(tally_npv(cf, 0, 1, 10, &u) == ZXM_E_ARG && tally_npv(cf, 3, 1, 0, &u) == ZXM_E_ARG &&
              tally_npv(cf, 3, -10, 10, &u) == ZXM_E_ARG &&
              tally_npv(cf, TALLY_FLOWS + 1, 1, 10, &u) == ZXM_E_ARG,
          "no flows, no rate, a rate of -1, too many flows: refused");

    /* TM4: minor units shown */
    CHECK(tally_minor_exponent("USD") == 2 && tally_minor_exponent("JPY") == 0 &&
              tally_minor_exponent("BHD") == 3 && tally_minor_exponent("XQQ") == -1,
          "ISO 4217 minor units");
    tally_minor_text(-1205, 2, "USD", s, sizeof s);
    CHECK(!strcmp(s, "-12.05 USD"), "%s", s);
    tally_minor_text(1205, 0, "JPY", s, sizeof s);
    CHECK(!strcmp(s, "1205 JPY"), "%s", s);
    tally_minor_text(7, -1, "XQQ", s, sizeof s);
    CHECK(!strcmp(s, "7 minor units of XQQ"), "%s", s);

    /* TM4 and TM5 against the real dispatcher, on scratch books */
    char root[256], scratch[512];
    tally_fx_t fx;
    CHECK(tally_convert(&TL, "USD", "EUR", 1000, 1, 1, &fx) == ZXM_E_STATE, "nothing loaded yet");
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/tally-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    coffer_init(&TLC, &LINK, COFFER_DAY_CAP);
    uint32_t id = 0;
    CHECK(coffer_enable(&TLC, "USD") == ZXM_OK && coffer_enable(&TLC, "EUR") == ZXM_OK &&
              coffer_enable(&TLC, "JPY") == ZXM_OK &&
              coffer_open_account(&TLC, "cash", "USD", false, &id) == ZXM_OK &&
              coffer_open_account(&TLC, "travel", "EUR", false, &id) == ZXM_OK &&
              coffer_open_account(&TLC, "savings", "USD", false, &id) == ZXM_OK &&
              coffer_open_account(&TLC, "trip", "JPY", false, &id) == ZXM_OK,
          "accounts: %s", LINK.err);
    CHECK(tally_load(&TL) == ZXM_OK && TL.loaded && TL.nccy == 3 && tally_currency(&TL, "USD") &&
              tally_currency(&TL, "EUR") && tally_currency(&TL, "JPY") &&
              !tally_currency(&TL, "GBP") && tally_currency(&TL, "JPY")->exponent == 0,
          "the enabled currencies, once each: %u", TL.nccy);
    CHECK(tally_convert(&TL, "USD", "EUR", 1000, 10873, 10000, &fx) == ZXM_OK && fx.whole == 1087 &&
              fx.rest == 3000,
          "10.00 USD at 10873/10000");
    CHECK(!strcmp(fx.text, "10.00 USD at 10873/10000 is 10.87 EUR and 3000/10000 of a minor unit "
                           "(shown, not posted)"),
          "%s", fx.text);
    CHECK(tally_convert(&TL, "USD", "JPY", 100, 15000, 100, &fx) == ZXM_OK && fx.whole == 15000 &&
              fx.rest == 0 &&
              !strcmp(fx.text, "1.00 USD at 15000/100 is 15000 JPY (shown, not posted)"),
          "%s", fx.text);
    CHECK(tally_convert(&TL, "USD", "GBP", 1000, 1, 1, &fx) == ZXM_E_ARG &&
              tally_convert(&TL, "USD", "EUR", -1, 1, 1, &fx) == ZXM_E_ARG &&
              tally_convert(&TL, "USD", "EUR", 1, 0, 1, &fx) == ZXM_E_ARG &&
              tally_convert(&TL, "USD", "EUR", 1, 1, TALLY_RATE_MAX + 1, &fx) == ZXM_E_ARG,
          "a currency not enabled, or a rate outside, refused");

    /* TM4: answers that are no list are refused, the list kept */
    zxm_link_t *real = TL.link;
    TL.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"accounts\":[{\"account\":1,"
             "\"name\":\"cash\",\"currency\":\"usd\",\"kind\":\"holder\"}]}}";
    CHECK(tally_load(&TL) == ZXM_E_PROTOCOL && TL.nccy == 3, "a currency that is no code");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"accounts\":7}}";
    CHECK(tally_load(&TL) == ZXM_E_PROTOCOL && TL.nccy == 3, "no array");
    CHECK(strstr(FAKE.req, "\"ledger.accounts\"") && !strstr(FAKE.req, "ledger.post"),
          "only ledger.accounts is asked");
    TL.link = real;

    /* back to books in memory: the scratch file removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
}
