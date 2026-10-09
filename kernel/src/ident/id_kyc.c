/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* id_kyc.c -- optional KYC: attestations shaped like W3C Verifiable
 * Credentials, salted-commitment selective disclosure, issuer revocation
 * bitmaps, operator profiles, verifier adapters and tiered limits.
 *
 * KYC is OFF unless an operator profile sets required_level > 0, and a
 * person's wallet does nothing until they opt in. The module never sees
 * identity documents: a licensed verifier (an id_kyc_adapter_t) checks
 * them in its own process and hands back only a signed attestation plus
 * the openings of its claim commitments.
 *
 * HONEST LIMITS: commitments hide claim values only as well as their
 * 128-bit salts are kept secret; once a claim is opened to a verifier,
 * that verifier knows it. Presentations of one credential are linkable
 * (same signature, same subject key); use one credential per verifier
 * for unlinkability. The trust decision (which issuers are licensed for
 * which levels) is the operator's: ident only enforces the list.
 */
#include "id_internal.h"
#include "../tensor/zt.h"

static pqm_sig_pk_t g_hpk;
static pqm_sig_sk_t g_hsk;

/* ---- claims ---- */
static bool tag_ok(const char *tag)
{
    uint32_t n = id_strlen(tag, ID_CLAIM_TAG);
    if (n == 0 || n >= ID_CLAIM_TAG) return false;
    for (uint32_t i = 0; i < n; i++) {
        char c = tag[i];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    }
    return true;
}

id_status_t id_claim_set(id_claim_t *c, const id_host_t *h, const char *tag, const uint8_t *value,
                         uint32_t len)
{
    if (!c || !h || !h->random || !tag_ok(tag) || (!value && len) || len > ID_CLAIM_VAL)
        return ID_ERR_ARG;
    id_mset(c, 0, sizeof *c);
    id_mcpy(c->tag, tag, id_strlen(tag, ID_CLAIM_TAG));
    c->len = (uint8_t) len;
    if (len) id_mcpy(c->value, value, len);
    h->random(h->ctx, c->salt, ID_SALT);
    return ID_OK;
}

static void claim_commit(const id_claim_t *c, uint8_t out[ID_HASH])
{
    uint8_t tag[ID_CLAIM_TAG], lv[1 + ID_CLAIM_VAL];
    uint32_t n = c->len > ID_CLAIM_VAL ? ID_CLAIM_VAL : c->len;
    id_mset(tag, 0, sizeof tag);
    id_mcpy(tag, c->tag, id_strlen(c->tag, ID_CLAIM_TAG - 1u));
    lv[0] = (uint8_t) n;
    id_mcpy(lv + 1, c->value, n);
    id_sha3(out, "ZXV-ident/claim", c->salt, ID_SALT, tag, ID_CLAIM_TAG, lv, 1u + n);
}

/* ---- credential ---- */
#define VC_MSG (2u * ID_HASH + 1u + 2u + 8u + 8u + 4u + 1u + ID_MAX_CLAIMS * ID_HASH)

static uint32_t vc_msg(const id_vc_t *vc, uint8_t out[VC_MSG])
{
    id_w_t w;
    id_w_init(&w, out, VC_MSG);
    id_w_bytes(&w, vc->issuer, ID_HASH);
    id_w_bytes(&w, vc->subject, ID_HASH);
    id_w_u8(&w, vc->kyc_level);
    id_w_bytes(&w, vc->jurisdiction, 2);
    id_w_u64(&w, vc->issued_ms);
    id_w_u64(&w, vc->expires_ms);
    id_w_u32(&w, vc->status_index);
    id_w_u8(&w, vc->nclaims);
    for (uint32_t i = 0; i < vc->nclaims && i < ID_MAX_CLAIMS; i++)
        id_w_bytes(&w, vc->commit[i], ID_HASH);
    return w.err ? 0 : w.len;
}

static bool jur_ok(const char *j)
{
    return j &&
           ((j[0] >= 'A' && j[0] <= 'Z' && j[1] >= 'A' && j[1] <= 'Z' && j[2] == 0) || j[0] == 0);
}

