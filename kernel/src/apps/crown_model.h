/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* crown_model.h — Crown, the Crown Allotment, as a model (zxm.h): mutual
 * endowment and allotment by sortition over zxvd's grant engine
 * (grants.*). Members bring their standing; the pool takes credits each
 * epoch; an allotment draws its recipients in a published order and splits
 * the grant by weight. Every unit is a sandbox unit of the grant engine,
 * never the owner's money. The rules are crown_rules.h; the model keeps a
 * copy for the view and writes nothing of its own.
 *
 *   CM1 THE POOL IS READ, NEVER GUESSED. crown_refresh reads grants.status;
 *       crown_credit moves Vino into the pool with grants.pool_credit and
 *       refuses an answer whose pool is not the old pool plus the credit
 *       (crown_pool_after, CW4). Numbers come as decimal strings of at
 *       most 19 digits; anything else is ZXM_E_PROTOCOL and nothing
 *       changes.
 *   CM2 STANDING. crown_eval asks grants.eval for one member; its reason
 *       mask (what appeals leave) becomes the eight flags of CW1, and the
 *       member is in good standing exactly when crown_standing says so.
 *       An "eligible" that disagrees with the mask is ZXM_E_PROTOCOL.
 *   CM3 THE DRAW. crown_propose takes the members in good standing, in
 *       the order they were added, and draws up to `seats` of them from a
 *       published seed (crown_draw_at, CW3): place k is the member at
 *       crown_draw_at(seed, eligible, k). Nothing is random here; anyone
 *       holding the seed draws the same order.
 *   CM4 THE GRANT. The grant is floor(floor(pool * cap_num / cap_den) /
 *       price) whole barrels (grants.h G3, the cap 1/1 unless the owner set
 *       one), at least 1 and at most CROWN_NINE, split by weight in draw
 *       order (crown_allot, CW2), its cost barrels * price
 *       (vintner_sale_value, Vintner's rule).
 *   CM5 THE OWNER APPROVES. crown_propose only holds the allotment with a
 *       notice; it sends nothing. crown_approve sends grants.distribute
 *       only while the owner has granted ledger.post and not revoked it
 *       (gatehouse_use_ok), and only the held allotment. The engine's
 *       answer must give every recipient what the proposal said and leave
 *       the pool less the cost; when it does not, mismatch is set and the
 *       answer is ZXM_E_PROTOCOL (the engine's numbers are kept, since it
 *       has acted). crown_cancel drops the held allotment.
 *
 * No drawing, no allocation, integers only. */
#ifndef CROWN_MODEL_H
#define CROWN_MODEL_H

#include "crown_rules.h"
#include "gatehouse_rules.h"
#include "vintner_model.h"
#include "zxm.h"

#define CROWN_MEMBERS    64u
#define CROWN_SEATS      32u /* grants.distribute takes 1..32 recipients */
#define CROWN_NOTICE_MAX 255u

typedef struct {
    uint32_t subject;
    uint32_t account; /* its barrel account */
    int64_t weight;   /* 0..CROWN_NINE */
    uint32_t uptime_bps, dropped, routing, failed, fraud;
    int64_t volume, stake; /* Vino minor units (sandbox) */
    bool evaluated;
    uint32_t mask; /* the reasons grants.eval left */
    bool standing; /* CW1 */
} crown_member_t;

typedef struct {
    bool pending;
    uint32_t lot;
    int64_t price, seed, barrels, cost;
    uint32_t seats;
    uint32_t order[CROWN_SEATS]; /* member indexes in draw order */
    int64_t share[CROWN_SEATS];  /* barrels each, in the same order */
} crown_allotment_t;

typedef struct {
    zxm_link_t *link;
    bool set_up;
    int64_t epoch;
    int64_t pool, inflow;            /* Vino minor units */
    int64_t granted, total;          /* barrels this epoch, and ever */
    int64_t cap_num, cap_den;        /* the budget cap (CM4) */
    bool post_granted, post_revoked; /* the owner's word on ledger.post */
    crown_member_t m[CROWN_MEMBERS];
    uint32_t n;
    crown_allotment_t held; /* CM5 */
    bool mismatch;
    char notice[CROWN_NOTICE_MAX + 1];
} crown_t;

void crown_init(crown_t *c, zxm_link_t *link);

/* The sandbox: grants.setup (funnel key id), grants.account_open,
 * grants.lot_open and grants.fund. *account is the account opened. */
int crown_setup(crown_t *c, uint32_t funnel);
int crown_open_account(crown_t *c, uint32_t owner, bool barrel, uint32_t *account);
int crown_open_lot(crown_t *c, uint32_t lot);
int crown_fund(crown_t *c, uint32_t account, int64_t amount);
/* grants.account: the account's balance in minor units. */
int crown_balance(crown_t *c, uint32_t account, int64_t *balance);

/* CM1 */
int crown_refresh(crown_t *c, uint64_t now);
int crown_epoch(crown_t *c, int64_t epoch);
int crown_credit(crown_t *c, uint32_t from, int64_t amount, uint64_t now);

/* Members and their standing (CM2). */
int crown_add_member(crown_t *c, const crown_member_t *m);
int crown_eval(crown_t *c, uint32_t idx);
/* How many members are evaluated and in good standing. */
uint32_t crown_eligible(const crown_t *c);

/* The owner's cap (1 .. den, den 1..CROWN_NINE) and word on ledger.post. */
int crown_set_cap(crown_t *c, int64_t num, int64_t den);
void crown_set_permission(crown_t *c, bool granted, bool revoked);

/* CM3, CM4: draw, split and hold an allotment; nothing is sent. */
int crown_propose(crown_t *c, uint32_t lot, int64_t price, int64_t seed, uint32_t seats);
/* CM5 */
int crown_approve(crown_t *c, uint64_t now);
int crown_cancel(crown_t *c);

#endif
