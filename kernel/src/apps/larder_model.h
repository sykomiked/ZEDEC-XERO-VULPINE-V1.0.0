/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* larder_model.h — Larder, the household budget, as a model (zxm.h): a
 * monthly budget per category against what the owner's books say was
 * spent in it. The books are zxvd's (ledger.accounts, ledger.balance,
 * ledger.entries); Larder only reads them and never posts.
 *
 *   LA1 A CATEGORY is a holder account of the books, in the budget's one
 *       currency (the first category's). Its budget this month is the
 *       owner's monthly base plus what the last month carried into it;
 *       each is 0 .. LARDER_MINOR_MAX minor units (18 digits), and so is
 *       their sum, else refused. A base of 0 tracks a category without a
 *       budget: it is never over.
 *   LA2 THE MONTH. ledger.entries carries no posting time, so a month
 *       starts when the owner says (larder_start_month): each category's
 *       balance then is its opening, and its newest entry id then is the
 *       month's mark. larder_refresh reads each balance again: spent is
 *       closing - opening (larder_category_spent, LR1), and the entries
 *       after the mark must add up to it, and the whole journal to the
 *       closing balance, or the answer is refused (ZXM_E_PROTOCOL) and the
 *       model keeps what it had. A category is over its budget by
 *       larder_category_over_budget (LR2) with the excess spent - budget
 *       (coffer_balance); the month's totals are coffer_holdings, and the
 *       carry-over larder_unspent_carry (LR3).
 *   LA3 THE OWNER CONFIRMS. Every call that changes what Larder keeps
 *       (larder_set_budget, larder_drop, larder_start_month,
 *       larder_close_month) takes owner_confirmed, checked after its
 *       arguments and before anything is asked of zxvd: without it the
 *       call returns ZXM_E_STATE, sends nothing and changes nothing. Over
 *       budget is advice: Larder never moves money.
 *   LA4 CLOSING A MONTH carries each category's unspent part, max(0,
 *       budget - spent), into its next budget (base + carried); a next
 *       budget past 18 digits refuses the close, and nothing changes.
 *       The new month starts at once, from the balances read then.
 *   LA5 Nothing is changed before it is checked, and the kept state is
 *       replaced only after every answer it depends on has been read.
 *
 * zxvd has no method that raises a notice, so the model composes the
 * notice text (larder_t.notice) for the view to show. No drawing, no
 * allocation, integers only. */
#ifndef LARDER_MODEL_H
#define LARDER_MODEL_H

#include "coffer_model.h"
#include "larder_rules.h"
#include "zxm.h"

#define LARDER_CATEGORIES 32u
#define LARDER_ACCOUNTS   64u /* ledger.accounts read at once (as Coffer) */
#define LARDER_NAME_MAX   63u /* ledger.open_account name */
#define LARDER_CCY_MAX    15u /* a currency code */
#define LARDER_NOTICE_MAX 511u

typedef struct {
    uint32_t account;
    char name[LARDER_NAME_MAX + 1];
    int64_t base;    /* the owner's monthly budget, minor units */
    int64_t carried; /* what the last month left unspent here */
    int64_t budget;  /* this month's: base + carried */
    int64_t opening; /* the balance when the month started */
    int64_t mark;    /* the newest entry id then; -1 none */
    int64_t closing; /* the balance at the last refresh */
    int64_t spent;   /* closing - opening (LR1) */
    bool over;       /* LR2, both witnesses' rule */
} larder_category_t;

typedef struct {
    zxm_link_t *link;
    char ccy[LARDER_CCY_MAX + 1]; /* the budget's currency; "" until the first category */
    larder_category_t c[LARDER_CATEGORIES];
    uint32_t n;
    bool month_open; /* LA2 */
    bool fresh;      /* refreshed since the last change */
    int64_t total_budget, total_spent, carry;
    uint32_t over_n;
    char notice[LARDER_NOTICE_MAX + 1];
} larder_t;

void larder_init(larder_t *l, zxm_link_t *link);

/* LA1, LA3: set the monthly base budget of the holder account `account`,
 * adding it as a category when it is not one yet (snapshotting it now
 * when a month is open). */
int larder_set_budget(larder_t *l, uint32_t account, int64_t base, bool owner_confirmed);
/* LA3: stop tracking a category; the last one dropped frees the currency. */
int larder_drop(larder_t *l, uint32_t account, bool owner_confirmed);
/* LA2, LA3: start a month from the balances now; carried amounts kept. */
int larder_start_month(larder_t *l, bool owner_confirmed);
/* LA2: read every category's balance and entries, and the totals. */
int larder_refresh(larder_t *l);
/* LA4, LA3: carry the unspent parts into next month's budgets and start it. */
int larder_close_month(larder_t *l, bool owner_confirmed);

/* The category of account, or NULL. */
const larder_category_t *larder_category(const larder_t *l, uint32_t account);

#endif
