/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_bridge.c — identity links, cross-world payments, content maps and the
 * routing policy (see web4_bridge.h; the reasoning is in docs/WEB4.md). */
#include "web4_bridge.h"

/* ===== Identity link ===== */
int w4_link_digest(const w4_link_t *l, uint8_t d[W4_HASH_LEN])
{
    if (!l || !d) return W4_ERR_ARG;
    w4_hb h;
    w4_hb_init(&h);
    w4_hb_str(&h, "web4/v1/link-record", 32);
    w4_hb_str(&h, l->oidc_iss, sizeof l->oidc_iss);
    w4_hb_str(&h, l->oidc_sub, sizeof l->oidc_sub);
    w4_hb_str(&h, l->oidc_aud, sizeof l->oidc_aud);
    w4_hb_put(&h, l->eth_addr, W4_ETH_ADDR_LEN);
    w4_hb_put(&h, l->eth_pub, 64);
    w4_hb_put(&h, l->agent_id, W4_ID_LEN);
    w4_hb_u64(&h, l->created_ms);
    w4_hb_u64(&h, l->expires_ms);
    return w4_hb_sha3(&h, d) ? W4_OK : W4_ERR_SPACE;
}

int32_t w4_link_eth_message(const uint8_t d[W4_HASH_LEN], char *out, uint32_t cap)
{
    w4_w w;
    w4_w_init(&w, out, cap);
    w4_w_str(&w, "web4 link v1 0x");
    w4_w_hex(&w, d, W4_HASH_LEN);
    return w4_w_cstr(&w);
}

int w4_link_nonce(const uint8_t d[W4_HASH_LEN], char out[44])
{
    return w4_b64url_encode(d, W4_HASH_LEN, out, 44) == 43 ? W4_OK : W4_ERR_SPACE;
}

static int link_eth_hash(const w4_link_t *l, uint8_t h[32])
{
    uint8_t d[W4_HASH_LEN];
    char msg[96];
    if (w4_link_digest(l, d)) return W4_ERR_ARG;
    int32_t n = w4_link_eth_message(d, msg, sizeof msg);
    if (n < 0) return W4_ERR_SPACE;
    w4_eth_personal_hash((const uint8_t *) msg, (uint32_t) n, h);
    return W4_OK;
}

int w4_link_sign_agent(w4_link_t *l, const w4_agent_t *a, const uint8_t *rnd)
{
    if (!l || !a || l->expires_ms <= l->created_ms) return W4_ERR_ARG;
    w4_memcpy(l->agent_id, a->card.agent_id, W4_ID_LEN);
    uint8_t d[W4_HASH_LEN];
    int r = w4_link_digest(l, d);
    if (r) return r;
    w4_sign(a->sk, W4_CTX_LINK, d, W4_HASH_LEN, rnd, l->agent_sig);
    return W4_OK;
}

int w4_link_sign_eth(w4_link_t *l, w4_eth_sign_fn sign, void *sign_ctx)
{
    if (!l || !sign) return W4_ERR_ARG;
    uint8_t h[32];
    int r = link_eth_hash(l, h);
    if (r) return r;
    r = sign(sign_ctx, h, &l->eth_sig);
    if (r) return r;
    return l->eth_sig.recid <= 1 ? W4_OK : W4_ERR_SIG;
}

int w4_link_set_id_token(w4_link_t *l, const char *jwt, uint32_t len)
{
    if (!l || !jwt) return W4_ERR_ARG;
    return w4_strlcpyn(l->id_token, sizeof l->id_token, jwt, len) ? W4_OK : W4_ERR_SPACE;
}

