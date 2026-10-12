/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/*
 * DISCLAIMER: This engine evaluates deterministic mathematical and state
 * constraints (boolean/paraconsistent policy logic). It does not provide
 * legal advice, make legal determinations, or replace statutory judicial
 * processes. Users are solely responsible for ensuring that parameterized
 * policies comply with applicable local and international laws.
 */
/* policy.c — zxv_policy: transfers, terms and settlement (see policy.h).
 * Freestanding: no libc, no allocation, no recursion, no floating point. */
#include "policy.h"

#define T_ LPRES_STATE_TRUE
#define F_ LPRES_STATE_FALSE
#define B_ LPRES_STATE_BOTH
#define N_ LPRES_STATE_NEITHER

static void zero_bytes(void *p, uint32_t n)
{
    volatile uint8_t *b = (volatile uint8_t *) p;
    for (uint32_t i = 0; i < n; i++) b[i] = 0;
}

static bool str_eq(const char *a, const char *b, uint32_t size)
{
    for (uint32_t i = 0; i < size; i++) {
        if (a[i] != b[i]) return false;
        if (a[i] == 0) return true;
    }
    return true;
}

static int32_t str_cmp(const char *a, const char *b)
{
    uint32_t i = 0;
    while (a[i] != 0 && a[i] == b[i]) i++;
    return (int32_t) (uint8_t) a[i] - (int32_t) (uint8_t) b[i];
}

static bool asset_ok(const char *a)
{
    for (uint32_t i = 0; i < 3u; i++)
        if (a[i] < 'A' || a[i] > 'Z') return false;
    return a[3] == 0;
}

static lpres_state_t clamp(lpres_state_t v)
{
    return (unsigned) v <= 3u ? v : N_;
}

/* ------------------------------------------------------------ packs */

void policy_host_init(policy_host_t *h)
{
    if (h) zero_bytes(h, (uint32_t) sizeof(*h));
}

static bool tags_ok(const char (*t)[ZXP_JUR_MAX + 1u], uint32_t n)
{
    if (n > ZXP_MAX_TAGS) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (!zxp_jur_ok(t[i])) return false;
        if (i > 0 && str_cmp(t[i - 1], t[i]) >= 0) return false;
    }
    return true;
}

bool policy_pack_init(policy_pack_t *p, const char *scope, const char *family, uint8_t flags)
{
    if (!p || !scope || !family) return false;
    zero_bytes(p, (uint32_t) sizeof(*p));
    for (uint32_t i = 0; i < ZXP_JUR_MAX + 1u; i++) {
        p->scope[i] = scope[i];
        if (scope[i] == 0) break;
    }
    for (uint32_t i = 0; i < POLICY_FAMILY_MAX && family[i] != 0; i++) p->family[i] = family[i];
    p->flags = flags;
    if (!zxp_jur_ok(p->scope) || (flags & ~(POLICY_PACK_NO_PENALTY | POLICY_PACK_DEFER)) != 0) {
        zero_bytes(p, (uint32_t) sizeof(*p));
        return false;
    }
    return true;
}

bool policy_host_register(policy_host_t *h, const policy_pack_t *p)
{
    if (!h || !p || h->n >= POLICY_MAX_PACKS) return false;
    if (!zxp_jur_ok(p->scope) || !tags_ok(p->allow, p->n_allow) || !tags_ok(p->deny, p->n_deny))
        return false;
    h->pack[h->n++] = p;
    return true;
}

bool policy_pack_applies(const policy_pack_t *p, const zxp_contract_t *c)
{
    if (!p || !c) return false;
    for (uint32_t i = 0; i < c->n_law && i < ZXP_MAX_LAWS; i++)
        if (zxp_jur_covers(p->scope, c->law[i])) return true;
    for (uint32_t i = 0; i < c->n_party && i < ZXP_MAX_PARTIES; i++)
        if (zxp_jur_covers(p->scope, c->party[i].jur)) return true;
    return false;
}

