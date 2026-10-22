/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* chg_bond.h — the chiglet gets to know its person.
 *
 * WHAT IT IS
 * ----------
 * A small, private, on-device model of one person's habits. It watches what
 * they do (an action id) in context (hour of the week, the app in front,
 * the action just before) and learns which action tends to come next, so
 * the chiglet can offer the right thing at the right moment. Every offer is
 * a suggestion held until the person accepts it; nothing runs on its own.
 *
 * HOW IT LEARNS (Interaction Surplus Framework, integer form)
 * -----------------------------------------------------------
 *   B1  Three experts read the context: TIME (168 hour-of-week rows), APP
 *       (one row per app) and PREV (one row per previous action). Each row
 *       is a weight vector over the person's own action vocabulary.
 *   B2  Each row is an open system (ISF Paper E): on every observation
 *       Q <- Q - Q*delta + eta*S - C. An observed action adds eta, an
 *       accepted suggestion adds eta again, a rejected one costs C, and the
 *       whole row decays by delta, so old habits fade as new ones form.
 *   B3  The experts are combined by how INDEPENDENT their evidence is, not
 *       how loudly they agree: u = 1 - (a.b)^2 / (|a|^2 |b|^2) (the ISF
 *       interaction parameter, exactly, in integers). Experts whose rows
 *       point the same way (u < 2%) are merged into one voice, and each
 *       voice is weighted by its independence from the others. The result
 *       reports R, the effective number of independent reasons.
 *   B4  It offers a suggestion only when the top action holds a clear share
 *       of the evidence and has been seen more than once; otherwise it says
 *       it is not sure.
 *
 * WHOSE IT IS
 * -----------
 *   P1  Bound to the owner's key id at birth. Everything stays on the device.
 *   P2  The owner can pause learning, inspect every row (why a suggestion
 *       was made), and forget one action, one app, or everything.
 *   P3  Export (to move to the owner's own new device) is refused unless the
 *       owner has opted in, and import accepts only the same owner's bond.
 *       The export is integrity-checked, not encrypted: seal it with the
 *       transport (Vinea / pq_matrix) before it leaves the device.
 *
 * HONEST LIMITS
 *  - A frequency model with three context experts, not a neural network.
 *    It learns routines, not reasons.
 *  - 32 actions and 48 apps are tracked; a new one evicts the least used.
 *  - Freestanding: integer only, no libc, no allocation, no 64-bit division
 *    instruction (a local shift-subtract divider is used).
 */
#ifndef ZXV_CHG_BOND_H
#define ZXV_CHG_BOND_H

#include <stdint.h>
#include <stdbool.h>

#define CHGB_ACTIONS 32u
#define CHGB_SLOTS   168u /* hours in a week */
#define CHGB_APPS    48u
#define CHGB_EXPERTS 3u
#define CHGB_WMAX    1023u
#define CHGB_NONE    0xffffu
#define CHGB_U_MERGE 20u /* permille: below this two experts are one voice */

typedef enum {
    CHGB_OK = 0,
    CHGB_ERR_ARG = -1,
    CHGB_PAUSED = -2, /* learning is paused: nothing was stored */
    CHGB_DENIED = -3, /* not the owner, or the owner has not opted in */
    CHGB_ERR_PARSE = -4,
    CHGB_ERR_SPACE = -5
} chgb_status_t;

typedef enum { CHGB_NOTHING = 0, CHGB_SUGGEST = 1, CHGB_UNSURE = 2 } chgb_state_t;
typedef enum { CHGB_E_TIME = 0, CHGB_E_APP = 1, CHGB_E_PREV = 2 } chgb_expert_t;

typedef struct {
    uint16_t slot; /* 0..167, hour of the week */
    uint16_t app;  /* app id, CHGB_NONE if none */
    uint16_t prev; /* previous action id, CHGB_NONE if none */
} chgb_ctx_t;

typedef struct {
    uint8_t owner[32];
    bool learning;
    bool share_opt_in;
    uint16_t eta;            /* B2 growth per observation (default 64) */
    uint16_t cost;           /* B2 cost of a rejected suggestion (default 96) */
    uint16_t decay_permille; /* B2 delta per row update (default 8) */
    uint16_t min_share;      /* B4 permille of evidence the top action needs (600) */
    uint16_t n_act;
    uint16_t act[CHGB_ACTIONS];
    uint16_t n_app;
    uint16_t app[CHGB_APPS];
    uint16_t w_time[CHGB_SLOTS][CHGB_ACTIONS];
    uint16_t w_app[CHGB_APPS][CHGB_ACTIONS];
    uint16_t w_prev[CHGB_ACTIONS][CHGB_ACTIONS];
    uint32_t observed, accepted, rejected;
} chgb_t;

typedef struct {
    chgb_state_t state;
    uint16_t action;                        /* meaningful when state == CHGB_SUGGEST */
    uint16_t share;                         /* permille of combined evidence on action */
    uint16_t second;                        /* permille on the runner-up */
    uint8_t experts;                        /* experts with any evidence */
    uint8_t distinct;                       /* independent voices after merging */
    uint32_t R_permille;                    /* ISF effective independent reasons x1000 */
    uint16_t u[CHGB_EXPERTS][CHGB_EXPERTS]; /* pairwise independence, permille */
    uint16_t support[CHGB_EXPERTS];         /* each expert's weight on action */
} chgb_suggestion_t;

void chgb_init(chgb_t *b, const uint8_t owner[32]);
void chgb_set_learning(chgb_t *b, bool on);
/* P3: only the owner may change sharing. */
chgb_status_t chgb_set_share(chgb_t *b, const uint8_t who[32], bool on);

/* B1/B2: the person did `action` in context ctx. */
chgb_status_t chgb_observe(chgb_t *b, const chgb_ctx_t *ctx, uint16_t action);
/* B3/B4 */
chgb_status_t chgb_suggest(const chgb_t *b, const chgb_ctx_t *ctx, chgb_suggestion_t *out);
/* B2: the person accepted or turned down a suggestion of `action`. */
chgb_status_t chgb_feedback(chgb_t *b, const chgb_ctx_t *ctx, uint16_t action, bool accepted);
/* B2: decay every row once (call on a slow timer, e.g. daily). */
void chgb_decay_all(chgb_t *b);

/* P2 */
void chgb_forget_action(chgb_t *b, uint16_t action);
void chgb_forget_app(chgb_t *b, uint16_t app);
void chgb_forget_all(chgb_t *b);
/* Total learned weight in an app's row: how much this person uses it. The
 * shell can rank apps by it, so each install arranges itself around its
 * person. 0 for an unknown app. */
uint32_t chgb_app_affinity(const chgb_t *b, uint16_t app);

/* P3. Returns bytes written, or a negative chgb_status_t. */
int32_t chgb_export(const chgb_t *b, const uint8_t who[32], uint8_t *out, uint32_t cap);
chgb_status_t chgb_import(chgb_t *b, const uint8_t owner[32], const uint8_t *in, uint32_t len);
uint32_t chgb_export_size(void);

/* The ISF interaction parameter of two weight rows, permille. Exposed for
 * tests and for the visit module. Zero rows give 0. */
uint32_t chgb_interaction(const uint16_t *a, const uint16_t *b, uint32_t n);

#endif /* ZXV_CHG_BOND_H */
