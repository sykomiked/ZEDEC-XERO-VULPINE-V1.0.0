/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle_token.h — the Sicilian Crown Raffle as a block on a token's signed
 * manifest (Zenny chips, barrel tokens, V-Note states), plus prize-linked
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
 *       raffle_token_decode() accepts only that form: right length, magic,
 *       version, zero reserved bytes, zero fields for features that are off,
 *       and every value in range (RAFFLE_ERR_MANIFEST or RAFFLE_ERR_EVENT).
 *       raffle_token_digest() is what the manifest embeds and signs; signing
 *       and the rest of the manifest are the token layer's job.
 *   T2  EVENTS. MINT, BURN and TRANSFER each turn on an opt-in raffle slice
 *       on that event; SAVINGS turns on prize-linked savings (T6). Any other
 *       bit is refused. There is deliberately no borrow, lien or collateral
 *       event (T5).
 *   T3  SLICES. The issuer's range [slice_min, slice_max] (each in (0, 1/2])
 *       is intersected with the deployment's [min_rate, max_rate]
 *       (raffle.h F2). An empty intersection refuses every entry
 *       (RAFFLE_ERR_RATE). Each holder picks their own rate inside it.
 *   T4  POTS. target NETWORK sends the tickets into the deployment's
 *       ladder (raffle_ladder.h, the 144 pots). target LOCAL sends them to
 *       the issuer's own pot, a plain raffle_t kept apart from the network
 *       pots. A local pot runs on raffle_token_config(): the deployment's
 *       jurisdiction switch, skim, grant slice, share cap and reveal rule,
 *       with only the rate range and (optionally) local_fib_index taken from
 *       the block. So the issuer cannot lower the skim, and the local pot's
 *       reserve skim posts, through raffle_post, to the same main
 *       reserve-floor account as every other pot. raffle_token_pot_check()
 *       refuses a local pot whose config differs from that.
 *   T7  BASKET NAV LEG (LOCAL only, default off). nav in (0, 1) sets the
 *       pot's NAV slice (raffle.h S5): that fraction of each settled pot is
 *       credited to RAFFLE_DEST_BASKET_NAV for the basket layer to buy more
 *       of the basket's BACKED components. It comes out of the prize side;
 *       skim + grant + nav must stay below 1, and the skim and grant slice
 *       are untouched. Network pots never carry it (they are shared).
 *   T8  TIER MAP (NETWORK only). tier_lo..tier_hi (1-based, 1 <= lo <= hi
 *       <= 144; 0/0 = every level) confines the token's tickets to those
 *       ladder levels, e.g. 1-12 or 89-144 (raffle_ladder.h P5: nothing
 *       spills outside the span; if no level in it can take the fee, the
 *       entry is refused). raffle_token_span() maps it onto a ladder.
 *
 * WHAT THE BLOCK CANNOT DO
 * ------------------------
 *   T5  It cannot switch the raffle on. With the deployment's
 *       jurisdiction_permits false (the default) every call here fails
 *       closed with RAFFLE_ERR_DISABLED, whatever the block says. It cannot
 *       opt anyone in: each holder starts out (raffle_guard.h G1) and all
 *       the player protections (G1-G6) apply to every slice and every
 *       savings tick. It cannot make tickets, pot shares or a savings
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
 *       no realised profit buys nothing (RAFFLE_ERR_DUST). Which number is
 *       "realised profit" is the finance layer's call (the V-Note swap,
 *       barrel grants and vault hooks live in the finance build), not this
 *       module's.
 *
 * HONEST LIMITS. Prize-linked savings is the most defensible shape of this
 * (it is lawful in some places, usually only through regulated banks or
 * credit unions), but it is still regulated. A slice on every transfer of a
 * third-party token turns every holder of that token into a potential raffle
 * player and widens the regulated surface a great deal; raffle.h L1 applies
 * in full.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_RAFFLE_TOKEN_H
#define ZXV_RAFFLE_TOKEN_H

#include "raffle.h"
#include "raffle_guard.h"
#include "raffle_ladder.h"

#define RAFFLE_TOKEN_BLOCK_LEN 80u
#define RAFFLE_TOKEN_VERSION   1u

/* T2 */
#define RAFFLE_TOKEN_EV_MINT     0x01u
#define RAFFLE_TOKEN_EV_BURN     0x02u
#define RAFFLE_TOKEN_EV_TRANSFER 0x04u
#define RAFFLE_TOKEN_EV_SAVINGS  0x08u
#define RAFFLE_TOKEN_EV_SLICES   0x07u
#define RAFFLE_TOKEN_EV_KNOWN    0x0Fu

/* T4 */
#define RAFFLE_TOKEN_TARGET_NETWORK 0u
#define RAFFLE_TOKEN_TARGET_LOCAL   1u

