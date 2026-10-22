/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* neap_model.c — Neap, debit-and-credit books (see neap_model.h). Integers
 * only: money is minor units, and nothing that does not fit is ever
 * rounded. */
#include <string.h>

#include "neap_model.h"

/* ---- text ---------------------------------------------------------------- */

static bool put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
    return !*s;
}

static bool put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
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
    return put(out, cap, at, t + k);
}

static void say(neap_t *n, const char *s)
{
    uint32_t at = 0;
    n->notice[0] = 0;
    put(n->notice, sizeof n->notice, &at, s);
}

/* A whole decimal string with an optional '-', no leading zero, that fits
 * int64. */
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
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    return dec_i64(s, v) ? ZXM_OK : ZXM_E_PROTOCOL;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t u = 0;
    if (zxm_get_u64(l, obj, key, &u) != ZXM_OK || u > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) u;
    return ZXM_OK;
}

/* A file name in the Hoard: one segment of [A-Za-z0-9._-], not starting
 * with '.'. */
static bool file_ok(const char *s)
{
    uint32_t k = 0;
    if (!s || !s[0] || s[0] == '.') return false;
    for (; s[k]; k++) {
        char c = s[k];
        if (k >= NEAP_FILE_MAX) return false;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              c == '-' || c == '_' || c == '.'))
            return false;
    }
    return true;
}

/* An IPP printer address: no control character, space or quote. */
static bool uri_ok(const char *s)
{
    uint32_t k = 0;
    if (!s || (strncmp(s, "ipp://", 6) && strncmp(s, "ipps://", 7))) return false;
    for (; s[k]; k++)
        if (k >= NEAP_URI_MAX || (unsigned char) s[k] <= 0x20u || s[k] == '"' || s[k] == '\\' ||
            s[k] == 0x7f)
            return false;
    return true;
}

/* ---- the chart (NM2) ------------------------------------------------------- */

void neap_init(neap_t *n, zxm_link_t *link, int64_t day_cap)
{
    memset(n, 0, sizeof *n);
    n->link = link;
    coffer_init(&n->books, link, day_cap);
    reckoner_init(&n->rk, link);
    n->period_state = LPRES_STATE_NEITHER;
    n->job = -1;
}

static int32_t find_code(const neap_t *n, int64_t code)
{
    for (uint32_t i = 0; i < n->nn; i++)
        if (n->node[i].code == code) return (int32_t) i;
    return -1;
}

const neap_node_t *neap_node_of(const neap_t *n, uint32_t account)
{
    for (uint32_t i = 0; i < n->nn; i++)
        if (n->node[i].account == (int64_t) account) return &n->node[i];
    return 0;
}

/* A node is open when it and every node above it are (the tree is at most
 * NEAP_NODES deep, so the walk ends). */
static bool node_open(const neap_t *n, const neap_node_t *d)
{
    for (uint32_t steps = 0; d && steps <= NEAP_NODES; steps++) {
        if (d->open != 1) return false;
        if (d->parent == 0) return true;
        int32_t up = find_code(n, d->parent);
        d = up >= 0 ? &n->node[up] : 0;
    }
    return false;
}

int neap_chart_add(neap_t *n, int64_t code, int64_t parent, int64_t kind, int64_t account)
{
    if (code < 1 || code > NEAP_CODE_MAX || kind < NEAP_ASSET || kind > NEAP_EXPENSE ||
        !neap_node_ok(parent, code, kind, 1) || find_code(n, code) >= 0 ||
        (parent != 0 && find_code(n, parent) < 0) || account < -1 || account > UINT32_MAX)
        return ZXM_E_ARG;
    if (n->nn >= NEAP_NODES) return ZXM_E_TOOBIG;
    if (account >= 0) {
        const coffer_account_t *a = coffer_account(&n->books, (uint32_t) account);
        if (!n->books.loaded) return ZXM_E_STATE;
        if (!a || neap_node_of(n, (uint32_t) account)) return ZXM_E_ARG;
        if (n->ccy[0] && strcmp(n->ccy, a->ccy)) {
            say(n, "The chart keeps one currency: another is another set of books.");
            return ZXM_E_ARG;
        }
        if (!n->ccy[0]) memcpy(n->ccy, a->ccy, sizeof n->ccy);
    }
    neap_node_t *d = &n->node[n->nn++];
    d->code = code;
    d->parent = parent;
    d->kind = kind;
    d->open = 1;
    d->account = account;
    return ZXM_OK;
}

