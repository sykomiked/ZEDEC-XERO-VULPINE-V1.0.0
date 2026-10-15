/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* intent.c — the AI intent layer (intent.h I1-I6). */
#include "intent.h"
#include "../robin_debanks/sha256.h"
#include "../chiglet/chg_vocab.h"

static lpx_t tag(uint8_t base)
{
    return lpx_make(base, LPX_P_NEUTRAL, LPX_PH_POTENTIAL);
}

static void zero(void *p, size_t n)
{
    uint8_t *b = p;
    while (n--) *b++ = 0;
}

/* ---- I1: the grammar ---- */
typedef struct {
    const char *p;
    size_t i, n;
} rd_t;

/* the next word: [i, end) up to a space or the end */
static bool word(rd_t *r, const char **w, size_t *wl)
{
    size_t s = r->i;
    while (r->i < r->n && r->p[r->i] != ' ') r->i++;
    if (r->i == s) return false;
    *w = r->p + s;
    *wl = r->i - s;
    return true;
}

/* exactly one space, then more text */
static bool space(rd_t *r)
{
    if (r->i >= r->n || r->p[r->i] != ' ') return false;
    r->i++;
    return r->i < r->n && r->p[r->i] != ' ';
}

static bool is(const char *w, size_t wl, const char *lit)
{
    size_t k = 0;
    while (k < wl && lit[k] && w[k] == lit[k]) k++;
    return k == wl && lit[k] == '\0';
}

static const intent_ccy_t *find_ccy(const intent_cfg_t *c, const char *w, size_t wl)
{
    if (wl != 3) return NULL;
    for (uint32_t i = 0; i < c->n_ccy && i < INTENT_MAX_CCY; i++)
        if (c->ccy[i].code[0] == w[0] && c->ccy[i].code[1] == w[1] && c->ccy[i].code[2] == w[2])
            return &c->ccy[i];
    return NULL;
}

static bool parse_amount(const char *w, size_t wl, uint8_t minor, uint64_t *out)
{
    uint64_t v = 0;
    size_t i = 0, frac = 0;
    bool dot = false;
    if (wl == 0 || minor > 18) return false;
    if (w[0] == '0' && wl > 1 && w[1] != '.') return false; /* no leading zeros */
    for (; i < wl; i++) {
        char c = w[i];
        if (c == '.') {
            if (dot || i == 0 || i + 1 == wl) return false;
            dot = true;
            continue;
        }
        if (c < '0' || c > '9') return false;
        if (dot && ++frac > minor) return false;
        if (__builtin_mul_overflow(v, 10u, &v) ||
            __builtin_add_overflow(v, (uint64_t) (c - '0'), &v))
            return false;
    }
    for (; frac < minor; frac++)
        if (__builtin_mul_overflow(v, 10u, &v)) return false;
    if (v == 0) return false;
    *out = v;
    return true;
}

static bool parse_account(const char *w, size_t wl, uint32_t *out)
{
    uint64_t v = 0;
    if (wl == 0 || wl > 10 || (w[0] == '0' && wl > 1)) return false;
    for (size_t i = 0; i < wl; i++) {
        if (w[i] < '0' || w[i] > '9') return false;
        v = v * 10u + (uint64_t) (w[i] - '0');
    }
    if (v > UINT32_MAX) return false;
    *out = (uint32_t) v;
    return true;
}

/* [ ' memo "' text '"' ] END */
static bool parse_memo(rd_t *r, char *memo)
{
    const char *w;
    size_t wl;
    memo[0] = '\0';
    if (r->i == r->n) return true;
    if (!space(r) || !word(r, &w, &wl) || !is(w, wl, "memo") || !space(r)) return false;
    if (r->p[r->i] != '"') return false;
    r->i++;
    size_t o = 0;
    for (;;) {
        if (r->i >= r->n) return false;
        char c = r->p[r->i++];
        if (c == '"') break;
        if (c == '\\' || o >= INTENT_MEMO_MAX) return false;
        memo[o++] = c;
    }
    memo[o] = '\0';
    return r->i == r->n;
}

