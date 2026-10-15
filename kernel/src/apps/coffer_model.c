/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* coffer_model.c — Coffer, the wallet and the books (see coffer_model.h).
 * Integers only: money is minor units, and nothing that does not fit is
 * ever rounded. */
#include <string.h>

#include "coffer_model.h"

/* ---- the rules ---------------------------------------------------------- */

static bool fits(int64_t v)
{
    return v >= -COFFER_MINOR_MAX && v <= COFFER_MINOR_MAX;
}

int64_t coffer_balance(int64_t before, int64_t moved)
{
    if (!fits(before) || !fits(moved)) return COFFER_REFUSED;
    int64_t b = before + moved; /* both 18 digits: no 64-bit overflow */
    return fits(b) ? b : COFFER_REFUSED;
}

bool coffer_transfer_ok(int64_t amount, int64_t after, int64_t room, int64_t gap)
{
    return amount >= 1 && amount <= COFFER_AMOUNT_MAX && after >= 0 && after <= COFFER_MINOR_MAX &&
           room >= 0 && room <= COFFER_MINOR_MAX && gap == 0;
}

int64_t coffer_holdings(const int64_t *balance, uint32_t n)
{
    int64_t s = 0;
    if (!balance && n) return COFFER_REFUSED;
    for (uint32_t i = 0; i < n; i++) {
        int64_t v = balance[i];
        if ((v > 0 && s > INT64_MAX - v) || (v < 0 && s < INT64_MIN - v)) return COFFER_REFUSED;
        s += v;
    }
    return s == COFFER_REFUSED ? COFFER_REFUSED : s;
}

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

static void say(coffer_t *c, const char *s)
{
    uint32_t at = 0;
    c->notice[0] = 0;
    put(c->notice, sizeof c->notice, &at, s);
}

/* A whole decimal string with an optional '-', no leading zero, that fits
 * int64 (CF5). */
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

static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[24];
    int rc = zxm_get_str(l, obj, key, s, sizeof s);
    if (rc != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t u = 0;
    if (zxm_get_u64(l, obj, key, &u) != ZXM_OK || u > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) u;
    return ZXM_OK;
}

/* 1..max bytes, printable ASCII or UTF-8, no control characters. */
static bool text_ok(const char *s, uint32_t max)
{
    if (!s || !s[0]) return false;
    uint32_t n = 0;
    for (const unsigned char *p = (const unsigned char *) s; *p; p++, n++)
        if (*p < 0x20u || *p == 0x7fu || n >= max) return false;
    return true;
}

/* An ISO 4217 alpha code: three capital letters. */
static bool ccy_ok(const char *s)
{
    if (!s) return false;
    for (uint32_t i = 0; i < 3u; i++)
        if (s[i] < 'A' || s[i] > 'Z') return false;
    return !s[3];
}

/* ---- the books ------------------------------------------------------------ */

void coffer_init(coffer_t *c, zxm_link_t *link, int64_t day_cap)
{
    memset(c, 0, sizeof *c);
    c->link = link;
    c->e_account = -1;
    c->day_cap = day_cap >= 0 && day_cap <= COFFER_AMOUNT_MAX ? day_cap : COFFER_DAY_CAP;
}

const coffer_account_t *coffer_account(const coffer_t *c, uint32_t id)
{
    for (uint32_t i = 0; i < c->n; i++)
        if (c->a[i].id == id) return &c->a[i];
    return 0;
}

int coffer_enable(coffer_t *c, const char *code)
{
    char got[COFFER_CCY_MAX + 1];
    if (!ccy_ok(code)) return ZXM_E_ARG;
    zxm_begin(c->link, "currency.enable");
    zxm_p_str(c->link, "code", code);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_str(c->link, c->link->result, "enabled", got, sizeof got) != ZXM_OK ||
        strcmp(got, code))
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ledger.balance of one account: its equity, checked against debit - credit. */
static int read_balance(coffer_t *c, uint32_t id, int64_t *eq)
{
    int64_t d = 0, cr = 0, q = 0;
    zxm_begin(c->link, "ledger.balance");
    zxm_p_u64(c->link, "account", id);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = c->link->result;
    if (get_dec(c->link, r, "debit_minor", &d) != ZXM_OK ||
        get_dec(c->link, r, "credit_minor", &cr) != ZXM_OK ||
        get_dec(c->link, r, "equity_minor", &q) != ZXM_OK || cr == INT64_MIN)
        return ZXM_E_PROTOCOL;
    int64_t want = coffer_balance(d, -cr);
    if (want == COFFER_REFUSED || want != q) return ZXM_E_PROTOCOL;
    *eq = q;
    return ZXM_OK;
}

