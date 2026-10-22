/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* mercer_model.c — Mercer, a small shop (see mercer_model.h). Integers
 * only: money is minor units, and nothing that does not fit is ever
 * rounded. Every posting, setting and saved file waits for the owner's
 * word (owner_confirmed), checked after the arguments and before anything
 * is sent. */
#include <string.h>

#include "mercer_model.h"
#include "tally_rules.h"

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

static void say(mercer_t *m, const char *s)
{
    uint32_t at = 0;
    m->notice[0] = 0;
    put(m->notice, sizeof m->notice, &at, s);
}

/* 1..max bytes, no control character. */
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
        if (n >= MERCER_FILE_MAX) return false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

/* An IPP printer address, 1..TSTK_URI_MAX bytes, no control character,
 * space or quote (as Tallystick takes it). */
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

static bool waits(mercer_t *m, bool owner_confirmed)
{
    if (owner_confirmed) return false;
    say(m, "Waiting for your word: nothing is posted, saved or sent until you confirm.");
    return true;
}

/* ---- the catalogue (MM2) ------------------------------------------------- */

void mercer_init(mercer_t *m, zxm_link_t *link)
{
    memset(m, 0, sizeof *m);
    m->link = link;
    tallystick_init(&m->till, link);
    fairground_init(&m->market, link);
    talisman_init(&m->card, link);
    m->sale.customer = -1;
    m->sale.state = LPRES_STATE_NEITHER;
}

int mercer_load(mercer_t *m)
{
    return tallystick_load(&m->till);
}

int32_t mercer_find(const mercer_t *m, int64_t code)
{
    for (uint32_t i = 0; i < m->np; i++)
        if (m->p[i].code == code) return (int32_t) i;
    return -1;
}

int32_t mercer_customer(const mercer_t *m, uint32_t account)
{
    for (uint32_t i = 0; i < m->nc; i++)
        if (m->c[i].account == account) return (int32_t) i;
    return -1;
}

/* MM2: one product worked out and checked into *p; false when it does not
 * fit (nothing else is looked at). */
static bool product_make(mercer_product_t *p, int64_t category, int64_t code, const char *name,
                         uint32_t name_len, int64_t cost, int64_t mnum, int64_t mden, int64_t stock)
{
    rat_t f;
    if (category < 1 || category > MERCER_CATEGORY_MAX || code < 1 || code > MERCER_CODE_MAX ||
        name_len < 1 || name_len > MERCER_NAME_MAX || stock < 0 || stock > MERCER_STOCK_MAX)
        return false;
    for (uint32_t i = 0; i < name_len; i++)
        if ((unsigned char) name[i] < 0x20u || name[i] == 0x7f) return false;
    int64_t price = mercer_cost_plus(cost, mnum, mden); /* MR2 */
    if (price == MERCER_REFUSED || price > TSTK_PRICE_MAX) return false;
    if (!mercer_node_ok(category, code, price, stock)) return false; /* MR5 */
    if (!tally_ratio(TALLY_ADD, 1, 1, mnum, mden, &f)) return false; /* tally.ratio_add */
    memset(p, 0, sizeof *p);
    p->category = category;
    p->code = code;
    memcpy(p->name, name, name_len);
    p->cost = cost;
    p->mnum = mnum;
    p->mden = mden;
    p->price = price;
    p->factor_n = f.num;
    p->factor_d = f.den;
    p->stock = stock;
    return true;
}

int mercer_add_product(mercer_t *m, int64_t category, int64_t code, const char *name, int64_t cost,
                       int64_t mnum, int64_t mden, int64_t stock)
{
    mercer_product_t p;
    if (!text_ok(name, MERCER_NAME_MAX) ||
        !product_make(&p, category, code, name, (uint32_t) strlen(name), cost, mnum, mden, stock)) {
        say(m, "The product does not fit: category 1..9999, code 1..999999, a cost of 0 to "
               "999999999, a margin of 0..99999 over 1..99999, a price of at most 9999999.");
        return ZXM_E_ARG;
    }
    if (mercer_find(m, code) >= 0) {
        say(m, "A product already has this code.");
        return ZXM_E_ARG;
    }
    if (m->np >= MERCER_PRODUCTS) return ZXM_E_TOOBIG;
    m->p[m->np++] = p;
    say(m, "Product added to the catalogue.");
    return ZXM_OK;
}

/* ---- the till and credit (MM3) ------------------------------------------- */

