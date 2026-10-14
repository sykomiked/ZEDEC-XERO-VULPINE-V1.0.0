/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* royalty.c — IP royalties from realised sales only. See royalty.h R1-R13. */
#include "royalty.h"
#include "../robin_debanks/sha256.h"

static const char DOM_WORK[] = "zxv/royalty-work/v1";   /* 19 bytes */
static const char DOM_SPLIT[] = "zxv/royalty-split/v1"; /* 20 bytes */
static const char DOM_QUOTE[] = "zxv/royalty-quote/v1"; /* 20 bytes */

/* ---- little-endian encoding ---- */
static uint32_t put(uint8_t *o, uint32_t at, uint64_t v, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) o[at + i] = (uint8_t) (v >> (8u * i));
    return at + n;
}

static uint32_t put_bytes(uint8_t *o, uint32_t at, const void *p, uint32_t n)
{
    if (p)
        pay_memcpy(o + at, p, n);
    else
        pay_memset(o + at, 0, n);
    return at + n;
}

static void h_u(sha256_ctx_t *c, uint64_t v, uint32_t n)
{
    uint8_t b[8];
    put(b, 0, v, n);
    sha256_update(c, b, n);
}

/* ---- accounts ---- */
static bool acct_ok(const roy_t *r, const pay_ledger_t *L, uint32_t a)
{
    return a < L->n_accounts && L->acct[a].active && L->acct[a].asset == r->asset &&
           L->acct[a].cap == r->cap && a != r->fee_acct;
}

roy_status_t roy_init(roy_t *r, const pay_ledger_t *L, uint32_t fee_acct, roy_verify_fn verify,
                      void *vctx)
{
    if (!r || !L || !verify) return ROY_ERR_ARG;
    pay_memset(r, 0, sizeof *r);
    if (fee_acct >= L->n_accounts || !L->acct[fee_acct].active ||
        (L->acct[fee_acct].flags & PAY_ACCT_ISSUER))
        return ROY_ERR_ARG;
    r->fee_acct = fee_acct;
    r->asset = L->acct[fee_acct].asset;
    r->cap = L->acct[fee_acct].cap;
    r->verify = verify;
    r->vctx = vctx;
    r->last_ledger = PAY_OK;
    r->ready = true;
    return ROY_OK;
}

/* ---- R3 / R6 ---- */
bool roy_terms_ok(const roy_terms_t *t)
{
    if (!t) return false;
    if (t->kind == ROY_TERMS_PERCENT)
        return t->den > 0 && t->num > 0 && t->num <= t->den && t->flat == 0;
    if (t->kind == ROY_TERMS_FLAT) return t->flat > 0 && t->num == 0 && t->den == 0;
    return false;
}

void roy_terms_zxv_hw(roy_terms_t *t)
{
    if (t) *t = ROY_TERMS_ZXV_HW_COMMERCIAL;
}

bool roy_royalty(const roy_terms_t *t, uint64_t gross, uint64_t *royalty)
{
    uint64_t v;
    if (!royalty || !roy_terms_ok(t) || gross == 0 || gross >= PAY_BAL_MAX) return false;
    if (t->kind == ROY_TERMS_FLAT) {
        if (gross < t->flat) return false;
        v = t->flat;
    } else {
        pay_muldiv(gross, t->num, t->den, &v, 0); /* num <= den: v <= gross fits */
    }
    *royalty = v;
    return true;
}

/* ---- R1 / R2 manifests ---- */
static const uint8_t ZERO_ID[ROY_ID_LEN];

static bool zero32(const uint8_t *p)
{
    return pay_memeq(p, ZERO_ID, ROY_ID_LEN);
}

static bool manifest_ok(const roy_t *r, const pay_ledger_t *L, const roy_manifest_t *m)
{
    uint64_t sum = 0;
    if (m->version == 0 || m->n_owners == 0 || m->n_owners > ROY_MAX_OWNERS) return false;
    if (!roy_terms_ok(&m->terms)) return false;
    if (m->has_parent) {
        if (m->up_num == 0 || m->up_num > m->up_den) return false;
    } else if (m->up_num || m->up_den || !zero32(m->parent)) {
        return false;
    }
    for (uint32_t i = 0; i < m->n_owners; i++) {
        const roy_owner_t *o = &m->owner[i];
        if (o->weight == 0 || !pay_add_ok(sum, o->weight, &sum)) return false;
        if (!acct_ok(r, L, o->account)) return false;
        for (uint32_t j = 0; j < i; j++)
            if (m->owner[j].id == o->id) return false;
    }
    return true;
}