int neap_chart_close(neap_t *n, int64_t code, bool confirmed)
{
    int32_t i = find_code(n, code);
    if (!confirmed) {
        say(n, "Closing an account waits for your word.");
        return ZXM_E_STATE;
    }
    if (i < 0) return ZXM_E_ARG;
    neap_node_t *d = &n->node[i];
    if (d->account >= 0) {
        const coffer_account_t *a = coffer_account(&n->books, (uint32_t) d->account);
        if (!a || a->balance != 0) {
            say(n, "Only an account with nothing on it may be closed.");
            return ZXM_E_STATE;
        }
    }
    d->open = 0;
    say(n, "Closed: nothing posts to it until it is opened again.");
    return ZXM_OK;
}

/* ---- the books and the equation (NM5) -------------------------------------- */

int neap_load(neap_t *n)
{
    int rc = coffer_load(&n->books);
    if (rc != ZXM_OK) return rc;
    return n->ccy[0] ? neap_equation(n) : ZXM_OK;
}

int neap_equation(neap_t *n)
{
    static int64_t by[NEAP_EXPENSE + 1][COFFER_ACCOUNTS];
    uint32_t k[NEAP_EXPENSE + 1] = {0};
    int64_t s[NEAP_EXPENSE + 1] = {0};
    n->equation_ok = false;
    if (!n->books.loaded || !n->ccy[0]) return ZXM_E_STATE;
    for (uint32_t i = 0; i < n->books.n; i++) {
        const coffer_account_t *a = &n->books.a[i];
        if (strcmp(a->ccy, n->ccy)) continue; /* another set of books */
        const neap_node_t *d = neap_node_of(n, a->id);
        if (!d) {
            say(n, "An account of the books is not in the chart: A = L + E is not asked.");
            return ZXM_E_STATE;
        }
        by[d->kind][k[d->kind]++] = a->balance;
    }
    for (int t = NEAP_ASSET; t <= NEAP_EXPENSE; t++) {
        s[t] = coffer_holdings(by[t], k[t]); /* coffer.holdings: the exact sum */
        if (s[t] == COFFER_REFUSED || !neap_fits18(s[t])) return ZXM_E_TOOBIG;
    }
    /* debit - credit on each: assets on their debit side, the rest on credit */
    int64_t equity = coffer_balance(-s[NEAP_EQUITY], -s[NEAP_INCOME]);
    equity = equity == COFFER_REFUSED ? COFFER_REFUSED : coffer_balance(equity, -s[NEAP_EXPENSE]);
    if (equity == COFFER_REFUSED) return ZXM_E_TOOBIG;
    int64_t gap = neap_equation_gap(s[NEAP_ASSET], -s[NEAP_LIABILITY], equity);
    if (gap == NEAP_REFUSED) return ZXM_E_TOOBIG;
    n->assets = s[NEAP_ASSET];
    n->liabilities = -s[NEAP_LIABILITY];
    n->equity = equity;
    n->gap = gap;
    n->equation_ok = gap == 0;
    say(n,
        gap == 0 ? "A = L + E: the books balance." : "A = L + E does not hold: look at the books.");
    return gap == 0 ? ZXM_OK : ZXM_E_STATE;
}

/* ---- entries (NM3, NM4) ------------------------------------------------------ */

