/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* kycwrap.c — see kycwrap.h (D1-D7). Freestanding: no libc, no float, no
 * 64-bit division helper. No cryptography of its own: signatures go through
 * the caller's hooks (ML-DSA-87 via cpk.h), hashing is SHA-256. */
#include "kycwrap.h"
#include "../robin_debanks/sha256.h"

#define TOKEN_MAGIC   "ZXVK"
#define TOKEN_VER     1u
#define DOM_TOKEN     "zxv/kyc-md-token/v1"
#define DOM_TOKEN_N   19u
#define DOM_PRESENT   "zxv/kyc-md-present/v1"
#define DOM_PRESENT_N 21u
#define DOM_TOKID     "zxv/kyc-md-tokid/v1"
#define DOM_TOKID_N   19u
#define SECS_PER_DAY  86400u

/* Token layout (D1). */
#define O_VER           4u
#define O_KIND          5u
#define O_AGE           6u
#define O_CC            7u
#define O_ICC           9u
#define O_ISSUER        11u
#define O_TOKID         (O_ISSUER + KYC_ID_LEN)
#define O_EXPIRY        (O_TOKID + KYC_ID_LEN)
#define O_HOLDER        (O_EXPIRY + 4u)
#define O_ISIG          KYC_MD_BODY_LEN
#define PRESENT_MSG_LEN (DOM_PRESENT_N + 32u + KYC_ID_LEN + 32u + 4u)
#define TOKEN_MSG_LEN   (DOM_TOKEN_N + 32u)

bool kyc_pred_ok(const kyc_pred_t *p)
{
    if (!p) return false;
    switch (p->kind) {
    case KYC_PRED_AGE_OVER:
        return p->age >= 1 && p->age <= 150 && p->country[0] == 0 && p->country[1] == 0;
    case KYC_PRED_KYCD:
        return p->age == 0 && p->country[0] == 0 && p->country[1] == 0;
    case KYC_PRED_RESIDENT:
        return p->age == 0 && cp_cc_ok(p->country, false);
    }
    return false;
}

static bool pred_eq(const kyc_pred_t *a, const kyc_pred_t *b)
{
    return a->kind == b->kind && a->age == b->age && a->country[0] == b->country[0] &&
           a->country[1] == b->country[1];
}

static uint32_t day_of(uint64_t now)
{
    uint64_t d = cp_udivmod64(now, SECS_PER_DAY, 0);
    return d > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t) d;
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) |
           ((uint32_t) p[3] << 24);
}

/* ---- ZK placeholder -------------------------------------------------- */
static kyc_status_t zk_verify(void *ctx, const kyc_request_t *req, const uint8_t *proof, size_t len,
                              uint64_t now, kyc_result_t *res)
{
    (void) ctx, (void) req, (void) proof, (void) len, (void) now, (void) res;
    return KYC_E_NOT_AVAILABLE;
}

void kyc_zk_backend(kyc_backend_t *b)
{
    b->name = "zk: not available (needs a vetted, audited ZK library)";
    b->zero_knowledge = true;
    b->verify = zk_verify;
    b->ctx = 0;
}

/* ---- verifier front (D7) -------------------------------------------- */
void kyc_verifier_init(kyc_verifier_t *v, const uint8_t verifier_id[KYC_ID_LEN],
                       const kyc_backend_t *backend)
{
    cp_zero(v, sizeof *v);
    v->enabled = false;
    if (verifier_id) cp_copy(v->verifier_id, verifier_id, KYC_ID_LEN);
    v->backend = backend;
}

void kyc_verifier_enable(kyc_verifier_t *v, bool on)
{
    v->enabled = on;
}

static bool live(const kyc_pending_t *p, uint64_t now)
{
    return p->used && now >= p->req.issued_at && now - p->req.issued_at <= KYC_CHALLENGE_TTL;
}

kyc_status_t kyc_request_new(kyc_verifier_t *v, const kyc_pred_t *pred, const uint8_t rnd[32],
                             uint64_t now, kyc_request_t *out)
{
    if (!v || !pred || !rnd || !out) return KYC_E_ARG;
    if (!v->enabled) return KYC_E_DISABLED;
    if (!kyc_pred_ok(pred)) return KYC_E_ARG;
    for (uint32_t i = 0; i < KYC_MAX_PENDING; i++) {
        kyc_pending_t *p = &v->pending[i];
        if (live(p, now)) continue;
        cp_zero(&p->req, sizeof p->req);
        p->req.pred = *pred;
        cp_copy(p->req.verifier_id, v->verifier_id, KYC_ID_LEN);
        cp_copy(p->req.challenge, rnd, 32);
        p->req.issued_at = now;
        p->used = true;
        cp_copy(out, &p->req, sizeof *out);
        return KYC_OK;
    }
    return KYC_E_FULL;
}

