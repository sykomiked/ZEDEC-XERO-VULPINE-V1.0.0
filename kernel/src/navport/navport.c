/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* navport.c — naval ports (navport.h N1-N9). Integer only. */
#include "navport.h"
#include "pay_util.h"
#include "pay_assure.h"
#include "../robin_debanks/sha256.h"

#define MAXM NAVP_MAX_MEMBERS

static const char CLOSE_DOM[17] = "zxv/navp-close/v1";
static const char PROP_DOM[16] = "zxv/navp-prop/v1";
static const char EXIT_DOM[16] = "zxv/navp-exit/v1";

static void le32(uint8_t *o, uint32_t v)
{
    for (int i = 0; i < 4; i++) o[i] = (uint8_t) (v >> (8 * i));
}
static void le64(uint8_t *o, uint64_t v)
{
    for (int i = 0; i < 8; i++) o[i] = (uint8_t) (v >> (8 * i));
}
static void h32(sha256_ctx_t *c, uint32_t v)
{
    uint8_t b[4];
    le32(b, v);
    sha256_update(c, b, 4);
}
static void h64(sha256_ctx_t *c, uint64_t v)
{
    uint8_t b[8];
    le64(b, v);
    sha256_update(c, b, 8);
}

void navp_config_default(navp_config_t *c)
{
    pay_memset(c, 0, sizeof *c);
    c->mode = NAVP_SUSPEND;
    c->stale_cap_bps = 1000;
    c->min_stake = 0;
    c->quorum = 0;
    c->yield_enabled = false;
    c->fee_weight = 0;
}

navp_status_t navp_init(navp_t *p, uint32_t port_id, const navp_config_t *cfg,
                        navp_verify_fn verify, void *vctx)
{
    if (!p || !verify) return NAVP_E_ARG;
    pay_memset(p, 0, sizeof *p);
    if (cfg) {
        if (cfg->stale_cap_bps > 10000 || cfg->quorum > MAXM ||
            (cfg->mode != NAVP_SUSPEND && cfg->mode != NAVP_DEFEND) ||
            cfg->min_stake >= NAVP_AMOUNT_MAX)
            return NAVP_E_ARG;
        p->cfg = *cfg;
    } else {
        navp_config_default(&p->cfg);
    }
    p->port_id = port_id;
    p->verify = verify;
    p->vctx = vctx;
    p->next_prop = 1;
    return NAVP_OK;
}

const char *navp_status_name(navp_status_t s)
{
    switch (s) {
    case NAVP_OK:
        return "OK";
    case NAVP_E_ARG:
        return "ARG";
    case NAVP_E_STATE:
        return "STATE";
    case NAVP_E_FULL:
        return "FULL";
    case NAVP_E_DUST:
        return "DUST";
    case NAVP_E_LIMIT:
        return "LIMIT";
    case NAVP_E_DEPEG:
        return "DEPEG";
    case NAVP_E_HELD:
        return "HELD";
    case NAVP_E_STALE:
        return "STALE";
    case NAVP_E_SIG:
        return "SIG";
    case NAVP_E_DUP:
        return "DUP";
    case NAVP_E_QUORUM:
        return "QUORUM";
    case NAVP_E_CONFLICT:
        return "CONFLICT";
    case NAVP_E_SHORT:
        return "SHORT";
    case NAVP_E_OVERFLOW:
        return "OVERFLOW";
    case NAVP_E_DISABLED:
        return "DISABLED";
    case NAVP_E_UNKNOWN:
        return "UNKNOWN";
    }
    return "?";
}

static bool live(const navp_t *p, uint32_t m)
{
    return m < p->n && (p->m[m].state == NAVP_M_ACTIVE || p->m[m].state == NAVP_M_EXITING);
}

static bool peg_ok(uint64_t num, uint64_t den)
{
    return num > 0 && den > 0 && num < NAVP_AMOUNT_MAX && den < NAVP_AMOUNT_MAX;
}

