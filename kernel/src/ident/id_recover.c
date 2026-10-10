/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_recover.c -- the recovery vault, guardians, and the recovery flow.
 *
 * VAULT (all integers big-endian; at most ID_VAULT_MAX bytes)
 *   hdr   'Z' 'I' 'V' ver | m_kib u32 | t u32 | p u32 | fe_t | blocks | k | n
 *         | fe_salt[16] | share_salt[16] | nonce_a[12] | nonce_b[12]
 *   A     ChaCha20-Poly1305 under key_a, aad = hdr:
 *         helper (32 * blocks) || n x (guardian id[32] || label[32]) || tag
 *   C     n x commit_i = SHA3-256("ZXV-ident/share" || share_salt || x_i || y_i)
 *   B     ChaCha20-Poly1305 under key_b, aad = every byte before B:
 *         root seed[32] || level || root DID[32] || tag
 * with   nk    = Argon2id(normalised name, "ZXV-ident/lookup", 64 bytes)
 *        key_a = SHAKE256("ZXV-ident/vault-a" || nk[32..64])
 *        key_b = SHAKE256("ZXV-ident/vault-b" || nk[32..64] || fe_key || G)
 *        lookup = SHA3-256("ZXV-ident/lookup-id" || nk[0..32])
 * The vault id is the SHA2-256 digest of the vault: the digest of its raw
 * CIDv1, so the IPFS address and the id guardians know are the same.
 *
 * GUARDIANS hold (vault id, account, x, y) and the account's root public
 * key. A request starts a waiting period (24 h by default) and alerts the
 * guardian; the person behind the guardian device approves after it; any
 * of the owner's certified passkeys may cancel during it (at most
 * ID_GUARD_MAX_CANCEL times per window, so a thief holding an old phone
 * cannot block recovery forever). At most ID_GUARD_MAX_REQ requests per
 * 30-day window. Shares travel sealed to a fresh pq_matrix KEM key of the
 * requesting device.
 *
 * HONEST LIMITS: see ident.h. In short: guardians are the barrier that
 * matters; the requester-side failure counter only protects honest users
 * from wasting approvals; a fixed lookup salt is unavoidable.
 */
#include "id_internal.h"
#include "../tls/aead.h"
#include "../robin_debanks/sha256.h"

#define HDR_LEN     76u
#define GREF_LEN    (ID_HASH + ID_NAME_MAX)
#define SECB_LEN    65u
#define TAG_LEN     16u
#define LOOKUP_SALT "ZXV-ident/lookup" /* 16 bytes */

enum { REC_IDLE = 0, REC_BEGUN = 1, REC_REQUESTED = 2, REC_DONE = 3, REC_LOCKED = 4 };

static pqm_kem_ct_t g_ct;

typedef struct {
    uint32_t m_kib, t, p;
    uint8_t ver, fe_t, blocks, k, n;
    const uint8_t *fe_salt, *share_salt, *nonce_a, *nonce_b;
    const uint8_t *a;
    uint32_t a_len;
    const uint8_t *commits;
    const uint8_t *b;
    uint32_t total;
} vault_view_t;

static bool parse_vault(const uint8_t *v, uint32_t len, vault_view_t *o)
{
    id_r_t r;
    uint8_t magic[3];
    if (!v || len < HDR_LEN) return false;
    id_r_init(&r, v, len);
    id_r_bytes(&r, magic, 3);
    if (magic[0] != 'Z' || magic[1] != 'I' || magic[2] != 'V') return false;
    o->ver = id_r_u8(&r);
    o->m_kib = id_r_u32(&r);
    o->t = id_r_u32(&r);
    o->p = id_r_u32(&r);
    o->fe_t = id_r_u8(&r);
    o->blocks = id_r_u8(&r);
    o->k = id_r_u8(&r);
    o->n = id_r_u8(&r);
    if (r.err || o->ver != ID_VAULT_VER) return false;
    if (o->fe_t < ID_FE_T_MIN || o->fe_t > ID_FE_T_MAX) return false;
    if (o->blocks == 0 || o->blocks > ID_FE_MAX_BLOCKS) return false;
    if (o->k == 0 || o->n < o->k || o->n > ID_MAX_GUARDIANS) return false;
    o->fe_salt = v + 16;
    o->share_salt = v + 32;
    o->nonce_a = v + 48;
    o->nonce_b = v + 60;
    o->a = v + HDR_LEN;
    o->a_len = 32u * o->blocks + GREF_LEN * o->n;
    o->commits = o->a + o->a_len + TAG_LEN;
    o->b = o->commits + ID_HASH * o->n;
    o->total = HDR_LEN + o->a_len + TAG_LEN + ID_HASH * o->n + SECB_LEN + TAG_LEN;
    return o->total == len;
}

void id_vault_params_default(id_vault_params_t *vp)
{
    id_kdf_default(&vp->kdf);
    vp->fe_t = ID_FE_T_DEFAULT;
    vp->k = ID_DEFAULT_K;
    vp->n = ID_DEFAULT_N;
}

