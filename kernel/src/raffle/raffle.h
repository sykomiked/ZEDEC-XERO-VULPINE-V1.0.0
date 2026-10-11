/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* raffle.h — the Sicilian Crown Raffle: opt-in ticket minting, a Fibonacci
 * epoch trigger, an exact pot split, and a commit-reveal draw anyone can
 * replay.
 *
 * NAME. "Crown" here is the name of the pot. This module is NOT part of the
 * Crown pillar (kernel/src/crown), which by design never touches money; it
 * lives on the money side, next to kernel/src/pay.
 *
 * OFF BY DEFAULT (LEGAL)
 * ----------------------
 *   L1  A raffle where people pay for a chance to win a prize is regulated
 *       gaming under most laws (in the US the test is prize + chance +
 *       consideration). In many places only licensed or charitable
 *       organisers may run one, often under limits. raffle_config_default()
 *       leaves `jurisdiction_permits` false, and while it is false every
 *       entry call fails closed with RAFFLE_ERR_DISABLED. Only a jurisdiction
 *       plug-in, after legal review, should set it.
 *   L2  `free_entry` turns on a no-purchase entry route (one ticket, no fee),
 *       the usual sweepstakes-style mitigation. Whether that makes a given
 *       deployment lawful is for counsel; this code makes no such claim.
 *       De-duplicating free entries (one per person per epoch) is the
 *       caller's job: it has the identity layer, this module does not.
 *
 * THE FEE (opt-in, separate from the assurance fee)
 * -------------------------------------------------
 *   F1  Entry is a separate, opt-in payment. The 0.08889% assurance fee
 *       (pay_assure.h) is untouched: a raffle fee is charged only on an
 *       amount the player chose to put into the raffle.
 *   F2  RATE SLIDER (owner decision 2026-10-11). Each entry carries the
 *       player's own exact rate r = num/den, which must lie in the
 *       deployment's [min_rate, max_rate] (default [1/100, 1/2], and
 *       max_rate may never exceed RAFFLE_RATE_CEIL = 1/2):
 *           fee(a) = floor(a * num / den)
 *       Full 64x64 -> 128-bit product, 128/64 division: exact for every
 *       a < 2^64. A player is OUT (rate 0) until they opt in; raffle_guard.h
 *       holds that choice and the player-protection controls.
 *   F3  PROPORTIONAL ODDS (owner decision 2026-10-11): one ticket per unit
 *       of fee. An entry paying fee f owns the next f ticket ids, so its
 *       chance is exactly f / (total fee in the epoch). Example: Alice pays
 *       610, Bob 987, pool 1597: Alice holds [0, 609], Bob [610, 1596], and
 *       the draw picks one integer in [0, 1596]. Tickets == pool, always:
 *       there is no ticket price and no leftover.
 *   F4  An amount whose fee rounds down to zero (below 100 base units at
 *       1%) is refused and nothing is charged.
 *   F5  (Replaced the earlier integer multiplier on 987/61000.)
 *   F6  NO MONOPOLY (the project's cooperative no-monopoly rule). With
 *       share_den > 0, no owner may hold more than
 *           max(floor(threshold * share_num / share_den), 1)
 *       tickets in one epoch; an entry that would pass it is refused
 *       (RAFFLE_ERR_CAP) and raffle_room() says how many more fit. Default
 *       8/21 (about 38%), the same share cap the commons uses
 *       (pay_assure_policy_t.commons_cap_share). share_den = 0 turns it off.
 *
 * THE EPOCH (Fibonacci trigger)
 * -----------------------------
 *   E1  threshold = F(fib_index) tickets, which under F3 are fee units
 *       (F(1) = F(2) = 1; F(18) = 2584, F(20) = 6765). fib_index is limited
 *       to [RAFFLE_FIB_MIN, RAFFLE_FIB_MAX]. A deployment picks the index to
 *       suit its base unit: F(18) minor units of a currency is a small pot.
 *   E2  The entry whose tickets make the count reach or pass the threshold
 *       closes the epoch inline. All of that entry's tickets belong to this
 *       epoch (the count may overshoot). After close no entry is accepted
 *       until the epoch is settled and the next one opened.
 *   E3  Every entry is folded into a SHA3-256 hash chain; at close the chain
 *       is the epoch's state root and the epoch header (parameters, counts,
 *       pool, state root, the committed parties) is hashed.
 *
 * THE DRAW (why not Hash(Header || Sig || StateRoot))
 * ---------------------------------------------------
 *   R1  A seed built only from data the closer controls can be ground: the
 *       closer (or whoever signs the header) tries variants, or withholds,
 *       until the winner suits them. ML-DSA's hedged signing makes this
 *       worse, because the same header can be re-signed with fresh
 *       randomness. So the signature is NOT a seed input; it signs the
 *       result instead.
 *   R2  Commit-reveal. Before the epoch's first ticket, up to
 *       RAFFLE_MAX_PARTIES parties (e.g. stewards, independent nodes) each
 *       commit c = SHA3-256("ZXV-RAFFLE-COMMIT" || epoch || party || secret).
 *       After close each reveals its secret. The seed is
 *       SHA3-256("ZXV-RAFFLE-SEED" || header_hash || flag_i || secret_i ...).
 *       If even one party is honest, nobody can predict or steer the seed.
 *   R3  Withholding. By default (min_reveals = 0, meaning "all") every party
 *       must reveal: a party that withholds stalls the draw, visibly, rather
 *       than choosing between outcomes. A deployment that lowers min_reveals
 *       lets the last revealer pick between "reveal" and "withhold"; pair
 *       that with a forfeitable bond. A published future beacon value can be
 *       fed in as one more party whose secret is the beacon output.
 *   R4  No modulo bias. 64-bit draws x come from SHAKE256(seed, counter);
 *       x is rejected while x >= 2^64 - (2^64 mod T), then
 *       winner = x mod T, for T tickets.
 *
 * THE SPLIT (exact)
 * -----------------
 *   S1  reserve_skim = floor(pool * skim_num / skim_den), default
 *       987/61000, posted to the reserve-floor account
 *       (PAY_ASSURE_RESERVE_FLOOR), where it backs Vino like any other
 *       reserve money. The solvency vault itself is funded only by its share
 *       of the assurance fee (owner decision), so the skim reaches it only if
 *       the owner later routes it through the vault's sweep.
 *   S2  grant_pool = floor(pool * grant_num / grant_den), default 5/55
 *       (about 9.09%, owner decision 2026-10-11), credited to the Olive Oil
 *       Barrel grant pool account (built in the finance thread; this module
 *       only names the destination). grant_den = 0 turns the slice off.
 *   S3  crown_pot = pool - reserve_skim - grant_pool - nav_slice, to the
 *       winning ticket's owner. Every floor's remainder lands in the crown
 *       pot, so the parts always sum to the pool exactly.
 *       raffle_config_check requires skim + grant + nav < 1 (exact, by
 *       cross-multiplication in 128 and 192 bits).
 *   S4  raffle_draw() only computes the settlement. raffle_post() hands its
 *       legs to a caller-supplied credit hook (reserve floor, grant pool,
 *       basket NAV, winner), which stages them into ONE balanced ledger posting;
 *       the caller commits that posting and then calls raffle_settle().
 *       Nothing moves inside this module.
 *   S5  nav_slice = floor(pool * nav_num / nav_den), default off. Only a
 *       basket token's own pot turns it on (raffle_token.h T7): the slice
 *       is cut from the prize side, never from the reserve skim or the
 *       grant slice, and is credited to RAFFLE_DEST_BASKET_NAV, where the
 *       basket layer (finance build) buys more of that basket's BACKED
 *       components, raising its NAV. This module only names the leg.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_RAFFLE_H