int w4_link_verify(const w4_link_t *l, const uint8_t agent_pk[W4_PK_LEN], const char *jws_alg,
                   w4_jws_verify_fn jws_verify, void *jws_ctx, w4_jws_t *scratch, uint64_t now_ms,
                   uint32_t *failed)
{
    uint32_t bad = 0;
    if (!l || !agent_pk || !failed) return W4_ERR_ARG;
    uint8_t d[W4_HASH_LEN];
    if (w4_link_digest(l, d)) return W4_ERR_ARG;

    /* Web 4: the agent's ML-DSA-65 signature, and the key is the agent's. */
    uint8_t id[W4_ID_LEN];
    w4_key_id(agent_pk, id);
    if (!w4_memeq(id, l->agent_id, W4_ID_LEN) ||
        !w4_verify(agent_pk, W4_CTX_LINK, d, W4_HASH_LEN, l->agent_sig))
        bad |= W4_LINK_WEB4;

    /* Web 3: the address is the key's, and the key personal_signed the digest. */
    uint8_t addr[W4_ETH_ADDR_LEN], h[32];
    w4_eth_address(l->eth_pub, addr);
    if (!w4_memeq(addr, l->eth_addr, W4_ETH_ADDR_LEN) || link_eth_hash(l, h) ||
        w4_secp_verify(l->eth_pub, h, &l->eth_sig, true) != W4_OK)
        bad |= W4_LINK_WEB3;

    /* Web 2: an ID token from the issuer, for this client, about this subject,
     * carrying the digest as its nonce, signed per the host's hook. */
    char nonce[44];
    w4_jwt_claims_t c;
    uint64_t now_s = w4_udiv64(now_ms, 1000u, NULL);
    if (!jws_alg || !jws_verify || !scratch || w4_link_nonce(d, nonce) ||
        w4_jws_parse(l->id_token, (uint32_t) w4_strnlen(l->id_token, sizeof l->id_token),
                     scratch) != W4_OK ||
        w4_jws_verify(scratch, jws_alg, jws_verify, jws_ctx) != W4_OK ||
        w4_jwt_claims(scratch, l->oidc_aud, &c) != W4_OK ||
        w4_jwt_validate(&c, l->oidc_iss, l->oidc_aud, nonce, now_s, 60) != W4_OK ||
        !w4_streq(c.sub, l->oidc_sub))
        bad |= W4_LINK_WEB2;

    *failed = bad;
    if (now_ms < l->created_ms || now_ms >= l->expires_ms) return W4_ERR_EXPIRED;
    return bad ? W4_ERR_SIG : W4_OK;
}

/* ===== Payments ===== */
int w4_pay_digest(const w4_pay_intent_t *p, uint8_t d[W4_HASH_LEN])
{
    if (!p || !d) return W4_ERR_ARG;
    uint8_t amt[32];
    w4_u256_to_be(&p->amount, amt);
    w4_hb h;
    w4_hb_init(&h);
    w4_hb_str(&h, "web4/v1/pay-intent", 32);
    w4_hb_put(&h, p->intent_id, 16);
    w4_hb_put(&h, p->payer_agent, W4_ID_LEN);
    w4_hb_put(&h, p->payee_agent, W4_ID_LEN);
    w4_hb_u8(&h, p->asset);
    w4_hb_u8(&h, p->decimals);
    w4_hb_put(&h, amt, 32);
    w4_hb_u64(&h, p->chain_id);
    w4_hb_put(&h, p->token, W4_ETH_ADDR_LEN);
    w4_hb_put(&h, p->payee_eth, W4_ETH_ADDR_LEN);
    w4_hb_u64(&h, p->created_ms);
    w4_hb_u64(&h, p->expires_ms);
    w4_hb_str(&h, p->memo, sizeof p->memo);
    return w4_hb_sha3(&h, d) ? W4_OK : W4_ERR_SPACE;
}

static bool nonzero(const uint8_t *p, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++)
        if (p[i]) return true;
    return false;
}

static int pay_shape_ok(const w4_pay_intent_t *p)
{
    if (p->expires_ms <= p->created_ms || w4_u256_is_zero(&p->amount)) return W4_ERR_ARG;
    uint64_t v;
    switch (p->asset) {
    case W4_ASSET_VFV:
        if (p->decimals != W4_VFV_MINOR || !w4_u256_to_u64(&p->amount, &v)) return W4_ERR_RANGE;
        return W4_OK;
    case W4_ASSET_ETH:
        if (!p->chain_id || p->decimals != 18 || !nonzero(p->payee_eth, W4_ETH_ADDR_LEN))
            return W4_ERR_ARG;
        return W4_OK;
    case W4_ASSET_ERC20:
        if (!p->chain_id || p->decimals > 77 || !nonzero(p->token, W4_ETH_ADDR_LEN) ||
            !nonzero(p->payee_eth, W4_ETH_ADDR_LEN))
            return W4_ERR_ARG;
        return W4_OK;
    default:
        return W4_ERR_UNSUPP;
    }
}

int w4_pay_sign(w4_pay_intent_t *p, const w4_agent_t *payer, const uint8_t *rnd)
{
    if (!p || !payer) return W4_ERR_ARG;
    if (!w4_memeq(p->payer_agent, payer->card.agent_id, W4_ID_LEN)) return W4_ERR_BINDING;
    int r = pay_shape_ok(p);
    if (r) return r;
    uint8_t d[W4_HASH_LEN];
    r = w4_pay_digest(p, d);
    if (r) return r;
    w4_sign(payer->sk, W4_CTX_PAY, d, W4_HASH_LEN, rnd, p->payer_sig);
    return W4_OK;
}

