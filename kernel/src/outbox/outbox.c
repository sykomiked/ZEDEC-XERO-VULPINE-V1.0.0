/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* outbox.c — offline grants, signed entries and once-only settlement
 * (outbox.h G1-G3, E1-E4, S1-S4, D1-D5, C1-C3). Freestanding. */
#include "outbox.h"
#include "../pay/pay_util.h"
#include "../mlkem/keccak.h"

static const uint8_t CTX_GRANT[13] = {'Z', 'X', 'V', '-', 'O', 'B', 'X',
                                      '-', 'G', 'R', 'A', 'N', 'T'};
static const uint8_t CTX_ENTRY[13] = {'Z', 'X', 'V', '-', 'O', 'B', 'X',
                                      '-', 'E', 'N', 'T', 'R', 'Y'};
static const uint8_t CTX_CLOSE[13] = {'Z', 'X', 'V', '-', 'O', 'B', 'X',
                                      '-', 'C', 'L', 'O', 'S', 'E'};

#define GRANT_BODY 130u
#define ENTRY_BODY 96u
#define CLOSE_BODY 76u

/* ---- canonical little-endian encodings -------------------------------- */
typedef struct {
    uint8_t *p;
    uint32_t o;
} wr_t;

static void put(wr_t *w, const uint8_t *s, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) w->p[w->o++] = s[i];
}
static void put_u(wr_t *w, uint64_t v, unsigned bytes)
{
    for (unsigned i = 0; i < bytes; i++) w->p[w->o++] = (uint8_t) (v >> (8 * i));
}

static void grant_body(const obx_grant_t *g, uint8_t b[GRANT_BODY])
{
    wr_t w = {b, 0};
    put(&w, g->id, OBX_ID_BYTES);
    put(&w, g->ledger_id, OBX_ID_BYTES);
    put(&w, g->device_id, OBX_ID_BYTES);
    put_u(&w, g->holder, 4);
    put_u(&w, g->asset, 2);
    put_u(&w, g->limit, 8);
    put_u(&w, g->max_entries, 4);
    put_u(&w, g->issued_tick, 8);
    put_u(&w, g->expires_tick, 8);
}

static void entry_body(const obx_entry_t *e, uint8_t b[ENTRY_BODY])
{
    wr_t w = {b, 0};
    put(&w, e->grant_id, OBX_ID_BYTES);
    put_u(&w, e->seq, 4);
    put_u(&w, e->amount, 8);
    put_u(&w, e->cumulative, 8);
    put_u(&w, e->payee, 4);
    put_u(&w, e->tick, 8);
    put(&w, e->prev, OBX_ID_BYTES);
}

static void close_body(const obx_close_t *c, uint8_t b[CLOSE_BODY])
{
    wr_t w = {b, 0};
    put(&w, c->grant_id, OBX_ID_BYTES);
    put_u(&w, c->last_seq, 4);
    put_u(&w, c->cumulative, 8);
    put(&w, c->last_hash, OBX_ID_BYTES);
}

void obx_entry_hash(const obx_entry_t *e, uint8_t h[OBX_ID_BYTES])
{
    uint8_t b[sizeof CTX_ENTRY + ENTRY_BODY];
    pay_memcpy(b, CTX_ENTRY, sizeof CTX_ENTRY);
    entry_body(e, b + sizeof CTX_ENTRY);
    shake256(b, sizeof b, h, OBX_ID_BYTES);
}

void obx_key_id(const uint8_t pk[OBX_PK_BYTES], uint8_t id[OBX_ID_BYTES])
{
    shake256(pk, OBX_PK_BYTES, id, OBX_ID_BYTES); /* == hwk_key_id */
}

static bool grant_sig_ok(const uint8_t ledger_pk[OBX_PK_BYTES], const obx_grant_t *g)
{
    uint8_t b[GRANT_BODY], id[OBX_ID_BYTES];
    obx_key_id(ledger_pk, id);
    if (!pay_memeq(id, g->ledger_id, OBX_ID_BYTES)) return false;
    grant_body(g, b);
    return pqm_mldsa87_verify(ledger_pk, b, sizeof b, CTX_GRANT, sizeof CTX_GRANT, g->sig);
}

static bool entry_sig_ok(const uint8_t device_pk[OBX_PK_BYTES], const obx_entry_t *e)
{
    uint8_t b[ENTRY_BODY];
    entry_body(e, b);
    return pqm_mldsa87_verify(device_pk, b, sizeof b, CTX_ENTRY, sizeof CTX_ENTRY, e->sig);
}

