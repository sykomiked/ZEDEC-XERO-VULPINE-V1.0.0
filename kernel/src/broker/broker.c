/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* broker.c — the House's back room. Every claim here is either checked or
 * refused; nothing is invented. See broker.h for the honest-boundaries manifest. */

#include "broker.h"
#include "../provenance/zx_provenance.h"

/* ---- tiny freestanding helpers: no libc, no <string.h> ---- */
static void b_memcpy(void *dst, const void *src, uint32_t n) {
    uint8_t *d = (uint8_t *)dst; const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i++) d[i] = s[i];
}
static void b_memzero(void *dst, uint32_t n) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < n; i++) d[i] = 0;
}
static uint32_t b_strlen(const char *s) {
    uint32_t n = 0; while (s[n]) n++; return n;
}
/* plain (non-constant-time) equality — only ever used on NON-secret keys and
 * scope strings; secrets use ct_equal from src/tls. */
static bool b_eq(const uint8_t *a, const uint8_t *b, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) if (a[i] != b[i]) return false;
    return true;
}

/* ============================================================= */
/* Capability contract matcher — glob-prefix only, no regex.      */
/* ============================================================= */
bool aipic_matches(const aipic_contract_t *c, uint8_t function,
                   const char *requested_scope) {
    if (!c || !requested_scope) return false;
    if (c->function != function) return false;        /* function must match     */

    uint32_t sl = b_strlen(c->scope);
    if (sl > 0 && c->scope[sl - 1] == '*') {
        /* glob-prefix: literal part is everything before the trailing '*'.
         * "*"        -> literal "" -> matches anything.
         * "prefix:*" -> literal "prefix:" -> requested must start with it. */
        uint32_t plen = sl - 1;
        for (uint32_t i = 0; i < plen; i++) {
            if (requested_scope[i] == '\0') return false;   /* requested shorter */
            if (requested_scope[i] != c->scope[i]) return false;
        }
        return true;
    }
    /* exact match */
    uint32_t rl = b_strlen(requested_scope);
    if (rl != sl) return false;
    for (uint32_t i = 0; i < sl; i++)
        if (requested_scope[i] != c->scope[i]) return false;
    return true;
}

/* ============================================================= */
/* Broker lifecycle + trust                                       */
/* ============================================================= */
void broker_init(broker_t *b) {
    if (!b) return;
    b_memzero(b, (uint32_t)sizeof(*b));
    b->n = 0; b->n_trusted = 0;
    b->verify = 0;
    b->settlement.settle = 0; b->settlement.ctx = 0;
}

bool broker_trust_author(broker_t *b, const uint8_t pubkey[BROKER_KEY_LEN]) {
    if (!b || !pubkey) return false;
    if (broker_is_trusted(b, pubkey)) return true;          /* idempotent        */
    if (b->n_trusted >= BROKER_MAX_TRUSTED) return false;
    b_memcpy(b->trusted[b->n_trusted], pubkey, BROKER_KEY_LEN);
    b->n_trusted++;
    return true;
}

bool broker_is_trusted(const broker_t *b, const uint8_t pubkey[BROKER_KEY_LEN]) {
    if (!b || !pubkey) return false;
    for (uint32_t i = 0; i < b->n_trusted; i++)
        if (b_eq(b->trusted[i], pubkey, BROKER_KEY_LEN)) return true;
    return false;
}

void broker_set_verifier(broker_t *b, broker_verify_fn fn) {
    if (b) b->verify = fn;
}
void broker_set_settlement(broker_t *b, const broker_settlement_t *s) {
    if (!b) return;
    if (s) b->settlement = *s;
    else { b->settlement.settle = 0; b->settlement.ctx = 0; }
}

/* ============================================================= */
/* Listing                                                        */
/* ============================================================= */
bool broker_listing_digest(const uint8_t cid[BROKER_CID_LEN], const aipic_contract_t *terms,
                           uint8_t out[32])
{
    for (uint32_t i = 0; i < 32; i++) out[i] = 0;
    if (!cid || !terms) return false;
    uint32_t sl = 0;
    while (sl < AIPIC_SCOPE_LEN && terms->scope[sl]) sl++;
    if (sl == AIPIC_SCOPE_LEN) return false; /* not NUL-terminated */
    zxp_canon_t c;
    zxp_canon_init(&c, ZXP_DOMAIN_BROKER);
    zxp_canon_bytes(&c, cid, BROKER_CID_LEN);
    zxp_canon_u32(&c, terms->function);
    zxp_canon_bytes(&c, (const uint8_t *) terms->scope, sl);
    zxp_canon_u32(&c, terms->max_price);
    zxp_canon_final(&c, out);
    return true;
}