static navp_status_t slot_fill(navp_t *p, uint32_t idx, uint32_t key, const char *code,
                               uint64_t num, uint64_t den, uint64_t stake)
{
    navp_member_t *mm = &p->m[idx];
    pay_memset(mm, 0, sizeof *mm);
    mm->key = key;
    pay_strlcpy(mm->code, code, sizeof mm->code);
    mm->num = num;
    mm->den = den;
    mm->share = stake;
    mm->state = NAVP_M_ACTIVE;
    return NAVP_OK;
}

static bool code_ok(const char *code)
{
    size_t l = code ? pay_strnlen(code, NAVP_CODE_MAX + 1) : 0;
    return l > 0 && l <= NAVP_CODE_MAX;
}

static bool key_taken(const navp_t *p, uint32_t key)
{
    for (uint32_t i = 0; i < p->n; i++)
        if (p->m[i].state != NAVP_M_FREE && p->m[i].key == key) return true;
    return false;
}

navp_status_t navp_member_add(navp_t *p, uint32_t key, const char *code, uint64_t num, uint64_t den,
                              uint64_t stake, uint32_t *out_idx)
{
    if (!p || !code_ok(code) || !peg_ok(num, den) || stake >= NAVP_AMOUNT_MAX ||
        stake < p->cfg.min_stake)
        return NAVP_E_ARG;
    if (p->started || p->cycle != 0 || p->pstate != NAVP_OPEN) return NAVP_E_STATE;
    if (p->n >= MAXM) return NAVP_E_FULL;
    if (key_taken(p, key)) return NAVP_E_DUP;
    uint32_t i = p->n++;
    slot_fill(p, i, key, code, num, den, stake);
    p->genesis += stake;
    if (out_idx) *out_idx = i;
    return NAVP_OK;
}

void navp_position(const navp_t *p, uint32_t m, uint64_t *liab, uint64_t *claim)
{
    uint64_t l = 0, c = 0;
    for (uint32_t j = 0; j < p->n; j++) {
        l += p->val[j][m]; /* bounded: each val < 2^60, at most 8 terms */
        c += p->val[m][j];
    }
    *liab = l;
    *claim = c;
}

static uint64_t net_debit(const navp_t *p, uint32_t m)
{
    uint64_t l, c;
    navp_position(p, m, &l, &c);
    return l > c ? l - c : 0;
}

navp_status_t navp_deposit(navp_t *p, uint32_t m, uint64_t vbills)
{
    if (!p || vbills == 0) return NAVP_E_ARG;
    if (!live(p, m)) return NAVP_E_UNKNOWN;
    if (p->m[m].share + vbills >= NAVP_AMOUNT_MAX || p->deposits + vbills >= ((uint64_t) 1 << 63))
        return NAVP_E_OVERFLOW;
    p->m[m].share += vbills;
    p->deposits += vbills;
    return NAVP_OK;
}

navp_status_t navp_withdraw(navp_t *p, uint32_t m, uint64_t vbills)
{
    if (!p || vbills == 0) return NAVP_E_ARG;
    if (!live(p, m)) return NAVP_E_UNKNOWN;
    if (p->pstate != NAVP_OPEN) return NAVP_E_STATE;
    uint64_t need = p->cfg.min_stake + net_debit(p, m);
    if (vbills > p->m[m].share || p->m[m].share - vbills < need) return NAVP_E_LIMIT;
    p->m[m].share -= vbills;
    p->withdrawals += vbills;
    return NAVP_OK;
}

static navp_lp_t lp_of(uint32_t yes, uint32_t no)
{
    if (yes && no) return NAVP_BOTH;
    if (yes) return NAVP_TRUE;
    if (no) return NAVP_FALSE;
    return NAVP_NEITHER;
}

navp_lp_t navp_health(const navp_t *p, uint32_t m)
{
    if (!p || m >= p->n) return NAVP_NEITHER;
    return lp_of(p->m[m].on_votes, p->m[m].off_votes);
}