int mercer_set_till(mercer_t *m, uint32_t counter, uint32_t payee, int64_t taxn, int64_t taxd,
                    bool owner_confirmed)
{
    if (tstk_line_total(1, 1, taxn, taxd) == TSTK_REFUSED) return ZXM_E_ARG;
    if (!m->till.books.loaded) return ZXM_E_STATE;
    const coffer_account_t *c = coffer_account(&m->till.books, counter);
    const coffer_account_t *p = coffer_account(&m->till.books, payee);
    if (!c || !p || counter == payee || !c->issuer || p->issuer || strcmp(c->ccy, p->ccy)) {
        say(m, "The counter is an issuer account and the till pays into one of your own, in one "
               "currency.");
        return ZXM_E_ARG;
    }
    if (m->sale.open) {
        say(m, "A sale is open: post it or void it first.");
        return ZXM_E_STATE;
    }
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    m->counter = counter;
    m->payee = payee;
    m->taxn = taxn;
    m->taxd = taxd;
    m->till_set = true;
    say(m, "Till set.");
    return ZXM_OK;
}

int mercer_set_credit(mercer_t *m, uint32_t account, int64_t limit, bool owner_confirmed)
{
    if (limit < 0 || limit > MERCER_AMOUNT_MAX) return ZXM_E_ARG;
    if (!m->till.books.loaded) return ZXM_E_STATE;
    const coffer_account_t *a = coffer_account(&m->till.books, account);
    if (!a || !a->issuer) {
        say(m, "A customer's credit is kept against their issuer account.");
        return ZXM_E_ARG;
    }
    int32_t k = mercer_customer(m, account);
    if (k >= 0 && limit < m->c[k].owed) {
        say(m, "The limit is below what the customer already owes.");
        return ZXM_E_ARG;
    }
    if (k < 0 && m->nc >= MERCER_CUSTOMERS) return ZXM_E_TOOBIG;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    if (k < 0) {
        k = (int32_t) m->nc++;
        m->c[k].account = account;
        m->c[k].owed = 0;
    }
    m->c[k].limit = limit;
    say(m, "Credit limit set as the customer declared it.");
    return ZXM_OK;
}

/* MR3 for one customer and a sale of amount: room = limit - owed - amount
 * (coffer.balance, twice). */
static bool credit_allows(const mercer_customer_t *c, int64_t amount)
{
    int64_t left = coffer_balance(c->limit, -c->owed);
    if (left == COFFER_REFUSED) return false;
    int64_t room = coffer_balance(left, -amount);
    return room != COFFER_REFUSED && mercer_credit_ok(amount, c->limit, room);
}

/* ---- a sale (MM4, MM5) ----------------------------------------------------- */

int mercer_ring(mercer_t *m, int64_t code, int64_t qty, uint32_t on_credit)
{
    int32_t i = mercer_find(m, code);
    if (i < 0) return ZXM_E_ARG;
    mercer_product_t *p = &m->p[i];
    if (mercer_stock_after(p->stock, qty) == MERCER_REFUSED) { /* MR1 */
        say(m, "A sale is at least one unit and never more than the stock.");
        return ZXM_E_ARG;
    }
    if (!m->till_set) return ZXM_E_STATE;
    int64_t amount = tstk_line_total(qty, p->price, m->taxn, m->taxd); /* taxed_line_total */
    if (amount == TSTK_REFUSED || amount < 1 || amount > TSTK_TOTAL_MAX) {
        say(m, "The sale's amount must be at least one minor unit and fit one posting.");
        return ZXM_E_ARG;
    }
    int32_t k = -1;
    if (on_credit) {
        k = mercer_customer(m, on_credit);
        if (k < 0) {
            say(m, "The customer has declared no credit limit.");
            return ZXM_E_ARG;
        }
        if (!credit_allows(&m->c[k], amount)) {
            say(m, "Not on credit: the customer's tab and this sale pass their declared limit.");
            return ZXM_E_ARG;
        }
    }
    if (m->sale.open) {
        say(m, "A sale is open: post it or void it first.");
        return ZXM_E_STATE;
    }
    tallystick_t *t = &m->till;
    int rc = tallystick_draft(t, k < 0 ? "Counter sale" : "Sale on credit",
                              k < 0 ? m->counter : on_credit, m->payee);
    if (rc == ZXM_OK) rc = tallystick_add_line(t, p->name, qty, p->price, m->taxn, m->taxd);
    if (rc == ZXM_OK) rc = tallystick_issue(t);
    if (rc != ZXM_OK) {
        memcpy(m->notice, t->notice, sizeof m->notice);
        return rc;
    }
    if (t->inv.total != amount) return ZXM_E_PROTOCOL;
    memset(&m->sale, 0, sizeof m->sale);
    m->sale.open = true;
    m->sale.number = t->inv.number;
    m->sale.product = (uint32_t) i;
    m->sale.qty = qty;
    m->sale.amount = amount;
    m->sale.customer = k;
    m->sale.state = mercer_sale_state(false, qty, amount, 0, 0); /* MR6: the till alone */
    say(m, "Sale rung up: check it against the buyer's receipt, then post it.");
    return ZXM_OK;
}

