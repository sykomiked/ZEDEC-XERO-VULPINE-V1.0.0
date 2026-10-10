/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* swarm_market.h — the free market for swarm tokens, over the nine forms of
 * capital, with cooperation and no monopoly. Design: docs/SWARM_ECONOMY.md.
 *
 * A CYCLE
 * -------
 *   M1  EMOTION FIRST.  The imaginary pool (swarm_emotion.h, E1) comes off
 *       the top of the cycle total T.
 *   M2  COMMONS FLOOR.  8/21 of the remaining real tokens are split by the
 *       Fibonacci rule (swarm_budget.h). No model can be starved out.
 *   M3  OPEN MARKET.  The other 13/21 are sold. Each model bids Financial
 *       capital it holds and receives market tokens in proportion to its bid
 *       (a proportional-share market: price = total bids / market tokens).
 *   M4  MARKET CAP.  No model may win more than 8/21 of the market tokens.
 *       Tokens over the cap go to the other bidders, in proportion to their
 *       bids. Tokens no bidder may take fall back into the commons floor.
 *   M5  PAY.  A model that wins tokens pays its bid into the cycle's pot.
 *
 * SETTLEMENT (after the cycle)
 * ----------------------------
 *   M6  EARN.  13/21 of the pot is paid as income in proportion to the value
 *       each model produced this cycle (credited in the Intellectual,
 *       Manufactured, Human or System forms). 8/21 is a cooperation dividend
 *       in proportion to the Social capital each model earned this cycle.
 *       If nobody earned one kind, its part goes to the other; if nobody
 *       earned either, the pot is refunded in proportion to what was paid.
 *   M7  WEALTH CAP.  No model may hold more than max(8/21 of all money,
 *       an equal share). The excess is shared equally among the models
 *       under the cap, never pushing one over it.
 *   M8  CONSERVATION.  Money is never created or destroyed here: the sum of
 *       all Financial capital equals money_supply after every call.
 *   M9  CROWN CAPITAL IS INALIENABLE.  Social, Natural, Cultural and
 *       Spiritual capital can be earned but there is no call that moves
 *       them between models, and they can never be bid.
 *
 * Every ratio is a ratio of Fibonacci numbers (8, 13, 21). All arithmetic is
 * exact integer arithmetic. Freestanding: no libc, no allocation, no float.
 */
#ifndef SWARM_MARKET_H
#define SWARM_MARKET_H

#include <stdint.h>
#include <stdbool.h>
#include "../zcapital/zcap_forms.h"
#include "swarm_budget.h"
#include "swarm_emotion.h"

#define SWARM_MKT_DEN        21u /* F(8) */
#define SWARM_MKT_FLOOR_NUM  8u  /* F(6): commons floor, market cap, wealth cap */
#define SWARM_MKT_INCOME_NUM 13u /* F(7): value income share of the pot */

/* The nine forms of capital: aliases of the canonical zcap_form_t
 * (kernel/src/zcapital/zcap_forms.h), checked form by form below. */
typedef enum {
    SWARM_CAP_FINANCIAL = ZCAP_FINANCIAL,
    SWARM_CAP_MANUFACTURED = ZCAP_MANUFACTURED,
    SWARM_CAP_INTELLECTUAL = ZCAP_INTELLECTUAL,
    SWARM_CAP_HUMAN = ZCAP_HUMAN,
    SWARM_CAP_SOCIAL = ZCAP_SOCIAL,       /* Crown */
    SWARM_CAP_NATURAL = ZCAP_NATURAL,     /* Crown */
    SWARM_CAP_CULTURAL = ZCAP_CULTURAL,   /* Crown */
    SWARM_CAP_SPIRITUAL = ZCAP_SPIRITUAL, /* Crown */
    SWARM_CAP_SYSTEM = ZCAP_SYSTEM,
    SWARM_CAP_COUNT = ZCAP_FORM_COUNT
} swarm_cap_t;
_Static_assert((int) SWARM_CAP_FINANCIAL == (int) ZCAP_FINANCIAL,
               "swarm FINANCIAL == ZCAP_FINANCIAL");
_Static_assert((int) SWARM_CAP_MANUFACTURED == (int) ZCAP_MANUFACTURED,
               "swarm MANUFACTURED == ZCAP_MANUFACTURED");