kyc_status_t kyc_verify(kyc_verifier_t *v, const kyc_request_t *req, const uint8_t *proof,
                        size_t len, uint64_t now, kyc_result_t *res)
{
    if (res) cp_zero(res, sizeof *res);
    if (!v || !req || !proof || !res) return KYC_E_ARG;
    if (!v->enabled) return KYC_E_DISABLED;
    if (!v->backend || !v->backend->verify) return KYC_E_NOT_AVAILABLE;
    kyc_pending_t *hit = 0;
    for (uint32_t i = 0; i < KYC_MAX_PENDING && !hit; i++) {
        kyc_pending_t *p = &v->pending[i];
        if (p->used && cp_eq(p->req.challenge, req->challenge, 32) &&
            cp_eq(p->req.verifier_id, req->verifier_id, KYC_ID_LEN) &&
            pred_eq(&p->req.pred, &req->pred) && p->req.issued_at == req->issued_at)
            hit = p;
    }
    if (!hit) return KYC_E_CHALLENGE;
    kyc_request_t r;
    cp_copy(&r, &hit->req, sizeof r);
    bool ok = live(hit, now);
    hit->used = false; /* consumed whatever happens next */
    if (!ok) return KYC_E_CHALLENGE;
    return v->backend->verify(v->backend->ctx, &r, proof, len, now, res);
}

/* ---- minimal-disclosure backend ------------------------------------- */
void kyc_md_token_id(const uint8_t batch_seed[32], uint32_t index, uint8_t out[KYC_ID_LEN])
{
    uint8_t ix[4] = {(uint8_t) index, (uint8_t) (index >> 8), (uint8_t) (index >> 16),
                     (uint8_t) (index >> 24)};
    sha256_ctx_t c;
    sha256_init(&c);
    sha256_update(&c, (const uint8_t *) DOM_TOKID, DOM_TOKID_N);
    sha256_update(&c, batch_seed, 32);
    sha256_update(&c, ix, 4);
    sha256_final(&c, out);
}

static void token_msg(const uint8_t *token, uint8_t out[TOKEN_MSG_LEN])
{
    cp_copy(out, DOM_TOKEN, DOM_TOKEN_N);
    sha256(token, KYC_MD_BODY_LEN, out + DOM_TOKEN_N);
}

static void present_msg(const uint8_t *token, const kyc_request_t *req,
                        uint8_t out[PRESENT_MSG_LEN])
{
    uint8_t *p = out;
    cp_copy(p, DOM_PRESENT, DOM_PRESENT_N);
    p += DOM_PRESENT_N;
    sha256(token, KYC_MD_TOKEN_LEN, p);
    p += 32;
    cp_copy(p, req->verifier_id, KYC_ID_LEN);
    p += KYC_ID_LEN;
    cp_copy(p, req->challenge, 32);
    p += 32;
    p[0] = req->pred.kind;
    p[1] = req->pred.age;
    p[2] = (uint8_t) req->pred.country[0];
    p[3] = (uint8_t) req->pred.country[1];
}

/* Format checks on a token; fills pred. */
static bool token_parse(const uint8_t *t, kyc_pred_t *pred)
{
    if (!cp_eq(t, TOKEN_MAGIC, 4) || t[O_VER] != TOKEN_VER) return false;
    pred->kind = t[O_KIND];
    pred->age = t[O_AGE];
    pred->country[0] = (char) t[O_CC];
    pred->country[1] = (char) t[O_CC + 1];
    if (!kyc_pred_ok(pred) || !cp_cc_ok((const char *) t + O_ICC, false)) return false;
    return rd32(t + O_EXPIRY) % KYC_MD_EXPIRY_QUANTUM == 0;
}

