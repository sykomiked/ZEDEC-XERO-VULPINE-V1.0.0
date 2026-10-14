/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* murmuration_model.h — Murmuration, the swarm dashboard, as a model (zxm.h):
 * one snapshot of the live swarm (zxvd's swarm.state), the model slot and
 * the swarm's earnings in the owner's books, the selected agent, and AI
 * that explains what the swarm is doing. No drawing, no allocation.
 *
 * WHAT THE NUMBERS MEAN (zxv_host.c state_json, swarm_emotion.h,
 * swarm_ledger.h)
 *   A1  An agent's allotment for the last closed market cycle is the triple
 *       re + mk + im tokens: re is the real part its level earns under the
 *       Fibonacci level rule (swarm_budget.h), mk the real part it bought on
 *       the market (swarm_market.h), im the imaginary part its emotional
 *       charge earns (swarm_emotion.h E1-E3). The allotments of one cycle
 *       add up to that cycle's tokens (swarm_ledger.h L1).
 *   A2  emoji + intensity is the agent's feeling (intensity 0..5, charge
 *       0 1 2 3 5 8); mood is the whole swarm's, sent as "<emoji> <k>".
 *   A3  witness is the trit another agent gave this agent's allotment
 *       (swarm_ledger.h W1-W3): MURM_WIT_TRUE agrees, MURM_WIT_FALSE the
 *       cycle does not add up, MURM_WIT_GLUT it adds up but this allotment
 *       differs (contested). witness_by names the witness; "kernel" when
 *       the swarm has one agent or no record was made (then TRUE).
 *   A4  ledger_halt is 0 while the settlement spine runs, else the
 *       settle_status_t (negative) it halted on.
 *   A5  Earnings are the owner's books (zxvd_rpc.h E1-E5): minor units of
 *       `currency`, sent as decimal strings and read as exact integers.
 *
 * REFRESH
 *   R1  murm_refresh asks swarm.state and then swarm.earnings; model.info
 *       only when the state carries no "model" object.
 *   R2  Either way it is all or nothing: a failed call or a malformed reply
 *       keeps the previous snapshot whole. A refusal of the optional parts
 *       (model.info, swarm.earnings: -32601 "not available", or books that
 *       are not open) marks that part absent (have_model, have_earnings
 *       false, the code in model_code / earn_code) and the refresh succeeds.
 *   R3  A field zxvd may leave out is optional; one that is present with
 *       the wrong type or out of range fails the refresh (ZXM_E_PROTOCOL).
 *       Text longer than its field is cut on a character boundary.
 *   R4  The selection follows the agent's id across refreshes and clears
 *       when that agent is gone. A held AI answer is kept. */
#ifndef MURMURATION_MODEL_H
#define MURMURATION_MODEL_H

#include "zxm.h"

#define MURM_AGENTS     64u /* agents kept from one snapshot */
#define MURM_NAME_MAX   31u /* zxv_host's agent names are char[32] */
#define MURM_EMOJI_MAX  15u /* bytes of one emoji, UTF-8 */
#define MURM_TEXT_MAX   63u /* version, host, audit, model name and arch, currency */
#define MURM_STATUS_MAX 255u

enum { MURM_WIT_FALSE = 0, MURM_WIT_TRUE = 1, MURM_WIT_GLUT = 2 }; /* swarm_ledger.h W3 */

typedef struct {
    uint32_t id;
    char name[MURM_NAME_MAX + 1];
    uint32_t level;                 /* its Fibonacci level; 0 is the companion */
    char emoji[MURM_EMOJI_MAX + 1]; /* its feeling, UTF-8 (A2) */
    uint32_t codepoint;             /* the emoji's first code point; 0 if none */
    uint8_t intensity;              /* 0 .. 5 */
    uint64_t re, mk, im;            /* last cycle's allotment (A1) */
    uint64_t money, social;         /* Financial and Social capital; 0 if not sent */
    uint32_t gen;                   /* DNA generation; 0 if not sent */
    uint8_t witness;                /* MURM_WIT_* (A3) */
    char witness_by[MURM_NAME_MAX + 1];
} murm_agent_t;

/* One swarm.state, as the model keeps it. */
typedef struct {
    char version[MURM_TEXT_MAX + 1];
    char host[MURM_TEXT_MAX + 1];
    uint32_t cores, levels;
    uint64_t tokens_per_cycle, tick, cycle;
    char mood[MURM_EMOJI_MAX + 1]; /* "" when not sent */
    uint32_t mood_codepoint;
    uint8_t mood_intensity;
    char audit[MURM_TEXT_MAX + 1]; /* "off", "active", "contradiction"; "" when not sent */
    uint64_t audit_records;
    uint64_t ledger_postings;
    int32_t ledger_halt; /* 0 = running (A4) */
    murm_agent_t agent[MURM_AGENTS];
    uint32_t n;        /* agents kept */
    uint32_t n_total;  /* agents in the swarm */
    bool truncated;    /* n_total > MURM_AGENTS */
    uint64_t allotted; /* every agent's re + mk + im, kept or not; saturates */
} murm_swarm_t;

typedef struct {
    bool loaded, can_generate;
    char name[MURM_TEXT_MAX + 1];
    char arch[MURM_TEXT_MAX + 1];
    char status[MURM_STATUS_MAX + 1];
} murm_model_t;

typedef struct {
    char currency[MURM_TEXT_MAX + 1];
    int64_t rate_minor;    /* minor units per SWC */
    int64_t balance_minor; /* the owner's earnings balance */
    uint64_t postings, cycles;
    bool resumed;
} murm_earnings_t;

typedef struct {
    zxm_link_t *link;
    murm_swarm_t sw;
    bool have_model;
    murm_model_t model;
    int32_t model_code; /* zxvd's error code when the model part is absent */
    bool have_earnings;
    murm_earnings_t earn;
    int32_t earn_code;  /* zxvd's error code when earnings are absent */
    uint64_t refreshes; /* snapshots taken; 0 = none yet */
    int32_t sel;        /* selected agent, or -1 */
    zxm_held_t held;    /* the last AI answer (M3) */
} murmuration_t;

/* The palette (M5). */
enum { MURM_AI_EXPLAIN, MURM_AI_WHY_UNHAPPY, MURM_AI_COUNT };
extern const zxm_ai_action_t MURM_AI[MURM_AI_COUNT];

void murm_init(murmuration_t *m, zxm_link_t *link);          /* no snapshot yet */
int murm_refresh(murmuration_t *m);                          /* R1-R4 */
int murm_select(murmuration_t *m, int32_t i);                /* -1 clears */
int32_t murm_find(const murmuration_t *m, const char *name); /* index, or -1 */

/* The swarm_emotion_t (SWARM_EMO_*) for an emoji code point, or -1 when it
 * is not in the palette (swarm_emotion_from_codepoint). */
int murm_emotion(uint32_t codepoint);

/* Agent i's share of the cycle in per-mille: its re + mk + im over every
 * agent's (sw.allotted), rounded down. 0 past the end or when nothing was
 * allotted. */
uint32_t murm_cycle_share(const murmuration_t *m, uint32_t i);

/* AI over the snapshot; the answer is held (M3). MURM_AI_WHY_UNHAPPY needs
 * a selected agent (ZXM_E_STATE, nothing sent, without one); both need a
 * snapshot. Accepting only marks it read: it changes nothing in zxvd. */
int murm_ai(murmuration_t *m, int32_t action);
int murm_ai_accept(murmuration_t *m);
int murm_ai_reject(murmuration_t *m);

#endif