int mercer_receipt(mercer_t *m, int64_t rqty, int64_t ramount)
{
    char s[MERCER_NOTICE_MAX + 1];
    uint32_t at = 0;
    if (rqty < 0 || ramount < 0) return ZXM_E_ARG;
    if (!m->sale.open) return ZXM_E_STATE;
    mercer_sale_t *v = &m->sale;
    v->have_receipt = true;
    v->rqty = rqty;
    v->ramount = ramount;
    v->ruled = false;
    v->state = mercer_sale_state(true, v->qty, v->amount, rqty, ramount);
    if (v->state != LPRES_STATE_BOTH) {
        say(m, "The receipt agrees with the till.");
        return ZXM_OK;
    }
    if (put(s, sizeof s, &at, "Sale ") && put_num(s, sizeof s, &at, v->number) &&
        put(s, sizeof s, &at, ": the till says ") && put_num(s, sizeof s, &at, v->qty) &&
        put(s, sizeof s, &at, " units for ") && put_num(s, sizeof s, &at, v->amount) &&
        put(s, sizeof s, &at, ", the receipt ") && put_num(s, sizeof s, &at, rqty) &&
        put(s, sizeof s, &at, " for ") && put_num(s, sizeof s, &at, ramount) &&
        put(s, sizeof s, &at, ". Held: nothing is posted until you rule."))
        say(m, s);
    else
        say(m, "The till and the receipt disagree. Held: nothing is posted until you rule.");
    return ZXM_OK;
}

int mercer_rule(mercer_t *m, bool till_stands, bool owner_confirmed)
{
    if (!m->sale.open || m->sale.state != LPRES_STATE_BOTH) return ZXM_E_STATE;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    if (till_stands) {
        m->sale.state = LPRES_STATE_TRUE;
        m->sale.ruled = true;
        say(m, "Ruled: the till's record stands and may be posted.");
        return ZXM_OK;
    }
    m->sale.state = LPRES_STATE_FALSE;
    m->sale.open = false;
    say(m, "Voided: nothing was posted. Ring the sale again as it was.");
    return ZXM_OK;
}

/* ---- posting (MM6) ----------------------------------------------------------- */

int mercer_post(mercer_t *m, bool owner_confirmed, uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    mercer_sale_t *v = &m->sale;
    if (!v->open) return ZXM_E_STATE;
    if (v->state != LPRES_STATE_TRUE) {
        say(m, "Held: the till and the receipt disagree; nothing is posted until you rule.");
        return ZXM_E_STATE;
    }
    mercer_product_t *p = &m->p[v->product];
    int64_t after = mercer_stock_after(p->stock, v->qty); /* MR1, checked again */
    if (after == MERCER_REFUSED) return ZXM_E_STATE;
    if (v->customer >= 0 && !credit_allows(&m->c[v->customer], v->amount)) return ZXM_E_STATE;
    if (m->nday >= MERCER_DAY_SALES) {
        say(m, "The day's sales are full: close the day before the next sale.");
        return ZXM_E_TOOBIG;
    }
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    *entry = 0;
    if (v->customer >= 0) { /* on the tab: no money moves */
        mercer_customer_t *c = &m->c[v->customer];
        int64_t owed = coffer_balance(c->owed, v->amount);
        if (owed == COFFER_REFUSED) return ZXM_E_STATE;
        c->owed = owed;
    } else {
        tallystick_t *t = &m->till;
        int rc = tallystick_propose_payment(t, v->amount);
        if (rc == ZXM_OK) {
            rc = tallystick_confirm_payment(t, entry);
            if (rc != ZXM_OK && t->books.held.pending) tallystick_cancel_payment(t);
        }
        if (rc == ZXM_OK && !t->inv.paid_full) rc = ZXM_E_PROTOCOL; /* paid_in_full */
        if (rc == ZXM_OK) rc = tallystick_receipt(t);               /* numbered: next_doc_number */
        if (rc != ZXM_OK) {
            memcpy(m->notice, t->notice, sizeof m->notice);
            return rc;
        }
    }
    p->stock = after;
    m->day_amount[m->nday] = v->amount;
    m->day_qty[m->nday] = v->qty;
    m->nday++;
    v->open = false;
    say(m, v->customer >= 0 ? "Sale on credit recorded on the customer's tab."
                            : "Sale posted to the books; its receipt is numbered.");
    return ZXM_OK;
}

