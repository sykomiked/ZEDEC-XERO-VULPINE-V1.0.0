/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot_token.h — the Sicilian Crown Allotment as a block on a token's signed
 * manifest (Zenny chips, barrel tokens, V-Note states), plus endowment-linked
 * savings. Owner request 2026-10-11.
 *
 * THE BLOCK
 * ---------
 *   T1  A fixed 80-byte canonical encoding (big-endian), so the issuer's
 *       manifest signature covers exactly one byte string per meaning:
 *           0  "ZXRT"            4  version (1)      5  events
 *           6  target            7  0
 *           8  slice_min.num    16  slice_min.den
 *          24  slice_max.num    32  slice_max.den
 *          40  savings_max.num  48  savings_max.den
 *          56  local_fib_index (u32)
 *          60  tier_lo (u8)     61  tier_hi (u8)     62  0 (u16)
 *          64  nav.num          72  nav.den
 *       allot_token_decode() accepts only that form: right length, magic,
 *       version, zero reserved bytes, zero fields for features that are off,
 *       and every value in range (ALLOT_ERR_MANIFEST or ALLOT_ERR_EVENT).
 *       allot_token_digest() is what the manifest embeds and signs; signing
 *       and the rest of the manifest are the token layer's job.
 *   T2  EVENTS. MINT, BURN and TRANSFER each turn on an opt-in allotment slice
 *       on that event; SAVINGS turns on endowment-linked savings (T6). Any other
 *       bit is refused. There is deliberately no borrow, lien or collateral
 *       event (T5).
 *   T3  SLICES. The issuer's range [slice_min, slice_max] (each in (0, 1/2])
 *       is intersected with the deployment's [min_rate, max_rate]
 *       (allot.h F2). An empty intersection refuses every entry
 *       (ALLOT_ERR_RATE). Each holder picks their own rate inside it.
 *   T4  POTS. target NETWORK sends the ballots into the deployment's
 *       ladder (allot_ladder.h, the 144 pots). target LOCAL sends them to
 *       the issuer's own pot, a plain allot_t kept apart from the network
 *       pots. A local pot runs on allot_token_config(): the deployment's
 *       jurisdiction switch, skim, grant slice, share cap and reveal rule,
 *       with only the rate range and (optionally) local_fib_index taken from
 *       the block. So the issuer cannot lower the skim, and the local pot's
 *       reserve skim posts, through allot_post, to the same main
 *       reserve-floor account as every other pot. allot_token_pot_check()
 *       refuses a local pot whose config differs from that.
 *   T7  BASKET NAV LEG (LOCAL only, default off). nav in (0, 1) sets the
 *       pot's NAV slice (allot.h S5): that fraction of each settled pot is
 *       credited to ALLOT_DEST_BASKET_NAV for the basket layer to buy more
 *       of the basket's BACKED components. It comes out of the endowment side;
 *       skim + grant + nav must stay below 1, and the skim and grant slice
 *       are untouched. Network pots never carry it (they are shared).
 *   T8  TIER MAP (NETWORK only). tier_lo..tier_hi (1-based, 1 <= lo <= hi
 *       <= 144; 0/0 = every level) confines the token's ballots to those
 *       ladder levels, e.g. 1-12 or 89-144 (allot_ladder.h P5: nothing
 *       spills outside the span; if no level in it can take the fee, the
 *       entry is refused). allot_token_span() maps it onto a ladder.
 *
 * WHAT THE BLOCK CANNOT DO
 * ------------------------
 *   T5  It cannot switch the allotment on. With the deployment's
 *       jurisdiction_permits false (the default) every call here fails
 *       closed with ALLOT_ERR_DISABLED, whatever the block says. It cannot
 *       opt anyone in: each holder starts out (allot_guard.h G1) and all
 *       the player protections (G1-G6) apply to every slice and every
 *       savings tick. It cannot make ballots, pot shares or a savings
 *       enrolment into collateral: borrowing against them stays off.
 *
 * PRIZE-LINKED SAVINGS (principal never at risk)
 * ----------------------------------------------
 *   T6  A holder enrols with a share s in (0, savings_max] (savings_max <=
 *       1) of their REALISED profit, e.g. a V-Note's realised gain for the
 *       tick. Each tick the caller passes only that realised profit y; the
 *       API takes no principal and no balance, so it cannot spend either.
 *       fee = floor(y * s), checked against the holder's limits (G3) and
 *       the other gates. G2 confirmation is given once, at enrolment, for
 *       the exact share (a share above confirm_above needs it). A tick with
 *       no realised profit buys nothing (ALLOT_ERR_DUST). Which number is
 *       "realised profit" is the finance layer's call (the V-Note swap,
 *       barrel grants and vault hooks live in the finance build), not this
 *       module's.
 *
 * HONEST LIMITS. Endowment-linked savings is the most defensible shape of this
 * (it is lawful in some places, usually only through regulated banks or
 * credit unions), but it is still regulated. A slice on every transfer of a
 * third-party token turns every holder of that token into a potential allotment
 * player and widens the regulated surface a great deal; allot.h L1 applies
 * in full.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_ALLOT_TOKEN_H
#define ZXV_ALLOT_TOKEN_H

#include "allot.h"
#include "allot_guard.h"
#include "allot_ladder.h"