int neap_propose(neap_t *n, uint32_t from, uint32_t to, int64_t amount, const char *ref)
{
    if (!n->books.loaded) return ZXM_E_STATE;
    const neap_node_t *f = neap_node_of(n, from), *t = neap_node_of(n, to);
    int64_t shut = (f && node_open(n, f) ? 0 : 1) + (t && node_open(n, t) ? 0 : 1);
    int64_t gap = amount - amount; /* two legs: the credit to `from`, the debit to `to` */
    int64_t period = n->rk.closed ? 0 : 1;
    if (!neap_entry_may_post(amount, gap, shut, period)) {
        say(n, period != 1 ? "The period is closed: nothing posts to it."
               : shut != 0 ? "An account of the entry is closed or not in the chart."
                           : "The amount must be at least one minor unit and fit the books.");
        return period != 1 ? ZXM_E_STATE : ZXM_E_ARG;
    }
    int rc = coffer_propose(&n->books, from, to, amount, ref); /* coffer.transfer_ok too */
    say(n, n->books.notice);
    return rc;
}

int neap_cancel(neap_t *n)
{
    int rc = coffer_cancel(&n->books);
    say(n, n->books.notice);
    return rc;
}

int neap_confirm(neap_t *n, bool confirmed, uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    if (!n->books.held.pending) return ZXM_E_STATE;
    if (!confirmed) {
        say(n, "The entry waits for your word; nothing was posted.");
        return ZXM_E_STATE;
    }
    const neap_node_t *f = neap_node_of(n, n->books.held.from);
    const neap_node_t *t = neap_node_of(n, n->books.held.to);
    if (n->rk.closed || !f || !t || !node_open(n, f) || !node_open(n, t)) {
        say(n, "Since it was proposed the period or an account closed: nothing was posted.");
        return ZXM_E_STATE;
    }
    int rc = coffer_confirm(&n->books, entry);
    if (rc != ZXM_OK) {
        say(n, n->books.notice);
        return rc;
    }
    rc = neap_equation(n); /* NM5: after every post */
    if (rc != ZXM_OK) return rc;
    rc = coffer_check(&n->books);
    say(n, rc == ZXM_OK ? "Posted: the books hold and A = L + E."
                        : "Posted, but ledger.check fails: the books are not trusted.");
    if (rc != ZXM_OK) n->equation_ok = false;
    return rc;
}

/* ---- reconciling (NM6) and suspense (NM7) ------------------------------------- */

static void period_join(neap_t *n)
{
    lpres_state_t st = LPRES_STATE_TRUE; /* the identity of the conjunction */
    for (uint32_t i = 0; i < n->nl; i++) st = lpres_conjoin(st, n->line[i].state);
    n->period_state = n->nl ? st : LPRES_STATE_NEITHER;
}

static bool entry_matched(const neap_t *n, int64_t entry)
{
    for (uint32_t i = 0; i < n->nl; i++)
        if (n->line[i].entry == entry) return true;
    return false;
}

int neap_reconcile(neap_t *n, uint32_t account, int64_t amount, lpres_state_t *state)
{
    if (!state || amount == 0 || amount > NEAP_AMOUNT_MAX || amount < -NEAP_AMOUNT_MAX)
        return ZXM_E_ARG;
    if (!n->books.loaded || !neap_node_of(n, account)) return ZXM_E_STATE;
    if (n->nl >= NEAP_LINES) return ZXM_E_TOOBIG;
    int rc = coffer_entries(&n->books, account);
    if (rc != ZXM_OK) return rc;
    int64_t entry = -1;
    for (uint32_t i = 0; i < n->books.ne && entry < 0; i++)
        if (n->books.e[i].minor == amount && !entry_matched(n, (int64_t) n->books.e[i].id))
            entry = (int64_t) n->books.e[i].id;
    /* two sources about one line: the statement says it happened, the books
     * say whether an entry of it is there; joined, never picked */
    lpres_state_t told = LPRES_STATE_TRUE,
                  books = entry >= 0 ? LPRES_STATE_TRUE : LPRES_STATE_FALSE;
    neap_line_t *L = &n->line[n->nl++];
    L->account = account;
    L->amount = amount;
    L->entry = entry;
    L->state = lpres_kjoin(told, books);
    L->hold = 0;
    period_join(n);
    *state = L->state;
    say(n, L->state == LPRES_STATE_BOTH
               ? "The statement has a line the books do not: held, for you to book or suspend."
               : "The line matches an entry in the books.");
    return ZXM_OK;
}

