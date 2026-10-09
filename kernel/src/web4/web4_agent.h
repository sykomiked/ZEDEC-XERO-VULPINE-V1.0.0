/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* web4_agent.h — Web 4 itself: assistants (Chiglet instances) talking to
 * each other, and to humans, across instances, post-quantum end to end.
 *
 * IDENTITY
 *   A1  An agent's key is an ML-DSA-65 key pair (FIPS 204, kernel/src/pqsec).
 *       agent_id = SHA3-256(agent public key): the same rule Carracho uses for
 *       a NodeID (carr_id.h I2), so an agent that runs with its node's key has
 *       agent_id == NodeID, and anyone can check the binding from the key.
 *   A2  An agent is BOUND to the peer (Carracho node) that hosts it by a
 *       binding record {agent_id, peer_id, name, time} signed by the node's
 *       ML-DSA-65 key, where peer_id = SHA3-256(node public key). A self-hosted
 *       agent signs its own binding and has peer_id == agent_id.
 *   A3  Every signature carries a context string naming what is signed
 *       (W4_CTX_*), so a signature for one purpose cannot be replayed as another.
 *
 * CAPABILITIES
 *   C1  A manifest lists the tools an agent offers: name, price in VFV minor
 *       units, a rate limit (per minute, with a burst), the most input it takes
 *       and flags (W4_TOOL_MONEY / W4_TOOL_PII / W4_TOOL_STREAM). It is signed
 *       and expires. It has a JSON face for Web 2 discovery.
 *
 * MESSAGES
 *   M1  One envelope shape for REQUEST, RESPONSE, STREAM chunk, CANCEL and
 *       ERROR: from, to, a strictly increasing per-sender sequence number, a
 *       timestamp, request id, chunk index, tool, payload (<= 2048 bytes), an
 *       optional consent token, and an ML-DSA-65 signature over all of it.
 *   M2  Receiving: decode -> addressed to me -> sender known (directory of
 *       verified bindings) -> replay window read-only check -> timestamp
 *       window -> signature -> commit the replay window (only now, so forged
 *       traffic cannot poison it) -> for a REQUEST: tool exists, rate limit,
 *       and the consent rule below.
 *   M3  Carriage: raw bytes over any transport callback, or freight packets
 *       (kernel/src/freight: 168 x 21-byte UBH-168 smart packets per freight,
 *       Reed-Solomon erasure coded, so a LAN or radio link may lose rows).
 *       Offline, envelopes wait in an outbox and are flushed when a link appears.
 *
 * CONSENT (the rule that does not bend)
 *   K1  An agent can never move money or share personal data without a
 *       recorded human confirmation token: an ML-DSA-65 signature by the
 *       HUMAN's key over {human_id, agent_id, action digest, scope, max amount,
 *       validity, nonce}. The action digest binds the exact tool, parties,
 *       request id and payload, so a token for one action is useless for any
 *       other. Tokens are single-use (a bounded log; when it is full, new
 *       consent is refused until expired entries are pruned: fail closed).
 *   K2  Money and personal data are enforced in BOTH places: a remote request
 *       to a W4_TOOL_MONEY / W4_TOOL_PII tool needs a token (w4_rt_receive), and
 *       a local action (a payment in web4_bridge) needs one (w4_consent_check).
 *
 * HONEST LIMITS
 *   - Envelopes are signed, not encrypted. Confidentiality comes from the
 *     carrier (Carracho sessions, or pq_mesh_encapsulate for a single sealed
 *     blob); this module does not add its own encryption layer.
 *   - The consent token proves a key holder signed; that the holder is the
 *     right human, and that they understood what they signed, depends on the
 *     device and UI that showed the action. This module cannot see either.
 *   - Rate limits and replay windows are per receiving process, in memory.
 *   - Interoperability is between ZXV instances only. No other agent protocol
 *     (MCP, A2A, ...) is implemented or claimed here.
 */
#ifndef ZXV_WEB4_AGENT_H
#define ZXV_WEB4_AGENT_H

#include "web4_util.h"
#include "../freight/freight.h"

/* ---- ML-DSA-65 (kernel/src/pqsec/pq_mldsa65.c), re-declared with the exact
 * prototypes of pq_security.h, as kernel/src/carracho/carr_pq.h does, because
 * pq_security.h pulls <complex.h> which a bare-metal target lacks. The tests
 * include both headers in one translation unit, so drift is a compile error. */