int32_t broker_list_bytes(broker_t *b, const uint8_t *cid, uint32_t cid_len,
                          const uint8_t author[BROKER_KEY_LEN],
                          const uint8_t sig[BROKER_SIG_LEN],
                          const aipic_contract_t *terms) {
    if (!b || !cid || !author || !sig || !terms) return -(int32_t)BROKER_ERR_NULL;
    /* A CID is exactly one SHA-256 digest. 16 (MD5) or 20 (SHA-1) — anything
     * not 32 — is weak-hash inventory the House refuses to stock. */
    if (cid_len != BROKER_CID_LEN) return -(int32_t)BROKER_ERR_BAD_CID;
    if (!b->verify) return -(int32_t)BROKER_ERR_NO_VERIFY;   /* fail closed       */
    if (!broker_is_trusted(b, author)) return -(int32_t)BROKER_ERR_UNTRUSTED;
    /* Verify the SIGNATURE over the CID AND the terms — the reputation root. We
     * attest that the seller signed THIS inventory under THESE terms, never any
     * real-world fact behind them. */
    uint8_t ld[32];
    if (!broker_listing_digest(cid, terms, ld)) return -(int32_t) BROKER_ERR_SCOPE;
    if (!b->verify(ld, 32u, sig, author)) return -(int32_t) BROKER_ERR_BAD_SIG;

    for (uint32_t i = 0; i < BROKER_MAX_LISTINGS; i++) {
        if (!b->listing[i].in_use) {
            broker_listing_t *L = &b->listing[i];
            b_memcpy(L->cid, cid, BROKER_CID_LEN);
            b_memcpy(L->author, author, BROKER_KEY_LEN);
            b_memcpy(L->sig, sig, BROKER_SIG_LEN);
            L->terms = *terms;
            L->terms.in_use = true;
            L->in_use = true;
            if (i + 1 > b->n) b->n = i + 1;
            return (int32_t)i;
        }
    }
    return -(int32_t)BROKER_ERR_FULL;
}

int32_t broker_list(broker_t *b, const uint8_t product_cid[BROKER_CID_LEN],
                    const uint8_t author[BROKER_KEY_LEN],
                    const uint8_t sig[BROKER_SIG_LEN],
                    const aipic_contract_t *terms) {
    return broker_list_bytes(b, product_cid, BROKER_CID_LEN, author, sig, terms);
}

const broker_listing_t *broker_get(const broker_t *b, int32_t idx) {
    if (!b || idx < 0 || (uint32_t)idx >= BROKER_MAX_LISTINGS) return 0;
    if (!b->listing[idx].in_use) return 0;
    return &b->listing[idx];
}

/* ============================================================= */
/* Delivery — fraud-proof, via the ipfs self-certifying get       */
/* ============================================================= */
broker_result_t broker_deliver(broker_t *b, int32_t listing_idx,
                               const aipi_handshake_t *hs,
                               uint8_t function, const char *scope,
                               ipfs_node_t *node,
                               uint8_t *buf, uint32_t cap, uint32_t *out_len) {
    if (!b || !hs || !node || !buf || !out_len) return BROKER_ERR_NULL;
    const broker_listing_t *L = broker_get(b, listing_idx);
    if (!L) return BROKER_ERR_NOT_FOUND;

    /* CAPABILITY GATE — a fetch requires a proven capability, not just a listing.
     * (1) the handshake must have granted a fetch ticket (a valid keyed MAC), and
     * (2) that proof must be bound to THIS product's CID, else it is a proof for
     * something else being replayed here. */
    if (!hs->allowed) return BROKER_ERR_HS_MAC;
    for (uint32_t i = 0; i < BROKER_CID_LEN; i++)
        if (hs->product_cid[i] != L->cid[i]) return BROKER_ERR_SCOPE;
    /* (3) the listing's contract must authorise the requested function+scope. */
    if (!aipic_matches(&L->terms, function, scope)) return BROKER_ERR_SCOPE;

    ipfs_result_t r = ipfs_get_verify(node, L->cid, buf, cap, out_len);
    switch (r) {
        case IPFS_OK:               return BROKER_OK;
        case IPFS_ERR_NO_TRANSPORT: return BROKER_ERR_NO_TRANSPORT;
        case IPFS_ERR_CID_MISMATCH: return BROKER_ERR_CID_MISMATCH; /* fraud caught */
        case IPFS_ERR_NOT_FOUND:    return BROKER_ERR_NOT_FOUND_NET;
        case IPFS_ERR_BAD_CID:      return BROKER_ERR_BAD_CID;
        default:                    return BROKER_ERR_CID_MISMATCH;
    }
}

