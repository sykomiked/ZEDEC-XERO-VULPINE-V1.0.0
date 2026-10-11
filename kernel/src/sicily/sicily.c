/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* sicily.c — Sicily Account view, Player Card tiers, steward threshold and
 * org card programs. See sicily.h S1-S7. */
#include "sicily.h"

/* ---- S1 ---- */
sic_status_t sic_view_compute(uint64_t liquid, uint64_t escrow, uint64_t liabilities,
                              uint64_t pending_in, uint64_t pending_out, sic_view_t *out)
{
    if (!out) return SIC_ERR_ARG;
    pay_memset(out, 0, sizeof *out);
    uint64_t assets;
    if (!pay_add_ok(liquid, escrow, &assets)) return SIC_ERR_OVERFLOW;
    /* net worth must be a representable int64: |assets - liabilities| <= INT64_MAX */
    int64_t nw;
    if (assets >= liabilities) {
        uint64_t d = assets - liabilities;
        if (d > (uint64_t) INT64_MAX) return SIC_ERR_OVERFLOW;
        nw = (int64_t) d;
    } else {
        uint64_t d = liabilities - assets;
        if (d > (uint64_t) INT64_MAX) return SIC_ERR_OVERFLOW;
        nw = -(int64_t) d;
    }
    out->liquid = liquid;
    out->escrow = escrow;
    out->assets = assets;
    out->liabilities = liabilities;
    out->pending_in = pending_in;
    out->pending_out = pending_out;
    out->spendable = liquid > pending_out ? liquid - pending_out : 0;
    out->net_worth = nw;
    out->equity = nw; /* derived, never stored */
    return SIC_OK;
}

static bool listed(const uint32_t *list, uint32_t n, uint32_t a)
{
    for (uint32_t i = 0; i < n; i++)
        if (list[i] == a) return true;
    return false;
}

sic_status_t sic_view(const pay_ledger_t *L, uint32_t owner, uint16_t asset, const uint32_t *escrow,
                      uint32_t n_escrow, uint64_t pending_in, uint64_t pending_out, sic_view_t *out)
{
    if (out) pay_memset(out, 0, sizeof *out);
    if (!L || !out || n_escrow > SIC_MAX_ESCROW || (n_escrow && !escrow)) return SIC_ERR_ARG;
    for (uint32_t i = 0; i < n_escrow; i++) {
        const pay_account_t *a = pay_ledger_account(L, escrow[i]);
        if (!a || a->owner != owner || a->asset != asset) return SIC_ERR_ARG;
    }
    uint64_t liquid = 0, esc = 0, liab = 0;
    for (uint32_t i = 0; i < L->n_accounts; i++) {
        const pay_account_t *a = pay_ledger_account(L, i);
        if (!a || a->owner != owner || a->asset != asset) continue;
        uint64_t *sum = listed(escrow, n_escrow, i) ? &esc : &liquid;
        if (!pay_add_ok(*sum, a->debit, sum) || !pay_add_ok(liab, a->credit, &liab))
            return SIC_ERR_OVERFLOW;
    }
    return sic_view_compute(liquid, esc, liab, pending_in, pending_out, out);
}

/* ---- S2 / S3 ---- */
void sic_tier_table_default(sic_tier_table_t *tab)
{
    if (!tab) return;
    static const sic_threshold_t d[SIC_EARNED_TIERS] = {
        {100, 1000, 9000}, {250, 10000, 9500}, {500, 100000, 9800}, {800, 1000000, 9950}};
    for (uint32_t i = 0; i < SIC_EARNED_TIERS; i++) tab->t[i] = d[i];
}

uint64_t sic_effective_cred(uint64_t credibility, uint64_t giving)
{
    uint64_t r, cap = pay_udiv64(credibility, 3, &r), g = giving < cap ? giving : cap, e;
    return pay_add_ok(credibility, g, &e) ? e : UINT64_MAX;
}

sic_tier_t sic_tier_eval(const sic_tier_table_t *tab, uint64_t credibility, uint64_t bond,
                         uint32_t uptime_bps, uint64_t giving)
{
    sic_tier_table_t def;
    if (!tab) {
        sic_tier_table_default(&def);
        tab = &def;
    }
    if (uptime_bps > SIC_BPS_MAX) return SIC_TIER_COPPER;
    uint64_t c = sic_effective_cred(credibility, giving);
    uint32_t t = 0;
    while (t < SIC_EARNED_TIERS && c >= tab->t[t].cred && bond >= tab->t[t].bond &&
           uptime_bps >= tab->t[t].uptime_bps)
        t++;
    return (sic_tier_t) t;
}