kyc_status_t kyc_md_issue(const kyc_md_issuer_t *is, const uint8_t batch_seed[32], uint32_t index,
                          const kyc_pred_t *pred, uint32_t expiry_day,
                          const uint8_t holder_pk[KYC_PK_LEN], uint8_t out[KYC_MD_TOKEN_LEN])
{
    if (!is || !is->sign || !batch_seed || !pred || !holder_pk || !out) return KYC_E_ARG;
    if (!kyc_pred_ok(pred) || !cp_cc_ok(is->issuer_country, false)) return KYC_E_ARG;
    if (index >= KYC_MD_BATCH_MAX || expiry_day == 0 || expiry_day % KYC_MD_EXPIRY_QUANTUM)
        return KYC_E_ARG;
    cp_copy(out, TOKEN_MAGIC, 4);
    out[O_VER] = TOKEN_VER;
    out[O_KIND] = pred->kind;
    out[O_AGE] = pred->age;
    out[O_CC] = (uint8_t) pred->country[0];
    out[O_CC + 1] = (uint8_t) pred->country[1];
    out[O_ICC] = (uint8_t) is->issuer_country[0];
    out[O_ICC + 1] = (uint8_t) is->issuer_country[1];
    cp_copy(out + O_ISSUER, is->issuer_id, KYC_ID_LEN);
    kyc_md_token_id(batch_seed, index, out + O_TOKID);
    for (unsigned i = 0; i < 4; i++) out[O_EXPIRY + i] = (uint8_t) (expiry_day >> (8 * i));
    cp_copy(out + O_HOLDER, holder_pk, KYC_PK_LEN);
    uint8_t msg[TOKEN_MSG_LEN];
    token_msg(out, msg);
    is->sign(is->sctx, msg, sizeof msg, out + O_ISIG);
    return KYC_OK;
}

kyc_status_t kyc_md_issue_batch(const kyc_md_issuer_t *is, const uint8_t batch_seed[32],
                                const kyc_pred_t *pred, uint32_t expiry_day,
                                const uint8_t *holder_pks, uint32_t n, uint8_t *out)
{
    if (!holder_pks || !out || n == 0 || n > KYC_MD_BATCH_MAX) return KYC_E_ARG;
    for (uint32_t i = 0; i < n; i++) {
        /* Two tokens with one holder key would be linkable: refuse. */
        for (uint32_t j = 0; j < i; j++)
            if (cp_eq(holder_pks + (size_t) i * KYC_PK_LEN, holder_pks + (size_t) j * KYC_PK_LEN,
                      KYC_PK_LEN))
                return KYC_E_ARG;
        kyc_status_t st =
            kyc_md_issue(is, batch_seed, i, pred, expiry_day, holder_pks + (size_t) i * KYC_PK_LEN,
                         out + (size_t) i * KYC_MD_TOKEN_LEN);
        if (st != KYC_OK) return st;
    }
    return KYC_OK;
}

kyc_status_t kyc_md_present(const uint8_t token[KYC_MD_TOKEN_LEN], const kyc_request_t *req,
                            kyc_sign_fn sign, void *sctx, uint8_t out[KYC_MD_PRESENT_LEN])
{
    if (!token || !req || !sign || !out) return KYC_E_ARG;
    kyc_pred_t tp;
    if (!token_parse(token, &tp)) return KYC_E_DECODE;
    if (!pred_eq(&tp, &req->pred)) return KYC_E_PREDICATE;
    cp_copy(out, token, KYC_MD_TOKEN_LEN);
    uint8_t msg[PRESENT_MSG_LEN];
    present_msg(token, req, msg);
    sign(sctx, msg, sizeof msg, out + KYC_MD_TOKEN_LEN);
    return KYC_OK;
}

kyc_status_t kyc_md_wallet_present(kyc_md_slot_t *slots, uint32_t n, const kyc_request_t *req,
                                   uint64_t now, uint8_t out[KYC_MD_PRESENT_LEN])
{
    if (!slots || !req || !out) return KYC_E_ARG;
    uint32_t today = day_of(now);
    for (uint32_t i = 0; i < n; i++) {
        kyc_md_slot_t *s = &slots[i];
        kyc_pred_t tp;
        if (s->used || !s->token || !token_parse(s->token, &tp) || !pred_eq(&tp, &req->pred))
            continue;
        if (today > rd32(s->token + O_EXPIRY)) continue;
        s->used = true; /* before presenting: never hand the same token out twice */
        return kyc_md_present(s->token, req, s->sign, s->sctx, out);
    }
    return KYC_E_FULL;
}

