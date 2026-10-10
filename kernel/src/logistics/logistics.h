/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* logistics.h — bare-roots logistics: many hands split one cargo; the escrow
 * does not open until the crates land.
 *
 * WHAT THIS IS
 * -----------
 * Real-world logistics primitives for tangible goods, built entirely from
 * modules that already exist — this file invents no economics and no trust:
 *
 *   (1) SYNDICATES. Many people band together and split ONE supply contract's
 *       units among themselves, proportional to their shares. The split is
 *       CONSERVED EXACTLY — the allocations sum to the contract total, to the
 *       last integer unit, by a remainder-to-the-last construction (never a
 *       rounding leak that mints or burns cargo).
 *
 *   (2) A SECONDARY CONTRACT MARKETPLACE. A negotiator lands a deal with a big
 *       supplier, then splits the supply + logistics into sub-contracts. The
 *       negotiator earns by negotiating — a BOUNDED, symbiotic margin capped at
 *       the customary 11% (LOG_CUSTOMARY_*). A margin over 11% is REFUSED, and
 *       every split is run through The One Policy (onepolicy) so a term that
 *       inflicts asymmetric harm is, by definition, not a term.
 *
 *   (3) MODULAR CONTRACTS with a CREDIBILITY / follow-through rating, drawn from
 *       reputation.h (badge_score over an earnable follow-through badge). A
 *       party's credibility RISES with each completed delivery.
 *
 *   (4) COMMODITY-BACKED value. Material capital (zcapital ZCAP_MANUFACTURED) is
 *       backed by a PHOTO-ATTESTATION of the physical asset — a SUPPLIED content
 *       address / digest (proof_cid), the same 32-byte shape ipfs/vino_stores
 *       use. The device never photographs or certifies the real world; the CID
 *       is stored verbatim as a witness (OPS BOUNDARY).
 *
 *   (5) AUTOMATED ESCROW. Deposited material capital is HELD, and RELEASED only
 *       when a delivery attestation is presented. No proof (UNBOUND / absent) =>
 *       LOG_HELD, funds unchanged — escrow never auto-releases itself.
 *
 * OPS BOUNDARIES (never fabricated): the photo-attestation of the physical asset
 * and the delivery confirmation are SUPPLIED attestations; real freight movement
 * is off-device; settlement into real money is the vino_stores boundary. This
 * module holds NO capability of its own — it surfaces the supplied facts and
 * refuses to invent the ones it was not given.
 *
 * Freestanding: integer-only surplus_real_t, fixed-size arrays, no libc, no
 * malloc, no floating point on target.
 */
#ifndef ZXV_LOGISTICS_H
#define ZXV_LOGISTICS_H

#include <stdint.h>
#include <stdbool.h>
#include "surplus.h"
#include "onepolicy.h"    /* op_term_t / op_symbiotic_ok — asymmetric harm is void */
#include "zcapital.h"     /* zcap_form_t / ZCAP_MANUFACTURED — material capital     */
#include "reputation.h"   /* rep_state_t / badge_award / badge_score — credibility  */

/* ===== Capacities (fixed, freestanding) ===== */
#define LOG_MAX_CONTRACTS   64u
#define LOG_MAX_SPLITS      64u
#define LOG_MAX_PARTIES     32u   /* members per contract / per split               */
#define LOG_CID_LEN         32u   /* one SHA-256 digest — == IPFS_CID_LEN / VINO_*  */

/* The customary negotiator margin ceiling: 11% (11/100). A split whose negotiator
 * cut exceeds this is not a deal — it is refused. */
#define LOG_CUSTOMARY_NUM   11
#define LOG_CUSTOMARY_DEN   100

/* The follow-through badge id in the internal reputation ledger. The only badge
 * this module ever awards, so badge_score == the follow-through level. */
#define LOG_BADGE_FOLLOWTHROUGH  0xF0110u

/* Material capital is backed as this canonical zcapital form. */
#define LOG_ESCROW_FORM  ZCAP_MANUFACTURED