/* One hold object of ledger.hold, ledger.release or ledger.holds. */
static int read_hold(const zxm_link_t *l, int32_t o, neap_hold_t *h)
{
    uint64_t id = 0;
    if (o < 0 || zxm_get_u64(l, o, "hold", &id) != ZXM_OK || id < 1 || id > INT64_MAX ||
        zxm_get_str(l, o, "state", h->state, sizeof h->state) != ZXM_OK ||
        get_u32(l, o, "from", &h->from) != ZXM_OK || get_u32(l, o, "to", &h->to) != ZXM_OK ||
        get_u32(l, o, "escrow_account", &h->escrow) != ZXM_OK ||
        get_dec(l, o, "amount_minor", &h->amount) != ZXM_OK || h->amount < 1 ||
        h->amount > NEAP_AMOUNT_MAX)
        return ZXM_E_PROTOCOL;
    if (strcmp(h->state, "held") && strcmp(h->state, "released") && strcmp(h->state, "refunded"))
        return ZXM_E_PROTOCOL;
    h->hold = (int64_t) id;
    return ZXM_OK;
}

int neap_suspend(neap_t *n, uint32_t i, uint32_t from, uint32_t to, bool confirmed)
{
    if (i >= n->nl) return ZXM_E_ARG;
    neap_line_t *L = &n->line[i];
    if (L->state != LPRES_STATE_BOTH || L->hold) return ZXM_E_STATE;
    if (!confirmed) {
        say(n, "A suspense hold waits for your word; nothing was held.");
        return ZXM_E_STATE;
    }
    const neap_node_t *f = neap_node_of(n, from), *t = neap_node_of(n, to);
    if (!f || !t || from == to || !node_open(n, f) || !node_open(n, t)) return ZXM_E_ARG;
    int64_t amount = L->amount < 0 ? -L->amount : L->amount;
    zxm_begin(n->link, "ledger.hold");
    zxm_p_u64(n->link, "from", from);
    zxm_p_u64(n->link, "to", to);
    zxm_p_i64(n->link, "amount_minor", amount);
    int rc = zxm_call(n->link);
    if (rc != ZXM_OK) {
        say(n, "zxvd refused the hold (the escrow switch may be off); nothing was held.");
        return rc;
    }
    neap_hold_t h;
    if (read_hold(n->link, n->link->result, &h) != ZXM_OK || h.amount != amount ||
        strcmp(h.state, "held") || h.from != from || h.to != to)
        return ZXM_E_PROTOCOL;
    L->hold = h.hold;
    rc = coffer_load(&n->books);
    if (rc != ZXM_OK) return rc;
    if (!neap_node_of(n, h.escrow)) { /* zxvd's escrow account is the suspense asset */
        rc = neap_chart_add(n, NEAP_SUSPENSE, 0, NEAP_ASSET, h.escrow);
        if (rc != ZXM_OK) return rc;
    }
    rc = neap_equation(n);
    if (rc == ZXM_OK)
        say(n, "On suspense: the line stays held until you release or refund it; A = L + E.");
    return rc;
}