_Static_assert((int) SWARM_CAP_INTELLECTUAL == (int) ZCAP_INTELLECTUAL,
               "swarm INTELLECTUAL == ZCAP_INTELLECTUAL");
_Static_assert((int) SWARM_CAP_HUMAN == (int) ZCAP_HUMAN, "swarm HUMAN == ZCAP_HUMAN");
_Static_assert((int) SWARM_CAP_SOCIAL == (int) ZCAP_SOCIAL, "swarm SOCIAL == ZCAP_SOCIAL");
_Static_assert((int) SWARM_CAP_NATURAL == (int) ZCAP_NATURAL, "swarm NATURAL == ZCAP_NATURAL");
_Static_assert((int) SWARM_CAP_CULTURAL == (int) ZCAP_CULTURAL, "swarm CULTURAL == ZCAP_CULTURAL");
_Static_assert((int) SWARM_CAP_SPIRITUAL == (int) ZCAP_SPIRITUAL,
               "swarm SPIRITUAL == ZCAP_SPIRITUAL");
_Static_assert((int) SWARM_CAP_SYSTEM == (int) ZCAP_SYSTEM, "swarm SYSTEM == ZCAP_SYSTEM");
_Static_assert((int) SWARM_CAP_COUNT == ZCAP_FORM_COUNT, "swarm count == ZCAP_FORM_COUNT");

/* True for the four inalienable Crown forms. */
bool swarm_cap_is_crown(swarm_cap_t f);

typedef struct {
    uint32_t model_id;
    uint64_t cap[SWARM_CAP_COUNT]; /* holdings across the nine forms */
    uint64_t bid;                  /* Financial capital bid this cycle */
    uint64_t won;                  /* market tokens won this cycle */
    uint64_t paid;                 /* paid into the pot this cycle */
    uint64_t value_cycle;          /* value credited this cycle (M6) */
    uint64_t social_cycle;         /* Social capital earned this cycle (M6) */
} swarm_trader_t;

typedef struct {
    swarm_trader_t t[SWARM_MAX_MODELS];
    uint32_t n;
    uint64_t money_supply; /* M8 */
    uint64_t pot;
    uint64_t last_floor;  /* real tokens shared by Fibonacci last cycle */
    uint64_t last_market; /* real tokens offered on the market */
    uint64_t last_unsold; /* market tokens that fell back to the floor */
} swarm_market_t;

void swarm_market_init(swarm_market_t *m);

/* Join with an endowment of Financial capital (adds to money_supply). */
swarm_status_t swarm_market_join(swarm_market_t *m, uint32_t model_id, uint64_t endowment);

/* Bid for this cycle. The bid may not exceed Financial holdings. */
swarm_status_t swarm_market_bid(swarm_market_t *m, uint32_t model_id, uint64_t amount);

/* M4 helper: proportional split of `total` by w[] where no item may exceed
 * `cap`. Returns the tokens nobody may take. */
uint64_t swarm_capped_split(uint64_t total, const uint64_t *w, uint32_t n, uint64_t cap,
                            uint64_t *out);

/* Open a cycle on every axis (M1-M5). `emo` may be NULL for no emotion. */
swarm_status_t swarm_market_begin_cycle(swarm_budget_t *b, swarm_market_t *m,
                                        swarm_emotion_state_t *emo);

/* Credit earned capital. Financial capital cannot be credited (it is only
 * earned through settlement); value forms count toward M6 income and Social
 * toward the M6 dividend. */
swarm_status_t swarm_market_credit(swarm_market_t *m, uint32_t model_id, swarm_cap_t form,
                                   uint64_t amount);

/* Before the cycle ends: credit each model Natural capital for the tokens it
 * did not spend (frugality). */
void swarm_market_credit_frugality(swarm_market_t *m, const swarm_budget_t *b);

/* After the cycle: pay out the pot (M6), apply the wealth cap (M7). */
swarm_status_t swarm_market_settle(swarm_market_t *m);

/* M8: true iff the sum of Financial holdings plus the pot equals money_supply. */
bool swarm_market_conserved(const swarm_market_t *m);

/* The trader for a model, or NULL. */
const swarm_trader_t *swarm_market_trader(const swarm_market_t *m, uint32_t model_id);

#endif /* SWARM_MARKET_H */