void roy_manifest_msg(const roy_manifest_t *m, uint8_t out[ROY_MANIFEST_MSG])
{
    uint32_t at = put_bytes(out, 0, DOM_WORK, 19);
    at = put_bytes(out, at, m->id, ROY_ID_LEN);
    at = put(out, at, m->version, 8);
    at = put(out, at, m->n_owners, 4);
    for (uint32_t i = 0; i < ROY_MAX_OWNERS; i++) {
        bool on = i < m->n_owners;
        at = put(out, at, on ? m->owner[i].id : 0, 4);
        at = put(out, at, on ? m->owner[i].account : 0, 4);
        at = put(out, at, on ? m->owner[i].weight : 0, 8);
    }
    at = put(out, at, m->has_parent ? 1 : 0, 1);
    at = put_bytes(out, at, m->has_parent ? m->parent : 0, ROY_ID_LEN);
    at = put(out, at, m->up_num, 8);
    at = put(out, at, m->up_den, 8);
    at = put(out, at, m->terms.kind, 1);
    at = put(out, at, m->terms.num, 8);
    at = put(out, at, m->terms.den, 8);
    put(out, at, m->terms.flat, 8);
}

int32_t roy_find(const roy_t *r, const uint8_t id[ROY_ID_LEN])
{
    if (!r || !id) return -1;
    for (uint32_t i = 0; i < r->n_works; i++)
        if (r->work[i].used && pay_memeq(r->work[i].m.id, id, ROY_ID_LEN)) return (int32_t) i;
    return -1;
}

const roy_work_t *roy_work(const roy_t *r, const uint8_t id[ROY_ID_LEN])
{
    int32_t i = roy_find(r, id);
    return i < 0 ? 0 : &r->work[i];
}

/* ---- R9 ---- */
typedef struct {
    uint32_t a[ROY_MAX_PAYOUTS * 3u]; /* every owner, giving, investing acct */
    uint32_t n;
} acct_set_t;

static void set_add(acct_set_t *s, uint32_t a)
{
    for (uint32_t i = 0; i < s->n; i++)
        if (s->a[i] == a) return;
    s->a[s->n++] = a;
}

/* lines a payment for work idx may post: payer + fee + distinct receivers
 * (no receiver is the fee account: R1, R8) */
static uint32_t lines_of(const roy_t *r, uint32_t idx)
{
    acct_set_t s;
    s.n = 0;
    for (uint32_t lev = 0; lev < ROY_MAX_LEVELS; lev++) {
        const roy_work_t *w = &r->work[idx];
        for (uint32_t j = 0; j < w->m.n_owners; j++) {
            set_add(&s, w->m.owner[j].account);
            if (w->split[j].giving_bps) set_add(&s, w->split[j].giving_acct);
            if (w->split[j].invest_bps) set_add(&s, w->split[j].invest_acct);
        }
        if (!w->m.has_parent) break;
        idx = w->parent;
    }
    return s.n + 2u;
}

static bool all_fit(const roy_t *r)
{
    for (uint32_t i = 0; i < r->n_works; i++)
        if (r->work[i].used && lines_of(r, i) > PAY_MAX_LINES) return false;
    return true;
}

uint32_t roy_lines(const roy_t *r, const uint8_t id[ROY_ID_LEN])
{
    int32_t i = roy_find(r, id);
    return i < 0 ? 0 : lines_of(r, (uint32_t) i);
}

/* a manifest with unused owner slots and absent parent zeroed */
static void normalise(roy_manifest_t *dst, const roy_manifest_t *m)
{
    *dst = *m;
    for (uint32_t i = m->n_owners; i < ROY_MAX_OWNERS; i++) {
        dst->owner[i].id = dst->owner[i].account = 0;
        dst->owner[i].weight = 0;
    }
}