/* What one allow/deny list pair says about `tag`, as evidence bits. */
static lpres_state_t list_view(const char (*allow)[ZXP_JUR_MAX + 1u], uint32_t n_allow,
                               const char (*deny)[ZXP_JUR_MAX + 1u], uint32_t n_deny, const char *tag)
{
    lpres_state_t v = N_;
    bool hit = false;
    for (uint32_t i = 0; i < n_deny; i++)
        if (zxp_jur_covers(deny[i], tag)) v = policy_k_join(v, F_);
    for (uint32_t i = 0; i < n_allow; i++)
        if (zxp_jur_covers(allow[i], tag)) hit = true;
    if (hit) v = policy_k_join(v, T_);
    else if (n_allow > 0) v = policy_k_join(v, F_);
    return v;
}

static lpres_state_t jur_view(const zxp_contract_t *c, const policy_host_t *h, const char *tag)
{
    lpres_state_t v = list_view((const char (*)[ZXP_JUR_MAX + 1u]) c->allow, c->n_allow,
                                (const char (*)[ZXP_JUR_MAX + 1u]) c->deny, c->n_deny, tag);
    for (uint32_t k = 0; h && k < h->n; k++) {
        const policy_pack_t *p = h->pack[k];
        if (!policy_pack_applies(p, c)) continue;
        v = policy_k_join(v, list_view(p->allow, p->n_allow, p->deny, p->n_deny, tag));
    }
    return v == N_ ? T_ : v; /* nobody has a view: permitted */
}

/* ------------------------------------------------------------ transfers */

void policy_state_init(policy_state_t *s)
{
    if (s) zero_bytes(s, (uint32_t) sizeof(*s));
}

static uint32_t popcount16(uint16_t x)
{
    uint32_t n = 0;
    while (x) {
        n += x & 1u;
        x >>= 1;
    }
    return n;
}

static bool in_span(const zxp_span_t *s, uint32_t d)
{
    return d >= s->from && d <= s->to;
}

lpres_state_t policy_in_force(const zxp_contract_t *c, uint16_t signers, uint32_t date)
{
    if (!c || zxp_validate(c) != ZXP_OK || !zxp_date_ok(date)) return F_;
    uint16_t mask = (uint16_t) ((1u << c->n_party) - 1u);
    if (popcount16((uint16_t) (signers & mask)) < c->sign) return F_;
    if (!in_span(&c->window, date)) return F_;
    for (uint32_t i = 0; i < c->n_lock; i++)
        if (in_span(&c->lock[i], date)) return F_;
    return T_;
}

static policy_result_t fault(int32_t code)
{
    policy_result_t r;
    zero_bytes(&r, (uint32_t) sizeof r);
    r.decision = POLICY_FAULT;
    r.value = F_;
    r.fault = code;
    return r;
}

/* Fold rule `bit` with value v into the result. */
static void fold(policy_result_t *r, uint32_t bit, lpres_state_t v)
{
    if (v == F_) r->deny_mask |= bit;
    if (v == B_) r->both_mask |= bit;
    r->value = lpres_conjoin(r->value, v);
}

