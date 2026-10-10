/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_passkey.c -- WebAuthn-shaped ML-DSA-65 passkeys, root certificates
 * for them, and the root-signed list of valid passkeys.
 *
 * Byte layout follows WebAuthn Level 3: authenticatorData = SHA-256(rp id)
 * || flags || signCount (big-endian u32) [|| AAGUID || credIdLen || credId
 * || COSE_Key], and the assertion signature covers authenticatorData ||
 * SHA-256(clientDataJSON). The COSE key is {1: 7 (AKP), 3: -49 (ML-DSA-65),
 * -1: public key} per draft-ietf-cose-dilithium. Signatures are pure
 * FIPS 204 ML-DSA-65 with an empty context.
 *
 * HONEST LIMITS: the passkey secret key sits in id_passkey_t; wrapping it
 * with the platform's secure hardware (Secure Enclave, StrongBox) is the
 * host's job. UV means "the device says its own biometric/PIN check
 * passed"; ident cannot verify that claim. No attestation statement
 * (format "none"): a relying party learns nothing about the authenticator
 * model.
 */
#include "id_internal.h"
#include "../robin_debanks/sha256.h"
#include "../mlkem/keccak.h"

/* FIPS 204 ML-DSA-65 from kernel/src/pqsec/pq_mldsa65.c. Declared here
 * rather than through pq_security.h, whose float-typed headers do not
 * build freestanding. Must match pq_security.h exactly. */
void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[1952], uint8_t sk[4032]);
void pq_mldsa65_sign(const uint8_t sk[4032], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[3309]);
bool pq_mldsa65_verify(const uint8_t pk[1952], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len, const uint8_t sig[3309]);

static const uint8_t ZXV_AAGUID[16] = {'Z', 'X', 'V', '-', 'P', 'Q', '-', 'P',
                                       'A', 'S', 'S', 'K', 'E', 'Y', '0', '1'};
static const uint8_t COSE_HDR[10] = {0xA3, 0x01, 0x07, 0x03, 0x38, 0x30, 0x20, 0x59, 0x07, 0xA0};

static bool json_safe(const char *s, uint32_t max)
{
    uint32_t n = id_strlen(s, max + 1u);
    if (n == 0 || n > max) return false;
    for (uint32_t i = 0; i < n; i++) {
        uint8_t c = (uint8_t) s[i];
        if (c < 0x20u || c == '"' || c == '\\') return false;
    }
    return true;
}

id_status_t id_client_data_hash(const char *type, const uint8_t *challenge, uint32_t chal_len,
                                const char *origin, uint8_t out[ID_HASH])
{
    char buf[ID_CLIENTDATA_MAX];
    char b64[96];
    id_w_t w;
    if (!type || !challenge || !origin || !out || chal_len < 16 || chal_len > 64) return ID_ERR_ARG;
    if (!json_safe(type, 32) || !json_safe(origin, ID_RP_MAX)) return ID_ERR_ARG;
    int32_t bl = id_b64url(challenge, chal_len, b64, sizeof b64);
    if (bl < 0) return ID_ERR_SIZE;
    id_w_init(&w, (uint8_t *) buf, sizeof buf);
    id_w_bytes(&w, "{\"type\":\"", 9);
    id_w_bytes(&w, type, id_strlen(type, 32));
    id_w_bytes(&w, "\",\"challenge\":\"", 15);
    id_w_bytes(&w, b64, (uint32_t) bl);
    id_w_bytes(&w, "\",\"origin\":\"", 12);
    id_w_bytes(&w, origin, id_strlen(origin, ID_RP_MAX));
    id_w_bytes(&w, "\",\"crossOrigin\":false}", 22);
    if (w.err) return ID_ERR_SIZE;
    sha256((const uint8_t *) buf, w.len, out);
    return ID_OK;
}

