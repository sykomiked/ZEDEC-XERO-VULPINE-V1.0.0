/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_bridge.h — Web 4 as the bridge between Web 2 and Web 3, each used
 * for what it is good at.
 *
 *   LINK      one person/agent across the three worlds: an OIDC subject (Web 2),
 *             an Ethereum address (Web 3) and a Web 4 agent, in one record that
 *             ALL THREE sign:
 *               Web 4  ML-DSA-65 by the agent over the record digest;
 *               Web 3  EIP-191 personal_sign (secp256k1) by the address over
 *                      "web4 link v1 0x<digest hex>";
 *               Web 2  an OIDC ID token from the issuer whose `nonce` claim is
 *                      base64url(digest): the standard OIDC way to bind a login
 *                      to a value the client chose. Its JWS signature is checked
 *                      through the host hook (HS256 in-module).
 *             A verifier reports which of the three failed (bit mask).
 *   PAY       a cross-world payment intent with exact integer amounts (uint256
 *             base units, never floating point), signed by the payer agent
 *             (ML-DSA-65) and released only with a human consent token whose
 *             action digest is the intent digest. It settles on VFV rails (a
 *             host ledger hook, e.g. kernel/src/pay) or becomes an EIP-1559
 *             transaction (native value or ERC-20 transfer) for a bridged token.
 *             VFV <-> token amounts are rescaled exactly or refused.
 *   CONTENT   an HTTP(S) URL mapped to the CIDv1 of the bytes it served, signed
 *             by the agent that fetched them, so Web 2 content becomes content
 *             addressed (and a CID gets a Web 2 gateway URL back).
 *   POLICY    a table that routes each operation to the world best suited to
 *             it, with what to do on a LAN or offline (docs/WEB4.md explains
 *             every row).
 *
 * HONEST LIMITS
 *   - The link proves three keys signed one record. It does not prove the OIDC
 *     issuer is trustworthy, that the wallet is not shared, or anything about
 *     the person beyond what each world itself asserts.
 *   - The Web 3 signature is secp256k1 (classical). The record also carries the
 *     ML-DSA-65 signature, so the Web 4 side stays post-quantum; the Web 3 leg
 *     is only as strong as Ethereum's own.
 *   - Bridged tokens: this module builds and signs the transaction; whether a
 *     given ERC-20 really is a 1:1 VFV representation, and who custodies the
 *     reserve, is outside this code (and is a legal question as much as a
 *     technical one). No contract is deployed or assumed.
 *   - Settlement finality is whatever the chosen rail provides; a broadcast
 *     EVM transaction is not final until the chain says so.
 */
#ifndef ZXV_WEB4_BRIDGE_H
#define ZXV_WEB4_BRIDGE_H

#include "web4_agent.h"
#include "web4_web2.h"
#include "web4_web3.h"

/* ======================================================================
 * Identity link
 * ====================================================================== */
#define W4_LINK_WEB2   0x01u
#define W4_LINK_WEB3   0x02u
#define W4_LINK_WEB4   0x04u
#define W4_LINK_ALL    0x07u
#define W4_IDTOKEN_MAX 6144u

typedef struct {
    char oidc_iss[192];
    char oidc_sub[192];
    char oidc_aud[192]; /* the client_id the ID token is for */
    uint8_t eth_addr[W4_ETH_ADDR_LEN];
    uint8_t eth_pub[64];
    uint8_t agent_id[W4_ID_LEN];
    uint64_t created_ms, expires_ms;
    /* signatures */
    uint8_t agent_sig[W4_SIG_LEN];
    w4_ecdsa_sig_t eth_sig;
    char id_token[W4_IDTOKEN_MAX];
} w4_link_t;

/* SHA3-256 over the canonical identity fields (not the signatures). */
int w4_link_digest(const w4_link_t *l, uint8_t d[W4_HASH_LEN]);
/* The exact text the wallet signs with personal_sign. */
int32_t w4_link_eth_message(const uint8_t d[W4_HASH_LEN], char *out, uint32_t cap);
/* The nonce the OIDC login must carry (43 chars + NUL). */
int w4_link_nonce(const uint8_t d[W4_HASH_LEN], char out[44]);

int w4_link_sign_agent(w4_link_t *l, const w4_agent_t *a, const uint8_t *rnd);
int w4_link_sign_eth(w4_link_t *l, w4_eth_sign_fn sign, void *sign_ctx);
int w4_link_set_id_token(w4_link_t *l, const char *jwt, uint32_t len);

/* Verify all three legs. *failed gets the W4_LINK_* bits that did not verify
 * (0 on success). jws_alg is the algorithm the host expects from this issuer;
 * `scratch` is caller memory for the parsed token. */
int w4_link_verify(const w4_link_t *l, const uint8_t agent_pk[W4_PK_LEN], const char *jws_alg,
                   w4_jws_verify_fn jws_verify, void *jws_ctx, w4_jws_t *scratch, uint64_t now_ms,
                   uint32_t *failed);

/* ======================================================================
 * Payments
 * ====================================================================== */
enum { W4_ASSET_VFV = 1, W4_ASSET_ETH = 2, W4_ASSET_ERC20 = 3 };
enum { W4_RAIL_VFV = 1, W4_RAIL_EVM = 2 };

typedef struct {
    uint8_t intent_id[16];
    uint8_t payer_agent[W4_ID_LEN];
    uint8_t payee_agent[W4_ID_LEN];
    uint8_t asset;                  /* W4_ASSET_* */
    uint8_t decimals;               /* of `amount`: W4_VFV_MINOR for VFV, 18 for ETH, token's own */
    w4_u256 amount;                 /* exact, in base units of the asset */
    uint64_t chain_id;              /* EVM assets */
    uint8_t token[W4_ETH_ADDR_LEN]; /* ERC-20 contract */
    uint8_t payee_eth[W4_ETH_ADDR_LEN]; /* EVM recipient */
    uint64_t created_ms, expires_ms;
    char memo[64];
    uint8_t payer_sig[W4_SIG_LEN];
} w4_pay_intent_t;

