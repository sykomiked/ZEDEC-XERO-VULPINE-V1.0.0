/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* evo_comm.c — community-maintained updates: compatibility contract, signed
 * audits, the install gate and the rollback journal. See evo_comm.h. */
#include "evo_comm.h"
#include "evo_util.h"

#define CONTRACT_MAGIC EVO_MAGIC('Z', 'X', 'C', 'T')
#define AUDIT_MAGIC    EVO_MAGIC('Z', 'X', 'A', 'U')

/* ===== U1 ===== */
evo_status_t evo_contract_init(evo_contract_t *c, uint32_t conform_req)
{
    if (!c || (conform_req & ~EVO_CHK_ALL)) return EVO_ERR_ARG;
    evo_zero(c, sizeof(*c));
    c->conform_req = conform_req;
    return EVO_OK;
}

evo_status_t evo_contract_add(evo_contract_t *c, const evo_cid_t *d)
{
    if (!c || !d || evo_cid_is_zero(d)) return EVO_ERR_ARG;
    uint32_t i = 0;
    while (i < c->n && evo_cid_cmp(&c->desc[i], d) < 0) i++;
    if (i < c->n && evo_cid_eq(&c->desc[i], d)) return EVO_ERR_DUP;
    if (c->n >= EVO_CONTRACT_MAX) return EVO_ERR_FULL;
    for (uint32_t j = c->n; j > i; j--) evo_cpy(&c->desc[j], &c->desc[j - 1], sizeof(evo_cid_t));
    evo_cpy(&c->desc[i], d, sizeof(evo_cid_t));
    c->n++;
    return EVO_OK;
}

uint32_t evo_contract_encode(const evo_contract_t *c, uint8_t *out, uint32_t cap)
{
    if (!c || !out || c->n > EVO_CONTRACT_MAX) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, CONTRACT_MAGIC);
    evo_w_u32(&w, c->conform_req);
    evo_w_u8(&w, c->n);
    for (uint32_t i = 0; i < c->n; i++) evo_w_bytes(&w, c->desc[i].b, EVO_CID_LEN);
    return w.err ? 0 : w.len;
}

evo_status_t evo_contract_decode(const uint8_t *in, uint32_t len, evo_contract_t *c)
{
    if (!in || !c) return EVO_ERR_ARG;
    evo_r_t r = {in, len, 0, false};
    evo_contract_t t;
    evo_zero(&t, sizeof(t));
    if ((uint32_t) evo_r_le(&r, 4) != CONTRACT_MAGIC) return EVO_ERR_PARSE;
    t.conform_req = (uint32_t) evo_r_le(&r, 4);
    uint32_t n = (uint32_t) evo_r_le(&r, 1);
    if (r.err || n > EVO_CONTRACT_MAX || (t.conform_req & ~EVO_CHK_ALL)) return EVO_ERR_PARSE;
    for (uint32_t i = 0; i < n; i++) {
        evo_r_copy(&r, t.desc[i].b, EVO_CID_LEN);
        /* canonical: strictly ascending, no zero CID */
        if (evo_cid_is_zero(&t.desc[i]) || (i && evo_cid_cmp(&t.desc[i - 1], &t.desc[i]) >= 0))
            return EVO_ERR_PARSE;
    }
    t.n = (uint8_t) n;
    if (r.err || r.pos != len) return EVO_ERR_PARSE;
    evo_cpy(c, &t, sizeof(t));
    return EVO_OK;
}

evo_status_t evo_contract_cid(const evo_contract_t *c, evo_cid_t *out)
{
    uint8_t buf[EVO_CONTRACT_ENC_MAX];
    if (!out) return EVO_ERR_ARG;
    uint32_t n = evo_contract_encode(c, buf, sizeof(buf));
    if (!n) return EVO_ERR_ARG;
    evo_cid_of(buf, n, out);
    return EVO_OK;
}

