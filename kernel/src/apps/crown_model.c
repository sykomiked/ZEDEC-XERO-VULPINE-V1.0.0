/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* crown_model.c — Crown's model (crown_model.h CM1-CM5). */
#include <stdio.h>
#include <string.h>

#include "crown_model.h"

#define DEC_MAX 19u /* digits of a 64-bit decimal string */

static void say(crown_t *c, const char *s)
{
    size_t n = strlen(s);
    if (n > CROWN_NOTICE_MAX) n = CROWN_NOTICE_MAX;
    memcpy(c->notice, s, n);
    c->notice[n] = 0;
}

/* CM1: a decimal string of 1..19 digits that fits int64 */
static int get_dec(const zxm_link_t *l, int32_t obj, const char *key, int64_t *v)
{
    char s[DEC_MAX + 2];
    if (zxm_get_str(l, obj, key, s, sizeof s) != ZXM_OK) return ZXM_E_PROTOCOL;
    size_t n = strlen(s);
    if (n == 0 || n > DEC_MAX) return ZXM_E_PROTOCOL;
    uint64_t x = 0;
    for (size_t i = 0; i < n; i++) {
        if (s[i] < '0' || s[i] > '9') return ZXM_E_PROTOCOL;
        uint64_t d = (uint64_t) (s[i] - '0');
        if (x > ((uint64_t) INT64_MAX - d) / 10u) return ZXM_E_PROTOCOL;
        x = x * 10u + d;
    }
    *v = (int64_t) x;
    return ZXM_OK;
}

static int get_u32(const zxm_link_t *l, int32_t obj, const char *key, uint32_t *v)
{
    uint64_t x = 0;
    if (zxm_get_u64(l, obj, key, &x) != ZXM_OK || x > UINT32_MAX) return ZXM_E_PROTOCOL;
    *v = (uint32_t) x;
    return ZXM_OK;
}

void crown_init(crown_t *c, zxm_link_t *link)
{
    if (!c) return;
    memset(c, 0, sizeof *c);
    c->link = link;
    c->cap_num = 1;
    c->cap_den = 1;
}

int crown_setup(crown_t *c, uint32_t funnel)
{
    if (!c || !c->link) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.setup");
    zxm_p_u64(c->link, "funnel", funnel);
    zxm_p_u64(c->link, "cap_num", (uint64_t) c->cap_num);
    zxm_p_u64(c->link, "cap_den", (uint64_t) c->cap_den);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    bool minted = true;
    if (zxm_get_bool(c->link, c->link->result, "minted", &minted) != ZXM_OK || minted)
        return ZXM_E_PROTOCOL;
    c->set_up = true;
    say(c, "The grant engine is set up on its sandbox ledger: grants come only from the pool.");
    return ZXM_OK;
}

int crown_open_account(crown_t *c, uint32_t owner, bool barrel, uint32_t *account)
{
    if (!c || !c->link || !account) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.account_open");
    zxm_p_u64(c->link, "owner", owner);
    zxm_p_str(c->link, "asset", barrel ? "barrel" : "vino");
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    uint32_t a = 0, o = 0;
    if (get_u32(c->link, c->link->result, "account", &a) != ZXM_OK ||
        get_u32(c->link, c->link->result, "owner", &o) != ZXM_OK || o != owner)
        return ZXM_E_PROTOCOL;
    *account = a;
    return ZXM_OK;
}