id_status_t id_issuer_init(id_issuer_t *iss, pqm_level_t level, const uint8_t seed[32],
                           const char *name)
{
    uint8_t ks[32];
    if (!iss || !seed) return ID_ERR_ARG;
    id_mset(iss, 0, sizeof *iss);
    id_mcpy(iss->name, name, id_strlen(name, ID_NAME_MAX - 1u));
    id_root_derive(seed, "issuer-sig", 0, ks);
    bool ok = pqm_sig_keygen(level, ks, &iss->pk, &iss->sk);
    id_wipe(ks, sizeof ks);
    if (!ok) return ID_ERR_CRYPTO;
    return id_did_of(&iss->pk, iss->did);
}

id_status_t id_vc_issue(const id_issuer_t *iss, const id_host_t *h, const id_kyc_request_t *req,
                        const id_claim_t *claims, uint8_t nclaims, uint64_t now_ms,
                        uint64_t expires_ms, uint32_t status_index, id_vc_t *vc)
{
    uint8_t m[VC_MSG];
    if (!iss || !req || !vc || (!claims && nclaims) || nclaims > ID_MAX_CLAIMS) return ID_ERR_ARG;
    if (req->level > ID_KYC_MAX_LEVEL || !jur_ok(req->jurisdiction)) return ID_ERR_ARG;
    if (expires_ms <= now_ms || status_index >= ID_REV_BITS) return ID_ERR_ARG;
    id_mset(vc, 0, sizeof *vc);
    id_mcpy(vc->issuer, iss->did, ID_HASH);
    id_mcpy(vc->subject, req->subject, ID_HASH);
    vc->kyc_level = req->level;
    id_mcpy(vc->jurisdiction, req->jurisdiction, 3);
    vc->issued_ms = now_ms;
    vc->expires_ms = expires_ms;
    vc->status_index = status_index;
    vc->nclaims = nclaims;
    for (uint32_t i = 0; i < nclaims; i++) {
        if (!tag_ok(claims[i].tag)) return ID_ERR_ARG;
        claim_commit(&claims[i], vc->commit[i]);
    }
    uint32_t n = vc_msg(vc, m);
    return id_pqm_sign(&iss->sk, h, "ZXV-ident/vc", m, n, vc->sig, &vc->sig_len);
}

id_status_t id_vc_verify_sig(const pqm_sig_pk_t *issuer_pk, const id_vc_t *vc)
{
    uint8_t m[VC_MSG], did[ID_HASH];
    if (!issuer_pk || !vc || vc->nclaims > ID_MAX_CLAIMS) return ID_ERR_ARG;
    if (id_did_of(issuer_pk, did) != ID_OK || !id_meq(did, vc->issuer, ID_HASH))
        return ID_ERR_UNTRUSTED;
    uint32_t n = vc_msg(vc, m);
    return id_pqm_verify(issuer_pk, "ZXV-ident/vc", m, n, vc->sig, vc->sig_len);
}

