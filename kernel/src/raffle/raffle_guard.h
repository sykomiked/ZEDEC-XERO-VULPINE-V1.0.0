/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle_guard.h — the player's rate choice and player-protection controls
 * for the Sicilian Crown Raffle. Every control is ON by default; a
 * deployment may adjust the policy.
 *
 *   G1  OPT-IN. A player starts OUT (rate 0). They pick their own exact rate
 *       inside the deployment's [min_rate, max_rate] (raffle.h F2, default
 *       1% to 50%). Lowering the rate or opting out takes effect at once.
 *   G2  CONFIRMATION. When the rate is above confirm_above (default 1/10),
 *       the entry is refused unless the caller passes the EXACT fee the
 *       player confirmed on screen (raffle_guard_check returns it to show).
 *   G3  LIMITS. The player sets a daily limit and a per-epoch limit (fee
 *       units). With require_limits (default true) no entry is accepted
 *       until both are set. An entry that would pass either is refused.
 *       Days are now / day_len (default 86,400 s); the epoch id is the
 *       caller's (e.g. the apex level's epoch).
 *   G4  COOLING-OFF. Setting a limit for the first time, or lowering one,
 *       applies at once. Raising one is queued and applies only after
 *       cooling_off seconds (default 86,400); until then the old limit holds.
 *   G5  SELF-EXCLUSION. raffle_player_exclude(now, duration) blocks entry
 *       until now + duration (UINT64_MAX = permanently). It can be extended
 *       but never shortened or lifted early through this API.
 *   G6  AGE / KYC GATE. The deployment supplies age_gate(ctx, owner). With
 *       no gate set, every entry is refused (fail closed).
 *   G7  LUCK BY PERSON (raffle.h W1-W3). The deployment supplies
 *       identity_of(ctx, owner, identity), mapping an account to its
 *       verified (KYC) person. raffle_guard_luck() keys the non-win count
 *       by that identity, so several accounts of one person share one
 *       count. With no hook, or an owner it cannot map, the weight is 1.
 *
 * USE. raffle_guard_check() (pure) -> raffle_enter / raffle_ladder_enter
 * with the player's rate -> raffle_guard_commit() to record the spend.
 *
 * HONEST LIMITS. These are standard harm-reduction controls, not legal
 * compliance. Letting players choose their stake does not take a paid
 * chance at a prize outside gambling law (raffle.h L1), and a high rate
 * raises both the regulatory and the consumer-harm stakes.
 */
#ifndef ZXV_RAFFLE_GUARD_H
#define ZXV_RAFFLE_GUARD_H

#include "raffle.h"

typedef bool (*raffle_age_gate_fn)(void *ctx, const uint8_t owner[RAFFLE_ID_LEN]);
typedef bool (*raffle_identity_fn)(void *ctx, const uint8_t owner[RAFFLE_ID_LEN],
                                   uint8_t identity[RAFFLE_ID_LEN]);

typedef struct {
    raffle_rate_t confirm_above; /* G2: default 1/10                */
    uint64_t cooling_off;        /* G4: seconds, default 86,400     */
    uint64_t day_len;            /* G3: seconds, default 86,400     */
    bool require_limits;         /* G3: default true                */
    raffle_age_gate_fn age_gate; /* G6: NULL = refuse everyone      */
    void *age_ctx;
    raffle_identity_fn identity_of; /* G7: NULL = no luck weight     */
    void *identity_ctx;
} raffle_guard_policy_t;

/* G7: one verified person's run of consecutive non-wins. */
typedef struct {
    uint8_t identity[RAFFLE_ID_LEN];
    uint32_t misses;
} raffle_luck_t;

typedef struct {
    uint8_t owner[RAFFLE_ID_LEN];
    raffle_rate_t rate;              /* G1: 0/1 = out (default)          */
    uint64_t day_limit, epoch_limit; /* G3: 0 = not set                  */
    uint64_t pend_day, pend_epoch;   /* G4: queued raises, 0 = none      */
    uint64_t pend_day_at, pend_epoch_at;
    uint64_t excluded_until;     /* G5: blocked while now < this     */
    uint64_t day, spent_day;     /* G3: spend in day `day`           */
    uint64_t epoch, spent_epoch; /* G3: spend in epoch `epoch`       */
} raffle_player_t;

void raffle_guard_policy_default(raffle_guard_policy_t *p);

/* G1: a new player, opted out. */
void raffle_player_init(raffle_player_t *pl, const uint8_t owner[RAFFLE_ID_LEN]);

/* G1: rate num == 0 opts out; otherwise it must lie in cfg's range. */
raffle_status_t raffle_player_set_rate(const raffle_config_t *cfg, raffle_player_t *pl,
                                       raffle_rate_t rate);

/* G3/G4: both limits must be > 0. */
raffle_status_t raffle_player_set_limits(const raffle_guard_policy_t *pol, raffle_player_t *pl,
                                         uint64_t day_limit, uint64_t epoch_limit, uint64_t now);

/* G5 */
void raffle_player_exclude(raffle_player_t *pl, uint64_t now, uint64_t duration);

/* The limits in force at `now` (queued raises that have matured count). */
uint64_t raffle_player_day_limit(const raffle_player_t *pl, uint64_t now);
uint64_t raffle_player_epoch_limit(const raffle_player_t *pl, uint64_t now);

/* G1-G6 for a PAID-mode entry (raffle.h M2; RAFFLE_ERR_MODE otherwise) of
 * `amount` at the player's rate. Changes nothing.
 * `confirmed_fee` is the amount the player confirmed (ignored at or below
 * confirm_above). On RAFFLE_OK or RAFFLE_ERR_CONFIRM, *fee is the exact fee
 * to show / that was charged. */
raffle_status_t raffle_guard_check(const raffle_guard_policy_t *pol, const raffle_config_t *cfg,
                                   const raffle_player_t *pl, uint64_t amount, uint64_t now,
                                   uint64_t epoch, uint64_t confirmed_fee, uint64_t *fee);

/* G1, G3, G5, G6 and the switch for a fee the caller already computed and
 * whose confirmation (G2) it already holds; pl->rate is only the opt-in
 * flag here. Used by prize-linked savings (raffle_token.h T6). */
raffle_status_t raffle_guard_check_fee(const raffle_guard_policy_t *pol, const raffle_config_t *cfg,
                                       const raffle_player_t *pl, uint64_t fee, uint64_t now,
                                       uint64_t epoch);

/* G7: the weight for `owner`: raffle_luck_mult of their person's record in
 * book[0..n) (no record = 0 misses). {1, 1} without a usable identity. */
raffle_rate_t raffle_guard_luck(const raffle_guard_policy_t *pol, const raffle_config_t *cfg,
                                const uint8_t owner[RAFFLE_ID_LEN], const raffle_luck_t *book,
                                uint32_t n);

/* G7/W1: after an epoch a person took part in, `won` (in any level) resets
 * the count, otherwise it grows by one (saturating). */
void raffle_luck_note(raffle_luck_t *rec, bool won);

/* Record `fee` as spent after the entry was accepted. */
void raffle_guard_commit(const raffle_guard_policy_t *pol, raffle_player_t *pl, uint64_t now,
                         uint64_t epoch, uint64_t fee);

#endif /* ZXV_RAFFLE_GUARD_H */
