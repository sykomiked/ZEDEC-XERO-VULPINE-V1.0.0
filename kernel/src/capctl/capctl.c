/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* capctl.c — see capctl.h (C1-C8). Freestanding: no libc, no float, no
 * 64-bit division helper (cp_udivmod64). */
#include "capctl.h"

#define SECS_PER_DAY 86400u

static bool cc_eq(const char a[2], const char b[2])
{
    return a[0] == b[0] && a[1] == b[1];
}

static bool code_ok(const char c[4])
{
    for (unsigned i = 0; i < 4; i++)
        if (!((c[i] >= 'A' && c[i] <= 'Z') || (c[i] >= '0' && c[i] <= '9'))) return false;
    return true;
}

static bool code_empty(const char c[4])
{
    return c[0] == 0 && c[1] == 0 && c[2] == 0 && c[3] == 0;
}

static bool profile_ok(const capctl_profile_t *p)
{
    if (p->hdr.kind != CP_KIND_CAPCTL || !cp_cc_ok(p->hdr.country, false)) return false;
    for (unsigned i = 0; i < CP_REGION_LEN; i++)
        if (p->hdr.region[i]) return false;
    if (p->hdr.version == 0 || p->hdr.not_after < p->hdr.not_before) return false;
    if (p->ccy == 0 || p->ccy > 999) return false;
    if (p->flags & ~CAPCTL_F_ALL) return false;
    if (!(p->flags & (CAPCTL_F_OUTBOUND | CAPCTL_F_INBOUND))) return false;
    if (p->n_period > CAPCTL_MAX_PERIOD || p->n_agg > CAPCTL_MAX_AGG ||
        p->n_purpose > CAPCTL_MAX_PURPOSE || p->n_corr > CAPCTL_MAX_CORR)
        return false;
    for (unsigned i = 0; i < p->n_period; i++)
        if (p->period[i].window_days == 0 || p->period[i].window_days > CAPCTL_DAYS) return false;
    for (unsigned i = 0; i < p->n_agg; i++)
        if (p->agg[i].window_days == 0 || p->agg[i].window_days > CAPCTL_DAYS ||
            p->agg[i].amount == 0)
            return false;
    for (unsigned i = 0; i < p->n_purpose; i++)
        if (!code_ok(p->purpose[i])) return false;
    for (unsigned i = 0; i < p->n_corr; i++) {
        const capctl_corridor_t *k = &p->corr[i];
        if (!cp_cc_ok(k->from, true) || !cp_cc_ok(k->to, true) || k->ccy > 999) return false;
        if (k->action != CAPCTL_CORR_ALLOW && k->action != CAPCTL_CORR_BLOCK) return false;
    }
    return true;
}

size_t capctl_encode(const capctl_profile_t *p, uint8_t *out, size_t cap)
{
    if (!p || !out || !profile_ok(p)) return 0;
    cp_w_t w = {out, cap, 0, false};
    cp_hdr_put(&w, &p->hdr);
    cp_put16(&w, p->ccy);
    cp_put8(&w, p->flags);
    cp_put64(&w, p->per_tx_limit);
    cp_put8(&w, p->n_period);
    for (unsigned i = 0; i < p->n_period; i++) {
        cp_put16(&w, p->period[i].window_days);
        cp_put64(&w, p->period[i].amount);
    }
    cp_put64(&w, p->report_single);
    cp_put8(&w, p->n_agg);
    for (unsigned i = 0; i < p->n_agg; i++) {
        cp_put16(&w, p->agg[i].window_days);
        cp_put64(&w, p->agg[i].amount);
    }
    cp_put64(&w, p->purpose_threshold);
    cp_put8(&w, p->n_purpose);
    for (unsigned i = 0; i < p->n_purpose; i++) cp_put(&w, p->purpose[i], 4);
    cp_put8(&w, p->n_corr);
    for (unsigned i = 0; i < p->n_corr; i++) {
        cp_put(&w, p->corr[i].from, 2);
        cp_put(&w, p->corr[i].to, 2);
        cp_put16(&w, p->corr[i].ccy);
        cp_put8(&w, p->corr[i].action);
    }
    return w.bad ? 0 : w.len;
}

