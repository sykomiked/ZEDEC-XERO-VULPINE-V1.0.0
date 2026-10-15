/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pp_state.c -- the personal receipt sets, their replay, the capsule and the
 * device vouchers. See passport.h. */
#include "pp_int.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"

#define STATE_MAGIC "ZPS1"
#define STATE_HDR   60u /* magic, version, 3 zero, root DID, five counts */
#define CAP_MAGIC   "ZPC1"
#define VERSION     1u
#define CODEC_RAW   0x55u

/* ---- entry encodings: also the canonical order ---- */
void pp_earn_enc(const pp_earn_t *e, uint8_t out[PP_EARN_BYTES])
{
    pp_mcpy(out, e->receipt, PP_HASH);
    pp_mcpy(out + 32, e->issuer, PP_HASH);
    pp_put64(out + 64, e->amount);
    pp_put64(out + 72, e->t_ms);
}

static void earn_dec(const uint8_t *in, pp_earn_t *e)
{
    pp_mcpy(e->receipt, in, PP_HASH);
    pp_mcpy(e->issuer, in + 32, PP_HASH);
    e->amount = pp_get64(in + 64);
    e->t_ms = pp_get64(in + 72);
}

pp_status_t pp_spend_encode(const pp_spend_t *sp, uint8_t out[PP_SPEND_BYTES])
{
    if (!sp || !out) return PP_ERR_ARG;
    pp_mcpy(out, sp->signer, PP_ID);
    pp_put64(out + 16, sp->seq);
    pp_put64(out + 24, sp->lamport);
    pp_put64(out + 32, sp->amount);
    pp_put64(out + 40, sp->t_ms);
    pp_mcpy(out + 48, sp->payee, PP_HASH);
    pp_mcpy(out + 80, sp->grant, PP_ID);
    return PP_OK;
}

void pp_spend_decode(const uint8_t *in, pp_spend_t *sp)
{
    pp_mcpy(sp->signer, in, PP_ID);
    sp->seq = pp_get64(in + 16);
    sp->lamport = pp_get64(in + 24);
    sp->amount = pp_get64(in + 32);
    sp->t_ms = pp_get64(in + 40);
    pp_mcpy(sp->payee, in + 48, PP_HASH);
    pp_mcpy(sp->grant, in + 80, PP_ID);
}

void pp_grant_rec_enc(const pp_grant_rec_t *g, uint8_t out[PP_GRANT_BYTES])
{
    pp_mcpy(out, g->grant, PP_ID);
    pp_mcpy(out + 16, g->session, PP_ID);
    pp_mcpy(out + 32, g->device, PP_ID);
    pp_put32(out + 48, g->rights);
    pp_put64(out + 52, g->cap);
    pp_put64(out + 60, g->issued_ms);
    pp_put64(out + 68, g->expires_ms);
}

static void grant_dec(const uint8_t *in, pp_grant_rec_t *g)
{
    pp_mcpy(g->grant, in, PP_ID);
    pp_mcpy(g->session, in + 16, PP_ID);
    pp_mcpy(g->device, in + 32, PP_ID);
    g->rights = pp_get32(in + 48);
    g->cap = pp_get64(in + 52);
    g->issued_ms = pp_get64(in + 60);
    g->expires_ms = pp_get64(in + 68);
}

static void revoke_enc(const pp_revoke_t *r, uint8_t out[PP_REVOKE_BYTES])
{
    pp_mcpy(out, r->key, PP_ID);
    pp_put64(out + 16, r->last_seq);
}

static void dev_enc(const id_cred_ref_t *d, uint8_t out[PP_DEV_BYTES])
{
    pp_mcpy(out, d->cred_id, PP_ID);
    pp_mcpy(out + 16, d->dev_id, PP_ID);
}

/* ---- validity of one entry (the same for local adds and merges) ---- */
static bool earn_ok(const pp_earn_t *e)
{
    return e->amount != 0;
}

static bool spend_ok(const pp_spend_t *sp)
{
    return sp->amount != 0 && sp->seq != 0 && sp->lamport != 0 && !pp_is_zero(sp->signer, PP_ID);
}

static bool grant_ok(const pp_grant_rec_t *g)
{
    return g->rights != 0 && (g->rights & ~PP_RIGHTS_ALL) == 0 && g->expires_ms > g->issued_ms &&
           g->expires_ms - g->issued_ms <= PP_CAFE_MAX_MS && !pp_is_zero(g->grant, PP_ID) &&
           !pp_is_zero(g->session, PP_ID);
}

static bool key_ok(const uint8_t *k)
{
    return !pp_is_zero(k, PP_ID);
}

/* ---- sorted sets ---- */
typedef void (*enc_fn)(const pp_state_t *st, uint32_t i, uint8_t *out);