/* I7: [a-z0-9._:/-]{1,INTENT_OBJECT_MAX} */
static bool parse_object(const char *w, size_t wl, char *out)
{
    if (wl == 0 || wl > INTENT_OBJECT_MAX) return false;
    for (size_t i = 0; i < wl; i++) {
        char c = w[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '.' || c == '_' ||
                  c == ':' || c == '/' || c == '-';
        if (!ok) return false;
        out[i] = c;
    }
    out[wl] = '\0';
    return true;
}

intent_status_t intent_parse(const intent_cfg_t *cfg, const char *text, size_t len,
                             intent_req_t *out)
{
    if (!out) return INTENT_E_ARG;
    zero(out, sizeof *out);
    if (!cfg || !text) return INTENT_E_ARG;
    if (len == 0 || len > INTENT_MAX_TEXT) return INTENT_E_PARSE;
    for (size_t i = 0; i < len; i++)
        if ((unsigned char) text[i] < 0x20 || (unsigned char) text[i] > 0x7E) return INTENT_E_PARSE;
    rd_t r = {text, 0, len};
    const char *w;
    size_t wl;
    intent_req_t q;
    zero(&q, sizeof q);
    if (!word(&r, &w, &wl)) return INTENT_E_PARSE;

    if (is(w, wl, "report")) {
        if (!space(&r) || !word(&r, &w, &wl) || r.i != r.n) return INTENT_E_PARSE;
        if (is(w, wl, "balance_sheet"))
            q.kind = INTENT_REPORT_BALANCE_SHEET;
        else if (is(w, wl, "tax"))
            q.kind = INTENT_REPORT_TAX;
        else
            return INTENT_E_PARSE;
        *out = q;
        return INTENT_OK;
    }
    if (is(w, wl, "act")) { /* I7 */
        if (!space(&r) || !word(&r, &w, &wl)) return INTENT_E_PARSE;
        int a = chgv_act_find(w, wl);
        if (a < 0) return INTENT_E_PARSE;
        q.kind = INTENT_ACT;
        q.act = (uint8_t) a;
        if (!space(&r) || !word(&r, &w, &wl) || !parse_object(w, wl, q.object))
            return INTENT_E_PARSE;
        if (!parse_memo(&r, q.memo)) return INTENT_E_PARSE;
        *out = q;
        return INTENT_OK;
    }
    const char *prep;
    if (is(w, wl, "pay")) {
        q.kind = INTENT_PAY;
        prep = "to";
    } else if (is(w, wl, "invoice")) {
        q.kind = INTENT_INVOICE;
        prep = "from";
    } else if (is(w, wl, "route")) {
        q.kind = INTENT_ROUTE;
        prep = "to";
    } else {
        return INTENT_E_PARSE;
    }
    const char *aw;
    size_t al;
    if (!space(&r) || !word(&r, &aw, &al)) return INTENT_E_PARSE;
    if (!space(&r) || !word(&r, &w, &wl)) return INTENT_E_PARSE;
    for (size_t i = 0; i < wl; i++)
        if (w[i] < 'A' || w[i] > 'Z') return INTENT_E_PARSE;
    if (wl != 3) return INTENT_E_PARSE;
    const intent_ccy_t *cc = find_ccy(cfg, w, wl);
    if (!cc) return INTENT_E_CCY;
    if (!parse_amount(aw, al, cc->minor, &q.amount)) return INTENT_E_PARSE;
    q.ccy[0] = w[0];
    q.ccy[1] = w[1];
    q.ccy[2] = w[2];
    if (!space(&r) || !word(&r, &w, &wl) || !is(w, wl, prep)) return INTENT_E_PARSE;
    uint32_t acct;
    if (!space(&r) || !word(&r, &w, &wl) || !parse_account(w, wl, &acct)) return INTENT_E_PARSE;
    if (q.kind == INTENT_INVOICE)
        q.from = acct;
    else
        q.to = acct;
    if (q.kind == INTENT_ROUTE) {
        if (r.i != r.n) return INTENT_E_PARSE;
    } else if (!parse_memo(&r, q.memo)) {
        return INTENT_E_PARSE;
    }
    *out = q;
    return INTENT_OK;
}

/* ---- the layer ---- */
void intent_init(intent_t *x, const intent_cfg_t *cfg)
{
    if (!x) return;
    zero(x, sizeof *x);
    if (cfg) x->cfg = *cfg;
}

void intent_set_epoch(intent_t *x, uint64_t epoch)
{
    if (!x || epoch <= x->epoch) return;
    x->epoch = epoch;
    x->spent = 0;
    x->proposed = 0;
}

static void put_le(uint8_t *o, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++) o[i] = (uint8_t) (v >> (8 * i));
}