navp_lp_t navp_evidence(navp_t *p, uint32_t m, uint32_t on, uint32_t off)
{
    if (!p || !live(p, m)) return NAVP_NEITHER;
    navp_member_t *mm = &p->m[m];
    mm->on_votes = (mm->on_votes + on < mm->on_votes) ? UINT32_MAX : mm->on_votes + on;
    mm->off_votes = (mm->off_votes + off < mm->off_votes) ? UINT32_MAX : mm->off_votes + off;
    return navp_health(p, m);
}

navp_status_t navp_quote(const navp_t *p, uint32_t from, uint32_t to, uint64_t x, navp_quote_t *q)
{
    if (!p || !q || from == to || x == 0 || x >= NAVP_AMOUNT_MAX) return NAVP_E_ARG;
    if (from >= p->n || to >= p->n || p->m[from].state == NAVP_M_FREE ||
        p->m[to].state == NAVP_M_FREE)
        return NAVP_E_UNKNOWN;
    const navp_member_t *f = &p->m[from], *t = &p->m[to];
    pay_memset(q, 0, sizeof *q);
    q->fee = pay_assure_fee(x);
    uint64_t xn = x - q->fee, v, y, xu, rem;
    if (!pay_muldiv(xn, f->den, f->num, &v, NULL) || v >= NAVP_AMOUNT_MAX) return NAVP_E_OVERFLOW;
    if (!pay_muldiv(v, t->num, t->den, &y, NULL) || y >= NAVP_AMOUNT_MAX) return NAVP_E_OVERFLOW;
    if (v == 0 || y == 0) return NAVP_E_DUST;
    if (!pay_muldiv(v, f->num, f->den, &xu, &rem)) return NAVP_E_OVERFLOW;
    if (rem) xu++;
    pay_muldiv(q->fee, f->den, f->num, &q->fee_v, NULL);
    q->v = v;
    q->y = y;
    q->x_used = xu; /* <= xn because v <= xn * den / num */
    return NAVP_OK;
}

navp_status_t navp_convert(navp_t *p, uint32_t from, uint32_t to, uint64_t x, navp_quote_t *q)
{
    navp_quote_t tmp;
    if (!q) q = &tmp;
    navp_status_t s = navp_quote(p, from, to, x, q);
    if (s != NAVP_OK) return s;
    if (p->pstate != NAVP_OPEN) return NAVP_E_STATE;
    if (p->m[from].state != NAVP_M_ACTIVE || p->m[to].state != NAVP_M_ACTIVE) return NAVP_E_STATE;
    navp_member_t *f = &p->m[from];
    /* N5: outflow from `from` */
    switch (navp_health(p, from)) {
    case NAVP_TRUE:
        break;
    case NAVP_BOTH:
        return NAVP_E_HELD;
    case NAVP_FALSE:
        if (p->cfg.mode != NAVP_DEFEND) return NAVP_E_DEPEG;
        break;
    case NAVP_NEITHER: {
        uint64_t cap;
        pay_muldiv(f->share, p->cfg.stale_cap_bps, 10000, &cap, NULL);
        if (f->stale_used + q->v > cap) return NAVP_E_STALE;
        break;
    }
    }
    /* N4 */
    uint64_t l, c;
    navp_position(p, from, &l, &c);
    if (l + q->v > c + f->share) return NAVP_E_LIMIT;
    if (p->val[to][from] + q->v >= NAVP_AMOUNT_MAX ||
        p->hold[to][from] + q->x_used >= NAVP_AMOUNT_MAX || f->fee_due + q->fee >= NAVP_AMOUNT_MAX)
        return NAVP_E_OVERFLOW;
    if (navp_health(p, from) == NAVP_NEITHER) f->stale_used += q->v;
    p->hold[to][from] += q->x_used;
    p->val[to][from] += q->v;
    f->fee_due += q->fee;
    f->fee_v += q->fee_v;
    p->started = true;
    return NAVP_OK;
}