int32_t id_name_normalise(const char *name, char out[ID_NAME_MAX])
{
    uint32_t o = 0;
    bool sp = false;
    if (!name || !out) return -1;
    id_mset(out, 0, ID_NAME_MAX);
    for (uint32_t i = 0; i < 256u && name[i]; i++) {
        char c = name[i];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
            if (o) sp = true;
            continue;
        }
        if (sp) {
            if (o + 1u >= ID_NAME_MAX) return -1;
            out[o++] = ' ';
            sp = false;
        }
        if (c >= 'A' && c <= 'Z') c = (char) (c + 32);
        if (o + 1u >= ID_NAME_MAX) return -1;
        out[o++] = c;
    }
    if (o < ID_NAME_MIN) {
        id_mset(out, 0, ID_NAME_MAX);
        return -1;
    }
    return (int32_t) o;
}

id_status_t id_name_keys(const char *name, const id_kdf_params_t *kp, uint64_t *mem,
                         uint32_t mem_blocks, uint8_t nk[64], uint8_t lookup[ID_HASH])
{
    char norm[ID_NAME_MAX];
    int32_t n = id_name_normalise(name, norm);
    if (n < 0) return ID_ERR_NAME;
    if (!kp || !nk || !lookup) return ID_ERR_ARG;
    id_status_t s =
        id_argon2id(kp, (const uint8_t *) norm, (uint32_t) n, (const uint8_t *) LOOKUP_SALT, 16, 0,
                    0, 0, 0, mem, mem_blocks, nk, 64);
    id_wipe(norm, sizeof norm);
    if (s != ID_OK) return s;
    id_sha3(lookup, "ZXV-ident/lookup-id", nk, 32, 0, 0, 0, 0);
    return ID_OK;
}

static void share_commit(const uint8_t salt[ID_SALT], uint8_t x, const uint8_t y[ID_SHARE_LEN],
                         uint8_t out[ID_HASH])
{
    id_sha3(out, "ZXV-ident/share", salt, ID_SALT, &x, 1, y, ID_SHARE_LEN);
}

static void key_b(const uint8_t nk[64], const uint8_t fe_key[32], const uint8_t g[32],
                  uint8_t out[32])
{
    id_shake(out, 32, "ZXV-ident/vault-b", nk + 32, 32, fe_key, 32, g, 32);
}

