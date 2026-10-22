/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_vf_prize.c — the prize court (vf_prize.h) over real ML-DSA-87. */
#include <stdio.h>
#include <string.h>
#include "vf_prize.h"

static int g_pass, g_fail;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            g_pass++;                                                                              \
        else {                                                                                     \
            g_fail++;                                                                              \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);                                     \
        }                                                                                          \
    } while (0)

static vna_identity_t IA, IB, IC, ICOM; /* A commissions and pays out, B and C deliver */
static uint8_t V[VF_VOUCHER_LEN];
static uint8_t BLOCK[10000];
static uint8_t BLOCK_CID[VNA_CID_RAW_LEN];

/* ---- a stand-in for the board's seal check: ML-DSA-87 over the fields ---- */
static void wbytes(const vfp_warrant_t *w, uint8_t *m, uint32_t *n)
{
    uint32_t k = 0;
    memcpy(m + k, w->id, 32), k += 32;
    memcpy(m + k, w->commissioner, 32), k += 32;
    memcpy(m + k, w->bearer.b, 32), k += 32;
    m[k++] = w->kind;
    m[k++] = w->resource;
    memcpy(m + k, &w->alexandria_x100, 4), k += 4;
    memcpy(m + k, w->scope, VNA_CID_RAW_LEN), k += VNA_CID_RAW_LEN;
    memcpy(m + k, &w->cap, 8), k += 8;
    memcpy(m + k, &w->expiry_epoch, 4), k += 4;
    *n = k;
}

static uint32_t g_verify_calls;
static bool verify_fn(void *ctx, const vfp_warrant_t *w, const uint8_t *seal, uint32_t len)
{
    (void) ctx;
    g_verify_calls++;
    uint8_t m[256];
    uint32_t n;
    wbytes(w, m, &n);
    return len == VNA_SIG_LEN && vna_verify(ICOM.pk, "test/board-letter", m, n, seal);
}

static bool deny_fn(void *ctx, const vfp_warrant_t *w, const uint8_t *seal, uint32_t len)
{
    (void) ctx, (void) w, (void) seal, (void) len;
    return false;
}

static uint8_t SEAL[VNA_SIG_LEN];
static void seal(const vfp_warrant_t *w)
{
    uint8_t m[256];
    uint32_t n;
    wbytes(w, m, &n);
    vna_sign(ICOM.sk, "test/board-letter", m, n, NULL, SEAL);
}

/* ---- a stand-in for AXION: RETRIEVAL prizes must be the exact block ---- */
typedef struct {
    lpres_state_t force; /* NEITHER: really check */
    bool forced;
    uint64_t force_units;
    uint32_t calls;
} audit_t;

static lpres_state_t audit_fn(void *ctx, const vfp_warrant_t *w, const vna_id_t *claimant,
                              const uint8_t *payload, uint32_t len, uint64_t *units)
{
    audit_t *a = (audit_t *) ctx;
    (void) claimant;
    a->calls++;
    if (a->forced) {
        *units = a->force_units;
        return a->force;
    }
    if (!payload || len == 0) return LPRES_STATE_NEITHER;
    uint8_t cid[VNA_CID_RAW_LEN];
    vna_cid_raw(payload, len, cid);
    if (memcmp(cid, w->scope, VNA_CID_RAW_LEN) != 0) return LPRES_STATE_FALSE;
    *units = ((uint64_t) len + VF_SECTOR_BYTES - 1) / VF_SECTOR_BYTES;
    return LPRES_STATE_TRUE;
}

static void ident(vna_identity_t *i, uint8_t s)
{
    uint8_t seed[32];
    memset(seed, s, 32);
    CHECK(vna_identity_create(i, seed, 0, 1) == VNA_OK);
}

static vfp_warrant_t warrant(uint8_t tag, const vna_id_t *bearer, uint64_t cap, uint32_t expiry)
{
    vfp_warrant_t w;
    memset(&w, 0, sizeof w);
    w.id[0] = tag;
    w.id[31] = 0x4C;
    memcpy(w.commissioner, ICOM.id.b, 32);
    if (bearer)
        w.bearer = *bearer;
    else
        memset(w.bearer.b, 0xFF, 32);
    w.kind = VFP_RETRIEVAL;
    w.resource = VF_RES_TRANSFER;
    w.alexandria_x100 = 52010;
    memcpy(w.scope, BLOCK_CID, VNA_CID_RAW_LEN);
    w.cap = cap;
    w.expiry_epoch = expiry;
    return w;
}