navp_status_t navp_books_report(navp_t *p, uint32_t m, const uint64_t nostro[NAVP_MAX_MEMBERS],
                                const uint64_t vostro[NAVP_MAX_MEMBERS])
{
    if (!p || !nostro || !vostro) return NAVP_E_ARG;
    if (!live(p, m)) return NAVP_E_UNKNOWN;
    if (p->pstate != NAVP_OPEN) return NAVP_E_STATE;
    for (uint32_t j = 0; j < MAXM; j++) {
        p->nostro_rep[m][j] = nostro[j];
        p->vostro_rep[m][j] = vostro[j];
    }
    p->reported |= (uint8_t) (1u << m);
    return NAVP_OK;
}

uint32_t navp_quorum(const navp_t *p)
{
    uint32_t n = 0;
    for (uint32_t i = 0; i < p->n; i++)
        if (live(p, i)) n++;
    uint32_t q = p->cfg.quorum ? p->cfg.quorum : (2 * n) / 3 + 1;
    return q > n ? n : q;
}

/* N6: pair (i holds j's vouchers). */
static navp_lp_t pair_tag(const navp_t *p, uint32_t i, uint32_t j)
{
    uint32_t yes = 0, no = 0;
    if (p->reported & (1u << i)) {
        if (p->nostro_rep[i][j] == p->hold[i][j])
            yes++;
        else
            no++;
    }
    if (p->reported & (1u << j)) {
        if (p->vostro_rep[j][i] == p->hold[i][j])
            yes++;
        else
            no++;
    }
    return lp_of(yes, no);
}

static bool included(const navp_t *p, uint32_t i, uint32_t j)
{
    if (p->hold[i][j] == 0 && p->val[i][j] == 0)
        return p->tag[i][j] != NAVP_BOTH && p->tag[i][j] != NAVP_FALSE;
    return p->tag[i][j] == NAVP_TRUE;
}

static void settle_hash(navp_t *p)
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) CLOSE_DOM, sizeof CLOSE_DOM);
    h32(&c, p->port_id);
    h64(&c, p->cycle);
    h32(&c, p->n);
    for (uint32_t i = 0; i < p->n; i++) {
        h32(&c, p->m[i].key);
        h64(&c, p->m[i].num);
        h64(&c, p->m[i].den);
        h64(&c, p->m[i].share);
        h64(&c, (uint64_t) p->net[i]);
        for (uint32_t j = 0; j < p->n; j++) {
            uint8_t t = p->tag[i][j];
            sha256_update(&c, &t, 1);
            h64(&c, p->hold[i][j]);
            h64(&c, p->val[i][j]);
        }
    }
    sha256_final(&c, p->hash);
}

navp_status_t navp_close_prepare(navp_t *p)
{
    if (!p) return NAVP_E_ARG;
    if (p->pstate != NAVP_OPEN) return NAVP_E_STATE;
    int64_t net[MAXM] = {0};
    uint64_t gross = 0, settled = 0;
    for (uint32_t i = 0; i < p->n; i++)
        for (uint32_t j = 0; j < p->n; j++)
            p->tag[i][j] = (i == j) ? NAVP_TRUE : (uint8_t) pair_tag(p, i, j);
    for (uint32_t i = 0; i < p->n; i++)
        for (uint32_t j = 0; j < p->n; j++) {
            if (i == j || !included(p, i, j)) continue;
            uint64_t v = p->val[i][j]; /* i holds j's: j owes i */
            net[i] += (int64_t) v;
            net[j] -= (int64_t) v;
            gross += v;
        }
    for (uint32_t i = 0; i < p->n; i++) {
        if (net[i] < 0 && (uint64_t) (-net[i]) > p->m[i].share) return NAVP_E_SHORT;
        if (net[i] > 0) settled += (uint64_t) net[i];
        p->net[i] = net[i];
    }
    p->gross = gross;
    p->settled = settled;
    p->agree = p->conflict = 0;
    settle_hash(p);
    p->pstate = NAVP_CLOSING;
    return NAVP_OK;
}

