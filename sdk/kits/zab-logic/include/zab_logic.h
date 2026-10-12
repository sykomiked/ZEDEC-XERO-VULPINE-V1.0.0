/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zab_logic.h — ZAB-Logic: per-cycle token budgets and a decision gate for
 * a swarm of agents, on the public libzxv ABI (zxv_api.h) only.
 *
 * WHAT IT DOES
 * ------------
 * A zab_swarm_t wraps one zxv_swarm (the Fibonacci + emotional token
 * economy) and one zxv_hexlog (the quarantine log). Each cycle:
 *
 *   1. zab_cycle_begin   the library splits T tokens across the levels
 *                        (Fibonacci weights, equal shares inside a level)
 *                        and moves the mood's share onto the emotional axis,
 *                        split by each agent's feeling. ZAB-Logic does not
 *                        compute any allotment itself: it reads them back
 *                        with zxv_swarm_allotment.
 *   2. zab_propose       an agent proposes work: a 64-state hexagram tag, a
 *                        token cost and the work bytes. The gate is the
 *                        library's action table (zxv_hex_action):
 *                          polarity FORBIDDEN (action ROLLBACK)
 *                                       -> ZAB_QUARANTINED, appended to the
 *                                          hash-chained quarantine log;
 *                          base BOTH    -> ZAB_HELD for review;
 *                          base FALSE   -> ZAB_REFUSED;
 *                          action WAIT  -> ZAB_DEFERRED (NEITHER, or a TRUE
 *                                          weakening under ENTROPIC evidence);
 *                          PROCEED but phase not MANIFEST -> ZAB_DEFERRED;
 *                          PROCEED, MANIFEST, cost > remaining allotment
 *                                       -> ZAB_OVER_BUDGET (nothing spent);
 *                          PROCEED, MANIFEST, cost fits
 *                                       -> ZAB_ADMITTED (zxv_swarm_consume).
 *   3. zab_review        a held (BOTH) proposal gets a reviewer's base and
 *                        polarity (zxv_hex_set keeps its phase) and goes
 *                        through the gate again in the current cycle.
 *   4. zab_cycle_end     a report: every agent's allotment (real and
 *                        emotional), use, gate outcomes and behaviour
 *                        profile, the expired tokens and the log head.
 *
 * NO MONOPOLY. The library already caps every agent at its own allotment
 * (zxv_swarm_consume never grants past it) and every level at F(d+2)
 * agents (zxv_swarm_register gives ZXV_E_CAPACITY). ZAB-Logic does not add
 * a second cap; its tests prove the library's. An agent's allotment is its
 * equal share of its level's Fibonacci-weighted tokens plus its share of
 * the emotional pool by its own charge.
 *
 * RULES. Status codes are the libzxv ZXV_* codes (int32_t). No floats.
 * Every call goes through the zxv_cap given at creation, so the library
 * re-checks it each time (revoking it stops the swarm: ZXV_E_CAP). A
 * zab_swarm_t is NOT thread-safe: use it from one thread at a time.
 *
 * LIFETIME. The zxv_ctx and zxv_cap passed to zab_swarm_create must outlive
 * the zab_swarm_t; destroy the swarm first.
 *
 * Kit API 0.1 (preview): may change until 1.0.
 */
#ifndef ZAB_LOGIC_H
#define ZAB_LOGIC_H

#include <stddef.h>
#include <stdint.h>

#include "zxv_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ZAB_VERSION_MAJOR 0
#define ZAB_VERSION_MINOR 1
#define ZAB_VERSION_PATCH 0

#define ZAB_MAX_AGENTS 64u /* the swarm's model table (SWARM_MAX_MODELS) */
#define ZAB_MAX_LEVELS 8u  /* zxv_swarm_level_capacity accepts 0..7 */
#define ZAB_MAX_HELD   64u /* BOTH proposals awaiting review */

/* The capabilities a zab_swarm_t needs on its cap. */
#define ZAB_CAPS_NEEDED (ZXV_CAP_SWARM | ZXV_CAP_ATTEST)

