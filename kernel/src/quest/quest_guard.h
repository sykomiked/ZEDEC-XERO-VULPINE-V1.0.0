/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* quest_guard.h — the engagement guardrails, as code.
 *
 * Every mechanic that wants to reward, nudge or rank a person describes
 * itself in a qst_mechanic_t and passes qst_guard_check before it can
 * exist. quest.c, quest_coop.c and quest_pay.c run their own mechanics
 * through it; other modules (games, market, swarm UI) can call it too.
 * A nonzero result is a bitmask of the rules broken, and the quest API
 * refuses to create anything that breaks one.
 *
 * THE RULES (docs/ENGAGEMENT.md section 4 gives the reasons)
 *   G1  NO LOOT BOXES. Paying for a chance at any reward.
 *   G2  NO PAID RANDOMNESS. A random outcome with money on either side
 *       (paid to enter, or money as the prize).
 *   G3  NO STREAK-LOSS PUNISHMENT. Nothing earned is lost by being away.
 *   G4  NO VARIABLE-RATIO MONEY. Money rewards follow a fixed rule: the
 *       same verified contribution earns the same amount, every time.
 *   G5  NO PURCHASE PRESSURE. No countdown timer and no scarcity claim
 *       attached to a purchase.
 *   G6  NO PULL NOTIFICATIONS. The notification bus carries results the
 *       person asked for, never messages meant to bring them back.
 *   G7  NO PAYING FOR STANDING. Standing and badges cannot be bought.
 *   G8  BADGES ARE EARNED. A badge is never a random or variable drop.
 *   G9  A STOPPING POINT. No endless feed or autoplay with no natural end.
 *   G10 MINORS. No ranking of people and no money mechanics aimed at
 *       minors.
 *   G11 FREE TO LEAVE. Opting out is never hidden and never costs what
 *       was already earned.
 *
 * qst_copy_is_calm checks user-facing text for pressure phrases ("hurry",
 * "last chance", "streak", "we miss you" and so on). Quest runs every
 * string it shows or posts through it.
 *
 * HONEST LIMITS. The guard checks what a mechanic DECLARES about itself;
 * it cannot see a mechanic that lies or is never registered. Code review
 * and the tests that pin each quest mechanic are what keep declarations
 * honest. The copy check is a short English phrase list matched without
 * regard to ASCII case: it catches common pressure wording, not every
 * manipulative sentence, and other locales need their own lists.
 * Freestanding C11: no libc, no allocation, no floating point.
 */
#ifndef ZXV_QUEST_GUARD_H
#define ZXV_QUEST_GUARD_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    QST_RW_NONE = 0,
    QST_RW_STANDING = 1, /* mastery level, goal progress     */
    QST_RW_BADGE = 2,    /* a signed achievement certificate */
    QST_RW_ITEM = 3,     /* a cosmetic or in-game item       */
    QST_RW_MONEY = 4     /* VFV or any other money           */
} qst_reward_kind_t;

typedef enum {
    QST_SCHED_FIXED = 0,            /* reward is a fixed function of verified work */
    QST_SCHED_VARIABLE_RATIO = 1,   /* reward after an unpredictable number of acts */
    QST_SCHED_VARIABLE_INTERVAL = 2 /* reward after an unpredictable time          */
} qst_schedule_t;

typedef struct {
    uint8_t reward;         /* qst_reward_kind_t */
    uint8_t schedule;       /* qst_schedule_t    */
    bool paid_entry;        /* costs money to take part or to try again   */
    bool random_outcome;    /* what you get depends on chance              */
    bool loss_on_lapse;     /* progress or standing lost by being away     */
    bool purchase_timer;    /* a countdown attached to a purchase          */
    bool purchase_scarcity; /* "only N left" attached to a purchase        */
    bool unrequested_push;  /* notifications the person did not ask for    */
    bool ranks_people;      /* a public ranking of individuals             */
    bool no_stopping_point; /* endless feed / autoplay, no natural end     */
    bool for_minors;        /* the audience includes minors                */
    bool hides_opt_out;     /* opting out is hard to find                  */
    bool opt_out_forfeits;  /* opting out loses earned standing or money   */
} qst_mechanic_t;

#define QST_V_LOOTBOX        (1u << 0)  /* G1  */
#define QST_V_PAID_RANDOM    (1u << 1)  /* G2  */
#define QST_V_STREAK_LOSS    (1u << 2)  /* G3  */
#define QST_V_VARIABLE_MONEY (1u << 3)  /* G4  */
#define QST_V_PURCHASE_PRESS (1u << 4)  /* G5  */
#define QST_V_PULL_NOTIFY    (1u << 5)  /* G6  */
#define QST_V_PAY_STANDING   (1u << 6)  /* G7  */
#define QST_V_BADGE_UNEARNED (1u << 7)  /* G8  */
#define QST_V_NO_STOP        (1u << 8)  /* G9  */
#define QST_V_MINORS         (1u << 9)  /* G10 */
#define QST_V_OPT_OUT        (1u << 10) /* G11 */
#define QST_V_BAD_ENUM       (1u << 11) /* reward/schedule out of range */
#define QST_V_COUNT          12u

/* A mechanic with every flag clear and the given reward, FIXED schedule. */
void qst_mechanic_plain(qst_mechanic_t *m, qst_reward_kind_t reward);

/* 0 = allowed; otherwise a mask of QST_V_* bits. NULL is QST_V_BAD_ENUM. */
uint32_t qst_guard_check(const qst_mechanic_t *m);

/* Name of the lowest set bit of `v` (e.g. "loot box"); "" for 0. */
const char *qst_guard_violation_name(uint32_t v);

/* True when `text` contains none of the pressure phrases. NULL is calm. */
bool qst_copy_is_calm(const char *text);

#endif /* ZXV_QUEST_GUARD_H */