policy_result_t policy_check(const zxp_contract_t *c, const policy_host_t *h,
                             const policy_state_t *s, const policy_txn_t *t)
{
    policy_result_t r;
    int32_t st;
    if (!c || !t) return fault(ZXP_E_ARG);
    if ((st = zxp_validate(c)) != ZXP_OK) return fault(st);
    if (t->from >= c->n_party || t->to >= c->n_party || t->from == t->to || !asset_ok(t->asset) ||
        t->amount <= 0 || t->amount > ZXP_PIC18_MAX || !zxp_date_ok(t->date) ||
        !zxp_jur_ok(t->from_jur) || !zxp_jur_ok(t->to_jur))
        return fault(POLICY_E_TXN);
    for (uint32_t i = 0; i < 32u; i++)
        if (t->doc[i] != c->doc[i]) return fault(POLICY_E_DOC);

    zero_bytes(&r, (uint32_t) sizeof r);
    r.value = T_;
    fold(&r, POLICY_R_WINDOW, in_span(&c->window, t->date) ? T_ : F_);
    {
        bool locked = false;
        for (uint32_t i = 0; i < c->n_lock; i++) locked = locked || in_span(&c->lock[i], t->date);
        fold(&r, POLICY_R_LOCK, locked ? F_ : T_);
    }
    {
        uint16_t mask = (uint16_t) ((1u << c->n_party) - 1u);
        fold(&r, POLICY_R_SIGN, popcount16((uint16_t) (t->signers & mask)) >= c->sign ? T_ : F_);
        fold(&r, POLICY_R_PAYER, (t->signers >> t->from) & 1u ? T_ : F_);
    }
    if (c->n_cap > 0) {
        int32_t k = -1;
        for (uint32_t i = 0; i < c->n_cap; i++)
            if (str_eq(c->cap[i].asset, t->asset, 4u)) k = (int32_t) i;
        fold(&r, POLICY_R_ASSET, k >= 0 ? T_ : F_);
        if (k >= 0) fold(&r, POLICY_R_CAP, t->amount <= c->cap[k].amount ? T_ : F_);
    }
    for (uint32_t i = 0; i < c->n_limit; i++) {
        const zxp_limit_t *l = &c->limit[i];
        if (l->party != t->from || !str_eq(l->asset, t->asset, 4u)) continue;
        int64_t spent = s ? s->spent[i] : 0;
        if (spent < 0 || spent > ZXP_PIC18_MAX) return fault(POLICY_E_STATE);
        /* both <= 10^18 - 1, so the sum fits in int64 */
        fold(&r, POLICY_R_LIMIT, spent + t->amount <= l->amount ? T_ : F_);
    }
    fold(&r, POLICY_R_JUR_FROM, jur_view(c, h, t->from_jur));
    fold(&r, POLICY_R_JUR_TO, jur_view(c, h, t->to_jur));
    {
        lpres_state_t v = N_;
        for (uint32_t k = 0; h && k < h->n; k++) {
            const policy_pack_t *p = h->pack[k];
            if (p->txn_fn && policy_pack_applies(p, c)) v = policy_k_join(v, clamp(p->txn_fn(p->ctx, c, t)));
        }
        fold(&r, POLICY_R_PACK, v == N_ ? T_ : v);
    }
    r.escalate = r.both_mask != 0;
    r.decision = r.value == T_ ? POLICY_ALLOW : POLICY_DENY;
    return r;
}

policy_result_t policy_commit(const zxp_contract_t *c, const policy_host_t *h, policy_state_t *s,
                              const policy_txn_t *t)
{
    policy_result_t r = policy_check(c, h, s, t);
    if (r.decision != POLICY_ALLOW || !s) return r;
    for (uint32_t i = 0; i < c->n_limit; i++) {
        const zxp_limit_t *l = &c->limit[i];
        if (l->party == t->from && str_eq(l->asset, t->asset, 4u)) s->spent[i] += t->amount;
    }
    return r;
}

/* ------------------------------------------------------------ terms */

static lpres_state_t term_compliance(const zxp_contract_t *c, const policy_host_t *h, uint32_t i)
{
    lpres_state_t v = N_;
    for (uint32_t k = 0; h && k < h->n; k++) {
        const policy_pack_t *p = h->pack[k];
        if (!policy_pack_applies(p, c)) continue;
        if (p->flags & POLICY_PACK_DEFER) continue; /* NEITHER adds nothing */
        if (p->flags & POLICY_PACK_NO_PENALTY) v = policy_k_join(v, c->clause[i].penalty > 0 ? F_ : T_);
        if (p->term_fn) v = policy_k_join(v, clamp(p->term_fn(p->ctx, c, i)));
    }
    return v;
}

