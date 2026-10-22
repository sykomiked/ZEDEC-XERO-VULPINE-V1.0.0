/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tallystick_model.c — Tallystick, invoices and receipts (see
 * tallystick_model.h). Integers only: money is minor units, and nothing
 * that does not fit is ever rounded. */
#include <string.h>

#include "tallystick_model.h"
#include "vintner_model.h"
#include "witness_rules.h"

/* ---- text ---------------------------------------------------------------- */

/* Append s to out at *at, bounded by cap (room for a NUL is kept); false
 * when it did not all fit. */
static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
    return !*s;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char d[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    d[k] = 0;
    do {
        d[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) d[--k] = '-';
    return put(out, cap, at, d + k);
}

static void say(tallystick_t *t, const char *s)
{
    uint32_t at = 0;
    t->notice[0] = 0;
    put(t->notice, sizeof t->notice, &at, s);
}

/* 1..max bytes, no control character: printed as it is. */
static bool text_ok(const char *s, uint32_t max)
{
    uint32_t n = 0;
    if (!s || !s[0]) return false;
    for (const unsigned char *p = (const unsigned char *) s; *p; p++, n++)
        if (*p < 0x20u || *p == 0x7fu || n >= max) return false;
    return true;
}

/* A file name in the Hoard: one segment of [A-Za-z0-9._-], not starting
 * with '.'. */
static bool file_ok(const char *s)
{
    uint32_t n = 0;
    if (!s || !s[0] || s[0] == '.') return false;
    for (; s[n]; n++) {
        char c = s[n];
        if (n >= TSTK_FILE_MAX) return false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

/* An IPP printer address, 1..TSTK_URI_MAX bytes, no control character,
 * space or quote. */
static bool uri_ok(const char *s)
{
    uint32_t n = 0;
    if (!s || (strncmp(s, "ipp://", 6) && strncmp(s, "ipps://", 7))) return false;
    for (; s[n]; n++)
        if (n >= TSTK_URI_MAX || (unsigned char) s[n] <= 0x20u || s[n] == '"' || s[n] == '\\' ||
            s[n] == 0x7f)
            return false;
    return true;
}

static void copy(char *out, uint32_t cap, const char *s)
{
    uint32_t at = 0;
    out[0] = 0;
    put(out, cap, &at, s);
}

/* ---- the register (TS2) --------------------------------------------------- */

void tallystick_init(tallystick_t *t, zxm_link_t *link)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->job = -1;
    coffer_init(&t->books, link, COFFER_DAY_CAP);
    reckoner_init(&t->stmt, link);
}

int tallystick_load(tallystick_t *t)
{
    return coffer_load(&t->books);
}

int tallystick_set_last(tallystick_t *t, int64_t last)
{
    if (t->nreg) {
        say(t, "Numbers have been given: the sequence is never restarted.");
        return ZXM_E_STATE;
    }
    if (tstk_next_number(last) == TSTK_REFUSED) return ZXM_E_ARG;
    t->start = last;
    return ZXM_OK;
}

int64_t tallystick_last(const tallystick_t *t)
{
    return t->nreg ? t->number[t->nreg - 1u] : t->start;
}

bool tallystick_register_ok(const tallystick_t *t)
{
    int64_t last = t->start;
    for (uint32_t i = 0; i < t->nreg; i++) {
        if (!witness_linked(t->number[i], t->prev[i], last)) return false; /* witness.chain_ok */
        if (t->number[i] != tstk_next_number(last)) return false;          /* no gap */
        last = t->number[i];
    }
    return true;
}

/* TS2: the next number, added to the register; 0 when refused. */
static int64_t take_number(tallystick_t *t)
{
    int64_t last = tallystick_last(t);
    int64_t next = tstk_next_number(last);
    if (next == TSTK_REFUSED) {
        say(t, "The sequence is at its last number (nine digits): nothing more can be numbered.");
        return 0;
    }
    if (t->nreg >= TSTK_REGISTER) {
        say(t, "The register is full: save it and start a new one from its last number.");
        return 0;
    }
    if (!tallystick_register_ok(t)) {
        say(t, "The register's chain is broken: nothing is numbered until it is checked.");
        return 0;
    }
    t->number[t->nreg] = next;
    t->prev[t->nreg] = last;
    t->nreg++;
    return next;
}

static int write_file(zxm_link_t *l, const char *name, const char *text, uint32_t len, bool fresh);

int tallystick_save_register(tallystick_t *t, const char *name)
{
    char text[64];
    uint32_t at = 0;
    if (!file_ok(name)) return ZXM_E_ARG;
    if (!tallystick_register_ok(t)) return ZXM_E_STATE;
    if (!put(text, sizeof text, &at, "tallystick register\nlast ") ||
        !put_num(text, sizeof text, &at, tallystick_last(t)) || !put(text, sizeof text, &at, "\n"))
        return ZXM_E_TOOBIG;
    int rc = write_file(t->link, name, text, at, false);
    if (rc != ZXM_OK) return rc;
    say(t, "Register saved through the Banker.");
    return ZXM_OK;
}

int tallystick_load_register(tallystick_t *t, const char *name)
{
    static const char head[] = "tallystick register\nlast ";
    char text[64];
    if (!file_ok(name)) return ZXM_E_ARG;
    if (t->nreg) {
        say(t, "Numbers have been given: the sequence is never restarted.");
        return ZXM_E_STATE;
    }
    zxm_begin(t->link, "files.read");
    zxm_p_str(t->link, "path", name);
    zxm_p_u64(t->link, "max", sizeof text - 1u);
    int rc = zxm_call(t->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(t->link, t->link->result, "text", text, sizeof text) != ZXM_OK ||
        strncmp(text, head, sizeof head - 1u))
        return ZXM_E_PROTOCOL;
    int64_t last = 0;
    const char *p = text + sizeof head - 1u;
    uint32_t digits = 0;
    for (; *p >= '0' && *p <= '9'; p++, digits++) {
        if (digits >= 9u) return ZXM_E_PROTOCOL;
        last = last * 10 + (*p - '0');
    }
    if (!digits || p[0] != '\n' || p[1] || (digits > 1u && text[sizeof head - 1u] == '0'))
        return ZXM_E_PROTOCOL;
    if (tstk_next_number(last) == TSTK_REFUSED) {
        say(t, "The register is at its last number (nine digits).");
        return ZXM_E_STATE;
    }
    t->start = last;
    say(t, "Register read: numbering goes on from its last number.");
    return ZXM_OK;
}

/* ---- the invoice (TS1) ------------------------------------------------------ */

int tallystick_draft(tallystick_t *t, const char *party, uint32_t customer, uint32_t payee)
{
    if (!text_ok(party, TSTK_PARTY_MAX)) return ZXM_E_ARG;
    if (!t->books.loaded) return ZXM_E_STATE;
    if (t->books.held.pending) {
        say(t, "A payment is waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    const coffer_account_t *c = coffer_account(&t->books, customer);
    const coffer_account_t *p = coffer_account(&t->books, payee);
    if (!c || !p || customer == payee) return ZXM_E_ARG;
    if (!c->issuer || p->issuer) {
        say(t, "The customer pays from an issuer account into one of your own accounts.");
        return ZXM_E_ARG;
    }
    if (strcmp(c->ccy, p->ccy)) {
        say(t, "Both accounts must be in the same currency.");
        return ZXM_E_ARG;
    }
    memset(&t->inv, 0, sizeof t->inv);
    t->inv.open = true;
    t->inv.state = TSTK_DRAFT;
    copy(t->inv.party, sizeof t->inv.party, party);
    t->inv.customer = customer;
    t->inv.payee = payee;
    memcpy(t->inv.ccy, c->ccy, sizeof t->inv.ccy);
    t->doc = TSTK_DOC_NONE;
    t->text_len = 0;
    t->text[0] = 0;
    say(t, "New invoice drafted: add its lines, then issue it.");
    return ZXM_OK;
}

int tallystick_add_line(tallystick_t *t, const char *item, int64_t qty, int64_t price, int64_t taxn,
                        int64_t taxd)
{
    tstk_invoice_t *v = &t->inv;
    if (!text_ok(item, TSTK_ITEM_MAX)) return ZXM_E_ARG;
    if (!v->open) return ZXM_E_STATE;
    if (v->state != TSTK_DRAFT) {
        say(t, "An issued invoice is never changed: draft a new one.");
        return ZXM_E_STATE;
    }
    if (v->n >= TSTK_LINES) return ZXM_E_TOOBIG;
    int64_t total = tstk_line_total(qty, price, taxn, taxd);
    if (total == TSTK_REFUSED) {
        say(t, "The line does not fit: 0 to 999999 units, a price of 0 to 9999999 minor units, "
               "a tax rate of 0 to 100%.");
        return ZXM_E_ARG;
    }
    int64_t net = vintner_sale_value(qty, price); /* vintner.sale_value */
    if (net == VINTNER_REFUSED || net != tstk_line_net(qty, price)) return ZXM_E_ARG;
    tstk_line_t *l = &v->line[v->n];
    memset(l, 0, sizeof *l);
    copy(l->item, sizeof l->item, item);
    l->qty = qty;
    l->price = price;
    l->taxn = taxn;
    l->taxd = taxd;
    l->net = net;
    l->tax = total - net;
    l->total = total;
    v->n++;
    return ZXM_OK;
}

/* The invoice or receipt as text into t->text. */
static int render(tallystick_t *t, int doc)
{
    static char text[TSTK_TEXT_MAX + 1];
    const tstk_invoice_t *v = &t->inv;
    uint32_t at = 0, cap = sizeof text;
    bool ok;
    if (doc == TSTK_DOC_INVOICE) {
        ok = put(text, cap, &at, "Invoice ") && put_num(text, cap, &at, v->number) &&
             put(text, cap, &at, "\nTo: ") && put(text, cap, &at, v->party) &&
             put(text, cap, &at, "\nAmounts in minor units of ") && put(text, cap, &at, v->ccy) &&
             put(text, cap, &at, "\n");
        for (uint32_t i = 0; ok && i < v->n; i++) {
            const tstk_line_t *l = &v->line[i];
            ok = put(text, cap, &at, l->item) && put(text, cap, &at, ": ") &&
                 put_num(text, cap, &at, l->qty) && put(text, cap, &at, " x ") &&
                 put_num(text, cap, &at, l->price) && put(text, cap, &at, " = ") &&
                 put_num(text, cap, &at, l->net) && put(text, cap, &at, ", tax ") &&
                 put_num(text, cap, &at, l->taxn) && put(text, cap, &at, "/") &&
                 put_num(text, cap, &at, l->taxd) && put(text, cap, &at, ": ") &&
                 put_num(text, cap, &at, l->tax) && put(text, cap, &at, ", line total ") &&
                 put_num(text, cap, &at, l->total) && put(text, cap, &at, "\n");
        }
        ok = ok && put(text, cap, &at, "Net: ") && put_num(text, cap, &at, v->net) &&
             put(text, cap, &at, "\nTax (each line rounded half up): ") &&
             put_num(text, cap, &at, v->tax) && put(text, cap, &at, "\nTotal due: ") &&
             put_num(text, cap, &at, v->total) && put(text, cap, &at, "\n");
    } else {
        ok = put(text, cap, &at, "Receipt ") && put_num(text, cap, &at, v->receipt) &&
             put(text, cap, &at, "\nFor invoice ") && put_num(text, cap, &at, v->number) &&
             put(text, cap, &at, "\nFrom: ") && put(text, cap, &at, v->party) &&
             put(text, cap, &at, "\nAmounts in minor units of ") && put(text, cap, &at, v->ccy) &&
             put(text, cap, &at, "\nTotal: ") && put_num(text, cap, &at, v->total) &&
             put(text, cap, &at, "\nReceived in ") && put_num(text, cap, &at, v->payments) &&
             put(text, cap, &at, v->payments == 1u ? " payment: " : " payments: ") &&
             put_num(text, cap, &at, v->paid) && put(text, cap, &at, "\nPaid in full.\n");
    }
    if (!ok) return ZXM_E_TOOBIG;
    memcpy(t->text, text, at + 1u);
    t->text_len = at;
    t->doc = doc;
    t->saved[0] = 0;
    t->job = -1;
    return ZXM_OK;
}

int tallystick_issue(tallystick_t *t)
{
    static int64_t totals[TSTK_LINES];
    tstk_invoice_t *v = &t->inv;
    if (!v->open || v->state != TSTK_DRAFT || !v->n) return ZXM_E_STATE;
    int64_t net = 0, tax = 0;
    for (uint32_t i = 0; i < v->n; i++) {
        totals[i] = v->line[i].total;
        net += v->line[i].net; /* each at most 13 digits, 32 lines: fits */
        tax += v->line[i].tax;
    }
    int64_t total = coffer_holdings(totals, v->n); /* coffer.holdings: the exact sum */
    if (total == COFFER_REFUSED || total != net + tax || total < 1 || total > TSTK_TOTAL_MAX) {
        say(t, "The invoice's total must be at least one minor unit and fit one posting.");
        return ZXM_E_ARG;
    }
    int64_t number = take_number(t);
    if (!number) return ZXM_E_STATE;
    v->number = number;
    v->net = net;
    v->tax = tax;
    v->total = total;
    v->state = TSTK_ISSUED;
    int rc = render(t, TSTK_DOC_INVOICE);
    if (rc != ZXM_OK) return rc;
    say(t, "Invoice issued: save it, print it, and record payments as they come.");
    return ZXM_OK;
}

/* ---- payments (TS3, TS4) ----------------------------------------------------- */

/* "INV-<number> P", the reference's head; then the payment's count. */
static bool ref_head(const tstk_invoice_t *v, char *out, uint32_t cap, uint32_t *at)
{
    *at = 0;
    return put(out, cap, at, "INV-") && put_num(out, cap, at, v->number) && put(out, cap, at, " P");
}

int tallystick_propose_payment(tallystick_t *t, int64_t amount)
{
    char ref[COFFER_REF_MAX + 1];
    uint32_t at = 0;
    tstk_invoice_t *v = &t->inv;
    if (v->state != TSTK_ISSUED) return ZXM_E_STATE;
    if (v->paid_full) {
        say(t, "This invoice is paid in full.");
        return ZXM_E_STATE;
    }
    if (v->payments >= TSTK_PAYMENTS) return ZXM_E_TOOBIG;
    int64_t owed = coffer_balance(v->total, -v->paid); /* coffer.balance */
    if (owed == COFFER_REFUSED || amount < 1 || amount > owed) {
        say(t, "A payment is at least one minor unit and at most what is still owed.");
        return ZXM_E_ARG;
    }
    if (!ref_head(v, ref, sizeof ref, &at) || !put_num(ref, sizeof ref, &at, v->payments + 1))
        return ZXM_E_TOOBIG;
    int rc = coffer_propose(&t->books, v->customer, v->payee, amount, ref);
    if (rc != ZXM_OK) {
        copy(t->notice, sizeof t->notice, t->books.notice);
        return rc;
    }
    say(t, "Record this payment against the invoice? Waiting for your confirmation.");
    return ZXM_OK;
}

int tallystick_cancel_payment(tallystick_t *t)
{
    int rc = coffer_cancel(&t->books);
    if (rc == ZXM_OK) say(t, "Payment cancelled; nothing was recorded.");
    return rc;
}

int tallystick_confirm_payment(tallystick_t *t, uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    if (t->inv.state != TSTK_ISSUED || !t->books.held.pending) return ZXM_E_STATE;
    int rc = coffer_confirm(&t->books, entry);
    if (rc != ZXM_OK) {
        copy(t->notice, sizeof t->notice, t->books.notice);
        return rc;
    }
    return tallystick_reconcile(t);
}

int tallystick_reconcile(tallystick_t *t)
{
    static int64_t got[COFFER_ENTRIES];
    char head[COFFER_REF_MAX + 1];
    uint32_t hn = 0, n = 0;
    tstk_invoice_t *v = &t->inv;
    if (v->state != TSTK_ISSUED) return ZXM_E_STATE;
    if (!ref_head(v, head, sizeof head, &hn)) return ZXM_E_TOOBIG;
    int rc = coffer_load(&t->books); /* balances first: entries are checked against them */
    if (rc == ZXM_OK) rc = coffer_entries(&t->books, v->payee); /* ledger.entries */
    if (rc != ZXM_OK) return rc;
    for (uint32_t i = 0; i < t->books.ne; i++) {
        const coffer_entry_t *e = &t->books.e[i];
        if (strncmp(e->ref, head, hn)) continue;
        if (e->minor <= 0 || e->counterparty != v->customer) return ZXM_E_PROTOCOL;
        got[n++] = e->minor;
    }
    int64_t paid = coffer_holdings(got, n); /* coffer.holdings */
    if (paid == COFFER_REFUSED) return ZXM_E_PROTOCOL;
    int64_t over = coffer_balance(paid, -v->total); /* coffer.balance: paid - total */
    if (over == COFFER_REFUSED) return ZXM_E_PROTOCOL;
    v->paid = paid;
    v->payments = n;
    v->paid_full = tstk_paid_in_full(v->total, over);
    say(t, v->paid_full ? "Paid in full: a receipt may be made."
                        : "Payment recorded: part of the invoice is still owed.");
    return ZXM_OK;
}

int tallystick_receipt(tallystick_t *t)
{
    tstk_invoice_t *v = &t->inv;
    if (v->state != TSTK_ISSUED || !v->paid_full) {
        say(t, "A receipt is made only for an invoice paid in full.");
        return ZXM_E_STATE;
    }
    if (!v->receipt) {
        int64_t number = take_number(t);
        if (!number) return ZXM_E_STATE;
        v->receipt = number;
    }
    int rc = render(t, TSTK_DOC_RECEIPT);
    if (rc == ZXM_OK) say(t, "Receipt made: save it and print it.");
    return rc;
}

/* ---- saved through the Banker and printed (TS7) ------------------------------ */

/* One request's text: at most this many bytes, cut on a character
 * boundary. Each byte escapes to at most six ("\u00XX"), inside the
 * request line. */
#define PIECE 2400u

static int write_file(zxm_link_t *l, const char *name, const char *text, uint32_t len, bool fresh)
{
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t n = len - at;
        if (n > PIECE) n = zxm_utf8_cut(text + at, PIECE);
        if (!n) return ZXM_E_TOOBIG;
        bool last = at + n >= len;
        zxm_begin(l, "files.write");
        zxm_p_str(l, "path", name);
        zxm_p_strn(l, "text", text + at, n);
        if (fresh) zxm_p_bool(l, "exclusive", true); /* TS7: never over an earlier one */
        if (!(first && last)) zxm_p_str(l, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(l);
        if (rc) return rc;
        at += n;
        first = false;
    } while (at < len);
    return ZXM_OK;
}

int tallystick_save(tallystick_t *t, const char *name)
{
    if (!file_ok(name)) return ZXM_E_ARG;
    if (t->doc == TSTK_DOC_NONE || !t->text_len) return ZXM_E_STATE;
    int rc = write_file(t->link, name, t->text, t->text_len, true);
    if (rc != ZXM_OK) {
        say(t, "Not saved: the Banker refused it (a document never replaces an earlier one).");
        return rc;
    }
    copy(t->saved, sizeof t->saved, name);
    t->job = -1;
    say(t, "Saved through the Banker.");
    return ZXM_OK;
}

int tallystick_print(tallystick_t *t, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    if (!t->saved[0]) return ZXM_E_STATE;
    zxm_begin(t->link, "print.submit");
    zxm_p_str(t->link, "uri", uri);
    zxm_p_str(t->link, "path", t->saved);
    zxm_p_str(t->link, "job_name", "Tallystick");
    int rc = zxm_call(t->link);
    if (rc != ZXM_OK) return rc;
    uint64_t job = 0;
    if (zxm_get_u64(t->link, t->link->result, "job_id", &job) != ZXM_OK || job > INT64_MAX)
        return ZXM_E_PROTOCOL;
    t->job = (int64_t) job;
    say(t, "Sent to the printer.");
    return ZXM_OK;
}

/* ---- statements (TS6) --------------------------------------------------------- */

int tallystick_statement(tallystick_t *t, uint32_t account, uint64_t from, uint64_t to,
                         const char *msg_id, const char *when)
{
    reckoner_t *r = &t->stmt;
    if (from > to) return ZXM_E_ARG;
    if (r->closed) {
        int rc = reckoner_reopen(r);
        if (rc != ZXM_OK) return rc;
    }
    int rc = reckoner_set_period(r, from, to);
    if (rc == ZXM_OK) rc = reckoner_close(r);
    if (rc == ZXM_OK) rc = reckoner_statement(r, account);
    if (rc == ZXM_OK) rc = reckoner_render(r, msg_id, when);
    copy(t->notice, sizeof t->notice, r->notice);
    return rc;
}