capctl_status_t capctl_decode(const uint8_t *m, size_t len, capctl_profile_t *p)
{
    if (!m || !p) return CAPCTL_E_ARG;
    cp_zero(p, sizeof *p);
    cp_r_t r = {m, len, 0, false};
    if (!cp_hdr_get(&r, &p->hdr, false)) return CAPCTL_E_DECODE;
    p->ccy = cp_get16(&r);
    p->flags = cp_get8(&r);
    p->per_tx_limit = cp_get64(&r);
    p->n_period = cp_get8(&r);
    if (p->n_period > CAPCTL_MAX_PERIOD) return CAPCTL_E_DECODE;
    for (unsigned i = 0; i < p->n_period; i++) {
        p->period[i].window_days = cp_get16(&r);
        p->period[i].amount = cp_get64(&r);
    }
    p->report_single = cp_get64(&r);
    p->n_agg = cp_get8(&r);
    if (p->n_agg > CAPCTL_MAX_AGG) return CAPCTL_E_DECODE;
    for (unsigned i = 0; i < p->n_agg; i++) {
        p->agg[i].window_days = cp_get16(&r);
        p->agg[i].amount = cp_get64(&r);
    }
    p->purpose_threshold = cp_get64(&r);
    p->n_purpose = cp_get8(&r);
    if (p->n_purpose > CAPCTL_MAX_PURPOSE) return CAPCTL_E_DECODE;
    for (unsigned i = 0; i < p->n_purpose; i++) cp_get(&r, p->purpose[i], 4);
    p->n_corr = cp_get8(&r);
    if (p->n_corr > CAPCTL_MAX_CORR) return CAPCTL_E_DECODE;
    for (unsigned i = 0; i < p->n_corr; i++) {
        cp_get(&r, p->corr[i].from, 2);
        cp_get(&r, p->corr[i].to, 2);
        p->corr[i].ccy = cp_get16(&r);
        p->corr[i].action = cp_get8(&r);
    }
    if (r.bad || r.off != len || !profile_ok(p)) return CAPCTL_E_DECODE;
    return CAPCTL_OK;
}

void capctl_init(capctl_t *c, cp_verify_fn verify, void *vctx, capctl_tally_t *tally,
                 uint32_t tally_cap)
{
    cp_zero(c, sizeof *c);
    c->enabled = false;
    c->verify = verify;
    c->vctx = vctx;
    c->tally = tally;
    c->tally_cap = tally ? tally_cap : 0;
    for (uint32_t i = 0; i < c->tally_cap; i++) c->tally[i].used = false;
}

void capctl_enable(capctl_t *c, bool on)
{
    c->enabled = on;
}

static const capctl_req_t *find_req(const capctl_t *c, const char cc[2])
{
    for (uint32_t i = 0; i < c->n_req; i++)
        if (cc_eq(c->req[i].country, cc)) return &c->req[i];
    return 0;
}

capctl_status_t capctl_require(capctl_t *c, const char cc[2], uint32_t min_version)
{
    if (!c || !cc || !cp_cc_ok(cc, false)) return CAPCTL_E_ARG;
    for (uint32_t i = 0; i < c->n_req; i++)
        if (cc_eq(c->req[i].country, cc)) {
            c->req[i].min_version = min_version;
            return CAPCTL_OK;
        }
    if (c->n_req >= CAPCTL_MAX_REQ) return CAPCTL_E_FULL;
    c->req[c->n_req].country[0] = cc[0];
    c->req[c->n_req].country[1] = cc[1];
    c->req[c->n_req].min_version = min_version;
    c->n_req++;
    return CAPCTL_OK;
}

const capctl_profile_t *capctl_profile(const capctl_t *c, const char cc[2])
{
    for (uint32_t i = 0; i < c->n_prof; i++)
        if (cc_eq(c->prof[i].hdr.country, cc)) return &c->prof[i];
    return 0;
}

capctl_status_t capctl_install(capctl_t *c, const uint8_t *m, size_t len, const uint8_t *sig,
                               size_t sig_len, uint64_t now)
{
    if (!c || !m) return CAPCTL_E_ARG;
    capctl_profile_t p;
    capctl_status_t st = capctl_decode(m, len, &p);
    if (st != CAPCTL_OK) return st;
    if (!cp_manifest_verify(c->verify, c->vctx, &p.hdr, m, len, sig, sig_len)) return CAPCTL_E_SIG;
    if (now < p.hdr.not_before || now > p.hdr.not_after) return CAPCTL_E_WINDOW;
    const capctl_req_t *rq = find_req(c, p.hdr.country);
    if (rq && p.hdr.version < rq->min_version) return CAPCTL_E_FLOOR;
    const capctl_profile_t *old = capctl_profile(c, p.hdr.country);
    if (old) {
        if (p.hdr.version <= old->hdr.version) return CAPCTL_E_VERSION;
        cp_copy((capctl_profile_t *) old, &p, sizeof p);
        return CAPCTL_OK;
    }
    if (c->n_prof >= CAPCTL_MAX_PROFILES) return CAPCTL_E_FULL;
    cp_copy(&c->prof[c->n_prof++], &p, sizeof p);
    return CAPCTL_OK;
}