roy_status_t roy_register(roy_t *r, const pay_ledger_t *L, const roy_manifest_t *m,
                          const uint8_t *sig, uint32_t sig_len)
{
    uint8_t msg[ROY_MANIFEST_MSG];
    uint32_t parent = 0, depth = 0;
    if (!r || !r->ready || !L || !m || !sig) return ROY_ERR_ARG;
    if (!manifest_ok(r, L, m)) return ROY_ERR_ARG;
    if (roy_find(r, m->id) >= 0) return ROY_ERR_DUP;
    if (m->has_parent) {
        int32_t p = roy_find(r, m->parent);
        if (p < 0) return ROY_ERR_UNKNOWN;
        parent = (uint32_t) p;
        depth = r->work[parent].depth + 1u;
        if (depth > ROY_MAX_DEPTH) return ROY_ERR_DEPTH;
    }
    if (r->n_works >= ROY_MAX_WORKS) return ROY_ERR_FULL;
    roy_manifest_msg(m, msg);
    if (!r->verify(r->vctx, m->owner[0].id, msg, ROY_MANIFEST_MSG, sig, sig_len))
        return ROY_ERR_SIG;
    roy_work_t *w = &r->work[r->n_works];
    pay_memset(w, 0, sizeof *w);
    normalise(&w->m, m);
    w->parent = parent;
    w->depth = depth;
    w->used = true;
    r->n_works++;
    if (!all_fit(r)) {
        r->n_works--;
        pay_memset(w, 0, sizeof *w);
        return ROY_ERR_LINES;
    }
    return ROY_OK;
}

roy_status_t roy_update(roy_t *r, const pay_ledger_t *L, const roy_manifest_t *m,
                        const roy_sig_t *sigs, uint32_t n_sigs)
{
    uint8_t msg[ROY_MANIFEST_MSG];
    if (!r || !r->ready || !L || !m || !sigs) return ROY_ERR_ARG;
    int32_t i = roy_find(r, m->id);
    if (i < 0) return ROY_ERR_UNKNOWN;
    roy_work_t *w = &r->work[i];
    if (!manifest_ok(r, L, m)) return ROY_ERR_ARG;
    if (m->has_parent != w->m.has_parent || m->up_num != w->m.up_num || m->up_den != w->m.up_den ||
        !pay_memeq(m->parent, w->m.parent, ROY_ID_LEN))
        return ROY_ERR_PARENT;
    if (m->version <= w->m.version) return ROY_ERR_VERSION;
    if (n_sigs != w->m.n_owners) return ROY_ERR_ARG;
    roy_manifest_msg(m, msg);
    for (uint32_t k = 0; k < n_sigs; k++)
        if (!sigs[k].sig ||
            !r->verify(r->vctx, w->m.owner[k].id, msg, ROY_MANIFEST_MSG, sigs[k].sig, sigs[k].len))
            return ROY_ERR_SIG;
    roy_split_t split[ROY_MAX_OWNERS];
    pay_memset(split, 0, sizeof split);
    for (uint32_t j = 0; j < m->n_owners; j++)
        for (uint32_t k = 0; k < w->m.n_owners; k++)
            if (w->m.owner[k].id == m->owner[j].id) split[j] = w->split[k];
    roy_manifest_t old_m = w->m;
    roy_split_t old_s[ROY_MAX_OWNERS];
    pay_memcpy(old_s, w->split, sizeof old_s);
    normalise(&w->m, m);
    pay_memcpy(w->split, split, sizeof split);
    if (!all_fit(r)) {
        w->m = old_m;
        pay_memcpy(w->split, old_s, sizeof old_s);
        return ROY_ERR_LINES;
    }
    return ROY_OK;
}

/* ---- R8 ---- */
void roy_split_msg(const uint8_t work[ROY_ID_LEN], uint64_t version, uint32_t owner_id,
                   const roy_split_t *s, uint8_t out[ROY_SPLIT_MSG])
{
    uint32_t at = put_bytes(out, 0, DOM_SPLIT, 20);
    at = put_bytes(out, at, work, ROY_ID_LEN);
    at = put(out, at, version, 8);
    at = put(out, at, owner_id, 4);
    at = put(out, at, s->giving_acct, 4);
    at = put(out, at, s->giving_bps, 2);
    at = put(out, at, s->invest_acct, 4);
    at = put(out, at, s->invest_bps, 2);
    put(out, at, s->seq, 8);
}

