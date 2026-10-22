/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* larder_model.c — Larder, the household budget (see larder_model.h).
 * Integers only: money is minor units, and nothing that does not fit is
 * ever rounded. */
#include <string.h>

#include "larder_model.h"

/* ---- text ---------------------------------------------------------------- */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
{
    char t[24];
    int k = 23;
    bool neg = v < 0;
    uint64_t u = neg ? (uint64_t) 0 - (uint64_t) v : (uint64_t) v;
    t[k] = 0;
    do {
        t[--k] = (char) ('0' + (int) (u % 10u));
        u /= 10u;
    } while (u && k > 1);
    if (neg) t[--k] = '-';
    put(out, cap, at, t + k);
}

static void say(larder_t *l, const char *s)
{
    uint32_t at = 0;
    l->notice[0] = 0;
    put(l->notice, sizeof l->notice, &at, s);
}

/* ---- reading zxvd's answers ------------------------------------------------ */

/* A whole decimal string with an optional '-', no leading zero, that fits
 * int64 (LA2: what is read is checked). */
static bool dec_i64(const char *s, int64_t *v)
{
    bool neg = *s == '-';
    uint64_t x = 0, lim = neg ? (uint64_t) INT64_MAX + 1u : (uint64_t) INT64_MAX;
    const char *p = s + neg;
    if (!*p || (p[0] == '0' && p[1])) return false;
    for (; *p; p++) {
        if (*p < '0' || *p > '9') return false;
        uint32_t d = (uint32_t) (*p - '0');
        if (x > (lim - d) / 10u) return false;
        x = x * 10u + d;
    }
    if (neg && !x) return false;
    *v = neg ? (x == (uint64_t) INT64_MAX + 1u ? INT64_MIN : -(int64_t) x) : (int64_t) x;
    return true;
}