#define ALLOT_TOKEN_BLOCK_LEN 80u
#define ALLOT_TOKEN_VERSION   1u

/* T2 */
#define ALLOT_TOKEN_EV_MINT     0x01u
#define ALLOT_TOKEN_EV_BURN     0x02u
#define ALLOT_TOKEN_EV_TRANSFER 0x04u
#define ALLOT_TOKEN_EV_SAVINGS  0x08u
#define ALLOT_TOKEN_EV_SLICES   0x07u
#define ALLOT_TOKEN_EV_KNOWN    0x0Fu

/* T4 */
#define ALLOT_TOKEN_TARGET_NETWORK 0u
#define ALLOT_TOKEN_TARGET_LOCAL   1u

typedef struct {
    uint8_t events;            /* T2: 0 = off (default)                     */
    uint8_t target;            /* T4                                        */
    allot_rate_t slice_min;   /* T3: 0/0 when no slice event is on         */
    allot_rate_t slice_max;   /* T3                                        */
    allot_rate_t savings_max; /* T6: 0/0 unless SAVINGS                    */
    uint32_t local_fib_index;  /* T4: LOCAL only; 0 = the deployment's      */
    uint8_t tier_lo, tier_hi;  /* T8: NETWORK only; 0/0 = every level       */
    allot_rate_t nav;         /* T7: LOCAL only; 0/0 = off                 */
} allot_token_block_t;

/* Off: no events, all fields zero. */
void allot_token_block_default(allot_token_block_t *b);

/* T1-T3, T6: every field in range and zero where unused. */
allot_status_t allot_token_block_check(const allot_token_block_t *b);

/* T1 */
allot_status_t allot_token_encode(const allot_token_block_t *b,
                                    uint8_t out[ALLOT_TOKEN_BLOCK_LEN]);
allot_status_t allot_token_decode(const uint8_t *in, size_t len, allot_token_block_t *b);

/* T1: SHA3-256("ZXV-ALLOT-TOKEN1" || encoding), for the signed manifest. */
allot_status_t allot_token_digest(const allot_token_block_t *b, uint8_t out[ALLOT_HASH_LEN]);

/* T3-T5, T7: the config a slice runs under (and a LOCAL pot is opened
 * with): the deployment's, with the rate range intersected (when a slice
 * event is on) and, for LOCAL, the block's fib_index and NAV slice
 * (ALLOT_ERR_CONFIG if skim + grant + nav would reach 1). ALLOT_ERR_DISABLED while the deployment
 * is off. */
allot_status_t allot_token_config(const allot_config_t *dep, const allot_token_block_t *b,
                                    allot_config_t *out);

/* T8: the 0-based ladder span [lo, hi] for a NETWORK token on a ladder of
 * n levels. ALLOT_ERR_CONFIG if the map names a level the ladder lacks. */
allot_status_t allot_token_span(const allot_token_block_t *b, uint32_t n, uint32_t *lo,
                                  uint32_t *hi);

/* T4: is `r` a valid LOCAL pot for this token (its config is exactly
 * allot_token_config's)? */
allot_status_t allot_token_pot_check(const allot_config_t *dep, const allot_token_block_t *b,
                                       const allot_t *r);

/* G1 for a token slice: the holder's own rate inside the T3 range (num 0
 * opts out). */
allot_status_t allot_token_set_rate(const allot_config_t *dep, const allot_token_block_t *b,
                                      allot_player_t *pl, allot_rate_t rate);

/* T2-T5 + G1-G6 for one slice on `event` (exactly one of MINT, BURN,
 * TRANSFER) of `amount`. Pure. On ALLOT_OK or ALLOT_ERR_CONFIRM, *fee is
 * the fee; the caller then enters it (allot_ladder_enter_span over
 * allot_token_span for NETWORK, allot_enter on the pot for LOCAL, both at
 * pl->rate) and calls
 * allot_guard_commit. */
allot_status_t allot_token_check(const allot_guard_policy_t *pol, const allot_config_t *dep,
                                   const allot_token_block_t *b, uint8_t event,
                                   const allot_player_t *pl, uint64_t amount, uint64_t now,
                                   uint64_t epoch, uint64_t confirmed_fee, uint64_t *fee);

/* T6: enrol (or, num 0, leave) with share s of realised profit. Use a
 * allot_player_t kept for savings only: its `rate` holds s. A share above
 * pol->confirm_above needs confirmed_share equal to it (G2, given once). */
allot_status_t allot_token_set_share(const allot_guard_policy_t *pol, const allot_config_t *dep,
                                       const allot_token_block_t *b, allot_player_t *pl,
                                       allot_rate_t share, allot_rate_t confirmed_share);

/* T6: the fee this tick's realised profit buys. Pure; no principal is ever
 * an input. The caller then enters it (allot_ladder_enter_span for NETWORK,
 * allot_enter_share for LOCAL, with amount = realised_profit and
 * rate = pl->rate) and calls allot_guard_commit. */
allot_status_t allot_token_savings_check(const allot_guard_policy_t *pol,
                                           const allot_config_t *dep,
                                           const allot_token_block_t *b, const allot_player_t *pl,
                                           uint64_t realised_profit, uint64_t now, uint64_t epoch,
                                           uint64_t *fee);

#endif /* ZXV_ALLOT_TOKEN_H */