#define ZXV_RAFFLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define RAFFLE_ID_LEN      32u
#define RAFFLE_HASH_LEN    32u
#define RAFFLE_MAX_PARTIES 16u
#define RAFFLE_FIB_MIN     2u  /* F(2)  = 1 ticket (tests)    */
#define RAFFLE_FIB_MAX     80u /* F(80) ~ 2.3e16 fee units */
#define RAFFLE_DRAW_TRIES  128u

#define RAFFLE_SKIM_NUM          987u
#define RAFFLE_SKIM_DEN          61000u
#define RAFFLE_DEFAULT_FIB_INDEX 18u /* 2584 fee units */

typedef enum {
    RAFFLE_OK = 0,
    RAFFLE_ERR_ARG = -1,
    RAFFLE_ERR_DISABLED = -2,    /* L1: the jurisdiction switch is off      */
    RAFFLE_ERR_CONFIG = -3,      /* a ratio or index is out of range        */
    RAFFLE_ERR_PHASE = -4,       /* wrong phase for this call               */
    RAFFLE_ERR_DUST = -5,        /* F4: the fee rounds to zero               */
    RAFFLE_ERR_FULL = -6,        /* the range table is full                 */
    RAFFLE_ERR_OVERFLOW = -7,    /* a counter or the pool would wrap        */
    RAFFLE_ERR_PARTY = -8,       /* unknown or duplicate party              */
    RAFFLE_ERR_COMMIT = -9,      /* reveal does not open the commitment     */
    RAFFLE_ERR_REVEALS = -10,    /* not enough reveals to draw              */
    RAFFLE_ERR_NO_TICKETS = -11, /* nothing to draw from                    */
    RAFFLE_ERR_DRAW = -12,       /* rejection sampling ran out (2^-128)     */
    RAFFLE_ERR_SETTLEMENT = -13, /* settlement record does not match        */
    RAFFLE_ERR_RATE = -14,       /* F2: rate outside [min_rate, max_rate]   */
    RAFFLE_ERR_CAP = -15,        /* F6: the owner would pass the share cap  */
    /* raffle_guard.h (player protection) */
    RAFFLE_ERR_NOT_IN = -20,      /* G1: the player has not opted in         */
    RAFFLE_ERR_EXCLUDED = -21,    /* G5: the player is self-excluded         */
    RAFFLE_ERR_AGE = -22,         /* G6: the age / KYC gate said no          */
    RAFFLE_ERR_NO_LIMITS = -23,   /* G3: limits must be set first            */
    RAFFLE_ERR_DAY_LIMIT = -24,   /* G3: would pass the player's daily limit */
    RAFFLE_ERR_EPOCH_LIMIT = -25, /* G3: would pass the per-epoch limit      */
    RAFFLE_ERR_CONFIRM = -26,     /* G2: exact-amount confirmation missing   */
    /* raffle_token.h */
    RAFFLE_ERR_EVENT = -27,   /* T2: event not enabled, or an unknown event bit */
    RAFFLE_ERR_MANIFEST = -28 /* T1: the manifest block is malformed            */
} raffle_status_t;