id_status_t id_recovery_enrol(const id_host_t *h, const id_root_t *root, const char *name,
                              const uint8_t *features, uint32_t nbits, const id_vault_params_t *vp,
                              const id_guardian_ref_t *guardians, uint64_t *mem,
                              uint32_t mem_blocks, uint8_t *vault, uint32_t vault_cap,
                              uint32_t *vault_len, ipfsn_cid_t *cid, id_guardian_share_t *shares)
{
    uint8_t nk[64], lookup[ID_HASH], ka[32], kb[32], fek[32], g[32];
    uint8_t ys[ID_MAX_GUARDIANS][ID_SHARE_LEN];
    uint8_t secb[SECB_LEN];
    id_status_t s;
    if (!h || !h->random || !root || !features || !vp || !guardians || !vault || !vault_len ||
        !cid || !shares)
        return ID_ERR_ARG;
    if (vp->k < 1 || vp->n < vp->k || vp->n > ID_MAX_GUARDIANS) return ID_ERR_ARG;
    if (vp->fe_t < ID_FE_T_MIN || vp->fe_t > ID_FE_T_MAX) return ID_ERR_ARG;
    uint32_t blocks = nbits / ID_BCH_N;
    if (blocks == 0 || blocks > ID_FE_MAX_BLOCKS || blocks * ID_BCH_N != nbits) return ID_ERR_ARG;
    uint32_t a_len = 32u * blocks + GREF_LEN * vp->n;
    uint32_t total = HDR_LEN + a_len + TAG_LEN + ID_HASH * vp->n + SECB_LEN + TAG_LEN;
    if (total > vault_cap || total > ID_VAULT_MAX) return ID_ERR_SIZE;

    if ((s = id_name_keys(name, &vp->kdf, mem, mem_blocks, nk, lookup)) != ID_OK) return s;
    if (h->fetch) {
        uint32_t got = 0;
        if (h->fetch(h->ctx, lookup, 0, vault, vault_cap, &got) == 0) {
            id_wipe(nk, sizeof nk);
            return ID_ERR_NAME; /* someone already uses this recovery name */
        }
    }

    /* header */
    id_w_t w;
    id_w_init(&w, vault, vault_cap);
    id_w_bytes(&w, "ZIV", 3);
    id_w_u8(&w, ID_VAULT_VER);
    id_w_u32(&w, vp->kdf.m_kib);
    id_w_u32(&w, vp->kdf.t);
    id_w_u32(&w, vp->kdf.p);
    id_w_u8(&w, vp->fe_t);
    id_w_u8(&w, (uint8_t) blocks);
    id_w_u8(&w, vp->k);
    id_w_u8(&w, vp->n);
    uint8_t rnd[ID_SALT * 2u + 24u];
    h->random(h->ctx, rnd, sizeof rnd);
    id_w_bytes(&w, rnd, sizeof rnd); /* fe_salt, share_salt, nonce_a, nonce_b */
    const uint8_t *fe_salt = vault + 16, *share_salt = vault + 32;
    const uint8_t *nonce_a = vault + 48, *nonce_b = vault + 60;

    /* A: helper data + guardian list */
    uint8_t *a = vault + HDR_LEN;
    if ((s = id_fe_gen(vp->fe_t, features, nbits, h, fe_salt, a, fek)) != ID_OK) goto out;
    for (uint32_t i = 0; i < vp->n; i++) {
        id_mcpy(a + 32u * blocks + GREF_LEN * i, guardians[i].id, ID_HASH);
        id_mset(a + 32u * blocks + GREF_LEN * i + ID_HASH, 0, ID_NAME_MAX);
        id_mcpy(a + 32u * blocks + GREF_LEN * i + ID_HASH, guardians[i].label,
                id_strlen(guardians[i].label, ID_NAME_MAX - 1u));
    }
    id_shake(ka, 32, "ZXV-ident/vault-a", nk + 32, 32, 0, 0, 0, 0);
    aead_seal(ka, nonce_a, vault, HDR_LEN, a, a, a_len, a + a_len);

    /* guardian secret and its shares */
    h->random(h->ctx, g, 32);
    if ((s = id_shamir_split(g, vp->k, vp->n, h, ys)) != ID_OK) goto out;
    uint8_t *c = a + a_len + TAG_LEN;
    for (uint32_t i = 0; i < vp->n; i++)
        share_commit(share_salt, (uint8_t) (i + 1u), ys[i], c + ID_HASH * i);

    /* B: the root seed */
    uint8_t *b = c + ID_HASH * vp->n;
    id_mcpy(secb, root->seed, 32);
    secb[32] = (uint8_t) root->level;
    id_mcpy(secb + 33, root->did, ID_HASH);
    key_b(nk, fek, g, kb);
    aead_seal(kb, nonce_b, vault, (uint32_t) (b - vault), secb, b, SECB_LEN, b + SECB_LEN);
    *vault_len = total;

    if (ipfsn_cid_sha256(IPFSN_MC_RAW, vault, total, cid) != IPFSN_OK) {
        s = ID_ERR_CRYPTO;
        goto out;
    }
    for (uint32_t i = 0; i < vp->n; i++) {
        id_mset(&shares[i], 0, sizeof shares[i]);
        id_mcpy(shares[i].vault_id, cid->digest, ID_HASH);
        id_mcpy(shares[i].account, root->did, ID_HASH);
        shares[i].index = (uint8_t) (i + 1u);
        shares[i].k = vp->k;
        shares[i].n = vp->n;
        id_mcpy(shares[i].share, ys[i], ID_SHARE_LEN);
    }
    if (h->publish && h->publish(h->ctx, lookup, cid, vault, total) != 0)
        s = ID_ERR_STATE;
    else
        s = ID_OK;
out:
    id_wipe(nk, sizeof nk);
    id_wipe(ka, sizeof ka);
    id_wipe(kb, sizeof kb);
    id_wipe(fek, sizeof fek);
    id_wipe(g, sizeof g);
    id_wipe(ys, sizeof ys);
    id_wipe(secb, sizeof secb);
    return s;
}

/* ===== requester ===== */

/* Decrypt section A of a candidate into r (guardians, helper, params). */
static id_status_t open_a(id_recovery_t *r, const uint8_t *v, uint32_t len)
{
    vault_view_t vv;
    uint8_t ka[32], buf[ID_FE_HELPER_MAX + GREF_LEN * ID_MAX_GUARDIANS];
    if (!parse_vault(v, len, &vv)) return ID_ERR_FORMAT;
    id_shake(ka, 32, "ZXV-ident/vault-a", r->nk + 32, 32, 0, 0, 0, 0);
    bool ok = aead_open(ka, vv.nonce_a, v, HDR_LEN, vv.a, buf, vv.a_len, vv.a + vv.a_len);
    id_wipe(ka, sizeof ka);
    if (!ok) return ID_ERR_NOMATCH;
    r->fe_t = vv.fe_t;
    r->k = vv.k;
    r->n = vv.n;
    r->blocks = vv.blocks;
    id_mcpy(r->fe_salt, vv.fe_salt, ID_SALT);
    id_mcpy(r->helper, buf, 32u * vv.blocks);
    id_mset(r->guardians, 0, sizeof r->guardians);
    for (uint32_t i = 0; i < vv.n; i++) {
        id_mcpy(r->guardians[i].id, buf + 32u * vv.blocks + GREF_LEN * i, ID_HASH);
        id_mcpy(r->guardians[i].label, buf + 32u * vv.blocks + GREF_LEN * i + ID_HASH,
                ID_NAME_MAX - 1u);
    }
    id_wipe(buf, sizeof buf);
    return ID_OK;
}