int32_t policy_terms(const zxp_contract_t *c, const policy_host_t *h, const policy_att_t *att,
                     uint32_t n_att, uint32_t now, policy_term_t out[ZXP_MAX_CLAUSES])
{
    int32_t st;
    if (!out) return ZXP_E_ARG;
    zero_bytes(out, (uint32_t) (sizeof(policy_term_t) * ZXP_MAX_CLAUSES));
    if (!c || (n_att > 0 && !att)) return ZXP_E_ARG;
    if ((st = zxp_validate(c)) != ZXP_OK) return st;
    if (n_att > POLICY_MAX_ATTS || !zxp_date_ok(now)) return POLICY_E_TXN;
    for (uint32_t a = 0; a < n_att; a++)
        if (att[a].verdict != T_ && att[a].verdict != F_) return POLICY_E_TXN;

    for (uint32_t i = 0; i < c->n_clause; i++) {
        const zxp_clause_t *k = &c->clause[i];
        policy_term_t *o = &out[i];
        if (k->kind == ZXP_CL_GATE) {
            lpres_state_t v = out[k->ref[0]].value;
            for (uint32_t r = 1; r < k->n_ref; r++)
                v = k->op == ZXP_GATE_ALL ? lpres_conjoin(v, out[k->ref[r]].value)
                                          : lpres_disjoin(v, out[k->ref[r]].value);
            o->status = POLICY_T_GATE;
            o->value = v;
            o->evidence = N_;
            o->condition = T_;
            o->compliance = N_;
            continue;
        }
        o->evidence = N_;
        for (uint32_t a = 0; a < n_att; a++) {
            if (att[a].clause != i || !zxp_date_ok(att[a].date) || att[a].date > now) continue;
            o->evidence = policy_k_join(o->evidence, att[a].verdict);
            if (att[a].verdict == T_) {
                o->att_true |= (uint64_t) 1 << a;
                if (o->performed_on == 0 || att[a].date < o->performed_on) o->performed_on = att[a].date;
            } else {
                o->att_false |= (uint64_t) 1 << a;
            }
        }
        o->compliance = term_compliance(c, h, i);
        o->condition = k->after ? out[k->after - 1u].value : T_;
        if (o->condition == F_) {
            o->status = POLICY_T_EXCUSED;
            o->value = T_;
        } else if (o->condition == B_) {
            o->status = POLICY_T_CONTESTED;
            o->value = B_;
        } else if (o->condition == N_) {
            o->status = POLICY_T_WAITING;
            o->value = N_;
        } else if (o->evidence == B_) {
            o->status = POLICY_T_CONTESTED;
            o->value = B_;
        } else if (o->evidence == T_) {
            o->status = o->performed_on <= k->date ? POLICY_T_PERFORMED : POLICY_T_LATE;
            o->value = T_;
        } else if (o->evidence == F_ || now > k->date) {
            o->status = POLICY_T_BREACHED;
            o->value = F_;
        } else {
            o->status = now == k->date ? POLICY_T_DUE : POLICY_T_OPEN;
            o->value = N_;
        }
    }
    return 0;
}

/* ------------------------------------------------------------ text */

typedef struct {
    char *p;
    uint32_t cap, n;
    bool ok;
} tbuf_t;

static void t_c(tbuf_t *b, char ch)
{
    if (b->n + 1u >= b->cap) {
        b->ok = false;
        return;
    }
    b->p[b->n++] = ch;
}
static void t_s(tbuf_t *b, const char *s)
{
    for (uint32_t i = 0; s[i] != 0; i++) t_c(b, s[i]);
}
static void t_u(tbuf_t *b, uint64_t v)
{
    char d[20];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v != 0);
    while (n > 0) t_c(b, d[--n]);
}
static void t_hex(tbuf_t *b, const uint8_t *x, uint32_t n)
{
    static const char H[] = "0123456789abcdef";
    for (uint32_t i = 0; i < n; i++) {
        t_c(b, H[x[i] >> 4]);
        t_c(b, H[x[i] & 15u]);
    }
}
static uint32_t t_end(tbuf_t *b)
{
    if (!b->ok) {
        if (b->cap > 0) b->p[0] = 0;
        return 0;
    }
    b->p[b->n] = 0;
    return b->n;
}

