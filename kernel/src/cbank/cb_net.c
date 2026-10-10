/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* cb_net.c — see cb_net.h for the model and the conservation invariants. */
#include "cb_net.h"
#include "cb_ccy.h"
#include "cb_util.h"

#define NO_INDEX 0xffffffffu

static int ccy_index(const cb_config *c, const char *alpha)
{
    for (uint32_t i = 0; i < c->n_ccy; i++)
        if (cb_streq(c->ccy[i].alpha, alpha)) return (int) i;
    return -1;
}

int cb_net_init(cb_net *e, const cb_config *cfg)
{
    if (!e || !cfg) return CB_E_NULL;
    int r = cb_config_validate(cfg);
    if (r != CB_OK) return r;
    cb_memset(e, 0, sizeof *e);
    e->cfg = cfg;
    return CB_OK;
}

int cb_net_find_participant(const cb_net *e, const char *bic)
{
    if (!e || !bic) return CB_E_NULL;
    for (uint32_t i = 0; i < e->n_part; i++)
        if (cb_streq(e->part[i].bic, bic)) return (int) i;
    return CB_E_PART;
}

int cb_net_add_participant(cb_net *e, const char *bic, const char *country, const char *ccy,
                           cb_role role, int64_t prefund)
{
    if (!e || !bic || !country || !ccy) return CB_E_NULL;
    if (!cb_bic_valid(bic)) return CB_E_BIC;
    if (!cb_country_by_a2(country)) return CB_E_COUNTRY;
    int ci = ccy_index(e->cfg, ccy);
    if (ci < 0) return CB_E_UNKNOWN_CCY;
    const cb_ccy_profile *p = &e->cfg->ccy[ci];
    if (!(p->cfm & CB_CFM_NONRESIDENT) && !cb_streq(country, p->issuer_country))
        return CB_E_COUNTRY;
    cb_role_profile rp;
    if (cb_role_default(role, &rp) != CB_OK) return CB_E_ROLE;
    if (!(rp.perms & CB_PERM_HOLD_PREFUND)) return CB_E_PERM;
    if (prefund < 0) return CB_E_RESERVE;
    if (cb_net_find_participant(e, bic) >= 0) return CB_E_DUP;
    if (e->n_part >= CB_NET_MAX_PART) return CB_E_FULL;
    cb_participant *q = &e->part[e->n_part];
    cb_memset(q, 0, sizeof *q);
    cb_strlcpy(q->bic, bic, sizeof q->bic);
    cb_strlcpy(q->country, country, sizeof q->country);
    cb_strlcpy(q->ccy, ccy, sizeof q->ccy);
    q->role = (uint8_t) role;
    q->prefund = prefund;
    q->active = !p->prefund_required || (uint64_t) prefund >= p->min_prefund;
    return (int) e->n_part++;
}

int cb_net_set_active(cb_net *e, uint16_t part, bool active)
{
    if (!e) return CB_E_NULL;
    if (part >= e->n_part) return CB_E_PART;
    e->part[part].active = active;
    return CB_OK;
}

int cb_net_prefund(cb_net *e, uint16_t part, int64_t delta)
{
    if (!e) return CB_E_NULL;
    if (part >= e->n_part) return CB_E_PART;
    int64_t v;
    if (!cb_sadd_ok(e->part[part].prefund, delta, &v) || v < 0) return CB_E_RESERVE;
    /* A withdrawal may not leave the open position uncovered. */
    if (delta < 0) {
        int64_t t;
        if (!cb_sadd_ok(v, e->pos[part], &t) || t < 0) return CB_E_RESERVE;
    }
    e->part[part].prefund = v;
    return CB_OK;
}