static void enc_earn_i(const pp_state_t *st, uint32_t i, uint8_t *out)
{
    pp_earn_enc(&st->s.earn[i], out);
}
static void enc_spend_i(const pp_state_t *st, uint32_t i, uint8_t *out)
{
    pp_spend_encode(&st->s.spend[i], out);
}
static void enc_grant_i(const pp_state_t *st, uint32_t i, uint8_t *out)
{
    pp_grant_rec_enc(&st->s.grant[i], out);
}
static void enc_revoke_i(const pp_state_t *st, uint32_t i, uint8_t *out)
{
    revoke_enc(&st->s.revoke[i], out);
}
static void enc_dev_i(const pp_state_t *st, uint32_t i, uint8_t *out)
{
    dev_enc(&st->s.dev[i], out);
}

/* First index whose first klen encoded bytes are >= key. */
static uint32_t lower_bound(const pp_state_t *st, uint32_t n, enc_fn enc, const uint8_t *key,
                            uint32_t klen, bool *found)
{
    uint8_t e[PP_SPEND_BYTES];
    uint32_t lo = 0, hi = n;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1);
        enc(st, mid, e);
        if (pp_mcmp(e, key, klen) < 0)
            lo = mid + 1;
        else
            hi = mid;
    }
    *found = false;
    if (lo < n) {
        enc(st, lo, e);
        *found = pp_mcmp(e, key, klen) == 0;
    }
    return lo;
}

static pp_status_t ins_earn(pp_state_t *st, const pp_earn_t *e, bool dry, uint32_t *added)
{
    uint8_t k[PP_EARN_BYTES];
    bool found;
    pp_earn_enc(e, k);
    uint32_t pos = lower_bound(st, st->n_earn, enc_earn_i, k, sizeof k, &found);
    if (found) return PP_OK;
    if (added) (*added)++;
    if (dry) return PP_OK;
    if (st->n_earn >= st->s.earn_cap) return PP_ERR_FULL;
    for (uint32_t i = st->n_earn; i > pos; i--) st->s.earn[i] = st->s.earn[i - 1];
    st->s.earn[pos] = *e;
    st->n_earn++;
    return PP_OK;
}

static pp_status_t ins_spend(pp_state_t *st, const pp_spend_t *sp, bool dry, uint32_t *added)
{
    uint8_t k[PP_SPEND_BYTES];
    bool found;
    pp_spend_encode(sp, k);
    uint32_t pos = lower_bound(st, st->n_spend, enc_spend_i, k, sizeof k, &found);
    if (found) return PP_OK;
    if (added) (*added)++;
    if (dry) return PP_OK;
    if (st->n_spend >= st->s.spend_cap) return PP_ERR_FULL;
    for (uint32_t i = st->n_spend; i > pos; i--) st->s.spend[i] = st->s.spend[i - 1];
    st->s.spend[pos] = *sp;
    st->n_spend++;
    return PP_OK;
}

static pp_status_t ins_grant(pp_state_t *st, const pp_grant_rec_t *g, bool dry, uint32_t *added)
{
    uint8_t k[PP_GRANT_BYTES];
    bool found;
    pp_grant_rec_enc(g, k);
    uint32_t pos = lower_bound(st, st->n_grant, enc_grant_i, k, sizeof k, &found);
    if (found) return PP_OK;
    if (added) (*added)++;
    if (dry) return PP_OK;
    if (st->n_grant >= st->s.grant_cap) return PP_ERR_FULL;
    for (uint32_t i = st->n_grant; i > pos; i--) st->s.grant[i] = st->s.grant[i - 1];
    st->s.grant[pos] = *g;
    st->n_grant++;
    return PP_OK;
}

/* One revocation per key; a merge keeps the lower last sequence. */
static pp_status_t ins_revoke(pp_state_t *st, const pp_revoke_t *r, bool dry, uint32_t *added)
{
    bool found;
    uint32_t pos = lower_bound(st, st->n_revoke, enc_revoke_i, r->key, PP_ID, &found);
    if (found) {
        if (r->last_seq >= st->s.revoke[pos].last_seq) return PP_OK;
        if (added) (*added)++;
        if (!dry) st->s.revoke[pos].last_seq = r->last_seq;
        return PP_OK;
    }
    if (added) (*added)++;
    if (dry) return PP_OK;
    if (st->n_revoke >= st->s.revoke_cap) return PP_ERR_FULL;
    for (uint32_t i = st->n_revoke; i > pos; i--) st->s.revoke[i] = st->s.revoke[i - 1];
    st->s.revoke[pos] = *r;
    st->n_revoke++;
    return PP_OK;
}

static pp_status_t ins_dev(pp_state_t *st, const id_cred_ref_t *d, bool dry, uint32_t *added)
{
    uint8_t k[PP_DEV_BYTES];
    bool found;
    dev_enc(d, k);
    uint32_t pos = lower_bound(st, st->n_dev, enc_dev_i, k, sizeof k, &found);
    if (found) return PP_OK;
    if (added) (*added)++;
    if (dry) return PP_OK;
    if (st->n_dev >= st->s.dev_cap) return PP_ERR_FULL;
    for (uint32_t i = st->n_dev; i > pos; i--) st->s.dev[i] = st->s.dev[i - 1];
    st->s.dev[pos] = *d;
    st->n_dev++;
    return PP_OK;
}