/* The build's descriptor for a family, or NULL. */
static const evo_cap_t *capset_family(const evo_capset_t *s, const char *name, uint32_t len)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (evo_name_cmp(s->c[i].name, s->c[i].name_len, name, len) == 0) return &s->c[i];
    return 0;
}

evo_status_t evo_contract_check(const evo_contract_t *c, const evo_registry_t *reg,
                                const evo_capset_t *build, uint32_t conform, uint32_t *bad)
{
    if (!c || !reg || !build) return EVO_ERR_ARG;
    for (uint32_t i = 0; i < c->n; i++) {
        if (bad) *bad = i;
        const evo_desc_t *want = evo_reg_find(reg, &c->desc[i]);
        if (!want) return EVO_ERR_NOT_FOUND;
        const evo_cap_t *cap = capset_family(build, want->name, want->name_len);
        if (!cap) return EVO_ERR_UNSATISFIED;
        const evo_desc_t *have = evo_reg_find(reg, &cap->cid);
        if (!have) return EVO_ERR_NOT_FOUND;
        /* satisfies: compatible (so no one-sided REQUIRED field) and every
         * contract field is understood the same way. */
        if (!evo_desc_satisfies(have, want)) return EVO_ERR_UNSATISFIED;
    }
    if ((conform & c->conform_req) != c->conform_req) return EVO_ERR_NONCONFORMANT;
    return EVO_OK;
}

bool evo_contract_extends(const evo_contract_t *nw, const evo_contract_t *old,
                          const evo_registry_t *reg)
{
    if (!nw || !old || !reg) return false;
    if ((nw->conform_req & old->conform_req) != old->conform_req) return false;
    for (uint32_t i = 0; i < old->n; i++) {
        const evo_desc_t *o = evo_reg_find(reg, &old->desc[i]);
        if (!o) return false;
        bool found = false;
        for (uint32_t j = 0; j < nw->n && !found; j++) {
            const evo_desc_t *d = evo_reg_find(reg, &nw->desc[j]);
            if (!d) return false;
            if (evo_name_cmp(d->name, d->name_len, o->name, o->name_len) == 0) {
                if (!evo_desc_satisfies(d, o)) return false;
                found = true;
            }
        }
        if (!found) return false;
    }
    return true;
}

/* ===== U2 ===== */
uint32_t evo_audit_encode(const evo_audit_t *a, uint8_t *out, uint32_t cap)
{
    if (!a || !out) return 0;
    if (a->verdict != EVO_AUDIT_PASS && a->verdict != EVO_AUDIT_FAIL) return 0;
    if (a->bounty_len > EVO_NAME_MAX || (a->conform & ~EVO_CHK_ALL)) return 0;
    if (evo_cid_is_zero(&a->record) || evo_cid_is_zero(&a->contract)) return 0;
    evo_w_t w = {out, cap, 0, false};
    evo_w_u32(&w, AUDIT_MAGIC);
    evo_w_bytes(&w, a->record.b, EVO_CID_LEN);
    evo_w_bytes(&w, a->contract.b, EVO_CID_LEN);
    evo_w_bytes(&w, a->auditor, 32);
    evo_w_u8(&w, a->verdict);
    evo_w_u32(&w, a->conform);
    evo_w_u64(&w, a->stamp);
    evo_w_u8(&w, a->bounty_len);
    evo_w_bytes(&w, a->bounty, a->bounty_len);
    return w.err ? 0 : w.len;
}

