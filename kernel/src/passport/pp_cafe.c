/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pp_cafe.c -- café mode: a short-lived session key on an untrusted
 * machine, approved from the user's phone. See passport.h.
 *
 * Wire formats (big-endian):
 *   request   "ZPQ1" 1 0 0 0 | rights u32 | cap u64 | want_ms u64
 *             | session ML-DSA-65 key (1952) | KEM key length u32
 *             | pq_matrix STANDARD KEM public key
 *             fingerprint = SHA3-256(the whole request)
 *   grant     "ZPG1" 1 0 0 0 | root DID | device id | session id
 *             | session key hash | request fingerprint | rights u32 | cap u64
 *             | issued u64 | expires u64 | lamport u64        (172 bytes)
 *             grant id = first 16 bytes of SHA3-256(those bytes);
 *             ML-DSA-65 by the approving passkey, context PP_CTX_GRANT
 *   envelope  "ZPE1" 1 0 0 0 | grant id | KEM ciphertext length u32
 *             | plaintext length u32 | KEM ciphertext
 *             | ChaCha20-Poly1305(report key || view) | tag
 *             key = SHAKE256("zxv.passport.envelope.v1" || ss || grant id),
 *             nonce 0 (a fresh KEM secret per envelope), AAD = header and
 *             KEM ciphertext
 *   report    "ZPR1" 1 0 0 0 | grant id | report sequence u64 | count u32
 *             | spends (96 each) | tag
 *             key = the report key, nonce = 0 0 0 0 || sequence, AAD = header
 */
#include "pp_int.h"
#include "../mlkem/keccak.h"
#include "../tls/aead.h"

#define REQ_FIXED   1984u
#define GRANT_BODY  172u
#define MAGIC_REQ   "ZPQ1"
#define MAGIC_GRANT "ZPG1"
#define MAGIC_ENV   "ZPE1"
#define MAGIC_REP   "ZPR1"
#define VERSION     1u

/* Large pq_matrix objects stay off the stack (single-threaded, as ident). */
static pqm_kem_pk_t s_kpk;
static pqm_kem_ct_t s_kct;

static void put_magic(uint8_t *out, const char *m)
{
    pp_mcpy(out, m, 4);
    out[4] = VERSION;
    out[5] = out[6] = out[7] = 0;
}

static bool magic_ok(const uint8_t *in, const char *m)
{
    return pp_mcmp(in, m, 4) == 0 && in[4] == VERSION && !in[5] && !in[6] && !in[7];
}

static bool rights_ok(uint32_t r)
{
    return r != 0 && (r & ~PP_RIGHTS_ALL) == 0;
}

uint32_t pp_compare_code(const uint8_t fp[PP_HASH])
{
    if (!fp) return 0;
    return pp_get32(fp) % PP_COMPARE_MOD;
}

/* ---- café: the request ---- */
pp_status_t pp_cafe_begin(pp_cafe_t *c, const uint8_t rnd[PP_SEED], uint32_t rights, uint64_t cap,
                          uint64_t want_ms, uint8_t *req, uint32_t cap_bytes, uint32_t *req_len)
{
    uint8_t ks[PP_SEED], kk[PP_SEED], h[PP_HASH];
    if (!c || !rnd || !req || !req_len || !rights_ok(rights) || want_ms == 0) return PP_ERR_ARG;
    uint32_t kem_len = (uint32_t) pqm_encoded_size(PQM_OBJ_KEM_PK, PQM_LEVEL_STANDARD);
    if (cap_bytes < REQ_FIXED + kem_len) return PP_ERR_SIZE;
    pp_wipe_mem(c, sizeof *c);
    pp_shake(ks, PP_SEED, "zxv.passport.cafe.sig.v1", rnd, PP_SEED, 0, 0, 0, 0);
    pp_shake(kk, PP_SEED, "zxv.passport.cafe.kem.v1", rnd, PP_SEED, 0, 0, 0, 0);
    pq_mldsa65_keygen(ks, c->sig_pk, c->sig_sk);
    bool ok = pqm_kem_keygen(PQM_LEVEL_STANDARD, kk, &s_kpk, &c->kem_sk);
    pp_wipe_mem(ks, sizeof ks);
    pp_wipe_mem(kk, sizeof kk);
    if (ok) ok = pqm_kem_pk_encode(&s_kpk, req + REQ_FIXED, kem_len) == kem_len;
    if (!ok) {
        pp_cafe_end(c);
        return PP_ERR_CRYPTO;
    }
    put_magic(req, MAGIC_REQ);
    pp_put32(req + 8, rights);
    pp_put64(req + 12, cap);
    pp_put64(req + 20, want_ms);
    pp_mcpy(req + 28, c->sig_pk, ID_MLDSA65_PK);
    pp_put32(req + 1980, kem_len);
    sha3_256(c->sig_pk, ID_MLDSA65_PK, h);
    pp_mcpy(c->id, h, PP_ID);
    *req_len = REQ_FIXED + kem_len;
    sha3_256(req, *req_len, c->fp);
    c->live = true;
    return PP_OK;
}