int cb_net_rate_submit(cb_net *e, const cb_rate_rec *r, uint64_t now)
{
    if (!e || !r) return CB_E_NULL;
    int rc = cb_rate_check(e->cfg, r, now);
    if (rc != CB_OK) return rc;
    int ci = ccy_index(e->cfg, r->ccy);
    if (ci < 0) return CB_E_UNKNOWN_CCY;
    if (e->have_rate[ci]) {
        const cb_rate_rec *o = &e->rate[ci];
        if (o->source_id == r->source_id ? r->seq <= o->seq : r->published_at < o->published_at)
            return CB_E_REPLAY;
    }
    cb_memcpy(&e->rate[ci], r, sizeof *r);
    e->have_rate[ci] = true;
    return CB_OK;
}

int cb_net_open_cycle(cb_net *e, uint64_t now)
{
    (void) now;
    if (!e) return CB_E_NULL;
    if (e->cycle_open) return CB_E_CYCLE;
    e->cycle++;
    e->cycle_open = true;
    return CB_OK;
}

/* ===== conversion ===== */

static bool is_unit(const cb_net *e, int ci)
{
    return cb_streq(e->cfg->ccy[ci].alpha, e->cfg->settle_unit);
}

static bool rate_of(const cb_net *e, int ci, uint64_t now, uint64_t *m, uint32_t *s)
{
    if (is_unit(e, ci)) {
        *m = 1;
        *s = 0;
        return true;
    }
    if (!e->have_rate[ci] || !cb_rate_fresh(e->cfg, &e->rate[ci], now)) return false;
    *m = e->rate[ci].mant;
    *s = e->rate[ci].scale;
    return true;
}

static uint8_t unit_minor(const cb_net *e)
{
    return e->cfg->ccy[ccy_index(e->cfg, e->cfg->settle_unit)].minor;
}

/* 0 ok, 1 no fresh rate, 2 arithmetic overflow / zero result */
static int convert(const cb_net *e, int cs, int cr, uint64_t send, uint64_t now, uint64_t *u,
                   uint64_t *recv, uint64_t *rem1, uint64_t *den1, uint64_t *rem2, uint64_t *den2)
{
    *rem1 = *rem2 = 0;
    *den1 = *den2 = 1;
    if (cs == cr) {
        *u = 0;
        *recv = send;
        return 0;
    }
    uint64_t ms, mr;
    uint32_t ss, sr;
    if (!rate_of(e, cs, now, &ms, &ss) || !rate_of(e, cr, now, &mr, &sr)) return 1;
    uint32_t eu = unit_minor(e), es = e->cfg->ccy[cs].minor, er = e->cfg->ccy[cr].minor;
    uint64_t d1 = cb_pow10(ss + es);
    if (!d1 || !cb_muldiv_p10(send, ms, eu, d1, u, rem1)) return 2;
    *den1 = d1;
    uint64_t d2 = mr * cb_pow10(eu); /* mr <= 10^12, eu <= 4: fits */
    if (!cb_muldiv_p10(*u, cb_pow10(sr), er, d2, recv, rem2)) return 2;
    *den2 = d2;
    if (*u == 0 || *recv == 0) return 2;
    return 0;
}

int cb_net_quote(const cb_net *e, uint16_t s, uint16_t r, uint64_t amount, uint64_t now,
                 uint64_t *recv, uint64_t *unit, bool *exact)
{
    if (!e || !recv || !unit || !exact) return CB_E_NULL;
    if (s >= e->n_part || r >= e->n_part) return CB_E_PART;
    int cs = ccy_index(e->cfg, e->part[s].ccy), cr = ccy_index(e->cfg, e->part[r].ccy);
    uint64_t r1, d1, r2, d2;
    int c = convert(e, cs, cr, amount, now, unit, recv, &r1, &d1, &r2, &d2);
    if (c == 1) return CB_E_STALE;
    if (c == 2) return CB_E_RATE;
    *exact = r1 == 0 && r2 == 0;
    return CB_OK;
}

/* ===== liquidity ===== */