bool pp_state_has_spend(const pp_state_t *st, const pp_spend_t *sp)
{
    uint8_t k[PP_SPEND_BYTES];
    bool found;
    pp_spend_encode(sp, k);
    lower_bound(st, st->n_spend, enc_spend_i, k, sizeof k, &found);
    return found;
}

bool pp_state_has_grant(const pp_state_t *st, const pp_grant_rec_t *g)
{
    uint8_t k[PP_GRANT_BYTES];
    bool found;
    pp_grant_rec_enc(g, k);
    lower_bound(st, st->n_grant, enc_grant_i, k, sizeof k, &found);
    return found;
}

bool pp_state_has_device(const pp_state_t *st, const uint8_t cred[PP_ID])
{
    for (uint32_t i = 0; i < st->n_dev; i++)
        if (pp_mcmp(st->s.dev[i].cred_id, cred, PP_ID) == 0) return true;
    return false;
}

bool pp_state_is_revoked(const pp_state_t *st, const uint8_t key[PP_ID])
{
    bool found;
    lower_bound(st, st->n_revoke, enc_revoke_i, key, PP_ID, &found);
    return found;
}

/* ---- public adds ---- */
pp_status_t pp_state_init(pp_state_t *st, const uint8_t root_did[PP_HASH], const pp_store_t *s)
{
    if (!st || !root_did || !s) return PP_ERR_ARG;
    if (!s->earn || !s->earn_st || !s->spend || !s->spend_st || !s->spend_order || !s->grant ||
        !s->grant_used || !s->revoke || !s->dev)
        return PP_ERR_ARG;
    pp_mset(st, 0, sizeof *st);
    pp_mcpy(st->root_did, root_did, PP_HASH);
    st->s = *s;
    return PP_OK;
}

static void one(uint32_t *added)
{
    if (added) *added = 0;
}

pp_status_t pp_state_add_earn(pp_state_t *st, const pp_earn_t *e, uint32_t *added)
{
    one(added);
    if (!st || !e || !earn_ok(e)) return PP_ERR_ARG;
    return ins_earn(st, e, false, added);
}

pp_status_t pp_state_add_spend(pp_state_t *st, const pp_spend_t *sp, uint32_t *added)
{
    one(added);
    if (!st || !sp || !spend_ok(sp)) return PP_ERR_ARG;
    return ins_spend(st, sp, false, added);
}

pp_status_t pp_state_add_grant(pp_state_t *st, const pp_grant_rec_t *g, uint32_t *added)
{
    one(added);
    if (!st || !g || !grant_ok(g)) return PP_ERR_ARG;
    return ins_grant(st, g, false, added);
}

pp_status_t pp_state_add_revoke(pp_state_t *st, const pp_revoke_t *r, uint32_t *added)
{
    one(added);
    if (!st || !r || !key_ok(r->key)) return PP_ERR_ARG;
    return ins_revoke(st, r, false, added);
}

pp_status_t pp_state_add_device(pp_state_t *st, const id_cred_ref_t *d, uint32_t *added)
{
    one(added);
    if (!st || !d || !key_ok(d->cred_id)) return PP_ERR_ARG;
    return ins_dev(st, d, false, added);
}

/* ---- replay ---- */
static int32_t find_revoke(const pp_state_t *st, const uint8_t key[PP_ID])
{
    bool found;
    uint32_t pos = lower_bound(st, st->n_revoke, enc_revoke_i, key, PP_ID, &found);
    return found ? (int32_t) pos : -1;
}

/* True if a revocation of `key` refuses sequence `seq`. */
static bool revoked_at(const pp_state_t *st, const uint8_t key[PP_ID], uint64_t seq)
{
    int32_t r = find_revoke(st, key);
    return r >= 0 && seq > st->s.revoke[r].last_seq;
}

/* The grant record for id: its index, -1 if none, -2 if two records with
 * different contents claim the id. */
static int32_t find_grant(const pp_state_t *st, const uint8_t id[PP_ID])
{
    bool found;
    uint32_t pos = lower_bound(st, st->n_grant, enc_grant_i, id, PP_ID, &found);
    if (!found) return -1;
    if (pos + 1u < st->n_grant && pp_mcmp(st->s.grant[pos + 1u].grant, id, PP_ID) == 0) return -2;
    return (int32_t) pos;
}

static bool add_ck(uint64_t *acc, uint64_t v)
{
    if (*acc > UINT64_MAX - v) return false;
    *acc += v;
    return true;
}

/* Funding order: Lamport time, then signer, then sequence. */
static bool before(const pp_spend_t *a, const pp_spend_t *b)
{
    if (a->lamport != b->lamport) return a->lamport < b->lamport;
    int c = pp_mcmp(a->signer, b->signer, PP_ID);
    if (c != 0) return c < 0;
    return a->seq < b->seq;
}