static uint32_t day_of(uint64_t now)
{
    uint64_t d = cp_udivmod64(now, SECS_PER_DAY, 0);
    return d > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) d;
}

static const capctl_tally_t *find_tally(const capctl_t *c, const char cc[2], uint8_t dir,
                                        uint64_t person)
{
    for (uint32_t i = 0; i < c->tally_cap; i++) {
        const capctl_tally_t *t = &c->tally[i];
        if (t->used && t->dir == dir && t->person == person && cc_eq(t->country, cc)) return t;
    }
    return 0;
}

static bool tally_room(const capctl_t *c)
{
    for (uint32_t i = 0; i < c->tally_cap; i++)
        if (!c->tally[i].used) return true;
    return false;
}

/* C6: the sum of days D-W+1..D; false on overflow. */
static bool window_sum(const capctl_tally_t *t, uint16_t w, uint32_t d, uint64_t *out)
{
    uint64_t s = 0;
    if (t)
        for (uint32_t i = 0; i < CAPCTL_DAYS; i++) {
            if (!t->amt[i] || t->day[i] > d) continue;
            if ((uint64_t) t->day[i] + w <= d) continue;
            if (!cp_add(s, t->amt[i], &s)) return false;
        }
    *out = s;
    return true;
}

uint64_t capctl_used(const capctl_t *c, const char cc[2], uint8_t dir, uint64_t person,
                     uint16_t window_days, uint64_t now)
{
    uint64_t s;
    if (!c || !cc) return 0;
    if (!window_sum(find_tally(c, cc, dir, person), window_days, day_of(now), &s))
        return UINT64_MAX;
    return s;
}

static bool corr_match(const capctl_corridor_t *k, const capctl_transfer_t *t)
{
    bool f = (k->from[0] == '*' && k->from[1] == '*') || cc_eq(k->from, t->from);
    bool to = (k->to[0] == '*' && k->to[1] == '*') || cc_eq(k->to, t->to);
    return f && to && (k->ccy == 0 || k->ccy == t->ccy);
}

typedef struct {
    capctl_decision_t d;
    capctl_reason_t r;
    capctl_report_t rep;
} side_t;

static void deny(side_t *s, capctl_reason_t r)
{
    s->d = CAPCTL_DENY;
    s->r = r;
}

/* The usable profile for a side, or NULL with s set (C4). */
static const capctl_profile_t *side_profile(const capctl_t *c, const char cc[2], uint64_t now,
                                            side_t *s)
{
    const capctl_req_t *rq = find_req(c, cc);
    const capctl_profile_t *p = capctl_profile(c, cc);
    s->d = CAPCTL_ALLOW;
    s->r = CAPCTL_R_NO_PROFILE;
    if (!p) {
        if (rq) deny(s, CAPCTL_R_PROFILE_MISSING);
        return 0;
    }
    if (now < p->hdr.not_before || now > p->hdr.not_after) {
        deny(s, CAPCTL_R_PROFILE_EXPIRED);
        return 0;
    }
    if (rq && p->hdr.version < rq->min_version) {
        deny(s, CAPCTL_R_PROFILE_ROLLBACK);
        return 0;
    }
    return p;
}