/* ---- revocation lists (D6) ------------------------------------------ */
size_t kyc_md_revlist_encode(const kyc_revlist_t *r, uint8_t *out, size_t cap)
{
    if (!r || !out || r->hdr.kind != CP_KIND_KYC_REVOKE || r->n > KYC_MD_REV_MAX) return 0;
    for (unsigned i = 0; i < r->n; i++)
        if (r->e[i].count == 0 || r->e[i].count > KYC_MD_BATCH_MAX) return 0;
    cp_w_t w = {out, cap, 0, false};
    cp_hdr_put(&w, &r->hdr);
    cp_put16(&w, r->n);
    for (unsigned i = 0; i < r->n; i++) {
        cp_put(&w, r->e[i].seed, 32);
        cp_put16(&w, r->e[i].count);
    }
    return w.bad ? 0 : w.len;
}

kyc_status_t kyc_md_revlist_decode(const uint8_t *m, size_t len, kyc_revlist_t *r)
{
    if (!m || !r) return KYC_E_ARG;
    cp_zero(r, sizeof *r);
    cp_r_t rd = {m, len, 0, false};
    if (!cp_hdr_get(&rd, &r->hdr, false) || r->hdr.kind != CP_KIND_KYC_REVOKE) return KYC_E_DECODE;
    for (unsigned i = 0; i < CP_REGION_LEN; i++)
        if (r->hdr.region[i]) return KYC_E_DECODE;
    r->n = cp_get16(&rd);
    if (r->n > KYC_MD_REV_MAX) return KYC_E_DECODE;
    for (unsigned i = 0; i < r->n; i++) {
        cp_get(&rd, r->e[i].seed, 32);
        r->e[i].count = cp_get16(&rd);
        if (r->e[i].count == 0 || r->e[i].count > KYC_MD_BATCH_MAX) return KYC_E_DECODE;
    }
    if (rd.bad || rd.off != len) return KYC_E_DECODE;
    return KYC_OK;
}

static kyc_revlist_t *find_rev(kyc_md_verifier_t *m, const uint8_t id[KYC_ID_LEN], const char cc[2])
{
    for (uint32_t i = 0; i < m->n_rev; i++)
        if (cp_eq(m->rev[i].hdr.key_id, id, KYC_ID_LEN) && m->rev[i].hdr.country[0] == cc[0] &&
            m->rev[i].hdr.country[1] == cc[1])
            return &m->rev[i];
    return 0;
}

kyc_status_t kyc_md_install_revlist(kyc_md_verifier_t *m, const uint8_t *list, size_t len,
                                    const uint8_t *sig, size_t sig_len, uint64_t now)
{
    if (!m || !list) return KYC_E_ARG;
    kyc_revlist_t r;
    kyc_status_t st = kyc_md_revlist_decode(list, len, &r);
    if (st != KYC_OK) return st;
    if (!cp_manifest_verify(m->issuer_verify, m->ictx, &r.hdr, list, len, sig, sig_len))
        return KYC_E_SIG;
    if (now < r.hdr.not_before || now > r.hdr.not_after) return KYC_E_WINDOW;
    kyc_revlist_t *old = find_rev(m, r.hdr.key_id, r.hdr.country);
    if (old) {
        if (r.hdr.version <= old->hdr.version) return KYC_E_VERSION;
        cp_copy(old, &r, sizeof r);
        return KYC_OK;
    }
    if (m->n_rev >= KYC_MD_MAX_ISSUERS) return KYC_E_FULL;
    cp_copy(&m->rev[m->n_rev++], &r, sizeof r);
    return KYC_OK;
}

static bool revoked(const kyc_revlist_t *r, const uint8_t id[KYC_ID_LEN])
{
    uint8_t t[KYC_ID_LEN];
    bool hit = false;
    for (unsigned i = 0; i < r->n; i++)
        for (uint32_t k = 0; k < r->e[i].count; k++) {
            kyc_md_token_id(r->e[i].seed, k, t);
            if (cp_eq(t, id, KYC_ID_LEN)) hit = true;
        }
    return hit;
}

void kyc_md_verifier_init(kyc_md_verifier_t *m, cp_verify_fn issuer_verify, void *ictx,
                          kyc_pk_verify_fn holder_verify, kyc_spent_t *spent, uint32_t cap)
{
    cp_zero(m, sizeof *m);
    m->issuer_verify = issuer_verify;
    m->ictx = ictx;
    m->holder_verify = holder_verify;
    m->require_revlist = true;
    m->spent = spent;
    m->spent_cap = spent ? cap : 0;
}

