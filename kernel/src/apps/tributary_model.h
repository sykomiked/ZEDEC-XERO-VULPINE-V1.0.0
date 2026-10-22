/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* tributary_model.h — Tributary, many-model answers, as a model (zxm.h):
 * one question put to several models, their answers joined by witnessed
 * agreement. The rules are tributary_rules.h.
 *
 * zxvd methods used: model.info, swarm.ask, swarm.state, play.run,
 * peer.ask and power.status. Nothing else is called and none is invented.
 * No drawing, no allocation, integers only; the time comes from the
 * caller's clock (ms since 1970), since a model makes no system calls.
 *
 *   TM1 THE SLOT: model.info's loaded, can_generate and name are kept; an
 *       answer without loaded and name is not zxvd's (ZXM_E_PROTOCOL).
 *       With no model loaded swarm.ask reads shorthand back, and the
 *       notice says so.
 *   TM2 CAPS (owner-confirmed): the most this question may spend, tokens
 *       1..TRIB_TOKEN_ROOM_MAX, energy 1..TRIB_ROOM_MAX uJ and price
 *       0..TRIB_ROOM_MAX VFV minor units, the quorum 1..TRIB_SOURCES and
 *       the octane 0..TRIB_TIER_MAX the query ledger enters tokens at.
 *       Arguments are checked first (ZXM_E_ARG), then the owner's word
 *       (ZXM_E_STATE without it, nothing changed). Nothing is asked before
 *       caps are set.
 *   TM3 A QUESTION: 1..RETORT_PROMPT_MAX bytes (retort.run_limits_ok);
 *       a new question drops the last one's answers and ledger.
 *   TM4 ASKING: up to TRIB_SOURCES answers a question, each from the slot
 *       (swarm.ask, then swarm.state's companion last_answer charged as its
 *       tokens, an estimate of a token per four bytes when absent, M4), a
 *       template (play.run, its verdict the answer, no tokens) or a peer
 *       (peer.ask). Before each ask the caps' margins, cap less spent less
 *       what the ask may spend, must be room (TB3), else ZXM_E_STATE and
 *       nothing is sent. A peer ask is a priced action: its margin counts
 *       its max price, and it needs the owner's word, checked after the
 *       arguments and the caps and before anything is sent. After each
 *       answer power.status's total_uj is its energy; its record must fit
 *       ^ZXTRIB (TB7) and its tokens the ledger (TB6, entered at the
 *       octane in canonical form, swarm_denom.h D3) before anything is
 *       kept. The answer's text is normalised (ASCII letters lowered,
 *       spaces trimmed and collapsed, a closing . ! or ? dropped) and
 *       numbered by first appearance in the question: its code.
 *   TM5 THE JOIN (tributary_join): every answer's agree count (TB1), the
 *       distinct models behind its code (TB5), its confidence (TB4), the
 *       verdict (TB2), the lead code (none on a tie), the share of models
 *       behind the lead (assay.pass_share_x100) and whether it meets the
 *       bar (assay.meets_bar: a majority, TRIB_BAR_X100, no refusal and
 *       the quorum reached), each model's points (retort.model_score_total,
 *       100 for each answer that agrees with the lead), the energy per
 *       answer (retort.energy_per_answer_uj) and the fastest and slowest
 *       answer (retort's latency fold). The answers stay held (M3).
 *   TM6 THE SHOWING (tributary_show): each code once with its answer and
 *       every model that gave it, so a split is shown whole.
 *   TM7 THE OWNER'S WORD: accept one given code (owner-confirmed), only
 *       when it follows from the verdict (twelvefold.lp_entail_kept);
 *       accepting changes nothing in zxvd. Reject drops the held answers'
 *       standing; both once per join.
 *   TM8 TOKEN SHARE: each source's share of the question's tokens, per
 *       mille (TB8, swarm_shares.cycle_share's meaning). */
#ifndef TRIBUTARY_MODEL_H
#define TRIBUTARY_MODEL_H

#include "tributary_rules.h"
#include "zxm.h"

#define TRIB_SOURCES        8u   /* answers to one question; model:i2 holds far more */
#define TRIB_TEXT           255u /* an answer kept, cut on a UTF-8 boundary */
#define TRIB_NAME           63u  /* a model name, template id or peer address */
#define TRIB_NOTICE         383u
#define TRIB_DEFAULT_QUORUM 2
#define TRIB_BAR_X100       INT64_C(5000) /* TM5: a majority, the default taken */

enum { TRIB_KIND_SLOT = 0, TRIB_KIND_PLAY = 1, TRIB_KIND_PEER = 2 };

typedef int64_t (*tributary_clock_fn)(void *ctx); /* ms since 1970 */

typedef struct {
    int64_t kind;
    char name[TRIB_NAME + 1];
    char text[TRIB_TEXT + 1]; /* as given */
    char norm[TRIB_TEXT + 1]; /* normalised (TM4) */
    int64_t code;             /* TM4 */
    int64_t model;            /* the first source of the same kind and name */
    int64_t tokens, uj, ms, paid, stamp;
} tributary_answer_t;

typedef struct {
    zxm_link_t *link;
    tributary_clock_fn clock;
    void *clock_ctx;
    /* TM1 */
    bool slot_known, can_generate;
    char slot_name[TRIB_NAME + 1];
    /* TM2 */
    bool caps_set;
    int64_t cap_tokens, cap_uj, cap_price, quorum, octane;
    /* TM3, TM4 */
    int64_t question; /* 0 before the first */
    char prompt[RETORT_PROMPT_MAX + 1];
    int64_t prompt_len;
    int64_t spent_tokens, spent_uj, spent_price;
    int64_t ledger; /* TB6: the question's tokens entered so far */
    uint32_t n;
    tributary_answer_t a[TRIB_SOURCES];
    /* TM5 */
    bool joined;
    int64_t agree[TRIB_SOURCES], models[TRIB_SOURCES];
    int64_t conf_num[TRIB_SOURCES], conf_den[TRIB_SOURCES];
    lpres_state_t verdict;
    int64_t points[TRIB_SOURCES]; /* answer i's model's points */
    int64_t lead, lead_share_x100;
    bool lead_meets_bar;
    int64_t uj_num, uj_den;   /* 0/0 with no answer */
    int64_t fastest, slowest; /* -1 with no answer */
    /* TM7 */
    uint8_t held; /* ZXM_HELD_* */
    int64_t accepted;
    char notice[TRIB_NOTICE + 1];
} tributary_t;

void tributary_init(tributary_t *t, zxm_link_t *link, tributary_clock_fn clock, void *clock_ctx);
/* TM1: model.info. */
int tributary_read_slot(tributary_t *t);
/* TM2 */
int tributary_set_caps(tributary_t *t, int64_t tokens, int64_t uj, int64_t price, int64_t quorum,
                       int64_t octane, bool owner_confirmed);
/* TM3 */
int tributary_question(tributary_t *t, const char *prompt);
/* TM4: one answer more; ZXM_E_STATE when no question is open, it is full,
 * caps are unset, a cap has no room, or (a peer) without the owner's word;
 * ZXM_E_ARG on an empty or overlong template or peer, a max price outside
 * 0..TRIB_ROOM_MAX. */
int tributary_ask_slot(tributary_t *t);
int tributary_ask_play(tributary_t *t, const char *template_id);
int tributary_ask_peer(tributary_t *t, const char *peer, int64_t max_price, bool owner_confirmed);
/* TM5: ZXM_E_STATE with no answer. */
int tributary_join(tributary_t *t);
/* TM6: the bytes written (cut to cap), or ZXM_E_STATE before a join. */
int32_t tributary_show(const tributary_t *t, char *out, uint32_t cap);
/* TM7 */
int tributary_accept(tributary_t *t, int64_t code, bool owner_confirmed);
int tributary_reject(tributary_t *t);
/* TM8: answer i's share per mille, or TRIB_REFUSED. */
int64_t tributary_token_share(const tributary_t *t, uint32_t i);

#endif