/* ---- phone: read the request, approve it ---- */
pp_status_t pp_request_parse(const uint8_t *req, uint32_t len, const uint8_t qr_fp[PP_HASH],
                             pp_request_t *out)
{
    uint8_t fp[PP_HASH];
    if (!req || !qr_fp || !out) return PP_ERR_ARG;
    pp_mset(out, 0, sizeof *out);
    if (len < REQ_FIXED || !magic_ok(req, MAGIC_REQ)) return PP_ERR_FORMAT;
    sha3_256(req, len, fp);
    if (!pp_ct_eq(fp, qr_fp, PP_HASH)) return PP_ERR_MISMATCH;
    uint32_t rights = pp_get32(req + 8), kem_len = pp_get32(req + 1980);
    uint64_t want = pp_get64(req + 20);
    if (!rights_ok(rights) || want == 0 || kem_len != len - REQ_FIXED) return PP_ERR_FORMAT;
    if (!pqm_kem_pk_decode(&s_kpk, req + REQ_FIXED, kem_len) || s_kpk.level != PQM_LEVEL_STANDARD)
        return PP_ERR_FORMAT;
    out->rights = rights;
    out->cap = pp_get64(req + 12);
    out->want_ms = want;
    sha3_256(req + 28, ID_MLDSA65_PK, out->session_pk_hash);
    pp_mcpy(out->session, out->session_pk_hash, PP_ID);
    pp_mcpy(out->fp, fp, PP_HASH);
    out->kem_pk = req + REQ_FIXED;
    out->kem_pk_len = kem_len;
    return PP_OK;
}

static void grant_body(const pp_grant_t *g, uint8_t b[GRANT_BODY])
{
    put_magic(b, MAGIC_GRANT);
    pp_mcpy(b + 8, g->root_did, PP_HASH);
    pp_mcpy(b + 40, g->device, PP_ID);
    pp_mcpy(b + 56, g->session, PP_ID);
    pp_mcpy(b + 72, g->session_pk_hash, PP_HASH);
    pp_mcpy(b + 104, g->request_fp, PP_HASH);
    pp_put32(b + 136, g->rights);
    pp_put64(b + 140, g->cap);
    pp_put64(b + 148, g->issued_ms);
    pp_put64(b + 156, g->expires_ms);
    pp_put64(b + 164, g->lamport);
}

static void grant_id(const uint8_t b[GRANT_BODY], uint8_t id[PP_ID])
{
    uint8_t h[PP_HASH];
    sha3_256(b, GRANT_BODY, h);
    pp_mcpy(id, h, PP_ID);
}

static void grant_rec(const pp_grant_t *g, pp_grant_rec_t *r)
{
    pp_mcpy(r->grant, g->grant, PP_ID);
    pp_mcpy(r->session, g->session, PP_ID);
    pp_mcpy(r->device, g->device, PP_ID);
    r->rights = g->rights;
    r->cap = g->cap;
    r->issued_ms = g->issued_ms;
    r->expires_ms = g->expires_ms;
}