evo_status_t evo_audit_decode(const uint8_t *in, uint32_t len, evo_audit_t *a)
{
    if (!in || !a) return EVO_ERR_ARG;
    evo_r_t r = {in, len, 0, false};
    evo_audit_t t;
    evo_zero(&t, sizeof(t));
    if ((uint32_t) evo_r_le(&r, 4) != AUDIT_MAGIC) return EVO_ERR_PARSE;
    evo_r_copy(&r, t.record.b, EVO_CID_LEN);
    evo_r_copy(&r, t.contract.b, EVO_CID_LEN);
    evo_r_copy(&r, t.auditor, 32);
    t.verdict = (uint8_t) evo_r_le(&r, 1);
    t.conform = (uint32_t) evo_r_le(&r, 4);
    t.stamp = evo_r_le(&r, 8);
    t.bounty_len = (uint8_t) evo_r_le(&r, 1);
    if (r.err || t.bounty_len > EVO_NAME_MAX) return EVO_ERR_PARSE;
    evo_r_copy(&r, t.bounty, t.bounty_len);
    if (r.err || r.pos != len) return EVO_ERR_PARSE;
    uint8_t chk[EVO_AUDIT_BODY_MAX];
    /* canonical: re-encoding gives the same bytes (rejects bad verdicts,
     * stray conformance bits, zero CIDs) */
    if (evo_audit_encode(&t, chk, sizeof(chk)) != len || evo_cmp(chk, in, len) != 0)
        return EVO_ERR_PARSE;
    evo_cpy(a, &t, sizeof(t));
    return EVO_OK;
}

evo_status_t evo_audit_open(const uint8_t *wire, uint32_t len, evo_verify_fn vf, void *vctx,
                            evo_audit_t *out)
{
    if (!wire || !vf || !out) return EVO_ERR_ARG;
    evo_r_t r = {wire, len, 0, false};
    uint32_t bl = (uint32_t) evo_r_le(&r, 4);
    const uint8_t *body = evo_r_bytes(&r, bl);
    uint32_t pl = (uint32_t) evo_r_le(&r, 4);
    const uint8_t *pk = evo_r_bytes(&r, pl);
    uint32_t sl = (uint32_t) evo_r_le(&r, 4);
    const uint8_t *sig = evo_r_bytes(&r, sl);
    if (r.err || r.pos != len || !body || !pk || !sig || !pl || !sl) return EVO_ERR_PARSE;
    evo_audit_t a;
    evo_status_t st = evo_audit_decode(body, bl, &a);
    if (st != EVO_OK) return st;
    uint8_t id[32];
    evo_key_id(pk, pl, id);
    if (evo_cmp(id, a.auditor, 32) != 0) return EVO_ERR_BAD_SIG;
    if (!vf(pk, pl, body, bl, sig, sl, vctx)) return EVO_ERR_BAD_SIG;
    evo_cpy(out, &a, sizeof(a));
    return EVO_OK;
}

/* ===== U3 ===== */
evo_status_t evo_audit_policy_init(evo_audit_policy_t *p, uint32_t k)
{
    if (!p || k > EVO_AUDITORS_MAX) return EVO_ERR_ARG;
    evo_zero(p, sizeof(*p));
    p->k = k ? k : 2u;
    return EVO_OK;
}

evo_status_t evo_audit_policy_add(evo_audit_policy_t *p, const uint8_t key_id[32])
{
    if (!p || !key_id) return EVO_ERR_ARG;
    for (uint32_t i = 0; i < p->n; i++)
        if (evo_cmp(p->auditor[i], key_id, 32) == 0) return EVO_ERR_DUP;
    if (p->n >= EVO_AUDITORS_MAX) return EVO_ERR_FULL;
    evo_cpy(p->auditor[p->n++], key_id, 32);
    return EVO_OK;
}

static int32_t policy_index(const evo_audit_policy_t *p, const uint8_t id[32])
{
    for (uint32_t i = 0; i < p->n; i++)
        if (evo_cmp(p->auditor[i], id, 32) == 0) return (int32_t) i;
    return -1;
}

static bool blocked(const evo_trust_t *t, const evo_dag_t *g, uint32_t idx)
{
    if (!t || !t->min_stars) return false;
    /* the record, its author, and everything it descends from */
    for (uint32_t i = 0; i < g->n; i++) {
        if (i != idx && !evo_dag_is_ancestor(g, i, idx)) continue;
        uint8_t s = evo_trust_stars_record(t, &g->node[i].cid);
        if (s && s < t->min_stars) return true;
        s = evo_trust_stars_key(t, g->node[i].f.author);
        if (s && s < t->min_stars) return true;
    }
    return false;
}

