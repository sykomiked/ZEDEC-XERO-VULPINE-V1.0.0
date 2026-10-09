/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pay_farm.c — VFV issuance for verified work. See pay_farm.h. */
#include "pay_farm.h"

void pay_farm_cfg_default(pay_farm_cfg_t *c)
{
    if (!c) return;
    pay_memset(c, 0, sizeof *c);
    for (uint32_t k = 0; k < PAY_WORK_KIND_COUNT; k++) {
        c->rate[k].num = 0;
        c->rate[k].den = 1;
    }
    c->sample_rate.num = 1;
    c->sample_rate.den = 8;
    c->cap_share.num = SWARM_MKT_FLOOR_NUM; /* 8  */
    c->cap_share.den = SWARM_MKT_DEN;       /* 21 */
    c->max_strikes = 3;
}

pay_status_t pay_farm_init(pay_farm_ctx_t *F, pay_ledger_t *L, const pay_farm_cfg_t *cfg,
                           uint32_t platform_owner, uint32_t commons_acct,
                           pay_farm_verify_fn verify, pay_farm_replicate_fn replicate, void *cb_ctx,
                           pay_equity_t *eq)
{
    if (!F || !L || !cfg || !verify || !replicate) return PAY_ERR_ARG;
    if (cfg->sample_rate.den == 0 || cfg->cap_share.den == 0 || cfg->max_strikes == 0)
        return PAY_ERR_ARG;
    for (uint32_t k = 0; k < PAY_WORK_KIND_COUNT; k++)
        if (cfg->rate[k].den == 0) return PAY_ERR_ARG;
    const pay_account_t *ca = pay_ledger_account(L, commons_acct);
    if (!ca || ca->asset != L->vfv_asset || !(ca->flags & PAY_ACCT_COMMONS))
        return PAY_ERR_NO_ACCOUNT;
    pay_memset(F, 0, sizeof *F);
    F->L = L;
    F->eq = eq;
    F->cfg = *cfg;
    F->verify = verify;
    F->replicate = replicate;
    F->cb_ctx = cb_ctx;
    F->commons_acct = commons_acct;
    F->commons_owner = ca->owner;
    return pay_ledger_open(L, platform_owner, L->vfv_asset, PAY_CAP_FINANCIAL, PAY_ACCT_ISSUER,
                           &F->issuer_acct);
}

static pay_farm_t *farm_of(pay_farm_ctx_t *F, uint32_t owner)
{
    for (uint32_t i = 0; i < F->n_farms; i++)
        if (F->farm[i].active && F->farm[i].owner == owner) return &F->farm[i];
    return 0;
}

const pay_farm_t *pay_farm_get(const pay_farm_ctx_t *F, uint32_t owner)
{
    return F ? farm_of((pay_farm_ctx_t *) F, owner) : 0;
}

pay_status_t pay_farm_register(pay_farm_ctx_t *F, uint32_t owner, const uint32_t *path)
{
    if (!F) return PAY_ERR_ARG;
    if (farm_of(F, owner)) return PAY_DUPLICATE;
    if (F->n_farms >= PAY_FARM_MAX) return PAY_ERR_FULL;
    pay_farm_t *f = &F->farm[F->n_farms];
    pay_memset(f, 0, sizeof *f);
    pay_status_t st =
        pay_ledger_open(F->L, owner, F->L->vfv_asset, PAY_CAP_FINANCIAL, 0, &f->vfv_acct);
    if (st != PAY_OK) return st;
    f->active = true;
    f->owner = owner;
    for (uint32_t i = 0; i < PAY_FARM_PATH; i++) f->path[i] = path ? path[i] : F->commons_owner;
    F->n_farms++;
    return PAY_OK;
}

pay_status_t pay_farm_set_rate(pay_farm_ctx_t *F, pay_work_kind_t kind, pay_rat_t rate)
{
    if (!F || (unsigned) kind >= PAY_WORK_KIND_COUNT || rate.den == 0) return PAY_ERR_ARG;
    F->cfg.rate[kind] = rate;
    return PAY_OK;
}

