/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* coffer_model.h — Coffer, the wallet and the books, as a model (zxm.h):
 * the owner's accounts and their balances, one account's entries,
 * transfers between accounts, what the owner holds in a currency, and the
 * books check. The books are zxvd's (ledger.*, currency.enable); the model
 * keeps a copy for the view and never writes a book of its own.
 *
 *   CF1 MONEY IS INTEGERS. Every amount and balance is an int64 count of
 *       minor units of its currency (cents for USD). A posting is 1 ..
 *       COFFER_AMOUNT_MAX (libzxv ZXV_AMOUNT_MAX, 2^53 - 1); the rules'
 *       arithmetic holds 18 digits either way (COFFER_MINOR_MAX). A value
 *       that does not fit is refused (COFFER_REFUSED, or ZXM_E_ARG),
 *       never rounded, cut short or wrapped.
 *   CF2 THE OWNER CONFIRMS. coffer_propose checks a transfer and holds it
 *       (coffer_t.held) with a notice for the view; it sends nothing.
 *       Only coffer_confirm posts it (ledger.post), and coffer_cancel
 *       drops it. One transfer waits at a time.
 *   CF3 A TRANSFER IS ALLOWED (coffer_transfer_ok, transfer.eve and
 *       transfer.sutra) when its amount is 1 .. COFFER_AMOUNT_MAX, the
 *       paying holder account is not below zero after it, the owner's
 *       per-day cap is not passed, and it is balanced (what leaves one
 *       account enters the other). An issuer account (an outside party's
 *       side of the books) has no floor and its money coming in is not the
 *       owner's spending: its "after" and "room" are taken at the edge, 0.
 *   CF4 THE DAY. The model counts what left holder accounts today
 *       (coffer_t.spent) against day_cap; coffer_set_day with a new day
 *       starts the count again.
 *   CF5 WHAT IS READ IS CHECKED. A balance whose equity is not debit -
 *       credit, an entry list that does not add up to the balance, or a
 *       number that is not a 64-bit decimal is refused (ZXM_E_PROTOCOL)
 *       and the model keeps what it had.
 *   CF6 Nothing is changed before it is checked, and the copy is replaced
 *       only after a whole answer has been read.
 *
 * zxvd has no notification method, so the model composes the notice text
 * (coffer_t.notice) for the view to show. No drawing, no allocation,
 * integers only. */
#ifndef COFFER_MODEL_H
#define COFFER_MODEL_H

#include "zxm.h"

#define COFFER_ACCOUNTS   64u
#define COFFER_ENTRIES    256u
#define COFFER_NAME_MAX   63u /* ledger.open_account name */
#define COFFER_CCY_MAX    15u /* a currency code */
#define COFFER_REF_MAX    35u /* ledger.post reference */
#define COFFER_PATH_MAX   255u
#define COFFER_NOTICE_MAX 255u

#define COFFER_AMOUNT_MAX INT64_C(9007199254740991)   /* 2^53 - 1, libzxv ZXV_AMOUNT_MAX */
#define COFFER_MINOR_MAX  INT64_C(999999999999999999) /* 18 digits, Sutra's s9(18) */
#define COFFER_REFUSED    INT64_MIN
#define COFFER_DAY_CAP    INT64_C(100000) /* default: 1000.00 of a 2-digit currency a day */

typedef struct {
    uint32_t id;
    char name[COFFER_NAME_MAX + 1];
    char ccy[COFFER_CCY_MAX + 1];
    bool issuer;
    int64_t balance; /* equity: debit - credit, minor units */
} coffer_account_t;

typedef struct {
    uint64_t id;
    int64_t minor; /* signed: + received, - sent */
    uint32_t counterparty;
    char ref[COFFER_REF_MAX + 1];
} coffer_entry_t;

typedef struct {
    bool pending;
    uint32_t from, to;
    int64_t amount;
    char ccy[COFFER_CCY_MAX + 1];
    char ref[COFFER_REF_MAX + 1];
} coffer_transfer_t;

typedef struct {
    zxm_link_t *link;
    bool loaded; /* the accounts have been read */
    coffer_account_t a[COFFER_ACCOUNTS];
    uint32_t n;
    coffer_entry_t e[COFFER_ENTRIES]; /* one account's journal, oldest first */
    uint32_t ne;
    int64_t e_account; /* whose entries e holds; -1 none */
    int64_t day_cap;   /* CF4 */
    int64_t day;
    int64_t spent;
    coffer_transfer_t held; /* CF2 */
    bool books_ok;          /* the last ledger.check */
    char notice[COFFER_NOTICE_MAX + 1];
} coffer_t;

/* The rules, as the app's logic says them (coffer.zxapp). */

/* before + moved; COFFER_REFUSED when before, moved or the result is past
 * COFFER_MINOR_MAX either way. */
int64_t coffer_balance(int64_t before, int64_t moved);
/* CF3: amount 1 .. COFFER_AMOUNT_MAX, after and room 0 .. COFFER_MINOR_MAX,
 * gap 0. */
bool coffer_transfer_ok(int64_t amount, int64_t after, int64_t room, int64_t gap);
/* The exact sum of n balances, added from the first; COFFER_REFUSED when
 * balance is NULL (and n > 0) or a partial sum passes 64 bits (a sum of
 * exactly INT64_MIN is the refusal's own value, and is refused too). */
int64_t coffer_holdings(const int64_t *balance, uint32_t n);

/* The books. */
void coffer_init(coffer_t *c, zxm_link_t *link, int64_t day_cap);
/* currency.enable: an ISO 4217 alpha code, 3 capital letters. */
int coffer_enable(coffer_t *c, const char *code);
/* ledger.accounts, then ledger.balance of each (CF5, CF6). */
int coffer_load(coffer_t *c);
/* ledger.open_account, then a load; *id is the new account. */
int coffer_open_account(coffer_t *c, const char *name, const char *ccy, bool issuer, uint32_t *id);
/* ledger.entries of one account into c->e (CF5). */
int coffer_entries(coffer_t *c, uint32_t account);
/* ledger.check: ZXM_OK and c->books_ok when the books hold. */
int coffer_check(coffer_t *c);
/* books.file: where the books are saved; *saved false when in memory only. */
int coffer_books(coffer_t *c, char *path, uint32_t cap, bool *saved);

/* CF4: the day (days since 1970) and the owner's per-day cap. */
int coffer_set_day(coffer_t *c, int64_t day);
int coffer_set_cap(coffer_t *c, int64_t day_cap);

/* CF2: check a transfer and hold it for the owner; nothing is sent. */
int coffer_propose(coffer_t *c, uint32_t from, uint32_t to, int64_t amount, const char *ref);
/* Post the held transfer (ledger.post), then load; *entry is its id. */
int coffer_confirm(coffer_t *c, uint64_t *entry);
int coffer_cancel(coffer_t *c);

/* What the owner's holder accounts hold in ccy (coffer_holdings). */
int coffer_holdings_in(const coffer_t *c, const char *ccy, int64_t *total);
/* The account with id, or NULL. */
const coffer_account_t *coffer_account(const coffer_t *c, uint32_t id);

#endif