static bool close_sig_ok(const uint8_t device_pk[OBX_PK_BYTES], const obx_close_t *c)
{
    uint8_t b[CLOSE_BODY];
    close_body(c, b);
    return pqm_mldsa87_verify(device_pk, b, sizeof b, CTX_CLOSE, sizeof CTX_CLOSE, c->sig);
}

obx_status_t obx_verify_offline(const uint8_t ledger_pk[OBX_PK_BYTES], const obx_grant_t *g,
                                const uint8_t device_pk[OBX_PK_BYTES], const obx_entry_t *e)
{
    if (!ledger_pk || !g || !device_pk || !e) return OBX_ERR_ARG;
    uint8_t id[OBX_ID_BYTES];
    if (!grant_sig_ok(ledger_pk, g)) return OBX_ERR_SIG;
    obx_key_id(device_pk, id);
    if (!pay_memeq(id, g->device_id, OBX_ID_BYTES)) return OBX_ERR_SIG;
    if (!pay_memeq(e->grant_id, g->id, OBX_ID_BYTES)) return OBX_ERR_STATE;
    if (!entry_sig_ok(device_pk, e)) return OBX_ERR_SIG;
    if (e->seq == 0 || e->seq > g->max_entries || e->amount == 0 || e->amount > e->cumulative ||
        e->cumulative > g->limit)
        return OBX_ERR_LIMIT;
    if (e->seq == 1 && (e->cumulative != e->amount || !pay_memeq(e->prev, g->id, OBX_ID_BYTES)))
        return OBX_ERR_LIMIT;
    return OBX_OK;
}

/* ---- device ----------------------------------------------------------- */
obx_status_t obx_device_init(obx_device_t *dev, const obx_grant_t *g,
                             const uint8_t ledger_pk[OBX_PK_BYTES],
                             const uint8_t device_pk[OBX_PK_BYTES])
{
    if (!dev || !g || !ledger_pk || !device_pk) return OBX_ERR_ARG;
    if (g->max_entries == 0 || g->max_entries > OBX_MAX_ENTRIES || g->limit == 0)
        return OBX_ERR_ARG;
    if (!grant_sig_ok(ledger_pk, g)) return OBX_ERR_SIG;
    uint8_t id[OBX_ID_BYTES];
    obx_key_id(device_pk, id);
    if (!pay_memeq(id, g->device_id, OBX_ID_BYTES)) return OBX_ERR_SIG;
    pay_memset(dev, 0, sizeof *dev);
    pay_memcpy(&dev->grant, g, sizeof *g);
    dev->next_seq = 1;
    pay_memcpy(dev->prev, g->id, OBX_ID_BYTES);
    return OBX_OK;
}

obx_status_t obx_device_spend(obx_device_t *dev, uint32_t payee, uint64_t amount, uint64_t tick,
                              obx_sign_fn sign, void *sign_ctx, obx_persist_fn persist,
                              void *persist_ctx, obx_entry_t *out)
{
    if (!dev || !sign || !persist || !out || amount == 0) return OBX_ERR_ARG;
    if (dev->closed) return OBX_ERR_STATE;
    uint64_t cum;
    if (dev->next_seq > dev->grant.max_entries || dev->n >= OBX_MAX_ENTRIES ||
        !pay_add_ok(dev->cumulative, amount, &cum) || cum > dev->grant.limit)
        return OBX_ERR_LIMIT; /* E2 */
    uint32_t i = dev->n;
    obx_entry_t *e = &dev->e[i];
    pay_memset(e, 0, sizeof *e);
    pay_memcpy(e->grant_id, dev->grant.id, OBX_ID_BYTES);
    e->seq = dev->next_seq;
    e->amount = amount;
    e->cumulative = cum;
    e->payee = payee;
    e->tick = tick;
    pay_memcpy(e->prev, dev->prev, OBX_ID_BYTES);
    uint8_t b[ENTRY_BODY];
    entry_body(e, b);
    if (sign(sign_ctx, b, sizeof b, CTX_ENTRY, sizeof CTX_ENTRY, e->sig)) {
        pay_memset(e, 0, sizeof *e);
        return OBX_ERR_SIGNER;
    }
    /* E3: advance, persist, and undo on failure */
    uint8_t old_prev[OBX_ID_BYTES];
    uint64_t old_cum = dev->cumulative;
    pay_memcpy(old_prev, dev->prev, OBX_ID_BYTES);
    obx_entry_hash(e, dev->prev);
    dev->cumulative = cum;
    dev->next_seq++;
    dev->state[i] = OBX_E_PENDING;
    dev->result[i] = 0;
    dev->n++;
    if (persist(persist_ctx, dev)) {
        dev->n--;
        dev->next_seq--;
        dev->cumulative = old_cum;
        pay_memcpy(dev->prev, old_prev, OBX_ID_BYTES);
        pay_memset(e, 0, sizeof *e);
        return OBX_ERR_PERSIST;
    }
    pay_memcpy(out, e, sizeof *e);
    return OBX_OK;
}