/* Authority of a spend that is alone on its slot. Returns PP_ST_SETTLED for
 * a candidate (funding decides), else REFUSED or UNKNOWN. *gi: its grant. */
static uint8_t authority(const pp_state_t *st, const pp_spend_t *sp, int32_t *gi)
{
    *gi = -1;
    if (pp_is_zero(sp->grant, PP_ID)) {
        if (!pp_state_has_device(st, sp->signer)) return PP_ST_UNKNOWN;
        return revoked_at(st, sp->signer, sp->seq) ? PP_ST_REFUSED : PP_ST_SETTLED;
    }
    int32_t g = find_grant(st, sp->grant);
    if (g == -1) return PP_ST_UNKNOWN;
    if (g < 0) return PP_ST_REFUSED;
    const pp_grant_rec_t *gr = &st->s.grant[g];
    if (pp_mcmp(gr->session, sp->signer, PP_ID) != 0) return PP_ST_REFUSED;
    if (!(gr->rights & PP_RIGHT_SPEND)) return PP_ST_REFUSED;
    if (sp->t_ms < gr->issued_ms || sp->t_ms > gr->expires_ms) return PP_ST_REFUSED;
    if (revoked_at(st, gr->grant, sp->seq) || revoked_at(st, gr->session, sp->seq))
        return PP_ST_REFUSED;
    *gi = g;
    return PP_ST_SETTLED;
}

pp_status_t pp_state_replay(pp_state_t *st)
{
    if (!st) return PP_ERR_ARG;
    pp_balance_t b;
    pp_mset(&b, 0, sizeof b);

    /* earnings: one receipt hash, one content */
    for (uint32_t i = 0; i < st->n_earn;) {
        uint32_t j = i + 1u;
        while (j < st->n_earn &&
               pp_mcmp(st->s.earn[j].receipt, st->s.earn[i].receipt, PP_HASH) == 0)
            j++;
        bool contested = j - i > 1u;
        for (uint32_t k = i; k < j; k++) {
            st->s.earn_st[k] = contested ? PP_ST_CONTESTED : PP_ST_COUNTED;
            if (!contested && !add_ck(&b.earned, st->s.earn[k].amount)) return PP_ERR_OVERFLOW;
        }
        i = j;
    }

    /* spends: equivocations, then authority */
    uint32_t nc = 0;
    for (uint32_t i = 0; i < st->n_spend;) {
        const pp_spend_t *si = &st->s.spend[i];
        uint32_t j = i + 1u;
        while (j < st->n_spend && pp_mcmp(st->s.spend[j].signer, si->signer, PP_ID) == 0 &&
               st->s.spend[j].seq == si->seq)
            j++;
        if (j - i > 1u) {
            uint64_t mx = 0;
            for (uint32_t k = i; k < j; k++) {
                st->s.spend_st[k] = PP_ST_HELD;
                if (st->s.spend[k].amount > mx) mx = st->s.spend[k].amount;
            }
            if (!add_ck(&b.held, mx)) return PP_ERR_OVERFLOW;
        } else {
            int32_t gi;
            uint8_t a = authority(st, si, &gi);
            st->s.spend_st[i] = a;
            if (a == PP_ST_UNKNOWN && !add_ck(&b.unknown, si->amount)) return PP_ERR_OVERFLOW;
            if (a == PP_ST_REFUSED && !add_ck(&b.refused, si->amount)) return PP_ERR_OVERFLOW;
            if (a == PP_ST_SETTLED) {
                /* insertion into the funding order */
                uint32_t p = nc;
                while (p > 0 && before(si, &st->s.spend[st->s.spend_order[p - 1u]])) {
                    st->s.spend_order[p] = st->s.spend_order[p - 1u];
                    p--;
                }
                st->s.spend_order[p] = i;
                nc++;
            }
        }
        i = j;
    }

    /* what is kept back comes off first */
    uint64_t left = b.earned, keep = 0;
    if (!add_ck(&keep, b.held) || !add_ck(&keep, b.unknown)) return PP_ERR_OVERFLOW;
    if (keep > left) {
        b.deficit = keep - left;
        left = 0;
    } else {
        left -= keep;
    }

    for (uint32_t g = 0; g < st->n_grant; g++) st->s.grant_used[g] = 0;
    for (uint32_t k = 0; k < nc; k++) {
        uint32_t i = st->s.spend_order[k];
        const pp_spend_t *sp = &st->s.spend[i];
        int32_t gi;
        authority(st, sp, &gi);
        uint64_t amt = sp->amount;
        /* grant_used never exceeds the cap, so the subtraction is safe */
        if (gi >= 0 && amt > st->s.grant[gi].cap - st->s.grant_used[gi]) {
            st->s.spend_st[i] = PP_ST_REFUSED;
            if (!add_ck(&b.refused, amt)) return PP_ERR_OVERFLOW;
        } else if (amt > left) {
            st->s.spend_st[i] = PP_ST_QUARANTINED;
            if (!add_ck(&b.quarantined, amt)) return PP_ERR_OVERFLOW;
        } else {
            st->s.spend_st[i] = PP_ST_SETTLED;
            left -= amt;
            b.settled += amt; /* bounded by earned */
            if (gi >= 0) st->s.grant_used[gi] += amt;
        }
    }
    b.available = left;
    st->bal = b;
    return PP_OK;
}