evo_status_t evo_community_gate(const evo_dag_t *g, const evo_cid_t *record,
                                const evo_trust_t *trust, const evo_contract_t *contract,
                                const evo_registry_t *reg, const evo_capset_t *build,
                                uint32_t build_conform, const evo_audit_t *audits,
                                uint32_t n_audits, const evo_audit_policy_t *pol,
                                const evo_install_consent_t *consent, uint64_t now,
                                evo_gate_result_t *res)
{
    if (!g || !record || !contract || !reg || !build || !pol || !res || (n_audits && !audits))
        return EVO_ERR_ARG;
    if (pol->k == 0 || pol->k > EVO_AUDITORS_MAX || pol->n > EVO_AUDITORS_MAX) return EVO_ERR_ARG;
    evo_zero(res, sizeof(*res));

    int32_t idx = evo_dag_find(g, record);
    if (idx < 0) {
        res->verdict = EVO_GATE_UNKNOWN_RECORD;
        return EVO_OK;
    }
    if (blocked(trust, g, (uint32_t) idx)) {
        res->verdict = EVO_GATE_BLOCKED;
        return EVO_OK;
    }
    if (evo_contract_check(contract, reg, build, build_conform, &res->bad) != EVO_OK) {
        res->verdict = EVO_GATE_INCOMPATIBLE;
        return EVO_OK;
    }

    evo_cid_t ccid;
    if (evo_contract_cid(contract, &ccid) != EVO_OK) return EVO_ERR_ARG;
    const uint8_t *author = g->node[idx].f.author;
    uint32_t passed = 0, failed = 0; /* bitmaps over policy index */
    for (uint32_t i = 0; i < n_audits; i++) {
        const evo_audit_t *a = &audits[i];
        if (!evo_cid_eq(&a->record, record)) continue;
        int32_t p = policy_index(pol, a->auditor);
        if (p < 0) continue;                                /* not chosen by this user */
        if (evo_cmp(a->auditor, author, 32) == 0) continue; /* no self-audit */
        if (a->verdict == EVO_AUDIT_FAIL) {
            failed |= 1u << p; /* a fail counts whatever contract it used */
            continue;
        }
        if (!evo_cid_eq(&a->contract, &ccid)) continue;
        if ((a->conform & contract->conform_req) != contract->conform_req) continue;
        if (a->bounty_len && !res->bounty_len) { /* first passing claim */
            res->bounty_len = a->bounty_len;
            evo_cpy(res->bounty, a->bounty, a->bounty_len);
        }
        passed |= 1u << p;
    }
    for (uint32_t b = 0; b < EVO_AUDITORS_MAX; b++) {
        res->passes += (passed >> b) & 1u;
        res->fails += (failed >> b) & 1u;
    }
    res->bounty_ready = res->passes >= pol->k && res->bounty_len > 0;
    if (res->fails) {
        res->verdict = EVO_GATE_AUDIT_FAILED;
        res->bounty_ready = false;
        return EVO_OK;
    }
    if (res->passes < pol->k) {
        res->verdict = EVO_GATE_NEED_AUDITS;
        return EVO_OK;
    }
    if (!consent || !consent->granted || !evo_cid_eq(&consent->record, record) ||
        now >= consent->expires_at) {
        res->verdict = EVO_GATE_NEED_CONSENT;
        return EVO_OK;
    }
    res->verdict = EVO_GATE_INSTALLABLE;
    return EVO_OK;
}