/* Gate outcomes (zab_decision_t.outcome). */
#define ZAB_ADMITTED    0
#define ZAB_QUARANTINED 1
#define ZAB_HELD        2
#define ZAB_REFUSED     3
#define ZAB_DEFERRED    4
#define ZAB_OVER_BUDGET 5
#define ZAB_OUTCOMES    6

/* Reason code written with every quarantine record. */
#define ZAB_REASON_FORBIDDEN 1u

typedef struct zab_swarm zab_swarm_t;

/* An agent to add: an id unique in the swarm, a level 0..levels-1, and
 * its feeling (an emoji of the kernel palette, intensity 0..5). A
 * feeling_codepoint of 0 means the neutral face U+1F610 at intensity 0. */
typedef struct {
    uint32_t id;
    uint32_t level;
    uint32_t feeling_codepoint;
    uint32_t feeling_intensity;
} zab_agent_t;

/* A proposal: who, its hexagram tag (0..63), its token cost (> 0), and the
 * work itself. The work bytes are hashed (SHA3-256) into the subject that
 * a quarantine record or a held entry keeps; they are not stored. */
typedef struct {
    uint32_t agent_id;
    uint8_t tag;
    uint64_t tokens;
    const uint8_t *work;
    size_t work_len;
} zab_proposal_t;

typedef struct {
    int32_t outcome;         /* ZAB_* */
    int32_t action;          /* ZXV_HEX_ACT_* of the tag that was gated */
    uint8_t tag;             /* the tag that was gated */
    uint64_t granted;        /* tokens spent (ADMITTED only, else 0) */
    uint64_t remaining;      /* the agent's allotment left after this call */
    uint64_t quarantine_seq; /* QUARANTINED only */
    uint32_t held_id;        /* HELD only: pass to zab_review */
    uint8_t subject[32];     /* SHA3-256 of the work */
} zab_decision_t;

/* One agent in a cycle report. */
typedef struct {
    uint32_t id;
    uint32_t level;
    int32_t active;
    uint64_t allotted;  /* real + emotional, as the library computed it */
    uint64_t imaginary; /* the emotional part of allotted */
    uint64_t used;
    uint32_t outcomes[ZAB_OUTCOMES]; /* gate outcomes this cycle, by ZAB_* */
    uint32_t feeling_codepoint;
    uint32_t feeling_intensity;
    /* zxv_swarm_emotion_profile of the agent's feeling */
    int32_t valence;
    uint32_t temperature_milli;
    uint32_t explore_permille;
    uint32_t verify_passes;
    uint32_t priority;
} zab_agent_report_t;

typedef struct {
    uint64_t cycle;            /* the library's cycle number */
    uint64_t tokens_per_cycle; /* T this cycle */
    uint32_t mood_codepoint;
    uint32_t mood_intensity;
    uint64_t allotted_total;   /* == T when any agent is active, else 0 */
    uint64_t imaginary_total;
    uint64_t used_total;
    uint64_t expired;          /* unused tokens; they never carry over */
    uint32_t outcomes[ZAB_OUTCOMES];
    uint32_t held_pending;     /* BOTH proposals still awaiting review */
    uint32_t quarantine_count; /* records in the log, all cycles */
    uint8_t quarantine_head[32];
    uint32_t num_agents;
    zab_agent_report_t agents[ZAB_MAX_AGENTS]; /* in the order they were added */
} zab_cycle_report_t;

/* A held entry (zab_held_at). */
typedef struct {
    uint32_t held_id;
    uint32_t agent_id;
    uint8_t tag;
    uint64_t tokens;
    uint64_t cycle; /* the cycle it was proposed in */
    uint8_t subject[32];
} zab_held_t;

/* Create a swarm of `levels` (1..8) Fibonacci levels with T =
 * tokens_per_cycle, and a quarantine log of `quarantine_capacity` records
 * (1..ZXV_HEXLOG_MAX_RECORDS). `cap` needs ZAB_CAPS_NEEDED (else ZXV_E_CAP)
 * and must belong to `ctx` (checked: ZXV_E_CAP otherwise). */
int32_t zab_swarm_create(zxv_ctx *ctx, const zxv_cap *cap, uint32_t levels,
                         uint64_t tokens_per_cycle, uint32_t quarantine_capacity,
                         zab_swarm_t **out_swarm);