int mercer_credit_payment(mercer_t *m, uint32_t account, int64_t amount, bool owner_confirmed,
                          uint64_t *entry)
{
    char ref[COFFER_REF_MAX + 1];
    uint32_t at = 0;
    if (!entry) return ZXM_E_ARG;
    int32_t k = mercer_customer(m, account);
    if (k < 0) return ZXM_E_ARG;
    mercer_customer_t *c = &m->c[k];
    if (amount < 1 || amount > c->owed) {
        say(m, "A payment is at least one minor unit and at most the tab.");
        return ZXM_E_ARG;
    }
    if (!m->till_set) return ZXM_E_STATE;
    if (!put(ref, sizeof ref, &at, "TAB-") || !put_num(ref, sizeof ref, &at, (int64_t) account))
        return ZXM_E_TOOBIG;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    int rc = coffer_propose(&m->till.books, account, m->payee, amount, ref);
    if (rc == ZXM_OK) {
        rc = coffer_confirm(&m->till.books, entry);
        if (rc != ZXM_OK && m->till.books.held.pending) coffer_cancel(&m->till.books);
    }
    if (rc != ZXM_OK) {
        memcpy(m->notice, m->till.books.notice, sizeof m->notice);
        return rc;
    }
    c->owed -= amount; /* 1..owed: never below 0 */
    say(m, "Payment against the tab posted to the books.");
    return ZXM_OK;
}

/* ---- saved and printed (MM7) ---------------------------------------------------- */

int mercer_save_receipt(mercer_t *m, const char *name, bool owner_confirmed)
{
    if (!file_ok(name)) return ZXM_E_ARG;
    if (m->till.doc != TSTK_DOC_RECEIPT) return ZXM_E_STATE;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    int rc = tallystick_save(&m->till, name);
    memcpy(m->notice, m->till.notice, sizeof m->notice);
    return rc;
}

int mercer_print_receipt(mercer_t *m, const char *uri, bool owner_confirmed)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    if (m->till.doc != TSTK_DOC_RECEIPT || !m->till.saved[0]) return ZXM_E_STATE;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    int rc = tallystick_print(&m->till, uri);
    memcpy(m->notice, m->till.notice, sizeof m->notice);
    return rc;
}

static const char CAT_HEAD[] = "mercer catalogue\n";

/* One request's text: at most this many bytes. Each byte escapes to at
 * most six ("\u00XX") inside the request line. */
#define PIECE 2400u

int mercer_save_catalogue(mercer_t *m, const char *name, bool owner_confirmed)
{
    static char text[MERCER_CAT_MAX + 1];
    uint32_t at = 0, cap = sizeof text;
    if (!file_ok(name)) return ZXM_E_ARG;
    bool ok = put(text, cap, &at, CAT_HEAD);
    for (uint32_t i = 0; ok && i < m->np; i++) {
        const mercer_product_t *p = &m->p[i];
        ok = put_num(text, cap, &at, p->category) && put(text, cap, &at, " ") &&
             put_num(text, cap, &at, p->code) && put(text, cap, &at, " ") &&
             put_num(text, cap, &at, p->cost) && put(text, cap, &at, " ") &&
             put_num(text, cap, &at, p->mnum) && put(text, cap, &at, " ") &&
             put_num(text, cap, &at, p->mden) && put(text, cap, &at, " ") &&
             put_num(text, cap, &at, p->stock) && put(text, cap, &at, " ") &&
             put(text, cap, &at, p->name) && put(text, cap, &at, "\n");
    }
    if (!ok) return ZXM_E_TOOBIG;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    uint32_t done = 0;
    bool first = true;
    do { /* replaced as a whole, in parts when long */
        uint32_t n = at - done > PIECE ? PIECE : at - done;
        bool last = done + n >= at;
        zxm_begin(m->link, "files.write");
        zxm_p_str(m->link, "path", name);
        zxm_p_strn(m->link, "text", text + done, n);
        if (!(first && last)) zxm_p_str(m->link, "mode", first ? "begin" : last ? "end" : "add");
        int rc = zxm_call(m->link);
        if (rc != ZXM_OK) return rc;
        done += n;
        first = false;
    } while (done < at);
    say(m, "Catalogue saved in the Hoard.");
    return ZXM_OK;
}