static bool covered(const cb_net *e, uint16_t p, uint64_t debit)
{
    const cb_participant *q = &e->part[p];
    const cb_ccy_profile *k = &e->cfg->ccy[ccy_index(e->cfg, q->ccy)];
    uint64_t reserve = 0;
    if (q->prefund > 0 && k->reserve_bps)
        cb_muldiv_p10((uint64_t) q->prefund, k->reserve_bps, 0, 10000u, &reserve, 0);
    if (k->net_debit_cap > CB_AMT_MAX * 1000u || debit > CB_AMT_MAX) return false;
    int64_t avail = q->prefund - (int64_t) reserve + (int64_t) k->net_debit_cap + e->pos[p];
    return avail >= (int64_t) debit;
}

static bool book(cb_net *e, uint16_t s, uint16_t r, uint64_t send, uint64_t recv, uint64_t u)
{
    int cs = ccy_index(e->cfg, e->part[s].ccy), cr = ccy_index(e->cfg, e->part[r].ccy);
    int64_t a = (int64_t) send, b = (int64_t) recv, c = (int64_t) u;
    int64_t ps, as, ups, aus, pr, ar, upr, aur;
    if (!cb_sadd_ok(e->pos[s], -a, &ps) || !cb_sadd_ok(e->agent[cs], a, &as) ||
        !cb_sadd_ok(e->upos[s], -c, &ups) || !cb_sadd_ok(e->aupos[cs], c, &aus))
        return false;
    e->pos[s] = ps;
    e->agent[cs] = as;
    e->upos[s] = ups;
    e->aupos[cs] = aus;
    if (!cb_sadd_ok(e->pos[r], b, &pr) || !cb_sadd_ok(e->agent[cr], -b, &ar) ||
        !cb_sadd_ok(e->upos[r], c, &upr) || !cb_sadd_ok(e->aupos[cr], -c, &aur)) {
        /* undo the first leg; cannot overflow, it restores prior values */
        e->pos[s] += a;
        e->agent[cs] -= a;
        e->upos[s] += c;
        e->aupos[cs] -= c;
        return false;
    }
    e->pos[r] = pr;
    e->agent[cr] = ar;
    e->upos[r] = upr;
    e->aupos[cr] = aur;
    return true;
}

/* ===== records ===== */

const cb_pay_rec *cb_net_find(const cb_net *e, const char *msg_id)
{
    if (!e || !msg_id) return 0;
    for (uint32_t i = 0; i < e->n_pay; i++)
        if (cb_streq(e->pay[i].req.msg_id, msg_id)) return &e->pay[i];
    return 0;
}

static bool same_req(const cb_pay_req *a, const cb_pay_req *b)
{
    return cb_streq(a->msg_id, b->msg_id) && cb_streq(a->e2e_id, b->e2e_id) &&
           cb_streq(a->uetr, b->uetr) && a->dbtr_agt == b->dbtr_agt && a->cdtr_agt == b->cdtr_agt &&
           a->amount == b->amount && cb_streq(a->purpose, b->purpose) &&
           cb_streq(a->dbtr_name, b->dbtr_name) && cb_streq(a->cdtr_name, b->cdtr_name) &&
           cb_streq(a->dbtr_ctry, b->dbtr_ctry) && cb_streq(a->cdtr_ctry, b->cdtr_ctry) &&
           a->fi_transfer == b->fi_transfer;
}

static bool id_ok(const char *s, size_t cap)
{
    size_t n = cb_strnlen(s, cap);
    return n > 0 && n < cap;
}

static void copy_req(cb_pay_req *d, const cb_pay_req *s)
{
    cb_memset(d, 0, sizeof *d);
    cb_strlcpy(d->msg_id, s->msg_id, sizeof d->msg_id);
    cb_strlcpy(d->e2e_id, s->e2e_id, sizeof d->e2e_id);
    cb_strlcpy(d->uetr, s->uetr, sizeof d->uetr);
    d->dbtr_agt = s->dbtr_agt;
    d->cdtr_agt = s->cdtr_agt;
    d->amount = s->amount;
    cb_strlcpy(d->purpose, s->purpose, sizeof d->purpose);
    cb_strlcpy(d->dbtr_name, s->dbtr_name, sizeof d->dbtr_name);
    cb_strlcpy(d->cdtr_name, s->cdtr_name, sizeof d->cdtr_name);
    cb_strlcpy(d->dbtr_ctry, s->dbtr_ctry, sizeof d->dbtr_ctry);
    cb_strlcpy(d->cdtr_ctry, s->cdtr_ctry, sizeof d->cdtr_ctry);
    d->fi_transfer = s->fi_transfer;
}