bool pay_farm_rate_from_swarm(const swarm_market_t *m, pay_rat_t scale, pay_rat_t *out)
{
    if (!m || !out || scale.den == 0 || m->pot == 0 || m->last_market == 0) return false;
    pay_u128 n = pay_mul64(m->pot, scale.num), d = pay_mul64(m->last_market, scale.den);
    if (n.hi || d.hi || n.lo == 0) return false;
    out->num = n.lo;
    out->den = d.lo;
    return true;
}

/* ===== Receipts ===== */

void pay_farm_receipt_digest(const pay_work_receipt_t *r, uint8_t out[32])
{
    pay_hbuf h;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-FARM-RECEIPT-v1");
    pay_hbuf_u64(&h, r->farm);
    pay_hbuf_put(&h, r->job_id, 32);
    pay_hbuf_u64(&h, r->kind);
    pay_hbuf_u64(&h, r->qty);
    pay_hbuf_u64(&h, r->period);
    pay_hbuf_put(&h, r->result, 32);
    pay_hbuf_final(&h, out);
}

static uint64_t le64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* Replay table: open addressing on SHA3(job id). Returns the slot holding
 * job, or the empty slot where it would go, or -1 when full and absent. */
static int32_t seen_slot(const pay_farm_ctx_t *F, const uint8_t job[32])
{
    uint8_t h[32];
    pay_sha3_256(job, 32, h);
    uint32_t i = (uint32_t) le64(h) & (PAY_FARM_SEEN - 1u);
    for (uint32_t k = 0; k < PAY_FARM_SEEN; k++) {
        uint32_t s = (i + k) & (PAY_FARM_SEEN - 1u);
        if (!F->seen_used[s]) return (int32_t) s;
        if (pay_memeq(F->seen[s], job, 32)) return (int32_t) s;
    }
    return -1;
}

pay_status_t pay_farm_submit(pay_farm_ctx_t *F, const pay_work_receipt_t *r, const uint8_t *sig,
                             size_t sig_len)
{
    if (!F || !r || !sig || sig_len == 0) return PAY_ERR_ARG;
    pay_farm_t *f = farm_of(F, r->farm);
    if (!f) return PAY_ERR_NO_ACCOUNT;
    if (f->suspended) return PAY_ERR_POLICY;
    if (r->kind >= PAY_WORK_KIND_COUNT || r->qty == 0 || r->qty >= PAY_BAL_MAX) return PAY_ERR_ARG;
    if (r->period != F->period) return PAY_ERR_STATE;
    int32_t s = seen_slot(F, r->job_id);
    if (s < 0) return PAY_ERR_FULL;
    if (F->seen_used[s]) return PAY_ERR_REPLAY;
    if (F->n_queue >= PAY_FARM_QUEUE) return PAY_ERR_FULL;
    uint8_t d[32];
    pay_farm_receipt_digest(r, d);
    if (!F->verify(F->cb_ctx, r->farm, d, sig, sig_len)) {
        F->stats.refused++;
        return PAY_ERR_POLICY;
    }
    pay_memcpy(F->seen[s], r->job_id, 32);
    F->seen_used[s] = 1;
    F->n_seen++;
    pay_farm_q_t *q = &F->queue[F->n_queue++];
    q->r = *r;
    pay_memcpy(q->digest, d, 32);
    return PAY_OK;
}

bool pay_farm_sampled(const uint8_t beacon[32], const uint8_t job_id[32], pay_rat_t rate)
{
    if (rate.den == 0 || rate.num == 0) return false;
    if (rate.num >= rate.den) return true;
    pay_hbuf h;
    uint8_t d[32];
    uint64_t rem;
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-FARM-SAMPLE-v1");
    pay_hbuf_put(&h, beacon, 32);
    pay_hbuf_put(&h, job_id, 32);
    pay_hbuf_final(&h, d);
    pay_udiv64(le64(d), rate.den, &rem);
    return rem < rate.num;
}

/* ===== The W4 cap ===== */