int w4_vfv_to_token(uint64_t vfv_minor, uint8_t token_decimals, w4_u256 *out)
{
    w4_u256 a;
    w4_u256_from_u64(&a, vfv_minor);
    return w4_u256_rescale(out, &a, W4_VFV_MINOR, token_decimals);
}

int w4_token_to_vfv(const w4_u256 *amount, uint8_t token_decimals, uint64_t *vfv_minor)
{
    w4_u256 t;
    int r = w4_u256_rescale(&t, amount, token_decimals, W4_VFV_MINOR);
    if (r) return r;
    return w4_u256_to_u64(&t, vfv_minor) ? W4_OK : W4_ERR_RANGE;
}

int w4_pay_settle(const w4_pay_intent_t *p, const uint8_t payer_pk[W4_PK_LEN],
                  const w4_consent_t *consent, const uint8_t human_pk[W4_PK_LEN],
                  w4_consent_log_t *log, const w4_rails_t *rails, uint64_t now_ms,
                  w4_settlement_t *out)
{
    if (!p || !payer_pk || !rails || !out) return W4_ERR_ARG;
    w4_memset(out, 0, sizeof *out);
    int r = pay_shape_ok(p);
    if (r) return r;
    if (now_ms < p->created_ms || now_ms >= p->expires_ms) return W4_ERR_EXPIRED;
    uint8_t d[W4_HASH_LEN], id[W4_ID_LEN];
    if (w4_pay_digest(p, d)) return W4_ERR_ARG;
    w4_key_id(payer_pk, id);
    if (!w4_memeq(id, p->payer_agent, W4_ID_LEN)) return W4_ERR_BINDING;
    if (!w4_verify(payer_pk, W4_CTX_PAY, d, W4_HASH_LEN, p->payer_sig)) return W4_ERR_SIG;

    /* Route first, so an intent that cannot settle now keeps its consent. */
    uint8_t world, action;
    bool vfv = p->asset == W4_ASSET_VFV;
    w4_route(vfv ? W4_OP_PAY_VFV : W4_OP_PAY_TOKEN, rails->reach, &world, &action);
    if (vfv && (action != W4_DO_NOW || !rails->ledger)) return W4_ERR_PENDING;
    if (!vfv && !rails->eth_sign) return W4_ERR_UNSUPP;

    /* K2: no money moves without the human's token for exactly this intent.
     * The VFV ceiling is checked for VFV intents; for chain assets the digest
     * already binds the exact asset, chain and amount the human approved. */
    uint64_t vfv_amount = 0;
    if (vfv) w4_u256_to_u64(&p->amount, &vfv_amount);
    if (!consent || !human_pk) return W4_ERR_CONSENT;
    r = w4_consent_check(consent, human_pk, p->payer_agent, d, W4_CONSENT_MONEY, vfv_amount, now_ms,
                         log);
    if (r) return r;

    if (vfv) {
        out->rail = W4_RAIL_VFV;
        w4_memcpy(out->vfv.intent_digest, d, W4_HASH_LEN);
        w4_memcpy(out->vfv.payer, p->payer_agent, W4_ID_LEN);
        w4_memcpy(out->vfv.payee, p->payee_agent, W4_ID_LEN);
        out->vfv.amount_minor = vfv_amount;
        w4_memcpy(out->vfv.consent_nonce, consent->nonce, 16);
        return rails->ledger(rails->ledger_ctx, &out->vfv);
    }

    w4_eth_tx_t tx;
    uint8_t data[68];
    w4_memset(&tx, 0, sizeof tx);
    tx.chain_id = p->chain_id;
    tx.nonce = rails->evm_nonce;
    tx.max_priority = rails->max_priority;
    tx.max_fee = rails->max_fee;
    tx.gas_limit = rails->gas_limit;
    tx.has_to = true;
    if (p->asset == W4_ASSET_ETH) {
        w4_memcpy(tx.to, p->payee_eth, W4_ETH_ADDR_LEN);
        tx.value = p->amount;
    } else {
        w4_memcpy(tx.to, p->token, W4_ETH_ADDR_LEN);
        w4_abi_erc20_transfer(p->payee_eth, &p->amount, data);
        tx.data = data;
        tx.data_len = sizeof data;
    }
    uint8_t un[256], h[32];
    int32_t n = w4_eth_1559_unsigned(&tx, un, sizeof un);
    if (n < 0) return n;
    w4_eth_signing_hash(un, (uint32_t) n, h);
    w4_ecdsa_sig_t sig;
    r = rails->eth_sign(rails->eth_ctx, h, &sig);
    if (r) return r;
    n = w4_eth_1559_signed(&tx, &sig, out->raw_tx, sizeof out->raw_tx);
    if (n < 0) return n;
    out->raw_len = (uint32_t) n;
    w4_eth_tx_hash(out->raw_tx, out->raw_len, out->tx_hash);
    if (w4_rpc_eth_send_raw(out->rpc, sizeof out->rpc, rails->evm_nonce + 1u, out->raw_tx,
                            out->raw_len) < 0)
        return W4_ERR_SPACE;
    out->rail = W4_RAIL_EVM;
    out->broadcast_now = action == W4_DO_NOW;
    return W4_OK;
}