const char *sic_tier_name(sic_tier_t t, uint32_t which)
{
    static const char *const names[SIC_TIER_COUNT][2] = {
        {"Copper", "Obsidian"}, {"Bronze", "Carnelian"}, {"Silver", "Jade"},
        {"Gold", "Lapis"},      {"Platinum", "Ruby"},    {"Curzi", "Admas"}};
    if ((uint32_t) t >= SIC_TIER_COUNT || which > 1) return "";
    return names[t][which];
}

uint32_t sic_tier_perks(sic_tier_t t)
{
    static const uint32_t add[SIC_TIER_COUNT] = {0,
                                                 SIC_PERK_FREE_REGISTRATION | SIC_PERK_STOREFRONTS,
                                                 SIC_PERK_FORWARDS | SIC_PERK_IP_NOTES,
                                                 0,
                                                 SIC_PERK_XB_LICENSING,
                                                 0};
    if ((uint32_t) t >= SIC_TIER_COUNT) return 0;
    uint32_t p = 0;
    for (uint32_t i = 0; i <= (uint32_t) t; i++) p |= add[i];
    return p;
}

/* ---- S4 ---- */
static void put32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static void put64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++) p[i] = (uint8_t) (v >> (8 * i));
}

static uint32_t popcount8(uint8_t m)
{
    uint32_t n = 0;
    for (; m; m &= (uint8_t) (m - 1)) n++;
    return n;
}

sic_status_t sic_steward_init(sic_steward_t *s, const uint32_t key[SIC_STEWARDS], uint32_t founder,
                              uint64_t window, sic_verify_fn verify, void *vctx)
{
    if (!s || !key || !verify || founder >= SIC_STEWARDS) return SIC_ERR_ARG;
    if (window == 0) window = SIC_WINDOW_DEFAULT;
    if (window <= SIC_TIMELOCK) return SIC_ERR_ARG;
    for (uint32_t i = 0; i < SIC_STEWARDS; i++)
        for (uint32_t j = i + 1; j < SIC_STEWARDS; j++)
            if (key[i] == key[j]) return SIC_ERR_ARG; /* one holder, one key */
    pay_memset(s, 0, sizeof *s);
    for (uint32_t i = 0; i < SIC_STEWARDS; i++) s->key[i] = key[i];
    s->founder = founder;
    s->window = window;
    s->verify = verify;
    s->vctx = vctx;
    s->next_id = 1;
    return SIC_OK;
}

void sic_steward_msg(uint8_t op, uint32_t id, uint8_t kind, const uint8_t params[32],
                     uint8_t out[SIC_STEWARD_MSG])
{
    static const char tag[] = "zxv/sic-steward/v1";
    for (uint32_t i = 0; i < 18; i++) out[i] = (uint8_t) tag[i];
    out[18] = op;
    put32(out + 19, id);
    out[23] = kind;
    pay_memcpy(out + 24, params, 32);
}

static bool expired(const sic_steward_t *s, const sic_proposal_t *p, uint64_t tick)
{
    /* elapsed time, so no deadline sum can wrap */
    return tick >= p->proposed && tick - p->proposed >= s->window;
}

static sic_proposal_t *find(const sic_steward_t *s, uint32_t id)
{
    if (!s || id == 0) return 0;
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS; i++)
        if (s->p[i].state != SIC_PROP_FREE && s->p[i].id == id) return (sic_proposal_t *) &s->p[i];
    return 0;
}

sic_status_t sic_steward_propose(sic_steward_t *s, uint8_t kind, const uint8_t params[32],
                                 uint64_t tick, uint32_t *out_id)
{
    if (!s || !params || kind < SIC_ACT_MASTER_SEED || kind > SIC_ACT_ARBITRATION ||
        s->next_id == 0)
        return SIC_ERR_ARG;
    sic_proposal_t *p = 0;
    for (uint32_t i = 0; i < SIC_MAX_PROPOSALS && !p; i++)
        if (s->p[i].state != SIC_PROP_OPEN || expired(s, &s->p[i], tick)) p = &s->p[i];
    if (!p) return SIC_ERR_FULL;
    pay_memset(p, 0, sizeof *p);
    p->id = s->next_id++;
    p->kind = kind;
    p->state = SIC_PROP_OPEN;
    pay_memcpy(p->params, params, 32);
    p->proposed = tick;
    if (out_id) *out_id = p->id;
    return SIC_OK;
}