static bool split_ok(const roy_t *r, const pay_ledger_t *L, const roy_split_t *s)
{
    if ((uint32_t) s->giving_bps + s->invest_bps > ROY_BPS) return false;
    if (s->giving_bps ? !acct_ok(r, L, s->giving_acct) : s->giving_acct != 0) return false;
    if (s->invest_bps ? !acct_ok(r, L, s->invest_acct) : s->invest_acct != 0) return false;
    return true;
}

roy_status_t roy_split_set(roy_t *r, const pay_ledger_t *L, const uint8_t work[ROY_ID_LEN],
                           uint32_t owner_id, const roy_split_t *s, const uint8_t *sig,
                           uint32_t sig_len)
{
    uint8_t msg[ROY_SPLIT_MSG];
    if (!r || !r->ready || !L || !work || !s || !sig) return ROY_ERR_ARG;
    int32_t i = roy_find(r, work);
    if (i < 0) return ROY_ERR_UNKNOWN;
    roy_work_t *w = &r->work[i];
    uint32_t j = 0;
    while (j < w->m.n_owners && w->m.owner[j].id != owner_id) j++;
    if (j == w->m.n_owners) return ROY_ERR_UNKNOWN;
    if (!split_ok(r, L, s)) return ROY_ERR_ARG;
    if (s->seq <= w->split[j].seq) return ROY_ERR_VERSION;
    roy_split_msg(work, w->m.version, owner_id, s, msg);
    if (!r->verify(r->vctx, owner_id, msg, ROY_SPLIT_MSG, sig, sig_len)) return ROY_ERR_SIG;
    roy_split_t old = w->split[j];
    w->split[j] = *s;
    if (!all_fit(r)) {
        w->split[j] = old;
        return ROY_ERR_LINES;
    }
    return ROY_OK;
}

/* ---- R7 largest remainder ---- */
bool roy_lrm(uint64_t amount, const uint64_t *w, uint32_t n, uint64_t *out)
{
    uint64_t W = 0, q[ROY_MAX_OWNERS], rem[ROY_MAX_OWNERS], given = 0;
    bool got[ROY_MAX_OWNERS];
    if (!w || !out || n == 0 || n > ROY_MAX_OWNERS) return false;
    for (uint32_t i = 0; i < n; i++)
        if (w[i] == 0 || !pay_add_ok(W, w[i], &W)) return false;
    for (uint32_t i = 0; i < n; i++) {
        pay_muldiv(amount, w[i], W, &q[i], &rem[i]); /* w <= W: fits */
        given += q[i];                               /* <= amount */
        got[i] = false;
    }
    for (uint64_t left = amount - given; left > 0; left--) {
        uint32_t best = n;
        for (uint32_t i = 0; i < n; i++)
            if (!got[i] && (best == n || rem[i] > rem[best])) best = i;
        got[best] = true;
        q[best]++;
    }
    for (uint32_t i = 0; i < n; i++) out[i] = q[i];
    return true;
}

/* ---- R10 quote ---- */
static bool leg_add(roy_quote_t *q, uint32_t acct, uint64_t amt)
{
    if (amt == 0) return true;
    for (uint32_t i = 0; i < q->n_legs; i++)
        if (q->leg[i].account == acct) {
            q->leg[i].d_debit += (int64_t) amt; /* sum <= royalty < 2^62 */
            return true;
        }
    if (q->n_legs >= PAY_MAX_LINES - 1u) return false; /* the payer needs one */
    q->leg[q->n_legs].account = acct;
    q->leg[q->n_legs].d_debit = (int64_t) amt;
    q->leg[q->n_legs].d_credit = 0;
    q->n_legs++;
    return true;
}