static void fill_res(cb_pay_result *res, const cb_pay_rec *p, uint32_t idx, bool dup)
{
    res->status = p->status;
    cb_strlcpy(res->reason, p->reason, sizeof res->reason);
    res->duplicate = dup;
    res->index = idx;
    res->send = p->send;
    res->recv = p->recv;
    res->unit = p->unit;
}

static void reject_res(cb_pay_result *res, const char *code)
{
    cb_memset(res, 0, sizeof *res);
    res->status = CB_PS_REJECTED;
    cb_strlcpy(res->reason, code, sizeof res->reason);
    res->index = NO_INDEX;
}

/* Decide a new payment. Returns a reason code, or 0 when accepted. */
static const char *decide(cb_net *e, cb_pay_rec *p, uint64_t now, int *corr_ix)
{
    const cb_pay_req *q = &p->req;
    const cb_config *c = e->cfg;
    *corr_ix = -1;
    if (!e->cycle_open) return "TM01";
    if (q->dbtr_agt >= e->n_part || q->cdtr_agt >= e->n_part || q->dbtr_agt == q->cdtr_agt)
        return "RC01";
    const cb_participant *s = &e->part[q->dbtr_agt], *r = &e->part[q->cdtr_agt];
    if (!s->active || !r->active) return "AC06";
    cb_role_profile rp;
    cb_role_default((cb_role) s->role, &rp);
    if (!(rp.perms & (q->fi_transfer ? CB_PERM_SEND_FI : CB_PERM_SEND_CUSTOMER))) return "AG01";
    if (q->amount == 0) return "AM01";
    if (q->amount > CB_AMT_MAX) return "AM02";
    if (rp.per_payment_max && q->amount > rp.per_payment_max) return "AM02";
    if ((q->dbtr_ctry[0] && !cb_country_by_a2(q->dbtr_ctry)) ||
        (q->cdtr_ctry[0] && !cb_country_by_a2(q->cdtr_ctry)))
        return "BE09"; /* invalid country */
    int cs = ccy_index(c, s->ccy), cr = ccy_index(c, r->ccy);
    const cb_ccy_profile *ks = &c->ccy[cs], *kr = &c->ccy[cr];
    if ((ks->cfm | kr->cfm) & CB_CFM_HALT) return "RR04";
    if (cs != cr && (!(ks->cfm & CB_CFM_OUTBOUND) || !(kr->cfm & CB_CFM_INBOUND))) return "RR04";
    if (((ks->cfm | kr->cfm) & CB_CFM_PURPOSE_REQ) && !q->purpose[0]) return "RR04";
    if (cs != cr && ks->cfm_max_outbound && q->amount > ks->cfm_max_outbound) return "AM02";
    if (cb_business_status(ks, now) != CB_BIZ_OPEN || cb_business_status(kr, now) != CB_BIZ_OPEN)
        return "TM01";
    if (cs != cr) {
        const cb_corridor *k = cb_config_corridor(c, s->ccy, r->ccy);
        if (!k) {
            if (c->corridor_default_deny) return "AG01";
        } else {
            *corr_ix = (int) (k - c->corr);
            if (!k->enabled) return "AG01";
            if (k->per_payment_max && q->amount > k->per_payment_max) return "AM02";
            uint64_t used;
            if (!cb_add_ok(e->corr_used[*corr_ix], q->amount, &used)) return "AM14";
            if (k->per_cycle_max && used > k->per_cycle_max) return "AM14";
        }
    }
    if (c->rp.screening_required) {
        if (!c->screen) return "MS03";
        cb_screen_req sr = {q->msg_id,    s->bic,       r->bic, q->dbtr_ctry, q->cdtr_ctry,
                            q->dbtr_name, q->cdtr_name, s->ccy, q->amount,    q->purpose};
        int v = c->screen(c->screen_ctx, &sr);
        if (v == CB_SCREEN_HIT || v == CB_SCREEN_REVIEW) return "RR04";
        if (v != CB_SCREEN_CLEAR) return "MS03";
    }
    int cv = convert(e, cs, cr, q->amount, now, &p->unit, &p->recv, &p->rem1, &p->den1, &p->rem2,
                     &p->den2);
    if (cv == 1) return "MS03"; /* no fresh signed rate */
    if (cv == 2) return "AM12";
    if ((p->rem1 || p->rem2) && (ks->rounding == CB_ROUND_EXACT || kr->rounding == CB_ROUND_EXACT))
        return "AM12";
    p->send = q->amount;
    if (!covered(e, q->dbtr_agt, p->send)) return "AM04";
    return 0;
}