pp_status_t pp_state_spend(pp_state_t *st, const uint8_t signer[PP_ID], uint64_t amount,
                           const uint8_t payee[PP_HASH], uint64_t t_ms, pp_spend_t *out)
{
    if (!st || !signer || !payee || !out || amount == 0) return PP_ERR_ARG;
    if (!pp_state_has_device(st, signer)) return PP_ERR_NOTFOUND;
    if (pp_state_is_revoked(st, signer)) return PP_ERR_REVOKED;
    pp_status_t s = pp_state_replay(st);
    if (s != PP_OK) return s;
    if (amount > st->bal.available) return PP_ERR_FUNDS;
    if (st->n_spend >= st->s.spend_cap) return PP_ERR_FULL;
    uint64_t seq = 0, lam = 0;
    for (uint32_t i = 0; i < st->n_spend; i++) {
        const pp_spend_t *x = &st->s.spend[i];
        if (x->lamport > lam) lam = x->lamport;
        if (pp_mcmp(x->signer, signer, PP_ID) == 0 && x->seq > seq) seq = x->seq;
    }
    if (seq == UINT64_MAX || lam == UINT64_MAX) return PP_ERR_OVERFLOW;
    pp_spend_t sp;
    pp_mset(&sp, 0, sizeof sp);
    pp_mcpy(sp.signer, signer, PP_ID);
    sp.seq = seq + 1u;
    sp.lamport = lam + 1u;
    sp.amount = amount;
    sp.t_ms = t_ms;
    pp_mcpy(sp.payee, payee, PP_HASH);
    s = ins_spend(st, &sp, false, 0);
    if (s != PP_OK) return s;
    *out = sp;
    return pp_state_replay(st);
}

/* ---- canonical encoding ---- */
uint32_t pp_state_encoded_size(const pp_state_t *st)
{
    if (!st) return 0;
    uint64_t n = STATE_HDR + (uint64_t) st->n_earn * PP_EARN_BYTES +
                 (uint64_t) st->n_spend * PP_SPEND_BYTES + (uint64_t) st->n_grant * PP_GRANT_BYTES +
                 (uint64_t) st->n_revoke * PP_REVOKE_BYTES + (uint64_t) st->n_dev * PP_DEV_BYTES;
    return n > UINT32_MAX - PP_CAPSULE_HDR - PP_TAG ? 0 : (uint32_t) n;
}

pp_status_t pp_state_encode(const pp_state_t *st, uint8_t *out, uint32_t cap, uint32_t *len)
{
    if (!st || !out || !len) return PP_ERR_ARG;
    uint32_t need = pp_state_encoded_size(st);
    if (need == 0) return PP_ERR_OVERFLOW;
    if (cap < need) return PP_ERR_SIZE;
    pp_mcpy(out, STATE_MAGIC, 4);
    out[4] = VERSION;
    out[5] = out[6] = out[7] = 0;
    pp_mcpy(out + 8, st->root_did, PP_HASH);
    pp_put32(out + 40, st->n_earn);
    pp_put32(out + 44, st->n_spend);
    pp_put32(out + 48, st->n_grant);
    pp_put32(out + 52, st->n_revoke);
    pp_put32(out + 56, st->n_dev);
    uint32_t o = STATE_HDR;
    for (uint32_t i = 0; i < st->n_earn; i++, o += PP_EARN_BYTES) enc_earn_i(st, i, out + o);
    for (uint32_t i = 0; i < st->n_spend; i++, o += PP_SPEND_BYTES) enc_spend_i(st, i, out + o);
    for (uint32_t i = 0; i < st->n_grant; i++, o += PP_GRANT_BYTES) enc_grant_i(st, i, out + o);
    for (uint32_t i = 0; i < st->n_revoke; i++, o += PP_REVOKE_BYTES) enc_revoke_i(st, i, out + o);
    for (uint32_t i = 0; i < st->n_dev; i++, o += PP_DEV_BYTES) enc_dev_i(st, i, out + o);
    *len = o;
    return PP_OK;
}

