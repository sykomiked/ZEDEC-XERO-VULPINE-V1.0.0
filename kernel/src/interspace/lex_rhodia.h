/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* lex_rhodia.h — the Rhodian Sea-Law primitives of the interstitial commons.
 *
 * Four ancient rules, made numeric and honest:
 *   GENERAL AVERAGE   — jettison to save the ship; the loss is shared in
 *                       proportion to what each party had at stake. This is the
 *                       measured ANTIDOTE to onepolicy's asymmetric harm: the
 *                       sacrifice of one becomes the shared burden of all.
 *   SALVAGE           — a share of what was saved, scaled by the risk taken.
 *   SOVEREIGN IMMUNITY— your content-hash IS your flag; a rename mutates the
 *                       content so the recomputed hash diverges. Seizure is not
 *                       "forbidden", it is cryptographically impossible.
 *   SAFE PASSAGE      — a non-retroactive grant measured in phase-ticks, valid
 *                       up to and including its deadline, then simply gone.
 *
 * "Protection, not predation."
 */
#ifndef ZXV_LEX_RHODIA_H
#define ZXV_LEX_RHODIA_H

/* shared vocabulary — pull the umbrella's core without re-triggering the family
 * re-include (this header is one of the family). */
#define ZXV_IN_FAMILY_HEADER
#include "interspace.h"
#undef ZXV_IN_FAMILY_HEADER

/* ---- GENERAL AVERAGE (Lex Rhodia de Iactu) ---- */

typedef struct {
    zxv_node_id_t  node;
    surplus_real_t stake;   /* contributory value; >= 0 */
} ga_party_t;

/* Apportion `sacrifice` across `parties` in proportion to stake:
 *     contrib_out[i] = sacrifice * stake[i] / total_stake
 * computed MULTIPLY-then-DIVIDE so the Q32.32 target never floors to zero.
 *
 * CONSERVATION IS EXACT, not "within epsilon": any truncation residue is
 * assigned to the LARGEST-stake party, so sum(contrib_out) == sacrifice on the
 * nose. A zero-stake party contributes exactly SR_ZERO. total_stake == 0 (which
 * includes n == 0) returns ZXV_EDEGEN — we never divide by zero. */
zxv_status_t lex_rhodia_general_average(surplus_real_t sacrifice,
                                        const ga_party_t *parties, uint32_t n,
                                        surplus_real_t *contrib_out);

/* ---- SALVAGE ---- */

/* award = salved * clamp(BASE + RISK_BONUS*risk, 0, 1),
 * with BASE = 0.10 and RISK_BONUS = 0.40 as fixed-point constants (never
 * open-coded fractions). Monotone non-decreasing in both `salved` and `risk`,
 * and 0 < award <= salved for salved > 0 and risk >= 0 (BASE keeps the rate off
 * zero; the clamp keeps it off "more than everything"). salved <= 0 is
 * degenerate -> ZXV_EDEGEN. */
zxv_status_t lex_rhodia_salvage_award(surplus_real_t salved, surplus_real_t risk,
                                      surplus_real_t *award_out);

/* ---- SOVEREIGN IMMUNITY (your hash is your key) ---- */

typedef struct {
    uint8_t       flag_hash[ZXV_HASH_LEN];  /* ipfs CID of the vessel content   */
    uint8_t       sovereign_sig[64];        /* carried attestation — OPS BOUNDARY:
                                             * a real key backend signs this; we
                                             * do NOT fabricate authenticity, and
                                             * verify() does not trust this field */
    zxv_node_id_t keyholder;                /* the one node the flag answers to  */
} vessel_flag_t;

/* Raise a flag over `content`: flag_hash = ipfs_cid_from_bytes(content). If
 * `sig` is non-NULL its 64 bytes are carried verbatim (an external attestation);
 * if NULL the field is zeroed. keyholder is recorded. */
void vessel_flag_raise(vessel_flag_t *v, const void *content, uint32_t len,
                       zxv_node_id_t keyholder, const uint8_t sig[64]);

/* RECOMPUTE the CID over exactly these bytes and compare to flag_hash. A single
 * flipped byte (a rename, a re-flag, a forged seizure) changes the content and
 * the recomputed hash diverges -> false. There is no trusted "immune = true"
 * byte to lie with. */
bool vessel_flag_verify(const vessel_flag_t *v, const void *content, uint32_t len);

/* Does the flag's immunity vest in `actor`? True ONLY for the keyholder; false
 * for every other node (and false for ZXV_NODE_NONE). No foreign port may
 * assert authority over the flagged vessel — a vessel flags OUT of a port, it
 * is not owned by it. */
bool vessel_immune_from(const vessel_flag_t *v, zxv_node_id_t actor);

/* ---- SAFE PASSAGE (phase-tick, non-retroactive) ---- */

typedef struct {
    vessel_flag_t vessel;
    zxv_node_id_t through;        /* the node whose waters are being transited   */
    uint64_t      issued_tick;    /* the through-node's phase-tick at issue       */
    uint64_t      ttl_deadline;   /* issued_tick + ttl (saturating)               */
} safe_passage_grant_t;

/* Cut a grant: ttl_deadline = issued_tick + ttl, saturating at UINT64_MAX so a
 * huge ttl never wraps backward into the past. Ticks are PHASE-ticks, resolved
 * on the through-node's clock — not wall-clock. */
void safe_passage_request(safe_passage_grant_t *g, const vessel_flag_t *vessel,
                          zxv_node_id_t through, uint64_t issued_tick,
                          uint64_t ttl);

/* Valid strictly from issued_tick up to AND INCLUDING ttl_deadline; false
 * before it was issued (non-retroactive) and false after it lapses. */
bool safe_passage_valid(const safe_passage_grant_t *g, uint64_t now_tick);

#endif /* ZXV_LEX_RHODIA_H */