uint32_t policy_att_text(const zxp_contract_t *c, const policy_att_t *a, char *out, uint32_t cap)
{
    if (!c || !a || !out || cap == 0 || zxp_validate(c) != ZXP_OK || a->clause >= c->n_clause ||
        c->clause[a->clause].kind != ZXP_CL_DUE || (a->verdict != T_ && a->verdict != F_) ||
        !zxp_date_ok(a->date))
        return 0;
    tbuf_t b = {out, cap, 0, true};
    t_s(&b, "ATT ");
    t_s(&b, c->name);
    t_c(&b, ' ');
    t_s(&b, c->clause[a->clause].id);
    t_s(&b, a->verdict == T_ ? " T " : " F ");
    t_u(&b, a->date);
    t_c(&b, ' ');
    t_u(&b, a->attestor);
    t_c(&b, ' ');
    t_hex(&b, c->doc, 32u);
    t_c(&b, '\n');
    return t_end(&b);
}

/* ------------------------------------------------------------ settlement */

static bool add_owe(zxt_treaty_t *t, uint8_t from, uint8_t to, int64_t amount)
{
    if (t->n_owe >= ZXT_MAX_OWES) return false;
    t->owe[t->n_owe].from = from;
    t->owe[t->n_owe].to = to;
    t->owe[t->n_owe].amount = amount;
    t->n_owe++;
    return true;
}

int32_t policy_lower(const zxp_contract_t *c, const policy_host_t *h, const policy_att_t *att,
                     uint32_t n_att, uint32_t now, const char asset[4], zxt_treaty_t *out,
                     policy_settle_t *rep)
{
    policy_term_t tm[ZXP_MAX_CLAUSES];
    int32_t st;
    if (!out || !rep) return ZXP_E_ARG;
    zxt_clear(out);
    zero_bytes(rep, (uint32_t) sizeof(*rep));
    if (!c || !asset || !asset_ok(asset)) return ZXP_E_ARG;
    if ((st = policy_terms(c, h, att, n_att, now, tm)) != 0) return st;

    for (uint32_t i = 0; i < ZXP_NAME_MAX && c->name[i] != 0; i++) out->name[i] = c->name[i];
    for (uint32_t i = 0; i < 3u; i++) out->asset[i] = asset[i];
    out->op = ZXT_OP_NET;
    out->n_party = c->n_party;
    for (uint32_t p = 0; p < c->n_party; p++)
        for (uint32_t i = 0; i <= ZXP_ID_MAX; i++) out->party[p].id[i] = c->party[p].id[i];

    uint8_t escrow = c->n_party; /* its index, if used */
    for (uint32_t i = 0; i < c->n_clause; i++) {
        const zxp_clause_t *k = &c->clause[i];
        const policy_term_t *o = &tm[i];
        if (k->kind != ZXP_CL_DUE || !str_eq(k->asset, asset, 4u)) continue;
        uint8_t reason = 0;
        if (o->compliance == F_) {
            rep->blocked[rep->n_blocked++] = (uint8_t) i;
            continue;
        }
        if (o->compliance == B_) reason = POLICY_ESC_COMPLIANCE;
        else if (o->condition == B_) reason = POLICY_ESC_CONDITION;
        else if (o->evidence == B_) reason = POLICY_ESC_EVIDENCE;
        if (reason != 0) {
            /* Only an obligation that is or would be payable is escrowed:
             * a contested term not yet at its date still defers. */
            if (now < k->date && o->evidence != B_) {
                rep->n_deferred++;
                continue;
            }
            policy_escrow_t *e = &rep->escrow[rep->n_escrow++];
            e->clause = (uint8_t) i;
            e->reason = reason;
            e->offset = zxp_clause_offset(c, i);
            e->amount = k->amount;
            e->att_true = o->att_true;
            e->att_false = o->att_false;
            if (!add_owe(out, k->payer, escrow, k->amount)) goto overflow;
            rep->n_lines++;
            continue;
        }
        switch (o->status) {
        case POLICY_T_DUE:
            if (!add_owe(out, k->payer, k->payee, k->amount)) goto overflow;
            rep->n_lines++;
            break;
        case POLICY_T_BREACHED:
            if (!add_owe(out, k->payer, k->payee, k->amount)) goto overflow;
            rep->n_lines++;
            if (k->penalty > 0) {
                if (!add_owe(out, k->payer, k->payee, k->penalty)) goto overflow;
                rep->n_lines++;
            }
            break;
        case POLICY_T_OPEN:
        case POLICY_T_WAITING:
            rep->n_deferred++;
            break;
        default: /* PERFORMED, LATE, EXCUSED: nothing to settle */
            break;
        }
    }
    if (rep->n_escrow > 0) {
        const char *e = ZXP_ESCROW_ID;
        for (uint32_t i = 0; e[i] != 0; i++) out->party[escrow].id[i] = e[i];
        out->n_party++;
    }
    for (uint32_t p = 0; p < out->n_party; p++) {
        out->party[p].w_num = 1;
        out->party[p].w_den = out->n_party;
    }
    if (out->n_party == 1) out->party[0].w_den = 1;
    if ((st = zxt_validate(out)) != ZXT_OK) {
        zxt_clear(out);
        zero_bytes(rep, (uint32_t) sizeof(*rep));
        return st;
    }
    return ZXT_OK;
overflow:
    zxt_clear(out);
    zero_bytes(rep, (uint32_t) sizeof(*rep));
    return ZXT_E_COUNT;
}