/* Walk an encoding: check (dry) or insert. Strictly ascending sets only. */
static pp_status_t walk(pp_state_t *st, const uint8_t *in, const uint32_t cnt[5], bool dry,
                        uint32_t add[5])
{
    static const uint32_t SZ[5] = {PP_EARN_BYTES, PP_SPEND_BYTES, PP_GRANT_BYTES, PP_REVOKE_BYTES,
                                   PP_DEV_BYTES};
    /* the order compares the whole entry, but revocations only their key */
    static const uint32_t KEY[5] = {PP_EARN_BYTES, PP_SPEND_BYTES, PP_GRANT_BYTES, PP_ID,
                                    PP_DEV_BYTES};
    uint32_t o = STATE_HDR;
    for (uint32_t set = 0; set < 5u; set++) {
        for (uint32_t i = 0; i < cnt[set]; i++, o += SZ[set]) {
            const uint8_t *e = in + o;
            if (dry && i > 0 && pp_mcmp(e - SZ[set], e, KEY[set]) >= 0) return PP_ERR_FORMAT;
            pp_status_t s = PP_OK;
            if (set == 0) {
                pp_earn_t x;
                earn_dec(e, &x);
                if (dry && !earn_ok(&x)) return PP_ERR_FORMAT;
                s = ins_earn(st, &x, dry, &add[0]);
            } else if (set == 1) {
                pp_spend_t x;
                pp_spend_decode(e, &x);
                if (dry && !spend_ok(&x)) return PP_ERR_FORMAT;
                s = ins_spend(st, &x, dry, &add[1]);
            } else if (set == 2) {
                pp_grant_rec_t x;
                grant_dec(e, &x);
                if (dry && !grant_ok(&x)) return PP_ERR_FORMAT;
                s = ins_grant(st, &x, dry, &add[2]);
            } else if (set == 3) {
                pp_revoke_t x;
                pp_mcpy(x.key, e, PP_ID);
                x.last_seq = pp_get64(e + 16);
                if (dry && !key_ok(x.key)) return PP_ERR_FORMAT;
                s = ins_revoke(st, &x, dry, &add[3]);
            } else {
                id_cred_ref_t x;
                pp_mcpy(x.cred_id, e, PP_ID);
                pp_mcpy(x.dev_id, e + 16, PP_ID);
                if (dry && !key_ok(x.cred_id)) return PP_ERR_FORMAT;
                s = ins_dev(st, &x, dry, &add[4]);
            }
            if (s != PP_OK) return s;
        }
    }
    return PP_OK;
}

pp_status_t pp_state_merge_encoded(pp_state_t *st, const uint8_t *in, uint32_t len, uint32_t *added)
{
    one(added);
    if (!st || !in) return PP_ERR_ARG;
    if (len < STATE_HDR || pp_mcmp(in, STATE_MAGIC, 4) != 0 || in[4] != VERSION || in[5] || in[6] ||
        in[7])
        return PP_ERR_FORMAT;
    if (!pp_ct_eq(in + 8, st->root_did, PP_HASH)) return PP_ERR_MISMATCH;
    uint32_t cnt[5];
    uint64_t need = STATE_HDR;
    static const uint32_t SZ[5] = {PP_EARN_BYTES, PP_SPEND_BYTES, PP_GRANT_BYTES, PP_REVOKE_BYTES,
                                   PP_DEV_BYTES};
    for (uint32_t i = 0; i < 5u; i++) {
        cnt[i] = pp_get32(in + 40 + 4u * i);
        need += (uint64_t) cnt[i] * SZ[i];
    }
    if (need != len) return PP_ERR_FORMAT;
    uint32_t add[5] = {0, 0, 0, 0, 0};
    pp_status_t s = walk(st, in, cnt, true, add);
    if (s != PP_OK) return s;
    if ((uint64_t) st->n_earn + add[0] > st->s.earn_cap ||
        (uint64_t) st->n_spend + add[1] > st->s.spend_cap ||
        (uint64_t) st->n_grant + add[2] > st->s.grant_cap ||
        (uint64_t) st->n_revoke + add[3] > st->s.revoke_cap ||
        (uint64_t) st->n_dev + add[4] > st->s.dev_cap)
        return PP_ERR_FULL;
    uint32_t done[5] = {0, 0, 0, 0, 0};
    s = walk(st, in, cnt, false, done);
    if (s != PP_OK) return s;
    if (added) *added = done[0] + done[1] + done[2] + done[3] + done[4];
    return PP_OK;
}

pp_status_t pp_state_digest(const pp_state_t *st, uint8_t *work, uint32_t work_cap,
                            uint8_t out[PP_HASH])
{
    uint32_t n;
    if (!st || !work || !out) return PP_ERR_ARG;
    pp_status_t s = pp_state_encode(st, work, work_cap, &n);
    if (s != PP_OK) return s;
    sha3_256(work, n, out);
    return PP_OK;
}