pp_status_t pp_grant_issue(const pp_passport_t *p, const id_passkey_t *device,
                           const pp_request_t *req, uint32_t rights, uint64_t cap, uint64_t now_ms,
                           const uint8_t *rnd, pp_state_t *st, pp_grant_t *out)
{
    uint8_t b[GRANT_BODY];
    if (!p || !device || !req || !st || !out) return PP_ERR_ARG;
    if (!rights_ok(rights) || (rights & ~req->rights) != 0) return PP_ERR_RIGHTS;
    if (cap > req->cap) return PP_ERR_CAP;
    if ((rights & PP_RIGHT_SPEND) && cap == 0) return PP_ERR_CAP;
    if (!pp_ct_eq(p->root.did, st->root_did, PP_HASH)) return PP_ERR_MISMATCH;
    if (!pp_state_has_device(st, device->cred_id)) return PP_ERR_NOTFOUND;
    if (pp_state_is_revoked(st, device->cred_id)) return PP_ERR_REVOKED;
    uint64_t dur = req->want_ms < PP_CAFE_MAX_MS ? req->want_ms : PP_CAFE_MAX_MS;
    if (dur == 0) return PP_ERR_ARG;
    if (now_ms > UINT64_MAX - dur) return PP_ERR_OVERFLOW;
    if (st->n_grant >= st->s.grant_cap) return PP_ERR_FULL;
    pp_grant_t g;
    pp_mset(&g, 0, sizeof g);
    pp_mcpy(g.root_did, p->root.did, PP_HASH);
    pp_mcpy(g.device, device->cred_id, PP_ID);
    pp_mcpy(g.session, req->session, PP_ID);
    pp_mcpy(g.session_pk_hash, req->session_pk_hash, PP_HASH);
    pp_mcpy(g.request_fp, req->fp, PP_HASH);
    g.rights = rights;
    g.cap = cap;
    g.issued_ms = now_ms;
    g.expires_ms = now_ms + dur;
    for (uint32_t i = 0; i < st->n_spend; i++)
        if (st->s.spend[i].lamport > g.lamport) g.lamport = st->s.spend[i].lamport;
    grant_body(&g, b);
    grant_id(b, g.grant);
    pq_mldsa65_sign(device->sk, b, GRANT_BODY, (const uint8_t *) PP_CTX_GRANT,
                    sizeof PP_CTX_GRANT - 1u, rnd, g.sig);
    pp_grant_rec_t r;
    grant_rec(&g, &r);
    pp_status_t s = pp_state_add_grant(st, &r, 0);
    if (s != PP_OK) return s;
    pp_mcpy(out, &g, sizeof g);
    return PP_OK;
}

bool pp_grant_verify(const pp_grant_t *g, const uint8_t device_pk[ID_MLDSA65_PK])
{
    uint8_t b[GRANT_BODY], id[PP_ID];
    if (!g || !device_pk) return false;
    grant_body(g, b);
    grant_id(b, id);
    if (!pp_ct_eq(id, g->grant, PP_ID)) return false;
    return pq_mldsa65_verify(device_pk, b, GRANT_BODY, (const uint8_t *) PP_CTX_GRANT,
                             sizeof PP_CTX_GRANT - 1u, g->sig);
}

void pp_report_key(const pp_passport_t *p, const uint8_t grant[PP_ID], uint8_t out[PP_KEY])
{
    if (!p || !grant || !out) return;
    pp_shake(out, PP_KEY, "zxv.passport.cafe.report.v1", p->vault_key, PP_KEY, grant, PP_ID, 0, 0);
}

static void env_key(const uint8_t ss[PQM_SS_BYTES], const uint8_t grant[PP_ID], uint8_t k[PP_KEY])
{
    pp_shake(k, PP_KEY, "zxv.passport.envelope.v1", ss, PQM_SS_BYTES, grant, PP_ID, 0, 0);
}