static id_status_t rec_start(id_recovery_t *r, const id_host_t *h, const char *name,
                             const id_kdf_params_t *kp, uint64_t *mem, uint32_t mem_blocks)
{
    if (!r || !h || !h->random || !kp) return ID_ERR_ARG;
    id_mset(r, 0, sizeof *r);
    id_mcpy(&r->host, h, sizeof r->host);
    return id_name_keys(name, kp, mem, mem_blocks, r->nk, r->lookup);
}

static id_status_t add_cand(id_recovery_t *r, const uint8_t *v, uint32_t len)
{
    if (len > ID_VAULT_MAX || r->ncand >= ID_REC_MAX_CAND) return ID_ERR_FULL;
    if (open_a(r, v, len) != ID_OK) return ID_ERR_NOMATCH;
    id_mcpy(r->cand[r->ncand].blob, v, len);
    r->cand[r->ncand].len = len;
    r->ncand++;
    return ID_OK;
}

id_status_t id_rec_begin(id_recovery_t *r, const id_host_t *h, const char *name,
                         const id_kdf_params_t *kp, uint64_t *mem, uint32_t mem_blocks,
                         uint8_t *ncand)
{
    static uint8_t buf[ID_VAULT_MAX];
    id_status_t s = rec_start(r, h, name, kp, mem, mem_blocks);
    if (s != ID_OK) return s;
    if (!h->fetch) return ID_ERR_ARG;
    for (uint32_t i = 0; i < 16u && r->ncand < ID_REC_MAX_CAND; i++) {
        uint32_t len = 0;
        if (h->fetch(h->ctx, r->lookup, i, buf, sizeof buf, &len) != 0) break;
        add_cand(r, buf, len);
    }
    if (ncand) *ncand = r->ncand;
    if (r->ncand == 0) return ID_ERR_NOTFOUND;
    r->state = REC_BEGUN;
    return ID_OK;
}

id_status_t id_rec_begin_with(id_recovery_t *r, const id_host_t *h, const char *name,
                              const id_kdf_params_t *kp, uint64_t *mem, uint32_t mem_blocks,
                              const uint8_t *vault, uint32_t len)
{
    id_status_t s = rec_start(r, h, name, kp, mem, mem_blocks);
    if (s != ID_OK) return s;
    if (!vault) return ID_ERR_ARG;
    if ((s = add_cand(r, vault, len)) != ID_OK) return s;
    r->state = REC_BEGUN;
    return ID_OK;
}

const id_guardian_ref_t *id_rec_guardians(id_recovery_t *r, uint32_t cand, uint8_t *n)
{
    if (!r || cand >= r->ncand) return 0;
    if (open_a(r, r->cand[cand].blob, r->cand[cand].len) != ID_OK) return 0;
    if (n) *n = r->n;
    return r->guardians;
}

id_status_t id_rec_request(id_recovery_t *r, uint32_t cand, pqm_level_t kem_level, uint64_t now_ms,
                           id_rec_request_t *req)
{
    uint8_t seed[32];
    id_status_t s;
    if (!r || !req || cand >= r->ncand) return ID_ERR_ARG;
    if (r->state != REC_BEGUN && r->state != REC_REQUESTED) return ID_ERR_STATE;
    if ((s = open_a(r, r->cand[cand].blob, r->cand[cand].len)) != ID_OK) return s;
    r->chosen = (uint8_t) cand;
    sha256(r->cand[cand].blob, r->cand[cand].len, r->vault_id);
    r->host.random(r->host.ctx, r->req_id, ID_REQ_ID);
    r->host.random(r->host.ctx, seed, 32);
    bool ok = pqm_kem_keygen(kem_level, seed, &r->kem_pk, &r->kem_sk);
    id_wipe(seed, sizeof seed);
    if (!ok) return ID_ERR_CRYPTO;
    r->kem_level = kem_level;
    r->started_ms = now_ms;
    r->nshares = 0;
    id_wipe(r->ys, sizeof r->ys);
    id_mset(req, 0, sizeof *req);
    id_mcpy(req->vault_id, r->vault_id, ID_HASH);
    id_mcpy(req->req_id, r->req_id, ID_REQ_ID);
    req->requested_ms = now_ms;
    size_t pl = pqm_kem_pk_encode(&r->kem_pk, req->kem_pk, sizeof req->kem_pk);
    if (pl == 0) return ID_ERR_CRYPTO;
    req->kem_pk_len = (uint32_t) pl;
    r->state = REC_REQUESTED;
    return ID_OK;
}

