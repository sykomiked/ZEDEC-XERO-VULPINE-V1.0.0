/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Coffer's model against zxvd's real dispatcher (LINK). Every posting goes
 * to a scratch books file in the test's own folder, never to real books:
 * the books are closed, pointed at the scratch file, opened, and closed
 * again (in memory) at the end. */
#include "coffer_model.h"

static coffer_t CF, CF2;

/* the account named name, or NULL */
static const coffer_account_t *cf_named(const coffer_t *c, const char *name)
{
    for (uint32_t i = 0; i < c->n; i++)
        if (!strcmp(c->a[i].name, name)) return &c->a[i];
    return 0;
}

static int64_t cf_bal(const coffer_t *c, const char *name)
{
    const coffer_account_t *a = cf_named(c, name);
    return a ? a->balance : COFFER_REFUSED;
}

APPS_TEST(coffer)
{
    /* the rules */
    CHECK(coffer_balance(0, 0) == 0 && coffer_balance(100, -100) == 0 &&
              coffer_balance(-50, 25) == -25 &&
              coffer_balance(COFFER_MINOR_MAX - 1, 1) == COFFER_MINOR_MAX &&
              coffer_balance(COFFER_MINOR_MAX, 1) == COFFER_REFUSED &&
              coffer_balance(-COFFER_MINOR_MAX, -1) == COFFER_REFUSED &&
              coffer_balance(COFFER_MINOR_MAX + 1, 0) == COFFER_REFUSED &&
              coffer_balance(0, INT64_MIN) == COFFER_REFUSED,
          "balance: 18 digits either way, refused past them");
    CHECK(coffer_transfer_ok(1, 0, 0, 0) && coffer_transfer_ok(COFFER_AMOUNT_MAX, 0, 0, 0) &&
              !coffer_transfer_ok(0, 0, 0, 0) && !coffer_transfer_ok(-1, 0, 0, 0) &&
              !coffer_transfer_ok(COFFER_AMOUNT_MAX + 1, 0, 0, 0) &&
              !coffer_transfer_ok(5, -1, 0, 0) && !coffer_transfer_ok(5, 0, -1, 0) &&
              !coffer_transfer_ok(5, 0, 0, 1) && !coffer_transfer_ok(5, 0, 0, -1) &&
              !coffer_transfer_ok(5, COFFER_MINOR_MAX + 1, 0, 0),
          "transfer allowed: its edges");
    int64_t h1[4] = {100, 250, -50, 0}, h2[2] = {INT64_MAX, 1}, h3[3] = {INT64_MAX, 1, -1},
            h4[2] = {INT64_MAX - 5, 5};
    CHECK(coffer_holdings(h1, 4) == 300 && coffer_holdings(h1, 0) == 0 &&
              coffer_holdings(h4, 2) == INT64_MAX && coffer_holdings(h2, 2) == COFFER_REFUSED &&
              coffer_holdings(h3, 3) == COFFER_REFUSED &&
              coffer_holdings(NULL, 1) == COFFER_REFUSED,
          "holdings: exact, refused when a partial sum passes 64 bits");

    /* a scratch books file, never the owner's */
    char root[256], scratch[512], path[COFFER_PATH_MAX + 1];
    bool saved = false;
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/coffer-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    coffer_init(&CF, &LINK, -1);
    CHECK(CF.day_cap == COFFER_DAY_CAP && CF.e_account == -1 && !CF.loaded, "init: default cap");
    CHECK(coffer_books(&CF, path, sizeof path, &saved) == ZXM_OK && saved && !strcmp(path, scratch),
          "every post goes to the scratch file: %s", path);

    /* nothing is sent before it is checked */
    int before = sent;
    uint32_t id = 0;
    CHECK(coffer_enable(&CF, "usd") == ZXM_E_ARG && coffer_enable(&CF, "USDX") == ZXM_E_ARG &&
              coffer_open_account(&CF, "", "USD", false, &id) == ZXM_E_ARG &&
              coffer_open_account(&CF, "cash", "US", false, &id) == ZXM_E_ARG &&
              coffer_propose(&CF, 0, 1, 5, "x") == ZXM_E_STATE &&
              coffer_entries(&CF, 0) == ZXM_E_STATE && coffer_confirm(&CF, NULL) == ZXM_E_ARG &&
              coffer_set_day(&CF, -1) == ZXM_E_ARG &&
              coffer_set_cap(&CF, COFFER_AMOUNT_MAX + 1) == ZXM_E_ARG && sent == before,
          "bad input refused before anything is asked");

    /* currencies and accounts */
    uint32_t employer = 0, cash = 0, savings = 0, euros = 0;
    CHECK(coffer_enable(&CF, "USD") == ZXM_OK && coffer_enable(&CF, "EUR") == ZXM_OK,
          "currency.enable: %s", LINK.err);
    CHECK(coffer_open_account(&CF, "employer", "USD", true, &employer) == ZXM_OK &&
              coffer_open_account(&CF, "cash", "USD", false, &cash) == ZXM_OK &&
              coffer_open_account(&CF, "savings", "USD", false, &savings) == ZXM_OK &&
              coffer_open_account(&CF, "travel", "EUR", false, &euros) == ZXM_OK,
          "ledger.open_account: %s", LINK.err);
    CHECK(CF.loaded && cf_named(&CF, "employer")->issuer && !cf_named(&CF, "cash")->issuer &&
              !strcmp(cf_named(&CF, "travel")->ccy, "EUR") && cf_bal(&CF, "cash") == 0 &&
              coffer_account(&CF, cash) == cf_named(&CF, "cash"),
          "ledger.accounts and ledger.balance");

    /* CF2: a transfer waits for the owner; nothing is posted until confirmed */
    uint64_t entry = 0;
    CHECK(coffer_set_day(&CF, 20000) == ZXM_OK, "day");
    before = sent;
    CHECK(coffer_propose(&CF, employer, cash, 250000, "salary 1") == ZXM_OK && CF.held.pending &&
              sent == before && strstr(CF.notice, "Waiting for your confirmation") &&
              strstr(CF.notice, "250000 minor units of USD from \"employer\" to \"cash\""),
          "proposed, nothing sent: %s", CF.notice);
    CHECK(coffer_propose(&CF, employer, cash, 1, "salary 2") == ZXM_E_STATE && sent == before,
          "one transfer waits at a time");
    CHECK(coffer_load(&CF) == ZXM_OK && cf_bal(&CF, "cash") == 0 && CF.held.pending,
          "the books do not have it yet");
    CHECK(coffer_confirm(&CF, &entry) == ZXM_OK && !CF.held.pending &&
              cf_bal(&CF, "cash") == 250000 && cf_bal(&CF, "employer") == -250000 &&
              CF.spent == 0 && strstr(CF.notice, "Posted: 250000"),
          "confirmed and posted, money coming in is not spending: %s", LINK.err);
    CHECK(coffer_confirm(&CF, &entry) == ZXM_E_STATE && coffer_cancel(&CF) == ZXM_E_STATE,
          "nothing waits now");

    /* CF3: not below zero, under the per-day cap, balanced */
    CHECK(coffer_propose(&CF, cash, savings, 60000, "save 1") == ZXM_OK &&
              coffer_confirm(&CF, &entry) == ZXM_OK && CF.spent == 60000 &&
              cf_bal(&CF, "cash") == 190000 && cf_bal(&CF, "savings") == 60000,
          "a transfer: %s", LINK.err);
    before = sent;
    CHECK(coffer_propose(&CF, savings, cash, 60001, "back 1") == ZXM_E_ARG && !CF.held.pending &&
              strstr(CF.notice, "below zero") && sent == before,
          "never below zero: %s", CF.notice);
    CHECK(coffer_propose(&CF, cash, savings, 40001, "save 2") == ZXM_E_ARG &&
              strstr(CF.notice, "per-day cap") && sent == before,
          "past the day's cap by one: %s", CF.notice);
    CHECK(coffer_propose(&CF, cash, savings, 40000, "save 2") == ZXM_OK &&
              coffer_confirm(&CF, &entry) == ZXM_OK && CF.spent == COFFER_DAY_CAP,
          "the cap met exactly: %s", LINK.err);
    before = sent;
    CHECK(coffer_propose(&CF, cash, savings, 1, "save 3") == ZXM_E_ARG &&
              coffer_set_day(&CF, 20000) == ZXM_OK && CF.spent == COFFER_DAY_CAP,
          "the same day: still met");
    CHECK(coffer_set_day(&CF, 20001) == ZXM_OK && CF.spent == 0 &&
              coffer_propose(&CF, cash, savings, 1, "save 3") == ZXM_OK &&
              coffer_cancel(&CF) == ZXM_OK && !CF.held.pending && sent == before &&
              cf_bal(&CF, "savings") == 100000,
          "a new day; a cancelled transfer posts nothing");
    CHECK(coffer_propose(&CF, cash, euros, 5, "fx 1") == ZXM_E_ARG &&
              strstr(CF.notice, "same currency") &&
              coffer_propose(&CF, cash, cash, 5, "self") == ZXM_E_ARG &&
              coffer_propose(&CF, cash, 999, 5, "nobody") == ZXM_E_ARG &&
              coffer_propose(&CF, cash, savings, 0, "zero") == ZXM_E_ARG &&
              coffer_propose(&CF, cash, savings, 5, "") == ZXM_E_ARG &&
              coffer_propose(&CF, cash, savings, 5, "a reference that is longer than 35 b") ==
                  ZXM_E_ARG &&
              sent == before,
          "refused before anything is asked");
    CHECK(coffer_set_cap(&CF, 0) == ZXM_OK &&
              coffer_propose(&CF, cash, savings, 1, "save 4") == ZXM_E_ARG &&
              coffer_propose(&CF, employer, cash, 1, "salary 2") == ZXM_OK &&
              coffer_confirm(&CF, &entry) == ZXM_OK &&
              coffer_set_cap(&CF, COFFER_DAY_CAP) == ZXM_OK,
          "a cap of 0 stops spending, not money coming in: %s", LINK.err);

    /* the books refuse a reference already used: nothing is posted */
    CHECK(coffer_propose(&CF, cash, savings, 10, "save 1") == ZXM_OK &&
              coffer_confirm(&CF, &entry) == ZXM_E_REMOTE && !CF.held.pending &&
              strstr(CF.notice, "refused") && CF.spent == 0 && cf_bal(&CF, "cash") == 150001,
          "a duplicate reference: %d %s", LINK.err_code, LINK.err);

    /* entries, holdings and the books check */
    CHECK(coffer_entries(&CF, cash) == ZXM_OK && CF.ne == 4 && CF.e_account == cash &&
              CF.e[0].minor == 250000 && CF.e[0].counterparty == employer &&
              !strcmp(CF.e[0].ref, "salary 1") && CF.e[1].minor == -60000 &&
              CF.e[2].minor == -40000 && CF.e[3].minor == 1 && CF.e[1].id < CF.e[2].id,
          "ledger.entries, oldest first: %u %s", CF.ne, LINK.err);
    CHECK(coffer_entries(&CF, 999) == ZXM_E_ARG, "no such account");
    int64_t total = 0;
    CHECK(coffer_holdings_in(&CF, "USD", &total) == ZXM_OK && total == 250001 &&
              coffer_holdings_in(&CF, "EUR", &total) == ZXM_OK && total == 0 &&
              coffer_holdings_in(&CF, "USD", NULL) == ZXM_E_ARG,
          "holdings in a currency, issuers left out: %lld", (long long) total);
    CHECK(coffer_check(&CF) == ZXM_OK && CF.books_ok && strstr(CF.notice, "The books hold"),
          "ledger.check: %s", LINK.err);

    /* the scratch file is replayed: a second model reads the same books */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_open() == 0, "scratch books replayed");
    coffer_init(&CF2, &LINK, COFFER_DAY_CAP);
    CHECK(coffer_load(&CF2) == ZXM_OK && CF2.n == CF.n &&
              !memcmp(CF2.a, CF.a, CF.n * sizeof CF.a[0]),
          "read back from the scratch file: %s", LINK.err);

    /* CF5: an answer that does not add up is refused and the copy kept */
    zxm_link_t *real = CF.link;
    CF.link = &FAKE;
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"entry_id\":\"1\","
             "\"signed_minor\":\"5\",\"counterparty\":0,\"reference\":\"x\"}]}}";
    CHECK(coffer_entries(&CF, cash) == ZXM_E_PROTOCOL && CF.ne == 4, "entries not the balance");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"entry_id\":\"01\","
             "\"signed_minor\":\"150001\",\"counterparty\":0,\"reference\":\"x\"}]}}";
    CHECK(coffer_entries(&CF, cash) == ZXM_E_PROTOCOL && CF.ne == 4, "a leading zero");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"entry_id\":\"1\","
             "\"signed_minor\":\"9223372036854775808\",\"counterparty\":0,\"reference\":\"x\"}]}}";
    CHECK(coffer_entries(&CF, cash) == ZXM_E_PROTOCOL && CF.e_account == cash,
          "past 64 bits: refused");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"accounts\":[{\"account\":0,"
             "\"name\":\"a\",\"currency\":\"USD\",\"kind\":\"other\"}]}}";
    CHECK(coffer_load(&CF) == ZXM_E_PROTOCOL && CF.n == CF2.n, "an unknown kind");
    CANNED = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"enabled\":\"EUR\"}}";
    CHECK(coffer_enable(&CF, "USD") == ZXM_E_PROTOCOL, "another currency enabled");
    CF.link = real;

    /* back to books in memory: the scratch file is removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
    CHECK(coffer_load(&CF2) == ZXM_E_REMOTE && CF2.n == CF.n, "books closed: %s", LINK.err);
}