int coffer_load(coffer_t *c)
{
    static coffer_account_t got[COFFER_ACCOUNTS];
    char kind[16];
    zxm_begin(c->link, "ledger.accounts");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(c->link, c->link->result, "accounts");
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(c->link, arr);
    if (n > COFFER_ACCOUNTS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(c->link, arr, i);
        coffer_account_t *a = &got[i];
        if (o < 0 || get_u32(c->link, o, "account", &a->id) != ZXM_OK ||
            zxm_get_str(c->link, o, "name", a->name, sizeof a->name) != ZXM_OK ||
            zxm_get_str(c->link, o, "currency", a->ccy, sizeof a->ccy) != ZXM_OK ||
            zxm_get_str(c->link, o, "kind", kind, sizeof kind) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        if (strcmp(kind, "issuer") && strcmp(kind, "holder")) return ZXM_E_PROTOCOL;
        a->issuer = !strcmp(kind, "issuer");
    }
    /* the list is read whole before any balance is asked: the link's reply is reused */
    for (uint32_t i = 0; i < n; i++) {
        rc = read_balance(c, got[i].id, &got[i].balance);
        if (rc != ZXM_OK) return rc;
        if (!got[i].issuer && got[i].balance < 0) return ZXM_E_PROTOCOL; /* a holder never is */
    }
    memcpy(c->a, got, n * sizeof got[0]);
    c->n = n;
    c->loaded = true;
    return ZXM_OK;
}

int coffer_open_account(coffer_t *c, const char *name, const char *ccy, bool issuer, uint32_t *id)
{
    if (!text_ok(name, COFFER_NAME_MAX) || !ccy_ok(ccy) || !id) return ZXM_E_ARG;
    zxm_begin(c->link, "ledger.open_account");
    zxm_p_str(c->link, "name", name);
    zxm_p_str(c->link, "currency", ccy);
    zxm_p_str(c->link, "kind", issuer ? "issuer" : "holder");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    uint32_t got = 0;
    if (get_u32(c->link, c->link->result, "account", &got) != ZXM_OK) return ZXM_E_PROTOCOL;
    rc = coffer_load(c);
    if (rc != ZXM_OK) return rc;
    if (!coffer_account(c, got)) return ZXM_E_PROTOCOL;
    *id = got;
    return ZXM_OK;
}

int coffer_entries(coffer_t *c, uint32_t account)
{
    static coffer_entry_t got[COFFER_ENTRIES];
    const coffer_account_t *a = coffer_account(c, account);
    if (!c->loaded) return ZXM_E_STATE;
    if (!a) return ZXM_E_ARG;
    zxm_begin(c->link, "ledger.entries");
    zxm_p_u64(c->link, "account", account);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(c->link, c->link->result, "entries");
    if (arr < 0 || c->link->tok[arr].type != W4_J_ARR) return ZXM_E_PROTOCOL;
    uint32_t n = zxm_len(c->link, arr);
    if (n > COFFER_ENTRIES) return ZXM_E_TOOBIG;
    int64_t sum = 0;
    for (uint32_t i = 0; i < n; i++) {
        int32_t o = zxm_at(c->link, arr, i);
        coffer_entry_t *e = &got[i];
        int64_t id = 0;
        if (o < 0 || get_dec(c->link, o, "entry_id", &id) != ZXM_OK || id < 0 ||
            get_dec(c->link, o, "signed_minor", &e->minor) != ZXM_OK ||
            get_u32(c->link, o, "counterparty", &e->counterparty) != ZXM_OK ||
            zxm_get_str(c->link, o, "reference", e->ref, sizeof e->ref) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        e->id = (uint64_t) id;
        sum = coffer_balance(sum, e->minor); /* CF1: the running balance, refused past 18 digits */
        if (sum == COFFER_REFUSED) return ZXM_E_PROTOCOL;
    }
    if (sum != a->balance) return ZXM_E_PROTOCOL; /* CF5: the journal is the balance */
    memcpy(c->e, got, n * sizeof got[0]);
    c->ne = n;
    c->e_account = account;
    return ZXM_OK;
}

int coffer_check(coffer_t *c)
{
    bool ok = false;
    c->books_ok = false;
    zxm_begin(c->link, "ledger.check");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(c->link, c->link->result, "ok", &ok) != ZXM_OK) return ZXM_E_PROTOCOL;
    c->books_ok = ok;
    say(c, ok ? "The books hold: debits equal credits and no holder account is below zero."
              : "The books do not hold.");
    return ok ? ZXM_OK : ZXM_E_STATE;
}

int coffer_books(coffer_t *c, char *path, uint32_t cap, bool *saved)
{
    if (!path || !cap || !saved) return ZXM_E_ARG;
    path[0] = 0;
    zxm_begin(c->link, "books.file");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    if (zxm_get_bool(c->link, c->link->result, "saved", saved) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (*saved && zxm_get_str(c->link, c->link->result, "path", path, cap) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

/* ---- the day and the cap (CF4) ------------------------------------------- */

int coffer_set_day(coffer_t *c, int64_t day)
{
    if (day < 0) return ZXM_E_ARG;
    if (day != c->day) {
        c->day = day;
        c->spent = 0;
    }
    return ZXM_OK;
}

int coffer_set_cap(coffer_t *c, int64_t day_cap)
{
    if (day_cap < 0 || day_cap > COFFER_AMOUNT_MAX) return ZXM_E_ARG;
    c->day_cap = day_cap;
    return ZXM_OK;
}

/* ---- transfers (CF2, CF3) -------------------------------------------------- */

static void say_transfer(coffer_t *c, const char *lead, const coffer_account_t *f,
                         const coffer_account_t *t, const char *tail)
{
    uint32_t at = 0;
    c->notice[0] = 0;
    put(c->notice, sizeof c->notice, &at, lead);
    put_num(c->notice, sizeof c->notice, &at, c->held.amount);
    put(c->notice, sizeof c->notice, &at, " minor units of ");
    put(c->notice, sizeof c->notice, &at, f->ccy);
    put(c->notice, sizeof c->notice, &at, " from \"");
    put(c->notice, sizeof c->notice, &at, f->name);
    put(c->notice, sizeof c->notice, &at, "\" to \"");
    put(c->notice, sizeof c->notice, &at, t->name);
    put(c->notice, sizeof c->notice, &at, "\"");
    put(c->notice, sizeof c->notice, &at, tail);
}

int coffer_propose(coffer_t *c, uint32_t from, uint32_t to, int64_t amount, const char *ref)
{
    if (!c->loaded) return ZXM_E_STATE;
    if (c->held.pending) {
        say(c, "Another transfer is waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    const coffer_account_t *f = coffer_account(c, from), *t = coffer_account(c, to);
    if (!f || !t || from == to || !text_ok(ref, COFFER_REF_MAX)) return ZXM_E_ARG;
    if (strcmp(f->ccy, t->ccy)) {
        say(c, "Both accounts must be in the same currency.");
        return ZXM_E_ARG;
    }
    if (amount < 1 || amount > COFFER_AMOUNT_MAX) {
        say(c, "The amount must be at least one minor unit and fit the books.");
        return ZXM_E_ARG;
    }
    /* CF3: the legs, the paying balance after, and what is left of today's cap */
    int64_t out = -amount, in = amount;
    int64_t gap = out + in;
    int64_t after = 0, room = 0;
    if (!f->issuer) {
        after = coffer_balance(f->balance, out);
        int64_t left = coffer_balance(c->day_cap, -c->spent);
        room = left == COFFER_REFUSED ? COFFER_REFUSED : coffer_balance(left, out);
    }
    if (!coffer_transfer_ok(amount, after, room, gap)) {
        say(c, after < 0  ? "Not enough in the paying account: it may not go below zero."
               : room < 0 ? "Past today's limit: the owner's per-day cap would be passed."
                          : "The transfer does not fit the books.");
        return ZXM_E_ARG;
    }
    c->held.pending = true;
    c->held.from = from;
    c->held.to = to;
    c->held.amount = amount;
    memcpy(c->held.ccy, f->ccy, sizeof c->held.ccy);
    memset(c->held.ref, 0, sizeof c->held.ref);
    memcpy(c->held.ref, ref, strlen(ref));
    say_transfer(c, "Move ", f, t, "? Waiting for your confirmation.");
    return ZXM_OK;
}

int coffer_cancel(coffer_t *c)
{
    if (!c->held.pending) return ZXM_E_STATE;
    memset(&c->held, 0, sizeof c->held);
    say(c, "Transfer cancelled; nothing was posted.");
    return ZXM_OK;
}

int coffer_confirm(coffer_t *c, uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    if (!c->held.pending) return ZXM_E_STATE;
    const coffer_account_t *f = coffer_account(c, c->held.from);
    const coffer_account_t *t = coffer_account(c, c->held.to);
    if (!f || !t) return ZXM_E_STATE;
    bool spends = !f->issuer;
    coffer_transfer_t h = c->held;
    zxm_begin(c->link, "ledger.post");
    zxm_p_u64(c->link, "from", h.from);
    zxm_p_u64(c->link, "to", h.to);
    zxm_p_i64(c->link, "amount_minor", h.amount);
    zxm_p_str(c->link, "currency", h.ccy);
    zxm_p_str(c->link, "reference", h.ref);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) {
        /* the books refused it: nothing was posted, the transfer is dropped */
        memset(&c->held, 0, sizeof c->held);
        say(c, "The books refused the transfer; nothing was posted.");
        return rc;
    }
    int64_t id = 0;
    if (get_dec(c->link, c->link->result, "entry_id", &id) != ZXM_OK || id < 0)
        return ZXM_E_PROTOCOL;
    *entry = (uint64_t) id;
    if (spends) c->spent += h.amount; /* within day_cap: checked when proposed */
    say_transfer(c, "Posted: ", f, t, ".");
    memset(&c->held, 0, sizeof c->held);
    return coffer_load(c);
}

/* ---- holdings ------------------------------------------------------------------ */

int coffer_holdings_in(const coffer_t *c, const char *ccy, int64_t *total)
{
    static int64_t bal[COFFER_ACCOUNTS];
    uint32_t k = 0;
    if (!c->loaded) return ZXM_E_STATE;
    if (!ccy || !total) return ZXM_E_ARG;
    for (uint32_t i = 0; i < c->n; i++)
        if (!c->a[i].issuer && !strcmp(c->a[i].ccy, ccy)) bal[k++] = c->a[i].balance;
    int64_t s = coffer_holdings(bal, k);
    if (s == COFFER_REFUSED) return ZXM_E_TOOBIG;
    *total = s;
    return ZXM_OK;
}