static int get_dec(const zxm_link_t *k, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    if (zxm_get_str(k, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_u32(const zxm_link_t *k, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t u = 0;
    if (zxm_get_u64(k, obj, key, &u) != ZXM_OK || u > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) u;
    return ZXM_OK;
}

/* ledger.accounts: the name, currency and kind of one account; ZXM_E_ARG
 * when the books have no such account. */
static int read_account(larder_t *l, uint32_t account, char *name, char *ccy, bool *issuer)
{
    char kind[16];
    zxm_begin(l->link, "ledger.accounts");
    int rc = zxm_call(l->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l->link, l->link->result, "accounts");
    if (arr < 0 || l->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l->link, arr);
    if (n > LARDER_ACCOUNTS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l->link, arr, i);
        uint32_t id = 0;
        if (o < 0 || get_u32(l->link, o, "account", &id) != ZXM_OK) return ZXM_E_PROTOCOL;
        if (id != account) continue;
        if (zxm_get_str(l->link, o, "name", name, LARDER_NAME_MAX + 1u) != ZXM_OK ||
            zxm_get_str(l->link, o, "currency", ccy, LARDER_CCY_MAX + 1u) != ZXM_OK ||
            zxm_get_str(l->link, o, "kind", kind, sizeof kind) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (strcmp(kind, "issuer") && strcmp(kind, "holder")) return ZXM_E_PROTOCOL;
        *issuer = !strcmp(kind, "issuer");
        return ZXM_OK;
    }
    return ZXM_E_ARG;
}

/* ledger.balance of one holder account: its equity, checked against debit -
 * credit and held to 0 .. LARDER_MINOR_MAX (a holder is never below zero). */
static int read_balance(larder_t *l, uint32_t account, int64_t *eq)
{
    int64_t d = 0, cr = 0, q = 0;
    zxm_begin(l->link, "ledger.balance");
    zxm_p_u64(l->link, "account", account);
    int rc = zxm_call(l->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = l->link->result;
    if (get_dec(l->link, r, "debit_minor", &d) != ZXM_OK ||
        get_dec(l->link, r, "credit_minor", &cr) != ZXM_OK ||
        get_dec(l->link, r, "equity_minor", &q) != ZXM_OK || cr == INT64_MIN)
        return ZXM_E_PROTOCOL;
    int64_t want = coffer_balance(d, -cr);
    if (want == COFFER_REFUSED || want != q || q < 0 || q > LARDER_MINOR_MAX) return ZXM_E_PROTOCOL;
    *eq = q;
    return ZXM_OK;
}

/* ledger.entries of one account, oldest first with rising ids: the sum of
 * the whole journal, the sum of the entries after mark, and the newest id
 * (-1 when there are none). Every partial sum is held to 18 digits. */
static int read_journal(larder_t *l, uint32_t account, int64_t mark, int64_t *total, int64_t *after,
                        int64_t *newest)
{
    zxm_begin(l->link, "ledger.entries");
    zxm_p_u64(l->link, "account", account);
    int rc = zxm_call(l->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l->link, l->link->result, "entries");
    if (arr < 0 || l->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(l->link, arr);
    int64_t all = 0, since = 0, last = -1;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(l->link, arr, i);
        int64_t id = 0, minor = 0;
        if (o < 0 || get_dec(l->link, o, "entry_id", &id) != ZXM_OK || id <= last ||
            get_dec(l->link, o, "signed_minor", &minor) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        all = coffer_balance(all, minor);
        if (all == COFFER_REFUSED) return ZXM_E_PROTOCOL;
        if (id > mark) {
            since = coffer_balance(since, minor);
            if (since == COFFER_REFUSED) return ZXM_E_PROTOCOL;
        }
        last = id;
    }
    *total = all;
    *after = since;
    *newest = last;
    return ZXM_OK;
}

/* LA2: a category's balance now and its newest entry: where a month starts. */
static int snapshot(larder_t *l, uint32_t account, int64_t *opening, int64_t *mark)
{
    int64_t bal = 0, total = 0, after = 0, newest = -1;
    int rc = read_balance(l, account, &bal);
    if (rc != ZXM_OK) return rc;
    rc = read_journal(l, account, -1, &total, &after, &newest);
    if (rc != ZXM_OK) return rc;
    if (total != bal) return ZXM_E_PROTOCOL; /* the journal is the balance */
    *opening = bal;
    *mark = newest;
    return ZXM_OK;
}

/* LA2: a category's balance now and what was spent since its mark, the
 * balances' difference (LR1) checked against the month's entries. */
static int measure(larder_t *l, const larder_category_t *c, int64_t *closing, int64_t *spent)
{
    int64_t bal = 0, total = 0, after = 0, newest = -1;
    int rc = read_balance(l, c->account, &bal);
    if (rc != ZXM_OK) return rc;
    rc = read_journal(l, c->account, c->mark, &total, &after, &newest);
    if (rc != ZXM_OK) return rc;
    int64_t s = larder_category_spent(c->opening, bal);
    if (total != bal || s == LARDER_REFUSED || s != after) return ZXM_E_PROTOCOL;
    *closing = bal;
    *spent = s;
    return ZXM_OK;
}

/* ---- the totals ------------------------------------------------------------- */

/* LR2 with the excess spent - budget (coffer.balance); an excess below 18
 * digits the other way is far within the budget. */
static bool over_of(int64_t budget, int64_t spent)
{
    int64_t excess = coffer_balance(spent, -budget);
    return excess != COFFER_REFUSED && larder_category_over_budget(budget, excess);
}

/* The month's totals from what the categories hold (coffer.holdings, LR3);
 * ZXM_E_TOOBIG and nothing changed when a sum passes 64 bits. */
static int totals(larder_t *l)
{
    int64_t b[LARDER_CATEGORIES], s[LARDER_CATEGORIES];
    uint32_t over = 0;
    for (uint32_t i = 0; i < l->n; i++) {
        b[i] = l->c[i].budget;
        s[i] = l->c[i].spent;
        over += l->c[i].over ? 1u : 0u;
    }
    int64_t tb = coffer_holdings(b, l->n), ts = coffer_holdings(s, l->n);
    int64_t carry = larder_unspent_carry(b, s, l->n);
    if (tb == COFFER_REFUSED || ts == COFFER_REFUSED || carry == LARDER_REFUSED)
        return ZXM_E_TOOBIG;
    l->total_budget = tb;
    l->total_spent = ts;
    l->carry = carry;
    l->over_n = over;
    return ZXM_OK;
}

static int find(const larder_t *l, uint32_t account)
{
    for (uint32_t i = 0; i < l->n; i++)
        if (l->c[i].account == account) return (int) i;
    return -1;
}

/* LA5: every change is made on this copy, its totals checked, and only
 * then kept. */
static larder_t NEXT;

static int keep(larder_t *l)
{
    int rc = totals(&NEXT);
    if (rc != ZXM_OK) return rc;
    *l = NEXT;
    return ZXM_OK;
}

/* ---- the budget ------------------------------------------------------------- */

void larder_init(larder_t *l, zxm_link_t *link)
{
    memset(l, 0, sizeof *l);
    l->link = link;
}

const larder_category_t *larder_category(const larder_t *l, uint32_t account)
{
    int i = find(l, account);
    return i < 0 ? 0 : &l->c[i];
}

int larder_set_budget(larder_t *l, uint32_t account, int64_t base, bool owner_confirmed)
{
    if (!l) return ZXM_E_ARG;
    if (base < 0 || base > LARDER_MINOR_MAX) return ZXM_E_ARG;
    int i = find(l, account);
    if (i >= 0 && base > LARDER_MINOR_MAX - l->c[i].carried) return ZXM_E_ARG;
    if (i < 0 && l->n >= LARDER_CATEGORIES) return ZXM_E_TOOBIG;
    if (!owner_confirmed) {
        say(l, "Setting a budget waits for your confirmation; nothing was changed.");
        return ZXM_E_STATE;
    }
    larder_category_t c;
    char ccy[LARDER_CCY_MAX + 1];
    memset(&c, 0, sizeof c);
    memset(ccy, 0, sizeof ccy);
    if (i < 0) {
        bool issuer = false;
        int rc = read_account(l, account, c.name, ccy, &issuer);
        if (rc != ZXM_OK) return rc;
        if (issuer) {
            say(l, "A category is one of your own accounts, never an outside party's.");
            return ZXM_E_ARG;
        }
        if (l->ccy[0] && strcmp(l->ccy, ccy)) {
            say(l, "Every category of a budget is in one currency.");
            return ZXM_E_ARG;
        }
        c.account = account;
        c.mark = -1;
        if (l->month_open) {
            rc = snapshot(l, account, &c.opening, &c.mark);
            if (rc != ZXM_OK) return rc;
            c.closing = c.opening;
        }
    }
    NEXT = *l;
    if (i < 0) {
        NEXT.c[NEXT.n++] = c;
        if (!NEXT.ccy[0]) memcpy(NEXT.ccy, ccy, sizeof NEXT.ccy);
        i = (int) NEXT.n - 1;
    }
    larder_category_t *k = &NEXT.c[i];
    k->base = base;
    k->budget = base + k->carried;
    k->over = over_of(k->budget, k->spent);
    NEXT.fresh = false;
    int rc = keep(l);
    if (rc != ZXM_OK) return rc;
    say(l, "Budget set.");
    return ZXM_OK;
}

int larder_drop(larder_t *l, uint32_t account, bool owner_confirmed)
{
    if (!l) return ZXM_E_ARG;
    int i = find(l, account);
    if (i < 0) return ZXM_E_ARG;
    if (!owner_confirmed) {
        say(l, "Dropping a category waits for your confirmation; nothing was changed.");
        return ZXM_E_STATE;
    }
    NEXT = *l;
    memmove(&NEXT.c[i], &NEXT.c[i + 1], (NEXT.n - (uint32_t) i - 1u) * sizeof NEXT.c[0]);
    NEXT.n--;
    if (!NEXT.n) {
        NEXT.ccy[0] = 0;
        NEXT.month_open = false;
    }
    NEXT.fresh = false;
    int rc = keep(l);
    if (rc != ZXM_OK) return rc;
    say(l, "Category dropped.");
    return ZXM_OK;
}

/* Read where a month starts for every category (LA2). */
static int snapshot_all(larder_t *l, int64_t *open, int64_t *mark)
{
    for (uint32_t i = 0; i < l->n; i++) {
        int rc = snapshot(l, l->c[i].account, &open[i], &mark[i]);
        if (rc != ZXM_OK) return rc;
    }
    return ZXM_OK;
}

/* Start the month in NEXT from the balances read (open, mark). */
static void begin_in_next(const int64_t *open, const int64_t *mark)
{
    for (uint32_t i = 0; i < NEXT.n; i++) {
        larder_category_t *c = &NEXT.c[i];
        c->opening = c->closing = open[i];
        c->mark = mark[i];
        c->spent = 0;
        c->over = false;
    }
    NEXT.month_open = true;
    NEXT.fresh = true;
}

int larder_start_month(larder_t *l, bool owner_confirmed)
{
    int64_t open[LARDER_CATEGORIES], mark[LARDER_CATEGORIES];
    if (!l) return ZXM_E_ARG;
    if (!l->n) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(l, "Starting a month waits for your confirmation; nothing was changed.");
        return ZXM_E_STATE;
    }
    int rc = snapshot_all(l, open, mark);
    if (rc != ZXM_OK) return rc;
    NEXT = *l;
    begin_in_next(open, mark);
    rc = keep(l);
    if (rc != ZXM_OK) return rc;
    say(l, "A new month has started.");
    return ZXM_OK;
}

/* The notice after a refresh: each category over its budget and by how
 * much, or the month so far. Advice only: nothing is moved. */
static void say_month(larder_t *l)
{
    uint32_t at = 0, k = 0;
    char *o = l->notice;
    const uint32_t cap = sizeof l->notice;
    o[0] = 0;
    if (!l->over_n) {
        put(o, cap, &at, "Every category is within its budget: spent ");
        put_num(o, cap, &at, l->total_spent);
        put(o, cap, &at, " of ");
        put_num(o, cap, &at, l->total_budget);
        put(o, cap, &at, " minor units of ");
        put(o, cap, &at, l->ccy);
        put(o, cap, &at, ".");
        return;
    }
    put(o, cap, &at, "Over budget: ");
    for (uint32_t i = 0; i < l->n; i++) {
        const larder_category_t *c = &l->c[i];
        if (!c->over) continue;
        if (k++) put(o, cap, &at, "; ");
        put(o, cap, &at, c->name);
        put(o, cap, &at, " by ");
        put_num(o, cap, &at, c->spent - c->budget); /* LR2: 1 .. 18 digits */
    }
    put(o, cap, &at, " (minor units of ");
    put(o, cap, &at, l->ccy);
    put(o, cap, &at, "). Advice only: nothing was moved.");
}

int larder_refresh(larder_t *l)
{
    int64_t closing[LARDER_CATEGORIES], spent[LARDER_CATEGORIES];
    if (!l) return ZXM_E_ARG;
    if (!l->month_open) return ZXM_E_STATE;
    for (uint32_t i = 0; i < l->n; i++) {
        int rc = measure(l, &l->c[i], &closing[i], &spent[i]);
        if (rc != ZXM_OK) return rc;
    }
    NEXT = *l;
    for (uint32_t i = 0; i < NEXT.n; i++) {
        larder_category_t *c = &NEXT.c[i];
        c->closing = closing[i];
        c->spent = spent[i];
        c->over = over_of(c->budget, c->spent);
    }
    NEXT.fresh = true;
    int rc = keep(l);
    if (rc != ZXM_OK) return rc;
    say_month(l);
    return ZXM_OK;
}

int larder_close_month(larder_t *l, bool owner_confirmed)
{
    int64_t carried[LARDER_CATEGORIES], open[LARDER_CATEGORIES], mark[LARDER_CATEGORIES];
    if (!l) return ZXM_E_ARG;
    if (!l->month_open) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(l, "Closing the month waits for your confirmation; nothing was changed.");
        return ZXM_E_STATE;
    }
    int rc = larder_refresh(l);
    if (rc != ZXM_OK) return rc;
    for (uint32_t i = 0; i < l->n; i++) {
        const larder_category_t *c = &l->c[i];
        carried[i] = larder_unspent(c->budget, c->spent);
        if (carried[i] == LARDER_REFUSED || carried[i] > LARDER_MINOR_MAX - c->base) {
            say(l, "A budget would pass 18 digits with what it carries; the month stays open.");
            return ZXM_E_ARG;
        }
    }
    rc = snapshot_all(l, open, mark);
    if (rc != ZXM_OK) return rc;
    int64_t moved = l->carry;
    NEXT = *l;
    for (uint32_t i = 0; i < NEXT.n; i++) {
        NEXT.c[i].carried = carried[i];
        NEXT.c[i].budget = NEXT.c[i].base + carried[i];
    }
    begin_in_next(open, mark);
    rc = keep(l);
    if (rc != ZXM_OK) return rc;
    uint32_t at = 0;
    l->notice[0] = 0;
    put(l->notice, sizeof l->notice, &at, "Month closed: ");
    put_num(l->notice, sizeof l->notice, &at, moved);
    put(l->notice, sizeof l->notice, &at, " minor units of ");
    put(l->notice, sizeof l->notice, &at, l->ccy);
    put(l->notice, sizeof l->notice, &at, " left unspent are carried into next month's budgets.");
    return ZXM_OK;
}