#ifndef PQ_MLDSA65_PK_BYTES
#    define PQ_MLDSA65_PK_BYTES  1952
#    define PQ_MLDSA65_SK_BYTES  4032
#    define PQ_MLDSA65_SIG_BYTES 3309
#    define PQ_MLDSA65_CTX_BYTES 255
#endif
void pq_mldsa65_keygen(const uint8_t seed[32], uint8_t pk[PQ_MLDSA65_PK_BYTES],
                       uint8_t sk[PQ_MLDSA65_SK_BYTES]);
void pq_mldsa65_sign(const uint8_t sk[PQ_MLDSA65_SK_BYTES], const uint8_t *msg, uint32_t msg_len,
                     const uint8_t *ctx, uint32_t ctx_len, const uint8_t rnd[32],
                     uint8_t sig[PQ_MLDSA65_SIG_BYTES]);
bool pq_mldsa65_verify(const uint8_t pk[PQ_MLDSA65_PK_BYTES], const uint8_t *msg, uint32_t msg_len,
                       const uint8_t *ctx, uint32_t ctx_len,
                       const uint8_t sig[PQ_MLDSA65_SIG_BYTES]);

#define W4_PK_LEN  PQ_MLDSA65_PK_BYTES
#define W4_SK_LEN  PQ_MLDSA65_SK_BYTES
#define W4_SIG_LEN PQ_MLDSA65_SIG_BYTES
#define W4_ID_LEN  32u

#define W4_CTX_BIND     "web4/v1/agent-bind"
#define W4_CTX_MANIFEST "web4/v1/manifest"
#define W4_CTX_ENV      "web4/v1/envelope"
#define W4_CTX_CONSENT  "web4/v1/consent"
#define W4_CTX_LINK     "web4/v1/link"
#define W4_CTX_PAY      "web4/v1/pay-intent"
#define W4_CTX_CMAP     "web4/v1/content-map"