static id_status_t rp_hash(const char *rp_id, uint8_t out[ID_HASH])
{
    uint32_t n = id_strlen(rp_id, ID_RP_MAX + 1u);
    if (!rp_id || n == 0 || n > ID_RP_MAX) return ID_ERR_ARG;
    sha256((const uint8_t *) rp_id, n, out);
    return ID_OK;
}

id_status_t id_passkey_create(id_passkey_t *pk, const id_host_t *h, const char *rp_id,
                              const uint8_t dev_id[ID_DEV_ID], const uint8_t user[16])
{
    uint8_t seed[32];
    if (!pk || !h || !h->random || !dev_id || !user) return ID_ERR_ARG;
    id_mset(pk, 0, sizeof *pk);
    if (rp_hash(rp_id, pk->rp_hash) != ID_OK) return ID_ERR_ARG;
    h->random(h->ctx, pk->cred_id, ID_CRED_ID);
    h->random(h->ctx, seed, 32);
    id_mcpy(pk->dev_id, dev_id, ID_DEV_ID);
    id_mcpy(pk->user, user, 16);
    pq_mldsa65_keygen(seed, pk->pk, pk->sk);
    id_wipe(seed, sizeof seed);
    return ID_OK;
}

void id_passkey_wipe(id_passkey_t *pk)
{
    if (pk) id_wipe(pk, sizeof *pk);
}

id_status_t id_passkey_register(const id_passkey_t *pk, bool uv, uint8_t *auth, uint32_t cap,
                                uint32_t *len)
{
    id_w_t w;
    if (!pk || !auth || !len) return ID_ERR_ARG;
    id_w_init(&w, auth, cap);
    id_w_bytes(&w, pk->rp_hash, ID_HASH);
    id_w_u8(&w, (uint8_t) (ID_AUTH_UP | ID_AUTH_AT | (uv ? ID_AUTH_UV : 0u)));
    id_w_u32(&w, pk->sign_count);
    id_w_bytes(&w, ZXV_AAGUID, 16);
    id_w_u16(&w, ID_CRED_ID);
    id_w_bytes(&w, pk->cred_id, ID_CRED_ID);
    id_w_bytes(&w, COSE_HDR, sizeof COSE_HDR);
    id_w_bytes(&w, pk->pk, ID_MLDSA65_PK);
    if (w.err) return ID_ERR_SIZE;
    *len = w.len;
    return ID_OK;
}

id_status_t id_passkey_assert(id_passkey_t *pk, const id_host_t *h, bool uv,
                              const uint8_t client_data_hash[ID_HASH],
                              uint8_t auth[ID_AUTHDATA_MIN], uint8_t sig[ID_MLDSA65_SIG])
{
    uint8_t msg[ID_AUTHDATA_MIN + ID_HASH], rnd[32];
    id_w_t w;
    if (!pk || !h || !h->random || !client_data_hash || !auth || !sig) return ID_ERR_ARG;
    pk->sign_count++;
    id_w_init(&w, auth, ID_AUTHDATA_MIN);
    id_w_bytes(&w, pk->rp_hash, ID_HASH);
    id_w_u8(&w, (uint8_t) (ID_AUTH_UP | (uv ? ID_AUTH_UV : 0u)));
    id_w_u32(&w, pk->sign_count);
    id_mcpy(msg, auth, ID_AUTHDATA_MIN);
    id_mcpy(msg + ID_AUTHDATA_MIN, client_data_hash, ID_HASH);
    h->random(h->ctx, rnd, 32);
    pq_mldsa65_sign(pk->sk, msg, sizeof msg, 0, 0, rnd, sig);
    id_wipe(rnd, sizeof rnd);
    return ID_OK;
}