pp_status_t pp_envelope_seal(const pp_passport_t *p, const pp_grant_t *g, const pp_request_t *req,
                             const uint8_t *view, uint32_t view_len, const uint8_t coins[PP_SEED],
                             uint8_t *out, uint32_t cap, uint32_t *len)
{
    uint8_t ss[PQM_SS_BYTES], k[PP_KEY], nonce[CHACHA20_NONCE_LEN];
    if (!p || !g || !req || !coins || !out || !len || (view_len && !view)) return PP_ERR_ARG;
    if (!pp_ct_eq(g->session, req->session, PP_ID) || !pp_ct_eq(g->request_fp, req->fp, PP_HASH) ||
        !pp_ct_eq(g->root_did, p->root.did, PP_HASH))
        return PP_ERR_MISMATCH;
    if (view_len && !(g->rights & PP_RIGHT_VIEW)) return PP_ERR_RIGHTS;
    uint32_t ct_len = (uint32_t) pqm_encoded_size(PQM_OBJ_KEM_CT, PQM_LEVEL_STANDARD);
    uint64_t need = (uint64_t) PP_ENVELOPE_HDR + ct_len + PP_KEY + view_len + PP_TAG;
    if (need > cap) return PP_ERR_SIZE;
    if (!pqm_kem_pk_decode(&s_kpk, req->kem_pk, req->kem_pk_len) ||
        s_kpk.level != PQM_LEVEL_STANDARD)
        return PP_ERR_FORMAT;
    if (!pqm_encaps(&s_kpk, coins, &s_kct, ss) ||
        pqm_kem_ct_encode(&s_kct, out + PP_ENVELOPE_HDR, ct_len) != ct_len) {
        pp_wipe_mem(ss, sizeof ss);
        return PP_ERR_CRYPTO;
    }
    uint32_t pt_len = PP_KEY + view_len;
    put_magic(out, MAGIC_ENV);
    pp_mcpy(out + 8, g->grant, PP_ID);
    pp_put32(out + 24, ct_len);
    pp_put32(out + 28, pt_len);
    uint8_t *pt = out + PP_ENVELOPE_HDR + ct_len;
    pp_report_key(p, g->grant, pt);
    if (view_len) pp_mcpy(pt + PP_KEY, view, view_len);
    env_key(ss, g->grant, k);
    pp_mset(nonce, 0, sizeof nonce);
    aead_seal(k, nonce, out, PP_ENVELOPE_HDR + ct_len, pt, pt, pt_len, pt + pt_len);
    pp_wipe_mem(ss, sizeof ss);
    pp_wipe_mem(k, sizeof k);
    *len = (uint32_t) need;
    return PP_OK;
}

/* ---- café: take the grant ---- */
pp_status_t pp_cafe_accept(pp_cafe_t *c, const pp_grant_t *g, const uint8_t *env, uint32_t len,
                           uint8_t *view, uint32_t view_cap, uint32_t *view_len)
{
    uint8_t ss[PQM_SS_BYTES], k[PP_KEY], nonce[CHACHA20_NONCE_LEN], h[PP_HASH];
    if (!c || !g || !env || !view || !view_len) return PP_ERR_ARG;
    if (!c->live || c->granted) return PP_ERR_STATE;
    sha3_256(c->sig_pk, ID_MLDSA65_PK, h);
    if (!pp_ct_eq(g->session, c->id, PP_ID) || !pp_ct_eq(g->session_pk_hash, h, PP_HASH) ||
        !pp_ct_eq(g->request_fp, c->fp, PP_HASH))
        return PP_ERR_MISMATCH;
    uint32_t ct_len = (uint32_t) pqm_encoded_size(PQM_OBJ_KEM_CT, PQM_LEVEL_STANDARD);
    if (len < PP_ENVELOPE_HDR || !magic_ok(env, MAGIC_ENV)) return PP_ERR_FORMAT;
    if (!pp_ct_eq(env + 8, g->grant, PP_ID)) return PP_ERR_MISMATCH;
    uint32_t pt_len = pp_get32(env + 28);
    if (pp_get32(env + 24) != ct_len || pt_len < PP_KEY ||
        (uint64_t) PP_ENVELOPE_HDR + ct_len + pt_len + PP_TAG != len)
        return PP_ERR_FORMAT;
    if (view_cap < pt_len) return PP_ERR_SIZE;
    if (!pqm_kem_ct_decode(&s_kct, env + PP_ENVELOPE_HDR, ct_len) ||
        !pqm_decaps(&c->kem_sk, &s_kct, ss))
        return PP_ERR_FORMAT;
    env_key(ss, g->grant, k);
    pp_wipe_mem(ss, sizeof ss);
    pp_mset(nonce, 0, sizeof nonce);
    const uint8_t *ct = env + PP_ENVELOPE_HDR + ct_len;
    bool ok = aead_open(k, nonce, env, PP_ENVELOPE_HDR + ct_len, ct, view, pt_len, ct + pt_len);
    pp_wipe_mem(k, sizeof k);
    if (!ok) return PP_ERR_AUTH;
    pp_mcpy(c->report_key, view, PP_KEY);
    for (uint32_t i = 0; i + PP_KEY < pt_len; i++) view[i] = view[i + PP_KEY];
    pp_wipe_mem(view + pt_len - PP_KEY, PP_KEY);
    *view_len = pt_len - PP_KEY;
    pp_mcpy(&c->grant, g, sizeof *g);
    c->granted = true;
    return PP_OK;
}