static void eval_side(const capctl_t *c, const char cc[2], uint8_t dir, uint64_t person,
                      const capctl_transfer_t *t, uint64_t now, side_t *s)
{
    const capctl_profile_t *p = side_profile(c, cc, now, s);
    if (!p) return;
    uint8_t need = dir == CAPCTL_DIR_OUT ? CAPCTL_F_OUTBOUND : CAPCTL_F_INBOUND;
    s->r = CAPCTL_R_OK;
    if (!(p->flags & need)) return;

    uint64_t base = t->amount;
    if (t->ccy != p->ccy) {
        if (t->base_amount == 0) return deny(s, CAPCTL_R_FX_REQUIRED);
        base = t->base_amount;
    }

    bool allow = (p->flags & CAPCTL_F_DEFAULT_ALLOW) != 0, matched = false;
    for (unsigned i = 0; i < p->n_corr; i++) {
        if (!corr_match(&p->corr[i], t)) continue;
        if (p->corr[i].action == CAPCTL_CORR_BLOCK) return deny(s, CAPCTL_R_CORRIDOR_BLOCKED);
        matched = true;
    }
    if (!matched && !allow) return deny(s, CAPCTL_R_CORRIDOR_NOT_ALLOWED);

    if (p->per_tx_limit && base > p->per_tx_limit) return deny(s, CAPCTL_R_PER_TX_LIMIT);

    if (p->purpose_threshold && base >= p->purpose_threshold) {
        if (code_empty(t->purpose)) return deny(s, CAPCTL_R_PURPOSE_REQUIRED);
        if (p->n_purpose) {
            bool ok = false;
            for (unsigned i = 0; i < p->n_purpose && !ok; i++)
                ok = cp_eq(p->purpose[i], t->purpose, 4);
            if (!ok) return deny(s, CAPCTL_R_PURPOSE_NOT_ALLOWED);
        }
    }

    uint32_t day = day_of(now);
    const capctl_tally_t *tl = 0;
    if (p->n_period || p->n_agg) {
        tl = find_tally(c, cc, dir, person);
        if (!tl && !tally_room(c)) return deny(s, CAPCTL_R_TALLY_FULL);
    }
    for (unsigned i = 0; i < p->n_period; i++) {
        uint64_t used, after;
        if (!window_sum(tl, p->period[i].window_days, day, &used) || !cp_add(used, base, &after))
            return deny(s, CAPCTL_R_OVERFLOW);
        if (after > p->period[i].amount) return deny(s, CAPCTL_R_PERIOD_LIMIT);
    }

    uint8_t flags = 0;
    if (p->report_single && base >= p->report_single) flags |= CAPCTL_REP_SINGLE;
    for (unsigned i = 0; i < p->n_agg; i++) {
        uint64_t used, after;
        if (!window_sum(tl, p->agg[i].window_days, day, &used) || !cp_add(used, base, &after))
            return deny(s, CAPCTL_R_OVERFLOW);
        if (after >= p->agg[i].amount) flags |= CAPCTL_REP_AGGREGATE;
    }
    if (!flags) return;
    s->d = CAPCTL_ALLOW_WITH_REPORT;
    s->r = CAPCTL_R_REPORT;
    capctl_report_t *r = &s->rep;
    r->country[0] = cc[0];
    r->country[1] = cc[1];
    r->dir = dir;
    r->flags = flags;
    r->profile_version = p->hdr.version;
    r->person = person;
    r->base_amount = base;
    r->profile_ccy = p->ccy;
    r->ccy = t->ccy;
    r->amount = t->amount;
    cp_copy(r->from, t->from, 2);
    cp_copy(r->to, t->to, 2);
    cp_copy(r->purpose, t->purpose, 4);
    r->time = now;
}

static bool transfer_ok(const capctl_transfer_t *t)
{
    if (!cp_cc_ok(t->from, false) || !cp_cc_ok(t->to, false)) return false;
    if (t->amount == 0 || t->ccy == 0 || t->ccy > 999) return false;
    return code_empty(t->purpose) || code_ok(t->purpose);
}