/* ============================================================= */
/* Settlement — never a simulated fill                            */
/* ============================================================= */
broker_result_t broker_settle(broker_t *b, int32_t listing_idx,
                              const uint8_t buyer[BROKER_KEY_LEN], uint64_t amount) {
    if (!b || !buyer) return BROKER_ERR_NULL;
    const broker_listing_t *L = broker_get(b, listing_idx);
    if (!L) return BROKER_ERR_NOT_FOUND;
    /* the price ceiling is a signed term: a relay cannot raise it */
    if (L->terms.max_price && amount > L->terms.max_price) return BROKER_ERR_SCOPE;
    if (!b->settlement.settle) return BROKER_ERR_NO_SETTLEMENT;  /* fail closed   */
    int rc = b->settlement.settle(buyer, L->author, amount, b->settlement.ctx);
    if (rc != 0) return BROKER_ERR_NO_SETTLEMENT;   /* backend declined: NO fill  */
    return BROKER_OK;
}

/* ============================================================= */
/* HKDF key derivation — reuses src/tls, never reimplemented      */
/* ============================================================= */
int32_t aipi_derive_key(const uint8_t *salt, uint32_t salt_len,
                        const uint8_t *ikm, uint32_t ikm_len,
                        const uint8_t *info, uint32_t info_len,
                        uint8_t out_key[AIPI_TAG_LEN]) {
    if (!out_key || (!ikm && ikm_len)) return -(int32_t)BROKER_ERR_NULL;
    uint8_t prk[HASH_LEN];
    hkdf_extract(salt, salt_len, ikm, ikm_len, prk);
    if (!hkdf_expand(prk, info, info_len, out_key, AIPI_TAG_LEN))
        return -(int32_t)BROKER_ERR_NULL;
    return 0;
}

/* ============================================================= */
/* The hardened capability handshake                              */
/* ============================================================= */
void aipi_hs_init(aipi_handshake_t *hs, const uint8_t session_key[AIPI_TAG_LEN]) {
    if (!hs) return;
    b_memzero(hs, (uint32_t)sizeof(*hs));
    if (session_key) { b_memcpy(hs->key, session_key, AIPI_TAG_LEN); hs->have_key = true; }
    hs->stage = AIPI_HS_INIT;
    hs->transcript_len = 0;
    hs->allowed = false;
}

int32_t aipi_hs_begin(aipi_handshake_t *hs, const uint8_t product_cid[BROKER_CID_LEN]) {
    if (!hs || !product_cid) return -(int32_t)BROKER_ERR_NULL;
    if (!hs->have_key)         return -(int32_t)BROKER_ERR_HS_STATE;
    if (hs->stage != AIPI_HS_INIT) return -(int32_t)BROKER_ERR_HS_STATE;
    b_memcpy(hs->transcript, product_cid, BROKER_CID_LEN);
    b_memcpy(hs->product_cid, product_cid, BROKER_CID_LEN);  /* what this proof is for */
    hs->transcript_len = BROKER_CID_LEN;
    hs->stage = AIPI_HS_HELLO;
    return 0;
}

int32_t aipi_hs_challenge(aipi_handshake_t *hs, uint8_t nonce24[AIPI_NONCE_LEN],
                          uint32_t phase) {
    if (!hs || !nonce24) return -(int32_t)BROKER_ERR_NULL;
    if (hs->stage != AIPI_HS_HELLO) return -(int32_t)BROKER_ERR_HS_STATE;
    /* The nonce is SUPPLIED by a verifiable beacon / bound RNG — we commit it to
     * the transcript, we do NOT invent it (that would be a hollow capability). */
    b_memcpy(&hs->transcript[BROKER_CID_LEN], nonce24, AIPI_NONCE_LEN);
    /* phase from a verifiable monotonic beacon, little-endian, NOT a clock. */
    uint32_t off = BROKER_CID_LEN + AIPI_NONCE_LEN;
    hs->transcript[off + 0] = (uint8_t)(phase & 0xFFu);
    hs->transcript[off + 1] = (uint8_t)((phase >> 8) & 0xFFu);
    hs->transcript[off + 2] = (uint8_t)((phase >> 16) & 0xFFu);
    hs->transcript[off + 3] = (uint8_t)((phase >> 24) & 0xFFu);
    hs->transcript_len = off + 4;
    hs->phase = phase;
    hs->stage = AIPI_HS_CHALLENGE;
    return 0;
}