int cb_net_submit(cb_net *e, const cb_pay_req *req, uint64_t now, cb_pay_result *res)
{
    if (!e || !req || !res) return CB_E_NULL;
    if (!id_ok(req->msg_id, CB_ID_LEN) || !id_ok(req->e2e_id, CB_ID_LEN)) return CB_E_ID;
    for (uint32_t i = 0; i < e->n_pay; i++) {
        if (!cb_streq(e->pay[i].req.msg_id, req->msg_id)) continue;
        if (same_req(&e->pay[i].req, req))
            fill_res(res, &e->pay[i], i, true);
        else
            reject_res(res, "DUPL");
        return CB_OK;
    }
    if (e->n_pay >= CB_NET_MAX_PAY) return CB_E_FULL;
    cb_pay_rec *p = &e->pay[e->n_pay];
    cb_memset(p, 0, sizeof *p);
    copy_req(&p->req, req);
    p->cycle = e->cycle;
    int corr = -1;
    const char *why = decide(e, p, now, &corr);
    if (!why && !book(e, req->dbtr_agt, req->cdtr_agt, p->send, p->recv, p->unit)) why = "AM02";
    if (why) {
        p->status = CB_PS_REJECTED;
        cb_strlcpy(p->reason, why, sizeof p->reason);
        p->send = p->recv = p->unit = 0;
    } else {
        p->status = CB_PS_ACCEPTED;
        p->accepted_at = now;
        /* decide() already refused AM14 when corr_used + amount would wrap */
        if (corr >= 0) (void) cb_add_ok(e->corr_used[corr], p->send, &e->corr_used[corr]);
    }
    fill_res(res, p, e->n_pay, false);
    e->n_pay++;
    return CB_OK;
}

static uint32_t rec_index(const cb_net *e, const char *msg_id)
{
    for (uint32_t i = 0; i < e->n_pay; i++)
        if (cb_streq(e->pay[i].req.msg_id, msg_id)) return i;
    return NO_INDEX;
}

