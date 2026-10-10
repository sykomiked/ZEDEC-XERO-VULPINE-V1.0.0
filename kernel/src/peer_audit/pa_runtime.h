/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* pa_runtime.h — the deterministic state machines peer_audit replays.
 *
 * Two transition kinds, both exact-integer and deterministic, run through
 * the REAL module code (public headers only):
 *
 *   PA_KIND_LEDGER_TRANSFER  pay_ledger_transfer on a pay_ledger_t.
 *     input  (96 bytes, little-endian): from u32, to u32, amount i64 (signed
 *            on the wire so negative and zero canaries share the format),
 *            initiator u32, tick u64, nonce u64, idem_key[32], rnd[16]
 *            (UETR randomness), attestor u32, reserved u32 = 0.
 *     output (56 bytes): status i32 (pay_status_t), reserved u32 = 0, from
 *            DEBIT after u64, to DEBIT after u64, receipt chain hash[32]
 *            (all but the status zero on refusal).
 *     A non-positive or over-limit amount, a bad account or a bad reserved
 *     field is refused before the ledger is touched (fail closed: the new
 *     state hash equals the previous one).
 *     State hash: SHA-256 over seq, chain head, account count and every
 *     account's (owner, asset, cap, flags, debit, credit, equity).
 *
 *   PA_KIND_BUDGET  swarm_budget_t operations.
 *     input  (16 bytes): op u32 (1 begin cycle, 2 consume, 3 end cycle),
 *            model u32, requested u64.
 *     output (24 bytes): status i32 (swarm_status_t), reserved u32 = 0,
 *            granted u64, remaining u64.
 *     State hash: SHA-256 over every field of swarm_budget_t.
 *
 * pa_runtime_t holds the node's replica of each machine plus one scratch
 * copy per machine; pa_runtime_replay is a pa_replay_fn that applies a
 * transition to the scratch copy only (the replica is never changed by a
 * replay); pa_runtime_apply applies an accepted record's input to the
 * replica itself (the same deterministic code). The scratch buffers may be
 * shared between runtimes on one thread.
 */
#ifndef ZXV_PA_RUNTIME_H
#define ZXV_PA_RUNTIME_H

#include <stdint.h>
#include <stdbool.h>
#include "peer_audit.h"
#include "../pay/pay_ledger.h"
#include "../swarm/swarm_budget.h"

#define PA_XFER_IN_LEN    96u
#define PA_XFER_OUT_LEN   56u
#define PA_BUDGET_IN_LEN  16u
#define PA_BUDGET_OUT_LEN 24u

typedef enum { PA_BUDGET_BEGIN = 1, PA_BUDGET_CONSUME = 2, PA_BUDGET_END = 3 } pa_budget_op_t;

typedef struct {
    uint32_t from, to;
    int64_t amount;
    uint32_t initiator;
    uint64_t tick, nonce;
    uint8_t idem[32];
    uint8_t rnd[16];
    uint32_t attestor;
} pa_xfer_t;

typedef struct {
    uint32_t op, model;
    uint64_t requested;
} pa_budget_req_t;

typedef struct {
    pay_ledger_t *ledger;     /* replica */
    pay_ledger_t *ledger_tmp; /* scratch */
    swarm_budget_t *budget;
    swarm_budget_t *budget_tmp;
    uint16_t asset; /* asset whose DEBIT total is the conserved quantity */
} pa_runtime_t;

void pa_xfer_encode(const pa_xfer_t *x, uint8_t out[PA_XFER_IN_LEN]);
bool pa_xfer_decode(const uint8_t *in, uint32_t len, pa_xfer_t *x);
void pa_budget_encode(const pa_budget_req_t *b, uint8_t out[PA_BUDGET_IN_LEN]);

void pa_ledger_state_hash(const pay_ledger_t *L, uint8_t out[PA_HASH_LEN]);
void pa_budget_state_hash(const swarm_budget_t *b, uint8_t out[PA_HASH_LEN]);

/* The transition itself, applied in place to L (which the caller owns).
 * Always writes PA_XFER_OUT_LEN bytes. */
void pa_ledger_apply(pay_ledger_t *L, const uint8_t *in, uint32_t in_len,
                     uint8_t out[PA_XFER_OUT_LEN]);
void pa_budget_apply(swarm_budget_t *b, const uint8_t *in, uint32_t in_len,
                     uint8_t out[PA_BUDGET_OUT_LEN]);

/* pa_replay_fn over a pa_runtime_t. */
pa_status_t pa_runtime_replay(void *rt, uint32_t kind, const uint8_t prev[PA_HASH_LEN],
                              const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len,
                              uint8_t new_state[PA_HASH_LEN]);

/* Self-audit inputs for a ledger transition (conserved: DEBIT total of
 * rt->asset over every capital form; minted = burned = 0 for a transfer).
 * headroom is passed through; a transfer has no token cost. */
void pa_ledger_precommit(const pay_ledger_t *before, const pay_ledger_t *after, uint16_t asset,
                         int64_t headroom, pa_precommit_t *p);
/* Self-audit inputs for a budget consume: requested vs remaining, and the
 * granted tokens conserved against the slot's used counter. */
void pa_budget_precommit(const swarm_budget_t *before, const swarm_budget_t *after,
                         const pa_budget_req_t *req, int64_t headroom, pa_precommit_t *p);

/* Apply an accepted record's input to the replica (not the scratch copy).
 * Returns PA_ERR_NO_STATE if the replica is not at prev. */
pa_status_t pa_runtime_apply(pa_runtime_t *rt, const pa_record_t *r);

#endif /* ZXV_PA_RUNTIME_H */