/* ===== Content maps ===== */
static int cmap_digest(const w4_content_map_t *m, uint8_t d[W4_HASH_LEN])
{
    uint8_t cb[IPFSN_CID_BIN_MAX];
    int cl = ipfsn_cid_encode(&m->cid, cb, sizeof cb);
    if (cl <= 0) return W4_ERR_ARG;
    w4_hb h;
    w4_hb_init(&h);
    w4_hb_str(&h, "web4/v1/content-map", 32);
    w4_hb_str(&h, m->url, sizeof m->url);
    w4_hb_u32(&h, (uint32_t) cl);
    w4_hb_put(&h, cb, (uint32_t) cl);
    w4_hb_str(&h, m->content_type, sizeof m->content_type);
    w4_hb_u64(&h, m->fetched_ms);
    w4_hb_put(&h, m->agent_id, W4_ID_LEN);
    return w4_hb_sha3(&h, d) ? W4_OK : W4_ERR_SPACE;
}

int w4_cmap_make(w4_content_map_t *m, const char *url, const uint8_t *content, uint32_t len,
                 const char *content_type, const w4_agent_t *a, uint64_t now_ms, const uint8_t *rnd)
{
    if (!m || !url || !a || (!content && len)) return W4_ERR_ARG;
    w4_memset(m, 0, sizeof *m);
    uint32_t ul = (uint32_t) w4_strnlen(url, sizeof m->url);
    w4_url_t u;
    if (ul >= sizeof m->url || w4_url_parse(url, ul, &u) != W4_OK) return W4_ERR_ARG;
    if (!w4_casecmp_eq(u.scheme, u.scheme_len, "https", 5) &&
        !w4_casecmp_eq(u.scheme, u.scheme_len, "http", 4))
        return W4_ERR_UNSUPP;
    w4_strlcpy(m->url, url, sizeof m->url);
    if (content_type && !w4_strlcpy(m->content_type, content_type, sizeof m->content_type))
        return W4_ERR_ARG;
    if (w4_cid_of(content, len, &m->cid)) return W4_ERR_ARG;
    m->fetched_ms = now_ms;
    w4_memcpy(m->agent_id, a->card.agent_id, W4_ID_LEN);
    uint8_t d[W4_HASH_LEN];
    int r = cmap_digest(m, d);
    if (r) return r;
    w4_sign(a->sk, W4_CTX_CMAP, d, W4_HASH_LEN, rnd, m->sig);
    return W4_OK;
}

int w4_cmap_verify(const w4_content_map_t *m, const uint8_t agent_pk[W4_PK_LEN],
                   const uint8_t *content, uint32_t len)
{
    if (!m || !agent_pk) return W4_ERR_ARG;
    uint8_t id[W4_ID_LEN], d[W4_HASH_LEN];
    w4_key_id(agent_pk, id);
    if (!w4_memeq(id, m->agent_id, W4_ID_LEN)) return W4_ERR_BINDING;
    if (cmap_digest(m, d)) return W4_ERR_ARG;
    if (!w4_verify(agent_pk, W4_CTX_CMAP, d, W4_HASH_LEN, m->sig)) return W4_ERR_SIG;
    if (content && ipfsn_cid_verify(&m->cid, content, len) != IPFSN_OK) return W4_ERR_HASH;
    return W4_OK;
}