/* ===== int32_t return codes (id/0 on success, negated error < 0) ===== */
#define LOG_OK             0
#define LOG_ERR_NULL      (-1)
#define LOG_ERR_FULL      (-2)   /* contract/split table full                      */
#define LOG_ERR_NOT_FOUND (-3)   /* no such contract                               */
#define LOG_ERR_VOID      (-4)   /* onepolicy: the term is not a term (harm/etc.)  */
#define LOG_ERR_MARGIN    (-5)   /* negotiator margin over the customary 11%       */
#define LOG_ERR_SHARES    (-6)   /* bad member count or non-positive share sum     */
#define LOG_ERR_CAPITAL   (-7)   /* escrow form is not priceable material capital   */
#define LOG_ERR_AMOUNT    (-8)   /* negative deposit amount                        */

/* ===== The contract model ===== */
typedef struct {
    uint64_t       id;                          /* > 0 when in_use                  */
    bool           in_use;
    uint32_t       parties[LOG_MAX_PARTIES];    /* the participating party ids      */
    uint32_t       n_parties;
    surplus_real_t total_units;                 /* total units of the good          */
    surplus_real_t escrow_held;                 /* material capital held in escrow   */
    bool           delivered;                   /* set true only on a delivery proof */
    uint8_t        proof_cid[LOG_CID_LEN];      /* photo-attestation of the goods    */
    bool           has_proof_cid;               /* false => UNBOUND (no witness given)*/
    uint64_t       parent_id;                   /* 0 top-level; else the parent      */
} log_contract_t;

/* ===== A split of a contract's units (syndicate OR sub-contract) ===== */
typedef struct {
    bool           in_use;
    uint64_t       contract_id;                 /* which contract this splits        */
    uint32_t       members[LOG_MAX_PARTIES];
    surplus_real_t alloc[LOG_MAX_PARTIES];      /* units each member gets            */
    uint32_t       n;
    bool           is_subcontract;              /* true => a secondary-market split  */
    uint32_t       negotiator;                  /* meaningful iff is_subcontract     */
    surplus_real_t negotiator_cut;              /* units to the negotiator           */
    surplus_real_t margin;                      /* the cut as a fraction (<= 0.11)   */
} log_split_t;

/* ===== A delivery attestation (SUPPLIED — an ops boundary) =====
 * `attestor` is the 32-byte Ed25519 public key of the confirming party; `sig`
 * is its signature over (contract_id LE64 || delivery_cid || confirmed byte).
 * log_escrow_release verifies THIS signature via the bound verifier — it never
 * trusts the bare `confirmed` bool, and never verifies the fact it asserts. */
typedef struct {
    uint32_t confirming_party;                  /* who attests the crates landed     */
    bool     confirmed;                         /* the attestation asserts delivery  */
    uint8_t  delivery_cid[LOG_CID_LEN];         /* content-address of the evidence   */
    uint8_t  attestor[32];                      /* Ed25519 pubkey of confirming party*/
    uint8_t  sig[64];                           /* sig over id||delivery_cid||confirmed*/
} log_delivery_t;

/* ===== The escrow release result ===== */
typedef enum {
    LOG_RELEASED    = 0,    /* proof honoured — escrow paid out                      */
    LOG_HELD        = 1,    /* no/false proof — escrow untouched, still held         */
    LOG_NO_CONTRACT = 2,    /* no such contract                                      */
    LOG_BAD_ARG     = 3     /* NULL state                                            */
} log_status_t;

typedef struct {
    log_status_t   status;
    surplus_real_t released;            /* what was paid out (0 unless RELEASED)     */
    surplus_real_t escrow_remaining;    /* what is still held afterward              */
} log_result_t;

/* ===== Delivery-attestation verifier (ops boundary) =====
 * Returns true iff `d`'s signature verifies as an attestation of `contract_id`.
 * Model your real delivery oracle as this function pointer. When UNBOUND (NULL),
 * log_escrow_release fails CLOSED (LOG_HELD) — a caller-set `confirmed` bool is
 * never, by itself, proof that the crates landed. */
typedef bool (*log_deliv_verify_fn)(uint64_t contract_id, const log_delivery_t *d);

/* ===== The logistics state ===== */
typedef struct {
    log_contract_t      contracts[LOG_MAX_CONTRACTS];
    log_split_t         splits[LOG_MAX_SPLITS];
    rep_state_t         rep;            /* credibility ledger (follow-through badge)  */
    uint64_t            next_id;        /* next contract id (starts at 1)            */
    log_deliv_verify_fn verify;         /* delivery verifier (ops boundary); NULL=shut*/
} log_state_t;

/* ===== Lifecycle ===== */
void log_init(log_state_t *s);