void navp_close_msg(uint32_t port_id, uint64_t cycle, const uint8_t hash[32],
                    uint8_t out[NAVP_CLOSE_MSG])
{
    pay_memcpy(out, CLOSE_DOM, 17);
    le32(out + 17, port_id);
    le64(out + 21, cycle);
    pay_memcpy(out + 29, hash, 32);
}

navp_status_t navp_close_vote(navp_t *p, uint32_t m, const uint8_t hash[32], const uint8_t *sig,
                              uint32_t sig_len)
{
    if (!p || !hash || !sig) return NAVP_E_ARG;
    if (!live(p, m)) return NAVP_E_UNKNOWN;
    if (p->pstate != NAVP_CLOSING) return NAVP_E_STATE;
    uint8_t bit = (uint8_t) (1u << m);
    if ((p->agree | p->conflict) & bit) return NAVP_E_DUP;
    uint8_t msg[NAVP_CLOSE_MSG];
    navp_close_msg(p->port_id, p->cycle, hash, msg);
    if (!p->verify(p->vctx, p->m[m].key, msg, sizeof msg, sig, sig_len)) return NAVP_E_SIG;
    if (pay_memeq(hash, p->hash, 32))
        p->agree |= bit;
    else
        p->conflict |= bit;
    return NAVP_OK;
}

static uint32_t popcount8(uint8_t b)
{
    uint32_t c = 0;
    for (; b; b &= (uint8_t) (b - 1)) c++;
    return c;
}

static bool carries(const navp_t *p, uint32_t m)
{
    for (uint32_t j = 0; j < p->n; j++)
        if (p->hold[m][j] || p->hold[j][m] || p->val[m][j] || p->val[j][m]) return true;
    return false;
}

navp_status_t navp_close_commit(navp_t *p)
{
    if (!p) return NAVP_E_ARG;
    if (p->pstate != NAVP_CLOSING) return NAVP_E_STATE;
    if (p->conflict) return NAVP_E_CONFLICT;
    if (popcount8(p->agree) < navp_quorum(p)) return NAVP_E_QUORUM;
    for (uint32_t i = 0; i < p->n; i++)
        for (uint32_t j = 0; j < p->n; j++)
            if (i != j && included(p, i, j)) {
                p->hold[i][j] = 0;
                p->val[i][j] = 0;
            }
    for (uint32_t i = 0; i < p->n; i++) {
        if (p->net[i] >= 0)
            p->m[i].share += (uint64_t) p->net[i];
        else
            p->m[i].share -= (uint64_t) (-p->net[i]);
        p->net[i] = 0;
    }
    p->total_gross += p->gross;
    p->total_settled += p->settled;
    p->closes++;
    /* N8: ratified proposals */
    for (uint32_t k = 0; k < NAVP_MAX_PROPOSALS; k++) {
        navp_proposal_t *r = &p->prop[k];
        if (!r->kind || !r->ratified) continue;
        if (r->kind == NAVP_PROP_PEG && live(p, r->member)) {
            p->m[r->member].num = r->num;
            p->m[r->member].den = r->den;
        } else if (r->kind == NAVP_PROP_ADMIT && p->n < MAXM && !key_taken(p, r->key)) {
            uint32_t idx = p->n++;
            slot_fill(p, idx, r->key, r->code, r->num, r->den, 0);
            /* the stake arrives as a deposit, counted for conservation */
            p->m[idx].share = r->stake;
            p->deposits += r->stake;
        }
        pay_memset(r, 0, sizeof *r);
    }
    /* N8: exits with nothing carried */
    for (uint32_t i = 0; i < p->n; i++)
        if (p->m[i].state == NAVP_M_EXITING && !carries(p, i)) {
            p->m[i].returned = p->m[i].share;
            p->m[i].share = 0;
            p->m[i].state = NAVP_M_EXITED;
        }
    for (uint32_t i = 0; i < p->n; i++) {
        p->m[i].on_votes = p->m[i].off_votes = 0;
        p->m[i].stale_used = 0;
    }
    p->reported = 0;
    p->agree = p->conflict = 0;
    p->cycle++;
    p->pstate = NAVP_OPEN;
    return NAVP_OK;
}