int neap_holds(neap_t *n)
{
    static neap_hold_t got[NEAP_HOLDS];
    zxm_link_t *l = n->link;
    zxm_begin(l, "ledger.holds");
    int rc = zxm_call(l);
    if (rc != ZXM_OK) return rc;
    int32_t arr = zxm_get(l, l->result, "holds"), esc = zxm_get(l, l->result, "escrow_accounts");
    if (arr < 0 || l->tok[arr].type != W4_J_ARR || esc < 0 || l->tok[esc].type != W4_J_ARR)
        return ZXM_E_PROTOCOL;
    uint32_t nh = zxm_len(l, arr), ne = zxm_len(l, esc);
    if (nh > NEAP_HOLDS) return ZXM_E_TOOBIG;
    for (uint32_t i = 0; i < nh; i++)
        if (read_hold(l, zxm_at(l, arr, i), &got[i]) != ZXM_OK) return ZXM_E_PROTOCOL;
    bool all = true;
    for (uint32_t i = 0; i < ne; i++) {
        bool same = false;
        if (zxm_get_bool(l, zxm_at(l, esc, i), "consistent", &same) != ZXM_OK)
            return ZXM_E_PROTOCOL;
        all = all && same;
    }
    memcpy(n->hold, got, nh * sizeof got[0]);
    n->nh = nh;
    n->escrow_ok = all;
    say(n, all ? "Every suspense account matches its open holds."
               : "A suspense account does not match its open holds: look at the books.");
    return all ? ZXM_OK : ZXM_E_STATE;
}

int neap_release(neap_t *n, uint32_t i, bool confirmed)
{
    if (i >= n->nl) return ZXM_E_ARG;
    neap_line_t *L = &n->line[i];
    if (!L->hold || L->state != LPRES_STATE_BOTH) return ZXM_E_STATE;
    if (!confirmed) {
        say(n, "Releasing a hold waits for your word; nothing moved.");
        return ZXM_E_STATE;
    }
    zxm_begin(n->link, "ledger.release");
    zxm_p_u64(n->link, "hold", (uint64_t) L->hold);
    int rc = zxm_call(n->link);
    if (rc != ZXM_OK) return rc;
    neap_hold_t h;
    int64_t closed = 0;
    if (read_hold(n->link, n->link->result, &h) != ZXM_OK || h.hold != L->hold ||
        strcmp(h.state, "released") ||
        get_dec(n->link, n->link->result, "closed_entry_id", &closed) != ZXM_OK || closed < 0)
        return ZXM_E_PROTOCOL;
    /* booked now: the books tell the line too */
    L->entry = closed;
    L->state = lpres_kjoin(LPRES_STATE_TRUE, LPRES_STATE_TRUE);
    period_join(n);
    rc = coffer_load(&n->books);
    if (rc != ZXM_OK) return rc;
    return neap_equation(n);
}

/* ---- depreciation (NM8), ledger lines (NR5) and the grid (NR6) ----------------- */

int neap_schedule(neap_t *n, int64_t method, int64_t cost, int64_t salvage, int64_t life,
                  int64_t rate)
{
    static int64_t next[NEAP_LIFE_MAX];
    if ((method != NEAP_STRAIGHT && method != NEAP_DECLINING) || life < 1 || life > NEAP_LIFE_MAX)
        return ZXM_E_ARG;
    int64_t book = cost, sum = 0;
    for (int64_t p = 1; p <= life; p++) {
        int64_t c = method == NEAP_STRAIGHT ? neap_straight_charge(cost, salvage, life, p)
                                            : neap_declining_charge(book, salvage, rate, p == life);
        if (c == NEAP_REFUSED) return ZXM_E_ARG;
        next[p - 1] = c;
        book -= c;
        sum += c; /* each charge at most cost, 12 digits: no overflow */
    }
    if (sum != cost - salvage || book != salvage) return ZXM_E_PROTOCOL; /* NM8 */
    memcpy(n->dep, next, (size_t) life * sizeof next[0]);
    n->ndep = (uint32_t) life;
    n->dep_method = method;
    n->dep_cost = cost;
    n->dep_salvage = salvage;
    say(n, "Depreciation scheduled: the charges sum to cost less salvage.");
    return ZXM_OK;
}

