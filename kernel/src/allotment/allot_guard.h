/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot_guard.h — the player's rate choice and player-protection controls
 * for the Sicilian Crown Allotment. Every control is ON by default; a
 * deployment may adjust the policy.
 *
 *   G1  OPT-IN. A player starts OUT (rate 0). They pick their own exact rate
 *       inside the deployment's [min_rate, max_rate] (allot.h F2, default
 *       1% to 50%). Lowering the rate or opting out takes effect at once.
 *   G2  CONFIRMATION. When the rate is above confirm_above (default 1/10),
 *       the entry is refused unless the caller passes the EXACT fee the
 *       player confirmed on screen (allot_guard_check returns it to show).
 *   G3  LIMITS. The player sets a daily limit and a per-epoch limit (fee
 *       units). With require_limits (default true) no entry is accepted
 *       until both are set. An entry that would pass either is refused.
 *       Days are now / day_len (default 86,400 s); the epoch id is the
 *       caller's (e.g. the apex level's epoch).
 *   G4  COOLING-OFF. Setting a limit for the first time, or lowering one,
 *       applies at once. Raising one is queued and applies only after
 *       cooling_off seconds (default 86,400); until then the old limit holds.
 *   G5  SELF-EXCLUSION. allot_player_exclude(now, duration) blocks entry
 *       until now + duration (UINT64_MAX = permanently). It can be extended
 *       but never shortened or lifted early through this API.
 *   G6  AGE / KYC GATE. The deployment supplies age_gate(ctx, owner). With
 *       no gate set, every entry is refused (fail closed).
 *   G7  LUCK BY PERSON (allot.h W1-W3). The deployment supplies
 *       identity_of(ctx, owner, identity), mapping an account to its
 *       verified (KYC) person. allot_guard_luck() keys the non-selection count
 *       by that identity, so several accounts of one person share one
 *       count. With no hook, or an owner it cannot map, the weight is 1.
 *
 *   G8  FREE ENTRY (allot.h M0). allot_guard_check_free() needs no rate,
 *       limits or confirmation, since nothing is spent. It still applies
 *       the switch, self-exclusion (G5) and the age gate (G6), then maps
 *       the account to its verified person through identity_of (no hook,
 *       or no person: ALLOT_ERR_IDENTITY, fail closed) and asks the
 *       optional merit gate `eligible` (Model C; NULL = no merit gate,
 *       otherwise a "no" is ALLOT_ERR_INELIGIBLE). It returns the person
 *       id, which is the ballot owner, so one person holds one ballot an
 *       epoch however many accounts they have.
 *
 * USE. allot_guard_check() (pure) -> allot_enter / allot_ladder_enter
 * with the player's rate -> allot_guard_commit() to record the spend.
 *
 * HONEST LIMITS. These are standard harm-reduction controls, not legal
 * compliance. Letting players choose their stake does not take a paid
 * chance at a prize outside gaming law (allot.h L1), and a high rate
 * raises both the regulatory and the consumer-harm stakes.
 */
#ifndef ZXV_ALLOT_GUARD_H
#define ZXV_ALLOT_GUARD_H

#include "allot.h"

typedef bool (*allot_age_gate_fn)(void *ctx, const uint8_t owner[ALLOT_ID_LEN]);
typedef bool (*allot_eligible_fn)(void *ctx, const uint8_t identity[ALLOT_ID_LEN]);
typedef bool (*allot_identity_fn)(void *ctx, const uint8_t owner[ALLOT_ID_LEN],
                                  uint8_t identity[ALLOT_ID_LEN]);

typedef struct {
    allot_rate_t confirm_above; /* G2: default 1/10                */
    uint64_t cooling_off;       /* G4: seconds, default 86,400     */
    uint64_t day_len;           /* G3: seconds, default 86,400     */
    bool require_limits;        /* G3: default true                */
    allot_age_gate_fn age_gate; /* G6: NULL = refuse everyone      */
    void *age_ctx;
    allot_identity_fn identity_of; /* G7: NULL = no luck weight     */
    void *identity_ctx;
    allot_eligible_fn eligible; /* G8: NULL = no merit gate          */
    void *eligible_ctx;
} allot_guard_policy_t;