typedef struct {
    vf_t a, b, c;
    vfp_court_t ca, cb, cc;
    audit_t au;
    int32_t ab, ac, ba, ca_;
} world_t;

static world_t W;

static void world(void)
{
    memset(&W, 0, sizeof W);
    vf_init(&W.a, &IA, 10);
    vf_init(&W.b, &IB, 10);
    vf_init(&W.c, &IC, 10);
    vfp_init(&W.ca, verify_fn, NULL, audit_fn, &W.au);
    vfp_init(&W.cb, verify_fn, NULL, NULL, NULL);
    vfp_init(&W.cc, verify_fn, NULL, NULL, NULL);
    vfp_bind(&W.ca, &W.a);
    vfp_bind(&W.cb, &W.b);
    vfp_bind(&W.cc, &W.c);
    W.ab = vf_add_peer(&W.a, IB.pk);
    W.ac = vf_add_peer(&W.a, IC.pk);
    W.ba = vf_add_peer(&W.b, IA.pk);
    W.ca_ = vf_add_peer(&W.c, IA.pk);
}

/* commission w at all three courts */
static void everywhere(const vfp_warrant_t *w)
{
    seal(w);
    CHECK(vfp_commission(&W.ca, w, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vfp_commission(&W.cb, w, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vfp_commission(&W.cc, w, SEAL, VNA_SIG_LEN) == VF_OK);
}

static void t_commission(void)
{
    world();
    CHECK(W.a.letter == vfp_letter && W.a.letter_ctx == &W.ca && W.ca.f == &W.a);
    vfp_warrant_t w = warrant(1, &IB.id, 100, 50);
    seal(&w);
    CHECK(vfp_commission(&W.ca, NULL, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    vfp_warrant_t x = w;
    x.kind = 0;
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    x.kind = 4;
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    x = w, x.resource = VF_RES_COUNT;
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    x = w, x.cap = 0;
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    x = w, x.cap = VF_CAP_LIMIT + 1;
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    x = w, x.scope[0] = 0;
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_ARG);
    CHECK(g_verify_calls == 0);
    x = w, x.cap = 101; /* a field the seal does not cover */
    CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_E_SIG);
    CHECK(vfp_commission(&W.ca, &w, SEAL, VNA_SIG_LEN - 1) == VF_E_SIG);
    vfp_court_t none, no;
    vfp_init(&none, NULL, NULL, NULL, NULL);
    vfp_init(&no, deny_fn, NULL, NULL, NULL);
    CHECK(vfp_commission(&none, &w, SEAL, VNA_SIG_LEN) == VF_E_SIG);
    CHECK(vfp_commission(&no, &w, SEAL, VNA_SIG_LEN) == VF_E_SIG);
    CHECK(vfp_commission(&W.ca, &w, SEAL, VNA_SIG_LEN) == VF_OK);
    const vfp_entry_t *e = vfp_get(&W.ca, w.id);
    CHECK(e && e->state == VFP_ACTIVE && e->drawn == 0 && e->w.cap == 100);
    CHECK(e->w.alexandria_x100 == 52010 && e->w.kind == VFP_RETRIEVAL);
    CHECK(vfp_commission(&W.ca, &w, SEAL, VNA_SIG_LEN) == VFP_E_DUP);
    for (uint32_t k = VFP_CARTOGRAPHY; k >= VFP_RETRIEVAL; k--) {
        x = w, x.id[1] = (uint8_t) k, x.kind = (uint8_t) k;
        x.resource = (uint8_t) (VF_RES_COUNT - k);
        x.cap = (k == VFP_AUDIT) ? VF_CAP_LIMIT : 1;
        seal(&x);
        CHECK(vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN) == VF_OK);
    }
    /* fill to the limit */
    uint32_t n = 4;
    int32_t st = VF_OK;
    for (uint32_t i = 0; i < VFP_MAX_WARRANTS + 1 && st == VF_OK; i++) {
        x = w, x.id[2] = (uint8_t) (i + 1);
        seal(&x);
        st = vfp_commission(&W.ca, &x, SEAL, VNA_SIG_LEN);
        if (st == VF_OK) n++;
    }
    CHECK(n == VFP_MAX_WARRANTS && st == VF_E_FULL);
    CHECK(vfp_get(&W.ca, x.id) == NULL);
    uint8_t unk[32] = {9};
    CHECK(vfp_get(&W.ca, unk) == NULL);
    CHECK(vfp_revoke(&W.ca, unk) == VFP_E_PIRACY);
}

static void t_claim(void)
{
    world();
    vfp_warrant_t w = warrant(1, &IB.id, 5, 50);
    everywhere(&w);
    uint8_t unk[32] = {7};
    lpres_state_t v = LPRES_STATE_BOTH;

    vfp_court_t loose;
    vfp_init(&loose, verify_fn, NULL, audit_fn, &W.au);
    CHECK(vfp_claim(&loose, 0, w.id, BLOCK, 9000, 9, NULL, V, &v) == VF_E_ARG);
    CHECK(v == LPRES_STATE_NEITHER);
    CHECK(vfp_claim(&W.ca, 0, NULL, BLOCK, 9000, 9, NULL, V, NULL) == VF_E_ARG);
    CHECK(vfp_claim(&W.ca, 2, w.id, BLOCK, 9000, 9, NULL, V, NULL) == VF_E_PEER);
    CHECK(vfp_claim(&W.ca, VF_MAX_PEERS, w.id, BLOCK, 9000, 9, NULL, V, NULL) == VF_E_PEER);
    CHECK(vfp_claim(&W.ca, 0, unk, BLOCK, 9000, 9, NULL, V, NULL) == VFP_E_PIRACY);
    CHECK(W.ca.piracy == 1);
    CHECK(vfp_claim(&W.ca, 1, w.id, BLOCK, 9000, 9, NULL, V, NULL) == VF_E_BEARER); /* C, not B */
    CHECK(W.au.calls == 0);

    /* the audit: wrong bytes are condemned, nothing is drawn */
    BLOCK[5] ^= 1;
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, &v) == VFP_E_CONDEMNED);
    CHECK(v == LPRES_STATE_FALSE);
    BLOCK[5] ^= 1;
    CHECK(vfp_claim(&W.ca, 0, w.id, NULL, 0, 9, NULL, V, &v) == VFP_E_HELD);
    CHECK(v == LPRES_STATE_NEITHER);
    W.au.forced = true;
    W.au.force = LPRES_STATE_BOTH;
    W.au.force_units = 2;
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, &v) == VFP_E_HELD);
    CHECK(v == LPRES_STATE_BOTH);
    W.au.force = LPRES_STATE_TRUE;
    W.au.force_units = 0;
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, &v) == VFP_E_CONDEMNED);
    CHECK(v == LPRES_STATE_TRUE);
    W.au.forced = false;
    const vfp_entry_t *e = vfp_get(&W.ca, w.id);
    CHECK(e->condemned == 2 && e->held == 2 && e->cleared == 0 && e->drawn == 0);
    CHECK(W.ca.condemned == 2 && W.ca.held == 2 && W.ca.cleared == 0);
    CHECK(W.a.peer[0].out_seq == 0);

    /* no audit hook: held */
    W.ca.audit = NULL;
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, &v) == VFP_E_HELD);
    CHECK(v == LPRES_STATE_NEITHER && W.ca.held == 2); /* not counted: no audit ran */
    W.ca.audit = audit_fn;

    /* P5: a direct issue is refused even under a live warrant */
    CHECK(vf_issue(&W.a, 0, VF_RES_TRANSFER, BLOCK_CID, w.id, 1, 9, NULL, V) == VF_E_LETTER);

    /* the prize clears: 9000 bytes = 3 sectors */
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, &v) == VF_OK);
    CHECK(v == LPRES_STATE_TRUE && !W.ca.pending);
    CHECK(e->drawn == 3 && e->cleared == 1 && W.ca.cleared == 1 && e->state == VFP_ACTIVE);
    vf_voucher_t vv;
    CHECK(vf_decode(V, VF_VOUCHER_LEN, &vv) == VF_OK);
    CHECK(vv.units == 3 && memcmp(vv.letter, w.id, 32) == 0 && vv.resource == VF_RES_TRANSFER);
    CHECK(memcmp(vv.scope, BLOCK_CID, VNA_CID_RAW_LEN) == 0);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_net(&W.b, 0, VF_RES_TRANSFER) == 3 && W.cb.piracy == 0);
    CHECK(vfp_get(&W.cb, w.id)->drawn == 0); /* the bearer only checks */

    /* over the cap: refused whole */
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, NULL) == VF_E_CAP);
    CHECK(e->drawn == 3 && W.a.peer[0].out_seq == 1);
    /* other bytes than the scope: condemned */
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 8000, 9, NULL, V, NULL) == VFP_E_CONDEMNED);
    /* exactly to the cap: fulfilled */
    W.au.forced = true;
    W.au.force = LPRES_STATE_TRUE;
    W.au.force_units = 2;
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, NULL) == VF_OK);
    CHECK(e->drawn == 5 && e->state == VFP_FULFILLED && e->cleared == 2);
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 9000, 9, NULL, V, NULL) == VFP_E_CLOSED);
    W.au.forced = false;
}

