/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* pdx_agent.h — four-valued agents that carry contradictory premises
 * (roadmap 6.6b, P2.2). Design note: docs/PARACONSISTENT_LEDGER.md.
 *
 *   G1 SUPPORT SETS. For each proposition the agent keeps the set of
 *      sources that told it "true" and the set that told it "false" (bit
 *      masks over up to 64 sources). Belnap-Dunn values: neither set ->
 *      Neither, only true -> True, only false -> False, both -> Both.
 *   G2 RULES. head <- l1 & l2 & ... (literals may be negated). A conjunction
 *      is told true when every literal is (support: the union) and told
 *      false when any literal is (support: the union of the false ones). A
 *      head is told true by any rule whose body is, and told false by its
 *      rules only when every one of them is (Fitting's semantics); facts
 *      about the head join both sets. Sets only grow, so the fixed point is
 *      reached in a bounded number of rounds.
 *   G3 NO EXPLOSION. p and not-p make conclusions that depend on p Both;
 *      a proposition that does not depend on p is untouched.
 *   G4 SELF-CONTRADICTION IS FORBIDDEN. A source that told the agent both p
 *      and not-p is masked out of every derivation and quarantined (once).
 *      It is computed from the fact set, so arrival order does not matter.
 *   G5 MERGE. pag_merge ORs the fact sets: commutative, associative and
 *      idempotent, so agents converge whatever order they gossip in.
 *   G6 DECISIONS. pag_decide: base from G1, polarity from the trend of the
 *      support counts since the last snapshot (lpx_polarity), phase
 *      manifest; pdx_classify gives the kind and lpx_action the action
 *      (proceed, wait, refuse, escalate, roll back).
 *
 * Note: G2 follows Belnap-Dunn exactly, so Neither & Both is False (the Both
 * side is told false); lpres_conjoin gives Neither for that one case. The
 * agent does not use lpres's tables. Freestanding C11: no libc, no
 * allocation, no floating point; every array is the caller's.
 */
#ifndef PDX_AGENT_H
#define PDX_AGENT_H

#include <stdbool.h>
#include <stdint.h>
#include "paradox.h"

#define PAG_MAX_BODY    4u
#define PAG_MAX_SOURCES 64u

typedef uint64_t pag_set_t;

typedef struct {
    uint32_t head;
    uint8_t n;                  /* literals in the body, 1..PAG_MAX_BODY */
    uint32_t lit[PAG_MAX_BODY]; /* propositions */
    bool neg[PAG_MAX_BODY];     /* the literal is negated */
} pag_rule_t;

typedef struct {
    uint32_t n_prop;
    pag_set_t *ft, *ff;    /* facts: told true / told false, by source */
    pag_set_t *t, *f;      /* derived support after pag_run */
    uint32_t *pos0, *neg0; /* support counts at the last snapshot */
    const pag_rule_t *rule;
    uint32_t n_rule;
    pag_set_t bad;     /* sources that contradicted themselves (G4) */
    pag_set_t qlogged; /* bad sources already quarantined */
    uint32_t rounds;   /* rounds the last pag_run took */
} pag_agent_t;

/* reason code in the quarantine log */
enum { PAG_Q_SELF_CONTRADICTION = 4 };

typedef struct {
    lpx_t tag;
    uint8_t kind; /* pdx_kind_t */
    lpx_action_t act;
    uint32_t pos, neg; /* support counts now */
} pag_decision_t;

/* All arrays n_prop long. False for missing storage, n_prop 0, or a rule
 * naming a proposition out of range or with a body of 0 or too many. */
bool pag_init(pag_agent_t *a, uint32_t n_prop, pag_set_t *ft, pag_set_t *ff, pag_set_t *t,
              pag_set_t *f, uint32_t *pos0, uint32_t *neg0, const pag_rule_t *rules,
              uint32_t n_rule);
/* Source `src` tells the agent prop is `value`. False when out of range. */
bool pag_tell(pag_agent_t *a, uint32_t prop, uint32_t src, bool value);
/* G5: dst gets every fact src has. False when the sizes differ. */
bool pag_merge(pag_agent_t *dst, const pag_agent_t *src);
/* G4 then G2. q may be NULL (no quarantine records). False when the round
 * limit (2 * 64 * n_prop + 1, more than the sets can grow) is hit or a
 * quarantine record does not fit (fail closed). */
bool pag_run(pag_agent_t *a, lpx_qlog_t *q);
/* The proposition's value after pag_run (NEITHER when out of range). */
lpres_state_t pag_value(const pag_agent_t *a, uint32_t prop);
/* Remember the support counts, for the next decisions' trend. */
void pag_snapshot(pag_agent_t *a);
/* G6. stale: the newest evidence is past the caller's freshness limit.
 * reg and sig may be NULL (no registry lookup). */
pag_decision_t pag_decide(const pag_agent_t *a, uint32_t prop, bool stale,
                          const pdx_registry_t *reg, const uint8_t sig[PDX_HASH]);
/* Number of sources in a set. */
uint32_t pag_count(pag_set_t s);

#endif /* PDX_AGENT_H */