static void seal_key(const uint8_t ss[PQM_SS_BYTES], const uint8_t vault_id[ID_HASH],
                     const uint8_t req_id[ID_REQ_ID], uint8_t key[32])
{
    id_shake(key, 32, "ZXV-ident/seal", ss, PQM_SS_BYTES, vault_id, ID_HASH, req_id, ID_REQ_ID);
}

static const uint8_t ZERO_NONCE[12] = {0};

id_status_t id_rec_add_share(id_recovery_t *r, const id_sealed_share_t *s)
{
    uint8_t ss[PQM_SS_BYTES], key[32], aad[ID_HASH + ID_REQ_ID], pt[1 + ID_SHARE_LEN], c[ID_HASH];
    vault_view_t vv;
    if (!r || !s) return ID_ERR_ARG;
    if (r->state != REC_REQUESTED) return r->state == REC_LOCKED ? ID_ERR_LOCKED : ID_ERR_STATE;
    if (!id_meq(s->vault_id, r->vault_id, ID_HASH) || !id_meq(s->req_id, r->req_id, ID_REQ_ID))
        return ID_ERR_STATE;
    if (s->ct_len > sizeof s->ct || !pqm_kem_ct_decode(&g_ct, s->ct, s->ct_len))
        return ID_ERR_FORMAT;
    if (!pqm_decaps(&r->kem_sk, &g_ct, ss)) return ID_ERR_CRYPTO;
    seal_key(ss, s->vault_id, s->req_id, key);
    id_mcpy(aad, s->vault_id, ID_HASH);
    id_mcpy(aad + ID_HASH, s->req_id, ID_REQ_ID);
    bool ok = aead_open(key, ZERO_NONCE, aad, sizeof aad, s->enc, pt, sizeof pt, s->tag);
    id_wipe(ss, sizeof ss);
    id_wipe(key, sizeof key);
    if (!ok) return ID_ERR_AUTH;
    const id_vault_blob_t *vb = &r->cand[r->chosen];
    if (!parse_vault(vb->blob, vb->len, &vv)) return ID_ERR_FORMAT;
    uint8_t x = pt[0];
    if (x == 0 || x > vv.n) return ID_ERR_FORMAT;
    share_commit(vv.share_salt, x, pt + 1, c);
    if (!id_meq(c, vv.commits + ID_HASH * (x - 1u), ID_HASH)) {
        id_wipe(pt, sizeof pt);
        return ID_ERR_AUTH;
    }
    for (uint32_t i = 0; i < r->nshares; i++)
        if (r->xs[i] == x) {
            id_wipe(pt, sizeof pt);
            return ID_OK; /* duplicate: already have it */
        }
    if (r->nshares >= ID_MAX_GUARDIANS) return ID_ERR_FULL;
    r->xs[r->nshares] = x;
    id_mcpy(r->ys[r->nshares], pt + 1, ID_SHARE_LEN);
    r->nshares++;
    id_wipe(pt, sizeof pt);
    return ID_OK;
}

id_status_t id_rec_finish(id_recovery_t *r, const uint8_t *features, uint32_t nbits,
                          uint64_t now_ms, uint8_t seed[32], pqm_level_t *level)
{
    uint8_t g[32], fek[32], kb[32], secb[SECB_LEN];
    vault_view_t vv;
    id_status_t s;
    if (!r || !features || !seed || !level) return ID_ERR_ARG;
    if (r->state == REC_LOCKED) return ID_ERR_LOCKED;
    if (r->state != REC_REQUESTED) return ID_ERR_STATE;
    if (r->nshares < r->k) return ID_ERR_THRESHOLD;
    if (nbits != (uint32_t) r->blocks * ID_BCH_N) return ID_ERR_ARG;
    const id_vault_blob_t *vb = &r->cand[r->chosen];
    if (!parse_vault(vb->blob, vb->len, &vv)) return ID_ERR_FORMAT;
    if ((s = id_shamir_combine(r->xs, (const uint8_t(*)[ID_SHARE_LEN]) r->ys, r->k, g)) != ID_OK)
        return s;
    bool ok = false;
    if (id_fe_rep(r->fe_t, features, nbits, r->helper, r->fe_salt, fek) == ID_OK) {
        key_b(r->nk, fek, g, kb);
        ok = aead_open(kb, vv.nonce_b, vb->blob, (uint32_t) (vv.b - vb->blob), vv.b, secb, SECB_LEN,
                       vv.b + SECB_LEN);
    }
    id_wipe(g, sizeof g);
    id_wipe(fek, sizeof fek);
    id_wipe(kb, sizeof kb);
    if (!ok) {
        r->fails++;
        id_alert(&r->host, ID_ALERT_RECOVERY_FAILED, 0, r->vault_id, r->req_id, now_ms, r->fails);
        if (r->fails >= ID_REC_MAX_FAILS) {
            id_wipe(r->ys, sizeof r->ys);
            id_wipe(r->xs, sizeof r->xs);
            id_wipe(r->nk, sizeof r->nk);
            r->nshares = 0;
            r->state = REC_LOCKED;
            id_alert(&r->host, ID_ALERT_RECOVERY_LOCKED, 0, r->vault_id, r->req_id, now_ms,
                     r->fails);
            return ID_ERR_LOCKED;
        }
        return ID_ERR_NOMATCH;
    }
    id_mcpy(seed, secb, 32);
    *level = (pqm_level_t) secb[32];
    id_mcpy(r->account, secb + 33, ID_HASH);
    id_wipe(secb, sizeof secb);
    r->state = REC_DONE;
    return ID_OK;
}

