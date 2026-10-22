/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Larder's model against zxvd's real dispatcher (LINK). The books are a
 * scratch file in the test's own folder, never the owner's: Coffer's model
 * opens the accounts and posts the month's spending (Larder only reads),
 * and the books go back to memory at the end. A scripted link (LD_LINK)
 * answers with a journal that does not add up to its balance. */
#include "larder_model.h"

static larder_t LD, LD_KEEP;
static coffer_t LD_CF;
static zxm_link_t LD_LINK;
static const char *LD_SCRIPT[4];
static uint32_t LD_STEP;

static int32_t ld_send(void *ctx, const char *req, uint32_t len, char *resp, uint32_t cap)
{
    zxm_link_t *l = ctx;
    (void) req;
    (void) len;
    if (LD_STEP >= 4u || !LD_SCRIPT[LD_STEP]) return -1;
    return snprintf(resp, cap, LD_SCRIPT[LD_STEP++], l->seq);
}

static uint32_t ld_open(const char *name, const char *ccy, bool issuer)
{
    uint32_t id = UINT32_MAX;
    if (coffer_open_account(&LD_CF, name, ccy, issuer, &id) != ZXM_OK) return UINT32_MAX;
    return id;
}

/* Coffer posts a transfer the owner confirmed. */
static bool ld_post(uint32_t from, uint32_t to, int64_t amount, const char *ref)
{
    uint64_t entry = 0;
    return coffer_propose(&LD_CF, from, to, amount, ref) == ZXM_OK &&
           coffer_confirm(&LD_CF, &entry) == ZXM_OK;
}