int cb_net_return(cb_net *e, const char *orig_msg_id, const char *rtr_id, const char *reason,
                  uint64_t now, cb_pay_result *res)
{
    if (!e || !orig_msg_id || !rtr_id || !reason || !res) return CB_E_NULL;
    cb_memset(res, 0, sizeof *res);
    res->index = NO_INDEX;
    if (!id_ok(rtr_id, CB_ID_LEN) || cb_strnlen(reason, 5) != 4) return CB_E_ID;
    uint32_t ix = rec_index(e, orig_msg_id);
    for (uint32_t i = 0; i < e->n_pay; i++) {
        if (!cb_streq(e->pay[i].rtr_id, rtr_id)) continue;
        if (i != ix) {
            cb_strlcpy(res->reason, "DUPL", sizeof res->reason);
            return CB_E_DUP;
        }
        fill_res(res, &e->pay[i], i, true);
        return CB_OK;
    }
    if (ix == NO_INDEX) {
        cb_strlcpy(res->reason, "NOOR", sizeof res->reason);
        return CB_E_ID;
    }
    cb_pay_rec *p = &e->pay[ix];
    if (p->status != CB_PS_ACCEPTED && p->status != CB_PS_SETTLED) {
        cb_strlcpy(res->reason, p->status == CB_PS_REJECTED ? "ARJR" : "ARDT", sizeof res->reason);
        return CB_E_CYCLE;
    }
    const cb_participant *s = &e->part[p->req.dbtr_agt], *r = &e->part[p->req.cdtr_agt];
    const cb_ccy_profile *ks = cb_config_ccy_c(e->cfg, s->ccy),
                         *kr = cb_config_ccy_c(e->cfg, r->ccy);
    if (!e->cycle_open || cb_business_status(ks, now) != CB_BIZ_OPEN ||
        cb_business_status(kr, now) != CB_BIZ_OPEN) {
        cb_strlcpy(res->reason, "TM01", sizeof res->reason);
        return CB_E_CYCLE;
    }
    if (!covered(e, p->req.cdtr_agt, p->recv)) {
        cb_strlcpy(res->reason, "AM04", sizeof res->reason);
        return CB_E_RESERVE;
    }
    if (!book(e, p->req.cdtr_agt, p->req.dbtr_agt, p->recv, p->send, p->unit)) {
        cb_strlcpy(res->reason, "AM02", sizeof res->reason);
        return CB_E_RESERVE;
    }
    p->status = CB_PS_RETURNED;
    cb_strlcpy(p->rtr_id, rtr_id, sizeof p->rtr_id);
    cb_strlcpy(p->rtr_reason, reason, sizeof p->rtr_reason);
    fill_res(res, p, ix, false);
    return CB_OK;
}

int cb_net_cancel(cb_net *e, const char *orig_msg_id, const char *case_id, uint64_t now,
                  bool *accepted, char reason[5])
{
    (void) now;
    if (!e || !orig_msg_id || !case_id || !accepted || !reason) return CB_E_NULL;
    *accepted = false;
    reason[0] = 0;
    if (!id_ok(case_id, CB_ID_LEN)) return CB_E_ID;
    uint32_t ix = rec_index(e, orig_msg_id);
    if (ix == NO_INDEX) {
        cb_strlcpy(reason, "NOOR", 5);
        return CB_OK;
    }
    cb_pay_rec *p = &e->pay[ix];
    if (cb_streq(p->case_id, case_id)) { /* idempotent replay */
        *accepted = p->status == CB_PS_CANCELLED;
        if (!*accepted) cb_strlcpy(reason, "ARDT", 5);
        return CB_OK;
    }
    if (p->status == CB_PS_CANCELLED || p->status == CB_PS_RETURNED) {
        cb_strlcpy(reason, "ARDT", 5);
        return CB_OK;
    }
    if (p->status == CB_PS_REJECTED) {
        cb_strlcpy(reason, "ARJR", 5);
        return CB_OK;
    }
    if (p->status == CB_PS_SETTLED || p->cycle != e->cycle || !e->cycle_open) {
        cb_strlcpy(reason, "AGNT", 5); /* settled: only a return can undo it */
        return CB_OK;
    }
    if (!covered(e, p->req.cdtr_agt, p->recv) ||
        !book(e, p->req.cdtr_agt, p->req.dbtr_agt, p->recv, p->send, p->unit)) {
        cb_strlcpy(reason, "AM04", 5);
        return CB_OK;
    }
    /* Release the corridor allowance the payment consumed. */
    const char *fs = e->part[p->req.dbtr_agt].ccy, *ft = e->part[p->req.cdtr_agt].ccy;
    const cb_corridor *k = cb_config_corridor(e->cfg, fs, ft);
    if (k && !cb_streq(fs, ft)) {
        uint32_t ci = (uint32_t) (k - e->cfg->corr);
        e->corr_used[ci] = e->corr_used[ci] >= p->send ? e->corr_used[ci] - p->send : 0;
    }
    p->status = CB_PS_CANCELLED;
    cb_strlcpy(p->case_id, case_id, sizeof p->case_id);
    *accepted = true;
    return CB_OK;
}