/* An exact rational rate num/den (den > 0). */
typedef struct {
    uint64_t num, den;
} raffle_rate_t;

/* F2: the hard ceiling on any deployment's max_rate. */
#define RAFFLE_RATE_CEIL_NUM 1u
#define RAFFLE_RATE_CEIL_DEN 2u

/* -1, 0, 1 as a/b <, ==, > c/d (dens > 0), exact in 128 bits. */
int raffle_rate_cmp(raffle_rate_t a, raffle_rate_t b);

typedef struct {
    bool jurisdiction_permits;     /* L1: default false                       */
    bool free_entry;               /* L2: default false                       */
    raffle_rate_t min_rate;        /* F2: default 1/100                       */
    raffle_rate_t max_rate;        /* F2: default 1/2 (the ceiling)           */
    uint64_t skim_num, skim_den;   /* S1: default 987/61000                 */
    uint64_t grant_num, grant_den; /* S2: default 5/55; den 0 = no grants   */
    uint32_t fib_index;            /* E1: default 18 (2584 tickets)           */
    uint64_t threshold;            /* E1: explicit threshold; 0 = F(fib_index) */
    uint32_t min_reveals;          /* R3: 0 = every committed party (default) */
    uint64_t share_num, share_den; /* F6: default 8/21; den 0 = no cap      */
    uint64_t nav_num, nav_den;     /* S5: default off (den 0)               */
} raffle_config_t;

/* One contiguous run of ticket ids owned by one player. */
typedef struct {
    uint8_t owner[RAFFLE_ID_LEN];
    uint64_t first;
    uint64_t count;
} raffle_range_t;

