/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quest_coop.h — cooperative goals and seasons.
 *
 *   C1  A GOAL belongs to a group (a co-op raising capacity, a syndicate
 *       finishing a project). It counts verified units of the event kinds in
 *       its mask, from its members only, toward one shared target. Members
 *       compete with nobody: progress is the group's, and each member's
 *       share of the units is recorded so a reward can be split fairly.
 *   C2  A goal may carry a VFV pool (an existing pay account and an
 *       amount). The reward is shared through quest_pay.h in proportion to
 *       verified units, under the commons no-monopoly cap. A goal without a
 *       pool rewards standing only.
 *   C3  Every goal declares its mechanic to the guard (quest_guard.h). The
 *       caller may add a declaration of extra features; anything that
 *       breaks a rule is refused with QST_ERR_GUARD.
 *   C4  SEASONS RENEW GOALS, NEVER STANDING. qst_season_begin archives
 *       settled goals and starts a new season number. Levels, badges and
 *       lifetime contribution are untouched. An unfinished goal carries
 *       over into the new season with its progress: nothing expires, so a
 *       season end is never a deadline. A goal complete but not yet paid
 *       stays owed. A goal archived a full season ago frees its slot (goal
 *       ids are never reused), unless fraud was found after its payout.
 *   C5  Completion posts a notification only to members who asked
 *       (quest.h Q7).
 *
 * HONEST LIMITS. A goal holds at most QST_GOAL_MEMBERS members and the
 * world QST_MAX_GOALS goals. Membership is whatever the group's owner
 * says (qst_goal_join takes no proof of membership); a syndicate's own
 * governance decides who may join. The pool amount is a promise recorded
 * here; quest_pay checks the pool account's balance only at settlement.
 * Freestanding C11: no libc, no allocation, no floating point.
 */
#ifndef ZXV_QUEST_COOP_H
#define ZXV_QUEST_COOP_H

#include "quest.h"

/* C1-C3. `extra` (may be NULL) declares any extra features; the reward
 * kind is set by the pool (MONEY when pool_amount > 0, else STANDING).
 * Returns the new goal id (> 0) or a negative qst_status_t. */
int32_t qst_goal_create(qst_world_t *w, uint32_t group, const char *title, uint32_t ev_mask,
                        uint64_t target, uint32_t pool_acct, uint64_t pool_amount,
                        const qst_mechanic_t *extra);
qst_status_t qst_goal_join(qst_world_t *w, uint32_t goal, uint32_t subject);
qst_goal_t *qst_goal(qst_world_t *w, uint32_t goal);
const qst_goal_t *qst_goal_c(const qst_world_t *w, uint32_t goal);
/* Member index of `subject` in the goal, or -1. */
int32_t qst_goal_member(const qst_goal_t *g, uint32_t subject);

/* C4. Returns the new season number. */
uint32_t qst_season_begin(qst_world_t *w, uint32_t day);

/* Called by quest.c. Credit / un-credit units for an event. */
bool qst__goal_credit(qst_world_t *w, uint32_t goal, uint32_t subject, uint8_t kind,
                      uint64_t units);
void qst__goal_uncredit(qst_world_t *w, uint32_t goal, uint32_t subject, uint64_t units);
/* Defined in quest.c: post to members who asked (quest.h Q7). */
void qst__notify_goal_complete(qst_world_t *w, const qst_goal_t *g);

#endif /* ZXV_QUEST_COOP_H */