int cb_net_check(const cb_net *e)
{
    if (!e) return CB_E_NULL;
    int64_t usum = 0, ausum = 0;
    for (uint32_t c = 0; c < e->cfg->n_ccy; c++) {
        int64_t s = e->agent[c];
        for (uint32_t p = 0; p < e->n_part; p++)
            if (cb_streq(e->part[p].ccy, e->cfg->ccy[c].alpha) && !cb_sadd_ok(s, e->pos[p], &s))
                return CB_E_CONSERV;
        if (s != 0) return CB_E_CONSERV;
        if (!cb_sadd_ok(ausum, e->aupos[c], &ausum)) return CB_E_CONSERV;
    }
    for (uint32_t p = 0; p < e->n_part; p++)
        if (!cb_sadd_ok(usum, e->upos[p], &usum)) return CB_E_CONSERV;
    return (usum == 0 && ausum == 0) ? CB_OK : CB_E_CONSERV;
}

int cb_net_close_cycle(cb_net *e, uint64_t now, cb_settle_instr *out, uint32_t cap, uint32_t *n)
{
    (void) now;
    if (!e || !out || !n) return CB_E_NULL;
    *n = 0;
    if (!e->cycle_open) return CB_E_CYCLE;
    if (cap < e->cfg->n_ccy) return CB_E_FULL;
    int rc = cb_net_check(e);
    if (rc != CB_OK) return rc;
    /* every prefund update must fit before anything is written */
    for (uint32_t p = 0; p < e->n_part; p++) {
        int64_t np;
        if (!cb_sadd_ok(e->part[p].prefund, e->pos[p], &np)) return CB_E_CONSERV;
    }
    for (uint32_t c = 0; c < e->cfg->n_ccy; c++) {
        const cb_ccy_profile *k = &e->cfg->ccy[c];
        cb_settle_instr *o = &out[c];
        cb_memset(o, 0, sizeof *o);
        cb_strlcpy(o->ccy, k->alpha, sizeof o->ccy);
        cb_strlcpy(o->issuer_bic, k->issuer_bic, sizeof o->issuer_bic);
        cb_strlcpy(o->issuer_name, k->issuer_name, sizeof o->issuer_name);
        o->cycle = e->cycle;
        o->agent_net = e->agent[c];
        o->unit_net = -e->aupos[c];
        for (uint32_t p = 0; p < e->n_part; p++) {
            if (!cb_streq(e->part[p].ccy, k->alpha)) continue;
            o->lines[o->n_lines].part = (uint16_t) p;
            o->lines[o->n_lines].net = e->pos[p];
            o->n_lines++;
            if (!cb_sadd_ok(o->participants_net, e->pos[p], &o->participants_net))
                return CB_E_CONSERV; /* nothing written to the engine yet */
        }
    }
    for (uint32_t p = 0; p < e->n_part; p++) {
        e->part[p].prefund += e->pos[p];
        e->pos[p] = 0;
        e->upos[p] = 0;
    }
    for (uint32_t c = 0; c < CB_MAX_CCY; c++) e->agent[c] = e->aupos[c] = 0;
    for (uint32_t c = 0; c < CB_MAX_CORRIDORS; c++) e->corr_used[c] = 0;
    for (uint32_t i = 0; i < e->n_pay; i++)
        if (e->pay[i].status == CB_PS_ACCEPTED && e->pay[i].cycle == e->cycle)
            e->pay[i].status = CB_PS_SETTLED;
    e->cycle_open = false;
    *n = e->cfg->n_ccy;
    return CB_OK;
}

const char *cb_net_txsts(int status)
{
    switch (status) {
    case CB_PS_ACCEPTED:
        return "ACSP";
    case CB_PS_SETTLED:
        return "ACSC";
    case CB_PS_REJECTED:
        return "RJCT";
    case CB_PS_CANCELLED:
        return "CANC";
    case CB_PS_RETURNED:
        return "ACSC"; /* the original settled; the return is a pacs.004 */
    default:
        return "PDNG";
    }
}