/* A decimal of 1..18 digits at *s, no leading zero; *s moves past it. */
static bool read_dec(const char **s, int64_t *v)
{
    const char *p = *s;
    int64_t x = 0;
    uint32_t n = 0;
    for (; *p >= '0' && *p <= '9'; p++, n++) {
        if (n >= 18u) return false;
        x = x * 10 + (*p - '0');
    }
    if (!n || (n > 1u && **s == '0')) return false;
    *s = p;
    *v = x;
    return true;
}

int mercer_load_catalogue(mercer_t *m, const char *name)
{
    static char text[MERCER_CAT_MAX + 1];
    static mercer_product_t got[MERCER_PRODUCTS];
    uint32_t n = 0;
    bool eof = false;
    if (!file_ok(name)) return ZXM_E_ARG;
    if (m->sale.open) {
        say(m, "A sale is open: post it or void it first.");
        return ZXM_E_STATE;
    }
    zxm_begin(m->link, "files.read");
    zxm_p_str(m->link, "path", name);
    zxm_p_u64(m->link, "max", MERCER_CAT_MAX);
    int rc = zxm_call(m->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(m->link, m->link->result, "text", text, sizeof text) != ZXM_OK ||
        zxm_get_bool(m->link, m->link->result, "eof", &eof) != ZXM_OK || !eof ||
        strncmp(text, CAT_HEAD, sizeof CAT_HEAD - 1u))
        return ZXM_E_PROTOCOL;
    const char *s = text + sizeof CAT_HEAD - 1u;
    while (*s) {
        int64_t f[6];
        for (uint32_t k = 0; k < 6u; k++)
            if (!read_dec(&s, &f[k]) || *s++ != ' ') return ZXM_E_PROTOCOL;
        const char *e = strchr(s, '\n');
        if (!e || n >= MERCER_PRODUCTS ||
            !product_make(&got[n], f[0], f[1], s, (uint32_t) (e - s), f[2], f[3], f[4], f[5]))
            return ZXM_E_PROTOCOL;
        for (uint32_t j = 0; j < n; j++)
            if (got[j].code == got[n].code) return ZXM_E_PROTOCOL;
        n++;
        s = e + 1;
    }
    memcpy(m->p, got, n * sizeof got[0]);
    m->np = n;
    say(m, "Catalogue read from the Hoard.");
    return ZXM_OK;
}

/* ---- listings (MM8) --------------------------------------------------------------- */

int mercer_set_market_limits(mercer_t *m, int64_t ceiling, int64_t floor_price, int64_t most_units,
                             bool owner_confirmed)
{
    if (ceiling < 1 || ceiling > FAIRGROUND_NINE || floor_price < 1 ||
        floor_price > FAIRGROUND_NINE || most_units < 1 || most_units > FAIRGROUND_NINE)
        return ZXM_E_ARG;
    if (waits(m, owner_confirmed)) return ZXM_E_STATE;
    return fairground_set_limits(&m->market, ceiling, floor_price, most_units);
}

int mercer_read_offers(mercer_t *m)
{
    return fairground_read_offers(&m->market);
}

int mercer_read_card(mercer_t *m, uint32_t day)
{
    return talisman_refresh(&m->card, day);
}