typedef enum {
    RAFFLE_OPEN = 0,   /* accepting commits (before the first ticket) and entries */
    RAFFLE_CLOSED = 1, /* threshold reached: accepting reveals, then the draw     */
    RAFFLE_SETTLED = 2 /* settlement posted; call raffle_next_epoch              */
} raffle_phase_t;

typedef struct {
    uint64_t epoch;
    uint64_t tickets;
    uint64_t pool;
    uint64_t winning_ticket;
    uint8_t winner[RAFFLE_ID_LEN];
    uint64_t reserve_skim; /* S1: to the reserve floor   */
    uint64_t grant_pool;   /* S2: to the grant pool      */
    uint64_t nav_slice;    /* S5: to the basket's NAV    */
    uint64_t crown_pot;    /* S3: to the winner          */
    uint8_t header_hash[RAFFLE_HASH_LEN];
    uint8_t seed[RAFFLE_HASH_LEN];
    uint8_t link[RAFFLE_HASH_LEN]; /* hash of this record; seeds the next epoch's chain */
} raffle_settlement_t;

/* F6 bookkeeping: tickets held per owner this epoch (optional, see
 * raffle_set_tally). An empty slot has held == 0. */
typedef struct {
    uint8_t owner[RAFFLE_ID_LEN];
    uint64_t held;
} raffle_tally_t;

typedef struct {
    raffle_config_t cfg;
    uint64_t epoch;
    uint64_t threshold;
    raffle_phase_t phase;
    uint64_t tickets;
    uint64_t pool;
    uint64_t paid_entries, free_entries;
    uint8_t chain[RAFFLE_HASH_LEN];
    uint8_t header_hash[RAFFLE_HASH_LEN];
    raffle_range_t *ranges; /* caller storage */
    uint32_t range_cap, range_len;
    raffle_tally_t *tally; /* optional caller storage, power-of-two slots */
    uint32_t tally_cap, tally_len;
    uint32_t n_parties;
    uint8_t party[RAFFLE_MAX_PARTIES][RAFFLE_ID_LEN];
    uint8_t commit[RAFFLE_MAX_PARTIES][RAFFLE_HASH_LEN];
    uint8_t secret[RAFFLE_MAX_PARTIES][RAFFLE_HASH_LEN];
    bool revealed[RAFFLE_MAX_PARTIES];
    raffle_settlement_t last; /* valid once phase == RAFFLE_SETTLED */
} raffle_t;

/* What an entry minted (out-parameter of raffle_enter / raffle_free_entry). */
typedef struct {
    uint64_t fee;          /* charged into the pool                     */
    uint64_t first_ticket; /* first id minted (valid when count > 0)    */
    uint64_t count;        /* tickets minted                            */
    bool closed_epoch;     /* E2: this entry reached the threshold      */
} raffle_receipt_t;

/* Defaults listed above (the raffle is OFF). */
void raffle_config_default(raffle_config_t *c);

/* Checks every ratio (num < den, den > 0), fib_index and
 * min_reveals <= RAFFLE_MAX_PARTIES. */
raffle_status_t raffle_config_check(const raffle_config_t *c);

/* F(n) for 1 <= n <= 93 (fits 64 bits); 0 outside that range. */
uint64_t raffle_fib(uint32_t n);

/* F2/F3 without state: fee (== tickets) for an amount at `rate` (NULL =
 * the deployment's min_rate). */
raffle_status_t raffle_quote(const raffle_config_t *c, uint64_t amount, const raffle_rate_t *rate,
                             uint64_t *fee, uint64_t *tickets);

/* F6: how many more tickets `owner` may take this epoch (UINT64_MAX when
 * the cap is off). */
uint64_t raffle_room(const raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN]);

/* F6 speed-up: give the raffle a hash table of `cap` slots (a power of two,
 * >= 4) so raffle_room is O(1) instead of a scan of the ranges. Only before
 * the first ticket; kept and cleared by raffle_next_epoch. At most 3/4 of
 * the slots are used; an entry by a new owner past that is refused with
 * RAFFLE_ERR_FULL. */
raffle_status_t raffle_set_tally(raffle_t *r, raffle_tally_t *tally, uint32_t cap);

/* Start epoch `epoch`. `prev_link` is the previous settlement's link (zeros
 * for the first epoch). `ranges` is caller storage for range_cap runs. */