id_status_t id_rp_register(id_rp_cred_t *out, const char *rp_id, const uint8_t *auth, uint32_t len,
                           bool require_uv)
{
    uint8_t rh[ID_HASH], tmp[16];
    id_r_t r;
    if (!out || !auth) return ID_ERR_ARG;
    if (rp_hash(rp_id, rh) != ID_OK) return ID_ERR_ARG;
    if (len != ID_AUTHDATA_MAX) return ID_ERR_FORMAT;
    id_mset(out, 0, sizeof *out);
    id_r_init(&r, auth, len);
    id_r_bytes(&r, out->rp_hash, ID_HASH);
    uint8_t flags = id_r_u8(&r);
    out->sign_count = id_r_u32(&r);
    id_r_bytes(&r, tmp, 16); /* AAGUID: not checked (format "none") */
    if (id_r_u16(&r) != ID_CRED_ID) return ID_ERR_FORMAT;
    id_r_bytes(&r, out->cred_id, ID_CRED_ID);
    uint8_t hdr[sizeof COSE_HDR];
    id_r_bytes(&r, hdr, sizeof hdr);
    id_r_bytes(&r, out->pk, ID_MLDSA65_PK);
    if (r.err || r.pos != len) return ID_ERR_FORMAT;
    if (!id_meq(hdr, COSE_HDR, sizeof hdr)) return ID_ERR_FORMAT;
    if (!id_meq(out->rp_hash, rh, ID_HASH)) return ID_ERR_AUTH;
    if (!(flags & ID_AUTH_UP) || !(flags & ID_AUTH_AT)) return ID_ERR_FORMAT;
    if (flags & ID_AUTH_BE) return ID_ERR_FORMAT; /* we only accept device-bound keys */
    if (require_uv && !(flags & ID_AUTH_UV)) return ID_ERR_AUTH;
    return ID_OK;
}

id_status_t id_rp_verify(id_rp_cred_t *cred, const char *rp_id, const uint8_t *auth, uint32_t len,
                         const uint8_t client_data_hash[ID_HASH], const uint8_t sig[ID_MLDSA65_SIG],
                         bool require_uv)
{
    uint8_t rh[ID_HASH], msg[ID_AUTHDATA_MIN + ID_HASH];
    if (!cred || !auth || !client_data_hash || !sig) return ID_ERR_ARG;
    if (rp_hash(rp_id, rh) != ID_OK) return ID_ERR_ARG;
    if (len != ID_AUTHDATA_MIN) return ID_ERR_FORMAT;
    if (!id_meq(auth, rh, ID_HASH) || !id_meq(cred->rp_hash, rh, ID_HASH)) return ID_ERR_AUTH;
    uint8_t flags = auth[32];
    uint32_t cnt = ((uint32_t) auth[33] << 24) | ((uint32_t) auth[34] << 16) |
                   ((uint32_t) auth[35] << 8) | auth[36];
    if (!(flags & ID_AUTH_UP)) return ID_ERR_AUTH;
    if (require_uv && !(flags & ID_AUTH_UV)) return ID_ERR_AUTH;
    if ((cnt != 0 || cred->sign_count != 0) && cnt <= cred->sign_count) return ID_ERR_REPLAY;
    id_mcpy(msg, auth, ID_AUTHDATA_MIN);
    id_mcpy(msg + ID_AUTHDATA_MIN, client_data_hash, ID_HASH);
    if (!pq_mldsa65_verify(cred->pk, msg, sizeof msg, 0, 0, sig)) return ID_ERR_AUTH;
    cred->sign_count = cnt;
    return ID_OK;
}

/* ---- root certificates ---- */
/* SHA3-256("ZXV-ident/passkey-pk" || pk): too long for id_sha3's buffer. */
static void passkey_pk_hash(const uint8_t pk[ID_MLDSA65_PK], uint8_t out[ID_HASH])
{
    static const char lab[] = "ZXV-ident/passkey-pk";
    uint8_t buf[sizeof lab - 1u + ID_MLDSA65_PK];
    id_mcpy(buf, lab, sizeof lab - 1u);
    id_mcpy(buf + sizeof lab - 1u, pk, ID_MLDSA65_PK);
    sha3_256(buf, sizeof buf, out);
}

