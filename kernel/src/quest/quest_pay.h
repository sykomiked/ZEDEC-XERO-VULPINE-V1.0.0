/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quest_pay.h — shared co-op rewards, paid only through kernel/src/pay.
 *
 * Quest never mints, never holds money and never draws lots. A completed
 * goal with a VFV pool (quest_coop.h C2) is paid out by this file using the
 * pay module's own pieces, and nothing else:
 *
 *   P1  FIXED, PROPORTIONAL SPLIT. Each member's gross share is the pool
 *       split by verified units with pay_commons_split (pay_tithe.h K2): the
 *       commons no-monopoly rule, so no member takes more than
 *       max(cap of the pool, an equal share). The same contributions always
 *       give the same split. What nobody may take stays in the pool.
 *   P2  THE TITHE. Each share pays tithe = pay_tithe_phi(gross), exactly
 *       floor(gross * phi / 100), to the commons account; the member gets
 *       gross - tithe.
 *   P3  ONE POSTING PER MEMBER through pay_ledger_post: pool DEBIT -gross,
 *       member DEBIT +net, commons DEBIT +tithe, kind TRANSFER, all VFV.
 *       Each posting's idempotency key is SHA3-256("ZXV-QUEST-PAYOUT-v1" ||
 *       goal || member || pool account), and its UETR comes from that key,
 *       so a settlement interrupted halfway can simply be run again: posted
 *       shares come back PAY_DUPLICATE and are not paid twice. A rerun
 *       reuses the first attempt's tick and initiator, which are part of
 *       the request digest.
 *   P4  MINORS. A member whose age policy disallows money is paid into the
 *       policy's custodian account, or, with none, the share is HELD in the
 *       pool and listed in the plan. A member with no account is held too.
 *   P5  OPT-OUT LOSES NOTHING. Opted-out members are paid exactly as if
 *       they had stayed in.
 *   P6  VFV ONLY. The pool and commons accounts must hold the ledger's VFV
 *       asset; the commons account must carry PAY_ACCT_COMMONS.
 *
 * HONEST LIMITS. Settlement is one posting per member, not one atomic
 * posting for the whole goal (pay_ledger allows PAY_MAX_LINES lines), so a
 * failure part way leaves some members paid; rerunning is safe (P3). The
 * pool's balance is checked by pay_ledger when each share posts, not up
 * front. Fraud found after payout sets the goal's fraud_after_settle flag;
 * reversing a payment is the operator's call through pay_ledger_reverse,
 * not something quest does on its own. Whether a VFV share is income is a
 * question for counsel. Freestanding C11: no libc, no allocation, no
 * floating point, no 64-bit division.
 */
#ifndef ZXV_QUEST_PAY_H
#define ZXV_QUEST_PAY_H

#include "quest.h"
#include "quest_coop.h"
#include "pay_ledger.h"
#include "pay_tithe.h"

typedef struct {
    uint32_t member;
    uint32_t to_acct; /* 0 when held */
    uint64_t gross, tithe, net;
    bool held;
} qst_payout_line_t;

typedef struct {
    qst_payout_line_t line[QST_GOAL_MEMBERS];
    uint32_t n;
    uint64_t pool;
    uint64_t unallocated; /* over the cap: stays in the pool */
    uint64_t tithe_total;
    uint64_t held_total; /* P4: stays in the pool, listed per line */
} qst_payout_plan_t;

/* P1, P2, P4, P5. member_acct[i] is the VFV account of the goal's member i
 * (0 = none). The goal must be COMPLETE or SETTLED. */
qst_status_t qst_payout_plan(const qst_world_t *w, uint32_t goal,
                             const uint32_t member_acct[QST_GOAL_MEMBERS], pay_rat_t cap,
                             qst_payout_plan_t *out);

/* P3, P6: post the plan and mark the goal SETTLED. `plan` may be NULL. */
qst_status_t qst_goal_settle(qst_world_t *w, uint32_t goal, pay_ledger_t *L,
                             const uint32_t member_acct[QST_GOAL_MEMBERS], uint32_t commons_acct,
                             pay_rat_t cap, uint64_t tick, uint32_t initiator,
                             qst_payout_plan_t *plan);

#endif /* ZXV_QUEST_PAY_H */