static bool cap_ok(const uint64_t *e, uint32_t n, pay_rat_t s, uint64_t c)
{
    uint64_t sum = 0;
    for (uint32_t j = 0; j < n; j++) sum += e[j] < c ? e[j] : c; /* < 2^62 total */
    return pay_u128_cmp(pay_mul64(c, s.den), pay_mul64(s.num, sum)) <= 0;
}

uint64_t pay_farm_cap(const uint64_t *e, uint32_t n, pay_rat_t share)
{
    uint32_t nz = 0;
    uint64_t mx = 0;
    if (!e || n == 0 || share.den == 0) return 0;
    for (uint32_t j = 0; j < n; j++) {
        if (e[j]) nz++;
        if (e[j] > mx) mx = e[j];
    }
    if (nz == 0) return 0;
    /* effective share max(share, 1/nz) */
    if (pay_u128_cmp(pay_mul64(share.num, nz), pay_u128_from(share.den)) < 0) {
        share.num = 1;
        share.den = nz;
    }
    if (cap_ok(e, n, share, mx)) return mx;
    uint64_t lo = 0, hi = mx; /* lo feasible, hi infeasible */
    while (hi - lo > 1) {
        uint64_t mid = lo + ((hi - lo) >> 1);
        if (cap_ok(e, n, share, mid))
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

/* ===== Close a period ===== */

static void farm_ids(pay_farm_ctx_t *F, pay_posting_req_t *rq, uint32_t owner, uint64_t tick)
{
    pay_hbuf h;
    uint8_t d[32];
    pay_w w;
    pay_memset(rq, 0, sizeof *rq);
    pay_hbuf_init(&h);
    pay_hbuf_str(&h, "ZXV-FARM-MINT-v1");
    pay_hbuf_u64(&h, F->period);
    pay_hbuf_u64(&h, owner);
    pay_hbuf_u64(&h, F->L->seq);
    pay_hbuf_final(&h, d);
    pay_memcpy(rq->idem_key, d, 32);
    pay_uetr_from_random(d, rq->uetr);
    pay_w_init(&w, rq->e2e, sizeof rq->e2e);
    pay_w_s(&w, "FM");
    pay_w_hex(&w, d + 16, 12);
    pay_w_finish(&w);
    rq->tick = tick;
    rq->initiator = owner;
    rq->kind = PAY_KIND_ISSUE;
    rq->ext_cap = PAY_CAP_SYSTEM;
}

pay_status_t pay_farm_close_period(pay_farm_ctx_t *F, const uint8_t beacon[32], uint64_t tick)
{
    if (!F || !beacon) return PAY_ERR_ARG;
    uint32_t keep = 0;

    /* W2 + W3: spot checks and values */
    for (uint32_t i = 0; i < F->n_queue; i++) {
        pay_farm_q_t *q = &F->queue[i];
        pay_farm_t *f = farm_of(F, q->r.farm);
        if (!f || f->suspended) {
            F->stats.refused++;
            continue;
        }
        if (pay_farm_sampled(beacon, q->r.job_id, F->cfg.sample_rate)) {
            uint8_t res[32];
            F->stats.sampled++;
            if (!F->replicate(F->cb_ctx, &q->r, res)) {
                F->queue[keep++] = *q; /* no replica: try again next period */
                F->stats.carried++;
                continue;
            }
            if (!pay_memeq(res, q->r.result, 32)) {
                F->stats.mismatches++;
                f->tainted = true;
                continue;
            }
        }
        pay_rat_t rt = F->cfg.rate[q->r.kind];
        uint64_t v = 0, np;
        if (rt.num && !pay_muldiv(q->r.qty, rt.num, rt.den, &v, 0)) v = 0;
        if (v >= PAY_BAL_MAX || !pay_add_ok(f->pending, v, &np) || np >= PAY_BAL_MAX) {
            F->stats.refused++;
            continue;
        }
        f->pending = np;
        F->stats.verified_receipts++;
    }
    F->n_queue = keep;

    /* penalties */
    uint64_t e[PAY_FARM_MAX], total = 0;
    for (uint32_t j = 0; j < F->n_farms; j++) {
        pay_farm_t *f = &F->farm[j];
        if (f->tainted) {
            f->forfeited += f->pending;
            f->pending = 0;
            f->strikes++;
            if (f->strikes >= F->cfg.max_strikes) f->suspended = true;
            f->tainted = false;
        }
        if (f->suspended) {
            f->forfeited += f->pending;
            f->pending = 0;
        }
        f->verified += f->pending;
        e[j] = f->pending;
        if (total + e[j] >= PAY_BAL_MAX) { /* keep the cap arithmetic in range */
            f->verified -= f->pending;
            f->forfeited += f->pending;
            f->pending = e[j] = 0;
        }
        total += e[j];
    }

    /* W4 + W5 */
    uint64_t c = pay_farm_cap(e, F->n_farms, F->cfg.cap_share);
    pay_status_t err = PAY_OK;
    for (uint32_t j = 0; j < F->n_farms; j++) {
        pay_farm_t *f = &F->farm[j];
        if (!e[j]) continue;
        uint64_t g = e[j] < c ? e[j] : c;
        f->capped += e[j] - g;
        f->pending = 0;
        if (!g) continue;
        uint64_t t = pay_tithe_phi(g), net = g - t;
        pay_posting_req_t rq;
        farm_ids(F, &rq, f->owner, tick);
        uint32_t n = 0;
        rq.lines[n].account = F->issuer_acct;
        rq.lines[n].d_debit = 0;
        rq.lines[n++].d_credit = (int64_t) g;
        if (net) {
            rq.lines[n].account = f->vfv_acct;
            rq.lines[n].d_debit = (int64_t) net;
            rq.lines[n++].d_credit = 0;
        }
        if (t) {
            rq.lines[n].account = F->commons_acct;
            rq.lines[n].d_debit = (int64_t) t;
            rq.lines[n++].d_credit = 0;
        }
        rq.n_lines = n;
        pay_status_t st = pay_ledger_post(F->L, &rq, 0);
        if (st != PAY_OK) {
            f->verified -= g; /* not minted: give the value back as forfeited */
            f->forfeited += g;
            err = st;
            continue;
        }
        f->minted += g;
        F->minted += g;
        F->tithed += t;
        F->net += net;
        if (F->eq && F->eq->vfv.ready) {
            if (net && pay_vfv_equity_on_mint(F->eq, f->owner, f->path, net) != PAY_OK)
                F->stats.equity_errors++;
            if (t && pay_vfv_equity_on_mint(F->eq, F->commons_owner, f->path, t) != PAY_OK)
                F->stats.equity_errors++;
        }
    }
    F->period++;
    return err;
}

pay_status_t pay_farm_burn(pay_farm_ctx_t *F, uint32_t holder_acct, uint64_t amount, uint64_t tick)
{
    if (!F || amount == 0) return PAY_ERR_ARG;
    if (amount > F->minted - F->burned) return PAY_ERR_FUNDS;
    const pay_account_t *a = pay_ledger_account(F->L, holder_acct);
    if (!a || a->asset != F->L->vfv_asset) return PAY_ERR_NO_ACCOUNT;
    pay_posting_req_t rq;
    farm_ids(F, &rq, a->owner, tick);
    rq.e2e[0] = 'F';
    rq.e2e[1] = 'B';
    pay_status_t st = pay_ledger_redeem(F->L, &rq, holder_acct, F->issuer_acct, amount, 0);
    if (st == PAY_OK) F->burned += amount;
    return st;
}

bool pay_farm_audit(const pay_farm_ctx_t *F)
{
    if (!F) return false;
    uint64_t sum = 0;
    for (uint32_t j = 0; j < F->n_farms; j++) {
        const pay_farm_t *f = &F->farm[j];
        if (f->minted > f->verified) return false; /* I1 */
        if (f->pending) return false;              /* nothing left unminted */
        sum += f->minted;
    }
    if (sum != F->minted || F->minted != F->tithed + F->net) return false; /* I3 */
    if (F->burned > F->minted) return false;
    const pay_account_t *ia = pay_ledger_account(F->L, F->issuer_acct);
    if (!ia || ia->credit != F->minted - F->burned) return false; /* I2 */
    return pay_ledger_check(F->L);                                /* I4 */
}