int32_t aipi_hs_prove(aipi_handshake_t *hs, uint8_t tag[AIPI_TAG_LEN]) {
    if (!hs || !tag) return -(int32_t)BROKER_ERR_NULL;
    if (hs->stage != AIPI_HS_CHALLENGE) return -(int32_t)BROKER_ERR_HS_STATE;
    /* PROVE: keyed HMAC over the ENTIRE transcript (HELLO || CHALLENGE). */
    hmac_sha256(hs->key, AIPI_TAG_LEN, hs->transcript, hs->transcript_len, tag);
    b_memcpy(hs->presented_tag, tag, AIPI_TAG_LEN);
    hs->stage = AIPI_HS_PROVE;
    return 0;
}

broker_result_t aipi_hs_allow(aipi_handshake_t *hs) {
    if (!hs) return BROKER_ERR_NULL;
    if (hs->stage != AIPI_HS_PROVE) return BROKER_ERR_HS_STATE;

    /* Defense in depth against the classic "verify() succeeds on a zeroed buffer"
     * bug: an all-zero proof is refused outright, before any compare. */
    bool all_zero = true;
    for (uint32_t i = 0; i < AIPI_TAG_LEN; i++)
        if (hs->presented_tag[i] != 0) { all_zero = false; break; }
    if (all_zero) { hs->allowed = false; return BROKER_ERR_HS_MAC; }

    uint8_t expected[AIPI_TAG_LEN];
    hmac_sha256(hs->key, AIPI_TAG_LEN, hs->transcript, hs->transcript_len, expected);

    /* CONSTANT-TIME compare — a byte-at-a-time memcmp would leak the first
     * mismatching byte and let an attacker forge one byte at a time. */
    if (!ct_equal(expected, hs->presented_tag, AIPI_TAG_LEN)) {
        hs->allowed = false;
        return BROKER_ERR_HS_MAC;
    }
    hs->allowed = true;   /* fetch ticket issued */
    return BROKER_OK;
}

/* ============================================================= */
/* Tribute split — app-layer whole-unit economics (NON-security)  */
/* Largest-remainder apportionment: exact, sums to the whole.     */
/* ============================================================= */
void tribute_split(uint64_t amount, const uint8_t weights[5], uint64_t out[5]) {
    if (!weights || !out) return;
    uint64_t W = 0;
    for (int i = 0; i < 5; i++) W += (uint64_t)weights[i];
    for (int i = 0; i < 5; i++) out[i] = 0;
    if (W == 0) return;   /* nothing to apportion against */

    uint64_t rem[5];
    uint64_t allocated = 0;
    /* Exact amount*weight/W without a 128-bit intermediate (armv7 has no
     * __uint128_t). Write amount = q*W + r (r < W); then
     *   (amount*w)/W = q*w + (r*w)/W,   (amount*w)%W = (r*w)%W.
     * Every term stays in uint64: w <= W (W is the sum of weights) so q*w <= amount,
     * and r*w < W*255 <= 1275*255. */
    const uint64_t q = amount / W, r = amount % W;
    for (int i = 0; i < 5; i++) {
        const uint64_t w = (uint64_t)weights[i];
        out[i] = q * w + (r * w) / W;
        rem[i] = (r * w) % W;
        allocated += out[i];
    }
    /* Distribute the leftover units to the largest remainders (Hamilton). The
     * leftover is always < 5, so at most four passes. */
    uint64_t leftover = amount - allocated;
    bool picked[5] = { false, false, false, false, false };
    while (leftover > 0) {
        int best = -1;
        for (int i = 0; i < 5; i++) {
            if (picked[i]) continue;
            if (best < 0 || rem[i] > rem[best]) best = i;
        }
        if (best < 0) break;   /* unreachable while leftover>0, but fail safe */
        out[best] += 1;
        picked[best] = true;
        leftover--;
    }
}

/* ---- DECLARATION -----------------------------------------------------------

 * Mister Shanghai's listings, behind pirate_apps' Counter (app_counter_open).
 * Requirements measured from broker.o's `nm -u`: {ct_equal, hkdf_expand,
 * hkdf_extract, hmac_sha256} -> hkdf_ready, and {ipfs_get_verify} ->
 * ipfs_ready.
 */
#include "zxv_decl.h"
ZXV_DECLARE(broker,
    ZXV_PROVIDES(broker_ready),
    ZXV_REQUIRES(ipfs_ready, hkdf_ready),
    ZXV_NO_BRINGUP);