pp_status_t pp_cafe_spend(pp_cafe_t *c, uint64_t amount, const uint8_t payee[PP_HASH],
                          uint64_t now_ms, const uint8_t *rnd, pp_spend_t *out,
                          uint8_t sig[ID_MLDSA65_SIG])
{
    if (!c || !payee || !out || !sig || amount == 0) return PP_ERR_ARG;
    if (!c->live || !c->granted) return PP_ERR_STATE;
    const pp_grant_t *g = &c->grant;
    if (!(g->rights & PP_RIGHT_SPEND)) return PP_ERR_RIGHTS;
    if (now_ms < g->issued_ms || now_ms > g->expires_ms) return PP_ERR_EXPIRED;
    if (amount > g->cap - c->spent) return PP_ERR_CAP; /* spent never exceeds cap */
    pp_spend_t sp;
    pp_mset(&sp, 0, sizeof sp);
    pp_mcpy(sp.signer, c->id, PP_ID);
    sp.seq = c->seq + 1u;
    sp.lamport = g->lamport + sp.seq;
    sp.amount = amount;
    sp.t_ms = now_ms;
    pp_mcpy(sp.payee, payee, PP_HASH);
    pp_mcpy(sp.grant, g->grant, PP_ID);
    pp_voucher_sign(c->sig_sk, g->root_did, &sp, rnd, sig);
    c->seq = sp.seq;
    c->spent += amount;
    *out = sp;
    return PP_OK;
}

static void report_nonce(uint64_t seq, uint8_t nonce[CHACHA20_NONCE_LEN])
{
    pp_mset(nonce, 0, 4);
    pp_put64(nonce + 4, seq);
}

pp_status_t pp_cafe_report(pp_cafe_t *c, const pp_spend_t *spends, uint32_t n, uint8_t *out,
                           uint32_t cap, uint32_t *len)
{
    uint8_t nonce[CHACHA20_NONCE_LEN];
    if (!c || (n && !spends) || !out || !len) return PP_ERR_ARG;
    if (!c->live || !c->granted) return PP_ERR_STATE;
    uint64_t need = (uint64_t) PP_REPORT_HDR + (uint64_t) n * PP_SPEND_BYTES + PP_TAG;
    if (need > cap) return PP_ERR_SIZE;
    for (uint32_t i = 0; i < n; i++)
        if (!pp_ct_eq(spends[i].signer, c->id, PP_ID) ||
            !pp_ct_eq(spends[i].grant, c->grant.grant, PP_ID))
            return PP_ERR_MISMATCH;
    uint64_t seq = c->report_seq + 1u;
    put_magic(out, MAGIC_REP);
    pp_mcpy(out + 8, c->grant.grant, PP_ID);
    pp_put64(out + 24, seq);
    pp_put32(out + 32, n);
    for (uint32_t i = 0; i < n; i++)
        pp_spend_encode(&spends[i], out + PP_REPORT_HDR + i * PP_SPEND_BYTES);
    uint32_t pl = n * PP_SPEND_BYTES;
    report_nonce(seq, nonce);
    aead_seal(c->report_key, nonce, out, PP_REPORT_HDR, out + PP_REPORT_HDR, out + PP_REPORT_HDR,
              pl, out + PP_REPORT_HDR + pl);
    c->report_seq = seq;
    *len = (uint32_t) need;
    return PP_OK;
}