/* ===== Routing policy (reasoning per row in docs/WEB4.md) ===== */
static const w4_policy_t POLICY[W4_OP_COUNT] = {
    {W4_OP_LOGIN, W4_WORLD_WEB2, W4_NET_ONLINE, W4_WORLD_WEB4, W4_NET_OFFLINE, W4_DO_REFUSE,
     "Web 2 IdPs own human identity and recovery; a verified link record lets Web 4 recognise "
     "the person without the IdP"},
    {W4_OP_DISCOVER, W4_WORLD_WEB4, W4_NET_LAN, W4_WORLD_NONE, 0, W4_DO_LOCAL,
     "signed manifests over the Carracho DHT work on a LAN; the JSON face serves Web 2 clients; "
     "offline only the cached directory"},
    {W4_OP_AGENT_MSG, W4_WORLD_WEB4, W4_NET_LAN, W4_WORLD_NONE, 0, W4_DO_QUEUE,
     "post-quantum signed envelopes between instances; offline they wait in the outbox"},
    {W4_OP_STREAM, W4_WORLD_WEB4, W4_NET_LAN, W4_WORLD_NONE, 0, W4_DO_REFUSE,
     "a live stream needs a live link; it cannot be queued meaningfully"},
    {W4_OP_FEED_PUBLISH, W4_WORLD_WEB2, W4_NET_ONLINE, W4_WORLD_WEB4, W4_NET_LAN, W4_DO_QUEUE,
     "audience lives on ActivityPub / AT Protocol; LAN peers get the native feed; offline the "
     "post is kept and the mirror queued"},
    {W4_OP_FEED_READ, W4_WORLD_WEB2, W4_NET_ONLINE, W4_WORLD_WEB4, W4_NET_LAN, W4_DO_LOCAL,
     "outside posts come from Web 2 servers; peers may hold mirrored copies"},
    {W4_OP_NOTIFY, W4_WORLD_WEB2, W4_NET_ONLINE, W4_WORLD_WEB4, W4_NET_LAN, W4_DO_QUEUE,
     "webhooks are how Web 2 services expect to be told; a LAN agent gets an envelope instead"},
    {W4_OP_API, W4_WORLD_WEB2, W4_NET_ONLINE, W4_WORLD_NONE, 0, W4_DO_REFUSE,
     "an outside HTTP API exists only online"},
    {W4_OP_PAY_VFV, W4_WORLD_WEB4, W4_NET_LAN, W4_WORLD_NONE, 0, W4_DO_QUEUE,
     "VFV settles on the platform's own rails, fast and without gas; it needs the ledger "
     "(LAN or online); offline the signed intent waits and nothing moves"},
    {W4_OP_PAY_TOKEN, W4_WORLD_WEB3, W4_NET_ONLINE, W4_WORLD_NONE, 0, W4_DO_QUEUE,
     "a chain token moves only on its chain; the transaction can be signed offline and is "
     "broadcast when online"},
    {W4_OP_OWNERSHIP, W4_WORLD_WEB3, W4_NET_ONLINE, W4_WORLD_WEB4, W4_NET_OFFLINE, W4_DO_LOCAL,
     "public verifiable ownership is what chains are for; offline, the last verified record"},
    {W4_OP_SETTLE_FINAL, W4_WORLD_WEB3, W4_NET_ONLINE, W4_WORLD_NONE, 0, W4_DO_QUEUE,
     "a public, final settlement record belongs on a chain; it waits for a connection"},
    {W4_OP_CONTENT_PUBLISH, W4_WORLD_WEB3, W4_NET_OFFLINE, W4_WORLD_NONE, 0, W4_DO_LOCAL,
     "content addressing works with no network at all; the CID is announced when a link "
     "appears and gets a Web 2 gateway URL"},
    {W4_OP_CONTENT_FETCH, W4_WORLD_WEB3, W4_NET_LAN, W4_WORLD_NONE, 0, W4_DO_LOCAL,
     "CID-verified blocks from any peer (LAN or online, with an HTTPS gateway as a carrier); "
     "offline only the local blockstore"},
};

const w4_policy_t *w4_policy(uint8_t op)
{
    return op < W4_OP_COUNT ? &POLICY[op] : NULL;
}

int w4_route(uint8_t op, uint8_t reach, uint8_t *world, uint8_t *action)
{
    const w4_policy_t *p = w4_policy(op);
    if (!p || !world || !action || reach > W4_NET_ONLINE) return W4_ERR_ARG;
    if (reach >= p->primary_min) {
        *world = p->primary;
        *action = reach == W4_NET_OFFLINE ? W4_DO_LOCAL : W4_DO_NOW;
    } else if (p->fallback != W4_WORLD_NONE && reach >= p->fallback_min) {
        *world = p->fallback;
        *action = reach == W4_NET_OFFLINE ? W4_DO_LOCAL : W4_DO_NOW;
    } else {
        *world = p->primary;
        *action = p->otherwise;
    }
    return W4_OK;
}
