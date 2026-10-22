/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* neap_model.h — Neap, debit-and-credit books, as a model (zxm.h): a chart of
 * accounts over zxvd's ledger, entries checked and held for the owner,
 * the accounting equation after every post, bank statement lines
 * reconciled against the books (a line the books lack is held BOTH and
 * its amount put on a suspense hold), depreciation schedules, and reports
 * saved and printed. The books are zxvd's (ledger.*); Neap is built from
 * Coffer (the accounts, their balances and every posting, coffer_model.h)
 * and Reckoner (the period, camt.053 statements, the jurisdiction and the
 * tax estimate, reckoner_model.h), and never writes a book of its own.
 *
 *   NM1 MONEY IS INTEGERS. Every amount and balance is an int64 count of
 *       minor units; the rules are neap_rules.h (NR1-NR7). Anything that
 *       does not fit is refused, never rounded, cut short or wrapped.
 *   NM2 THE CHART. zxvd keeps no account class, so Neap's chart does: a
 *       tree of nodes (code 1..999999 under a parent's code, 0 a root),
 *       each of a kind (NEAP_ASSET .. NEAP_EXPENSE), open or closed, and
 *       at most one zxvd account each; a node is kept only when it fits
 *       ^ZXNEAP (NR7). A heading (no account) groups others. Closing a
 *       node is the owner's word; nothing posts to a closed one.
 *   NM3 AN ENTRY MAY POST (NR1) when its debits equal its credits, no
 *       account it touches is closed or outside the chart, the period is
 *       open (Reckoner's period, not closed), and coffer.transfer_ok
 *       holds too (the paying account, the owner's daily cap): both are
 *       asked, neither is enough alone.
 *   NM4 THE OWNER POSTS. neap_propose only checks an entry and holds it
 *       (Coffer's held transfer); nothing is posted until neap_confirm
 *       with the owner's word, and neap_cancel drops it.
 *   NM5 A = L + E AFTER EVERY POST (NR2): the balances are read again and
 *       summed by kind (coffer.holdings, the exact sum): assets, the
 *       debit-normal kind; liabilities and equity on their credit side,
 *       income less expenses carried into equity. The chart keeps one
 *       currency; every zxvd account of it must be in the chart (other
 *       currencies are another set of books), or the equation is not asked
 *       (ZXM_E_STATE). A gap other than 0, or ledger.check failing, is
 *       reported and the books are not trusted (equation_ok false).
 *   NM6 RECONCILING. A statement line (an amount on one account) is told
 *       by two sources: the statement (it happened) and the books (a
 *       journal entry of that amount not yet matched). They are joined
 *       with lpres_kjoin (lpres.h: the knowledge join, across sources
 *       about one subject), and the period's state with lpres_conjoin
 *       across lines, so one line the books lack keeps the period BOTH.
 *       A BOTH line is never matched by picking a source: the owner
 *       either books it or puts it on a suspense hold (NM7).
 *   NM7 SUSPENSE. neap_suspend puts a BOTH line's amount on a hold
 *       (ledger.hold, zxvd's escrow: one posting to "escrow: <CCY>", a
 *       holder account Neap charts as the suspense asset), with the
 *       owner's word; A = L + E still holds after it, and the line stays
 *       BOTH until the hold is released (ledger.release, the owner's
 *       word) or refunded. ledger.holds is read back and each escrow
 *       account must match its open holds.
 *   NM8 DEPRECIATION (NR3, NR4): a whole schedule in minor units, the
 *       remainder in the last period, its charges summing to cost -
 *       salvage exactly; a schedule that does not is refused.
 *   NM9 REPORTS. A trial balance by the chart, the equation and the last
 *       schedule as text; saved through the Banker as a new file only
 *       (files.write, exclusive) and printed (print.submit), each only
 *       when reckoner.report_ok holds (the period closed, ledger.check
 *       true). Tax figures (Reckoner's estimate) are advice, not tax
 *       advice: a person files, Neap never does.
 *
 * zxvd has no notification method, so the model composes its notice
 * (neap_t.notice) for the view to show. Missing from zxvd, not invented:
 * an account class, a closed account, a dated journal and a period kept
 * by the books. No drawing, no allocation, integers only. */
#ifndef NEAP_MODEL_H
#define NEAP_MODEL_H

#include "coffer_model.h"
#include "lpres.h"
#include "neap_rules.h"
#include "reckoner_model.h"
#include "zxm.h"

#define NEAP_NODES      64u
#define NEAP_LINES      32u /* statement lines reconciled in one period */
#define NEAP_HOLDS      32u
#define NEAP_TEXT_MAX   8192u
#define NEAP_FILE_MAX   127u
#define NEAP_URI_MAX    255u
#define NEAP_NOTICE_MAX 255u
#define NEAP_CODE_MAX   INT64_C(999999)
#define NEAP_SUSPENSE   INT64_C(999999) /* NM7: the suspense node's code */

enum { NEAP_ASSET = 1, NEAP_LIABILITY = 2, NEAP_EQUITY = 3, NEAP_INCOME = 4, NEAP_EXPENSE = 5 };
enum { NEAP_STRAIGHT = 1, NEAP_DECLINING = 2 };

typedef struct {
    int64_t code, parent;
    int64_t kind;    /* NEAP_ASSET .. NEAP_EXPENSE */
    int64_t open;    /* 1 open, 0 closed */
    int64_t account; /* zxvd's account id, -1 for a heading */
} neap_node_t;

typedef struct {
    uint32_t account;
    int64_t amount; /* signed, as the statement gives it: + in, - out */
    int64_t entry;  /* the journal entry it matched, -1 none */
    lpres_state_t state;
    int64_t hold; /* NM7: its suspense hold, 0 none */
} neap_line_t;

typedef struct {
    int64_t hold;
    char state[12]; /* held, released, refunded */
    uint32_t from, to, escrow;
    int64_t amount;
} neap_hold_t;

typedef struct {
    zxm_link_t *link;
    coffer_t books;               /* the accounts, their balances and every posting */
    reckoner_t rk;                /* the period, statements, jurisdiction, tax estimate */
    neap_node_t node[NEAP_NODES]; /* NM2 */
    uint32_t nn;
    int64_t assets, liabilities, equity, gap; /* NM5 */
    bool equation_ok;
    char ccy[COFFER_CCY_MAX + 1]; /* NM5: the chart's one currency, "" until an account */
    neap_line_t line[NEAP_LINES]; /* NM6 */
    uint32_t nl;
    lpres_state_t period_state;
    neap_hold_t hold[NEAP_HOLDS]; /* NM7: ledger.holds as last read */
    uint32_t nh;
    bool escrow_ok;             /* every escrow account matched its open holds */
    int64_t dep[NEAP_LIFE_MAX]; /* NM8: the last schedule's charges */
    uint32_t ndep;
    int64_t dep_method, dep_cost, dep_salvage;
    char text[NEAP_TEXT_MAX + 1]; /* NM9 */
    uint32_t text_len;
    char saved[NEAP_FILE_MAX + 1];
    int64_t job;
    char notice[NEAP_NOTICE_MAX + 1];
} neap_t;

void neap_init(neap_t *n, zxm_link_t *link, int64_t day_cap);

/* NM2: the chart. account is zxvd's id, or -1 for a heading. */
int neap_chart_add(neap_t *n, int64_t code, int64_t parent, int64_t kind, int64_t account);
int neap_chart_close(neap_t *n, int64_t code, bool confirmed);
const neap_node_t *neap_node_of(const neap_t *n, uint32_t account);

/* The books read (coffer_load) and NM5 asked. */
int neap_load(neap_t *n);
int neap_equation(neap_t *n);

/* NM3, NM4 */
int neap_propose(neap_t *n, uint32_t from, uint32_t to, int64_t amount, const char *ref);
int neap_confirm(neap_t *n, bool confirmed, uint64_t *entry);
int neap_cancel(neap_t *n);

/* NM6: one statement line of account; *state is the line's. */
int neap_reconcile(neap_t *n, uint32_t account, int64_t amount, lpres_state_t *state);
/* NM7: line i's amount on a suspense hold, paid from `from` to `to`. */
int neap_suspend(neap_t *n, uint32_t i, uint32_t from, uint32_t to, bool confirmed);
int neap_holds(neap_t *n);
int neap_release(neap_t *n, uint32_t i, bool confirmed);

/* NM8: a schedule into n->dep; rate is used by NEAP_DECLINING only. */
int neap_schedule(neap_t *n, int64_t method, int64_t cost, int64_t salvage, int64_t life,
                  int64_t rate);

/* NR5: what each of account's journal entries moves it on its normal
 * side, into out (up to cap), checked against its balance; *count set. */
int neap_ledger_lines(neap_t *n, uint32_t account, int64_t *out, uint32_t cap, uint32_t *count);
/* NR6: each row's cell of the accounts-by-months grid. */
int neap_grid(const int64_t *account, const int64_t *month, const int64_t *amount, uint32_t rows,
              int64_t *out);

/* NM9 */
int neap_report(neap_t *n);
int neap_save(neap_t *n, const char *name);
int neap_print(neap_t *n, const char *uri);

#endif