navp_status_t navp_close_abort(navp_t *p)
{
    if (!p) return NAVP_E_ARG;
    if (p->pstate != NAVP_CLOSING) return NAVP_E_STATE;
    for (uint32_t i = 0; i < MAXM; i++) p->net[i] = 0;
    p->agree = p->conflict = 0;
    p->pstate = NAVP_OPEN;
    return NAVP_OK;
}

static navp_proposal_t *prop_slot(navp_t *p)
{
    for (uint32_t k = 0; k < NAVP_MAX_PROPOSALS; k++)
        if (!p->prop[k].kind) return &p->prop[k];
    return NULL;
}

static navp_proposal_t *prop_find(navp_t *p, uint32_t id)
{
    for (uint32_t k = 0; k < NAVP_MAX_PROPOSALS; k++)
        if (p->prop[k].kind && p->prop[k].id == id) return &p->prop[k];
    return NULL;
}

static void prop_digest(navp_proposal_t *r)
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, &r->kind, 1);
    sha256_update(&c, &r->member, 1);
    h32(&c, r->key);
    sha256_update(&c, (const uint8_t *) r->code, NAVP_CODE_MAX + 1);
    h64(&c, r->num);
    h64(&c, r->den);
    h64(&c, r->stake);
    sha256_final(&c, r->digest);
}

navp_status_t navp_propose_peg(navp_t *p, uint32_t m, uint64_t num, uint64_t den, uint32_t *out_id)
{
    if (!p || !peg_ok(num, den)) return NAVP_E_ARG;
    if (!live(p, m)) return NAVP_E_UNKNOWN;
    navp_proposal_t *r = prop_slot(p);
    if (!r) return NAVP_E_FULL;
    pay_memset(r, 0, sizeof *r);
    r->id = p->next_prop++;
    r->kind = NAVP_PROP_PEG;
    r->member = (uint8_t) m;
    r->num = num;
    r->den = den;
    prop_digest(r);
    if (out_id) *out_id = r->id;
    return NAVP_OK;
}

navp_status_t navp_propose_admit(navp_t *p, uint32_t key, const char *code, uint64_t num,
                                 uint64_t den, uint64_t stake, uint32_t *out_id)
{
    if (!p || !code_ok(code) || !peg_ok(num, den) || stake >= NAVP_AMOUNT_MAX ||
        stake < p->cfg.min_stake)
        return NAVP_E_ARG;
    if (key_taken(p, key)) return NAVP_E_DUP;
    if (p->n >= MAXM) return NAVP_E_FULL;
    navp_proposal_t *r = prop_slot(p);
    if (!r) return NAVP_E_FULL;
    pay_memset(r, 0, sizeof *r);
    r->id = p->next_prop++;
    r->kind = NAVP_PROP_ADMIT;
    r->key = key;
    pay_strlcpy(r->code, code, sizeof r->code);
    r->num = num;
    r->den = den;
    r->stake = stake;
    prop_digest(r);
    if (out_id) *out_id = r->id;
    return NAVP_OK;
}

navp_status_t navp_prop_msg(const navp_t *p, uint32_t id, uint8_t out[NAVP_PROP_MSG])
{
    if (!p || !out) return NAVP_E_ARG;
    navp_proposal_t *r = prop_find((navp_t *) p, id);
    if (!r) return NAVP_E_UNKNOWN;
    pay_memcpy(out, PROP_DOM, 16);
    le32(out + 16, p->port_id);
    le32(out + 20, id);
    pay_memcpy(out + 24, r->digest, 32);
    return NAVP_OK;
}