pp_status_t pp_status_rebuild(const pp_passport_t *p, const id_host_t *h, const pp_state_t *st,
                              uint64_t epoch_ms, id_cred_status_t *out)
{
    if (!p || !h || !st || !out) return PP_ERR_ARG;
    if (!pp_ct_eq(p->root.did, st->root_did, PP_HASH)) return PP_ERR_MISMATCH;
    id_status_init(out, p->root.did, epoch_ms);
    for (uint32_t i = 0; i < st->n_dev; i++) {
        if (pp_state_is_revoked(st, st->s.dev[i].cred_id)) continue;
        id_status_t r = id_status_add(out, st->s.dev[i].cred_id, st->s.dev[i].dev_id);
        if (r == ID_ERR_FULL) return PP_ERR_FULL;
        if (r != ID_OK) return PP_ERR_FORMAT; /* one credential id on two devices */
    }
    return id_status_sign(&p->root, h, out) == ID_OK ? PP_OK : PP_ERR_CRYPTO;
}

/* ---- capsule ---- */
uint32_t pp_capsule_size(const pp_state_t *st)
{
    uint32_t n = pp_state_encoded_size(st);
    return n ? n + PP_CAPSULE_HDR + PP_TAG : 0;
}

static void capsule_key(const pp_passport_t *p, const uint8_t salt[PP_SALT], uint8_t k[PP_KEY])
{
    pp_shake(k, PP_KEY, "zxv.passport.capsule.v1", p->vault_key, PP_KEY, salt, PP_SALT, 0, 0);
}

pp_status_t pp_capsule_seal(const pp_passport_t *p, const pp_state_t *st,
                            const uint8_t salt[PP_SALT], uint8_t *out, uint32_t cap, uint32_t *len)
{
    uint8_t k[PP_KEY], nonce[CHACHA20_NONCE_LEN];
    uint32_t n;
    if (!p || !st || !salt || !out || !len) return PP_ERR_ARG;
    if (!pp_ct_eq(p->root.did, st->root_did, PP_HASH)) return PP_ERR_MISMATCH;
    uint32_t need = pp_capsule_size(st);
    if (need == 0) return PP_ERR_OVERFLOW;
    if (cap < need) return PP_ERR_SIZE;
    pp_status_t s = pp_state_encode(st, out + PP_CAPSULE_HDR, cap - PP_CAPSULE_HDR, &n);
    if (s != PP_OK) return s;
    pp_mcpy(out, CAP_MAGIC, 4);
    out[4] = VERSION;
    out[5] = out[6] = out[7] = 0;
    pp_mcpy(out + 8, salt, PP_SALT);
    pp_put32(out + 40, n);
    capsule_key(p, salt, k);
    pp_mset(nonce, 0, sizeof nonce);
    aead_seal(k, nonce, out, PP_CAPSULE_HDR, out + PP_CAPSULE_HDR, out + PP_CAPSULE_HDR, n,
              out + PP_CAPSULE_HDR + n);
    pp_wipe_mem(k, sizeof k);
    *len = PP_CAPSULE_HDR + n + PP_TAG;
    return PP_OK;
}

pp_status_t pp_capsule_open(const pp_passport_t *p, const uint8_t *in, uint32_t len, uint8_t *work,
                            uint32_t work_cap, pp_state_t *st, uint32_t *added)
{
    uint8_t k[PP_KEY], nonce[CHACHA20_NONCE_LEN];
    one(added);
    if (!p || !in || !work || !st) return PP_ERR_ARG;
    if (len < PP_CAPSULE_HDR + PP_TAG || pp_mcmp(in, CAP_MAGIC, 4) != 0 || in[4] != VERSION ||
        in[5] || in[6] || in[7])
        return PP_ERR_FORMAT;
    uint32_t n = pp_get32(in + 40);
    if (n != len - PP_CAPSULE_HDR - PP_TAG) return PP_ERR_FORMAT;
    if (work_cap < n) return PP_ERR_SIZE;
    capsule_key(p, in + 8, k);
    pp_mset(nonce, 0, sizeof nonce);
    bool ok = aead_open(k, nonce, in, PP_CAPSULE_HDR, in + PP_CAPSULE_HDR, work, n,
                        in + PP_CAPSULE_HDR + n);
    pp_wipe_mem(k, sizeof k);
    if (!ok) return PP_ERR_AUTH;
    pp_status_t s = pp_state_merge_encoded(st, work, n, added);
    pp_wipe_mem(work, n);
    return s;
}

pp_status_t pp_capsule_cid(const uint8_t *in, uint32_t len, ipfsn_cid_t *cid)
{
    if (!in || !cid) return PP_ERR_ARG;
    return ipfsn_cid_sha256(CODEC_RAW, in, len, cid) == 0 ? PP_OK : PP_ERR_FORMAT;
}

pp_status_t pp_capsule_publish(const pp_passport_t *p, const id_host_t *h, const pp_state_t *st,
                               uint64_t epoch, uint8_t *buf, uint32_t cap)
{
    uint8_t salt[PP_SALT], topic[PP_HASH];
    uint32_t n;
    ipfsn_cid_t cid;
    if (!p || !h || !h->random || !h->publish || !st || !buf) return PP_ERR_ARG;
    h->random(h->ctx, salt, PP_SALT);
    pp_status_t s = pp_capsule_seal(p, st, salt, buf, cap, &n);
    if (s != PP_OK) return s;
    if ((s = pp_capsule_cid(buf, n, &cid)) != PP_OK) return s;
    pp_topic(p, epoch, topic);
    return h->publish(h->ctx, topic, &cid, buf, n) == 0 ? PP_OK : PP_ERR_NOTFOUND;
}