int32_t obx_device_next_pending(const obx_device_t *dev, uint32_t from)
{
    if (!dev) return -1;
    for (uint32_t i = from; i < dev->n; i++)
        if (dev->state[i] == OBX_E_PENDING) return (int32_t) i;
    return -1;
}

obx_status_t obx_device_ack(obx_device_t *dev, uint32_t seq, obx_result_t r, obx_persist_fn persist,
                            void *persist_ctx)
{
    if (!dev || !persist || seq == 0 || seq > dev->n) return OBX_ERR_ARG;
    uint32_t i = seq - 1;
    uint8_t os = dev->state[i], orr = dev->result[i];
    if (r == OBX_R_ACCEPTED || r == OBX_R_DUPLICATE)
        dev->state[i] = OBX_E_SETTLED;
    else if (r == OBX_R_NO_PAYEE || r == OBX_R_LEDGER)
        dev->state[i] = OBX_E_PENDING;
    else
        dev->state[i] = OBX_E_REJECTED;
    dev->result[i] = (uint8_t) r;
    if (persist(persist_ctx, dev)) {
        dev->state[i] = os;
        dev->result[i] = orr;
        return OBX_ERR_PERSIST;
    }
    return OBX_OK;
}

obx_status_t obx_device_close(obx_device_t *dev, obx_sign_fn sign, void *sign_ctx,
                              obx_persist_fn persist, void *persist_ctx, obx_close_t *out)
{
    if (!dev || !sign || !persist || !out) return OBX_ERR_ARG;
    if (!dev->closed) {
        obx_close_t *c = &dev->close;
        pay_memset(c, 0, sizeof *c);
        pay_memcpy(c->grant_id, dev->grant.id, OBX_ID_BYTES);
        c->last_seq = dev->next_seq - 1;
        c->cumulative = dev->cumulative;
        pay_memcpy(c->last_hash, dev->prev, OBX_ID_BYTES);
        uint8_t b[CLOSE_BODY];
        close_body(c, b);
        if (sign(sign_ctx, b, sizeof b, CTX_CLOSE, sizeof CTX_CLOSE, c->sig)) {
            pay_memset(c, 0, sizeof *c);
            return OBX_ERR_SIGNER;
        }
        dev->closed = true;
        if (persist(persist_ctx, dev)) {
            dev->closed = false;
            pay_memset(c, 0, sizeof *c);
            return OBX_ERR_PERSIST;
        }
    }
    pay_memcpy(out, &dev->close, sizeof *out);
    return OBX_OK;
}

/* ---- ledger ----------------------------------------------------------- */
static bool find_acct(const pay_ledger_t *L, uint32_t owner, uint16_t asset, uint32_t *out)
{
    for (uint32_t a = 0; a < L->n_accounts; a++) {
        const pay_account_t *x = &L->acct[a];
        if (x->active && x->owner == owner && x->asset == asset && x->cap == PAY_CAP_FINANCIAL) {
            *out = a;
            return true;
        }
    }
    return false;
}

static void derive(const obx_ledger_t *lg, uint8_t tag, uint64_t n, uint8_t *out, uint32_t len)
{
    uint8_t b[32 + 1 + 8];
    pay_memcpy(b, lg->seed, 32);
    b[32] = tag;
    for (unsigned i = 0; i < 8; i++) b[33 + i] = (uint8_t) (n >> (8 * i));
    shake256(b, sizeof b, out, len);
}