raffle_status_t raffle_init(raffle_t *r, const raffle_config_t *c, uint64_t epoch,
                            const uint8_t prev_link[RAFFLE_HASH_LEN], raffle_range_t *ranges,
                            uint32_t range_cap);

/* R2: register a party's commitment. Only while OPEN with no tickets yet. */
raffle_status_t raffle_commit(raffle_t *r, const uint8_t party[RAFFLE_ID_LEN],
                              const uint8_t commitment[RAFFLE_HASH_LEN]);

/* The commitment a party publishes for `secret` in `epoch`. */
void raffle_commitment(uint64_t epoch, const uint8_t party[RAFFLE_ID_LEN],
                       const uint8_t secret[RAFFLE_HASH_LEN], uint8_t out[RAFFLE_HASH_LEN]);

/* F1-F6, E2: a paid, opt-in entry of `amount` at `rate`. On error
 * nothing changes. */
raffle_status_t raffle_enter(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                             const raffle_rate_t *rate, raffle_receipt_t *out);

/* Would minting `count` tickets for `fee` to `owner` succeed now? Checks the
 * switch, the phase, the share cap, overflow and the range table; changes
 * nothing. */
raffle_status_t raffle_mint_check(const raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                  uint64_t fee, uint64_t count);

/* Ladder use (raffle_ladder.h): mint `fee` tickets for a share of a payment
 * whose fee was already computed. `amount` and `rate` are recorded in
 * the chain for audit. */
raffle_status_t raffle_enter_share(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                                   const raffle_rate_t *rate, uint64_t fee, raffle_receipt_t *out);

/* L2: one free ticket (no fee). Fails with RAFFLE_ERR_DISABLED unless both
 * jurisdiction_permits and free_entry are set. */
raffle_status_t raffle_free_entry(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                  raffle_receipt_t *out);

/* R2: reveal a party's secret. Only while CLOSED. */
raffle_status_t raffle_reveal(raffle_t *r, const uint8_t party[RAFFLE_ID_LEN],
                              const uint8_t secret[RAFFLE_HASH_LEN]);

/* R2-R4, S1: compute the settlement. Pure with respect to `r`. */
raffle_status_t raffle_draw(const raffle_t *r, raffle_settlement_t *out);

/* S4: the destinations of a settlement's legs. */
typedef enum {
    RAFFLE_DEST_RESERVE_FLOOR = 0, /* PAY_ASSURE_RESERVE_FLOOR account   */
    RAFFLE_DEST_GRANT_POOL = 1,    /* Olive Oil Barrel grant pool account */
    RAFFLE_DEST_WINNER = 2,        /* `who` = the winning owner           */
    RAFFLE_DEST_BASKET_NAV = 3     /* S5: the basket token's component buy */
} raffle_dest_t;

/* Stage one leg; return false to abort. `who` is NULL except for the winner. */
typedef bool (*raffle_credit_fn)(void *ctx, raffle_dest_t dest, const uint8_t *who,
                                 uint64_t amount);

/* S4: call `credit` for the reserve floor, the grant pool, the basket NAV
 * leg (S5) and the winner, in that order, skipping zero legs. Returns false on the first refusal
 * (the caller then discards its staged posting). */
bool raffle_post(const raffle_settlement_t *s, raffle_credit_fn credit, void *ctx);

/* S4: after the caller has posted `s` to the ledger, mark the epoch settled.
 * Refuses a record that raffle_draw would not produce now. */
raffle_status_t raffle_settle(raffle_t *r, const raffle_settlement_t *s);

/* Open epoch+1 with the same config and storage, chained to the settlement. */
raffle_status_t raffle_next_epoch(raffle_t *r);

/* R4: an unbiased index in [0, t) drawn from `seed`. False if t == 0 or
 * every one of RAFFLE_DRAW_TRIES draws was rejected. */
bool raffle_pick(const uint8_t seed[RAFFLE_HASH_LEN], uint64_t t, uint64_t *out);

/* Who holds ticket `id` (binary search over the ranges). */
bool raffle_owner_of(const raffle_t *r, uint64_t id, uint8_t owner[RAFFLE_ID_LEN]);

#endif /* ZXV_RAFFLE_H */