int w4_pay_digest(const w4_pay_intent_t *p, uint8_t d[W4_HASH_LEN]);
int w4_pay_sign(w4_pay_intent_t *p, const w4_agent_t *payer, const uint8_t *rnd);

/* VFV minor units <-> token base units, exact (W4_ERR_RANGE otherwise). */
int w4_vfv_to_token(uint64_t vfv_minor, uint8_t token_decimals, w4_u256 *out);
int w4_token_to_vfv(const w4_u256 *amount, uint8_t token_decimals, uint64_t *vfv_minor);

/* What a VFV ledger (the host, e.g. kernel/src/pay) is asked to post. */
typedef struct {
    uint8_t intent_digest[W4_HASH_LEN];
    uint8_t payer[W4_ID_LEN], payee[W4_ID_LEN];
    uint64_t amount_minor;
    uint8_t consent_nonce[16];
} w4_vfv_settle_t;
typedef int (*w4_vfv_ledger_fn)(void *ctx, const w4_vfv_settle_t *s);

typedef struct {
    w4_vfv_ledger_fn ledger;
    void *ledger_ctx;
    w4_eth_sign_fn eth_sign; /* w4_secp_signer with a key, or a host wallet / HSM */
    void *eth_ctx;
    uint64_t evm_nonce;
    w4_u256 max_priority, max_fee;
    uint64_t gas_limit;
    uint8_t reach; /* W4_NET_* */
} w4_rails_t;

typedef struct {
    uint8_t rail;
    bool broadcast_now; /* EVM: online, send rpc now; else keep and send later */
    w4_vfv_settle_t vfv;
    uint8_t raw_tx[512];
    uint32_t raw_len;
    uint8_t tx_hash[32];
    char rpc[1400]; /* eth_sendRawTransaction body */
} w4_settlement_t;

/* Verify the intent (payer signature, validity, amounts), check and consume
 * the human consent token (action = intent digest, scope MONEY; the VFV
 * ceiling applies to VFV intents), then settle on the routed rail.
 * W4_ERR_PENDING: a VFV intent with no ledger reachable (nothing moved). */
int w4_pay_settle(const w4_pay_intent_t *p, const uint8_t payer_pk[W4_PK_LEN],
                  const w4_consent_t *consent, const uint8_t human_pk[W4_PK_LEN],
                  w4_consent_log_t *log, const w4_rails_t *rails, uint64_t now_ms,
                  w4_settlement_t *out);

/* ======================================================================
 * Content bridging
 * ====================================================================== */
typedef struct {
    char url[512];
    ipfsn_cid_t cid;
    char content_type[64];
    uint64_t fetched_ms;
    uint8_t agent_id[W4_ID_LEN];
    uint8_t sig[W4_SIG_LEN];
} w4_content_map_t;

/* Hash the bytes served at url into a CIDv1 and sign the mapping. */
int w4_cmap_make(w4_content_map_t *m, const char *url, const uint8_t *content, uint32_t len,
                 const char *content_type, const w4_agent_t *a, uint64_t now_ms,
                 const uint8_t *rnd);
/* Verify the signature; with content != NULL also that the bytes match. */
int w4_cmap_verify(const w4_content_map_t *m, const uint8_t agent_pk[W4_PK_LEN],
                   const uint8_t *content, uint32_t len);

/* ======================================================================
 * Routing policy
 * ====================================================================== */
enum { W4_WORLD_NONE = 0, W4_WORLD_WEB2 = 2, W4_WORLD_WEB3 = 3, W4_WORLD_WEB4 = 4 };
enum {
    W4_OP_LOGIN = 0,       /* who is this human                          */
    W4_OP_DISCOVER,        /* find an agent and its capabilities         */
    W4_OP_AGENT_MSG,       /* agent <-> agent request / response         */
    W4_OP_STREAM,          /* live stream between agents                 */
    W4_OP_FEED_PUBLISH,    /* publish a social post                      */
    W4_OP_FEED_READ,       /* read posts from people outside the platform */
    W4_OP_NOTIFY,          /* push an event to an outside service        */
    W4_OP_API,             /* call an outside HTTP API                   */
    W4_OP_PAY_VFV,         /* pay in VFV                                 */
    W4_OP_PAY_TOKEN,       /* pay in a bridged / chain token             */
    W4_OP_OWNERSHIP,       /* prove who owns an asset                    */
    W4_OP_SETTLE_FINAL,    /* final, public settlement record            */
    W4_OP_CONTENT_PUBLISH, /* publish bytes                              */
    W4_OP_CONTENT_FETCH,   /* fetch bytes                                */
    W4_OP_COUNT
};
enum { W4_DO_NOW = 1, W4_DO_QUEUE = 2, W4_DO_LOCAL = 3, W4_DO_REFUSE = 4 };

typedef struct {
    uint8_t op;
    uint8_t primary, primary_min;   /* world, and the reach it needs */
    uint8_t fallback, fallback_min; /* used when primary is out of reach */
    uint8_t otherwise;              /* W4_DO_* when neither is reachable */
    const char *why;
} w4_policy_t;

const w4_policy_t *w4_policy(uint8_t op);
/* Decide where `op` runs at the current reach. */
int w4_route(uint8_t op, uint8_t reach, uint8_t *world, uint8_t *action);

#endif /* ZXV_WEB4_BRIDGE_H */