navp_status_t navp_approve(navp_t *p, uint32_t id, uint32_t m, const uint8_t *sig, uint32_t sig_len)
{
    if (!p || !sig) return NAVP_E_ARG;
    navp_proposal_t *r = prop_find(p, id);
    if (!r) return NAVP_E_UNKNOWN;
    if (!live(p, m)) return NAVP_E_UNKNOWN;
    uint8_t bit = (uint8_t) (1u << m);
    if (r->approved & bit) return NAVP_E_DUP;
    uint8_t msg[NAVP_PROP_MSG];
    navp_prop_msg(p, id, msg);
    if (!p->verify(p->vctx, p->m[m].key, msg, sizeof msg, sig, sig_len)) return NAVP_E_SIG;
    r->approved |= bit;
    uint32_t votes = 0;
    for (uint32_t i = 0; i < p->n; i++)
        if (live(p, i) && (r->approved & (1u << i))) votes++;
    bool own = r->kind != NAVP_PROP_PEG || (r->approved & (1u << r->member));
    r->ratified = (uint8_t) (own && votes >= navp_quorum(p));
    return NAVP_OK;
}

void navp_exit_msg(uint32_t port_id, uint32_t m, uint8_t out[NAVP_EXIT_MSG])
{
    pay_memcpy(out, EXIT_DOM, 16);
    le32(out + 16, port_id);
    le32(out + 20, m);
}

navp_status_t navp_exit(navp_t *p, uint32_t m, const uint8_t *sig, uint32_t sig_len)
{
    if (!p || !sig) return NAVP_E_ARG;
    if (m >= p->n || p->m[m].state != NAVP_M_ACTIVE) return NAVP_E_STATE;
    uint8_t msg[NAVP_EXIT_MSG];
    navp_exit_msg(p->port_id, m, msg);
    if (!p->verify(p->vctx, p->m[m].key, msg, sizeof msg, sig, sig_len)) return NAVP_E_SIG;
    p->m[m].state = NAVP_M_EXITING;
    return NAVP_OK;
}

navp_status_t navp_yield(navp_t *p, uint64_t pool, uint64_t shares[NAVP_MAX_MEMBERS])
{
    if (!p || !shares) return NAVP_E_ARG;
    if (!p->cfg.yield_enabled) return NAVP_E_DISABLED;
    uint64_t u[MAXM] = {0}, U = 0, rem[MAXM] = {0}, paid = 0;
    for (uint32_t i = 0; i < MAXM; i++) shares[i] = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        if (!live(p, i)) continue;
        pay_u128 w = pay_mul64(p->m[i].fee_v, p->cfg.fee_weight);
        if (w.hi || !pay_add_ok(p->m[i].share, w.lo, &u[i]) || !pay_add_ok(U, u[i], &U))
            return NAVP_E_OVERFLOW;
    }
    if (U == 0 || pool == 0) return NAVP_E_ARG;
    for (uint32_t i = 0; i < p->n; i++) {
        if (!pay_muldiv(pool, u[i], U, &shares[i], &rem[i])) return NAVP_E_OVERFLOW;
        paid += shares[i];
    }
    /* largest remainders, ties to the lower index */
    for (uint64_t left = pool - paid; left > 0; left--) {
        uint32_t best = MAXM;
        for (uint32_t i = 0; i < p->n; i++)
            if (u[i] && (best == MAXM || rem[i] > rem[best])) best = i;
        shares[best]++;
        rem[best] = 0;
    }
    for (uint32_t i = 0; i < p->n; i++) p->m[i].fee_v = 0;
    return NAVP_OK;
}

bool navp_check(const navp_t *p)
{
    if (!p) return false;
    uint64_t total = 0, returned = 0;
    for (uint32_t i = 0; i < p->n; i++) {
        total += p->m[i].share;
        returned += p->m[i].returned;
        if (p->m[i].state == NAVP_M_EXITED && (p->m[i].share || carries(p, i))) return false;
        if (live(p, i) && net_debit(p, i) > p->m[i].share) return false;
        if (p->val[i][i] || p->hold[i][i]) return false;
        for (uint32_t j = 0; j < p->n; j++)
            if ((p->hold[i][j] == 0) != (p->val[i][j] == 0)) return false;
    }
    return total + returned + p->withdrawals == p->genesis + p->deposits;
}