static void digest_of(const roy_t *r, roy_quote_t *q)
{
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) DOM_QUOTE, 20);
    h_u(&c, r->fee_acct, 4);
    h_u(&c, q->gross, 8);
    h_u(&c, q->royalty, 8);
    h_u(&c, q->fee, 8);
    h_u(&c, q->n_levels, 4);
    uint32_t p = 0;
    for (uint32_t l = 0; l < q->n_levels; l++) {
        const roy_level_t *lv = &q->level[l];
        const roy_work_t *w = &r->work[lv->work];
        sha256_update(&c, w->m.id, ROY_ID_LEN);
        h_u(&c, w->m.version, 8);
        h_u(&c, w->m.terms.kind, 1);
        h_u(&c, w->m.terms.num, 8);
        h_u(&c, w->m.terms.den, 8);
        h_u(&c, w->m.terms.flat, 8);
        h_u(&c, w->m.up_num, 8);
        h_u(&c, w->m.up_den, 8);
        h_u(&c, lv->received, 8);
        h_u(&c, lv->upstream, 8);
        h_u(&c, lv->kept, 8);
        h_u(&c, w->m.n_owners, 4);
        for (uint32_t j = 0; j < w->m.n_owners; j++, p++) {
            const roy_split_t *s = &w->split[j];
            const roy_payout_t *po = &q->payout[p];
            h_u(&c, w->m.owner[j].id, 4);
            h_u(&c, w->m.owner[j].account, 4);
            h_u(&c, w->m.owner[j].weight, 8);
            h_u(&c, s->giving_acct, 4);
            h_u(&c, s->giving_bps, 2);
            h_u(&c, s->invest_acct, 4);
            h_u(&c, s->invest_bps, 2);
            h_u(&c, s->seq, 8);
            h_u(&c, po->gross, 8);
            h_u(&c, po->giving, 8);
            h_u(&c, po->investing, 8);
            h_u(&c, po->net, 8);
        }
    }
    sha256_final(&c, q->digest);
}

static roy_status_t build(const roy_t *r, uint32_t idx, uint64_t gross, roy_quote_t *q)
{
    uint64_t R;
    pay_memset(q, 0, sizeof *q);
    if (!roy_royalty(&r->work[idx].m.terms, gross, &R)) return ROY_ERR_ARG;
    q->gross = gross;
    q->royalty = R;
    q->fee = pay_assure_fee(R);
    uint64_t x = R - q->fee; /* fee < R, or 0 */
    for (uint32_t lev = 0; lev < ROY_MAX_LEVELS; lev++) {
        const roy_work_t *w = &r->work[idx];
        roy_level_t *lv = &q->level[q->n_levels++];
        uint64_t u = 0, w8[ROY_MAX_OWNERS], share[ROY_MAX_OWNERS];
        lv->work = idx;
        pay_memcpy(lv->id, w->m.id, ROY_ID_LEN);
        lv->version = w->m.version;
        lv->received = x;
        if (w->m.has_parent) pay_muldiv(x, w->m.up_num, w->m.up_den, &u, 0); /* <= x */
        lv->upstream = u;
        lv->kept = x - u;
        q->upstream_total += u;
        for (uint32_t j = 0; j < w->m.n_owners; j++) w8[j] = w->m.owner[j].weight;
        roy_lrm(lv->kept, w8, w->m.n_owners, share); /* weights checked at R1 */
        for (uint32_t j = 0; j < w->m.n_owners; j++) {
            roy_payout_t *po = &q->payout[q->n_payouts++];
            const roy_split_t *s = &w->split[j];
            po->level = lev;
            po->owner_id = w->m.owner[j].id;
            po->account = w->m.owner[j].account;
            po->giving_acct = s->giving_acct;
            po->invest_acct = s->invest_acct;
            po->gross = share[j];
            pay_muldiv(share[j], s->giving_bps, ROY_BPS, &po->giving, 0);
            pay_muldiv(share[j], s->invest_bps, ROY_BPS, &po->investing, 0);
            po->net = share[j] - po->giving - po->investing; /* bps sum <= 10000 */
            q->owners_total += po->net;
            q->giving_total += po->giving;
            q->investing_total += po->investing;
        }
        if (!w->m.has_parent) break;
        x = u;
        idx = w->parent;
    }
    bool ok = true;
    for (uint32_t p = 0; p < q->n_payouts; p++) {
        const roy_payout_t *po = &q->payout[p];
        ok = ok && leg_add(q, po->account, po->net) && leg_add(q, po->giving_acct, po->giving) &&
             leg_add(q, po->invest_acct, po->investing);
    }
    ok = ok && leg_add(q, r->fee_acct, q->fee); /* last: no receiver is the fee account */
    if (!ok) return ROY_ERR_LINES;              /* only reachable if R9 was bypassed */
    digest_of(r, q);
    return ROY_OK;
}