/* G7: one verified person's run of consecutive non-selections. */
typedef struct {
    uint8_t identity[ALLOT_ID_LEN];
    uint32_t misses;
} allot_luck_t;

typedef struct {
    uint8_t owner[ALLOT_ID_LEN];
    allot_rate_t rate;               /* G1: 0/1 = out (default)          */
    uint64_t day_limit, epoch_limit; /* G3: 0 = not set                  */
    uint64_t pend_day, pend_epoch;   /* G4: queued raises, 0 = none      */
    uint64_t pend_day_at, pend_epoch_at;
    uint64_t excluded_until;     /* G5: blocked while now < this     */
    uint64_t day, spent_day;     /* G3: spend in day `day`           */
    uint64_t epoch, spent_epoch; /* G3: spend in epoch `epoch`       */
} allot_player_t;

void allot_guard_policy_default(allot_guard_policy_t *p);

/* G1: a new player, opted out. */
void allot_player_init(allot_player_t *pl, const uint8_t owner[ALLOT_ID_LEN]);

/* G1: rate num == 0 opts out; otherwise it must lie in cfg's range. */
allot_status_t allot_player_set_rate(const allot_config_t *cfg, allot_player_t *pl,
                                     allot_rate_t rate);

/* G3/G4: both limits must be > 0. */
allot_status_t allot_player_set_limits(const allot_guard_policy_t *pol, allot_player_t *pl,
                                       uint64_t day_limit, uint64_t epoch_limit, uint64_t now);

/* G5 */
void allot_player_exclude(allot_player_t *pl, uint64_t now, uint64_t duration);

/* The limits in force at `now` (queued raises that have matured count). */
uint64_t allot_player_day_limit(const allot_player_t *pl, uint64_t now);
uint64_t allot_player_epoch_limit(const allot_player_t *pl, uint64_t now);

/* G1-G6 for a PAID-mode entry (allot.h M2; ALLOT_ERR_MODE otherwise) of
 * `amount` at the player's rate. Changes nothing.
 * `confirmed_fee` is the amount the player confirmed (ignored at or below
 * confirm_above). On ALLOT_OK or ALLOT_ERR_CONFIRM, *fee is the exact fee
 * to show / that was charged. */
allot_status_t allot_guard_check(const allot_guard_policy_t *pol, const allot_config_t *cfg,
                                 const allot_player_t *pl, uint64_t amount, uint64_t now,
                                 uint64_t epoch, uint64_t confirmed_fee, uint64_t *fee);

/* G1, G3, G5, G6 and the switch for a fee the caller already computed and
 * whose confirmation (G2) it already holds; pl->rate is only the opt-in
 * flag here. Used by prize-linked savings (allot_token.h T6). */
allot_status_t allot_guard_check_fee(const allot_guard_policy_t *pol, const allot_config_t *cfg,
                                     const allot_player_t *pl, uint64_t fee, uint64_t now,
                                     uint64_t epoch);

/* G8: may `pl` register a free ballot (FREE mode, or free_entry on in the
 * other modes)? On ALLOT_OK `ballot_owner` is the verified person id to
 * pass to allot_free_entry. Changes nothing. */
allot_status_t allot_guard_check_free(const allot_guard_policy_t *pol, const allot_config_t *cfg,
                                      const allot_player_t *pl, uint64_t now,
                                      uint8_t ballot_owner[ALLOT_ID_LEN]);

/* G7: the weight for `owner`: allot_luck_mult of their person's record in
 * book[0..n) (no record = 0 misses). {1, 1} without a usable identity. */
allot_rate_t allot_guard_luck(const allot_guard_policy_t *pol, const allot_config_t *cfg,
                              const uint8_t owner[ALLOT_ID_LEN], const allot_luck_t *book,
                              uint32_t n);

/* G7/W1: after an epoch a person took part in, `won` (in any level) resets
 * the count, otherwise it grows by one (saturating). */
void allot_luck_note(allot_luck_t *rec, bool won);

/* Record `fee` as spent after the entry was accepted. */
void allot_guard_commit(const allot_guard_policy_t *pol, allot_player_t *pl, uint64_t now,
                        uint64_t epoch, uint64_t fee);

#endif /* ZXV_ALLOT_GUARD_H */