int neap_ledger_lines(neap_t *n, uint32_t account, int64_t *out, uint32_t cap, uint32_t *count)
{
    const neap_node_t *d = neap_node_of(n, account);
    if (!out || !count) return ZXM_E_ARG;
    if (!d) return ZXM_E_STATE;
    int rc = coffer_entries(&n->books, account);
    if (rc != ZXM_OK) return rc;
    if (n->books.ne > cap) return ZXM_E_TOOBIG;
    int64_t normal = d->kind == NEAP_ASSET || d->kind == NEAP_EXPENSE ? 1 : -1, run = 0;
    for (uint32_t i = 0; i < n->books.ne; i++) {
        int64_t m = n->books.e[i].minor;
        int64_t g = neap_gl_line_amount(m > 0 ? m : 0, m < 0 ? -m : 0, normal);
        if (g == NEAP_REFUSED) return ZXM_E_PROTOCOL;
        run = coffer_balance(run, g); /* coffer.balance: refused past 18 digits */
        if (run == COFFER_REFUSED) return ZXM_E_PROTOCOL;
        out[i] = g;
    }
    const coffer_account_t *a = coffer_account(&n->books, account);
    if (!a || run != a->balance * normal) return ZXM_E_PROTOCOL; /* the lines are the balance */
    *count = n->books.ne;
    return ZXM_OK;
}

int neap_grid(const int64_t *account, const int64_t *month, const int64_t *amount, uint32_t rows,
              int64_t *out)
{
    if (!out || (rows && (!account || !month || !amount))) return ZXM_E_ARG;
    for (uint32_t i = 0; i < rows; i++) {
        out[i] = neap_grid_cell(account, month, amount, rows, i);
        if (out[i] == NEAP_REFUSED) return ZXM_E_ARG;
    }
    return ZXM_OK;
}

/* ---- reports (NM9) ------------------------------------------------------------ */

static const char *kind_name(int64_t kind)
{
    static const char *const name[] = {"", "asset", "liability", "equity", "income", "expense"};
    return kind >= NEAP_ASSET && kind <= NEAP_EXPENSE ? name[kind] : "";
}

int neap_report(neap_t *n)
{
    static char text[NEAP_TEXT_MAX + 1];
    uint32_t at = 0, cap = sizeof text;
    int rc = neap_equation(n);
    if (rc != ZXM_OK) return rc;
    uint32_t held = 0;
    for (uint32_t i = 0; i < n->nl; i++) held += n->line[i].state == LPRES_STATE_BOTH;
    bool ok = put(text, cap, &at,
                  "Neap books. Tax figures are advice, not tax advice: a person files.\n") &&
              put(text, cap, &at, "Currency: ") && put(text, cap, &at, n->ccy) &&
              put(text, cap, &at, "\nAssets: ") && put_num(text, cap, &at, n->assets) &&
              put(text, cap, &at, "\nLiabilities: ") && put_num(text, cap, &at, n->liabilities) &&
              put(text, cap, &at, "\nEquity: ") && put_num(text, cap, &at, n->equity) &&
              put(text, cap, &at, "\nA = L + E: holds\n");
    for (uint32_t i = 0; ok && i < n->nn; i++) {
        const neap_node_t *d = &n->node[i];
        const coffer_account_t *a =
            d->account >= 0 ? coffer_account(&n->books, (uint32_t) d->account) : 0;
        int64_t normal = d->kind == NEAP_ASSET || d->kind == NEAP_EXPENSE ? 1 : -1;
        ok = put(text, cap, &at, "Node ") && put_num(text, cap, &at, d->code) &&
             put(text, cap, &at, " under ") && put_num(text, cap, &at, d->parent) &&
             put(text, cap, &at, ", ") && put(text, cap, &at, kind_name(d->kind)) &&
             put(text, cap, &at, d->open == 1 ? ", open" : ", closed");
        if (ok && a)
            ok = put(text, cap, &at, ": ") && put(text, cap, &at, a->name) &&
                 put(text, cap, &at, " ") && put_num(text, cap, &at, a->balance * normal);
        ok = ok && put(text, cap, &at, "\n");
    }
    ok = ok && put(text, cap, &at, "Statement lines: ") && put_num(text, cap, &at, n->nl) &&
         put(text, cap, &at, ", held BOTH: ") && put_num(text, cap, &at, held) &&
         put(text, cap, &at, "\n");
    if (ok && n->ndep)
        ok = put(text, cap, &at,
                 n->dep_method == NEAP_STRAIGHT ? "Depreciation, straight line, of "
                                                : "Depreciation, declining balance, of ") &&
             put_num(text, cap, &at, n->dep_cost) && put(text, cap, &at, " to ") &&
             put_num(text, cap, &at, n->dep_salvage) && put(text, cap, &at, ":\n");
    for (uint32_t p = 0; ok && p < n->ndep; p++)
        ok = put(text, cap, &at, "Period ") && put_num(text, cap, &at, p + 1) &&
             put(text, cap, &at, ": ") && put_num(text, cap, &at, n->dep[p]) &&
             put(text, cap, &at, "\n");
    if (!ok) return ZXM_E_TOOBIG;
    memcpy(n->text, text, at + 1u);
    n->text_len = at;
    say(n, "Report written; it may be saved once the period is closed and the books hold.");
    return ZXM_OK;
}