roy_status_t roy_quote(const roy_t *r, const uint8_t work[ROY_ID_LEN], uint64_t gross,
                       roy_quote_t *q)
{
    if (!r || !r->ready || !work || !q) return ROY_ERR_ARG;
    int32_t i = roy_find(r, work);
    if (i < 0) return ROY_ERR_UNKNOWN;
    return build(r, (uint32_t) i, gross, q);
}

/* ---- R5 / R11 pay ---- */
bool roy_receipt_used(const roy_t *r, const uint8_t id[ROY_ID_LEN])
{
    if (!r || !id) return false;
    for (uint32_t i = 0; i < r->n_receipts; i++)
        if (pay_memeq(r->receipt[i], id, ROY_ID_LEN)) return true;
    return false;
}

/* R5: the payer's net DEBIT gain in the unreversed journal record whose
 * provenance hash is `id` is at least gross. */
static bool realised(const pay_ledger_t *L, const uint8_t id[ROY_ID_LEN], uint32_t payer,
                     uint64_t gross)
{
    for (uint32_t k = 0; k < PAY_JOURNAL_MAX; k++) {
        const pay_journal_t *j = &L->journal[k];
        if (!j->used || !pay_memeq(j->hash, id, PAY_HASH_LEN)) continue;
        if (j->reversed) return false;
        int64_t gain = 0;
        for (uint32_t i = 0; i < j->n_lines; i++) /* n_lines <= PAY_MAX_LINES */
            if (j->lines[i].account == payer) gain += j->lines[i].d_debit; /* |d| < 2^62 */
        return gain >= (int64_t) gross; /* gross < PAY_BAL_MAX: fits */
    }
    return false;
}

static bool tot_add(uint64_t *v, uint64_t d)
{
    return pay_add_ok(*v, d, v);
}

roy_status_t roy_pay(roy_t *r, pay_ledger_t *L, pay_posting_req_t *req, uint32_t payer,
                     const uint8_t work[ROY_ID_LEN], uint64_t gross,
                     const uint8_t receipt[ROY_ID_LEN], const uint8_t digest[ROY_DIGEST_LEN],
                     roy_quote_t *out)
{
    if (!r || !r->ready || !L || !req || !work || !receipt || !digest) return ROY_ERR_ARG;
    int32_t wi = roy_find(r, work);
    if (wi < 0) return ROY_ERR_UNKNOWN;
    roy_quote_t *q = &r->scratch;
    roy_status_t st = build(r, (uint32_t) wi, gross, q);
    if (st != ROY_OK) return st;
    if (!pay_memeq(q->digest, digest, ROY_DIGEST_LEN)) return ROY_ERR_STALE_QUOTE;
    if (roy_receipt_used(r, receipt)) return ROY_ERR_DUP;
    if (r->n_receipts + 2u > ROY_MAX_RECEIPTS) return ROY_ERR_FULL;
    if (!acct_ok(r, L, payer)) return ROY_ERR_ARG;
    for (uint32_t i = 0; i < q->n_legs; i++)
        if (q->leg[i].account == payer) return ROY_ERR_ARG;
    if (!realised(L, receipt, payer, gross)) return ROY_ERR_UNREALISED;

    /* stage every counter, overflow-checked, before anything posts */
    roy_totals_t t[ROY_MAX_LEVELS];
    bool ok = true;
    for (uint32_t l = 0; l < q->n_levels; l++) {
        const roy_level_t *lv = &q->level[l];
        t[l] = r->work[lv->work].tot;
        if (l == 0)
            ok = ok && tot_add(&t[0].payments, 1) && tot_add(&t[0].gross, gross) &&
                 tot_add(&t[0].royalty, q->royalty) && tot_add(&t[0].fee, q->fee);
        else
            ok = ok && tot_add(&t[l].up_in, lv->received);
        ok = ok && tot_add(&t[l].up_out, lv->upstream);
    }
    for (uint32_t p = 0; p < q->n_payouts; p++) {
        const roy_payout_t *po = &q->payout[p];
        roy_totals_t *tl = &t[po->level];
        ok = ok && tot_add(&tl->owners, po->net) && tot_add(&tl->giving, po->giving) &&
             tot_add(&tl->investing, po->investing);
    }
    if (!ok) return ROY_ERR_OVERFLOW;

    pay_receipt_t rc;
    if (q->royalty > 0) {
        req->lines[0].account = payer;
        req->lines[0].d_debit = -(int64_t) q->royalty;
        req->lines[0].d_credit = 0;
        for (uint32_t i = 0; i < q->n_legs; i++) req->lines[1 + i] = q->leg[i];
        req->n_lines = 1u + q->n_legs;
        req->kind = PAY_KIND_TRANSFER;
        r->last_ledger = pay_ledger_post(L, req, &rc);
        if (r->last_ledger != PAY_OK) return ROY_ERR_LEDGER;
    }
    for (uint32_t l = 0; l < q->n_levels; l++) r->work[q->level[l].work].tot = t[l];
    pay_memcpy(r->receipt[r->n_receipts++], receipt, ROY_ID_LEN);
    if (q->royalty > 0) pay_memcpy(r->receipt[r->n_receipts++], rc.hash, ROY_ID_LEN);
    if (out) *out = *q;
    return q->royalty > 0 ? ROY_OK : ROY_ZERO;
}