static uint32_t cert_msg(const id_dev_cert_t *c, uint8_t out[128])
{
    id_w_t w;
    id_w_init(&w, out, 128);
    id_w_bytes(&w, c->root_did, ID_HASH);
    id_w_bytes(&w, c->cred_id, ID_CRED_ID);
    id_w_bytes(&w, c->dev_id, ID_DEV_ID);
    id_w_bytes(&w, c->pk_hash, ID_HASH);
    id_w_u64(&w, c->issued_ms);
    return w.len;
}

id_status_t id_dev_cert_issue(const id_root_t *root, const id_host_t *h, const id_passkey_t *pk,
                              uint64_t now_ms, id_dev_cert_t *cert)
{
    uint8_t m[128];
    if (!root || !pk || !cert) return ID_ERR_ARG;
    id_mset(cert, 0, sizeof *cert);
    id_mcpy(cert->root_did, root->did, ID_HASH);
    id_mcpy(cert->cred_id, pk->cred_id, ID_CRED_ID);
    id_mcpy(cert->dev_id, pk->dev_id, ID_DEV_ID);
    passkey_pk_hash(pk->pk, cert->pk_hash);
    cert->issued_ms = now_ms;
    uint32_t n = cert_msg(cert, m);
    return id_pqm_sign(&root->sk, h, "ZXV-ident/devcert", m, n, cert->sig, &cert->sig_len);
}

id_status_t id_dev_cert_verify(const pqm_sig_pk_t *root_pk, const id_dev_cert_t *cert,
                               const uint8_t passkey_pk[ID_MLDSA65_PK])
{
    uint8_t m[128], did[ID_HASH], ph[ID_HASH];
    if (!root_pk || !cert || !passkey_pk) return ID_ERR_ARG;
    if (id_did_of(root_pk, did) != ID_OK || !id_meq(did, cert->root_did, ID_HASH))
        return ID_ERR_AUTH;
    passkey_pk_hash(passkey_pk, ph);
    if (!id_meq(ph, cert->pk_hash, ID_HASH)) return ID_ERR_AUTH;
    uint32_t n = cert_msg(cert, m);
    return id_pqm_verify(root_pk, "ZXV-ident/devcert", m, n, cert->sig, cert->sig_len);
}

/* ---- status list ---- */
void id_status_init(id_cred_status_t *st, const uint8_t root_did[ID_HASH], uint64_t epoch_ms)
{
    id_mset(st, 0, sizeof *st);
    if (root_did) id_mcpy(st->root_did, root_did, ID_HASH);
    st->epoch_ms = epoch_ms;
}

id_status_t id_status_add(id_cred_status_t *st, const uint8_t cred_id[ID_CRED_ID],
                          const uint8_t dev_id[ID_DEV_ID])
{
    if (!st || !cred_id || !dev_id) return ID_ERR_ARG;
    if (st->n >= ID_MAX_CREDS) return ID_ERR_FULL;
    if (id_status_is_active(st, cred_id)) return ID_ERR_STATE;
    id_mcpy(st->active[st->n].cred_id, cred_id, ID_CRED_ID);
    id_mcpy(st->active[st->n].dev_id, dev_id, ID_DEV_ID);
    st->n++;
    st->sig_len = 0;
    return ID_OK;
}

static uint32_t status_msg(const id_cred_status_t *st, uint8_t *out, uint32_t cap)
{
    id_w_t w;
    id_w_init(&w, out, cap);
    id_w_bytes(&w, st->root_did, ID_HASH);
    id_w_u64(&w, st->epoch_ms);
    id_w_u8(&w, st->n);
    for (uint32_t i = 0; i < st->n && i < ID_MAX_CREDS; i++) {
        id_w_bytes(&w, st->active[i].cred_id, ID_CRED_ID);
        id_w_bytes(&w, st->active[i].dev_id, ID_DEV_ID);
    }
    return w.err ? 0 : w.len;
}

