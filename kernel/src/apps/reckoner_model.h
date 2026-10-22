/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* reckoner_model.h — Reckoner, statements and tax reports, as a model
 * (zxm.h): one account's statement for a period from the books, rendered
 * as an ISO 20022 camt.053, a tax estimate over integer bands, the
 * jurisdiction the books keep, and the report saved through the Banker
 * and printed. The books are zxvd's (ledger.*); the model reads them and
 * never posts.
 *
 *   RK1 MONEY IS INTEGERS. Every amount is an int64 count of minor units
 *       of its currency; the rules are reckoner_rules.h (RR1-RR3). A value
 *       that does not fit is refused, never rounded, cut short or wrapped.
 *   RK2 THE PERIOD. zxvd's journal carries no dates, so a period is a
 *       range of entry ids, from..to, both inside, that the owner sets.
 *       The owner closes it (reckoner_close): then it cannot be moved
 *       until reopened, and reopening drops what was rendered from it.
 *   RK3 A STATEMENT. ledger.accounts names the account and its currency,
 *       ledger.entries gives its journal: the opening balance is the net
 *       of the entries before from and the period's net the net of those
 *       inside (coffer.holdings, the exact sum), credits and debits each
 *       direction's total, and the closing is RR1, checked against
 *       coffer.balance(opening, net). An answer that does not add up is
 *       refused (ZXM_E_PROTOCOL) and the statement kept as it was.
 *   RK4 REPORT ALLOWED (RR3, report.eve): closed, and the books hold as
 *       the last ledger.check answered (reckoner_check). Rendering,
 *       saving and printing each ask it first; nothing is sent otherwise.
 *   RK5 THE STATEMENT RENDERED. iso20022.render camt053 with the
 *       statement's opening, closing and booked entries; zxvd renders it
 *       only when it reconciles, and nothing is sent anywhere.
 *   RK6 THE TAX ESTIMATE. Over bands the owner gives (RR2): zxvd has no
 *       tax table, so none is assumed. It is advice, not tax advice, and
 *       says so; a person files, Reckoner never does.
 *   RK7 SAVED THROUGH THE BANKER (files.write, in parts when long): a new
 *       file only (exclusive), never over an earlier report; then it may
 *       be printed (print.submit).
 *
 * Missing from zxvd, not invented: a dated journal (an entry's date), a
 * period close kept by the books, and a tax table per jurisdiction. No
 * drawing, no allocation, integers only. */
#ifndef RECKONER_MODEL_H
#define RECKONER_MODEL_H

#include "reckoner_rules.h"
#include "zxm.h"

#define RECKONER_ENTRIES    128u /* entries in one statement (one render request) */
#define RECKONER_JOURNAL    512u /* entries of one account's journal read */
#define RECKONER_NAME_MAX   63u
#define RECKONER_CCY_MAX    15u
#define RECKONER_ID_MAX     35u /* a message id */
#define RECKONER_WHEN_MAX   31u /* an ISO 8601 date-time */
#define RECKONER_FILE_MAX   127u
#define RECKONER_URI_MAX    255u
#define RECKONER_XML_MAX    (48u * 1024u)
#define RECKONER_TEXT_MAX   4096u
#define RECKONER_NOTICE_MAX 255u

typedef struct {
    uint64_t id;
    int64_t minor; /* signed: + credit, - debit */
} reckoner_entry_t;

typedef struct {
    bool ready;
    uint32_t account;
    char name[RECKONER_NAME_MAX + 1];
    char ccy[RECKONER_CCY_MAX + 1];
    int64_t opening, credits, debits, net, closing;
    reckoner_entry_t e[RECKONER_ENTRIES]; /* the period's entries, oldest first */
    uint32_t n;
} reckoner_statement_t;

typedef struct {
    bool ready;
    int64_t income;
    int64_t lo[RECKONER_BANDS], hi[RECKONER_BANDS], rate[RECKONER_BANDS];
    int64_t tax[RECKONER_BANDS];
    uint32_t n;
    int64_t total;
} reckoner_tax_t;

typedef struct {
    zxm_link_t *link;
    bool period_set, closed; /* RK2 */
    uint64_t from, to;
    bool books_ok; /* the last ledger.check */
    bool juris_loaded;
    char country[3];
    char standard[16];
    char report_standard[16];
    reckoner_statement_t st;
    char xml[RECKONER_XML_MAX + 1]; /* RK5 */
    uint32_t xml_len;               /* 0: nothing rendered */
    reckoner_tax_t tax;             /* RK6 */
    char text[RECKONER_TEXT_MAX + 1];
    uint32_t text_len;
    char saved[RECKONER_FILE_MAX + 1]; /* RK7: the last file saved, "" none */
    int64_t job;                       /* the last print job, -1 none */
    char notice[RECKONER_NOTICE_MAX + 1];
} reckoner_t;

void reckoner_init(reckoner_t *r, zxm_link_t *link);

/* RK2: the period in entry ids, from <= to; ZXM_E_STATE while closed. */
int reckoner_set_period(reckoner_t *r, uint64_t from, uint64_t to);
int reckoner_close(reckoner_t *r);
int reckoner_reopen(reckoner_t *r);

/* ledger.check: ZXM_OK and r->books_ok when the books hold. */
int reckoner_check(reckoner_t *r);
/* RK4: the period is closed and the books hold (reckoner_report_ok). */
bool reckoner_allowed(const reckoner_t *r);

/* RK3: ledger.accounts and ledger.entries of one account into r->st. */
int reckoner_statement(reckoner_t *r, uint32_t account);
/* RK5: the statement as a camt.053 into r->xml; msg_id 1..35 bytes and
 * when an ISO 8601 date-time (YYYY-MM-DDThh:mm:ss) the caller gives. */
int reckoner_render(reckoner_t *r, const char *msg_id, const char *when);

/* jurisdiction.get: the country and the standard reports follow. */
int reckoner_jurisdiction(reckoner_t *r);
/* RK6: the estimate over n bands into r->tax and its text into r->text. */
int reckoner_tax(reckoner_t *r, int64_t income, const int64_t *lo, const int64_t *hi,
                 const int64_t *rate, uint32_t n);

/* RK7: the rendered statement, or the tax estimate's text, as a new file
 * named name (one segment of the Hoard, 1..127 bytes, no '/'). */
int reckoner_save_statement(reckoner_t *r, const char *name);
int reckoner_save_estimate(reckoner_t *r, const char *name);
/* RK7: print the last saved file on the printer at uri (print.submit). */
int reckoner_print(reckoner_t *r, const char *uri);

#endif