/* ===== (3)+(4) Open a modular, commodity-backed contract =====
 * `parties`/`n_parties` name the participants (n_parties <= LOG_MAX_PARTIES).
 * `total_units` is the quantity of the good. `proof_cid` is the SUPPLIED photo-
 * attestation of the physical asset (may be NULL => no witness bound). `term`, if
 * non-NULL, is run through onepolicy — a term that inflicts asymmetric harm (or
 * usury / coercion / fraud root) is VOID and the contract is refused.
 * Returns the new contract id (> 0), or a negative LOG_ERR_*. */
int32_t log_contract_open(log_state_t *s,
                          const uint32_t parties[], uint32_t n_parties,
                          surplus_real_t total_units,
                          const uint8_t proof_cid[LOG_CID_LEN],
                          const op_term_t *term);

/* ===== (1) SYNDICATE — split a contract's units among members =====
 * Splits contract `contract_id`'s total_units among `members` in proportion to
 * `shares`, CONSERVED EXACTLY: the allocations sum to total_units to the unit.
 * Returns LOG_OK, or a negative LOG_ERR_*. */
int32_t log_syndicate_form(log_state_t *s, uint64_t contract_id,
                           const uint32_t members[], const surplus_real_t shares[],
                           uint32_t n);

/* ===== (2) SECONDARY MARKETPLACE — split into sub-contracts =====
 * The negotiator earns `margin` (a fraction, e.g. 0.11 == the customary ceiling)
 * of parent `parent_id`'s units; the REMAINDER is split among `members`
 * proportional to `shares`, conserved so negotiator_cut + sum(alloc) == total.
 * REFUSED (nothing written) when margin > the customary 11% (LOG_ERR_MARGIN), or
 * when `term` (if non-NULL) fails onepolicy (LOG_ERR_VOID). The sub-contractors
 * always take the majority (margin <= 11% => they hold >= 89%).
 * Returns LOG_OK, or a negative LOG_ERR_*. */
int32_t log_subcontract_split(log_state_t *s, uint64_t parent_id,
                              const uint32_t members[], const surplus_real_t shares[],
                              uint32_t n, uint32_t negotiator, surplus_real_t margin,
                              const op_term_t *term);

/* ===== (5) AUTOMATED ESCROW ===== */

/* Deposit `amount` of material capital (ZCAP_MANUFACTURED) into a contract's
 * escrow. Material capital must be a priceable form (it is) — refuses otherwise.
 * Adds to escrow_held. Returns LOG_OK, or a negative LOG_ERR_*. */
int32_t log_escrow_deposit(log_state_t *s, uint64_t contract_id,
                           surplus_real_t amount);

/* Install the delivery-attestation verifier (ops boundary). Until this is set,
 * log_escrow_release fails CLOSED — the escrow cannot open on an unverified word. */
void log_set_verifier(log_state_t *s, log_deliv_verify_fn fn);

/* Built-in Ed25519 delivery verifier: recomputes the canonical message
 * (contract_id LE64 || delivery_cid || confirmed) and checks proof->sig against
 * proof->attestor. Bind it with log_set_verifier to require real signatures. */
bool log_ed25519_delivery_verify(uint64_t contract_id, const log_delivery_t *d);

/* Release the escrow — ONLY on a VERIFIED delivery attestation. Returns LOG_HELD
 * (escrow untouched, never auto-released) unless ALL of these hold:
 *   - a verifier is bound (log_set_verifier) — UNBOUND fails closed;
 *   - proof is non-NULL and proof->confirmed is true;
 *   - proof->confirming_party is one of the contract's parties (no outsider);
 *   - the contract had a goods witness bound at open (has_proof_cid);
 *   - the bound verifier accepts proof->sig over id||delivery_cid||confirmed.
 * Only then does it pay out the whole held amount, mark the contract delivered
 * (once — idempotent), and RAISE every party's credibility (follow-through). */
log_result_t log_escrow_release(log_state_t *s, uint64_t contract_id,
                                const log_delivery_t *proof);

/* ===== (3) CREDIBILITY — follow-through rating (reputation.h) ===== */
surplus_real_t log_credibility(const log_state_t *s, uint32_t party);

/* ===== Read-only lookups (for callers and tests) ===== */
const log_contract_t *log_contract_find(const log_state_t *s, uint64_t contract_id);
const log_split_t    *log_split_find(const log_state_t *s, uint64_t contract_id);

#endif /* ZXV_LOGISTICS_H */