pp_status_t pp_report_open(const pp_passport_t *p, const pp_grant_t *g, const uint8_t *in,
                           uint32_t len, uint8_t *work, uint32_t work_cap, pp_state_t *st,
                           uint32_t *added)
{
    uint8_t k[PP_KEY], nonce[CHACHA20_NONCE_LEN];
    if (added) *added = 0;
    if (!p || !g || !in || !work || !st) return PP_ERR_ARG;
    if (!pp_ct_eq(g->root_did, st->root_did, PP_HASH)) return PP_ERR_MISMATCH;
    if (len < PP_REPORT_HDR + PP_TAG || !magic_ok(in, MAGIC_REP)) return PP_ERR_FORMAT;
    if (!pp_ct_eq(in + 8, g->grant, PP_ID)) return PP_ERR_MISMATCH;
    uint32_t n = pp_get32(in + 32);
    if ((uint64_t) n * PP_SPEND_BYTES + PP_REPORT_HDR + PP_TAG != len) return PP_ERR_FORMAT;
    uint32_t pl = n * PP_SPEND_BYTES;
    if (work_cap < pl) return PP_ERR_SIZE;
    pp_report_key(p, g->grant, k);
    report_nonce(pp_get64(in + 24), nonce);
    bool ok = aead_open(k, nonce, in, PP_REPORT_HDR, in + PP_REPORT_HDR, work, pl,
                        in + PP_REPORT_HDR + pl);
    pp_wipe_mem(k, sizeof k);
    if (!ok) return PP_ERR_AUTH;

    /* check everything and count room before changing the state */
    pp_grant_rec_t r;
    grant_rec(g, &r);
    uint32_t new_sp = 0, new_g = pp_state_has_grant(st, &r) ? 0u : 1u;
    pp_status_t s = PP_OK;
    for (uint32_t i = 0; i < n && s == PP_OK; i++) {
        pp_spend_t sp;
        pp_spend_decode(work + i * PP_SPEND_BYTES, &sp);
        if (!pp_ct_eq(sp.signer, g->session, PP_ID) || !pp_ct_eq(sp.grant, g->grant, PP_ID))
            s = PP_ERR_MISMATCH;
        else if (sp.seq == 0 || sp.lamport == 0 || sp.amount == 0)
            s = PP_ERR_FORMAT;
        else if (!pp_state_has_spend(st, &sp))
            new_sp++;
    }
    if (s == PP_OK && ((uint64_t) st->n_spend + new_sp > st->s.spend_cap ||
                       (uint64_t) st->n_grant + new_g > st->s.grant_cap))
        s = PP_ERR_FULL;
    if (s == PP_OK && new_g) s = pp_state_add_grant(st, &r, 0);
    uint32_t got = 0;
    for (uint32_t i = 0; i < n && s == PP_OK; i++) {
        pp_spend_t sp;
        uint32_t a = 0;
        pp_spend_decode(work + i * PP_SPEND_BYTES, &sp);
        s = pp_state_add_spend(st, &sp, &a);
        got += a;
    }
    pp_wipe_mem(work, pl);
    if (s != PP_OK) return s;
    if (added) *added = got;
    return pp_state_replay(st);
}

void pp_cafe_end(pp_cafe_t *c)
{
    if (!c) return;
    pqm_kem_sk_wipe(&c->kem_sk);
    pp_wipe_mem(c, sizeof *c);
}

pp_status_t pp_verify_cafe_spend(const pqm_sig_pk_t *root_pk, const id_cred_status_t *status,
                                 const id_dev_cert_t *cert, const uint8_t device_pk[ID_MLDSA65_PK],
                                 const pp_grant_t *g, const uint8_t session_pk[ID_MLDSA65_PK],
                                 const pp_spend_t *sp, const uint8_t sig[ID_MLDSA65_SIG],
                                 uint64_t now_ms)
{
    uint8_t h[PP_HASH];
    if (!g || !session_pk || !sp || !sig) return PP_ERR_ARG;
    pp_status_t s = pp_device_chain(root_pk, status, cert, device_pk, g->device);
    if (s != PP_OK) return s;
    if (!pp_ct_eq(g->root_did, status->root_did, PP_HASH)) return PP_ERR_MISMATCH;
    if (!pp_grant_verify(g, device_pk)) return PP_ERR_AUTH;
    sha3_256(session_pk, ID_MLDSA65_PK, h);
    if (!pp_ct_eq(h, g->session_pk_hash, PP_HASH) || !pp_ct_eq(sp->signer, g->session, PP_ID) ||
        !pp_ct_eq(sp->grant, g->grant, PP_ID))
        return PP_ERR_MISMATCH;
    if (!(g->rights & PP_RIGHT_SPEND)) return PP_ERR_RIGHTS;
    if (now_ms < g->issued_ms || now_ms > g->expires_ms || sp->t_ms < g->issued_ms ||
        sp->t_ms > g->expires_ms)
        return PP_ERR_EXPIRED;
    if (sp->amount > g->cap) return PP_ERR_CAP;
    return pp_voucher_verify(session_pk, g->root_did, sp, sig) ? PP_OK : PP_ERR_AUTH;
}