pp_status_t pp_capsule_fetch(const pp_passport_t *p, const id_host_t *h, uint64_t epoch,
                             uint8_t *buf, uint32_t cap, uint8_t *work, uint32_t work_cap,
                             pp_state_t *st, uint32_t *merged, uint32_t *skipped)
{
    uint8_t topic[PP_HASH];
    uint32_t m = 0, sk = 0;
    if (merged) *merged = 0;
    if (skipped) *skipped = 0;
    if (!p || !h || !h->fetch || !buf || !work || !st) return PP_ERR_ARG;
    pp_topic(p, epoch, topic);
    for (uint32_t i = 0; i < PP_FETCH_MAX; i++) {
        uint32_t n = 0, add;
        if (h->fetch(h->ctx, topic, i, buf, cap, &n) != 0) break;
        if (n > cap) return PP_ERR_SIZE;
        pp_status_t s = pp_capsule_open(p, buf, n, work, work_cap, st, &add);
        if (s == PP_ERR_FULL || s == PP_ERR_SIZE) return s;
        if (s == PP_OK)
            m++;
        else
            sk++; /* someone else's blob, or a damaged one */
    }
    if (merged) *merged = m;
    if (skipped) *skipped = sk;
    return pp_state_replay(st);
}

/* ---- vouchers ---- */
static void voucher_msg(const uint8_t root_did[PP_HASH], const pp_spend_t *sp,
                        uint8_t m[PP_HASH + PP_SPEND_BYTES])
{
    pp_mcpy(m, root_did, PP_HASH);
    pp_spend_encode(sp, m + PP_HASH);
}

pp_status_t pp_voucher_sign(const uint8_t sk[ID_MLDSA65_SK], const uint8_t root_did[PP_HASH],
                            const pp_spend_t *sp, const uint8_t *rnd, uint8_t sig[ID_MLDSA65_SIG])
{
    uint8_t m[PP_HASH + PP_SPEND_BYTES];
    if (!sk || !root_did || !sp || !sig) return PP_ERR_ARG;
    voucher_msg(root_did, sp, m);
    pq_mldsa65_sign(sk, m, sizeof m, (const uint8_t *) PP_CTX_SPEND, sizeof PP_CTX_SPEND - 1u, rnd,
                    sig);
    return PP_OK;
}

bool pp_voucher_verify(const uint8_t pk[ID_MLDSA65_PK], const uint8_t root_did[PP_HASH],
                       const pp_spend_t *sp, const uint8_t sig[ID_MLDSA65_SIG])
{
    uint8_t m[PP_HASH + PP_SPEND_BYTES];
    if (!pk || !root_did || !sp || !sig) return false;
    voucher_msg(root_did, sp, m);
    return pq_mldsa65_verify(pk, m, sizeof m, (const uint8_t *) PP_CTX_SPEND,
                             sizeof PP_CTX_SPEND - 1u, sig);
}

/* Shared by both verifiers: root -> status list -> certificate -> key. */
pp_status_t pp_device_chain(const pqm_sig_pk_t *root_pk, const id_cred_status_t *status,
                            const id_dev_cert_t *cert, const uint8_t device_pk[ID_MLDSA65_PK],
                            const uint8_t cred[PP_ID])
{
    if (!root_pk || !status || !cert || !device_pk || !cred) return PP_ERR_ARG;
    if (pp_mcmp(cert->cred_id, cred, PP_ID) != 0) return PP_ERR_MISMATCH;
    if (id_status_verify(root_pk, status) != ID_OK) return PP_ERR_AUTH;
    if (!id_status_is_active(status, cert->cred_id)) return PP_ERR_REVOKED;
    if (id_dev_cert_verify(root_pk, cert, device_pk) != ID_OK) return PP_ERR_AUTH;
    return PP_OK;
}

pp_status_t pp_verify_device_spend(const pqm_sig_pk_t *root_pk, const id_cred_status_t *status,
                                   const id_dev_cert_t *cert,
                                   const uint8_t device_pk[ID_MLDSA65_PK], const pp_spend_t *sp,
                                   const uint8_t sig[ID_MLDSA65_SIG])
{
    if (!sp || !sig) return PP_ERR_ARG;
    if (!pp_is_zero(sp->grant, PP_ID)) return PP_ERR_MISMATCH;
    pp_status_t s = pp_device_chain(root_pk, status, cert, device_pk, sp->signer);
    if (s != PP_OK) return s;
    return pp_voucher_verify(device_pk, status->root_did, sp, sig) ? PP_OK : PP_ERR_AUTH;
}
