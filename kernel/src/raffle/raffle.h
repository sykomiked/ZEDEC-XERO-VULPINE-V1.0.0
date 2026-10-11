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
 *   L1  Paying for a chance to win a prize is a lottery under most laws (in
 *       the US: prize + chance + consideration) and is illegal without a
 *       licence. raffle_config_default() leaves `jurisdiction_permits` false,
 *       and while it is false every entry call fails closed with
 *       RAFFLE_ERR_DISABLED. Only a jurisdiction plug-in, after legal review,
 *       should set it.
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
 *   F2  fee(a) = floor(a * fee_num / fee_den), default 987/61000 (about
 *       1.618%). Full 64x64 -> 128-bit product, 128/64 division: exact for
 *       every a < 2^64.
 *   F3  tickets = floor(fee / ticket_price), numbered sequentially from the
 *       epoch's next ticket id. The WHOLE fee goes to the pool, so the part
 *       below one ticket price stays in the pool (no dust leaves).
 *   F4  With `refuse_dust` (default true) an entry whose fee buys zero
 *       tickets is refused and nothing is charged, so nobody pays for no
 *       chance. With it false the fee is taken into the pool anyway.
 *   F5  The ticket price is a fixed integer in the deployment's base unit
 *       (Zenny chips once they exist). It cannot change inside an epoch.
 *
 * THE EPOCH (Fibonacci trigger)
 * -----------------------------
 *   E1  threshold = F(fib_index) tickets (F(1) = F(2) = 1; F(18) = 2584,
 *       F(20) = 6765). fib_index is limited to [RAFFLE_FIB_MIN,
 *       RAFFLE_FIB_MAX].
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
 *       crown_pot = pool - reserve_skim, to the winning ticket's owner.
 *       The two always sum to the pool.
 *   S2  raffle_draw() only computes the settlement. The caller posts it
 *       (vault deposit + winner credit) as one balanced ledger posting, then
 *       calls raffle_settle(); nothing moves here.
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
#define RAFFLE_FIB_MAX     40u /* F(40) = 102,334,155 tickets */
#define RAFFLE_DRAW_TRIES  128u

#define RAFFLE_DEFAULT_FEE_NUM   987u
#define RAFFLE_DEFAULT_FEE_DEN   61000u
#define RAFFLE_DEFAULT_FIB_INDEX 18u /* 2584 tickets */

typedef enum {
    RAFFLE_OK = 0,
    RAFFLE_ERR_ARG = -1,
    RAFFLE_ERR_DISABLED = -2,    /* L1: the jurisdiction switch is off      */
    RAFFLE_ERR_CONFIG = -3,      /* a ratio, price or index is out of range */
    RAFFLE_ERR_PHASE = -4,       /* wrong phase for this call               */
    RAFFLE_ERR_DUST = -5,        /* F4: the fee buys no ticket              */
    RAFFLE_ERR_FULL = -6,        /* the range table is full                 */
    RAFFLE_ERR_OVERFLOW = -7,    /* a counter or the pool would wrap        */
    RAFFLE_ERR_PARTY = -8,       /* unknown or duplicate party              */
    RAFFLE_ERR_COMMIT = -9,      /* reveal does not open the commitment     */
    RAFFLE_ERR_REVEALS = -10,    /* not enough reveals to draw              */
    RAFFLE_ERR_NO_TICKETS = -11, /* nothing to draw from                    */
    RAFFLE_ERR_DRAW = -12,       /* rejection sampling ran out (2^-128)     */
    RAFFLE_ERR_SETTLEMENT = -13  /* settlement record does not match        */
} raffle_status_t;

typedef struct {
    bool jurisdiction_permits;   /* L1: default false                       */
    bool free_entry;             /* L2: default false                       */
    bool refuse_dust;            /* F4: default true                        */
    uint64_t fee_num, fee_den;   /* F2: default 987/61000                   */
    uint64_t skim_num, skim_den; /* S1: default 987/61000                 */
    uint64_t ticket_price;       /* F5: base units per ticket, default 1    */
    uint32_t fib_index;          /* E1: default 18 (2584 tickets)           */
    uint32_t min_reveals;        /* R3: 0 = every committed party (default) */
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
    uint64_t reserve_skim; /* S1: to the solvency vault  */
    uint64_t crown_pot;    /* S1: to the winner          */
    uint8_t header_hash[RAFFLE_HASH_LEN];
    uint8_t seed[RAFFLE_HASH_LEN];
    uint8_t link[RAFFLE_HASH_LEN]; /* hash of this record; seeds the next epoch's chain */
} raffle_settlement_t;

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

/* Checks every ratio (num < den, den > 0), the price (> 0), fib_index and
 * min_reveals <= RAFFLE_MAX_PARTIES. */
raffle_status_t raffle_config_check(const raffle_config_t *c);

/* F(n) for 1 <= n <= 93 (fits 64 bits); 0 outside that range. */
uint64_t raffle_fib(uint32_t n);

/* F2/F3 without state: fee and ticket count for an amount. */
raffle_status_t raffle_quote(const raffle_config_t *c, uint64_t amount, uint64_t *fee,
                             uint64_t *tickets);

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

/* F1-F4, E2: a paid, opt-in entry of `amount`. On error nothing changes. */
raffle_status_t raffle_enter(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN], uint64_t amount,
                             raffle_receipt_t *out);

/* L2: one free ticket (no fee). Fails with RAFFLE_ERR_DISABLED unless both
 * jurisdiction_permits and free_entry are set. */
raffle_status_t raffle_free_entry(raffle_t *r, const uint8_t owner[RAFFLE_ID_LEN],
                                  raffle_receipt_t *out);

/* R2: reveal a party's secret. Only while CLOSED. */
raffle_status_t raffle_reveal(raffle_t *r, const uint8_t party[RAFFLE_ID_LEN],
                              const uint8_t secret[RAFFLE_HASH_LEN]);

/* R2-R4, S1: compute the settlement. Pure with respect to `r`. */
raffle_status_t raffle_draw(const raffle_t *r, raffle_settlement_t *out);

/* S2: after the caller has posted `s` to the ledger, mark the epoch settled.
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
