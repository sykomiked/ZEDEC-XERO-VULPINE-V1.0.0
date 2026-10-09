/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0 AND LicenseRef-Royal-Writ-Sicilian-Crown-1.0 AND LicenseRef-SEL-3.3 */
/* swarm_logic.h — the kernel's spaces and paradox operators, applied to what
 * agents claim.
 *
 * Every claim an agent makes sits in one of the kernel's three spaces
 * (trispace.h, dimfold.h): S+ positive (asserts, provides), S- negative
 * (denies, needs, undoes) or S0 neutral (unresolved, no effect). It has a
 * Fibonacci level (edp_risk.h's Fibonacci-signed algebra) and says whether it
 * rests on evidence. When two claims on the same topic meet:
 *
 *   L1  UNRELATED.  Different topics do not interact.
 *   L2  REINFORCE.  Same space: one claim, one level higher. The second
 *       agent's tokens are freed (the Venn rule: don't compute it twice).
 *   L3  HOLD.  Neutral meets anything: the other claim stands. S0 never acts.
 *   L4  ANNIHILATE.  S+ against S-, neither verified, same level: the pair is
 *       a signed zero (A + not-A = 0). Both lines stop, all their tokens are
 *       freed and the topic returns to S0. No compute is spent arguing.
 *   L5  DOMINATE.  S+ against S-: evidence beats no evidence, otherwise the
 *       higher level wins. The loser's tokens are freed.
 *   L6  GLUT.  Both verified, different levels: the contradiction is held,
 *       the higher-level reading leads and both stay on record.
 *   L7  PARADOX TRAP.  Both verified, same level: both rails high, the
 *       dual-rail (1,1) case of tantra.h. The output is forced to S0 and
 *       escalated; nothing is built on it until it is resolved.
 * The outcome carries the matching Hackronomicon truth state.
 * Freestanding: no libc, no floating point.
 */
#ifndef SWARM_LOGIC_H
#define SWARM_LOGIC_H

#include <stdint.h>
#include <stdbool.h>
#include "swarm_hk.h"

/* Values match trispace.h tri_role_t and dimfold.h. */
typedef enum { SWARM_SPACE_POS = 0, SWARM_SPACE_NEG = 1, SWARM_SPACE_NEU = 2 } swarm_space_t;

typedef struct {
    uint64_t      topic;      /* what the claim is about (e.g. an overlap key) */
    swarm_space_t space;
    uint8_t       level;      /* Fibonacci level; magnitude F(level) */
    bool          verified;   /* rests on a test, source or witness */
    uint32_t      tokens;     /* tokens committed to pursuing it */
} swarm_claim_t;

typedef enum {
    SWARM_LG_UNRELATED = 0, SWARM_LG_REINFORCE, SWARM_LG_HOLD, SWARM_LG_ANNIHILATE,
    SWARM_LG_DOMINATE, SWARM_LG_GLUT, SWARM_LG_PARADOX
} swarm_lg_kind_t;

typedef struct {
    swarm_lg_kind_t  kind;
    swarm_claim_t    out;          /* the claim that goes forward */
    uint32_t         freed;        /* tokens released back to the budget */
    bool             escalate;     /* L7 */
    swarm_hk_truth_t truth;
} swarm_lg_result_t;

swarm_lg_result_t swarm_logic_meet(const swarm_claim_t *a, const swarm_claim_t *b);
swarm_hk_truth_t  swarm_logic_truth(const swarm_claim_t *c);

#endif /* SWARM_LOGIC_H */