capctl_decision_t capctl_evaluate(const capctl_t *c, const capctl_transfer_t *t, uint64_t now,
                                  capctl_result_t *out)
{
    if (!out) return CAPCTL_DENY;
    cp_zero(out, sizeof *out);
    out->decision = CAPCTL_DENY;
    out->reason = CAPCTL_R_BAD_ARG;
    if (!c || !t) return CAPCTL_DENY;
    if (!c->enabled) {
        out->decision = CAPCTL_ALLOW;
        out->reason = CAPCTL_R_DISABLED;
        return CAPCTL_ALLOW;
    }
    if (!transfer_ok(t)) return CAPCTL_DENY;
    if (cc_eq(t->from, t->to)) {
        out->decision = CAPCTL_ALLOW;
        out->reason = CAPCTL_R_DOMESTIC;
        return CAPCTL_ALLOW;
    }
    side_t s[2];
    cp_zero(s, sizeof s);
    eval_side(c, t->from, CAPCTL_DIR_OUT, t->sender, t, now, &s[0]);
    eval_side(c, t->to, CAPCTL_DIR_IN, t->receiver, t, now, &s[1]);
    for (unsigned i = 0; i < 2; i++)
        if (s[i].d == CAPCTL_DENY) {
            out->reason = s[i].r;
            cp_copy(out->country, i ? t->to : t->from, 2);
            return CAPCTL_DENY;
        }
    out->decision = CAPCTL_ALLOW;
    out->reason = s[0].r == CAPCTL_R_NO_PROFILE && s[1].r == CAPCTL_R_NO_PROFILE
                      ? CAPCTL_R_NO_PROFILE
                      : CAPCTL_R_OK;
    for (unsigned i = 0; i < 2; i++)
        if (s[i].d == CAPCTL_ALLOW_WITH_REPORT)
            cp_copy(&out->report[out->n_reports++], &s[i].rep, sizeof s[i].rep);
    if (out->n_reports) {
        out->decision = CAPCTL_ALLOW_WITH_REPORT;
        out->reason = CAPCTL_R_REPORT;
    }
    return out->decision;
}

static capctl_status_t commit_side(capctl_t *c, const char cc[2], uint8_t dir, uint64_t person,
                                   const capctl_transfer_t *t, uint64_t now)
{
    side_t s;
    const capctl_profile_t *p = side_profile(c, cc, now, &s);
    uint8_t need = dir == CAPCTL_DIR_OUT ? CAPCTL_F_OUTBOUND : CAPCTL_F_INBOUND;
    if (!p || !(p->flags & need) || (!p->n_period && !p->n_agg)) return CAPCTL_OK;
    uint64_t base = t->ccy == p->ccy ? t->amount : t->base_amount;
    if (base == 0) return CAPCTL_E_ARG;
    capctl_tally_t *tl = (capctl_tally_t *) find_tally(c, cc, dir, person);
    if (!tl) {
        for (uint32_t i = 0; i < c->tally_cap && !tl; i++)
            if (!c->tally[i].used) tl = &c->tally[i];
        if (!tl) return CAPCTL_E_FULL;
        cp_zero(tl, sizeof *tl);
        tl->used = true;
        tl->country[0] = cc[0];
        tl->country[1] = cc[1];
        tl->dir = dir;
        tl->person = person;
    }
    uint32_t d = day_of(now), slot = d % CAPCTL_DAYS;
    if (tl->day[slot] != d) {
        tl->day[slot] = d;
        tl->amt[slot] = 0;
    }
    uint64_t sum;
    if (!cp_add(tl->amt[slot], base, &sum)) return CAPCTL_E_ARG;
    tl->amt[slot] = sum;
    return CAPCTL_OK;
}

capctl_status_t capctl_commit(capctl_t *c, const capctl_transfer_t *t, uint64_t now)
{
    if (!c || !t || !transfer_ok(t)) return CAPCTL_E_ARG;
    if (!c->enabled || cc_eq(t->from, t->to)) return CAPCTL_OK;
    capctl_status_t st = commit_side(c, t->from, CAPCTL_DIR_OUT, t->sender, t, now);
    if (st != CAPCTL_OK) return st;
    return commit_side(c, t->to, CAPCTL_DIR_IN, t->receiver, t, now);
}

static const char *const REASON[CAPCTL_R_COUNT] = {
    "ok",
    "disabled",
    "domestic",
    "no_profile",
    "report",
    "bad_arg",
    "profile_missing",
    "profile_expired",
    "profile_rollback",
    "fx_required",
    "corridor_blocked",
    "corridor_not_allowed",
    "per_tx_limit",
    "purpose_required",
    "purpose_not_allowed",
    "period_limit",
    "tally_full",
    "overflow",
};

const char *capctl_reason_name(capctl_reason_t r)
{
    return (unsigned) r < CAPCTL_R_COUNT ? REASON[r] : "unknown";
}

const char *capctl_decision_name(capctl_decision_t d)
{
    switch (d) {
    case CAPCTL_ALLOW:
        return "ALLOW";
    case CAPCTL_ALLOW_WITH_REPORT:
        return "ALLOW_WITH_REPORT";
    case CAPCTL_DENY:
        return "DENY";
    }
    return "unknown";
}