const char *evo_gate_str(evo_gate_t g)
{
    switch (g) {
    case EVO_GATE_INSTALLABLE:
        return "installable";
    case EVO_GATE_UNKNOWN_RECORD:
        return "unknown record";
    case EVO_GATE_BLOCKED:
        return "blocked by you";
    case EVO_GATE_INCOMPATIBLE:
        return "breaks the compatibility contract";
    case EVO_GATE_AUDIT_FAILED:
        return "an auditor failed it";
    case EVO_GATE_NEED_AUDITS:
        return "needs more independent audits";
    case EVO_GATE_NEED_CONSENT:
        return "waiting for your consent";
    }
    return "?";
}

/* ===== U4 ===== */
void evo_journal_init(evo_journal_t *j, const evo_cid_t *base, uint64_t trial_window)
{
    if (!j) return;
    evo_zero(j, sizeof(*j));
    if (base) evo_cpy(&j->base, base, sizeof(evo_cid_t));
    j->cur = -1;
    j->trial_window = trial_window;
}

bool evo_journal_in_trial(const evo_journal_t *j)
{
    return j && j->cur >= 0 && j->e[j->cur].state == EVO_J_TRIAL;
}

static void drop(evo_journal_t *j, uint32_t i)
{
    for (uint32_t k = i; k + 1 < j->n; k++) evo_cpy(&j->e[k], &j->e[k + 1], sizeof(evo_jent_t));
    j->n--;
    if (j->cur > (int32_t) i) j->cur--;
}

evo_status_t evo_journal_install(evo_journal_t *j, const evo_cid_t *record, uint64_t now)
{
    if (!j || !record || evo_cid_is_zero(record)) return EVO_ERR_ARG;
    if (evo_journal_in_trial(j)) return EVO_ERR_CONFLICT;
    if (j->n >= EVO_JOURNAL_MAX) {
        /* drop the oldest entry that is not running; rolled-back ones first */
        int32_t victim = -1;
        for (uint32_t i = 0; i < j->n && victim < 0; i++)
            if ((int32_t) i != j->cur && j->e[i].state == EVO_J_ROLLED_BACK) victim = (int32_t) i;
        for (uint32_t i = 0; i < j->n && victim < 0; i++)
            if ((int32_t) i != j->cur) victim = (int32_t) i;
        if (victim < 0) return EVO_ERR_FULL;
        drop(j, (uint32_t) victim);
    }
    evo_cpy(&j->e[j->n].record, record, sizeof(evo_cid_t));
    j->e[j->n].at = now;
    j->e[j->n].state = EVO_J_TRIAL;
    j->cur = (int32_t) j->n;
    j->n++;
    return EVO_OK;
}

evo_status_t evo_journal_rollback(evo_journal_t *j)
{
    if (!j) return EVO_ERR_ARG;
    if (j->cur < 0) return EVO_ERR_NOT_FOUND;
    j->e[j->cur].state = EVO_J_ROLLED_BACK;
    int32_t prev = -1;
    for (int32_t i = j->cur - 1; i >= 0 && prev < 0; i--)
        if (j->e[i].state == EVO_J_KEPT) prev = i;
    j->cur = prev;
    return EVO_OK;
}

evo_status_t evo_journal_confirm(evo_journal_t *j, bool health_ok)
{
    if (!j) return EVO_ERR_ARG;
    if (!evo_journal_in_trial(j)) return EVO_ERR_NOT_FOUND;
    if (health_ok) {
        j->e[j->cur].state = EVO_J_KEPT;
        return EVO_OK;
    }
    return evo_journal_rollback(j);
}

bool evo_journal_tick(evo_journal_t *j, uint64_t now)
{
    if (!evo_journal_in_trial(j)) return false;
    if (now < j->e[j->cur].at || now - j->e[j->cur].at < j->trial_window) return false;
    evo_journal_rollback(j);
    return true;
}

const evo_cid_t *evo_journal_current(const evo_journal_t *j)
{
    if (!j) return 0;
    return j->cur < 0 ? &j->base : &j->e[j->cur].record;
}