static void t_cap_open(void)
{
    world();
    /* a warrant over a 4096-byte block, open to anyone, cap 3 sectors */
    vfp_warrant_t w = warrant(2, NULL, 3, 50);
    vna_cid_raw(BLOCK, 4096, w.scope);
    everywhere(&w);
    const vfp_entry_t *e = vfp_get(&W.ca, w.id);
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK); /* B */
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vfp_claim(&W.ca, 1, w.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK); /* C */
    CHECK(vf_accept(&W.c, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_E_BEARER);
    CHECK(e->drawn == 2 && e->state == VFP_ACTIVE);
    CHECK(vfp_claim(&W.ca, 0, w.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK);
    CHECK(e->drawn == 3 && e->state == VFP_FULFILLED);
    CHECK(vfp_claim(&W.ca, 1, w.id, BLOCK, 4096, 9, NULL, V, NULL) == VFP_E_CLOSED);
    /* the fulfilled warrant still lets the bearer accept what was drawn */
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_OK);
    CHECK(vf_net(&W.b, 0, VF_RES_TRANSFER) == 2 && vf_net(&W.c, 0, VF_RES_TRANSFER) == 1);

    /* the issuer's pair ceiling refuses after the audit: no draw, nothing pending */
    vfp_warrant_t x = warrant(3, NULL, 10, 50);
    vna_cid_raw(BLOCK, 4096, x.scope);
    everywhere(&x);
    CHECK(vf_set_cap(&W.a, 0, VF_RES_TRANSFER, 2) == VF_OK); /* B already holds 2 */
    CHECK(vfp_claim(&W.ca, 0, x.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_E_CAP);
    CHECK(vfp_get(&W.ca, x.id)->drawn == 0 && vfp_get(&W.ca, x.id)->cleared == 0);
    CHECK(!W.ca.pending && W.ca.cleared == 3);

    /* expiry closes a warrant */
    CHECK(vfp_claim(&W.ca, 1, x.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK);
    CHECK(vf_tick(&W.a, 49) == VF_OK);
    CHECK(vfp_claim(&W.ca, 1, x.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK);
    CHECK(vf_tick(&W.a, 50) == VF_OK);
    CHECK(vfp_claim(&W.ca, 1, x.id, BLOCK, 4096, 9, NULL, V, NULL) == VFP_E_CLOSED);

    /* the owner revokes: claims stop, and bearers refuse what arrives */
    world();
    vfp_warrant_t r = warrant(4, &IB.id, 10, 50);
    vna_cid_raw(BLOCK, 4096, r.scope);
    everywhere(&r);
    CHECK(vfp_claim(&W.ca, 0, r.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK);
    CHECK(vfp_revoke(&W.cb, r.id) == VF_OK && vfp_get(&W.cb, r.id)->state == VFP_REVOKED);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_E_LETTER && W.b.peer[0].in_seq == 0);
    CHECK(vfp_revoke(&W.ca, r.id) == VF_OK);
    CHECK(vfp_claim(&W.ca, 0, r.id, BLOCK, 4096, 9, NULL, V, NULL) == VFP_E_CLOSED);

    /* a bearer that never saw the warrant: uncommissioned */
    world();
    vfp_warrant_t p = warrant(5, &IB.id, 10, 50);
    vna_cid_raw(BLOCK, 4096, p.scope);
    seal(&p);
    CHECK(vfp_commission(&W.ca, &p, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vfp_claim(&W.ca, 0, p.id, BLOCK, 4096, 9, NULL, V, NULL) == VF_OK);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_E_LETTER && W.cb.piracy == 1);
    /* the bearer's copy says a different resource, or a smaller cap */
    vfp_warrant_t q = p;
    q.resource = VF_RES_STORAGE;
    seal(&q);
    CHECK(vfp_commission(&W.cb, &q, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_E_LETTER);
    vfp_court_t cb2;
    vfp_init(&cb2, verify_fn, NULL, NULL, NULL);
    vfp_bind(&cb2, &W.b);
    q = p;
    q.cap = 1; /* equal to the voucher's 1 sector: allowed */
    seal(&q);
    CHECK(vfp_commission(&cb2, &q, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_OK);
    /* and a bearer copy naming someone else */
    world();
    vfp_warrant_t s = warrant(6, &IB.id, 10, 50);
    vna_cid_raw(BLOCK, 4096, s.scope);
    seal(&s);
    CHECK(vfp_commission(&W.ca, &s, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vfp_claim(&W.ca, 0, s.id, BLOCK, 8192, 9, NULL, V, NULL) == VFP_E_CONDEMNED);
    vna_cid_raw(BLOCK, 8192, s.scope);
    vfp_warrant_t s2 = s;
    s2.id[3] = 1;
    seal(&s2);
    CHECK(vfp_commission(&W.ca, &s2, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vfp_claim(&W.ca, 0, s2.id, BLOCK, 8192, 9, NULL, V, NULL) == VF_OK); /* 2 sectors */
    vfp_warrant_t s3 = s2;
    s3.bearer = IC.id;
    seal(&s3);
    CHECK(vfp_commission(&W.cb, &s3, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_E_LETTER);
    vfp_court_t cb3;
    vfp_init(&cb3, verify_fn, NULL, NULL, NULL);
    vfp_bind(&cb3, &W.b);
    s3 = s2;
    s3.cap = 1; /* the bearer's copy caps below this voucher */
    seal(&s3);
    CHECK(vfp_commission(&cb3, &s3, SEAL, VNA_SIG_LEN) == VF_OK);
    CHECK(vf_accept(&W.b, V, VF_VOUCHER_LEN) == VF_E_LETTER);
}

/* the court's hook directly: every P5 condition */
static void t_hook(void)
{
    world();
    vfp_warrant_t w = warrant(8, &IB.id, 10, 50);
    everywhere(&w);
    vfp_court_t *c = &W.ca;
    CHECK(vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 4, false));
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 4, true)); /* nothing pending */
    c->pending = true;
    c->pend_slot = 0;
    c->pend_bearer = IB.id;
    c->pend_units = 4;
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 5, true));
    CHECK(!vfp_letter(c, w.id, &IC.id, VF_RES_TRANSFER, 4, true));
    c->pend_bearer = IC.id;
    vfp_warrant_t o = warrant(9, NULL, 10, 50);
    everywhere(&o);
    CHECK(!vfp_letter(c, o.id, &IB.id, VF_RES_TRANSFER, 4, true)); /* bearer differs */
    c->pend_bearer = IB.id;
    c->pend_slot = 1;
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 4, true)); /* other slot */
    c->pend_slot = 0;
    CHECK(vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 4, true));
    CHECK(vfp_get(c, w.id)->drawn == 4);
    CHECK(vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 4, true));
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 4, true)); /* 12 > 10 */
    c->pend_units = 2;
    CHECK(vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 2, true));
    CHECK(vfp_get(c, w.id)->state == VFP_FULFILLED);
    c->pend_units = 1;
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 1, true)); /* fulfilled */
    CHECK(vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 1, false)); /* bearer view still fine */
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 11, false));
    CHECK(vfp_letter(c, w.id, &IB.id, VF_RES_TRANSFER, 10, false));
    CHECK(!vfp_letter(c, w.id, &IB.id, VF_RES_COMPUTE, 1, false));
    CHECK(!vfp_letter(c, w.id, &IC.id, VF_RES_TRANSFER, 1, false));
    CHECK(vfp_letter(c, o.id, &IC.id, VF_RES_TRANSFER, 1, false)); /* open */
    vna_id_t almost;
    memset(almost.b, 0xFF, 32);
    almost.b[17] = 0xFE;
    vfp_warrant_t a2 = warrant(10, &almost, 10, 50);
    everywhere(&a2);
    CHECK(!vfp_letter(c, a2.id, &IC.id, VF_RES_TRANSFER, 1, false)); /* not quite open */
    CHECK(vfp_letter(c, a2.id, &almost, VF_RES_TRANSFER, 1, false));
    c->pending = false;
}

int main(void)
{
    ident(&IA, 0x1A);
    ident(&IB, 0x2B);
    ident(&IC, 0x3C);
    ident(&ICOM, 0x4D);
    for (uint32_t i = 0; i < sizeof BLOCK; i++) BLOCK[i] = (uint8_t) (i * 13 + 5);
    vna_cid_raw(BLOCK, 9000, BLOCK_CID);
    t_commission();
    t_claim();
    t_cap_open();
    t_hook();
    printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
