/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* edgeclus.h — the edge cluster: several small inference boxes (Raspberry
 * Pi 5, Jetson-class boards, x86_64 mini PCs running headless zxvd) sharing
 * work over Vinea under one per-cycle token budget. Roadmap step R4.5.
 *
 * WHAT THIS IS
 * ------------
 * Every node publishes a signed ADVERT saying what it can do right now. Each
 * node keeps a table of the adverts it has heard and, every cycle, runs the
 * same deterministic functions over it:
 *   - ec_cycle_begin splits the cycle's token budget across the nodes with
 *     the swarm's Fibonacci allocator (swarm_budget.h), capped per node;
 *   - ec_schedule assigns requests to nodes;
 *   - ec_failover moves the work of a node that stopped answering.
 * Same adverts and same clock in, same allotments and assignments out, on
 * every node and every target. Nothing here talks to the network: the host
 * carries the advert bytes over a Vinea session (see TRANSPORT below).
 *
 * THE ADVERT (signed, fixed little-endian layout)
 * -----------------------------------------------
 *   body  = magic "XZEC" u32 | version u8 = 1 | flags u8 | n_models u8 |
 *           reserved u8 = 0 | node id 32 | pow nonce u64 | seq u64 |
 *           issued_ms u64 | ttl_ms u32 | free_mem_mb u32 | queue_depth u32 |
 *           price u64 | n_models x (name 32 | quant u8 | mem_mb u32 |
 *           prefill_mtps u32 | decode_mtps u32)
 *   wire  = body | ML-DSA-87 public key (2592) | ML-DSA-87 signature (4627)
 * The signature (FIPS 204, pqsec's ML-DSA-87 through vna_sign) covers the
 * body under the context string EC_CTX_ADVERT, so an advert signature can
 * never be replayed as any other Vinea object. Rates are in milli-tokens
 * per second (mtps: 4500 = 4.5 tok/s), measured by the node itself; the
 * price is VFV minor units per 1000 tokens and is advisory only here.
 *
 * RULES
 * -----
 *   A1  IDENTITY. The node id is the node's Vinea NodeID: SHA3-256 of the
 *       ML-DSA-87 public key carried in the advert (vna_id.h I2), with the
 *       Vinea proof of work at the cluster's difficulty (I3). A key that
 *       does not hash to the id, a nonce below the difficulty or a bad
 *       signature is refused. Parsing is strict: exact length, known magic,
 *       version, flags and quantisation codes, reserved bytes zero, model
 *       names printable ASCII and zero-padded, no duplicate (name, quant),
 *       every rate above zero, 1 <= ttl_ms <= EC_MAX_TTL_MS.
 *   A2  FRESHNESS. An advert is live from issued_ms until issued_ms + ttl_ms
 *       (exclusive). One issued more than skew_ms in the future, or already
 *       expired, is refused. ec_cluster_expire drops dead entries; every
 *       function that reads the table also ignores them.
 *   A3  ORDER INDEPENDENCE. Per node the table keeps the highest seq. Two
 *       different adverts with the same seq are an equivocation: the one
 *       whose body has the lower SHA3-256 is kept (and the count goes up).
 *       The table is sorted by node id. When it is full, a new node is
 *       admitted only if its id is below the highest id held, which is then
 *       dropped, so the table holds the EC_MAX_NODES lowest ids heard. For a
 *       fixed set of adverts and a fixed `now`, the table, and so every
 *       allotment and assignment, is the same whatever the arrival order.
 *   A4  BUDGET. Each cycle's token total T is split with swarm_budget (the
 *       swarm's Fibonacci rule, reused, not re-implemented): eligible nodes
 *       (live, not failed, not draining) are ranked by their best decode
 *       rate (ties to the lower id) and placed into Fibonacci levels in rank
 *       order: rank 0 at level 0, ranks 1-2 at level 1, ranks 3-5 at level
 *       2, and so on (level d holds F(d+2) nodes). swarm_budget then gives
 *       each level a share F(L-d+1) and splits it equally inside the level.
 *   A5  NO MONOPOLY. No node gets more than cap = floor(T * cap_permille /
 *       1000) tokens in a cycle. Tokens cut off by the cap go to the nodes
 *       under it, in proportion to what they already hold (so the Fibonacci
 *       ratios between them are kept), repeated until nothing is over the
 *       cap. Only when every eligible node is at the cap is anything left
 *       over, and it is reported as `unassigned`, never handed out.
 *   A6  CONSERVATION. sum(alloc) + unassigned == T, exactly, always.
 *       unassigned is 0 whenever (eligible nodes) x cap >= T.
 *   A7  SCHEDULING. Requests are taken in increasing req_id (so the order
 *       the caller lists them in does not matter either). A node fits a
 *       request if it is eligible, has the model loaded (and the requested
 *       quantisation, unless EC_Q_ANY), has the request's working memory
 *       free after this cycle's earlier assignments, has prompt +
 *       max_new_tokens left in its cycle allotment, and its estimate
 *           service = ceil(prompt * 10^6 / prefill_mtps)
 *                   + ceil(max_new * 10^6 / decode_mtps)       (ms)
 *           estimate = service * (1 + queue_depth + assigned this cycle)
 *       meets the latency target (0 = no target). Among fitting nodes the
 *       lowest estimate wins, then the lower price, then the lower node id.
 *       A request nobody fits is left unassigned with the furthest check it
 *       got to (model < memory < budget < latency).
 *   A8  FAILOVER. ec_failover marks a node failed, gives its whole cycle
 *       allotment back to the other eligible nodes by the A5 rule (so A6
 *       still holds), and re-runs A7 for exactly the requests that were on
 *       it, in req_id order. Requests on other nodes do not move. A failed
 *       node comes back only with a newer advert (higher seq) issued after
 *       the failure.
 *   A9  DRAINING. A node that sets EC_F_DRAINING gets no new budget or
 *       work (it is finishing what it has before a shutdown or update).
 *
 * TRANSPORT
 * ---------
 * An advert is at most EC_ADVERT_WIRE_MAX (7663) bytes, larger than a
 * single Vinea datagram body (VNA_PAYLOAD_MAX = 2600) but well inside a
 * Vinea session's plaintext limit (VNA_SESS_MAX_PT = 20000), so the host
 * sends it as one application message on each open session. Because the
 * advert is signed by the node's own Vinea identity, any node can forward
 * it unchanged and every receiver still checks it end to end.
 *
 * HONEST LIMITS
 * -------------
 *   - The rates, free memory and queue depth are what the node says about
 *     itself. The signature proves who said it, not that it is true; a node
 *     that overstates its speed is caught only by its missed latency
 *     targets (ec_failover), not by this module.
 *   - The estimate is a simple linear model; it ignores batching, KV-cache
 *     reuse, thermal throttling (common on a fanless Pi 5) and contention
 *     between models on one board.
 *   - Lowest-id admission (A3) is deterministic but an attacker who grinds
 *     low node ids can crowd out others when the table is full; the Vinea
 *     proof of work is what prices that, so run clusters with pow_bits > 0
 *     on any network you do not control.
 *   - This module is not yet wired into zxvd's RPC or its Vinea node; that
 *     glue lives in kernel/arch/hosted and is a separate step.
 *   - Nothing here has run on a real Pi 5 or Jetson board; it is tested on
 *     x86_64 Linux (and cross-compiled freestanding for aarch64 and i386).
 *
 * Freestanding: integer only, no libc, no allocation, no clock (time is
 * passed in, in milliseconds), no 64-bit division helpers.
 */
#ifndef ZXV_EDGECLUS_H
#define ZXV_EDGECLUS_H

#include <stdint.h>
#include <stdbool.h>
#include "../vinea/vna_id.h"
#include "../swarm/swarm_budget.h"

#define EC_MAX_NODES      32u /* = 1+2+3+5+8+13, six Fibonacci levels */
#define EC_MAX_MODELS     8u  /* models one node advertises */
#define EC_NAME_LEN       32u /* model name, zero-padded */
#define EC_MAX_REQS       256u
#define EC_MAX_TTL_MS     600000u     /* ten minutes */
#define EC_ADVERT_MAGIC   0x43455A58u /* "XZEC" little-endian */
#define EC_ADVERT_VERSION 1u
#define EC_CTX_ADVERT     "zxv-edgeclus/v1/advert"

#define EC_BODY_FIXED 84u
#define EC_MODEL_WIRE 45u
#define EC_ADVERT_WIRE_MAX                                                                         \
    (EC_BODY_FIXED + EC_MAX_MODELS * EC_MODEL_WIRE + VNA_PK_LEN + VNA_SIG_LEN)

#define EC_F_DRAINING 0x01u
#define EC_F_KNOWN    0x01u

/* Quantisation codes (GGUF names). EC_Q_ANY is only valid in a request. */
typedef enum {
    EC_Q_ANY = 0,
    EC_Q4_0 = 1,
    EC_Q4_K_M = 2,
    EC_Q5_K_M = 3,
    EC_Q6_K = 4,
    EC_Q8_0 = 5,
    EC_F16 = 6,
    EC_Q2_K = 7,
    EC_Q3_K_M = 8,
    EC_Q_LAST = 8
} ec_quant_t;

typedef enum {
    EC_OK = 0,
    EC_ERR_ARG = -1,           /* NULL, out of range, or a field breaking A1 */
    EC_ERR_PARSE = -2,         /* malformed advert bytes */
    EC_ERR_SIG = -3,           /* ML-DSA-87 signature does not verify */
    EC_ERR_BINDING = -4,       /* node id != SHA3-256(public key) */
    EC_ERR_POW = -5,           /* proof of work below the cluster difficulty */
    EC_ERR_STALE = -6,         /* seq not newer than the one held */
    EC_ERR_FUTURE = -7,        /* issued more than skew_ms ahead of now */
    EC_ERR_EXPIRED = -8,       /* already past issued_ms + ttl_ms */
    EC_ERR_FULL = -9,          /* table full and the id is not below the highest held */
    EC_ERR_UNKNOWN = -10,      /* node id not in the table */
    EC_ERR_EQUIVOCATION = -11, /* same seq, different body, and this one lost (A3) */
    EC_ERR_CYCLE = -12         /* the table changed since ec_cycle_begin */
} ec_status_t;

/* Why a request was left unassigned (A7), furthest check first reached. */
typedef enum {
    EC_WHY_OK = 0,
    EC_WHY_NO_MODEL = 1,
    EC_WHY_NO_MEMORY = 2,
    EC_WHY_NO_BUDGET = 3,
    EC_WHY_LATENCY = 4
} ec_why_t;

typedef struct {
    char name[EC_NAME_LEN];
    uint8_t quant;         /* ec_quant_t, never EC_Q_ANY */
    uint32_t mem_mb;       /* resident size of the loaded model */
    uint32_t prefill_mtps; /* measured prompt processing, milli-tok/s */
    uint32_t decode_mtps;  /* measured generation, milli-tok/s */
} ec_model_t;

typedef struct {
    vna_id_t node;
    uint64_t pow_nonce;
    uint64_t seq;
    uint64_t issued_ms;
    uint32_t ttl_ms;
    uint32_t free_mem_mb; /* working memory free beyond the loaded models */
    uint32_t queue_depth; /* requests already waiting on the node */
    uint64_t price;       /* VFV minor units per 1000 tokens (advisory) */
    uint8_t flags;
    uint8_t n_models;
    ec_model_t models[EC_MAX_MODELS];
} ec_advert_t;

/* ---- advert codec ---- */
uint32_t ec_advert_body_len(uint32_t n_models);
/* Fill a->node and a->pow_nonce from idn, check every field (A1), and write
 * the signed wire form. rnd: 32 fresh bytes (hedged ML-DSA) or NULL for the
 * deterministic variant. Returns the length written, or -1. */
int32_t ec_advert_seal(ec_advert_t *a, const vna_identity_t *idn, const uint8_t rnd[32],
                       uint8_t *out, uint32_t cap);
/* Parse and verify (A1) without any freshness check. body_hash (may be
 * NULL) receives SHA3-256 of the body. */
ec_status_t ec_advert_open(const uint8_t *buf, uint32_t len, uint32_t pow_bits, ec_advert_t *out,
                           uint8_t body_hash[32]);

/* ---- the advert table ---- */
typedef struct {
    ec_advert_t ad;
    uint8_t body_hash[32];
    uint64_t failed_at;
    bool failed;
} ec_node_t;

typedef struct {
    uint32_t pow_bits;
    uint64_t skew_ms;
    uint32_t cap_permille;         /* A5, 1..1000 */
    ec_node_t nodes[EC_MAX_NODES]; /* [0, n) used, sorted by node id */
    uint32_t n;
    uint64_t gen; /* bumps on every table change */
    uint32_t equivocations;
    swarm_budget_t sb; /* workspace for A4 */
} ec_cluster_t;

ec_status_t ec_cluster_init(ec_cluster_t *c, uint32_t pow_bits, uint64_t skew_ms,
                            uint32_t cap_permille);
/* Expire (A2), verify (A1) and insert (A3) one advert. EC_OK when the table
 * now holds it (or already held these exact bytes). */
ec_status_t ec_cluster_ingest(ec_cluster_t *c, const uint8_t *buf, uint32_t len, uint64_t now_ms);
/* Drop adverts no longer live at now_ms. Returns how many were dropped. */
uint32_t ec_cluster_expire(ec_cluster_t *c, uint64_t now_ms);
/* Index of a node in the table, or -1. */
int32_t ec_cluster_find(const ec_cluster_t *c, const vna_id_t *node);
/* Live, not failed, not draining at now_ms (A2, A8, A9). */
bool ec_node_eligible(const ec_node_t *n, uint64_t now_ms);

/* ---- one cycle ---- */
typedef struct {
    uint64_t now_ms;
    uint64_t gen;
    uint64_t total;
    uint64_t unassigned;          /* A5, A6 */
    uint64_t alloc[EC_MAX_NODES]; /* by table index */
    uint64_t used[EC_MAX_NODES];
    uint32_t mem_used[EC_MAX_NODES];
    uint32_t queued[EC_MAX_NODES];
    uint8_t level[EC_MAX_NODES]; /* A4 level, 0xFF if not eligible */
} ec_cycle_t;

typedef struct {
    uint32_t req_id;
    char model[EC_NAME_LEN];
    uint8_t quant; /* ec_quant_t; EC_Q_ANY accepts any */
    uint32_t prompt_tokens;
    uint32_t max_new_tokens;
    uint32_t kv_mem_mb;  /* working memory on top of the loaded model */
    uint32_t latency_ms; /* target; 0 = none */
} ec_req_t;

typedef struct {
    uint32_t req_id;
    int32_t node;      /* table index, or -1 */
    vna_id_t node_id;  /* zero when unassigned */
    uint8_t model_idx; /* index into that node's models */
    uint8_t why;       /* ec_why_t */
    uint32_t est_ms;   /* A7 estimate, saturating */
} ec_assign_t;

/* A4-A6: allot `total` (<= SWARM_MAX_TOKENS_PER_CYCLE) tokens for the cycle
 * starting at now_ms. Uses c->sb as workspace. */
ec_status_t ec_cycle_begin(ec_cluster_t *c, uint64_t now_ms, uint64_t total, ec_cycle_t *cy);
/* A7: assign n <= EC_MAX_REQS requests (distinct req_id). out[i] answers
 * reqs[i]. Accounting accumulates in cy across calls within a cycle. */
ec_status_t ec_schedule(const ec_cluster_t *c, ec_cycle_t *cy, const ec_req_t *reqs, uint32_t n,
                        ec_assign_t *out);
/* A8: node failed. asg[] is the result of ec_schedule for the same reqs;
 * it is updated in place. *moved (may be NULL) = requests that were on the
 * failed node. */
ec_status_t ec_failover(ec_cluster_t *c, ec_cycle_t *cy, const vna_id_t *node, const ec_req_t *reqs,
                        uint32_t n, ec_assign_t *asg, uint32_t *moved);

#endif /* ZXV_EDGECLUS_H */