static pay_status_t move(obx_ledger_t *lg, uint32_t from, uint32_t to, uint64_t amount,
                         uint64_t now, const char *memo)
{
    pay_posting_req_t req;
    pay_receipt_t rc;
    uint8_t rnd[16];
    pay_memset(&req, 0, sizeof req);
    derive(lg, 'I', lg->n_posts, req.idem_key, sizeof req.idem_key);
    derive(lg, 'U', lg->n_posts, rnd, sizeof rnd);
    pay_uetr_from_random(rnd, req.uetr);
    pay_w w;
    pay_w_init(&w, req.e2e, sizeof req.e2e);
    pay_w_s(&w, "OBX-");
    pay_w_u64(&w, lg->n_posts);
    pay_w_finish(&w);
    pay_strlcpy(req.memo, memo, sizeof req.memo);
    req.initiator = lg->escrow_owner;
    req.attestor = lg->escrow_owner;
    req.tick = now;
    req.kind = PAY_KIND_TRANSFER;
    req.ext_cap = PAY_CAP_FINANCIAL;
    pay_status_t st = pay_ledger_transfer(lg->L, &req, from, to, amount, &rc);
    if (st == PAY_OK) lg->n_posts++;
    return st;
}

obx_status_t obx_ledger_init(obx_ledger_t *lg, pay_ledger_t *L, uint16_t asset,
                             uint32_t escrow_owner, const uint8_t seed[32],
                             const uint8_t ledger_pk[OBX_PK_BYTES], obx_sign_fn sign,
                             void *sign_ctx, uint64_t grace_ticks)
{
    if (!lg || !L || !seed || !ledger_pk || !sign) return OBX_ERR_ARG;
    pay_memset(lg, 0, sizeof *lg);
    lg->L = L;
    lg->asset = asset;
    lg->escrow_owner = escrow_owner;
    pay_memcpy(lg->seed, seed, 32);
    pay_memcpy(lg->pk, ledger_pk, OBX_PK_BYTES);
    obx_key_id(ledger_pk, lg->id);
    lg->sign = sign;
    lg->sign_ctx = sign_ctx;
    lg->grace = grace_ticks;
    if (!find_acct(L, escrow_owner, asset, &lg->escrow_acct) &&
        pay_ledger_open(L, escrow_owner, asset, PAY_CAP_FINANCIAL, 0, &lg->escrow_acct) != PAY_OK)
        return OBX_ERR_LEDGER;
    return OBX_OK;
}

bool obx_ledger_flagged(const obx_ledger_t *lg, const uint8_t device_id[OBX_ID_BYTES])
{
    if (!lg || !device_id) return false;
    for (uint32_t i = 0; i < lg->n_flagged; i++)
        if (pay_memeq(lg->flagged[i], device_id, OBX_ID_BYTES)) return true;
    return false;
}

static void flag(obx_ledger_t *lg, const uint8_t device_id[OBX_ID_BYTES])
{
    lg->conflicts++;
    if (obx_ledger_flagged(lg, device_id)) return;
    if (lg->n_flagged < OBX_MAX_FLAGGED) {
        pay_memcpy(lg->flagged[lg->n_flagged++], device_id, OBX_ID_BYTES);
    } else { /* full: keep the newest, the oldest has had its grants refused longest */
        for (uint32_t i = 1; i < OBX_MAX_FLAGGED; i++)
            pay_memcpy(lg->flagged[i - 1], lg->flagged[i], OBX_ID_BYTES);
        pay_memcpy(lg->flagged[OBX_MAX_FLAGGED - 1], device_id, OBX_ID_BYTES);
    }
}

