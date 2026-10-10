/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* peer_audit.h — two-layer verification for the decentralised swarm.
 *
 * LAYER 1: INTERNAL SELF-AUDIT (node level, before anything is signed)
 *   pa_precommit_check runs local invariants over a transition the node is
 *   about to sign and emit: value conservation (after == before + minted -
 *   burned, computed without overflow), token budget (requested <= remaining),
 *   ISF headroom (h_t <= 0 means stop) and size bounds. On any failure the
 *   transition is NOT signed, the node enters PA_NODE_CONTRADICTION (the
 *   paraconsistent "both" state, value-equal to LPRES_STATE_BOTH in lpres.h)
 *   and every output it would emit is forced to S0 (no effect), as in the
 *   swarm_logic.h L7 paradox trap. Only an explicit pa_node_release clears it.
 *   pa_emit_record is the only path to a signed record and always runs the
 *   self-audit first.
 *
 * LAYER 2: EXTERNAL PEER AUDIT (network level, zero trust)
 *   A. Deterministic replay. A transition record carries {kind, node, seq,
 *      prev_state_hash, inputs, claimed outputs, new_state_hash} and the
 *      node's ML-DSA-65 signature. A verifier replays it through its own
 *      runtime (a pa_replay_fn: pa_runtime.h wires pay_ledger transfers and
 *      swarm_budget allocation, both integer and deterministic) and compares
 *      outputs and the new state hash bit for bit. Committee: k witnesses
 *      drawn by a SHA-256 stream seeded with the record hash from the epoch's
 *      peer list, author excluded, so nobody picks their own auditors.
 *      Quorum: 2f+1 matching signed votes out of a 3f+1 committee.
 *   B. Challenge injection. A canary is an ordinary pa_request_t (same
 *      format and size as a real request; the canary mark stays in the
 *      challenger's own table) with a boundary input. The challenger knows
 *      the correct fail-closed answer from its own replay; a signed answer
 *      that differs fails the challenge and is itself evidence. Canaries are
 *      rate-limited per target per tick window.
 *   C. Isolation, never confiscation. Verified misbehaviour yields a
 *      pa_evidence_t: the accused's own signed record plus the accuser's
 *      signed replay result. Any third party re-checks it by its own replay.
 *      Valid evidence: the accused is blocklisted locally and the host's
 *      revoke callback is called (capability tokens, port seals, routing).
 *      Evidence that does not verify is rejected and counts against the
 *      accuser (it signed it). Nothing in this module moves, freezes or
 *      seizes funds: isolation only.
 *   Cost control: every ledger/state-machine record is replayed; model
 *   inference is committed (SHA-256 over model CID, seed, prompt, tokens) and
 *   spot-checked at a configurable rate, the sample decided by a beacon that
 *   must not be known when the commitment is made.
 *
 * HONEST LIMITS: this is the verification logic only. Moving records, votes,
 * canaries and evidence between machines (the vinea / app transport hook) is
 * not wired here; the host does it. Collusion of more than f committee
 * members, and Sybil identities, are out of scope (identity cost is ident's
 * job). See docs/PEER_AUDIT.md.
 *
 * Freestanding: integer C11, no libc, no allocation, no floating point, no
 * 64-bit division; all tables are fixed size and fail closed when full.
 */
#ifndef ZXV_PEER_AUDIT_H
#define ZXV_PEER_AUDIT_H

#include <stdint.h>
#include <stdbool.h>
#include "pa_pq.h"

#define PA_HASH_LEN      32u
#define PA_PK_LEN        PQ_MLDSA65_PK_BYTES
#define PA_SK_LEN        PQ_MLDSA65_SK_BYTES
#define PA_SIG_LEN       PQ_MLDSA65_SIG_BYTES
#define PA_MAX_IO        128u /* inputs / outputs bytes per record */
#define PA_MAX_PEERS     32u
#define PA_MAX_COMMITTEE 31u /* 3f+1 with f <= 10 */
#define PA_MAX_EVENTS    32u
#define PA_MAX_CANARIES  32u
#define PA_REQ_NONCE_LEN 16u

/* ML-DSA-65 domain separators, one per signed object type. */
#define PA_CTX_RECORD   "zxv/peer-audit/v1/record"
#define PA_CTX_VOTE     "zxv/peer-audit/v1/vote"
#define PA_CTX_EVIDENCE "zxv/peer-audit/v1/evidence"

typedef enum {
    PA_OK = 0,
    PA_ERR_ARG = -1,
    PA_ERR_FULL = -2,
    PA_ERR_CONSERVATION = -3, /* after != before + minted - burned */
    PA_ERR_OVERFLOW = -4,
    PA_ERR_BUDGET = -5,   /* tokens requested > remaining */
    PA_ERR_HEADROOM = -6, /* ISF headroom h_t <= 0: stop */
    PA_ERR_SIZE = -7,
    PA_ERR_QUARANTINED = -8, /* node is in the contradiction state */
    PA_ERR_SIG = -9,
    PA_ERR_UNKNOWN_PEER = -10,
    PA_ERR_BLOCKED = -11,
    PA_ERR_MISMATCH = -12, /* replay differs from the claim */
    PA_ERR_NO_STATE = -13, /* verifier's replica is not at prev_state_hash */
    PA_ERR_RATE = -14,
    PA_ERR_NOT_COMMITTEE = -15,
    PA_ERR_FALSE_ACCUSATION = -16,
    PA_ERR_INCONCLUSIVE = -17,
    PA_ERR_KIND = -18,
    PA_ERR_INVARIANT = -19, /* a runtime-specific invariant failed */
    PA_ERR_NOT_FOUND = -20
} pa_status_t;

/* Node states. Values equal lpres_state_t (lpres.h): ACTIVE is S+ (TRUE),
 * CONTRADICTION is BOTH. A contradicted node emits only S0 (nothing). */
typedef enum { PA_NODE_ACTIVE = 1, PA_NODE_CONTRADICTION = 3 } pa_node_state_t;

typedef enum { PA_KIND_LEDGER_TRANSFER = 1, PA_KIND_BUDGET = 2, PA_KIND_INFERENCE = 3 } pa_kind_t;

typedef enum {
    PA_VERDICT_UNDECIDED = 0,
    PA_VERDICT_ACCEPT = 1,
    PA_VERDICT_REJECT = 2
} pa_verdict_t;
typedef enum { PA_VOTE_ABSTAIN = 0, PA_VOTE_ACCEPT = 1, PA_VOTE_REJECT = 2 } pa_vote_kind_t;

/* ===== Records ===== */
typedef struct {
    uint32_t kind;
    uint32_t node; /* author peer id */
    uint64_t seq;
    uint8_t prev_state[PA_HASH_LEN];
    uint8_t new_state[PA_HASH_LEN];
    uint32_t in_len, out_len;
    uint8_t in[PA_MAX_IO];
    uint8_t out[PA_MAX_IO];
    uint8_t sig[PA_SIG_LEN];
} pa_record_t;

/* SHA-256 over a canonical little-endian encoding of every field but sig.
 * Returns false if a length is out of range (the hash is then all zero). */
bool pa_record_hash(const pa_record_t *r, uint8_t out[PA_HASH_LEN]);

/* Replay hook: from the verifier's own replica at prev_state, compute the
 * outputs and new state hash for `in`. Must be deterministic. Returns PA_OK
 * when a result was computed (a fail-closed refusal is a valid result),
 * PA_ERR_NO_STATE if the replica is not at prev_state, PA_ERR_KIND if the
 * kind is not supported. Must not change the replica. */
typedef pa_status_t (*pa_replay_fn)(void *ctx, uint32_t kind, const uint8_t prev[PA_HASH_LEN],
                                    const uint8_t *in, uint32_t in_len, uint8_t *out,
                                    uint32_t *out_len, uint8_t new_state[PA_HASH_LEN]);

/* Isolation hook, wired by the host to porter_house (close the peer's port
 * seals / capability tokens) and vinea (vna_rt_remove from routing). It must
 * never move or freeze funds. evidence_hash is NULL when the reason is not
 * backed by an evidence object (e.g. a canary failure counted locally). */
typedef enum {
    PA_REVOKE_MISBEHAVIOUR = 1,  /* verified evidence against the peer */
    PA_REVOKE_FALSE_ACCUSER = 2, /* too many evidence objects that failed */
} pa_revoke_reason_t;
typedef void (*pa_revoke_fn)(void *ctx, uint32_t peer, pa_revoke_reason_t why,
                             const uint8_t evidence_hash[PA_HASH_LEN]);

/* ===== Self-audit ===== */
typedef struct {
    uint64_t sum_before, sum_after; /* conserved quantity, e.g. sum of DEBIT */
    uint64_t minted, burned;
    uint64_t tokens_requested, tokens_remaining;
    int64_t headroom; /* ISF headroom h_t (any fixed-point scale); <= 0 stops */
    uint32_t size, size_max;
} pa_precommit_t;

typedef struct {
    uint64_t seq;   /* node seq at the time */
    int32_t status; /* the pa_status_t that fired */
    uint32_t kind;
} pa_event_t;

/* ===== Peers and the per-node context ===== */
typedef struct {
    uint32_t id;
    uint8_t pk[PA_PK_LEN];
    bool used;
    bool blocked;
    uint32_t strikes;       /* evidence objects of theirs that failed */
    uint32_t canary_failed; /* challenges they failed */
    uint64_t canary_win;    /* start tick of the current canary window */
    uint32_t canary_n;      /* canaries sent in that window */
} pa_peer_t;

typedef struct {
    bool used;
    uint32_t target;
    uint8_t req_hash[PA_HASH_LEN];
    uint8_t prev[PA_HASH_LEN];
    uint8_t exp_state[PA_HASH_LEN];
    uint32_t exp_len;
    uint8_t exp_out[PA_MAX_IO];
} pa_canary_t;

typedef struct {
    uint32_t canary_per_window; /* max canaries per target per window */
    uint64_t canary_window;     /* window length in ticks */
    uint32_t strike_limit;      /* failed evidence objects before isolation */
} pa_config_t;

typedef struct {
    uint32_t self;
    const uint8_t *sk; /* caller-owned ML-DSA-65 secret key; never copied */
    pa_node_state_t state;
    bool self_audit; /* false models a node that switched its self-audit off */
    uint64_t seq;    /* next record seq */
    pa_config_t cfg;
    pa_peer_t peers[PA_MAX_PEERS];
    uint32_t n_peers;
    pa_replay_fn replay;
    void *replay_ctx;
    pa_revoke_fn revoke;
    void *revoke_ctx;
    pa_event_t events[PA_MAX_EVENTS]; /* first PA_MAX_EVENTS kept */
    uint32_t n_events;                /* saturating total count */
    pa_canary_t canaries[PA_MAX_CANARIES];
} pa_ctx_t;

void pa_config_default(pa_config_t *c);
/* sk may be NULL for a verify-only context. cfg NULL = defaults. */
void pa_init(pa_ctx_t *c, uint32_t self, const uint8_t *sk, const pa_config_t *cfg,
             pa_replay_fn replay, void *replay_ctx, pa_revoke_fn revoke, void *revoke_ctx);
pa_status_t pa_peer_add(pa_ctx_t *c, uint32_t id, const uint8_t pk[PA_PK_LEN]);
const pa_peer_t *pa_peer_get(const pa_ctx_t *c, uint32_t id);
bool pa_peer_blocked(const pa_ctx_t *c, uint32_t id);

/* Layer 1. With self_audit on: checks p; on failure records an event, enters
 * PA_NODE_CONTRADICTION and returns the failing status. While contradicted,
 * returns PA_ERR_QUARANTINED. */
pa_status_t pa_precommit_check(pa_ctx_t *c, uint32_t kind, const pa_precommit_t *p);
/* Leave the contradiction state (operator action, after the cause is fixed). */
void pa_node_release(pa_ctx_t *c);

/* Self-audit, then build and sign a record. Nothing is signed on failure. */
pa_status_t pa_emit_record(pa_ctx_t *c, uint32_t kind, const uint8_t prev[PA_HASH_LEN],
                           const uint8_t *in, uint32_t in_len, const uint8_t *out, uint32_t out_len,
                           const uint8_t new_state[PA_HASH_LEN], const pa_precommit_t *p,
                           pa_record_t *rec);

/* Signature check of a record against the author's registered key. */
pa_status_t pa_record_verify_sig(const pa_ctx_t *c, const pa_record_t *r);

/* ===== Layer 2A: replay, committee, votes, quorum ===== */
typedef struct {
    uint8_t record_hash[PA_HASH_LEN];
    uint32_t voter;
    uint8_t vote; /* pa_vote_kind_t */
    uint8_t replay_state[PA_HASH_LEN];
    uint8_t sig[PA_SIG_LEN];
} pa_vote_t;

/* SHA-256 over (record_hash, voter, vote, replay_state): what a vote signs. */
void pa_vote_hash(const pa_vote_t *v, uint8_t out[PA_HASH_LEN]);

/* Replay `r` locally. PA_OK: matches bit for bit. PA_ERR_MISMATCH: differs;
 * if exp_out/exp_len/exp_state are non-NULL they get the correct result.
 * Other errors: signature, unknown or blocked author, no state. */
pa_status_t pa_replay_check(pa_ctx_t *c, const pa_record_t *r, uint8_t *exp_out, uint32_t *exp_len,
                            uint8_t exp_state[PA_HASH_LEN]);
/* Replay and sign a vote (ACCEPT on match, REJECT on mismatch, ABSTAIN when
 * the verifier has no state). Needs a signing context. */
pa_status_t pa_vote(pa_ctx_t *c, const pa_record_t *r, pa_vote_t *v);

/* Deterministic committee: sorts and de-duplicates `peers`, removes
 * `author`, then draws k distinct ids by a partial Fisher-Yates shuffle
 * driven by SHA-256("zxv-pa-committee" || record_hash || counter) with
 * rejection sampling (no modulo bias). Output in draw order. */
pa_status_t pa_committee(const uint32_t *peers, uint32_t n, uint32_t author,
                         const uint8_t record_hash[PA_HASH_LEN], uint32_t k, uint32_t *out);

/* Tally signed votes for one record from a committee of k = 3f+1 (k must be
 * >= 3f+1). Votes with a bad signature, a voter outside the committee, a
 * different record hash or a repeated voter are ignored. ACCEPT or REJECT
 * needs >= 2f+1 votes; otherwise UNDECIDED. *counted gets the number of valid
 * votes (may be NULL). */
pa_verdict_t pa_quorum(const pa_ctx_t *c, const uint8_t record_hash[PA_HASH_LEN],
                       const uint32_t *committee, uint32_t k, uint32_t f, const pa_vote_t *votes,
                       uint32_t n_votes, uint32_t *counted);

/* ===== Layer 2C: evidence and isolation ===== */
typedef struct {
    pa_record_t record; /* the accused's own signed record */
    uint32_t accuser;
    uint32_t correct_len;
    uint8_t correct_out[PA_MAX_IO];
    uint8_t correct_state[PA_HASH_LEN];
    uint8_t sig[PA_SIG_LEN]; /* accuser's signature */
} pa_evidence_t;

bool pa_evidence_hash(const pa_evidence_t *e, uint8_t out[PA_HASH_LEN]);
/* Build and sign evidence against `r` from a mismatching replay (runs it). */
pa_status_t pa_evidence_make(pa_ctx_t *c, const pa_record_t *r, pa_evidence_t *e);
/* Independent check by any node:
 *   PA_OK                   valid: accused blocked, revoke callback called.
 *   PA_ERR_FALSE_ACCUSATION the accused's record replays correctly or its
 *                           signature is not the accused's: one strike for
 *                           the accuser; at cfg.strike_limit it is isolated.
 *   PA_ERR_SIG              accuser signature invalid (unattributable; the
 *                           object is dropped and no one is penalised).
 *   PA_ERR_INCONCLUSIVE     no local state, or the accuser's correct result
 *                           is not ours either.
 * No funds are ever touched. */
pa_status_t pa_evidence_check(pa_ctx_t *c, const pa_evidence_t *e);

/* ===== Layer 2B: canary challenges ===== */
typedef struct {
    uint32_t kind;
    uint8_t nonce[PA_REQ_NONCE_LEN]; /* caller randomness, real or canary alike */
    uint8_t prev_state[PA_HASH_LEN];
    uint32_t in_len;
    uint8_t in[PA_MAX_IO];
} pa_request_t;

bool pa_request_hash(const pa_request_t *q, uint8_t out[PA_HASH_LEN]);
/* Register a canary for `target` at `tick`: the expected answer comes from
 * our own replay. Rate-limited (PA_ERR_RATE) per target per window; the
 * table is bounded (PA_ERR_FULL). The request goes out like any other. */
pa_status_t pa_canary_issue(pa_ctx_t *c, uint32_t target, uint64_t tick, const pa_request_t *q);
/* Check a signed answer to a canary request. PA_ERR_NOT_FOUND: not a canary
 * of ours (handle as a normal record). PA_OK: answered correctly. PA_ERR_
 * MISMATCH: failed; canary_failed++ and, when ev is non-NULL, signed
 * evidence is built. The canary entry is consumed either way. */
pa_status_t pa_canary_check(pa_ctx_t *c, const pa_request_t *q, const pa_record_t *answer,
                            pa_evidence_t *ev);

/* ===== Inference spot checks ===== */
typedef int32_t (*pa_infer_fn)(void *ctx, const uint8_t model_cid[PA_HASH_LEN], uint64_t seed,
                               const uint8_t *prompt, uint32_t prompt_len, int32_t *tokens,
                               uint32_t max_tokens);

/* SHA-256("zxv-pa-infer" || model_cid || seed || len || prompt || n ||
 * tokens), little-endian. */
void pa_infer_commit(const uint8_t model_cid[PA_HASH_LEN], uint64_t seed, const uint8_t *prompt,
                     uint32_t prompt_len, const int32_t *tokens, uint32_t n,
                     uint8_t out[PA_HASH_LEN]);
/* True iff this commitment is sampled at rate rate_q16 / 65536 (65536 =
 * always, 0 = never). beacon: randomness published after the commitment. */
bool pa_spot_sampled(const uint8_t beacon[PA_HASH_LEN], const uint8_t commit[PA_HASH_LEN],
                     uint32_t rate_q16);
/* Replay a sampled inference with `infer` and compare to the commitment.
 * PA_OK on a bit-exact match, PA_ERR_MISMATCH otherwise. */
pa_status_t pa_spot_check(pa_infer_fn infer, void *ictx, const uint8_t model_cid[PA_HASH_LEN],
                          uint64_t seed, const uint8_t *prompt, uint32_t prompt_len,
                          uint32_t n_tokens, const uint8_t commit[PA_HASH_LEN]);

#endif /* ZXV_PEER_AUDIT_H */