/* I5 */
static void draft_digest(intent_draft_t *d)
{
    uint8_t b[13 + 1 + 8 + 4 + 4 + 4 + 1 + INTENT_MEMO_MAX + 8 + 8 + 1 + 1 + INTENT_OBJECT_MAX];
    static const char dom[13] = {'z', 'x', 'v', '/', 'i', 'n', 't', 'e', 'n', 't', '/', 'v', '1'};
    size_t o = 0, ml = 0;
    for (unsigned i = 0; i < 13; i++) b[o++] = (uint8_t) dom[i];
    b[o++] = d->req.kind;
    put_le(b + o, d->req.amount, 8);
    o += 8;
    for (unsigned i = 0; i < 4; i++) b[o++] = (uint8_t) d->req.ccy[i];
    put_le(b + o, d->req.from, 4);
    o += 4;
    put_le(b + o, d->req.to, 4);
    o += 4;
    while (ml < INTENT_MEMO_MAX && d->req.memo[ml]) ml++;
    b[o++] = (uint8_t) ml;
    for (size_t i = 0; i < ml; i++) b[o++] = (uint8_t) d->req.memo[i];
    put_le(b + o, d->seq, 8);
    o += 8;
    put_le(b + o, d->epoch, 8);
    o += 8;
    if (d->req.kind == INTENT_ACT) { /* I7: money drafts keep their v1 bytes */
        size_t ol = 0;
        while (ol < INTENT_OBJECT_MAX && d->req.object[ol]) ol++;
        b[o++] = d->req.act;
        b[o++] = (uint8_t) ol;
        for (size_t i = 0; i < ol; i++) b[o++] = (uint8_t) d->req.object[i];
    }
    sha256(b, o, d->digest);
}

static bool listed(const uint32_t *a, uint32_t n, uint32_t cap, uint32_t v)
{
    for (uint32_t i = 0; i < n && i < cap; i++)
        if (a[i] == v) return true;
    return false;
}

static bool expired(const intent_t *x, const intent_draft_t *d)
{
    return x->epoch > d->epoch && x->epoch - d->epoch > x->cfg.ttl;
}

/* I3 */
static lpx_t guard(intent_t *x, intent_req_t *q)
{
    const intent_cfg_t *c = &x->cfg;
    lpx_t acc = tag(LPX_B_TRUE);
    if (q->kind == INTENT_ACT) /* G7 */
        return q->act < 64u && (c->acts >> q->act) & 1u ? acc : tag(LPX_B_FALSE);
    if (q->kind == INTENT_INVOICE) {
        if (c->n_own == 0) return tag(LPX_B_FALSE);
        q->to = c->own[0]; /* paid into the owner's first account */
        if (q->from == q->to || listed(c->own, c->n_own, INTENT_MAX_OWN, q->from) ||
            listed(c->deny, c->n_deny, INTENT_MAX_DENY, q->from))
            return tag(LPX_B_FALSE); /* G4 */
        return acc;
    }
    if (q->kind != INTENT_PAY) return acc; /* routes and reports only read */
    if (c->n_own == 0) return tag(LPX_B_FALSE);
    q->from = c->own[0];                                          /* G1 */
    if (q->amount > c->cap) acc = lpx_and(acc, tag(LPX_B_FALSE)); /* G2 */
    uint64_t s;
    if (__builtin_add_overflow(x->spent, q->amount, &s) || s > c->epoch_budget)
        acc = lpx_and(acc, tag(LPX_B_FALSE)); /* G3 */
    if (q->to == q->from || listed(c->own, c->n_own, INTENT_MAX_OWN, q->to) ||
        listed(c->deny, c->n_deny, INTENT_MAX_DENY, q->to))
        acc = lpx_and(acc, tag(LPX_B_FALSE)); /* G4 */
    if (c->juris) {                           /* G6 */
        juris_transfer_t t;
        zero(&t, sizeof t);
        for (unsigned i = 0; i < 3; i++) t.ccy[i] = q->ccy[i];
        t.amount = q->amount;
        t.sender = q->from;
        t.receiver = q->to;
        t.epoch = x->epoch;
        acc = lpx_and(acc, juris_check_transfer(c->juris, &t));
    }
    return acc;
}

static intent_draft_t *slot_for(intent_t *x, uint64_t seq)
{
    return &x->slot[seq % INTENT_SLOTS];
}

intent_status_t intent_propose(intent_t *x, const char *text, size_t len, uint64_t *seq)
{
    if (seq) *seq = 0;
    if (!x || !text) return INTENT_E_ARG;
    if (x->proposed >= x->cfg.max_per_epoch) return INTENT_E_WAIT; /* G5 */
    x->proposed++;
    intent_req_t q;
    intent_status_t ps = intent_parse(&x->cfg, text, len, &q);
    if (ps != INTENT_OK) {
        x->refused++;
        return ps;
    }
    if (x->seq == UINT64_MAX) return INTENT_E_FULL;
    uint64_t n = x->seq + 1;
    intent_draft_t *d = slot_for(x, n);
    if (d->state == INTENT_ST_PROPOSED && !expired(x, d)) return INTENT_E_FULL;
    x->seq = n;
    zero(d, sizeof *d);
    d->seq = n;
    d->epoch = x->epoch;
    d->tag = guard(x, &q);
    d->req = q;
    draft_digest(d);
    if (seq) *seq = n;
    switch (lpx_action(d->tag)) {
    case LPX_ACT_PROCEED:
        if (q.kind == INTENT_PAY || q.kind == INTENT_INVOICE || q.kind == INTENT_ACT) {
            d->state = INTENT_ST_PROPOSED;
        } else {
            d->state = INTENT_ST_RELEASED; /* I2: read-only, nothing to approve */
            x->released++;
        }
        return INTENT_OK;
    case LPX_ACT_WAIT:
        d->state = INTENT_ST_REFUSED;
        x->refused++;
        return INTENT_E_WAIT;
    default:
        d->state = INTENT_ST_REFUSED;
        x->refused++;
        return INTENT_E_GUARD;
    }
}

