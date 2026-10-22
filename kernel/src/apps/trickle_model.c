/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* trickle_model.c — Trickle, streaming payments and tithes (see
 * trickle_model.h). Integers only: money is minor units, and nothing that
 * does not fit is ever rounded. */
#include <string.h>

#include "trickle_model.h"

/* ---- text ---------------------------------------------------------------- */

/* Append s to out at *at, bounded by cap (room for a NUL is kept). */
static void put(char *out, uint32_t cap, uint32_t *at, const char *s)
{
    while (*s && *at + 1u < cap) out[(*at)++] = *s++;
    out[*at] = 0;
}

static void put_num(char *out, uint32_t cap, uint32_t *at, int64_t v)
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
    put(out, cap, at, d + k);
}

static void say(trickle_t *t, const char *s)
{
    uint32_t at = 0;
    t->notice[0] = 0;
    put(t->notice, sizeof t->notice, &at, s);
}

/* "<a><n><b>" as the notice. */
static void say_n(trickle_t *t, const char *a, int64_t n, const char *b)
{
    uint32_t at = 0;
    t->notice[0] = 0;
    put(t->notice, sizeof t->notice, &at, a);
    put_num(t->notice, sizeof t->notice, &at, n);
    put(t->notice, sizeof t->notice, &at, b);
}

/* "TRICKLE <kind><id> <step><n>" into ref (COFFER_REF_MAX + 1 bytes). */
static void make_ref(char *ref, char kind, uint32_t id, char step, int64_t n)
{
    uint32_t at = 0;
    char k[3] = {kind, 0, 0};
    ref[0] = 0;
    put(ref, COFFER_REF_MAX + 1u, &at, "TRICKLE ");
    put(ref, COFFER_REF_MAX + 1u, &at, k);
    put_num(ref, COFFER_REF_MAX + 1u, &at, (int64_t) id);
    k[0] = ' ';
    k[1] = step;
    put(ref, COFFER_REF_MAX + 1u, &at, k);
    put_num(ref, COFFER_REF_MAX + 1u, &at, n);
}

/* ---- the books ------------------------------------------------------------ */

void trickle_init(trickle_t *t, zxm_link_t *link, int64_t day_cap)
{
    memset(t, 0, sizeof *t);
    t->link = link;
    t->next_id = 1;
    coffer_init(&t->books, link, day_cap);
}

int trickle_load(trickle_t *t)
{
    return coffer_load(&t->books);
}

int trickle_set_day(trickle_t *t, int64_t day)
{
    return coffer_set_day(&t->books, day);
}