APPS_TEST(larder)
{
    /* the rules, as the blocks run them */
    int64_t b4[4] = {1000, 500, 0, 300}, s4[4] = {400, 700, 50, -100}, z2[2] = {0, 0},
            big[2] = {INT64_MAX, 1}, neg[2] = {5, -1};
    CHECK(larder_category_spent(100, 350) == 250 && larder_category_spent(500, 200) == -300 &&
              larder_category_spent(0, LARDER_MINOR_MAX) == LARDER_MINOR_MAX &&
              larder_category_spent(-1, 5) == LARDER_REFUSED &&
              larder_category_spent(0, LARDER_MINOR_MAX + 1) == LARDER_REFUSED,
          "larder: LR1, closing - opening of a holder, 18 digits");
    CHECK(larder_category_over_budget(100, 1) && !larder_category_over_budget(100, 0) &&
              !larder_category_over_budget(0, 500) && !larder_category_over_budget(-5, 10) &&
              !larder_category_over_budget(100, LARDER_MINOR_MAX + 1),
          "larder: LR2, a budget set and an excess of 1 .. 18 digits");
    CHECK(larder_unspent_carry(b4, s4, 4) == 1000 && larder_unspent_carry(b4, s4, 0) == 0 &&
              larder_unspent_carry(big, z2, 2) == LARDER_REFUSED &&
              larder_unspent_carry(neg, z2, 2) == LARDER_REFUSED &&
              larder_unspent_carry(NULL, s4, 1) == LARDER_REFUSED,
          "larder: LR3, the unspent parts summed exactly");

    /* a scratch books file, never the owner's */
    char root[256], scratch[512];
    CHECK(zxf_root(root, sizeof root) == 0, "larder: the test's folder");
    snprintf(scratch, sizeof scratch, "%s/larder-scratch.books", root);
    (void) remove(scratch);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0,
          "larder: scratch books open");
    coffer_init(&LD_CF, &LINK, COFFER_AMOUNT_MAX);
    CHECK(coffer_enable(&LD_CF, "USD") == ZXM_OK && coffer_enable(&LD_CF, "EUR") == ZXM_OK,
          "larder: currencies: %s", LINK.err);
    uint32_t employer = ld_open("ld employer", "USD", true),
             cash = ld_open("ld cash", "USD", false), food = ld_open("groceries", "USD", false),
             fuel = ld_open("fuel", "USD", false), trip = ld_open("ld travel", "EUR", false);
    CHECK(employer != UINT32_MAX && cash != UINT32_MAX && food != UINT32_MAX &&
              fuel != UINT32_MAX && trip != UINT32_MAX,
          "larder: accounts: %s", LINK.err);
    CHECK(ld_post(employer, cash, 500000, "ld salary") && ld_post(cash, food, 1000, "ld before"),
          "larder: last month's books: %s", LINK.err);

    /* LA3: nothing is asked of zxvd and nothing changes without the owner's word */
    larder_init(&LD, &LINK);
    int before = sent;
    CHECK(larder_set_budget(&LD, food, 40000, false) == ZXM_E_STATE && LD.n == 0 &&
              strstr(LD.notice, "waits for your confirmation") &&
              larder_set_budget(&LD, food, -1, true) == ZXM_E_ARG &&
              larder_set_budget(&LD, food, LARDER_MINOR_MAX + 1, true) == ZXM_E_ARG &&
              larder_start_month(&LD, true) == ZXM_E_STATE && larder_refresh(&LD) == ZXM_E_STATE &&
              larder_close_month(&LD, true) == ZXM_E_STATE &&
              larder_drop(&LD, food, true) == ZXM_E_ARG && sent == before && LD.n == 0,
          "larder: refused before anything is asked");
    CHECK(larder_set_budget(&LD, employer, 100, true) == ZXM_E_ARG &&
              strstr(LD.notice, "outside party") &&
              larder_set_budget(&LD, 999, 100, true) == ZXM_E_ARG && LD.n == 0,
          "larder: a category is one of the owner's own accounts: %s", LD.notice);
    CHECK(larder_set_budget(&LD, food, 40000, true) == ZXM_OK &&
              larder_set_budget(&LD, fuel, 12000, true) == ZXM_OK && LD.n == 2 &&
              !strcmp(LD.ccy, "USD") && !strcmp(larder_category(&LD, food)->name, "groceries") &&
              LD.total_budget == 52000 && !LD.month_open,
          "larder: two categories: %s", LINK.err);
    CHECK(larder_set_budget(&LD, trip, 5000, true) == ZXM_E_ARG &&
              strstr(LD.notice, "one currency") && LD.n == 2,
          "larder: one currency a budget: %s", LD.notice);

    /* LA2: the month starts on the owner's word, from the balances then */
    before = sent;
    CHECK(larder_start_month(&LD, false) == ZXM_E_STATE && !LD.month_open && sent == before,
          "larder: a month waits for the owner");
    CHECK(larder_start_month(&LD, true) == ZXM_OK && LD.month_open &&
              larder_category(&LD, food)->opening == 1000 &&
              larder_category(&LD, food)->mark >= 0 && larder_category(&LD, fuel)->mark == -1 &&
              LD.total_spent == 0 && LD.carry == 52000,
          "larder: the month starts: %s", LINK.err);
    CHECK(ld_post(cash, food, 41500, "ld food 1") && ld_post(cash, fuel, 2000, "ld fuel 1"),
          "larder: this month's spending: %s", LINK.err);
    CHECK(larder_refresh(&LD) == ZXM_OK && larder_category(&LD, food)->spent == 41500 &&
              larder_category(&LD, food)->closing == 42500 && larder_category(&LD, food)->over &&
              larder_category(&LD, fuel)->spent == 2000 && !larder_category(&LD, fuel)->over &&
              LD.over_n == 1 && LD.total_spent == 43500 && LD.total_budget == 52000 &&
              LD.carry == 10000 &&
              strstr(LD.notice, "Over budget: groceries by 1500 (minor units of USD)") &&
              strstr(LD.notice, "Advice only"),
          "larder: groceries over by 1500: %s", LD.notice);
    CHECK(ld_post(food, cash, 1500, "ld refund 1") && larder_refresh(&LD) == ZXM_OK &&
              larder_category(&LD, food)->spent == 40000 && !larder_category(&LD, food)->over &&
              LD.over_n == 0 &&
              strstr(LD.notice, "within its budget: spent 42000 of 52000 minor units of USD"),
          "larder: a refund brings it back to the budget: %s", LD.notice);

    /* LA4: closing the month carries what was left unspent */
    before = sent;
    LD_KEEP = LD;
    CHECK(larder_close_month(&LD, false) == ZXM_E_STATE && sent == before &&
              !memcmp(LD.c, LD_KEEP.c, sizeof LD.c) && LD.carry == LD_KEEP.carry,
          "larder: closing waits for the owner; nothing sent, nothing changed");
    CHECK(larder_close_month(&LD, true) == ZXM_OK && LD.month_open &&
              larder_category(&LD, food)->carried == 0 &&
              larder_category(&LD, fuel)->carried == 10000 &&
              larder_category(&LD, fuel)->budget == 22000 &&
              larder_category(&LD, food)->opening == 41000 &&
              larder_category(&LD, fuel)->opening == 2000 && LD.total_budget == 62000 &&
              LD.total_spent == 0 && strstr(LD.notice, "Month closed: 10000 minor units of USD"),
          "larder: 10000 carried into fuel: %s", LD.notice);
    CHECK(ld_post(cash, fuel, 22001, "ld fuel 2") && larder_refresh(&LD) == ZXM_OK &&
              larder_category(&LD, fuel)->spent == 22001 && larder_category(&LD, fuel)->over &&
              strstr(LD.notice, "fuel by 1 "),
          "larder: the carried budget passed by one: %s", LD.notice);
    CHECK(larder_set_budget(&LD, fuel, 0, true) == ZXM_OK &&
              larder_category(&LD, fuel)->budget == 10000 && larder_category(&LD, fuel)->over &&
              !LD.fresh,
          "larder: a new base keeps what was carried");
    CHECK(larder_set_budget(&LD, fuel, LARDER_MINOR_MAX, true) == ZXM_E_ARG &&
              larder_category(&LD, fuel)->base == 0,
          "larder: base + carried past 18 digits refused");

    /* LA2, LA5: a journal that does not add up to its balance is refused */
    LD_KEEP = LD;
    LD.link = &LD_LINK;
    zxm_link_init(&LD_LINK, ld_send, &LD_LINK);
    LD_SCRIPT[0] = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"debit_minor\":\"5000\","
                   "\"credit_minor\":\"0\",\"equity_minor\":\"5000\"}}";
    LD_SCRIPT[1] = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"entries\":[{\"entry_id\":\"1\","
                   "\"signed_minor\":\"4000\",\"counterparty\":0,\"reference\":\"x\"}]}}";
    LD_STEP = 0;
    CHECK(larder_refresh(&LD) == ZXM_E_PROTOCOL && LD_STEP == 2 &&
              !memcmp(LD.c, LD_KEEP.c, sizeof LD.c),
          "larder: entries that are not the balance: refused, kept");
    LD_SCRIPT[0] = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"debit_minor\":\"5000\","
                   "\"credit_minor\":\"0\",\"equity_minor\":\"4999\"}}";
    LD_STEP = 0;
    CHECK(larder_refresh(&LD) == ZXM_E_PROTOCOL && !memcmp(LD.c, LD_KEEP.c, sizeof LD.c),
          "larder: an equity that is not debit - credit: refused");
    LD_SCRIPT[0] = "{\"jsonrpc\":\"2.0\",\"id\":%u,\"result\":{\"debit_minor\":\"0\","
                   "\"credit_minor\":\"5\",\"equity_minor\":\"-5\"}}";
    LD_STEP = 0;
    CHECK(larder_refresh(&LD) == ZXM_E_PROTOCOL && !memcmp(LD.c, LD_KEEP.c, sizeof LD.c),
          "larder: a holder below zero: refused");
    LD.link = &LINK;

    /* LA3: dropping a category */
    before = sent;
    CHECK(larder_drop(&LD, fuel, false) == ZXM_E_STATE && LD.n == 2 && sent == before &&
              larder_drop(&LD, fuel, true) == ZXM_OK && LD.n == 1 && !larder_category(&LD, fuel) &&
              LD.total_budget == 40000 && larder_drop(&LD, food, true) == ZXM_OK && LD.n == 0 &&
              !LD.ccy[0] && !LD.month_open,
          "larder: categories dropped on the owner's word");

    /* back to books in memory: the scratch file is removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "larder: scratch books removed");
}