void zab_swarm_destroy(zab_swarm_t *sw);

/* Add an agent (ZXV_E_CAPACITY when its level holds F(level+2) agents,
 * ZXV_E_DUPLICATE for a used id, ZXV_E_STATE while a cycle is open). */
int32_t zab_agent_add(zab_swarm_t *sw, const zab_agent_t *agent);
/* Park (0) or wake (1) an agent from the next cycle on. */
int32_t zab_agent_set_active(zab_swarm_t *sw, uint32_t agent_id, int32_t active);
/* Change an agent's feeling, from the next cycle on. */
int32_t zab_agent_set_feeling(zab_swarm_t *sw, uint32_t agent_id, uint32_t codepoint,
                              uint32_t intensity);
/* The swarm's mood (charge(intensity)/21 of T goes to the emotional axis
 * when any active agent has a charge), from the next cycle on. */
int32_t zab_set_mood(zab_swarm_t *sw, uint32_t codepoint, uint32_t intensity);
/* T from the next cycle on (at most 2^48, the library's bound). */
int32_t zab_set_rate(zab_swarm_t *sw, uint64_t tokens_per_cycle);

/* Open a cycle (ZXV_E_STATE if one is open). *out_cycle may be NULL. */
int32_t zab_cycle_begin(zab_swarm_t *sw, uint64_t *out_cycle);
/* Gate one proposal; see the table at the top. ZXV_OK for every outcome
 * (the outcome is the decision, not an error). Errors: ZXV_E_ARG (tag >
 * 63, tokens == 0, NULL), ZXV_E_NOT_FOUND (unknown agent), ZXV_E_STATE (no
 * open cycle, or the swarm failed closed), ZXV_E_CAPACITY (held queue full,
 * nothing recorded; or quarantine log full: the swarm then fails closed and
 * the context is halted, because forbidden work that cannot be recorded
 * must not continue), ZXV_E_CAP / ZXV_E_POISONED from the library. */
int32_t zab_propose(zab_swarm_t *sw, const zab_proposal_t *proposal, zab_decision_t *out);
/* Resolve a held proposal: new base and polarity (ZXV_HEX_BASE_*,
 * ZXV_HEX_POL_*) on its tag, phase kept, then gated again in the open
 * cycle. The held entry is removed whatever the new outcome (a BOTH again
 * is held again under a new id). ZXV_E_NOT_FOUND for an unknown held_id. */
int32_t zab_review(zab_swarm_t *sw, uint32_t held_id, uint8_t base, uint8_t polarity,
                   zab_decision_t *out);
/* Close the cycle and fill *report (may be NULL). Unused tokens expire. */
int32_t zab_cycle_end(zab_swarm_t *sw, zab_cycle_report_t *report);

/* The agent's allotment in the open cycle (any out may be NULL). */
int32_t zab_agent_allotment(zab_swarm_t *sw, uint32_t agent_id, uint64_t *out_allotted,
                            uint64_t *out_imaginary, uint64_t *out_used);
uint32_t zab_held_count(const zab_swarm_t *sw);
int32_t zab_held_at(const zab_swarm_t *sw, uint32_t index, zab_held_t *out);
/* The kit's quarantine log, for audit (zxv_hexlog_at / _verify / _head).
 * Owned by the swarm: do not destroy it. */
zxv_hexlog *zab_quarantine_log(zab_swarm_t *sw);
/* 1 once the swarm failed closed (quarantine log full). */
int32_t zab_failed_closed(const zab_swarm_t *sw);

/* Static name of a ZAB_* outcome ("admitted"); "INVALID" otherwise. */
const char *zab_outcome_name(int32_t outcome);
/* A plain-text report, two-call pattern (R4): *out_len is the length
 * without the NUL; ZXV_E_BUFFER when buf is NULL or too small. */
int32_t zab_report_format(const zab_cycle_report_t *report, char *buf, size_t cap,
                          size_t *out_len);
/* This thread's last error detail from a zab_* call ("" if none). */
int32_t zab_last_error(char *buf, size_t cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif /* ZAB_LOGIC_H */