int trickle_set_cap(trickle_t *t, int64_t day_cap, bool owner_confirmed)
{
    if (day_cap < 0 || day_cap > TRK_AMOUNT_MAX) return ZXM_E_ARG;
    if (!owner_confirmed) {
        say_n(t, "Set the daily cap to ", day_cap, "? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    int rc = coffer_set_cap(&t->books, day_cap);
    if (rc == ZXM_OK) say_n(t, "Daily cap set to ", day_cap, ".");
    return rc;
}

/* A payer and a payee for a stream or tithe: both read, distinct, in one
 * currency, and the payer the owner's own holder account. */
static int pair_ok(trickle_t *t, uint32_t from, uint32_t to)
{
    const coffer_account_t *f = coffer_account(&t->books, from);
    const coffer_account_t *p = coffer_account(&t->books, to);
    if (!f || !p || from == to) return ZXM_E_ARG;
    if (f->issuer) {
        say(t, "A stream or tithe is paid from your own account.");
        return ZXM_E_ARG;
    }
    if (strcmp(f->ccy, p->ccy)) {
        say(t, "Both accounts must be in the same currency.");
        return ZXM_E_ARG;
    }
    return ZXM_OK;
}

/* A posting's id as zxvd writes it: a whole decimal string, no leading
 * zero, below 2^63. */
static int get_id(const zxm_link_t *l, int32_t obj, uint64_t *id)
{
    char s[24];
    if (zxm_get_str(l, obj, "entry_id", s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    if (!s[0] || (s[0] == '0' && s[1])) return ZXM_E_PROTOCOL;
    uint64_t v = 0;
    for (const char *p = s; *p; p++) {
        if (*p < '0' || *p > '9') return ZXM_E_PROTOCOL;
        uint64_t d = (uint64_t) (*p - '0');
        if (v > ((uint64_t) INT64_MAX - d) / 10u) return ZXM_E_PROTOCOL;
        v = v * 10u + d;
    }
    *id = v;
    return ZXM_OK;
}

/* Post amount from -> to with ref (ledger.post), the owner having
 * confirmed (TQ2): Coffer's transfer rule first (coffer.transfer_ok, the
 * balance after it and what is left of today's cap), then the post, then
 * the caller reads the books again once it has kept what was posted.
 * Nothing is sent when the rule says no. */
static int post(trickle_t *t, uint32_t from, uint32_t to, int64_t amount, const char *ref,
                uint64_t *entry)
{
    const coffer_account_t *f = coffer_account(&t->books, from);
    if (!f || !coffer_account(&t->books, to)) return ZXM_E_STATE;
    int64_t after = coffer_balance(f->balance, -amount);
    int64_t cap_left = coffer_balance(t->books.day_cap, -t->books.spent);
    int64_t room = cap_left == COFFER_REFUSED ? COFFER_REFUSED : coffer_balance(cap_left, -amount);
    if (!coffer_transfer_ok(amount, after, room, 0)) {
        say(t, after < 0  ? "Not enough in the paying account: it may not go below zero."
               : room < 0 ? "Past today's cap: the owner's per-day cap would be passed."
                          : "The payment does not fit the books.");
        return ZXM_E_ARG;
    }
    char ccy[COFFER_CCY_MAX + 1];
    memcpy(ccy, f->ccy, sizeof ccy);
    zxm_begin(t->link, "ledger.post");
    zxm_p_u64(t->link, "from", from);
    zxm_p_u64(t->link, "to", to);
    zxm_p_i64(t->link, "amount_minor", amount);
    zxm_p_str(t->link, "currency", ccy);
    zxm_p_str(t->link, "reference", ref);
    int rc = zxm_call(t->link);
    if (rc != ZXM_OK) {
        say(t, "The books refused the payment; nothing was posted.");
        return rc;
    }
    rc = get_id(t->link, t->link->result, entry);
    if (rc != ZXM_OK) return rc;
    t->books.spent += amount; /* within day_cap: room was checked */
    return ZXM_OK;
}

/* ---- streams -------------------------------------------------------------- */

static trk_stream_t *stream_at(trickle_t *t, uint32_t slot)
{
    if (slot >= TRK_STREAMS || !t->s[slot].used) return NULL;
    return &t->s[slot];
}

int trickle_stream_start(trickle_t *t, uint32_t from, uint32_t to, int64_t total, int64_t ticks,
                         bool owner_confirmed, uint32_t *slot)
{
    if (!slot) return ZXM_E_ARG;
    if (!t->books.loaded) return ZXM_E_STATE;
    int rc = pair_ok(t, from, to);
    if (rc != ZXM_OK) return rc;
    if (total < 1 || total > TRK_TOTAL_MAX || ticks < 1 || ticks > TRK_TICKS_MAX) {
        say(t, "A stream pays 1 to 999999999999 minor units over 1 to 99999 periods.");
        return ZXM_E_ARG;
    }
    uint32_t k = 0;
    while (k < TRK_STREAMS && t->s[k].used) k++;
    if (k == TRK_STREAMS) {
        say(t, "Every stream place is taken.");
        return ZXM_E_TOOBIG;
    }
    if (!owner_confirmed) {
        say_n(t, "Start a stream of ", total, "? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    trk_stream_t *s = &t->s[k];
    memset(s, 0, sizeof *s);
    s->used = true;
    s->approved = true;
    s->id = t->next_id++;
    s->from = from;
    s->to = to;
    s->total = total;
    s->ticks = ticks;
    *slot = k;
    say_n(t, "Stream started: ", total, " over its periods.");
    return ZXM_OK;
}

int trickle_stream_change(trickle_t *t, uint32_t slot, int64_t total, int64_t ticks,
                          bool owner_confirmed)
{
    trk_stream_t *s = stream_at(t, slot);
    if (!s) return ZXM_E_ARG;
    if (!s->approved) return ZXM_E_STATE;
    if (total < 1 || total > TRK_TOTAL_MAX || ticks < 1 || ticks > TRK_TICKS_MAX) return ZXM_E_ARG;
    if (trk_stream_due(total, s->tick, ticks, s->paid) == TRK_REFUSED) {
        say(t, "The new terms owe less than the stream has paid; it never claws back.");
        return ZXM_E_ARG;
    }
    if (!owner_confirmed) {
        say_n(t, "Change the stream to ", total, "? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    s->total = total;
    s->ticks = ticks;
    say_n(t, "Stream changed: ", total, " over its periods.");
    return ZXM_OK;
}

int trickle_stream_stop(trickle_t *t, uint32_t slot, bool owner_confirmed)
{
    trk_stream_t *s = stream_at(t, slot);
    if (!s) return ZXM_E_ARG;
    if (!s->approved) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(t, "Stop the stream? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    s->approved = false;
    say_n(t, "Stream stopped; it paid ", s->paid, ".");
    return ZXM_OK;
}

int trickle_stream_due_at(const trickle_t *t, uint32_t slot, int64_t tick, int64_t *due)
{
    if (!due || slot >= TRK_STREAMS || !t->s[slot].used) return ZXM_E_ARG;
    const trk_stream_t *s = &t->s[slot];
    if (tick < s->tick || tick > s->ticks) return ZXM_E_ARG;
    int64_t d = trk_stream_due(s->total, tick, s->ticks, s->paid);
    if (d == TRK_REFUSED) return ZXM_E_ARG;
    *due = d;
    return ZXM_OK;
}

int trickle_stream_pay(trickle_t *t, uint32_t slot, int64_t tick, bool owner_confirmed,
                       uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    trk_stream_t *s = stream_at(t, slot);
    if (!s) return ZXM_E_ARG;
    if (!t->books.loaded) return ZXM_E_STATE;
    int64_t due = 0;
    int rc = trickle_stream_due_at(t, slot, tick, &due);
    if (rc != ZXM_OK) return rc;
    *entry = 0;
    if (due == 0 && s->approved) {
        say_n(t, "Nothing is due at tick ", tick, ".");
        return ZXM_OK;
    }
    /* TK3: the margins the rule reads */
    const coffer_account_t *f = coffer_account(&t->books, s->from);
    if (!f) return ZXM_E_STATE;
    int64_t after = coffer_balance(f->balance, -due);
    int64_t cap_left = coffer_balance(t->books.day_cap, -t->books.spent);
    int64_t room = cap_left == COFFER_REFUSED ? COFFER_REFUSED : coffer_balance(cap_left, -due);
    int64_t unpaid = coffer_balance(s->total, -s->paid);
    int64_t left = unpaid == COFFER_REFUSED ? COFFER_REFUSED : coffer_balance(unpaid, -due);
    if (!trk_stream_pay_ok(due, after, room, left, s->approved ? 1 : 0)) {
        say(t, !s->approved ? "The stream is stopped: nothing is paid."
               : after < 0  ? "Not enough in the paying account: it may not go below zero."
               : room < 0   ? "Past today's cap: the owner's per-day cap would be passed."
                            : "The payment does not fit the stream.");
        return s->approved ? ZXM_E_ARG : ZXM_E_STATE;
    }
    if (!owner_confirmed) {
        say_n(t, "Pay ", due, " now? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    char ref[COFFER_REF_MAX + 1];
    make_ref(ref, 'S', s->id, 'T', tick);
    rc = post(t, s->from, s->to, due, ref, entry);
    if (rc != ZXM_OK) return rc;
    s->paid += due; /* left >= 0: at most total */
    s->tick = tick;
    say_n(t, "Paid ", due, s->paid == s->total ? "; the stream is paid in full." : ".");
    return coffer_load(&t->books);
}

int trickle_stream_reconcile(trickle_t *t, uint32_t slot)
{
    trk_stream_t *s = stream_at(t, slot);
    if (!s) return ZXM_E_ARG;
    if (!t->books.loaded) return ZXM_E_STATE;
    int rc = coffer_load(&t->books);
    if (rc != ZXM_OK) return rc;
    rc = coffer_entries(&t->books, s->from);
    if (rc != ZXM_OK) return rc;
    char pre[COFFER_REF_MAX + 1];
    uint32_t at = 0;
    pre[0] = 0;
    put(pre, sizeof pre, &at, "TRICKLE S");
    put_num(pre, sizeof pre, &at, (int64_t) s->id);
    put(pre, sizeof pre, &at, " T");
    int64_t paid = 0;
    for (uint32_t i = 0; i < t->books.ne; i++) {
        const coffer_entry_t *e = &t->books.e[i];
        if (strncmp(e->ref, pre, at)) continue;
        paid = coffer_balance(paid, -e->minor);
        if (paid == COFFER_REFUSED) return ZXM_E_PROTOCOL;
    }
    if (paid < 0 || paid > s->total) return ZXM_E_PROTOCOL; /* TQ4: keep what we had */
    s->paid = paid;
    say_n(t, "The books show the stream has paid ", paid, ".");
    return ZXM_OK;
}

/* ---- tithes --------------------------------------------------------------- */

static trk_tithe_t *tithe_at(trickle_t *t, uint32_t slot)
{
    if (slot >= TRK_TITHES || !t->g[slot].used) return NULL;
    return &t->g[slot];
}

int trickle_tithe_start(trickle_t *t, uint32_t from, uint32_t to, int64_t tnum, int64_t tden,
                        bool owner_confirmed, uint32_t *slot)
{
    if (!slot) return ZXM_E_ARG;
    if (!t->books.loaded) return ZXM_E_STATE;
    int rc = pair_ok(t, from, to);
    if (rc != ZXM_OK) return rc;
    if (tden < 1 || tden > TRK_TICKS_MAX || tnum < 1 || tnum > tden) {
        say(t, "A tithe is a share of 1 to the whole, its parts 1 to 99999.");
        return ZXM_E_ARG;
    }
    uint32_t k = 0;
    while (k < TRK_TITHES && t->g[k].used) k++;
    if (k == TRK_TITHES) {
        say(t, "Every tithe place is taken.");
        return ZXM_E_TOOBIG;
    }
    if (!owner_confirmed) {
        say(t, "Start the tithe? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    trk_tithe_t *g = &t->g[k];
    memset(g, 0, sizeof *g);
    g->used = true;
    g->approved = true;
    g->id = t->next_id++;
    g->from = from;
    g->to = to;
    g->tnum = tnum;
    g->tden = tden;
    *slot = k;
    say(t, "Tithe started.");
    return ZXM_OK;
}

int trickle_tithe_stop(trickle_t *t, uint32_t slot, bool owner_confirmed)
{
    trk_tithe_t *g = tithe_at(t, slot);
    if (!g) return ZXM_E_ARG;
    if (!g->approved) return ZXM_E_STATE;
    if (!owner_confirmed) {
        say(t, "Stop the tithe? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    g->approved = false;
    say_n(t, "Tithe stopped; it gave ", g->given, ".");
    return ZXM_OK;
}

int trickle_tithe_pay(trickle_t *t, uint32_t slot, int64_t income, bool owner_confirmed,
                      uint64_t *entry)
{
    if (!entry) return ZXM_E_ARG;
    trk_tithe_t *g = tithe_at(t, slot);
    if (!g) return ZXM_E_ARG;
    if (!t->books.loaded || !g->approved) return ZXM_E_STATE;
    int64_t tithe = trk_tithe_due(income, g->tnum, g->tden, g->carry);
    int64_t rest = trk_tithe_carry(income, g->tnum, g->tden, g->carry);
    if (tithe == TRK_REFUSED || rest == TRK_REFUSED) return ZXM_E_ARG;
    int64_t given = coffer_balance(g->given, tithe); /* refused past 18 digits */
    if (given == COFFER_REFUSED || g->incomes == UINT32_MAX) return ZXM_E_TOOBIG;
    if (!owner_confirmed) {
        say_n(t, "Give ", tithe, " of this income? Waiting for your confirmation.");
        return ZXM_E_STATE;
    }
    *entry = 0;
    if (tithe > 0) {
        char ref[COFFER_REF_MAX + 1];
        make_ref(ref, 'G', g->id, 'N', (int64_t) g->incomes + 1);
        int rc = post(t, g->from, g->to, tithe, ref, entry);
        if (rc != ZXM_OK) return rc; /* nothing posted: the carry stays */
    }
    g->carry = rest;
    g->given = given;
    g->incomes++;
    say_n(t, "Given ", tithe, tithe ? "; the remainder carries on." : "; it all carries on.");
    return tithe ? coffer_load(&t->books) : ZXM_OK;
}