#define ID_STATUS_MSG (ID_HASH + 9u + ID_MAX_CREDS * (ID_CRED_ID + ID_DEV_ID))

id_status_t id_status_sign(const id_root_t *root, const id_host_t *h, id_cred_status_t *st)
{
    uint8_t m[ID_STATUS_MSG];
    if (!root || !st || st->n > ID_MAX_CREDS) return ID_ERR_ARG;
    id_mcpy(st->root_did, root->did, ID_HASH);
    uint32_t n = status_msg(st, m, sizeof m);
    return id_pqm_sign(&root->sk, h, "ZXV-ident/cred-status", m, n, st->sig, &st->sig_len);
}

id_status_t id_status_verify(const pqm_sig_pk_t *root_pk, const id_cred_status_t *st)
{
    uint8_t m[ID_STATUS_MSG], did[ID_HASH];
    if (!root_pk || !st || st->n > ID_MAX_CREDS) return ID_ERR_ARG;
    if (id_did_of(root_pk, did) != ID_OK || !id_meq(did, st->root_did, ID_HASH)) return ID_ERR_AUTH;
    uint32_t n = status_msg(st, m, sizeof m);
    return id_pqm_verify(root_pk, "ZXV-ident/cred-status", m, n, st->sig, st->sig_len);
}

bool id_status_is_active(const id_cred_status_t *st, const uint8_t cred_id[ID_CRED_ID])
{
    if (!st || !cred_id) return false;
    for (uint32_t i = 0; i < st->n && i < ID_MAX_CREDS; i++)
        if (id_meq(st->active[i].cred_id, cred_id, ID_CRED_ID)) return true;
    return false;
}

id_status_t id_status_accept(id_cred_status_t *cur, const id_cred_status_t *next,
                             const pqm_sig_pk_t *root_pk, const id_host_t *h)
{
    id_status_t s;
    if (!cur || !next || !root_pk) return ID_ERR_ARG;
    if ((s = id_status_verify(root_pk, next)) != ID_OK) return s;
    bool have = cur->epoch_ms != 0 || cur->n != 0;
    if (have && !id_meq(cur->root_did, next->root_did, ID_HASH)) return ID_ERR_AUTH;
    if (have && next->epoch_ms <= cur->epoch_ms) return ID_ERR_STATE;
    for (uint32_t i = 0; have && i < cur->n && i < ID_MAX_CREDS; i++) {
        if (id_status_is_active(next, cur->active[i].cred_id)) continue;
        if (h && h->revoke_device) h->revoke_device(h->ctx, cur->active[i].dev_id);
        id_alert(h, ID_ALERT_DEVICE_REVOKED, next->root_did, 0, cur->active[i].dev_id,
                 next->epoch_ms, 0);
    }
    id_mcpy(cur, next, sizeof *cur);
    return ID_OK;
}

id_status_t id_rp_login(const pqm_sig_pk_t *root_pk, const id_cred_status_t *st,
                        const id_dev_cert_t *cert, id_rp_cred_t *cred, const char *rp_id,
                        const uint8_t *auth, uint32_t len, const uint8_t client_data_hash[ID_HASH],
                        const uint8_t sig[ID_MLDSA65_SIG], bool require_uv)
{
    id_status_t s;
    if (!root_pk || !st || !cert || !cred) return ID_ERR_ARG;
    if (!id_meq(cert->cred_id, cred->cred_id, ID_CRED_ID)) return ID_ERR_AUTH;
    if ((s = id_dev_cert_verify(root_pk, cert, cred->pk)) != ID_OK) return s;
    if ((s = id_status_verify(root_pk, st)) != ID_OK) return s;
    if (!id_status_is_active(st, cert->cred_id)) return ID_ERR_REVOKED;
    return id_rp_verify(cred, rp_id, auth, len, client_data_hash, sig, require_uv);
}