void id_rec_wipe(id_recovery_t *r)
{
    if (!r) return;
    pqm_kem_sk_wipe(&r->kem_sk);
    id_wipe(r, sizeof *r);
}

/* ===== guardian ===== */
void id_guard_init(id_guardian_t *g, const id_host_t *h)
{
    id_mset(g, 0, sizeof *g);
    if (h) id_mcpy(&g->host, h, sizeof g->host);
}

static id_guard_acct_t *find_acct(id_guardian_t *g, const uint8_t vault_id[ID_HASH])
{
    for (uint32_t i = 0; i < ID_GUARD_MAX_ACCTS; i++)
        if (g->acct[i].used && id_meq(g->acct[i].sh.vault_id, vault_id, ID_HASH))
            return &g->acct[i];
    return 0;
}

id_status_t id_guard_keep(id_guardian_t *g, const id_guardian_share_t *sh,
                          const pqm_sig_pk_t *root_pk, uint64_t delay_ms)
{
    uint8_t did[ID_HASH];
    if (!g || !sh || !root_pk || sh->index == 0 || sh->index > sh->n) return ID_ERR_ARG;
    if (id_did_of(root_pk, did) != ID_OK || !id_meq(did, sh->account, ID_HASH)) return ID_ERR_AUTH;
    id_guard_acct_t *a = find_acct(g, sh->vault_id);
    for (uint32_t i = 0; !a && i < ID_GUARD_MAX_ACCTS; i++)
        if (!g->acct[i].used) a = &g->acct[i];
    if (!a) return ID_ERR_FULL;
    id_wipe(a, sizeof *a);
    a->used = 1;
    id_mcpy(&a->sh, sh, sizeof a->sh);
    id_mcpy(&a->root_pk, root_pk, sizeof a->root_pk);
    a->delay_ms = delay_ms ? delay_ms : ID_GUARD_DELAY_MS;
    return ID_OK;
}

static void roll_window(id_guard_acct_t *a, uint64_t now)
{
    if (a->window_start_ms == 0 || now < a->window_start_ms ||
        now - a->window_start_ms >= ID_GUARD_WINDOW_MS) {
        a->window_start_ms = now;
        a->requests = 0;
        a->cancels = 0;
    }
}

id_status_t id_guard_on_request(id_guardian_t *g, const id_rec_request_t *req, uint64_t now_ms)
{
    if (!g || !req || req->kem_pk_len > sizeof req->kem_pk) return ID_ERR_ARG;
    id_guard_acct_t *a = find_acct(g, req->vault_id);
    if (!a) return ID_ERR_NOTFOUND;
    roll_window(a, now_ms);
    if (a->pending) {
        if (id_meq(a->req_id, req->req_id, ID_REQ_ID)) return ID_OK; /* repeat */
        return ID_ERR_STATE;
    }
    if (a->requests >= ID_GUARD_MAX_REQ) {
        id_alert(&g->host, ID_ALERT_RECOVERY_RATE, a->sh.account, a->sh.vault_id, req->req_id,
                 now_ms, a->requests);
        return ID_ERR_RATE;
    }
    if (!pqm_kem_pk_decode(&a->kem_pk, req->kem_pk, req->kem_pk_len)) return ID_ERR_FORMAT;
    a->requests++;
    a->pending = 1;
    id_mcpy(a->req_id, req->req_id, ID_REQ_ID);
    a->req_ms = now_ms; /* the guardian's own clock, not the requester's claim */
    id_alert(&g->host, ID_ALERT_RECOVERY_REQUESTED, a->sh.account, a->sh.vault_id, req->req_id,
             now_ms + a->delay_ms, a->requests);
    return ID_OK;
}