obx_status_t obx_ledger_grant(obx_ledger_t *lg, uint32_t holder,
                              const uint8_t device_pk[OBX_PK_BYTES], uint64_t limit,
                              uint32_t max_entries, uint64_t now, uint64_t expires,
                              obx_grant_t *out)
{
    if (!lg || !device_pk || !out || limit == 0 || max_entries == 0 ||
        max_entries > OBX_MAX_ENTRIES || expires <= now || holder == lg->escrow_owner)
        return OBX_ERR_ARG;
    uint8_t dev_id[OBX_ID_BYTES];
    obx_key_id(device_pk, dev_id);
    if (obx_ledger_flagged(lg, dev_id)) return OBX_ERR_FLAGGED; /* G3 */
    uint32_t holder_acct;
    if (!find_acct(lg->L, holder, lg->asset, &holder_acct)) return OBX_ERR_LEDGER;
    /* a free slot, else the oldest finished one */
    obx_slot_t *s = 0;
    for (uint32_t i = 0; i < OBX_MAX_GRANTS && !s; i++)
        if (!lg->slot[i].used) s = &lg->slot[i];
    for (uint32_t i = 0; i < OBX_MAX_GRANTS && !s; i++)
        if (lg->slot[i].finished) s = &lg->slot[i];
    if (!s) return OBX_ERR_FULL;

    obx_grant_t g;
    pay_memset(&g, 0, sizeof g);
    derive(lg, 'G', lg->n_grants, g.id, OBX_ID_BYTES);
    pay_memcpy(g.ledger_id, lg->id, OBX_ID_BYTES);
    pay_memcpy(g.device_id, dev_id, OBX_ID_BYTES);
    g.holder = holder;
    g.asset = lg->asset;
    g.limit = limit;
    g.max_entries = max_entries;
    g.issued_tick = now;
    g.expires_tick = expires;
    uint8_t b[GRANT_BODY];
    grant_body(&g, b);
    if (lg->sign(lg->sign_ctx, b, sizeof b, CTX_GRANT, sizeof CTX_GRANT, g.sig))
        return OBX_ERR_SIGNER;
    if (move(lg, holder_acct, lg->escrow_acct, limit, now, "offline grant") != PAY_OK)
        return OBX_ERR_LEDGER; /* G1: the money must be there */
    lg->n_grants++;
    pay_memset(s, 0, sizeof *s);
    s->used = true;
    pay_memcpy(&s->grant, &g, sizeof g);
    pay_memcpy(s->device_pk, device_pk, OBX_PK_BYTES);
    s->holder_acct = holder_acct;
    s->covered = limit;
    pay_memcpy(out, &g, sizeof g);
    return OBX_OK;
}

static obx_slot_t *find_slot(obx_ledger_t *lg, const uint8_t id[OBX_ID_BYTES])
{
    for (uint32_t i = 0; i < OBX_MAX_GRANTS; i++)
        if (lg->slot[i].used && pay_memeq(lg->slot[i].grant.id, id, OBX_ID_BYTES))
            return &lg->slot[i];
    return 0;
}

static void finish(obx_ledger_t *lg, obx_slot_t *s, uint64_t now)
{
    uint64_t left = s->covered - s->settled;
    if (left &&
        move(lg, lg->escrow_acct, s->holder_acct, left, now, "offline grant return") != PAY_OK)
        return; /* stays live; obx_ledger_check will show it */
    s->settled = s->covered;
    s->finished = true;
}

obx_status_t obx_ledger_submit(obx_ledger_t *lg, const obx_entry_t *e, uint64_t now,
                               obx_result_t *res)
{
    if (!lg || !e || !res) return OBX_ERR_ARG;
    obx_slot_t *s = find_slot(lg, e->grant_id);
    if (!s) return (*res = OBX_R_UNKNOWN_GRANT), OBX_OK;
    if (!entry_sig_ok(s->device_pk, e)) return (*res = OBX_R_BAD_SIG), OBX_OK;
    if (e->seq == 0 || e->seq > s->grant.max_entries) return (*res = OBX_R_BAD_SEQ), OBX_OK;
    uint32_t i = e->seq - 1;
    uint8_t h[OBX_ID_BYTES];
    obx_entry_hash(e, h);
    if (s->seen[i]) { /* S3 / D2 */
        if (pay_memeq(s->hash[i], h, OBX_ID_BYTES)) return (*res = OBX_R_DUPLICATE), OBX_OK;
        flag(lg, s->grant.device_id);
        return (*res = OBX_R_CONFLICT), OBX_OK;
    }
    uint64_t end;
    if (s->finished || !pay_add_ok(s->grant.expires_tick, lg->grace, &end) || now >= end)
        return (*res = OBX_R_EXPIRED), OBX_OK;
    if (s->closed && e->seq > s->close_last) { /* D5 */
        flag(lg, s->grant.device_id);
        return (*res = OBX_R_CONFLICT), OBX_OK;
    }
    if (e->amount == 0 || e->amount > e->cumulative) { /* D4 */
        flag(lg, s->grant.device_id);
        return (*res = OBX_R_BAD_ENTRY), OBX_OK;
    }
    uint64_t total;
    if (e->cumulative > s->covered || !pay_add_ok(s->settled, e->amount, &total) ||
        total > s->covered) { /* D1, D4 */
        flag(lg, s->grant.device_id);
        return (*res = OBX_R_OVER_LIMIT), OBX_OK;
    }
    /* D3: agree with accepted neighbours */
    bool ok = true;
    if (e->seq == 1)
        ok = e->cumulative == e->amount && pay_memeq(e->prev, s->grant.id, OBX_ID_BYTES);
    else if (s->seen[i - 1])
        ok = pay_memeq(e->prev, s->hash[i - 1], OBX_ID_BYTES) &&
             e->cumulative - e->amount == s->cum[i - 1];
    if (ok && i + 1 < s->grant.max_entries && s->seen[i + 1])
        ok = pay_memeq(s->prev[i + 1], h, OBX_ID_BYTES) &&
             s->cum[i + 1] - s->amt[i + 1] == e->cumulative;
    if (ok && s->closed && e->seq == s->close_last) /* the entry the close named */
        ok = pay_memeq(h, s->close_hash, OBX_ID_BYTES) && e->cumulative == s->covered;
    if (!ok) {
        flag(lg, s->grant.device_id);
        return (*res = OBX_R_CONFLICT), OBX_OK;
    }
    uint32_t payee_acct;
    if (!find_acct(lg->L, e->payee, lg->asset, &payee_acct) || payee_acct == lg->escrow_acct)
        return (*res = OBX_R_NO_PAYEE), OBX_OK; /* S4: not recorded */
    if (move(lg, lg->escrow_acct, payee_acct, e->amount, now, "offline payment") != PAY_OK)
        return (*res = OBX_R_LEDGER), OBX_OK;
    s->seen[i] = 1;
    s->amt[i] = e->amount;
    s->cum[i] = e->cumulative;
    pay_memcpy(s->hash[i], h, OBX_ID_BYTES);
    pay_memcpy(s->prev[i], e->prev, OBX_ID_BYTES);
    s->settled = total;
    if (s->closed && s->settled == s->covered) s->finished = true;
    *res = OBX_R_ACCEPTED;
    return OBX_OK;
}