/* the common checks of approve / cancel / execute */
static sic_status_t open_at(sic_steward_t *s, uint32_t id, uint64_t tick, sic_proposal_t **out)
{
    sic_proposal_t *p = find(s, id);
    if (!p) return SIC_ERR_NOT_FOUND;
    if (p->state != SIC_PROP_OPEN) return SIC_ERR_STATE;
    if (tick < p->proposed) return SIC_ERR_ARG; /* time never runs backwards */
    if (expired(s, p, tick)) {
        p->state = SIC_PROP_EXPIRED;
        return SIC_ERR_EXPIRED;
    }
    *out = p;
    return SIC_OK;
}

static sic_status_t vote(sic_steward_t *s, uint8_t op, uint32_t id, uint32_t steward,
                         const uint8_t *sig, uint32_t sig_len, uint64_t tick)
{
    if (!s || !sig || steward >= SIC_STEWARDS) return SIC_ERR_ARG;
    sic_proposal_t *p;
    sic_status_t st = open_at(s, id, tick, &p);
    if (st != SIC_OK) return st;
    uint8_t m[SIC_STEWARD_MSG];
    sic_steward_msg(op, id, p->kind, p->params, m);
    if (!s->verify(s->vctx, s->key[steward], m, sizeof m, sig, sig_len)) return SIC_ERR_SIG;
    uint8_t *mask = op == SIC_STEWARD_OP_APPROVE ? &p->approved : &p->cancel;
    uint8_t bit = (uint8_t) (1u << steward);
    if (*mask & bit) return SIC_DUPLICATE;
    *mask |= bit;
    if (op == SIC_STEWARD_OP_CANCEL && popcount8(p->cancel) >= SIC_STEWARD_THRESHOLD)
        p->state = SIC_PROP_CANCELLED;
    return SIC_OK;
}

sic_status_t sic_steward_approve(sic_steward_t *s, uint32_t id, uint32_t steward,
                                 const uint8_t *sig, uint32_t sig_len, uint64_t tick)
{
    return vote(s, SIC_STEWARD_OP_APPROVE, id, steward, sig, sig_len, tick);
}

sic_status_t sic_steward_cancel(sic_steward_t *s, uint32_t id, uint32_t steward, const uint8_t *sig,
                                uint32_t sig_len, uint64_t tick)
{
    return vote(s, SIC_STEWARD_OP_CANCEL, id, steward, sig, sig_len, tick);
}

sic_status_t sic_steward_execute(sic_steward_t *s, uint32_t id, uint64_t tick)
{
    if (!s) return SIC_ERR_ARG;
    sic_proposal_t *p;
    sic_status_t st = open_at(s, id, tick, &p);
    if (st != SIC_OK) return st;
    if (popcount8(p->approved) < SIC_STEWARD_THRESHOLD) return SIC_ERR_THRESHOLD;
    if (tick - p->proposed < SIC_TIMELOCK) return SIC_ERR_TIMELOCK; /* tick >= proposed */
    p->state = SIC_PROP_EXECUTED;
    return SIC_OK;
}

sic_prop_state_t sic_steward_state(const sic_steward_t *s, uint32_t id, uint64_t tick)
{
    const sic_proposal_t *p = find(s, id);
    if (!p) return SIC_PROP_FREE;
    if (p->state == SIC_PROP_OPEN && expired(s, p, tick)) return SIC_PROP_EXPIRED;
    return (sic_prop_state_t) p->state;
}

uint32_t sic_steward_approvals(const sic_steward_t *s, uint32_t id)
{
    const sic_proposal_t *p = find(s, id);
    return p ? popcount8(p->approved) : 0;
}

/* ---- S5-S7 ---- */
const uint64_t sic_org_floor_default[SIC_ORG_SCALES] = {1000u, 10000u, 100000u, 1000000u,
                                                        10000000u};

void sic_org_manifest_msg(uint32_t org_id, uint8_t scale, uint64_t num, uint64_t den,
                          uint32_t version, uint8_t out[SIC_ORG_MSG])
{
    static const char tag[] = "zxv/sic-org/v1";
    for (uint32_t i = 0; i < 14; i++) out[i] = (uint8_t) tag[i];
    put32(out + 14, org_id);
    out[18] = scale;
    put64(out + 19, num);
    put64(out + 27, den);
    put32(out + 35, version);
}