static kyc_status_t md_verify(void *ctx, const kyc_request_t *req, const uint8_t *p, size_t len,
                              uint64_t now, kyc_result_t *res)
{
    kyc_md_verifier_t *m = (kyc_md_verifier_t *) ctx;
    if (!m || !req || !p || !res) return KYC_E_ARG;
    if (len != KYC_MD_PRESENT_LEN) return KYC_E_DECODE;
    kyc_pred_t tp;
    if (!token_parse(p, &tp)) return KYC_E_DECODE;
    if (!pred_eq(&tp, &req->pred)) return KYC_E_PREDICATE;

    cp_hdr_t h;
    cp_zero(&h, sizeof h);
    h.kind = CP_KIND_KYC_TOKEN;
    h.country[0] = (char) p[O_ICC];
    h.country[1] = (char) p[O_ICC + 1];
    cp_copy(h.key_id, p + O_ISSUER, KYC_ID_LEN);
    uint8_t tmsg[TOKEN_MSG_LEN];
    token_msg(p, tmsg);
    if (!m->issuer_verify ||
        !m->issuer_verify(m->ictx, &h, tmsg, sizeof tmsg, p + O_ISIG, KYC_SIG_LEN))
        return KYC_E_ISSUER;

    uint32_t today = day_of(now), expiry = rd32(p + O_EXPIRY);
    if (today > expiry) return KYC_E_EXPIRED;

    const uint8_t *tid = p + O_TOKID;
    kyc_revlist_t *rl = find_rev(m, h.key_id, h.country);
    if (rl && (now < rl->hdr.not_before || now > rl->hdr.not_after)) rl = 0;
    if (!rl && m->require_revlist) return KYC_E_REVLIST;
    if (rl && revoked(rl, tid)) return KYC_E_REVOKED;

    for (uint32_t i = 0; i < m->n_spent; i++)
        if (cp_eq(m->spent[i].token_id, tid, KYC_ID_LEN)) return KYC_E_SPENT;
    if (m->n_spent >= m->spent_cap) {
        uint32_t w = 0;
        for (uint32_t i = 0; i < m->n_spent; i++)
            if (m->spent[i].expiry_day >= today) {
                if (w != i) cp_copy(&m->spent[w], &m->spent[i], sizeof m->spent[i]);
                w++;
            }
        m->n_spent = w;
        if (m->n_spent >= m->spent_cap) return KYC_E_SPENT_FULL;
    }

    uint8_t pmsg[PRESENT_MSG_LEN];
    present_msg(p, req, pmsg);
    if (!m->holder_verify || !m->holder_verify(p + O_HOLDER, KYC_PK_LEN, pmsg, sizeof pmsg,
                                               p + KYC_MD_TOKEN_LEN, KYC_SIG_LEN))
        return KYC_E_HOLDER;

    cp_copy(m->spent[m->n_spent].token_id, tid, KYC_ID_LEN);
    m->spent[m->n_spent].expiry_day = expiry;
    m->n_spent++;
    res->pred = tp;
    res->issuer_country[0] = h.country[0];
    res->issuer_country[1] = h.country[1];
    cp_copy(res->issuer_id, h.key_id, KYC_ID_LEN);
    cp_copy(res->token_id, tid, KYC_ID_LEN);
    res->expiry_day = expiry;
    return KYC_OK;
}

void kyc_md_backend(kyc_backend_t *b, kyc_md_verifier_t *m)
{
    b->name = "minimal-disclosure (NOT zero-knowledge; issuer can link)";
    b->zero_knowledge = false;
    b->verify = md_verify;
    b->ctx = m;
}

const char *kyc_status_name(kyc_status_t s)
{
    switch (s) {
    case KYC_OK:
        return "ok";
    case KYC_E_ARG:
        return "bad_arg";
    case KYC_E_DISABLED:
        return "disabled";
    case KYC_E_NOT_AVAILABLE:
        return "not_available";
    case KYC_E_FULL:
        return "full";
    case KYC_E_CHALLENGE:
        return "challenge";
    case KYC_E_DECODE:
        return "decode";
    case KYC_E_PREDICATE:
        return "predicate";
    case KYC_E_ISSUER:
        return "issuer";
    case KYC_E_EXPIRED:
        return "expired";
    case KYC_E_REVLIST:
        return "no_revocation_list";
    case KYC_E_REVOKED:
        return "revoked";
    case KYC_E_SPENT:
        return "spent";
    case KYC_E_SPENT_FULL:
        return "spent_set_full";
    case KYC_E_HOLDER:
        return "holder_signature";
    case KYC_E_SIG:
        return "signature";
    case KYC_E_WINDOW:
        return "window";
    case KYC_E_VERSION:
        return "version";
    }
    return "unknown";
}