id_status_t id_guard_approve(id_guardian_t *g, const uint8_t vault_id[ID_HASH],
                             const uint8_t req_id[ID_REQ_ID], uint64_t now_ms,
                             id_sealed_share_t *out)
{
    uint8_t coins[32], ss[PQM_SS_BYTES], key[32], aad[ID_HASH + ID_REQ_ID], pt[1 + ID_SHARE_LEN];
    if (!g || !vault_id || !req_id || !out || !g->host.random) return ID_ERR_ARG;
    id_guard_acct_t *a = find_acct(g, vault_id);
    if (!a) return ID_ERR_NOTFOUND;
    if (!a->pending || !id_meq(a->req_id, req_id, ID_REQ_ID)) return ID_ERR_STATE;
    if (now_ms < a->req_ms + a->delay_ms) return ID_ERR_WAIT;
    g->host.random(g->host.ctx, coins, 32);
    bool ok = pqm_encaps(&a->kem_pk, coins, &g_ct, ss);
    id_wipe(coins, sizeof coins);
    if (!ok) return ID_ERR_CRYPTO;
    id_mset(out, 0, sizeof *out);
    id_mcpy(out->vault_id, vault_id, ID_HASH);
    id_mcpy(out->req_id, req_id, ID_REQ_ID);
    size_t cl = pqm_kem_ct_encode(&g_ct, out->ct, sizeof out->ct);
    if (cl == 0) {
        id_wipe(ss, sizeof ss);
        return ID_ERR_CRYPTO;
    }
    out->ct_len = (uint32_t) cl;
    seal_key(ss, vault_id, req_id, key);
    id_mcpy(aad, vault_id, ID_HASH);
    id_mcpy(aad + ID_HASH, req_id, ID_REQ_ID);
    pt[0] = a->sh.index;
    id_mcpy(pt + 1, a->sh.share, ID_SHARE_LEN);
    aead_seal(key, ZERO_NONCE, aad, sizeof aad, pt, out->enc, sizeof pt, out->tag);
    id_wipe(pt, sizeof pt);
    id_wipe(ss, sizeof ss);
    id_wipe(key, sizeof key);
    a->pending = 0;
    id_alert(&g->host, ID_ALERT_SHARE_RELEASED, a->sh.account, a->sh.vault_id, req_id, now_ms,
             a->requests);
    return ID_OK;
}

static void cancel_challenge(const uint8_t vault_id[ID_HASH], const uint8_t req_id[ID_REQ_ID],
                             uint8_t out[ID_HASH])
{
    id_sha3(out, "ZXV-ident/cancel", vault_id, ID_HASH, req_id, ID_REQ_ID, 0, 0);
}

id_status_t id_rec_cancel_make(id_passkey_t *pk, const id_host_t *h, const id_dev_cert_t *cert,
                               const uint8_t vault_id[ID_HASH], const uint8_t req_id[ID_REQ_ID],
                               id_rec_cancel_t *out)
{
    uint8_t chal[ID_HASH], cdh[ID_HASH];
    id_status_t s;
    if (!pk || !cert || !vault_id || !req_id || !out) return ID_ERR_ARG;
    id_mset(out, 0, sizeof *out);
    id_mcpy(out->vault_id, vault_id, ID_HASH);
    id_mcpy(out->req_id, req_id, ID_REQ_ID);
    id_mcpy(&out->cert, cert, sizeof out->cert);
    id_mcpy(out->passkey_pk, pk->pk, ID_MLDSA65_PK);
    cancel_challenge(vault_id, req_id, chal);
    if ((s = id_client_data_hash("webauthn.get", chal, ID_HASH, ID_RECOVERY_ORIGIN, cdh)) != ID_OK)
        return s;
    return id_passkey_assert(pk, h, true, cdh, out->auth, out->sig);
}

id_status_t id_guard_cancel(id_guardian_t *g, const id_rec_cancel_t *c, uint64_t now_ms)
{
    static id_rp_cred_t cred;
    uint8_t chal[ID_HASH], cdh[ID_HASH];
    id_status_t s;
    if (!g || !c) return ID_ERR_ARG;
    id_guard_acct_t *a = find_acct(g, c->vault_id);
    if (!a) return ID_ERR_NOTFOUND;
    roll_window(a, now_ms);
    if (!a->pending || !id_meq(a->req_id, c->req_id, ID_REQ_ID)) return ID_ERR_STATE;
    if (a->cancels >= ID_GUARD_MAX_CANCEL) {
        id_alert(&g->host, ID_ALERT_CANCEL_ABUSE, a->sh.account, a->sh.vault_id, c->req_id, now_ms,
                 a->cancels);
        return ID_ERR_RATE;
    }
    if ((s = id_dev_cert_verify(&a->root_pk, &c->cert, c->passkey_pk)) != ID_OK) return s;
    if (!id_meq(c->cert.root_did, a->sh.account, ID_HASH)) return ID_ERR_AUTH;
    id_mset(&cred, 0, sizeof cred);
    id_mcpy(cred.cred_id, c->cert.cred_id, ID_CRED_ID);
    sha256((const uint8_t *) ID_PLATFORM_RP, id_strlen(ID_PLATFORM_RP, ID_RP_MAX), cred.rp_hash);
    id_mcpy(cred.pk, c->passkey_pk, ID_MLDSA65_PK);
    cancel_challenge(c->vault_id, c->req_id, chal);
    if ((s = id_client_data_hash("webauthn.get", chal, ID_HASH, ID_RECOVERY_ORIGIN, cdh)) != ID_OK)
        return s;
    if ((s = id_rp_verify(&cred, ID_PLATFORM_RP, c->auth, ID_AUTHDATA_MIN, cdh, c->sig, true)) !=
        ID_OK)
        return s;
    a->pending = 0;
    a->cancels++;
    id_alert(&g->host, ID_ALERT_RECOVERY_CANCELLED, a->sh.account, a->sh.vault_id, c->req_id,
             now_ms, a->cancels);
    return ID_OK;
}

