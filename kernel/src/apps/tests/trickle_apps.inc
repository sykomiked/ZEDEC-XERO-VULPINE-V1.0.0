/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* Trickle's model against zxvd's real dispatcher (LINK). The books are a
 * scratch file in the test's own folder, never real books: accounts are
 * opened and funded through Coffer's model, streams and tithes paid
 * through Trickle's, and the books closed again (in memory) at the end. */
#include "trickle_model.h"

static trickle_t TRK_A;
static coffer_t TRK_BOOKS;

/* The balance of account id as the books say it now. */
static int64_t trickle_balance_of(uint32_t id)
{
    if (coffer_load(&TRK_BOOKS) != ZXM_OK) return COFFER_REFUSED;
    const coffer_account_t *a = coffer_account(&TRK_BOOKS, id);
    return a ? a->balance : COFFER_REFUSED;
}

APPS_TEST(trickle)
{
    /* the rules */
    CHECK(trk_stream_due(1000, 1, 3, 0) == 333 && trk_stream_due(1000, 2, 3, 333) == 333 &&
              trk_stream_due(1000, 3, 3, 666) == 334 && trk_stream_due(0, 0, 1, 0) == 0 &&
              trk_stream_due(TRK_TOTAL_MAX, TRK_TICKS_MAX, TRK_TICKS_MAX, 0) == TRK_TOTAL_MAX &&
              trk_stream_due(1000, 1, 3, 334) == TRK_REFUSED &&
              trk_stream_due(1000, 4, 3, 0) == TRK_REFUSED &&
              trk_stream_due(1000, 1, 0, 0) == TRK_REFUSED &&
              trk_stream_due(1000, 1, TRK_TICKS_MAX + 1, 0) == TRK_REFUSED &&
              trk_stream_due(TRK_TOTAL_MAX + 1, 1, 1, 0) == TRK_REFUSED &&
              trk_stream_due(1, 1, 1, -1) == TRK_REFUSED,
          "stream due: floor(total * tick / ticks) - paid, and its edges");
    CHECK(trk_tithe_due(1005, 1, 10, 0) == 100 && trk_tithe_carry(1005, 1, 10, 0) == 5 &&
              trk_tithe_due(1005, 1, 10, 5) == 101 && trk_tithe_carry(1005, 1, 10, 5) == 0 &&
              trk_tithe_due(7, 2, 3, 2) == 5 && trk_tithe_carry(7, 2, 3, 2) == 1 &&
              trk_tithe_due(1, 1, 10, 10) == TRK_REFUSED &&
              trk_tithe_carry(1, 11, 10, 0) == TRK_REFUSED &&
              trk_tithe_due(1, 0, 0, 0) == TRK_REFUSED &&
              trk_tithe_carry(-1, 1, 1, 0) == TRK_REFUSED,
          "tithe: floor((income * tnum + carry) / tden), its remainder carried");
    {
        /* TK2: over any run of incomes the tithes sum to floor(sum * tnum / tden) */
        int64_t carry = 0, given = 0, sum = 0;
        for (int64_t k = 0; k < 500; k++) {
            int64_t income = (k * 7919) % 1013;
            given += trk_tithe_due(income, 3, 7, carry);
            carry = trk_tithe_carry(income, 3, 7, carry);
            sum += income;
        }
        CHECK(given == sum * 3 / 7 && carry == sum * 3 % 7, "no unit lost: %lld of %lld",
              (long long) given, (long long) sum);
    }
    CHECK(trk_stream_pay_ok(1, 0, 0, 0, 1) &&
              trk_stream_pay_ok(TRK_AMOUNT_MAX, TRK_MINOR_MAX, TRK_MINOR_MAX, TRK_MINOR_MAX, 1) &&
              !trk_stream_pay_ok(0, 0, 0, 0, 1) && !trk_stream_pay_ok(5, -1, 0, 0, 1) &&
              !trk_stream_pay_ok(5, 0, -1, 0, 1) && !trk_stream_pay_ok(5, 0, 0, -1, 1) &&
              !trk_stream_pay_ok(5, 0, 0, 0, 0) && !trk_stream_pay_ok(5, 0, 0, 0, 2) &&
              !trk_stream_pay_ok(TRK_AMOUNT_MAX + 1, 0, 0, 0, 1),
          "stream payment allowed: each field in its range");

    /* scratch books: an issuer funds the owner's account, which pays a payee */
    char root[256], scratch[512], path[COFFER_PATH_MAX + 1];
    bool saved = false;
    CHECK(zxf_root(root, sizeof root) == 0, "the test's folder");
    snprintf(scratch, sizeof scratch, "%s/trickle-scratch.books", root);
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file(scratch) == 0 && zxvd_rpc_books_open() == 0, "scratch books open");
    coffer_init(&TRK_BOOKS, &LINK, COFFER_AMOUNT_MAX);
    CHECK(coffer_books(&TRK_BOOKS, path, sizeof path, &saved) == ZXM_OK && saved &&
              !strcmp(path, scratch),
          "every post goes to the scratch file: %s", path);
    uint32_t bank = 0, own = 0, payee = 0, euro = 0;
    uint64_t entry = 0;
    CHECK(coffer_enable(&TRK_BOOKS, "USD") == ZXM_OK &&
              coffer_enable(&TRK_BOOKS, "EUR") == ZXM_OK &&
              coffer_open_account(&TRK_BOOKS, "bank", "USD", true, &bank) == ZXM_OK &&
              coffer_open_account(&TRK_BOOKS, "own", "USD", false, &own) == ZXM_OK &&
              coffer_open_account(&TRK_BOOKS, "payee", "USD", false, &payee) == ZXM_OK &&
              coffer_open_account(&TRK_BOOKS, "euro", "EUR", false, &euro) == ZXM_OK &&
              coffer_propose(&TRK_BOOKS, bank, own, 100000, "funding") == ZXM_OK &&
              coffer_confirm(&TRK_BOOKS, &entry) == ZXM_OK,
          "accounts opened and funded: %s", LINK.err);

    /* nothing is asked before it is checked */
    trickle_init(&TRK_A, &LINK, 50000);
    uint32_t slot = 99, s2 = 99, s3 = 99, g = 99;
    int before = sent;
    CHECK(trickle_stream_start(&TRK_A, own, payee, 1000, 3, true, &slot) == ZXM_E_STATE &&
              trickle_tithe_start(&TRK_A, own, payee, 1, 10, true, &g) == ZXM_E_STATE &&
              trickle_stream_pay(&TRK_A, 0, 1, true, &entry) == ZXM_E_ARG &&
              trickle_set_cap(&TRK_A, -1, true) == ZXM_E_ARG && sent == before,
          "before the books are read: refused, nothing asked");
    CHECK(trickle_load(&TRK_A) == ZXM_OK && TRK_A.books.loaded, "accounts read: %s", LINK.err);
    before = sent;
    CHECK(trickle_stream_start(&TRK_A, own, payee, 1000, 3, true, NULL) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, bank, payee, 1000, 3, true, &slot) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, euro, 1000, 3, true, &slot) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, own, 1000, 3, true, &slot) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, 999, 1000, 3, true, &slot) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, payee, 0, 3, true, &slot) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, payee, TRK_TOTAL_MAX + 1, 3, true, &slot) ==
                  ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, payee, 1000, 0, true, &slot) == ZXM_E_ARG &&
              trickle_stream_start(&TRK_A, own, payee, 1000, TRK_TICKS_MAX + 1, true, &slot) ==
                  ZXM_E_ARG &&
              slot == 99 && sent == before,
          "a stream's accounts and terms checked");

    /* TQ2: without the owner's confirmation nothing starts */
    CHECK(trickle_stream_start(&TRK_A, own, payee, 1000, 3, false, &slot) == ZXM_E_STATE &&
              slot == 99 && !TRK_A.s[0].used && strstr(TRK_A.notice, "Waiting for your") &&
              sent == before,
          "unconfirmed start: nothing kept, nothing sent");
    CHECK(trickle_stream_start(&TRK_A, own, payee, 1000, 3, true, &slot) == ZXM_OK && slot == 0 &&
              TRK_A.s[0].approved && TRK_A.s[0].paid == 0 && sent == before,
          "stream started");
    int64_t due = -1;
    CHECK(trickle_stream_due_at(&TRK_A, slot, 1, &due) == ZXM_OK && due == 333 &&
              trickle_stream_due_at(&TRK_A, slot, 4, &due) == ZXM_E_ARG &&
              trickle_stream_due_at(&TRK_A, 5, 1, &due) == ZXM_E_ARG,
          "due at tick 1: %lld", (long long) due);

    /* TQ2: a payment waits for the owner */
    CHECK(trickle_stream_pay(&TRK_A, slot, 1, false, &entry) == ZXM_E_STATE &&
              TRK_A.s[0].paid == 0 && strstr(TRK_A.notice, "Pay 333 now?") && sent == before,
          "unconfirmed payment: nothing sent: %s", TRK_A.notice);
    entry = 0;
    CHECK(trickle_stream_pay(&TRK_A, slot, 1, true, &entry) == ZXM_OK && entry > 0 &&
              TRK_A.s[0].paid == 333 && TRK_A.s[0].tick == 1 && TRK_A.books.spent == 333 &&
              trickle_balance_of(own) == 100000 - 333 && trickle_balance_of(payee) == 333,
          "tick 1 paid: %s %s", TRK_A.notice, LINK.err);
    before = sent;
    CHECK(trickle_stream_pay(&TRK_A, slot, 1, true, &entry) == ZXM_OK && entry == 0 &&
              TRK_A.s[0].paid == 333 && sent == before &&
              trickle_stream_pay(&TRK_A, slot, 0, true, &entry) == ZXM_E_ARG,
          "tick 1 again: nothing due, nothing sent; an earlier tick refused");

    /* TQ2: a change never claws back */
    CHECK(trickle_stream_change(&TRK_A, slot, 500, 3, true) == ZXM_E_ARG &&
              trickle_stream_change(&TRK_A, slot, 1200, 3, false) == ZXM_E_STATE &&
              TRK_A.s[0].total == 1000 &&
              trickle_stream_change(&TRK_A, slot, 1200, 3, true) == ZXM_OK &&
              TRK_A.s[0].total == 1200 && sent == before,
          "changed to 1200 only when confirmed");
    CHECK(trickle_stream_pay(&TRK_A, slot, 3, true, &entry) == ZXM_OK && TRK_A.s[0].paid == 1200 &&
              trickle_balance_of(payee) == 1200 && strstr(TRK_A.notice, "paid in full"),
          "the last tick takes what was held back: %s", TRK_A.notice);

    /* TQ4: what is paid is read from the books */
    TRK_A.s[0].paid = 5;
    CHECK(trickle_stream_reconcile(&TRK_A, slot) == ZXM_OK && TRK_A.s[0].paid == 1200,
          "read back: %lld %s", (long long) TRK_A.s[0].paid, LINK.err);

    /* TQ3: the per-day cap */
    before = sent;
    CHECK(trickle_set_cap(&TRK_A, 1400, false) == ZXM_E_STATE && TRK_A.books.day_cap == 50000 &&
              trickle_set_cap(&TRK_A, 1400, true) == ZXM_OK && TRK_A.books.day_cap == 1400,
          "the cap moves only when confirmed");
    CHECK(trickle_stream_start(&TRK_A, own, payee, 600, 2, true, &s2) == ZXM_OK && s2 == 1 &&
              trickle_stream_pay(&TRK_A, s2, 1, true, &entry) == ZXM_E_ARG &&
              strstr(TRK_A.notice, "per-day cap") && TRK_A.s[1].paid == 0 && sent == before,
          "past today's cap: refused, nothing sent: %s", TRK_A.notice);
    CHECK(trickle_set_day(&TRK_A, 1) == ZXM_OK && TRK_A.books.spent == 0 &&
              trickle_stream_pay(&TRK_A, s2, 1, true, &entry) == ZXM_OK && TRK_A.s[1].paid == 300,
          "a new day: paid: %s", TRK_A.notice);

    /* TQ2: a stopped stream pays nothing */
    before = sent;
    CHECK(trickle_stream_stop(&TRK_A, s2, false) == ZXM_E_STATE && TRK_A.s[1].approved &&
              trickle_stream_stop(&TRK_A, s2, true) == ZXM_OK && !TRK_A.s[1].approved &&
              trickle_stream_pay(&TRK_A, s2, 2, true, &entry) == ZXM_E_STATE &&
              trickle_stream_change(&TRK_A, s2, 900, 2, true) == ZXM_E_STATE &&
              trickle_stream_stop(&TRK_A, s2, true) == ZXM_E_STATE && TRK_A.s[1].paid == 300 &&
              sent == before,
          "stopped: %s", TRK_A.notice);

    /* TQ3: the paying account never goes below zero */
    CHECK(trickle_set_cap(&TRK_A, TRK_AMOUNT_MAX, true) == ZXM_OK &&
              trickle_stream_start(&TRK_A, own, payee, 999999, 1, true, &s3) == ZXM_OK &&
              trickle_stream_pay(&TRK_A, s3, 1, true, &entry) == ZXM_E_ARG &&
              strstr(TRK_A.notice, "below zero") && sent == before,
          "not enough: %s", TRK_A.notice);

    /* tithes: a tenth of each income, the remainder carried */
    CHECK(trickle_tithe_start(&TRK_A, own, payee, 0, 10, true, &g) == ZXM_E_ARG &&
              trickle_tithe_start(&TRK_A, own, payee, 11, 10, true, &g) == ZXM_E_ARG &&
              trickle_tithe_start(&TRK_A, bank, payee, 1, 10, true, &g) == ZXM_E_ARG &&
              trickle_tithe_start(&TRK_A, own, payee, 1, 10, false, &g) == ZXM_E_STATE && g == 99 &&
              trickle_tithe_start(&TRK_A, own, payee, 1, 10, true, &g) == ZXM_OK && g == 0 &&
              sent == before,
          "a tithe starts only when confirmed");
    int64_t payee_before = trickle_balance_of(payee);
    before = sent;
    CHECK(trickle_tithe_pay(&TRK_A, g, 1005, false, &entry) == ZXM_E_STATE &&
              TRK_A.g[0].carry == 0 && TRK_A.g[0].incomes == 0 && sent == before &&
              trickle_tithe_pay(&TRK_A, g, -1, true, &entry) == ZXM_E_ARG &&
              trickle_tithe_pay(&TRK_A, g, TRK_TOTAL_MAX + 1, true, &entry) == ZXM_E_ARG,
          "an unconfirmed tithe changes nothing");
    static const int64_t income[5] = {1005, 1005, 999, 3, 7};
    static const int64_t tithe[5] = {100, 101, 99, 1, 0}, carry[5] = {5, 0, 9, 2, 9};
    bool ok = true;
    for (uint32_t k = 0; k < 5; k++) {
        entry = 1;
        int rc = trickle_tithe_pay(&TRK_A, g, income[k], true, &entry);
        ok = ok && rc == ZXM_OK && TRK_A.g[0].carry == carry[k] && (entry == 0) == (tithe[k] == 0);
    }
    CHECK(ok && TRK_A.g[0].given == 301 && TRK_A.g[0].incomes == 5 &&
              trickle_balance_of(payee) == payee_before + 301,
          "tithes 100, 101, 99, 1, 0 of 3019 income: %lld given, carry %lld",
          (long long) TRK_A.g[0].given, (long long) TRK_A.g[0].carry);
    before = sent;
    CHECK(trickle_tithe_stop(&TRK_A, g, false) == ZXM_E_STATE &&
              trickle_tithe_stop(&TRK_A, g, true) == ZXM_OK &&
              trickle_tithe_pay(&TRK_A, g, 1000, true, &entry) == ZXM_E_STATE && sent == before,
          "a stopped tithe gives nothing");
    CHECK(coffer_check(&TRK_BOOKS) == ZXM_OK && TRK_BOOKS.books_ok, "the books hold: %s", LINK.err);

    /* a stream whose books say it paid past its total is refused, kept as it was */
    CHECK(coffer_propose(&TRK_BOOKS, own, payee, 5, "TRICKLE S1 T9") == ZXM_OK &&
              coffer_confirm(&TRK_BOOKS, &entry) == ZXM_OK &&
              trickle_stream_reconcile(&TRK_A, slot) == ZXM_E_PROTOCOL && TRK_A.s[0].paid == 1200,
          "books past the stream's total: refused, the stream kept as it was");

    /* back to books in memory: the scratch file removed */
    zxvd_rpc_books_close();
    CHECK(zxvd_rpc_books_file("") == 0 && remove(scratch) == 0, "scratch books removed");
}