obx_status_t obx_ledger_close(obx_ledger_t *lg, const obx_close_t *c, uint64_t now)
{
    if (!lg || !c) return OBX_ERR_ARG;
    obx_slot_t *s = find_slot(lg, c->grant_id);
    if (!s || s->finished) return OBX_ERR_STATE;
    if (!close_sig_ok(s->device_pk, c)) return OBX_ERR_SIG;
    if (s->closed) return OBX_OK; /* the same close again */
    if (c->last_seq > s->grant.max_entries || c->cumulative > s->grant.limit ||
        c->cumulative < s->settled || (c->last_seq == 0) != (c->cumulative == 0)) {
        flag(lg, s->grant.device_id);
        return OBX_ERR_LIMIT;
    }
    for (uint32_t i = c->last_seq; i < s->grant.max_entries; i++)
        if (s->seen[i]) { /* D5: already paid an entry after the close */
            flag(lg, s->grant.device_id);
            return OBX_ERR_STATE;
        }
    if (c->last_seq && s->seen[c->last_seq - 1] &&
        (!pay_memeq(s->hash[c->last_seq - 1], c->last_hash, OBX_ID_BYTES) ||
         s->cum[c->last_seq - 1] != c->cumulative)) {
        flag(lg, s->grant.device_id);
        return OBX_ERR_STATE;
    }
    uint64_t back = s->grant.limit - c->cumulative;
    if (back &&
        move(lg, lg->escrow_acct, s->holder_acct, back, now, "offline grant close") != PAY_OK)
        return OBX_ERR_LEDGER;
    s->closed = true;
    s->close_last = c->last_seq;
    pay_memcpy(s->close_hash, c->last_hash, OBX_ID_BYTES);
    s->covered = c->cumulative;
    if (s->settled == s->covered) s->finished = true;
    return OBX_OK;
}

uint32_t obx_ledger_expire(obx_ledger_t *lg, uint64_t now)
{
    if (!lg) return 0;
    uint32_t n = 0;
    for (uint32_t i = 0; i < OBX_MAX_GRANTS; i++) {
        obx_slot_t *s = &lg->slot[i];
        uint64_t end;
        if (!s->used || s->finished) continue;
        if (pay_add_ok(s->grant.expires_tick, lg->grace, &end) && now < end) continue;
        finish(lg, s, now);
        if (s->finished) n++;
    }
    return n;
}

bool obx_ledger_check(const obx_ledger_t *lg)
{
    if (!lg) return false;
    const pay_account_t *a = pay_ledger_account(lg->L, lg->escrow_acct);
    if (!a || a->credit) return false;
    uint64_t want = 0;
    for (uint32_t i = 0; i < OBX_MAX_GRANTS; i++) {
        const obx_slot_t *s = &lg->slot[i];
        if (!s->used || s->finished) continue;
        if (s->settled > s->covered || !pay_add_ok(want, s->covered - s->settled, &want))
            return false;
    }
    return a->debit == want;
}