/* ---- R12 ---- */
uint64_t roy_grant_cap(uint64_t reserve_surplus, uint64_t requested)
{
    return requested < reserve_surplus ? requested : reserve_surplus;
}

/* ---- R13 ---- */
static bool work_ok(const roy_t *r, const pay_ledger_t *L, uint32_t i)
{
    const roy_work_t *w = &r->work[i];
    const roy_totals_t *t = &w->tot;
    uint64_t a, b;
    if (!w->used || !manifest_ok(r, L, &w->m)) return false;
    if (w->m.has_parent) {
        const roy_work_t *p = &r->work[w->parent];
        if (w->parent >= i || !pay_memeq(p->m.id, w->m.parent, ROY_ID_LEN) ||
            w->depth != p->depth + 1u || w->depth > ROY_MAX_DEPTH)
            return false;
    } else if (w->depth != 0 || w->parent != 0 || t->up_out != 0) {
        return false;
    }
    for (uint32_t j = w->m.n_owners; j < ROY_MAX_OWNERS; j++)
        if (w->m.owner[j].id || w->m.owner[j].account || w->m.owner[j].weight) return false;
    for (uint32_t j = 0; j < ROY_MAX_OWNERS; j++)
        if (!split_ok(r, L, &w->split[j]) || (j >= w->m.n_owners && w->split[j].seq)) return false;
    if (lines_of(r, i) > PAY_MAX_LINES) return false;
    if (t->royalty > t->gross || t->payments > t->gross) return false;
    if (!pay_add_ok(t->royalty, t->up_in, &a)) return false;
    if (!pay_add_ok(t->fee, t->up_out, &b) || !pay_add_ok(b, t->owners, &b) ||
        !pay_add_ok(b, t->giving, &b) || !pay_add_ok(b, t->investing, &b))
        return false;
    return a == b;
}

bool roy_check(const roy_t *r, const pay_ledger_t *L)
{
    uint64_t in = 0, outv = 0;
    if (!r || !r->ready || !L || !pay_ledger_check(L)) return false;
    if (r->n_works > ROY_MAX_WORKS || r->n_receipts > ROY_MAX_RECEIPTS) return false;
    if (r->fee_acct >= L->n_accounts || !L->acct[r->fee_acct].active) return false;
    for (uint32_t i = 0; i < ROY_MAX_WORKS; i++) {
        if (i >= r->n_works) {
            if (r->work[i].used) return false;
            continue;
        }
        if (!work_ok(r, L, i)) return false;
        if (!pay_add_ok(in, r->work[i].tot.up_in, &in) ||
            !pay_add_ok(outv, r->work[i].tot.up_out, &outv))
            return false;
    }
    return in == outv;
}