const intent_draft_t *intent_get(const intent_t *x, uint64_t seq)
{
    if (!x || seq == 0) return NULL;
    const intent_draft_t *d = &x->slot[seq % INTENT_SLOTS];
    return d->seq == seq && d->state != INTENT_ST_FREE ? d : NULL;
}

void intent_approve_msg(const intent_draft_t *d, uint8_t out[INTENT_APPROVE_MSG])
{
    static const char dom[21] = {'z', 'x', 'v', '/', 'i', 'n', 't', 'e', 'n', 't', '-',
                                 'a', 'p', 'p', 'r', 'o', 'v', 'e', '/', 'v', '1'};
    size_t o = 0;
    for (unsigned i = 0; i < 21; i++) out[o++] = (uint8_t) dom[i];
    for (unsigned i = 0; i < 32; i++) out[o++] = d->digest[i];
    put_le(out + o, d->seq, 8);
}

intent_status_t intent_release(intent_t *x, uint64_t seq, const uint8_t *sig, uint32_t sig_len,
                               intent_draft_t *out)
{
    if (out) zero(out, sizeof *out);
    if (!x || !out) return INTENT_E_ARG;
    intent_draft_t *d = (intent_draft_t *) intent_get(x, seq);
    if (!d) return INTENT_E_UNKNOWN;
    if (d->state != INTENT_ST_PROPOSED) return INTENT_E_USED;
    if (expired(x, d)) {
        d->state = INTENT_ST_EXPIRED;
        return INTENT_E_EXPIRED;
    }
    if (!x->cfg.verify || !sig) return INTENT_E_SIG;
    /* I5: the digest is recomputed, so a draft edited in memory fails */
    intent_draft_t chk = *d;
    draft_digest(&chk);
    uint8_t diff = 0;
    for (unsigned i = 0; i < 32; i++) diff |= (uint8_t) (chk.digest[i] ^ d->digest[i]);
    if (diff) return INTENT_E_SIG;
    uint8_t msg[INTENT_APPROVE_MSG];
    intent_approve_msg(d, msg);
    if (!x->cfg.verify(x->cfg.vctx, msg, INTENT_APPROVE_MSG, sig, sig_len)) return INTENT_E_SIG;
    if (d->req.kind == INTENT_PAY) { /* G3 again: the budget may be used up */
        uint64_t s;
        if (__builtin_add_overflow(x->spent, d->req.amount, &s) || s > x->cfg.epoch_budget) {
            d->state = INTENT_ST_REFUSED;
            x->refused++;
            return INTENT_E_GUARD;
        }
        x->spent = s;
    }
    d->state = INTENT_ST_RELEASED;
    x->released++;
    *out = *d;
    return INTENT_OK;
}

/* ---- I6 ---- */
static bool better(const intent_route_t *o, uint32_t a, uint32_t b)
{
    bool pa = lpx_action(o[a].tag) == LPX_ACT_PROCEED, pb = lpx_action(o[b].tag) == LPX_ACT_PROCEED;
    if (pa != pb) return pa;
    if (o[a].received != o[b].received) return o[a].received > o[b].received;
    if (o[a].fee != o[b].fee) return o[a].fee < o[b].fee;
    if (o[a].hops != o[b].hops) return o[a].hops < o[b].hops;
    return a < b;
}

uint32_t intent_rank_routes(const intent_route_t *opt, uint32_t n, uint32_t *order)
{
    if (!opt || !order) return 0;
    for (uint32_t i = 0; i < n; i++) {
        uint32_t k = i;
        while (k > 0 && better(opt, i, order[k - 1])) {
            order[k] = order[k - 1];
            k--;
        }
        order[k] = i;
    }
    uint32_t ok = 0;
    for (uint32_t i = 0; i < n; i++)
        if (lpx_action(opt[i].tag) == LPX_ACT_PROCEED) ok++;
    return ok;
}