uint32_t policy_bundle_text(const zxp_contract_t *c, const policy_escrow_t *e,
                            const policy_att_t *att, uint32_t n_att, char *out, uint32_t cap)
{
    if (!c || !e || !out || cap == 0 || (n_att > 0 && !att) || n_att > POLICY_MAX_ATTS ||
        zxp_validate(c) != ZXP_OK || e->clause >= c->n_clause || e->reason < POLICY_ESC_EVIDENCE ||
        e->reason > POLICY_ESC_COMPLIANCE)
        return 0;
    const zxp_clause_t *k = &c->clause[e->clause];
    tbuf_t b = {out, cap, 0, true};
    t_s(&b, "MEDIATION ");
    t_s(&b, c->name);
    t_c(&b, ' ');
    t_s(&b, k->id);
    t_s(&b, " AT ");
    t_u(&b, (uint64_t) (e->offset < 0 ? 0 : e->offset));
    t_s(&b, e->reason == POLICY_ESC_EVIDENCE    ? " EVIDENCE\nDOC "
             : e->reason == POLICY_ESC_CONDITION ? " CONDITION\nDOC "
                                                  : " COMPLIANCE\nDOC ");
    t_hex(&b, c->doc, 32u);
    t_s(&b, "\nAMOUNT ");
    t_s(&b, k->asset);
    t_c(&b, ' ');
    t_u(&b, (uint64_t) e->amount);
    t_c(&b, '\n');
    for (uint32_t side = 0; side < 2u; side++) {
        uint64_t m = side == 0 ? e->att_true : e->att_false;
        for (uint32_t a = 0; a < n_att; a++) {
            if (!((m >> a) & 1u)) continue;
            t_s(&b, side == 0 ? "FOR " : "AGAINST ");
            t_u(&b, att[a].attestor);
            t_c(&b, ' ');
            t_u(&b, att[a].date);
            t_c(&b, '\n');
        }
    }
    t_s(&b, "END\n");
    return t_end(&b);
}

const char *policy_decision_name(policy_decision_t d)
{
    switch (d) {
    case POLICY_ALLOW: return "POLICY_ALLOW";
    case POLICY_DENY: return "POLICY_DENY";
    case POLICY_FAULT: return "POLICY_FAULT";
    }
    return "POLICY_FAULT";
}

const char *policy_term_status_name(uint8_t s)
{
    static const char *const n[] = {"waiting", "open", "due", "performed", "late",
                                    "breached", "contested", "excused", "gate"};
    return s < sizeof n / sizeof n[0] ? n[s] : "unknown";
}