/* ===== re-enrolment ===== */
id_status_t id_reenrol(const id_root_t *root, const id_host_t *h, const id_passkey_t *new_pk,
                       uint64_t now_ms, id_dev_cert_t *cert, id_cred_status_t *next,
                       id_cred_status_t *old)
{
    id_status_t s;
    if (!root || !h || !new_pk || !cert || !next) return ID_ERR_ARG;
    if ((s = id_dev_cert_issue(root, h, new_pk, now_ms, cert)) != ID_OK) return s;
    id_status_init(next, root->did, now_ms);
    if ((s = id_status_add(next, new_pk->cred_id, new_pk->dev_id)) != ID_OK) return s;
    if ((s = id_status_sign(root, h, next)) != ID_OK) return s;
    if (old) return id_status_accept(old, next, &root->pk, h);
    return ID_OK;
}

/* ===== wire encodings ===== */
int32_t id_rec_request_encode(const id_rec_request_t *q, uint8_t *out, uint32_t cap)
{
    id_w_t w;
    if (!q || !out || q->kem_pk_len > sizeof q->kem_pk) return -1;
    id_w_init(&w, out, cap);
    id_w_bytes(&w, "ZIR", 3);
    id_w_bytes(&w, q->vault_id, ID_HASH);
    id_w_bytes(&w, q->req_id, ID_REQ_ID);
    id_w_u64(&w, q->requested_ms);
    id_w_u32(&w, q->kem_pk_len);
    id_w_bytes(&w, q->kem_pk, q->kem_pk_len);
    return w.err ? -1 : (int32_t) w.len;
}

id_status_t id_rec_request_decode(id_rec_request_t *q, const uint8_t *in, uint32_t len)
{
    id_r_t r;
    uint8_t m[3];
    if (!q || !in) return ID_ERR_ARG;
    id_mset(q, 0, sizeof *q);
    id_r_init(&r, in, len);
    id_r_bytes(&r, m, 3);
    if (m[0] != 'Z' || m[1] != 'I' || m[2] != 'R') return ID_ERR_FORMAT;
    id_r_bytes(&r, q->vault_id, ID_HASH);
    id_r_bytes(&r, q->req_id, ID_REQ_ID);
    q->requested_ms = id_r_u64(&r);
    q->kem_pk_len = id_r_u32(&r);
    if (r.err || q->kem_pk_len > sizeof q->kem_pk) return ID_ERR_FORMAT;
    id_r_bytes(&r, q->kem_pk, q->kem_pk_len);
    if (r.err || r.pos != len) return ID_ERR_FORMAT;
    return ID_OK;
}

int32_t id_sealed_share_encode(const id_sealed_share_t *s, uint8_t *out, uint32_t cap)
{
    id_w_t w;
    if (!s || !out || s->ct_len > sizeof s->ct) return -1;
    id_w_init(&w, out, cap);
    id_w_bytes(&w, "ZIS", 3);
    id_w_bytes(&w, s->vault_id, ID_HASH);
    id_w_bytes(&w, s->req_id, ID_REQ_ID);
    id_w_u32(&w, s->ct_len);
    id_w_bytes(&w, s->ct, s->ct_len);
    id_w_bytes(&w, s->enc, sizeof s->enc);
    id_w_bytes(&w, s->tag, sizeof s->tag);
    return w.err ? -1 : (int32_t) w.len;
}

id_status_t id_sealed_share_decode(id_sealed_share_t *s, const uint8_t *in, uint32_t len)
{
    id_r_t r;
    uint8_t m[3];
    if (!s || !in) return ID_ERR_ARG;
    id_mset(s, 0, sizeof *s);
    id_r_init(&r, in, len);
    id_r_bytes(&r, m, 3);
    if (m[0] != 'Z' || m[1] != 'I' || m[2] != 'S') return ID_ERR_FORMAT;
    id_r_bytes(&r, s->vault_id, ID_HASH);
    id_r_bytes(&r, s->req_id, ID_REQ_ID);
    s->ct_len = id_r_u32(&r);
    if (r.err || s->ct_len > sizeof s->ct) return ID_ERR_FORMAT;
    id_r_bytes(&r, s->ct, s->ct_len);
    id_r_bytes(&r, s->enc, sizeof s->enc);
    id_r_bytes(&r, s->tag, sizeof s->tag);
    if (r.err || r.pos != len) return ID_ERR_FORMAT;
    return ID_OK;
}