int mercer_list(mercer_t *m, int64_t code, int64_t rnum, int64_t rden)
{
    char s[MERCER_LISTING_MAX + 1];
    uint32_t at = 0, cap = sizeof s;
    int32_t i = mercer_find(m, code);
    if (i < 0) return ZXM_E_ARG;
    const mercer_product_t *p = &m->p[i];
    bool fx = rnum || rden;
    int64_t other = fx ? tally_fx_amount(p->price, rnum, rden) : 0; /* tally.fx_amount */
    if (other == TALLY_REFUSED) return ZXM_E_ARG;
    const fairground_t *f = &m->market;
    if (!f->most_units) {
        say(m, "Set your market limits before listing.");
        return ZXM_E_STATE;
    }
    if (!fairground_offer_allowed(p->price, p->stock, p->price - f->floor_price,
                                  f->most_units - p->stock)) { /* fairground.offer_allowed */
        say(m, "Not listed: the price is under your floor, the stock is out or past your most "
               "units.");
        return ZXM_E_STATE;
    }
    int64_t fee = fairground_assurance_fee(p->price); /* fairground.assurance_fee */
    bool ok = put(s, cap, &at, p->name) && put(s, cap, &at, ": ") &&
              put_num(s, cap, &at, p->price) && put(s, cap, &at, " a unit (cost x ") &&
              put_num(s, cap, &at, p->factor_n) && put(s, cap, &at, "/") &&
              put_num(s, cap, &at, p->factor_d) && put(s, cap, &at, "), ") &&
              put_num(s, cap, &at, p->stock) && put(s, cap, &at, " in stock. Assurance fee ") &&
              put_num(s, cap, &at, fee) && put(s, cap, &at, ".");
    if (ok && fx)
        ok = put(s, cap, &at, " At ") && put_num(s, cap, &at, rnum) && put(s, cap, &at, "/") &&
             put_num(s, cap, &at, rden) && put(s, cap, &at, ": ") && put_num(s, cap, &at, other) &&
             put(s, cap, &at, ".");
    if (ok && f->offers_known)
        ok = put(s, cap, &at, " Market: ") && put_num(s, cap, &at, f->nwant) &&
             put(s, cap, &at, " wants, ") && put_num(s, cap, &at, f->nout) &&
             put(s, cap, &at, " offers outstanding.");
    if (ok && m->card.have) {
        const talisman_next_t *nx = &m->card.next;
        bool earned =
            nx->have && talisman_earned(nx->margin[TALISMAN_CRED], nx->margin[TALISMAN_BOND],
                                        nx->margin[TALISMAN_UPTIME]); /* talisman.earned */
        ok = put(s, cap, &at, " Seller: ") && put(s, cap, &at, m->card.metal) &&
             put(s, cap, &at, " / ") && put(s, cap, &at, m->card.stone) &&
             put(s, cap, &at,
                 !nx->have ? ", the top tier."
                 : earned  ? ", next tier earned."
                           : ", next tier not earned yet.");
    }
    if (ok) ok = put(s, cap, &at, " Tiers give no priority, discount or credit.");
    if (!ok) return ZXM_E_TOOBIG;
    memcpy(m->listing, s, at + 1u);
    say(m, "Listing shown; nothing is sent to the market.");
    return ZXM_OK;
}

/* ---- the day's sales (MM9) -------------------------------------------------------- */

int mercer_unit_price(const mercer_t *m, int64_t *num, int64_t *den)
{
    if (!num || !den) return ZXM_E_ARG;
    if (!m->nday) return ZXM_E_STATE;
    /* fairground.offer_unit_price: the sum of the amounts over the sum of the units */
    return fairground_unit_price(m->day_amount, m->day_qty, m->nday, num, den) ? ZXM_OK
                                                                               : ZXM_E_STATE;
}

int mercer_stocktake(const mercer_t *m, const int64_t *product, const int64_t *place,
                     const int64_t *qty, uint32_t n, int64_t *cell, uint32_t *differ)
{
    if (!product || !place || !qty || !cell || !differ || !n || n > MERCER_COUNT_ROWS)
        return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++)
        if (mercer_find(m, product[i]) < 0 || place[i] < 1 || place[i] > MERCER_CATEGORY_MAX ||
            qty[i] < 0 || qty[i] > MERCER_STOCK_MAX)
            return ZXM_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        cell[i] = mercer_grid_cell(product, place, qty, n, i); /* MR4 */
        if (cell[i] == MERCER_REFUSED) return ZXM_E_ARG;
    }
    uint32_t d = 0;
    for (uint32_t k = 0; k < m->np; k++) {
        int64_t counted = 0; /* each row at most 9 digits, n rows: fits */
        for (uint32_t i = 0; i < n; i++)
            if (product[i] == m->p[k].code) counted += qty[i];
        d += counted != m->p[k].stock;
    }
    *differ = d;
    return ZXM_OK;
}