int crown_open_lot(crown_t *c, uint32_t lot)
{
    if (!c || !c->link) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.lot_open");
    zxm_p_u64(c->link, "lot", lot);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    uint32_t got = 0;
    if (get_u32(c->link, c->link->result, "lot", &got) != ZXM_OK || got != lot)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int crown_fund(crown_t *c, uint32_t account, int64_t amount)
{
    if (!c || !c->link || amount <= 0) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.fund");
    zxm_p_u64(c->link, "account", account);
    zxm_p_i64(c->link, "amount_minor", amount);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    bool sandbox = false;
    int64_t bal = 0;
    if (zxm_get_bool(c->link, c->link->result, "sandbox", &sandbox) != ZXM_OK || !sandbox ||
        get_dec(c->link, c->link->result, "balance_minor", &bal) != ZXM_OK || bal < amount)
        return ZXM_E_PROTOCOL;
    return ZXM_OK;
}

int crown_balance(crown_t *c, uint32_t account, int64_t *balance)
{
    if (!c || !c->link || !balance) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.account");
    zxm_p_u64(c->link, "account", account);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    uint32_t got = 0;
    int64_t b = 0;
    if (get_u32(c->link, c->link->result, "account", &got) != ZXM_OK || got != account ||
        get_dec(c->link, c->link->result, "balance_minor", &b) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    *balance = b;
    return ZXM_OK;
}

int crown_refresh(crown_t *c, uint64_t now)
{
    if (!c || !c->link) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.status");
    zxm_p_u64(c->link, "now", now);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int32_t r = c->link->result;
    bool set_up = false;
    if (zxm_get_bool(c->link, r, "set_up", &set_up) != ZXM_OK) return ZXM_E_PROTOCOL;
    int64_t epoch = 0, pool = 0, inflow = 0, granted = 0, total = 0;
    if (set_up &&
        (get_dec(c->link, r, "epoch", &epoch) != ZXM_OK ||
         get_dec(c->link, r, "pool_minor", &pool) != ZXM_OK ||
         get_dec(c->link, r, "inflow_minor", &inflow) != ZXM_OK ||
         get_dec(c->link, r, "granted_barrels", &granted) != ZXM_OK ||
         get_dec(c->link, r, "total_barrels_granted", &total) != ZXM_OK || granted > total))
        return ZXM_E_PROTOCOL;
    /* the whole answer is read: only now is the copy changed */
    c->set_up = set_up;
    c->epoch = epoch;
    c->pool = pool;
    c->inflow = inflow;
    c->granted = granted;
    c->total = total;
    return ZXM_OK;
}

int crown_epoch(crown_t *c, int64_t epoch)
{
    if (!c || !c->link || !c->set_up) return ZXM_E_STATE;
    if (epoch <= c->epoch) return ZXM_E_ARG;
    if (c->held.pending) return ZXM_E_STATE; /* an allotment waits on this epoch */
    zxm_begin(c->link, "grants.epoch");
    zxm_p_i64(c->link, "epoch", epoch);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int64_t got = 0;
    if (get_dec(c->link, c->link->result, "epoch", &got) != ZXM_OK || got != epoch)
        return ZXM_E_PROTOCOL;
    c->epoch = epoch;
    c->inflow = 0;
    c->granted = 0;
    for (uint32_t i = 0; i < c->n; i++) c->m[i].evaluated = false; /* standing is per epoch */
    return ZXM_OK;
}

int crown_credit(crown_t *c, uint32_t from, int64_t amount, uint64_t now)
{
    if (!c || !c->link || !c->set_up) return ZXM_E_STATE;
    if (amount <= 0) return ZXM_E_ARG;
    int64_t zero = 0, want = 0;
    if (crown_pool_after(c->pool, &amount, &zero, 1, &want) != 0) return ZXM_E_ARG;
    if (c->inflow > INT64_MAX - amount) return ZXM_E_ARG;
    zxm_begin(c->link, "grants.pool_credit");
    zxm_p_u64(c->link, "from", from);
    zxm_p_i64(c->link, "amount_minor", amount);
    zxm_p_u64(c->link, "now", now);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    int64_t pool = 0, inflow = 0;
    if (get_dec(c->link, c->link->result, "pool_minor", &pool) != ZXM_OK ||
        get_dec(c->link, c->link->result, "inflow_minor", &inflow) != ZXM_OK || pool != want ||
        inflow != c->inflow + amount)
        return ZXM_E_PROTOCOL;
    c->pool = pool;
    c->inflow = inflow;
    say(c, "Credited to the grant pool for this epoch's allotment.");
    return ZXM_OK;
}

int crown_add_member(crown_t *c, const crown_member_t *m)
{
    if (!c || !m) return ZXM_E_ARG;
    if (c->n >= CROWN_MEMBERS) return ZXM_E_STATE;
    if (m->weight < 0 || m->weight > CROWN_NINE || m->volume < 0 || m->stake < 0) return ZXM_E_ARG;
    for (uint32_t i = 0; i < c->n; i++)
        if (c->m[i].subject == m->subject || c->m[i].account == m->account) return ZXM_E_ARG;
    c->m[c->n] = *m;
    c->m[c->n].evaluated = false;
    c->m[c->n].standing = false;
    c->m[c->n].mask = 0;
    c->n++;
    return ZXM_OK;
}

int crown_eval(crown_t *c, uint32_t idx)
{
    if (!c || !c->link || idx >= c->n) return ZXM_E_ARG;
    if (!c->set_up) return ZXM_E_STATE;
    crown_member_t *m = &c->m[idx];
    zxm_begin(c->link, "grants.eval");
    zxm_p_u64(c->link, "subject", m->subject);
    zxm_p_u64(c->link, "uptime_bps", m->uptime_bps);
    zxm_p_u64(c->link, "dropped_reports", m->dropped);
    zxm_p_u64(c->link, "routing_reports", m->routing);
    zxm_p_u64(c->link, "failed_challenges", m->failed);
    zxm_p_u64(c->link, "fraud_findings", m->fraud);
    zxm_p_i64(c->link, "volume", m->volume);
    zxm_p_i64(c->link, "stake", m->stake);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    uint32_t subject = 0, mask = 0;
    bool eligible = false;
    if (get_u32(c->link, c->link->result, "subject", &subject) != ZXM_OK || subject != m->subject ||
        get_u32(c->link, c->link->result, "mask", &mask) != ZXM_OK || mask > 0xFFu ||
        zxm_get_bool(c->link, c->link->result, "eligible", &eligible) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    int64_t flag[CROWN_REASONS];
    crown_reason_flags(mask, flag);
    bool standing = crown_standing(flag);
    if (standing != eligible) return ZXM_E_PROTOCOL;
    m->mask = mask;
    m->standing = standing;
    m->evaluated = true;
    return ZXM_OK;
}

uint32_t crown_eligible(const crown_t *c)
{
    uint32_t k = 0;
    if (!c) return 0;
    for (uint32_t i = 0; i < c->n; i++) k += c->m[i].evaluated && c->m[i].standing;
    return k;
}

int crown_set_cap(crown_t *c, int64_t num, int64_t den)
{
    if (!c || den < 1 || den > CROWN_NINE || num < 1 || num > den) return ZXM_E_ARG;
    c->cap_num = num;
    c->cap_den = den;
    return ZXM_OK;
}

void crown_set_permission(crown_t *c, bool granted, bool revoked)
{
    if (!c) return;
    c->post_granted = granted;
    c->post_revoked = revoked;
}

int crown_propose(crown_t *c, uint32_t lot, int64_t price, int64_t seed, uint32_t seats)
{
    if (!c || !c->set_up) return ZXM_E_STATE;
    if (c->held.pending) return ZXM_E_STATE; /* one allotment waits at a time */
    if (price < 1 || price > CROWN_NINE || seats == 0) return ZXM_E_ARG;
    uint32_t elig[CROWN_MEMBERS], ne = 0;
    for (uint32_t i = 0; i < c->n; i++) {
        if (!c->m[i].evaluated) return ZXM_E_STATE; /* every member's standing first */
        if (c->m[i].standing) elig[ne++] = i;
    }
    if (ne == 0) return ZXM_E_STATE;
    if (seats > ne) seats = ne;
    if (seats > CROWN_SEATS) seats = CROWN_SEATS;
    crown_allotment_t a;
    memset(&a, 0, sizeof a);
    int64_t weight[CROWN_SEATS], extra[CROWN_SEATS], total = 0;
    for (uint32_t k = 0; k < seats; k++) {
        int64_t at = crown_draw_at(seed, (int64_t) ne, (int64_t) k);
        if (at == CROWN_REFUSED) return ZXM_E_ARG; /* a seed past 9(9) */
        a.order[k] = elig[at];
        weight[k] = c->m[elig[at]].weight;
    }
    /* CM4: the budget, pool * num / den, without overflow (num <= den) */
    int64_t budget =
        c->pool / c->cap_den * c->cap_num + c->pool % c->cap_den * c->cap_num / c->cap_den;
    int64_t barrels = budget / price;
    if (barrels < 1) {
        say(c, "The pool cannot pay for one whole grant yet: nothing is allotted.");
        return ZXM_E_STATE;
    }
    if (barrels > CROWN_NINE) return ZXM_E_ARG;
    if (crown_allot(barrels, weight, seats, a.share, extra, &total) != 0) {
        say(c, "The drawn members carry no weight: nothing is allotted.");
        return ZXM_E_STATE;
    }
    int64_t cost = vintner_sale_value(barrels, price);
    if (cost == VINTNER_REFUSED || cost > c->pool) return ZXM_E_ARG;
    a.pending = true;
    a.lot = lot;
    a.price = price;
    a.seed = seed;
    a.barrels = barrels;
    a.cost = cost;
    a.seats = seats;
    c->held = a;
    char s[CROWN_NOTICE_MAX + 1];
    snprintf(s, sizeof s,
             "Allotment for epoch %lld: %lld grants to %u members drawn from seed %lld, "
             "waiting for your approval.",
             (long long) c->epoch, (long long) barrels, (unsigned) seats, (long long) seed);
    say(c, s);
    return ZXM_OK;
}

/* the held allotment's recipients as grants.distribute's params */
static int params(const crown_t *c, uint64_t now, char *out, uint32_t cap)
{
    const crown_allotment_t *a = &c->held;
    uint32_t at = 0;
    int w = snprintf(out, cap, "{\"lot\":%u,\"price\":%lld,\"now\":%llu,\"recipients\":[",
                     (unsigned) a->lot, (long long) a->price, (unsigned long long) now);
    if (w < 0 || (uint32_t) w >= cap) return ZXM_E_TOOBIG;
    at = (uint32_t) w;
    for (uint32_t k = 0; k < a->seats; k++) {
        const crown_member_t *m = &c->m[a->order[k]];
        w = snprintf(out + at, cap - at,
                     "%s{\"account\":%u,\"weight\":%lld,\"subject\":%u,\"uptime_bps\":%u,"
                     "\"dropped_reports\":%u,\"routing_reports\":%u,\"failed_challenges\":%u,"
                     "\"fraud_findings\":%u,\"volume\":%lld,\"stake\":%lld}",
                     k ? "," : "", (unsigned) m->account, (long long) m->weight,
                     (unsigned) m->subject, (unsigned) m->uptime_bps, (unsigned) m->dropped,
                     (unsigned) m->routing, (unsigned) m->failed, (unsigned) m->fraud,
                     (long long) m->volume, (long long) m->stake);
        if (w < 0 || (uint32_t) w >= cap - at) return ZXM_E_TOOBIG;
        at += (uint32_t) w;
    }
    if (at + 3u > cap) return ZXM_E_TOOBIG;
    out[at++] = ']';
    out[at++] = '}';
    out[at] = 0;
    return (int) at;
}

int crown_approve(crown_t *c, uint64_t now)
{
    if (!c || !c->link || !c->held.pending) return ZXM_E_STATE;
    if (!gatehouse_use_ok(1, c->post_granted ? 1 : 0, c->post_revoked ? 1 : 0)) {
        say(c, "Crown may not post grants: allow ledger.post for Crown to approve this allotment.");
        return ZXM_E_STATE;
    }
    static char req[ZXM_REQ_MAX];
    int n = params(c, now, req, sizeof req);
    if (n < 0) return n;
    zxm_begin(c->link, "grants.distribute");
    zxm_p_raw(c->link, req, (uint32_t) n);
    int rc = zxm_call(c->link);
    if (rc != ZXM_OK) return rc;
    const crown_allotment_t *a = &c->held;
    int32_t r = c->link->result;
    int64_t barrels = 0, cost = 0, pool = 0;
    if (get_dec(c->link, r, "barrels", &barrels) != ZXM_OK ||
        get_dec(c->link, r, "cost_minor", &cost) != ZXM_OK ||
        get_dec(c->link, r, "pool_minor", &pool) != ZXM_OK)
        return ZXM_E_PROTOCOL;
    /* the engine has acted: its numbers are kept, and any difference is flagged */
    int64_t zero = 0, want = 0;
    bool same = barrels == a->barrels && cost == a->cost &&
                crown_pool_after(c->pool, &zero, &cost, 1, &want) == 0 && want == pool;
    int32_t arr = zxm_get(c->link, r, "shares");
    uint32_t ns = zxm_len(c->link, arr);
    for (uint32_t k = 0; k < a->seats; k++) {
        bool found = false;
        uint32_t acct = c->m[a->order[k]].account;
        for (uint32_t i = 0; i < ns; i++) {
            int32_t o = zxm_at(c->link, arr, i);
            uint32_t got = 0;
            int64_t b = 0;
            if (o < 0 || get_u32(c->link, o, "account", &got) != ZXM_OK) continue;
            if (got != acct) continue;
            found = true;
            if (get_dec(c->link, o, "barrels", &b) != ZXM_OK || b != a->share[k]) same = false;
        }
        if (!found && a->share[k] != 0) same = false;
    }
    c->pool = pool;
    c->granted += barrels;
    c->total += barrels;
    c->mismatch = !same;
    memset(&c->held, 0, sizeof c->held);
    if (!same) {
        say(c, "The grant engine's allotment differs from the one you approved: check the grants.");
        return ZXM_E_PROTOCOL;
    }
    say(c, "Allotment approved: each grant is in its member's barrel account.");
    return ZXM_OK;
}

int crown_cancel(crown_t *c)
{
    if (!c || !c->held.pending) return ZXM_E_STATE;
    memset(&c->held, 0, sizeof c->held);
    say(c, "The allotment was cancelled: no grant was made.");
    return ZXM_OK;
}