void w4_sign(const uint8_t sk[W4_SK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
             const uint8_t rnd[32], uint8_t sig[W4_SIG_LEN]);
bool w4_verify(const uint8_t pk[W4_PK_LEN], const char *ctx, const uint8_t *msg, uint32_t len,
               const uint8_t sig[W4_SIG_LEN]);
/* SHA3-256 of a public key: an agent_id, peer_id or human_id. */
void w4_key_id(const uint8_t pk[W4_PK_LEN], uint8_t id[W4_ID_LEN]);

/* ======================================================================
 * Identity
 * ====================================================================== */
#define W4_NAME_MAX 32u

typedef struct {
    uint8_t agent_id[W4_ID_LEN];
    uint8_t peer_id[W4_ID_LEN];
    char name[W4_NAME_MAX];
    uint64_t bound_ms;
    uint8_t pk[W4_PK_LEN];
    bool self_bound;            /* peer_id == agent_id */
    uint8_t node_pk[W4_PK_LEN]; /* only when !self_bound */
    uint8_t bind_sig[W4_SIG_LEN];
} w4_card_t; /* what a peer needs to verify this agent */

typedef struct {
    w4_card_t card;
    uint8_t sk[W4_SK_LEN];
    uint64_t next_seq; /* strictly increasing envelope sequence */
} w4_agent_t;

/* Create an agent from a 32-byte seed. node_pk/node_sk NULL: self-bound.
 * rnd: 32 bytes for the hedged ML-DSA signature (NULL: deterministic). */
int w4_agent_create(w4_agent_t *a, const uint8_t seed[32], const char *name, const uint8_t *node_pk,
                    const uint8_t *node_sk, uint64_t now_ms, const uint8_t *rnd);
/* Check a card: agent_id = H(pk), peer_id = H(node key), binding signature. */
int w4_card_verify(const w4_card_t *c);

/* Directory of verified peers (caller storage). */
typedef struct {
    w4_card_t *e;
    uint32_t cap, n;
} w4_dir_t;
void w4_dir_init(w4_dir_t *d, w4_card_t *storage, uint32_t cap);
/* Verifies, then adds or replaces. */
int w4_dir_add(w4_dir_t *d, const w4_card_t *c);
const w4_card_t *w4_dir_find(const w4_dir_t *d, const uint8_t agent_id[W4_ID_LEN]);

/* ======================================================================
 * Capability manifest
 * ====================================================================== */
#define W4_MAX_TOOLS     16u
#define W4_TOOL_NAME_MAX 32u
#define W4_TOOL_MONEY    0x01u /* the tool moves money                      */
#define W4_TOOL_PII      0x02u /* the tool reveals personal data            */
#define W4_TOOL_STREAM   0x04u /* answers with STREAM chunks                */
#define W4_VFV_MINOR     2u    /* VFV minor-unit digits (pay_platform_default) */

typedef struct {
    char name[W4_TOOL_NAME_MAX];
    uint64_t price_vfv;    /* VFV minor units per call */
    uint32_t rate_per_min; /* sustained calls per minute per caller, >= 1 */
    uint32_t burst;        /* bucket size, >= 1 */
    uint32_t flags;        /* W4_TOOL_* */
    uint32_t max_input;    /* payload bytes accepted, <= W4_MSG_MAX_PAYLOAD */
} w4_tool_t;

typedef struct {
    uint8_t agent_id[W4_ID_LEN];
    uint8_t peer_id[W4_ID_LEN];
    uint32_t version;
    uint64_t issued_ms, expires_ms;
    uint32_t ntools;
    w4_tool_t tools[W4_MAX_TOOLS];
    uint8_t sig[W4_SIG_LEN];
} w4_manifest_t;

#define W4_MANIFEST_MAX (128u + W4_MAX_TOOLS * (1u + W4_TOOL_NAME_MAX + 24u) + W4_SIG_LEN)

int w4_manifest_add_tool(w4_manifest_t *m, const char *name, uint64_t price_vfv,
                         uint32_t rate_per_min, uint32_t burst, uint32_t flags, uint32_t max_input);
int w4_manifest_sign(const w4_agent_t *a, w4_manifest_t *m, const uint8_t *rnd);
int w4_manifest_verify(const w4_manifest_t *m, const uint8_t pk[W4_PK_LEN], uint64_t now_ms);
int32_t w4_manifest_encode(const w4_manifest_t *m, uint8_t *out, uint32_t cap);
int w4_manifest_decode(const uint8_t *buf, uint32_t len, w4_manifest_t *m);
/* Web 2 discovery face (served e.g. at /.well-known/web4-agent.json). */
int32_t w4_manifest_json(const w4_manifest_t *m, char *out, uint32_t cap);
int32_t w4_manifest_find(const w4_manifest_t *m, const char *tool);

/* ======================================================================
 * Consent
 * ====================================================================== */
#define W4_CONSENT_MONEY 0x01u
#define W4_CONSENT_PII   0x02u

typedef struct {
    uint8_t human_id[W4_ID_LEN]; /* SHA3-256(human's ML-DSA public key) */
    uint8_t agent_id[W4_ID_LEN]; /* the agent allowed to act */
    uint8_t action[W4_HASH_LEN]; /* what exactly is allowed */
    uint32_t scope;              /* W4_CONSENT_* */
    uint64_t max_vfv;            /* money: ceiling in VFV minor units */
    uint64_t issued_ms, expires_ms;
    uint8_t nonce[16];
    uint8_t sig[W4_SIG_LEN];
} w4_consent_t;

#define W4_CONSENT_BYTES (4u + 3u * 32u + 4u + 3u * 8u + 16u + W4_SIG_LEN)

/* Issued on the HUMAN's device after showing them the action. */
int w4_consent_issue(const uint8_t human_pk[W4_PK_LEN], const uint8_t human_sk[W4_SK_LEN],
                     const uint8_t agent_id[W4_ID_LEN], const uint8_t action[W4_HASH_LEN],
                     uint32_t scope, uint64_t max_vfv, uint64_t issued_ms, uint64_t expires_ms,
                     const uint8_t nonce[16], const uint8_t *rnd, w4_consent_t *out);

/* Single-use log (caller storage). */
typedef struct {
    uint8_t key[W4_HASH_LEN];
    uint64_t expires_ms;
} w4_consent_used_t;
typedef struct {
    w4_consent_used_t *e;
    uint32_t cap, n;
} w4_consent_log_t;
void w4_consent_log_init(w4_consent_log_t *l, w4_consent_used_t *storage, uint32_t cap);
/* Drop entries whose token has expired (they can no longer be presented). */
void w4_consent_log_prune(w4_consent_log_t *l, uint64_t now_ms);

/* Check a token for `agent_id` doing `action` within `need` scope for `amount`
 * VFV minor units (money), then record it as used. W4_OK or W4_ERR_CONSENT /
 * W4_ERR_EXPIRED / W4_ERR_REPLAY / W4_ERR_SIG / W4_ERR_SPACE (log full). */
int w4_consent_check(const w4_consent_t *t, const uint8_t human_pk[W4_PK_LEN],
                     const uint8_t agent_id[W4_ID_LEN], const uint8_t action[W4_HASH_LEN],
                     uint32_t need, uint64_t amount, uint64_t now_ms, w4_consent_log_t *log);

/* ======================================================================
 * Envelopes
 * ====================================================================== */
enum {
    W4_MSG_REQUEST = 1,
    W4_MSG_RESPONSE = 2,
    W4_MSG_STREAM = 3,
    W4_MSG_CANCEL = 4,
    W4_MSG_ERROR = 5
};
#define W4_MF_FINAL        0x01u /* last STREAM chunk / final response */
#define W4_MF_CONSENT      0x02u /* a consent token is attached */
#define W4_MSG_MAX_PAYLOAD 2048u
#define W4_ENV_MAX         (160u + W4_MSG_MAX_PAYLOAD + W4_CONSENT_BYTES + W4_SIG_LEN)

typedef struct {
    uint8_t kind, flags;
    uint8_t from[W4_ID_LEN], to[W4_ID_LEN];
    uint64_t seq, ts_ms, req_id;
    uint32_t chunk;
    char tool[W4_TOOL_NAME_MAX];
    uint32_t payload_len;
    uint8_t payload[W4_MSG_MAX_PAYLOAD];
    w4_consent_t consent;
    uint8_t sig[W4_SIG_LEN];
} w4_env_t;

/* The digest a consent token for this request must carry:
 * SHA3-256("web4/v1/action" || tool || from || to || req_id || payload). */
void w4_action_digest(const char *tool, const uint8_t from[W4_ID_LEN], const uint8_t to[W4_ID_LEN],
                      uint64_t req_id, const uint8_t *payload, uint32_t len,
                      uint8_t out[W4_HASH_LEN]);

/* Fill an envelope (no signature yet). */
int w4_env_make(w4_env_t *e, uint8_t kind, const uint8_t from[W4_ID_LEN],
                const uint8_t to[W4_ID_LEN], uint64_t req_id, uint32_t chunk, const char *tool,
                const uint8_t *payload, uint32_t len);
/* The reply skeleton for a request (to/from swapped, same req_id and tool). */
int w4_env_reply(w4_env_t *r, const w4_env_t *req, uint8_t kind, uint32_t chunk,
                 const uint8_t *payload, uint32_t len);
/* Assign seq and ts, sign with the agent key and encode. Returns length. */
int32_t w4_env_seal(w4_agent_t *a, w4_env_t *e, uint64_t now_ms, const uint8_t *rnd, uint8_t *out,
                    uint32_t cap);
int w4_env_decode(const uint8_t *buf, uint32_t len, w4_env_t *e, uint32_t *signed_len);

/* ======================================================================
 * Receiving runtime
 * ====================================================================== */
typedef struct {
    uint8_t peer[W4_ID_LEN];
    uint64_t hi, bitmap, last_used;
    bool used;
} w4_replay_ent_t;

typedef struct {
    uint8_t peer[W4_ID_LEN];
    uint32_t tool;
    uint64_t milli_tokens; /* tokens * 1000 */
    uint64_t last_ms;
    bool used;
} w4_rate_ent_t;

/* Principal lookup: the ML-DSA public key of a human, by human_id. */
typedef const uint8_t *(*w4_human_pk_fn)(void *ctx, const uint8_t human_id[W4_ID_LEN]);

typedef struct {
    const w4_agent_t *self;
    const w4_manifest_t *manifest; /* own tools (requests are checked against it) */
    const w4_dir_t *dir;
    w4_replay_ent_t *replay;
    uint32_t replay_cap;
    uint64_t evict_floor_ms;
    w4_rate_ent_t *rate;
    uint32_t rate_cap;
    w4_consent_log_t *consent_log;
    w4_human_pk_fn human_pk;
    void *human_ctx;
    uint64_t window_ms; /* accepted clock skew / age, both directions */
    uint64_t tick;
} w4_rt_t;

void w4_rt_init(w4_rt_t *rt, const w4_agent_t *self, const w4_manifest_t *manifest,
                const w4_dir_t *dir, w4_replay_ent_t *replay, uint32_t replay_cap,
                w4_rate_ent_t *rate, uint32_t rate_cap, w4_consent_log_t *log,
                w4_human_pk_fn human_pk, void *human_ctx, uint64_t window_ms);

/* The M2 pipeline. On W4_OK *out holds the verified envelope. Errors:
 * W4_ERR_PARSE, W4_ERR_NOTFOUND (not for me / unknown sender / unknown tool),
 * W4_ERR_STALE, W4_ERR_REPLAY, W4_ERR_SIG, W4_ERR_RATE, W4_ERR_CONSENT, ...
 * The replay window is committed only after the signature verified; a request
 * refused for rate or consent still consumed its sequence number. */
int w4_rt_receive(w4_rt_t *rt, const uint8_t *buf, uint32_t len, uint64_t now_ms, w4_env_t *out);

/* ======================================================================
 * Carriage: transport callback, freight packets, offline outbox
 * ====================================================================== */
enum { W4_NET_OFFLINE = 0, W4_NET_LAN = 1, W4_NET_ONLINE = 2 };

typedef struct {
    void *ctx;
    /* deliver one frame to a peer; W4_OK or negative */
    int (*send)(void *ctx, const uint8_t peer_id[W4_ID_LEN], const uint8_t *frame, uint32_t len);
    uint8_t reach;     /* W4_NET_* currently available */
    bool freight;      /* frame envelopes as freight sets */
    uint8_t freight_k; /* data rows per freight (1..168); 168 - k rows may be lost */
} w4_transport_t;

/* One freight frame: [64-byte freight header][n x 21-byte smart packets]. */
#define W4_FRAME_MAX (FREIGHT_HEADER_BYTES + FREIGHT_BYTES)

/* Send bytes to a peer: raw, or as freight frames. */
int w4_transport_send(const w4_transport_t *t, const uint8_t peer_id[W4_ID_LEN],
                      const uint8_t *bytes, uint32_t len, uint64_t freight_id);

typedef struct {
    freight_join_t j;
    uint8_t buf[W4_ENV_MAX];
    uint8_t work[FREIGHT_DECODE_WORK_BYTES];
} w4_freight_rx_t;
void w4_freight_rx_init(w4_freight_rx_t *rx);
/* Feed one frame (possibly with rows missing). W4_OK and *len when the set is
 * complete (bytes in rx->buf), W4_ERR_PENDING while incomplete. */
int w4_freight_rx_add(w4_freight_rx_t *rx, const uint8_t *frame, uint32_t flen, uint32_t *len);

typedef struct {
    bool used;
    uint8_t peer[W4_ID_LEN];
    uint32_t len;
    uint8_t data[W4_ENV_MAX];
} w4_outbox_slot_t;
typedef struct {
    w4_outbox_slot_t *s;
    uint32_t cap;
} w4_outbox_t;
void w4_outbox_init(w4_outbox_t *ob, w4_outbox_slot_t *storage, uint32_t cap);
/* Send now when the transport is up, else queue (W4_ERR_PENDING), or
 * W4_ERR_SPACE when the outbox is full. */
int w4_post(const w4_transport_t *t, w4_outbox_t *ob, const uint8_t peer_id[W4_ID_LEN],
            const uint8_t *bytes, uint32_t len, uint64_t freight_id);
/* Flush queued envelopes; returns how many were sent. */
uint32_t w4_outbox_flush(const w4_transport_t *t, w4_outbox_t *ob, uint64_t freight_id_base);
uint32_t w4_outbox_count(const w4_outbox_t *ob);

#endif /* ZXV_WEB4_AGENT_H */