typedef struct {
    uint8_t events;            /* T2: 0 = off (default)                     */
    uint8_t target;            /* T4                                        */
    raffle_rate_t slice_min;   /* T3: 0/0 when no slice event is on         */
    raffle_rate_t slice_max;   /* T3                                        */
    raffle_rate_t savings_max; /* T6: 0/0 unless SAVINGS                    */
    uint32_t local_fib_index;  /* T4: LOCAL only; 0 = the deployment's      */
    uint8_t tier_lo, tier_hi;  /* T8: NETWORK only; 0/0 = every level       */
    raffle_rate_t nav;         /* T7: LOCAL only; 0/0 = off                 */
} raffle_token_block_t;

/* Off: no events, all fields zero. */
void raffle_token_block_default(raffle_token_block_t *b);

/* T1-T3, T6: every field in range and zero where unused. */
raffle_status_t raffle_token_block_check(const raffle_token_block_t *b);

/* T1 */
raffle_status_t raffle_token_encode(const raffle_token_block_t *b,
                                    uint8_t out[RAFFLE_TOKEN_BLOCK_LEN]);
raffle_status_t raffle_token_decode(const uint8_t *in, size_t len, raffle_token_block_t *b);

/* T1: SHA3-256("ZXV-RAFFLE-TOKEN1" || encoding), for the signed manifest. */
raffle_status_t raffle_token_digest(const raffle_token_block_t *b, uint8_t out[RAFFLE_HASH_LEN]);

/* T3-T5, T7: the config a slice runs under (and a LOCAL pot is opened
 * with): the deployment's, with the rate range intersected (when a slice
 * event is on) and, for LOCAL, the block's fib_index and NAV slice
 * (RAFFLE_ERR_CONFIG if skim + grant + nav would reach 1). RAFFLE_ERR_DISABLED while the deployment
 * is off. */
raffle_status_t raffle_token_config(const raffle_config_t *dep, const raffle_token_block_t *b,
                                    raffle_config_t *out);

/* T8: the 0-based ladder span [lo, hi] for a NETWORK token on a ladder of
 * n levels. RAFFLE_ERR_CONFIG if the map names a level the ladder lacks. */
raffle_status_t raffle_token_span(const raffle_token_block_t *b, uint32_t n, uint32_t *lo,
                                  uint32_t *hi);

/* T4: is `r` a valid LOCAL pot for this token (its config is exactly
 * raffle_token_config's)? */
raffle_status_t raffle_token_pot_check(const raffle_config_t *dep, const raffle_token_block_t *b,
                                       const raffle_t *r);

/* G1 for a token slice: the holder's own rate inside the T3 range (num 0
 * opts out). */
raffle_status_t raffle_token_set_rate(const raffle_config_t *dep, const raffle_token_block_t *b,
                                      raffle_player_t *pl, raffle_rate_t rate);

/* T2-T5 + G1-G6 for one slice on `event` (exactly one of MINT, BURN,
 * TRANSFER) of `amount`. Pure. On RAFFLE_OK or RAFFLE_ERR_CONFIRM, *fee is
 * the fee; the caller then enters it (raffle_ladder_enter_span over
 * raffle_token_span for NETWORK, raffle_enter on the pot for LOCAL, both at
 * pl->rate) and calls
 * raffle_guard_commit. */
raffle_status_t raffle_token_check(const raffle_guard_policy_t *pol, const raffle_config_t *dep,
                                   const raffle_token_block_t *b, uint8_t event,
                                   const raffle_player_t *pl, uint64_t amount, uint64_t now,
                                   uint64_t epoch, uint64_t confirmed_fee, uint64_t *fee);

/* T6: enrol (or, num 0, leave) with share s of realised profit. Use a
 * raffle_player_t kept for savings only: its `rate` holds s. A share above
 * pol->confirm_above needs confirmed_share equal to it (G2, given once). */
raffle_status_t raffle_token_set_share(const raffle_guard_policy_t *pol, const raffle_config_t *dep,
                                       const raffle_token_block_t *b, raffle_player_t *pl,
                                       raffle_rate_t share, raffle_rate_t confirmed_share);

/* T6: the fee this tick's realised profit buys. Pure; no principal is ever
 * an input. The caller then enters it (raffle_ladder_enter_span for NETWORK,
 * raffle_enter_share for LOCAL, with amount = realised_profit and
 * rate = pl->rate) and calls raffle_guard_commit. */
raffle_status_t raffle_token_savings_check(const raffle_guard_policy_t *pol,
                                           const raffle_config_t *dep,
                                           const raffle_token_block_t *b, const raffle_player_t *pl,
                                           uint64_t realised_profit, uint64_t now, uint64_t epoch,
                                           uint64_t *fee);

#endif /* ZXV_RAFFLE_TOKEN_H */