sic_status_t sic_org_open(sic_org_t *o, uint32_t org_id, uint32_t signer, uint8_t scale,
                          uint64_t num, uint64_t den, uint32_t version, uint64_t deposit,
                          const uint64_t floors[SIC_ORG_SCALES], const uint8_t *sig,
                          uint32_t sig_len, sic_verify_fn verify, void *vctx)
{
    if (!o || !sig || !verify || scale >= SIC_ORG_SCALES || den == 0 || num > den)
        return SIC_ERR_ARG;
    if (!floors) floors = sic_org_floor_default;
    if (deposit < floors[scale]) return SIC_ERR_FUNDS;
    uint8_t m[SIC_ORG_MSG];
    sic_org_manifest_msg(org_id, scale, num, den, version, m);
    if (!verify(vctx, signer, m, sizeof m, sig, sig_len)) return SIC_ERR_SIG;
    pay_memset(o, 0, sizeof *o);
    o->org_id = org_id;
    o->signer = signer;
    o->version = version;
    o->scale = scale;
    o->floor = floors[scale];
    o->escrow = deposit;
    o->sweep_num = num;
    o->sweep_den = den;
    return SIC_OK;
}

sic_status_t sic_org_deposit(sic_org_t *o, uint64_t amount)
{
    if (!o) return SIC_ERR_ARG;
    return pay_add_ok(o->escrow, amount, &o->escrow) ? SIC_OK : SIC_ERR_OVERFLOW;
}

sic_status_t sic_org_sweep(sic_org_t *o, uint64_t gross, uint64_t *fee_out, uint64_t *swept_out)
{
    if (!o || o->sweep_den == 0 || o->sweep_rem >= o->sweep_den) return SIC_ERR_ARG;
    uint64_t fee = pay_assure_fee(gross), net = gross - fee, rem;
    pay_u128 x = pay_u128_add64(pay_mul64(net, o->sweep_num), o->sweep_rem);
    pay_u128 q = pay_udiv128_64(x, o->sweep_den, &rem);
    uint64_t esc, gt;
    /* q <= net while num <= den; fee_total <= gross_total and swept_total <=
     * gross_total, so neither can overflow once gross_total does not */
    if (q.hi || !pay_add_ok(o->escrow, q.lo, &esc) || !pay_add_ok(o->gross_total, gross, &gt))
        return SIC_ERR_OVERFLOW;
    o->escrow = esc;
    o->sweep_rem = rem;
    o->gross_total = gt;
    o->fee_total += fee;
    o->swept_total += q.lo;
    if (fee_out) *fee_out = fee;
    if (swept_out) *swept_out = q.lo;
    return SIC_OK;
}

static sic_member_t *member(const sic_org_t *o, uint32_t user)
{
    for (uint32_t i = 0; i < o->n_members; i++)
        if (o->m[i].user == user) return (sic_member_t *) &o->m[i];
    return 0;
}

sic_status_t sic_org_join(sic_org_t *o, uint32_t user)
{
    if (!o) return SIC_ERR_ARG;
    sic_member_t *m = member(o, user);
    if (!m) {
        if (o->n_members >= SIC_ORG_MAX_MEMBERS) return SIC_ERR_FULL;
        m = &o->m[o->n_members++];
        m->user = user;
        m->points = 0;
    }
    m->joined = true;
    return SIC_OK;
}

sic_status_t sic_org_leave(sic_org_t *o, uint32_t user)
{
    if (!o) return SIC_ERR_ARG;
    sic_member_t *m = member(o, user);
    if (!m || !m->joined) return SIC_ERR_MEMBER;
    m->joined = false; /* earned points stay redeemable */
    return SIC_OK;
}

sic_status_t sic_org_award(sic_org_t *o, uint32_t user, uint64_t points)
{
    if (!o || points == 0) return SIC_ERR_ARG;
    sic_member_t *m = member(o, user);
    if (!m || !m->joined) return SIC_ERR_MEMBER;
    uint64_t need;
    if (!pay_add_ok(o->floor, o->owed, &need) || !pay_add_ok(need, points, &need) ||
        o->escrow < need)
        return SIC_ERR_PAUSED;
    /* floor + owed + points fits, and m->points <= owed: neither sum wraps */
    o->owed += points;
    m->points += points;
    return SIC_OK;
}

sic_status_t sic_org_redeem(sic_org_t *o, uint32_t user, uint64_t points)
{
    if (!o || points == 0) return SIC_ERR_ARG;
    sic_member_t *m = member(o, user);
    if (!m) return SIC_ERR_MEMBER;
    /* m->points <= owed always; escrow >= owed unless the state was corrupted */
    if (points > m->points || points > o->escrow) return SIC_ERR_FUNDS;
    m->points -= points;
    o->owed -= points;
    o->escrow -= points;
    return SIC_OK;
}

uint64_t sic_org_points(const sic_org_t *o, uint32_t user)
{
    const sic_member_t *m = o ? member(o, user) : 0;
    return m ? m->points : 0;
}

bool sic_org_paused(const sic_org_t *o)
{
    uint64_t need;
    if (!o) return true;
    return !pay_add_ok(o->floor, o->owed, &need) || o->escrow < need;
}
