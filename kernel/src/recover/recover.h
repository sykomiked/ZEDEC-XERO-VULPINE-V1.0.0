/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* recover.h — the designed recovery framework (audit gap B5, roadmap P3.2:
 * shadow state and signed RepairProof rollback).
 *
 * A fault is answered with exactly one of three actions, and nothing else:
 *
 *   REFUSE     the faulty work is refused; no state changes.
 *   ROLL BACK  the state is restored, byte for byte, from a checkpoint the
 *              caller made earlier (the shadow state). The framework never
 *              writes any other bytes: there is no way to patch state.
 *   ISOLATE    the component is quarantined. Isolation is sticky: no API
 *              takes a component back out, and an isolated component's
 *              rollbacks are refused.
 *
 *   EVERY ACTION IS PROVEN. Each action fills a RecoveryProof around the
 *   paradox engine's RepairProof (pdx_repair_t, paradox.h): its before and
 *   after state digests, the trips, the anomaly signature and the
 *   quarantine record. A rollback is run BY pdx_destructive itself (with
 *   the checkpoint slot as its checkpoint), so the restore, the quarantine
 *   record and the RepairProof are paradox's own. The proof is bound into
 *   paradox's hash-chained operator log (pdx_log_t) and signed by the
 *   caller's signer (recover_pq.h binds ML-DSA-87). Verification fails
 *   closed: a missing verifier, an empty or oversized signature, any field
 *   changed, or a log record that does not match, and it is false.
 *
 *   HARD INVARIANTS FAIL CLOSED. A forbidden tag (lpres_hex polarity) or a
 *   failed hard invariant (the caller's conservation check, and the
 *   optional per-case check) can never end in a refusal that leaves the
 *   broken state live: rcv_recover rolls it back, or, when no checkpoint is
 *   usable, isolates the component. A checkpoint that itself fails the hard
 *   invariant is never restored, and a state that fails it is never
 *   checkpointed. Forbidden work always gets a quarantine record (X6).
 *
 *   SHADOW STATE: TWO SLOTS. The A/B discipline of the update work
 *   (abupdate.h, hwr.h R4): a checkpoint is written into the slot that is
 *   NOT the known-good one, read back against its SHA-256, and only then
 *   becomes known-good, so a torn checkpoint never replaces a good one.
 *   Each slot keeps its digest and a strictly increasing generation. At
 *   rollback a slot whose bytes no longer match their digest is marked bad
 *   for good, and the other (previous known-good) slot is used instead.
 *
 *   A FULL LOG HALTS. When the operator log, the quarantine log or the
 *   isolation table lacks room, nothing changes and RCV_OUT_FULL is
 *   returned; the framework then refuses every later call (RCV_OUT_HALTED).
 *   If the signer fails, the action (always the safe direction: a refusal,
 *   a restore of the caller's own checkpoint, an isolation) has been taken,
 *   its log record says UNSIGNED, and the framework halts.
 *
 * HONEST LIMITS. This decides and records recoveries; it does not detect
 * faults. A fault is whatever the caller's tag and invariant checks say:
 * a fault they do not see is not recovered. Checkpoints are byte copies of
 * one caller-owned region of fixed length; anything outside that region
 * (devices, the network, other modules' state) is not rolled back. The
 * A/B code of abupdate (ZXVFS files and signed packages) and hwr (signed
 * device images behind load/readback/activate) is bound to those formats,
 * so the two-slot discipline is reused here, not their functions. The
 * signature covers the proof, not the checkpoint bytes themselves (their
 * digest is in the proof). Sending proofs to peers is the caller's job.
 * Freestanding C11: no libc, no allocation, no floating point; every
 * array is the caller's, with a fixed capacity.
 */
#ifndef RECOVER_H
#define RECOVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "../paradox/paradox.h"

#define RCV_SIG_MAX 4627u /* room for an ML-DSA-87 signature */
#define RCV_NO_SLOT 0xFFFFFFFFu
#define RCV_NO_QSEQ 0xFFFFFFFFFFFFFFFFull /* no quarantine record */

/* The three actions. */
typedef enum { RCV_ACT_REFUSE = 0, RCV_ACT_ROLLBACK = 1, RCV_ACT_ISOLATE = 2 } rcv_action_t;

typedef enum {
    RCV_OUT_REFUSED = 0, /* refused; no state changed (proof signed) */
    RCV_OUT_ROLLED_BACK, /* state restored from a checkpoint (proof signed) */
    RCV_OUT_ISOLATED,    /* component quarantined (proof signed) */
    RCV_OUT_UNSIGNED,    /* action taken, signer failed: halted, proof unusable */
    RCV_OUT_FULL,        /* a log or the isolation table is full: nothing changed, halted */
    RCV_OUT_HALTED,      /* halted earlier: nothing changed, no proof */
    RCV_OUT_BAD_INPUT    /* NULL or inconsistent arguments: nothing changed, no proof */
} rcv_out_t;

/* Why the action was chosen (the proof's `why`). */
enum {
    RCV_WHY_ASKED = 0,         /* the caller asked for exactly this action */
    RCV_WHY_SOFT = 1,          /* rcv_recover: no hard invariant broken, the work is refused */
    RCV_WHY_HARD = 2,          /* a hard invariant broke and was answered */
    RCV_WHY_ISOLATED = 3,      /* the component is isolated: its rollback is refused */
    RCV_WHY_NO_CHECKPOINT = 4, /* no shadow, no valid slot, or a length mismatch */
    RCV_WHY_TAMPERED = 5,      /* every valid slot failed its digest */
    RCV_WHY_BROKEN = 6         /* every intact slot fails the hard invariant */
};

/* Operator-log ops for recovery records (paradox's own ops are 1..7). */
enum {
    RCV_OP_REFUSE = 16, /* = RCV_OP_REFUSE + action */
    RCV_OP_ROLLBACK = 17,
    RCV_OP_ISOLATE = 18,
    RCV_OP_CHECKPOINT = 19
};

/* Signer: write a signature over msg (32 bytes) into sig (cap bytes) and
 * return its length, or 0 on failure. */
typedef uint32_t (*rcv_sign_fn)(void *ctx, const uint8_t msg[PDX_HASH], uint8_t *sig, uint32_t cap);
/* Verifier: true iff sig is a valid signature by pk over msg. */
typedef bool (*rcv_verify_fn)(void *ctx, const uint8_t *pk, uint32_t pk_len,
                              const uint8_t msg[PDX_HASH], const uint8_t *sig, uint32_t sig_len);

/* ---- shadow state: two checkpoint slots ---- */
typedef struct {
    uint8_t *slot[2]; /* caller buffers, len bytes each */
    uint32_t len;
    uint8_t digest[2][PDX_HASH]; /* pdx_state_hash of what each slot holds */
    uint64_t gen[2];             /* generation of each slot's checkpoint */
    uint8_t valid[2];            /* 1 while a slot holds a checkpoint not found bad */
    uint32_t good;               /* known-good slot, or RCV_NO_SLOT */
    uint64_t next_gen;           /* starts at 1, strictly increasing */
} rcv_shadow_t;

/* False for NULL or equal buffers or len 0. */
bool rcv_shadow_init(rcv_shadow_t *sh, uint8_t *slot_a, uint8_t *slot_b, uint32_t len);

/* ---- the framework ---- */
typedef struct {
    pdx_log_t *log;       /* paradox's operator log */
    lpx_qlog_t *q;        /* lpres_hex's quarantine log */
    rcv_shadow_t *shadow; /* may be NULL: no checkpoints, so no rollback */
    uint32_t *iso;        /* isolated component ids (caller array) */
    uint32_t iso_cap, iso_n;
    pdx_invariant_fn hard; /* the hard invariant (conservation...); may be NULL */
    void *hard_ctx;
    rcv_sign_fn sign;
    void *sign_ctx;
    bool halted;
} rcv_t;

/* False (and rc unusable) when rc, log, q, iso or sign is NULL. */
bool rcv_init(rcv_t *rc, pdx_log_t *log, lpx_qlog_t *q, rcv_shadow_t *shadow, uint32_t *iso,
              uint32_t iso_cap, pdx_invariant_fn hard, void *hard_ctx, rcv_sign_fn sign,
              void *sign_ctx);

/* One fault. inv (may be NULL) is a hard invariant for this case only,
 * checked together with rc->hard. */
typedef struct {
    uint32_t component; /* who: the component that would be isolated */
    lpx_t tag;          /* the work's tag; forbidden polarity is a hard break */
    uint32_t code;      /* the caller's reason code, carried in the proof */
    pdx_invariant_fn inv;
    void *inv_ctx;
} rcv_case_t;

/* The RecoveryProof: paradox's RepairProof plus what makes it a recovery
 * record. repair.what is pdx_destructive's for a rollback and, for a
 * refusal or an isolation, SHA-256 over "zxv/rcv-what/v1" || action ||
 * trips || component || len || before || after || repair.sig (u32 LE). */
typedef struct {
    pdx_repair_t repair;
    uint8_t action;  /* rcv_action_t */
    uint8_t outcome; /* rcv_out_t: REFUSED, ROLLED_BACK or ISOLATED when signed */
    uint8_t why;     /* RCV_WHY_* */
    uint8_t tag;     /* the case's tag */
    uint32_t component, code;
    uint64_t gen;           /* checkpoint generation restored (rollback), else 0 */
    uint64_t seq;           /* the operator-log record of this proof */
    uint8_t prev[PDX_HASH]; /* log head before that record */
    uint8_t head[PDX_HASH]; /* that record's hash */
    uint8_t msg[PDX_HASH];  /* the signed digest (layout in recover.c) */
    uint32_t sig_len;
    uint8_t sig[RCV_SIG_MAX];
} rcv_proof_t;

/* Take a checkpoint of state into the shadow (A/B, see above). False, with
 * nothing changed, when halted, without a shadow, on a length mismatch,
 * when the state or the case fails a hard invariant or the tag is
 * forbidden (a broken state is never checkpointed), or when the log is
 * full (that also halts). Record: op CHECKPOINT, aux = slot, a = digest,
 * b = zeros, kind 0, outcome 0. *gen_out (may be NULL) = the generation. */
bool rcv_checkpoint(rcv_t *rc, const rcv_case_t *k, const uint8_t *state, uint32_t len,
                    uint64_t *gen_out);

/* REFUSE. Always available while not halted. state (may be NULL with len 0)
 * is only hashed into before and after (equal: nothing changes). A
 * forbidden tag or a failed hard invariant on state also quarantines. */
rcv_out_t rcv_refuse(rcv_t *rc, const rcv_case_t *k, const uint8_t *state, uint32_t len,
                     rcv_proof_t *proof);

/* ROLL BACK state to the newest usable checkpoint (the known-good slot,
 * else the other one): valid, digest intact, same length, and passing the
 * hard invariants. Without one, or when the component is isolated, this
 * is a signed REFUSE (why says which); state is untouched. A rollback
 * always quarantines (pdx_destructive). */
rcv_out_t rcv_rollback(rcv_t *rc, const rcv_case_t *k, uint8_t *state, uint32_t len,
                       rcv_proof_t *proof);

/* ISOLATE the component (sticky) and quarantine the tag. Isolating an
 * isolated component is ISOLATED again with a fresh proof and no new
 * table entry. */
rcv_out_t rcv_isolate(rcv_t *rc, const rcv_case_t *k, rcv_proof_t *proof);

/* The policy: isolated component -> REFUSE (why ISOLATED). Otherwise, if
 * no hard invariant is broken (tag not forbidden, invariants hold on
 * state) -> REFUSE (why SOFT): the work is refused and nothing is patched.
 * A broken hard invariant -> ROLL BACK (why HARD) when a usable checkpoint
 * exists, else ISOLATE (why = the reason the rollback was impossible).
 * It never ends a hard break with a refusal that leaves state broken. */
rcv_out_t rcv_recover(rcv_t *rc, const rcv_case_t *k, uint8_t *state, uint32_t len,
                      rcv_proof_t *proof);

bool rcv_isolated(const rcv_t *rc, uint32_t component);

/* Recompute proof->msg from the fields and check the signature. False for
 * a NULL verifier, a signature length of 0 or above RCV_SIG_MAX, an
 * outcome that does not match the action, or any mismatch. */
bool rcv_proof_verify(const rcv_proof_t *proof, rcv_verify_fn verify, void *ctx, const uint8_t *pk,
                      uint32_t pk_len);
/* The proof is in this log: the chain verifies and record seq carries this
 * proof (op, why, tag, outcome, component, msg, signature digest, prev and
 * hash); for a rollback, the record before it is paradox's DESTRUCTIVE
 * record for the same RepairProof. */
bool rcv_proof_in_log(const pdx_log_t *log, const rcv_proof_t *proof);

/* SHA-256 over "zxv/rcv-proof/v1" || action || outcome || why || tag ||
 * component || code || trips || steps || gen || qseq || seq || prev ||
 * repair.what || repair.before || repair.after || repair.sig (integers
 * little-endian). The digest that is signed. */
void rcv_proof_digest(const rcv_proof_t *proof, uint8_t out[PDX_HASH]);

const char *rcv_action_name(uint8_t a);
const char *rcv_out_name(rcv_out_t o);

#endif /* RECOVER_H */