/* ---- JSON (W3C VC data model 2.0 shape) ---- */
static void put_u32(id_w_t *w, uint32_t v)
{
    char d[10];
    uint32_t n = 0;
    do {
        d[n++] = (char) ('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (n) id_w_u8(w, (uint8_t) d[--n]);
}

static void put_2(id_w_t *w, uint32_t v)
{
    id_w_u8(w, (uint8_t) ('0' + (v / 10u) % 10u));
    id_w_u8(w, (uint8_t) ('0' + v % 10u));
}

static void put_s(id_w_t *w, const char *s)
{
    id_w_bytes(w, s, id_strlen(s, 4096));
}

/* RFC 3339 UTC from milliseconds since 1970 (days-from-civil inverse). */
static void put_time(id_w_t *w, uint64_t ms)
{
    uint64_t rem;
    uint64_t secs = zt_udiv64(ms, 1000u, 0);
    uint32_t days = (uint32_t) zt_udiv64(secs, 86400u, &rem);
    uint32_t sod = (uint32_t) rem;
    uint32_t z = days + 719468u;
    uint32_t era = z / 146097u;
    uint32_t doe = z - era * 146097u;
    uint32_t yoe = (doe - doe / 1460u + doe / 36524u - doe / 146096u) / 365u;
    uint32_t y = yoe + era * 400u;
    uint32_t doy = doe - (365u * yoe + yoe / 4u - yoe / 100u);
    uint32_t mp = (5u * doy + 2u) / 153u;
    uint32_t d = doy - (153u * mp + 2u) / 5u + 1u;
    uint32_t m = mp < 10u ? mp + 3u : mp - 9u;
    if (m <= 2u) y++;
    id_w_u8(w, '"');
    put_2(w, y / 100u);
    put_2(w, y % 100u);
    id_w_u8(w, '-');
    put_2(w, m);
    id_w_u8(w, '-');
    put_2(w, d);
    id_w_u8(w, 'T');
    put_2(w, sod / 3600u);
    id_w_u8(w, ':');
    put_2(w, (sod / 60u) % 60u);
    id_w_u8(w, ':');
    put_2(w, sod % 60u);
    put_s(w, "Z\"");
}

static void put_did(id_w_t *w, const uint8_t did[ID_HASH])
{
    char s[ID_DID_CHARS];
    id_did_string(did, s);
    put_s(w, s);
}

static void put_b64(id_w_t *w, const uint8_t *b, uint32_t n)
{
    static const char T[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t acc = 0, bits = 0;
    for (uint32_t i = 0; i < n; i++) {
        acc = (acc << 8) | b[i];
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            id_w_u8(w, (uint8_t) T[(acc >> bits) & 63u]);
        }
    }
    if (bits) id_w_u8(w, (uint8_t) T[(acc << (6 - bits)) & 63u]);
}

int32_t id_vc_to_json(const id_vc_t *vc, char *out, uint32_t cap)
{
    id_w_t w;
    if (!vc || !out || cap == 0 || vc->nclaims > ID_MAX_CLAIMS) return -1;
    id_w_init(&w, (uint8_t *) out, cap - 1u);
    put_s(&w, "{\"@context\":[\"https://www.w3.org/ns/credentials/v2\"],"
              "\"type\":[\"VerifiableCredential\",\"KycAttestation\"],\"issuer\":\"");
    put_did(&w, vc->issuer);
    put_s(&w, "\",\"validFrom\":");
    put_time(&w, vc->issued_ms);
    put_s(&w, ",\"validUntil\":");
    put_time(&w, vc->expires_ms);
    put_s(&w, ",\"credentialSubject\":{\"id\":\"");
    put_did(&w, vc->subject);
    put_s(&w, "\",\"kycLevel\":");
    put_u32(&w, vc->kyc_level);
    put_s(&w, ",\"jurisdiction\":\"");
    id_w_bytes(&w, vc->jurisdiction, id_strlen(vc->jurisdiction, 2));
    put_s(&w, "\",\"claimCommitments\":[");
    for (uint32_t i = 0; i < vc->nclaims; i++) {
        if (i) id_w_u8(&w, ',');
        id_w_u8(&w, '"');
        put_b64(&w, vc->commit[i], ID_HASH);
        id_w_u8(&w, '"');
    }
    put_s(&w, "]},\"credentialStatus\":{\"type\":\"BitstringStatusListEntry\","
              "\"statusPurpose\":\"revocation\",\"statusListIndex\":\"");
    put_u32(&w, vc->status_index);
    put_s(&w, "\",\"statusListCredential\":\"");
    put_did(&w, vc->issuer);
    put_s(&w, "#revocation\"},\"proof\":{\"type\":\"DataIntegrityProof\","
              "\"cryptosuite\":\"zxv-pqm-2026\",\"proofPurpose\":\"assertionMethod\","
              "\"proofValue\":\"u");
    put_b64(&w, vc->sig, vc->sig_len > PQM_SIG_MAX_BYTES ? 0 : vc->sig_len);
    put_s(&w, "\"}}");
    if (w.err) return -1;
    out[w.len] = 0;
    return (int32_t) w.len;
}

/* ---- revocation lists ---- */
void id_revlist_init(id_revlist_t *rl, const uint8_t issuer[ID_HASH], uint64_t version,
                     uint64_t now_ms)
{
    id_mset(rl, 0, sizeof *rl);
    if (issuer) id_mcpy(rl->issuer, issuer, ID_HASH);
    rl->version = version;
    rl->issued_ms = now_ms;
}

id_status_t id_revlist_revoke(id_revlist_t *rl, uint32_t index)
{
    if (!rl || index >= ID_REV_BITS) return ID_ERR_ARG;
    rl->bits[index >> 3] |= (uint8_t) (1u << (index & 7u));
    rl->sig_len = 0;
    return ID_OK;
}

#define RL_MSG (ID_HASH + 16u + ID_REV_BITS / 8u)

static uint32_t rl_msg(const id_revlist_t *rl, uint8_t out[RL_MSG])
{
    id_w_t w;
    id_w_init(&w, out, RL_MSG);
    id_w_bytes(&w, rl->issuer, ID_HASH);
    id_w_u64(&w, rl->version);
    id_w_u64(&w, rl->issued_ms);
    id_w_bytes(&w, rl->bits, ID_REV_BITS / 8u);
    return w.len;
}

id_status_t id_revlist_sign(const id_issuer_t *iss, const id_host_t *h, id_revlist_t *rl)
{
    uint8_t m[RL_MSG];
    if (!iss || !rl) return ID_ERR_ARG;
    id_mcpy(rl->issuer, iss->did, ID_HASH);
    uint32_t n = rl_msg(rl, m);
    return id_pqm_sign(&iss->sk, h, "ZXV-ident/revlist", m, n, rl->sig, &rl->sig_len);
}

id_status_t id_revlist_verify(const pqm_sig_pk_t *issuer_pk, const id_revlist_t *rl)
{
    uint8_t m[RL_MSG], did[ID_HASH];
    if (!issuer_pk || !rl) return ID_ERR_ARG;
    if (id_did_of(issuer_pk, did) != ID_OK || !id_meq(did, rl->issuer, ID_HASH))
        return ID_ERR_UNTRUSTED;
    uint32_t n = rl_msg(rl, m);
    return id_pqm_verify(issuer_pk, "ZXV-ident/revlist", m, n, rl->sig, rl->sig_len);
}

/* ---- operator profile ---- */
void id_kyc_profile_default(id_kyc_profile_t *p, const char *name, id_op_kind_t kind)
{
    id_mset(p, 0, sizeof *p);
    if (name) id_mcpy(p->name, name, id_strlen(name, ID_NAME_MAX - 1u));
    p->kind = (uint8_t) kind;
    p->required_level = 0;
}

id_status_t id_kyc_profile_require(id_kyc_profile_t *p, uint8_t level)
{
    if (!p || level > ID_KYC_MAX_LEVEL) return ID_ERR_ARG;
    p->required_level = level;
    return ID_OK;
}

id_status_t id_kyc_profile_trust(id_kyc_profile_t *p, const pqm_sig_pk_t *issuer_pk,
                                 uint8_t max_level)
{
    if (!p || !issuer_pk || max_level > ID_KYC_MAX_LEVEL) return ID_ERR_ARG;
    if (p->nissuers >= ID_MAX_ISSUERS) return ID_ERR_FULL;
    id_trusted_issuer_t *t = &p->issuers[p->nissuers];
    if (id_did_of(issuer_pk, t->did) != ID_OK) return ID_ERR_CRYPTO;
    id_mcpy(&t->pk, issuer_pk, sizeof t->pk);
    t->max_level = max_level;
    p->nissuers++;
    return ID_OK;
}

id_status_t id_kyc_profile_jurisdiction(id_kyc_profile_t *p, const char *iso2)
{
    if (!p || !iso2 || !iso2[0] || !jur_ok(iso2)) return ID_ERR_ARG;
    if (p->njur >= ID_MAX_JUR) return ID_ERR_FULL;
    id_mcpy(p->jur[p->njur], iso2, 3);
    p->njur++;
    return ID_OK;
}

id_status_t id_kyc_profile_tier(id_kyc_profile_t *p, uint8_t level, uint64_t max_single,
                                uint64_t max_daily)
{
    if (!p || level > ID_KYC_MAX_LEVEL) return ID_ERR_ARG;
    uint32_t i = 0;
    while (i < p->ntiers && p->tiers[i].level < level) i++;
    if (i < p->ntiers && p->tiers[i].level == level) {
        p->tiers[i].max_single = max_single;
        p->tiers[i].max_daily = max_daily;
        return ID_OK;
    }
    if (p->ntiers >= ID_MAX_TIERS) return ID_ERR_FULL;
    for (uint32_t j = p->ntiers; j > i; j--)
        id_mcpy(&p->tiers[j], &p->tiers[j - 1], sizeof p->tiers[0]);
    p->tiers[i].level = level;
    p->tiers[i].max_single = max_single;
    p->tiers[i].max_daily = max_daily;
    p->ntiers++;
    return ID_OK;
}

static const id_trusted_issuer_t *find_issuer(const id_kyc_profile_t *p, const uint8_t did[32])
{
    for (uint32_t i = 0; i < p->nissuers && i < ID_MAX_ISSUERS; i++)
        if (id_meq(p->issuers[i].did, did, ID_HASH)) return &p->issuers[i];
    return 0;
}

/* ---- wallet ---- */
void id_kyc_wallet_init(id_kyc_wallet_t *w, const uint8_t root_seed[32])
{
    id_mset(w, 0, sizeof *w);
    w->opted_in = false;
    if (root_seed) id_root_derive(root_seed, "kyc-holder", 0, w->holder_master);
}

void id_kyc_opt_in(id_kyc_wallet_t *w, bool yes)
{
    if (w) w->opted_in = yes;
}

static id_status_t holder_key(const uint8_t master[32], uint32_t idx, uint8_t did[ID_HASH])
{
    uint8_t seed[32];
    id_root_derive(master, "kyc-holder-key", idx, seed);
    bool ok = pqm_sig_keygen(PQM_LEVEL_STANDARD, seed, &g_hpk, &g_hsk);
    id_wipe(seed, sizeof seed);
    if (!ok) return ID_ERR_CRYPTO;
    return did ? id_did_of(&g_hpk, did) : ID_OK;
}

id_status_t id_kyc_request(id_kyc_wallet_t *w, const id_kyc_adapter_t *a,
                           const id_kyc_profile_t *trust, const id_host_t *h, uint8_t level,
                           const char *jurisdiction, uint64_t now_ms, uint32_t *slot_out)
{
    id_kyc_request_t req;
    id_status_t s;
    if (!w || !a || !a->verify || !trust || !h || !h->random || level > ID_KYC_MAX_LEVEL)
        return ID_ERR_ARG;
    if (!w->opted_in) return ID_ERR_DISABLED;
    if (!jur_ok(jurisdiction ? jurisdiction : "")) return ID_ERR_ARG;
    uint32_t slot = 0;
    while (slot < ID_WALLET_SLOTS && w->slot[slot].used) slot++;
    if (slot == ID_WALLET_SLOTS) return ID_ERR_FULL;
    id_wallet_slot_t *ws = &w->slot[slot];
    uint32_t idx = w->next_key;
    id_mset(&req, 0, sizeof req);
    if ((s = holder_key(w->holder_master, idx, req.subject)) != ID_OK) return s;
    pqm_sig_sk_wipe(&g_hsk);
    req.level = level;
    if (jurisdiction) id_mcpy(req.jurisdiction, jurisdiction, id_strlen(jurisdiction, 2));
    h->random(h->ctx, req.nonce, 32);

    uint8_t nclaims = 0;
    id_mset(ws, 0, sizeof *ws);
    int rc = a->verify(a->ctx, &req, &ws->vc, ws->claims, &nclaims);
    if (rc != 0) {
        id_mset(ws, 0, sizeof *ws);
        return rc < 0 ? (id_status_t) rc : ID_ERR_AUTH;
    }
    const id_trusted_issuer_t *ti = find_issuer(trust, ws->vc.issuer);
    s = ID_OK;
    if (!ti || ws->vc.kyc_level > ti->max_level)
        s = ID_ERR_UNTRUSTED;
    else if (id_vc_verify_sig(&ti->pk, &ws->vc) != ID_OK)
        s = ID_ERR_AUTH;
    else if (!id_meq(ws->vc.subject, req.subject, ID_HASH))
        s = ID_ERR_AUTH;
    else if (ws->vc.kyc_level < level)
        s = ID_ERR_LEVEL;
    else if (ws->vc.expires_ms <= now_ms)
        s = ID_ERR_EXPIRED;
    else if (nclaims != ws->vc.nclaims)
        s = ID_ERR_FORMAT;
    for (uint32_t i = 0; s == ID_OK && i < nclaims; i++) {
        uint8_t c[ID_HASH];
        claim_commit(&ws->claims[i], c);
        if (!id_meq(c, ws->vc.commit[i], ID_HASH)) s = ID_ERR_AUTH;
    }
    if (s != ID_OK) {
        id_wipe(ws, sizeof *ws);
        return s;
    }
    ws->used = 1;
    ws->key_index = idx;
    w->next_key = idx + 1u;
    if (slot_out) *slot_out = slot;
    return ID_OK;
}

#define VP_MSG (ID_HASH + 2u + 32u + ID_HASH)

static void vp_msg(const id_vp_t *vp, uint8_t out[VP_MSG])
{
    uint8_t m[VC_MSG];
    uint32_t n = vc_msg(&vp->vc, m);
    id_sha3(out, "ZXV-ident/vp-vc", m, n, 0, 0, 0, 0);
    out[ID_HASH] = (uint8_t) (vp->disc_mask >> 8);
    out[ID_HASH + 1u] = (uint8_t) vp->disc_mask;
    id_mcpy(out + ID_HASH + 2u, vp->nonce, 32);
    id_mcpy(out + ID_HASH + 34u, vp->aud_hash, ID_HASH);
}

static void aud_hash(const char *aud, uint8_t out[ID_HASH])
{
    id_sha3(out, "ZXV-ident/aud", aud, id_strlen(aud, ID_AUD_MAX), 0, 0, 0, 0);
}

id_status_t id_vp_create(const id_kyc_wallet_t *w, uint32_t slot, uint16_t mask,
                         const uint8_t nonce[32], const char *audience, const id_host_t *h,
                         id_vp_t *vp)
{
    uint8_t m[VP_MSG];
    id_status_t s;
    if (!w || slot >= ID_WALLET_SLOTS || !nonce || !audience || !vp) return ID_ERR_ARG;
    if (!w->opted_in) return ID_ERR_DISABLED;
    const id_wallet_slot_t *ws = &w->slot[slot];
    if (!ws->used) return ID_ERR_NOTFOUND;
    if (mask >> ws->vc.nclaims) return ID_ERR_ARG;
    id_mset(vp, 0, sizeof *vp);
    id_mcpy(&vp->vc, &ws->vc, sizeof vp->vc);
    vp->disc_mask = mask;
    for (uint32_t i = 0; i < ws->vc.nclaims; i++)
        if (mask & (1u << i)) id_mcpy(&vp->disc[i], &ws->claims[i], sizeof vp->disc[i]);
    id_mcpy(vp->nonce, nonce, 32);
    aud_hash(audience, vp->aud_hash);
    if ((s = holder_key(w->holder_master, ws->key_index, 0)) != ID_OK) return s;
    size_t pl = pqm_sig_pk_encode(&g_hpk, vp->holder_pk, sizeof vp->holder_pk);
    if (pl == 0) {
        pqm_sig_sk_wipe(&g_hsk);
        return ID_ERR_CRYPTO;
    }
    vp->holder_pk_len = (uint32_t) pl;
    vp_msg(vp, m);
    s = id_pqm_sign(&g_hsk, h, "ZXV-ident/vp", m, sizeof m, vp->sig, &vp->sig_len);
    pqm_sig_sk_wipe(&g_hsk);
    return s;
}

static bool jur_in(const id_kyc_profile_t *p, const char *j)
{
    if (p->njur == 0) return true;
    for (uint32_t i = 0; i < p->njur && i < ID_MAX_JUR; i++)
        if (p->jur[i][0] == j[0] && p->jur[i][1] == j[1] && j[0]) return true;
    return false;
}

id_status_t id_vp_verify(const id_kyc_profile_t *p, const id_vp_t *vp, const id_revlist_t *rl,
                         const uint8_t nonce[32], const char *audience, uint64_t now_ms,
                         id_kyc_status_t *out)
{
    uint8_t m[VP_MSG], ah[ID_HASH], did[ID_HASH];
    id_status_t s;
    if (!p || !vp || !nonce || !audience || !out) return ID_ERR_ARG;
    id_mset(out, 0, sizeof *out);
    const id_vc_t *vc = &vp->vc;
    if (vc->nclaims > ID_MAX_CLAIMS || (vp->disc_mask >> vc->nclaims)) return ID_ERR_FORMAT;
    const id_trusted_issuer_t *ti = find_issuer(p, vc->issuer);
    if (!ti || vc->kyc_level > ti->max_level) return ID_ERR_UNTRUSTED;
    if ((s = id_vc_verify_sig(&ti->pk, vc)) != ID_OK) return s;
    if (now_ms < vc->issued_ms || now_ms >= vc->expires_ms) return ID_ERR_EXPIRED;
    if (!jur_in(p, vc->jurisdiction)) return ID_ERR_JURISDICTION;
    if (rl) {
        if ((s = id_revlist_verify(&ti->pk, rl)) != ID_OK) return s;
        if (p->max_revlist_age_ms &&
            (now_ms < rl->issued_ms || now_ms - rl->issued_ms > p->max_revlist_age_ms))
            return ID_ERR_EXPIRED;
        if (vc->status_index >= ID_REV_BITS) return ID_ERR_FORMAT;
        if (rl->bits[vc->status_index >> 3] & (1u << (vc->status_index & 7u)))
            return ID_ERR_REVOKED;
    } else if (p->required_level > 0) {
        return ID_ERR_NOTFOUND; /* fail closed: cannot show it is not revoked */
    }
    /* holder binding */
    if (!pqm_sig_pk_decode(&g_hpk, vp->holder_pk, vp->holder_pk_len)) return ID_ERR_FORMAT;
    if (id_did_of(&g_hpk, did) != ID_OK || !id_meq(did, vc->subject, ID_HASH)) return ID_ERR_AUTH;
    aud_hash(audience, ah);
    if (!id_meq(ah, vp->aud_hash, ID_HASH) || !id_meq(nonce, vp->nonce, 32)) return ID_ERR_AUTH;
    vp_msg(vp, m);
    if ((s = id_pqm_verify(&g_hpk, "ZXV-ident/vp", m, sizeof m, vp->sig, vp->sig_len)) != ID_OK)
        return s;
    /* openings */
    for (uint32_t i = 0; i < vc->nclaims; i++) {
        if (!(vp->disc_mask & (1u << i))) continue;
        uint8_t c[ID_HASH];
        claim_commit(&vp->disc[i], c);
        if (!id_meq(c, vc->commit[i], ID_HASH)) return ID_ERR_AUTH;
        id_mcpy(&out->disc[out->ndisc++], &vp->disc[i], sizeof out->disc[0]);
    }
    out->verified = true;
    out->level = vc->kyc_level;
    id_mcpy(out->jurisdiction, vc->jurisdiction, 3);
    out->expires_ms = vc->expires_ms;
    id_mcpy(out->issuer, vc->issuer, ID_HASH);
    if (vc->kyc_level < p->required_level) return ID_ERR_LEVEL;
    return ID_OK;
}

const id_claim_t *id_kyc_claim(const id_kyc_status_t *s, const char *tag)
{
    if (!s || !tag) return 0;
    for (uint32_t i = 0; i < s->ndisc && i < ID_MAX_CLAIMS; i++)
        if (id_streq(s->disc[i].tag, tag, ID_CLAIM_TAG)) return &s->disc[i];
    return 0;
}

/* ---- tiered limits ---- */
static bool tier_fits(const id_tier_t *t, uint64_t amount, uint64_t spent)
{
    if (t->max_single && amount > t->max_single) return false;
    if (t->max_daily) {
        if (spent > t->max_daily || amount > t->max_daily - spent) return false;
    }
    return true;
}

id_status_t id_kyc_check_payment(const id_kyc_profile_t *p, const id_kyc_status_t *s,
                                 uint64_t now_ms, uint16_t rail, uint64_t amount,
                                 uint64_t spent_today, uint8_t *need_level)
{
    if (need_level) *need_level = 0;
    if (!p || (rail != 555u && rail != 777u && rail != 888u)) return ID_ERR_ARG;
    uint8_t eff = 0;
    if (s && s->verified && now_ms < s->expires_ms) eff = s->level;
    if (eff < p->required_level) {
        if (need_level) *need_level = p->required_level;
        return ID_ERR_LEVEL;
    }
    if (p->ntiers == 0) return ID_OK; /* no limits configured */
    const id_tier_t *app = 0;
    for (uint32_t i = 0; i < p->ntiers && i < ID_MAX_TIERS; i++)
        if (p->tiers[i].level <= eff) app = &p->tiers[i];
    if (app && tier_fits(app, amount, spent_today)) return ID_OK;
    uint8_t need = 255;
    for (uint32_t i = 0; i < p->ntiers && i < ID_MAX_TIERS; i++) {
        if (p->tiers[i].level <= eff || p->tiers[i].level < p->required_level) continue;
        if (tier_fits(&p->tiers[i], amount, spent_today)) {
            need = p->tiers[i].level;
            break;
        }
    }
    if (need_level) *need_level = need;
    return app ? ID_ERR_LIMIT : ID_ERR_LEVEL;
}