/* One request's text: at most this many bytes, cut on a character
 * boundary (each byte escapes to at most six inside the request line). */
#define PIECE 2400u

int neap_save(neap_t *n, const char *name)
{
    if (!file_ok(name)) return ZXM_E_ARG;
    if (!n->text_len) return ZXM_E_STATE;
    if (!n->rk.closed) {
        say(n, "Close the period first: an open period may still change.");
        return ZXM_E_STATE;
    }
    int rc = reckoner_check(&n->rk);
    if (rc != ZXM_OK || !reckoner_allowed(&n->rk)) return rc != ZXM_OK ? rc : ZXM_E_STATE;
    uint32_t at = 0;
    bool first = true;
    do {
        uint32_t k = n->text_len - at;
        if (k > PIECE) k = zxm_utf8_cut(n->text + at, PIECE);
        if (!k) return ZXM_E_TOOBIG;
        bool last = at + k >= n->text_len;
        zxm_begin(n->link, "files.write");
        zxm_p_str(n->link, "path", name);
        zxm_p_strn(n->link, "text", n->text + at, k);
        zxm_p_bool(n->link, "exclusive", true); /* never over an earlier report */
        if (!(first && last)) zxm_p_str(n->link, "mode", first ? "begin" : last ? "end" : "add");
        rc = zxm_call(n->link);
        if (rc != ZXM_OK) {
            say(n, "Not saved: the Banker refused it (a report never replaces an earlier one).");
            return rc;
        }
        at += k;
        first = false;
    } while (at < n->text_len);
    memset(n->saved, 0, sizeof n->saved);
    memcpy(n->saved, name, strlen(name));
    n->job = -1;
    say(n, "Saved through the Banker.");
    return ZXM_OK;
}

int neap_print(neap_t *n, const char *uri)
{
    if (!uri_ok(uri)) return ZXM_E_ARG;
    if (!n->saved[0] || !reckoner_allowed(&n->rk)) return ZXM_E_STATE;
    zxm_begin(n->link, "print.submit");
    zxm_p_str(n->link, "uri", uri);
    zxm_p_str(n->link, "path", n->saved);
    zxm_p_str(n->link, "job_name", "Neap");
    int rc = zxm_call(n->link);
    if (rc != ZXM_OK) return rc;
    uint64_t job = 0;
    if (zxm_get_u64(n->link, n->link->result, "job_id", &job) != ZXM_OK || job > INT64_MAX)
        return ZXM_E_PROTOCOL;
    n->job = (int64_t) job;
    say(n, "Sent to the printer.");
    return ZXM_OK;
}
