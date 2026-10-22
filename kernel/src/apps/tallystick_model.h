/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tallystick_model.h — Tallystick, invoices and receipts, as a model
 * (zxm.h): an invoice drafted line by line, numbered and issued, payments
 * against it recorded in the owner's books once the owner confirms, a
 * receipt when it is paid in full, statements of a customer's account,
 * and every document saved through the Banker and printed. The books are
 * zxvd's (ledger.*), reached through Coffer's model; statements are
 * Reckoner's (iso20022.render camt.053).
 *
 *   TS1 MONEY IS INTEGERS. Every amount is an int64 count of minor units of
 *       the invoice's currency; the rules are tallystick_rules.h (TR1-TR3).
 *       A value that does not fit is refused, never rounded, cut short or
 *       wrapped. A line's net is vintner.sale_value (qty * price), its
 *       total TR1 (the tax rounded half up), and the invoice's total the
 *       exact sum of its lines (coffer.holdings).
 *   TS2 ONE SEQUENCE. Invoices and receipts take the next number of one
 *       nine-digit sequence (TR3) only when issued, so a draft never uses
 *       one up. The register of numbers given is one chain: each entry
 *       links to the one before it (witness.chain_ok), checked whenever it
 *       grows or is read back. The last number is kept in a file of the
 *       Hoard (tallystick_save_register, tallystick_load_register), since
 *       zxvd keeps no counter of its own.
 *   TS3 THE OWNER CONFIRMS A PAYMENT. tallystick_propose_payment checks a
 *       payment against an issued invoice (1 .. what is still owed) and
 *       holds it through Coffer (coffer_propose, CF2); nothing is posted
 *       until tallystick_confirm_payment (ledger.post, from the customer's
 *       issuer account to the owner's holder account, reference "INV-<n>
 *       P<k>"), and tallystick_cancel_payment drops it.
 *   TS4 WHAT IS PAID IS READ FROM THE BOOKS. tallystick_reconcile sums the
 *       payee account's entries whose reference names the invoice
 *       (ledger.entries); paid in full is TR2 over paid - total
 *       (coffer.balance), held in Eve and Sutra.
 *   TS5 A RECEIPT only for an invoice paid in full, numbered in the same
 *       sequence (TS2).
 *   TS6 A STATEMENT of an account over a range of entry ids is Reckoner's
 *       (reckoner_set_period, reckoner_close, reckoner_statement,
 *       reckoner_render): a closed period, the books checked, camt.053.
 *   TS7 SAVED THROUGH THE BANKER (files.write, in parts when long): an
 *       invoice or receipt as a new file only (exclusive), never over an
 *       earlier one; then it may be printed (print.submit).
 *
 * Missing from zxvd, not invented: an invoice message to render (iso20022
 * renders pacs.008 and camt.053 only, so an invoice or receipt is text),
 * and a document counter kept by the daemon (the register is a file).
 * No drawing, no allocation, integers only. */
#ifndef TALLYSTICK_MODEL_H
#define TALLYSTICK_MODEL_H

#include "coffer_model.h"
#include "reckoner_model.h"
#include "tallystick_rules.h"
#include "zxm.h"

#define TSTK_LINES      32u  /* lines on one invoice */
#define TSTK_ITEM_MAX   63u  /* a line's description */
#define TSTK_PARTY_MAX  63u  /* the customer's name */
#define TSTK_REGISTER   256u /* numbers kept in the register */
#define TSTK_PAYMENTS   99u  /* payments against one invoice */
#define TSTK_FILE_MAX   127u
#define TSTK_URI_MAX    255u
#define TSTK_TEXT_MAX   8192u
#define TSTK_NOTICE_MAX 255u

typedef struct {
    char item[TSTK_ITEM_MAX + 1];
    int64_t qty, price, taxn, taxd;
    int64_t net, tax, total; /* TR1 */
} tstk_line_t;

enum { TSTK_DRAFT = 0, TSTK_ISSUED = 1 };
enum { TSTK_DOC_NONE = 0, TSTK_DOC_INVOICE = 1, TSTK_DOC_RECEIPT = 2 };

typedef struct {
    bool open;      /* a draft or an invoice is here */
    int state;      /* TSTK_DRAFT or TSTK_ISSUED */
    int64_t number; /* TS2: 0 while a draft */
    char party[TSTK_PARTY_MAX + 1];
    uint32_t customer, payee; /* an issuer account, a holder account */
    char ccy[COFFER_CCY_MAX + 1];
    tstk_line_t line[TSTK_LINES];
    uint32_t n;
    int64_t net, tax, total;
    int64_t paid;      /* TS4: from the books */
    bool paid_full;    /* TR2 */
    uint32_t payments; /* payments posted against it */
    int64_t receipt;   /* TS5: its receipt's number, 0 none */
} tstk_invoice_t;

typedef struct {
    zxm_link_t *link;
    coffer_t books;  /* TS3, TS4: the books through Coffer */
    reckoner_t stmt; /* TS6: statements through Reckoner */
    int64_t start;   /* TS2: the number before the register's first */
    int64_t number[TSTK_REGISTER], prev[TSTK_REGISTER];
    uint32_t nreg;
    tstk_invoice_t inv;
    int doc; /* what text holds */
    char text[TSTK_TEXT_MAX + 1];
    uint32_t text_len;
    char saved[TSTK_FILE_MAX + 1]; /* TS7: the last file saved, "" none */
    int64_t job;                   /* the last print job, -1 none */
    char notice[TSTK_NOTICE_MAX + 1];
} tallystick_t;

void tallystick_init(tallystick_t *t, zxm_link_t *link);
/* The books' accounts (coffer_load). */
int tallystick_load(tallystick_t *t);

/* TS2: the last number used before this register (0 .. 999999998), only
 * while the register is empty. */
int tallystick_set_last(tallystick_t *t, int64_t last);
/* The last number given: the register's newest, or start. */
int64_t tallystick_last(const tallystick_t *t);
/* TS2: every entry linked to the one before it (witness.chain_ok). */
bool tallystick_register_ok(const tallystick_t *t);
/* TS2: the last number as a file of the Hoard ("tallystick register\nlast
 * N\n"), replaced on each save; a load sets start from it and is refused
 * once numbers have been given. */
int tallystick_save_register(tallystick_t *t, const char *name);
int tallystick_load_register(tallystick_t *t, const char *name);

/* A new draft for party, paid from the issuer account customer into the
 * holder account payee, both in one currency. */
int tallystick_draft(tallystick_t *t, const char *party, uint32_t customer, uint32_t payee);
/* TR1: one line on the draft. */
int tallystick_add_line(tallystick_t *t, const char *item, int64_t qty, int64_t price, int64_t taxn,
                        int64_t taxd);
/* TS1, TS2: total it, number it and render it into t->text. */
int tallystick_issue(tallystick_t *t);

/* TS3: hold a payment of amount for the owner; post it on confirm. */
int tallystick_propose_payment(tallystick_t *t, int64_t amount);
int tallystick_confirm_payment(tallystick_t *t, uint64_t *entry);
int tallystick_cancel_payment(tallystick_t *t);
/* TS4: what has been paid against the invoice, from the books. */
int tallystick_reconcile(tallystick_t *t);
/* TS5: number and render the receipt into t->text. */
int tallystick_receipt(tallystick_t *t);

/* TS7: the text rendered last as a new file named name (one segment,
 * 1..127 bytes of [A-Za-z0-9._-], not starting with '.'), then printed
 * on the printer at uri. */
int tallystick_save(tallystick_t *t, const char *name);
int tallystick_print(tallystick_t *t, const char *uri);

/* TS6: account's statement over entry ids from..to as a camt.053 (msg_id
 * and when as reckoner_render takes them), into t->stmt.xml; then saved
 * as a new file and printed by Reckoner. */
int tallystick_statement(tallystick_t *t, uint32_t account, uint64_t from, uint64_t to,
                         const char *msg_id, const char *when);

#endif
