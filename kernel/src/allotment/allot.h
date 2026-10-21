/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* allot.h — the Sicilian Crown Allotment: selection of a steward by lot
 * (sortition), a Fibonacci epoch trigger, an exact split, and a
 * commit-reveal selection anyone can replay.
 *
 * NAME. "Crown" here is the name of the endowment. This module is NOT part
 * of the Crown pillar (kernel/src/crown), which by design never touches
 * money; it lives on the money side, next to kernel/src/pay. It was called
 * the Sicilian Crown Raffle until 2026-10-15 (owner decision).
 *
 * WHAT THE RENAME DOES AND DOES NOT DO (LEGAL)
 * --------------------------------------------
 *   L0  Most laws (California Penal Code 319, US federal law, the UK
 *       Gambling Act, EU member-state laws) test the SUBSTANCE: a prize, a
 *       chance and consideration. If all three are present the activity is
 *       regulated gaming whatever it is called. Calling it an "allotment",
 *       a ballot or a steward changes nothing on its own. What can change
 *       the analysis is removing one element; this module removes
 *       consideration (M0, the default). Whether a given deployment is
 *       lawful is still a question for counsel in each market; this code
 *       makes no such claim.
 *   L1  Off by default. allot_config_default() leaves `jurisdiction_permits`
 *       false, and while it is false every entry and endowment call fails
 *       closed with ALLOT_ERR_DISABLED, in every mode. Only a jurisdiction
 *       plug-in, after legal review, should set it. Some places restrict
 *       any private selection by lot, free or not; they keep it off.
 *   L2  ALWAYS-ON FREE ENTRY (owner rule 2026-10-15). Wherever the
 *       allotment runs, a free civic ballot is accepted: allot_free_entry()
 *       needs no payment, stake or voucher, and allot_config_check refuses
 *       a config with `free_entry` false. Each free run of ballots is marked
 *       `gratuitous` in its range record. In FREE mode (M0) it is the only
 *       route. In the
 *       SAVINGS and PAID modes a free entry mints ONE ballot while a paid
 *       or yield entry mints one per fee unit, so a free entrant does not
 *       get the same odds as a paying one. Most sweepstakes rules require
 *       that ("equal dignity"), so those two modes stay ordinary regulated
 *       gaming and stay behind L1.
 *
 * THREE MODES (owner requests 2026-10-11 and 2026-10-15)
 * -----------------------------------------------------
 *   M0  FREE (the default, mode 2): a no-consideration civic allotment.
 *       Nothing is paid in and no yield is given up. A1 allot_free_entry
 *       registers ONE ballot per owner per epoch (the module refuses a
 *       second with ALLOT_ERR_CAP); the owner should be the verified PERSON
 *       (allot_guard_check_free returns that id), so many accounts do not
 *       mean many ballots. A2 The endowment is put in by the operator from
 *       surplus it already holds (the regenerative or grant pool, a
 *       sponsor): allot_endow(), never by entrants. A3 Every paid, ladder,
 *       weighted and yield entry is refused (ALLOT_ERR_MODE): a share of
 *       yield given up to enter is consideration too. A4 The whole pool
 *       goes to the selected steward: no reserve skim, grant slice, NAV
 *       leg or rebate (the money was already surplus; nobody paid in to
 *       rebate). A5 Merit gate (Model C, optional): allot_guard_policy_t
 *       .eligible lets a deployment limit ballots to members in good
 *       standing (credibility, a seat, a duty already held), as jury and
 *       civic sortition do. Keep the gate on status a person already has;
 *       a gate that makes people do new work or spend in order to qualify
 *       can itself be argued to be consideration.
 *   M1  SAVINGS (mode 0, opt-in per deployment). Nothing is paid in. The
 *       amount a player opts in is converted, outside this module, into
 *       their own V-Notes (finance build); only a share of the REALISED
 *       profit those notes earn buys ballots: allot_enter_yield(), fee =
 *       floor(profit * share), share in (0, 1]. allot_enter() and every
 *       other amount-at-a-rate entry are refused (ALLOT_ERR_MODE).
 *       HONEST LIMITS: the principal is kept in units, but V-Notes redeem
 *       at the exact reserve ratio, so their value can still fall; this is
 *       not a guarantee against loss. Forgone yield can itself count as
 *       consideration, and prize-linked savings is lawful mainly through
 *       licensed institutions, so L1 (the switch, default off) still
 *       governs this mode too.
 *   M2  PAID (mode 1, opt-in per deployment). The rate slider below: a
 *       slice of an amount is paid into the pool and is gone. This is
 *       regulated gaming in most places.
 *
 * THE FEE (PAID mode; opt-in, separate from the assurance fee)
 * ------------------------------------------------------------
 *   F1  Entry is a separate, opt-in payment. The 0.08889% assurance fee
 *       (pay_assure.h) is untouched: an allotment fee is charged only on an
 *       amount the player chose to put into the allotment.
 *   F2  RATE SLIDER (owner decision 2026-10-11). Each entry carries the
 *       player's own exact rate r = num/den, which must lie in the
 *       deployment's [min_rate, max_rate] (default [1/100, 1/2], and
 *       max_rate may never exceed ALLOT_RATE_CEIL = 1/2):
 *           fee(a) = floor(a * num / den)
 *       Full 64x64 -> 128-bit product, 128/64 division: exact for every
 *       a < 2^64. A player is OUT (rate 0) until they opt in; allot_guard.h
 *       holds that choice and the player-protection controls.
 *   F3  PROPORTIONAL ODDS (owner decision 2026-10-11): one ballot per unit
 *       of fee. An entry paying fee f owns the next f ballot ids, so its
 *       chance is exactly f / (total fee in the epoch). Example: Alice pays
 *       610, Bob 987, pool 1597: Alice holds [0, 609], Bob [610, 1596], and
 *       the selection picks one integer in [0, 1596]. Ballots == pool, always:
 *       there is no ballot price and no leftover.
 *   F4  An amount whose fee rounds down to zero (below 100 base units at
 *       1%) is refused and nothing is charged.
 *   F5  (Replaced the earlier integer multiplier on 987/61000.)
 *   F6  NO MONOPOLY (the project's cooperative no-monopoly rule). With
 *       share_den > 0, no owner may hold more than
 *           max(floor(threshold * share_num / share_den), 1)
 *       ballots in one epoch; an entry that would pass it is refused
 *       (ALLOT_ERR_CAP) and allot_room() says how many more fit. Default
 *       8/21 (about 38%), the same share cap the commons uses
 *       (pay_assure_policy_t.commons_cap_share). share_den = 0 turns it off.
 *
 * THE EPOCH (Fibonacci trigger)
 * -----------------------------
 *   E1  threshold = F(fib_index) ballots, which under F3 are fee units
 *       (F(1) = F(2) = 1; F(18) = 2584, F(20) = 6765). fib_index is limited
 *       to [ALLOT_FIB_MIN, ALLOT_FIB_MAX]. A deployment picks the index to
 *       suit its base unit: F(18) minor units of a currency is a small pot.
 *   E2  The entry whose ballots make the count reach or pass the threshold
 *       closes the epoch inline. All of that entry's ballots belong to this
 *       epoch (the count may overshoot). After close no entry is accepted
 *       until the epoch is settled and the next one opened.
 *   E3  Every entry is folded into a SHA3-256 hash chain; at close the chain
 *       is the epoch's state root and the epoch header (parameters, counts,
 *       pool, state root, the committed parties) is hashed.
 *
 * THE SELECTION (why not Hash(Header || Sig || StateRoot))
 * ---------------------------------------------------
 *   R1  A seed built only from data the closer controls can be ground: the
 *       closer (or whoever signs the header) tries variants, or withholds,
 *       until the steward suits them. ML-DSA's hedged signing makes this
 *       worse, because the same header can be re-signed with fresh
 *       randomness. So the signature is NOT a seed input; it signs the
 *       result instead.
 *   R2  Commit-reveal. Before the epoch's first ballot, up to
 *       ALLOT_MAX_PARTIES parties (e.g. stewards, independent nodes) each
 *       commit c = SHA3-256("ZXV-ALLOT-COMMIT" || epoch || party || secret).
 *       After close each reveals its secret. The seed is
 *       SHA3-256("ZXV-ALLOT-SEED" || header_hash || flag_i || secret_i ...).
 *       If even one party is honest, nobody can predict or steer the seed.
 *   R3  Withholding. By default (min_reveals = 0, meaning "all") every party
 *       must reveal: a party that withholds stalls the selection, visibly, rather
 *       than choosing between outcomes. A deployment that lowers min_reveals
 *       lets the last revealer pick between "reveal" and "withhold"; pair
 *       that with a forfeitable bond. A published future beacon value can be
 *       fed in as one more party whose secret is the beacon output.
 *   R5  VRF beacon party (kernel/src/vrf, RFC 9381 ECVRF-EDWARDS25519-
 *       SHA512-TAI, shared with the dispute juries). Before the first
 *       ballot, allot_commit_vrf() registers a party by its public key; its
 *       commitment slot holds SHA3-256("ZXV-ALLOT-VRF-PK" || epoch || party
 *       || pk), so the header binds the key. After close, allot_reveal_vrf()
 *       takes a proof pi over alpha = the epoch's header hash, which nobody
 *       knows until the epoch closes; vrf_verify must accept it, and the
 *       party's secret becomes SHA3-256("ZXV-ALLOT-VRF-BETA" || beta). A
 *       VRF output is unique for (pk, alpha), so the key holder cannot
 *       grind it by re-proving; like any party it can only withhold (R3).
 *       allot_reveal() refuses a VRF party and allot_reveal_vrf() refuses a
 *       secret party (ALLOT_ERR_PARTY).
 *   R4  No modulo bias. 64-bit draws x come from SHAKE256(seed, counter);
 *       x is rejected while x >= 2^64 - (2^64 mod T), then
 *       steward = x mod T, for T ballots.
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
 *       Barrel grant pool account (kernel/src/grants, finance build; this
 *       module only names the destination). The credit hook stages the
 *       line on grant_pool_account(g) inside the one posting (S4) and, only
 *       after that posting commits, calls grant_pool_record(g, amount) so
 *       the grant rate counts money that actually landed (it refuses to
 *       count more than the pool holds uncounted). grant_den = 0 turns the
 *       slice off.
 *   S3  crown_endowment = pool - reserve_skim - grant_pool - nav_slice -
 *       rebate_paid (S6), to the
 *       selected ballot's owner. Every floor's remainder lands in the crown
 *       pot, so the parts always sum to the pool exactly.
 *       allot_config_check requires skim + grant + nav + rebate < 1 (exact, by
 *       cross-multiplication in 128 and 192 bits).
 *   S4  allot_select() only computes the settlement. allot_post() hands its
 *       legs to a caller-supplied credit hook (reserve floor, grant pool,
 *       basket NAV, steward), which stages them into ONE balanced ledger posting;
 *       the caller commits that posting and then calls allot_settle().
 *       Nothing moves inside this module.
 *   S6  REBATE (default 1/5). rebate = floor(pool * rebate_num /
 *       rebate_den) goes back to everyone except the selected owner, pro rata
 *       to what each paid in this epoch: a range with fee f gets
 *       floor(rebate * f / (pool - steward's fees)). The floors' dust, and
 *       the whole rebate if nobody else paid, go to the Crown endowment. With the
 *       defaults the pot splits about 69.3% crown, 20% rebate, 1.6% reserve
 *       skim, 9.1% grants. allot_post() credits each rebate
 *       (ALLOT_DEST_REBATE, `who` = the owner).
 *   S5  nav_slice = floor(pool * nav_num / nav_den), default off. Only a
 *       basket token's own pot turns it on (allot_token.h T7): the slice
 *       is cut from the endowment side, never from the reserve skim or the
 *       grant slice, and is credited to ALLOT_DEST_BASKET_NAV, where the
 *       basket layer (finance build) buys more of that basket's BACKED
 *       components, raising its NAV. This module only names the leg. The
 *       credit hook should stage it, in full, to a per-basket NAV holding
 *       account and buy components later from that account in whole top-up
 *       units (kernel/src/zenny zenny_topup). The unit moves with prices and
 *       a top-up can be refused (prices moved, an unbacked leg), and neither
 *       may stall or change a settlement that anyone can replay.
 *
 *   S7  FREE mode (M0, A4): reserve_skim, grant_pool, nav_slice and the
 *       rebate are all 0 and crown_endowment is the whole pool. The ratios
 *       stay in the config (and are checked) so a deployment can switch
 *       modes, but FREE mode does not apply them.
 *
 * LUCK WEIGHT (owner request 2026-10-11)
 * -------------------------------------
 *   W1  A player who keeps entering without selected gets more ballots per
 *       fee unit: m(n) = min(1 + n * F(20) / F(21), luck_cap), with n
 *       consecutive non-selections (F(20)/F(21) = 6765/10946, a Fibonacci
 *       convergent of 1/phi) and luck_cap at most ALLOT_LUCK_CEIL (8),
 *       default 5. Any selection resets n to 0. Entries with a weight mint
 *       floor(fee * m) ballots, so odds are no longer exactly fee-
 *       proportional for those players. The ballot count, not the pool,
 *       closes the epoch (E1) and the share cap (F6) counts ballots, so a
 *       weighted player also reaches the cap sooner.
 *   W2  The weight belongs to a PERSON, not an account: allot_guard.h
 *       looks it up through the deployment's verified-identity hook, and
 *       with no hook every weight is 1 (otherwise splitting play across
 *       many accounts would farm it). The ladder applies it only to its
 *       lowest levels (default 24).
 *   W3  "Eventually selected" holds only as a bounded expected wait in small
 *       pots: with the cap, a player's chance per epoch is at most
 *       luck_cap times their fee share, so in a large pot the wait can
 *       still be very long. Nothing here guarantees selection.
 *
 * Freestanding: no libc, no allocation, no floating point.
 */
#ifndef ZXV_ALLOT_H
#define ZXV_ALLOT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#define ALLOT_ID_LEN       32u
#define ALLOT_HASH_LEN     32u
#define ALLOT_VRF_PK       32u /* R5: VRF_PK_LEN */
#define ALLOT_VRF_PROOF    80u /* R5: VRF_PROOF_LEN */
#define ALLOT_MAX_PARTIES  16u
#define ALLOT_FIB_MIN      2u  /* F(2)  = 1 ballot (tests)    */
#define ALLOT_FIB_MAX      80u /* F(80) ~ 2.3e16 fee units */
#define ALLOT_SELECT_TRIES 128u

#define ALLOT_SKIM_NUM          987u
#define ALLOT_SKIM_DEN          61000u
#define ALLOT_DEFAULT_FIB_INDEX 18u /* 2584 fee units */

#define ALLOT_MODE_SAVINGS 0u /* M1 */
#define ALLOT_MODE_PAID    1u /* M2 */
#define ALLOT_MODE_FREE    2u /* M0, the default */

#define ALLOT_LUCK_CEIL    8u /* W1: hard ceiling on luck_cap */
#define ALLOT_LUCK_DEFAULT 5u
#define ALLOT_LUCK_PHI_NUM 6765u  /* F(20) */
#define ALLOT_LUCK_PHI_DEN 10946u /* F(21) */

typedef enum {
    ALLOT_OK = 0,
    ALLOT_ERR_ARG = -1,
    ALLOT_ERR_DISABLED = -2,    /* L1: the jurisdiction switch is off      */
    ALLOT_ERR_CONFIG = -3,      /* a ratio or index is out of range        */
    ALLOT_ERR_PHASE = -4,       /* wrong phase for this call               */
    ALLOT_ERR_DUST = -5,        /* F4: the fee rounds to zero               */
    ALLOT_ERR_FULL = -6,        /* the range table is full                 */
    ALLOT_ERR_OVERFLOW = -7,    /* a counter or the pool would wrap        */
    ALLOT_ERR_PARTY = -8,       /* unknown or duplicate party              */
    ALLOT_ERR_COMMIT = -9,      /* reveal does not open the commitment     */
    ALLOT_ERR_REVEALS = -10,    /* not enough reveals to draw              */
    ALLOT_ERR_NO_BALLOTS = -11, /* nothing to draw from                    */
    ALLOT_ERR_SELECT = -12,     /* rejection sampling ran out (2^-128)     */
    ALLOT_ERR_SETTLEMENT = -13, /* settlement record does not match        */
    ALLOT_ERR_RATE = -14,       /* F2: rate outside [min_rate, max_rate]   */
    ALLOT_ERR_CAP = -15,        /* F6: the owner would pass the share cap  */
    /* allot_guard.h (player protection) */
    ALLOT_ERR_NOT_IN = -20,      /* G1: the player has not opted in         */
    ALLOT_ERR_EXCLUDED = -21,    /* G5: the player is self-excluded         */
    ALLOT_ERR_AGE = -22,         /* G6: the age / KYC gate said no          */
    ALLOT_ERR_NO_LIMITS = -23,   /* G3: limits must be set first            */
    ALLOT_ERR_DAY_LIMIT = -24,   /* G3: would pass the player's daily limit */
    ALLOT_ERR_EPOCH_LIMIT = -25, /* G3: would pass the per-epoch limit      */
    ALLOT_ERR_CONFIRM = -26,     /* G2: exact-amount confirmation missing   */
    /* allot_token.h */
    ALLOT_ERR_EVENT = -27,    /* T2: event not enabled, or an unknown event bit */
    ALLOT_ERR_MANIFEST = -28, /* T1: the manifest block is malformed            */
    ALLOT_ERR_MODE = -29,     /* M0-M2: not allowed in this deployment's mode   */
    /* allot_guard.h (M0) */
    ALLOT_ERR_IDENTITY = -30,  /* G8: no verified person for this account        */
    ALLOT_ERR_INELIGIBLE = -31 /* G8/A5: the merit gate said no                  */
} allot_status_t;

/* An exact rational rate num/den (den > 0). */
typedef struct {
    uint64_t num, den;
} allot_rate_t;

/* F2: the hard ceiling on any deployment's max_rate. */
#define ALLOT_RATE_CEIL_NUM 1u
#define ALLOT_RATE_CEIL_DEN 2u

/* -1, 0, 1 as a/b <, ==, > c/d (dens > 0), exact in 128 bits. */
int allot_rate_cmp(allot_rate_t a, allot_rate_t b);

typedef struct {
    bool jurisdiction_permits;       /* L1: default false                       */
    bool free_entry;                 /* L2: must be true (always-on free entry) */
    allot_rate_t min_rate;           /* F2: default 1/100                       */
    allot_rate_t max_rate;           /* F2: default 1/2 (the ceiling)           */
    uint64_t skim_num, skim_den;     /* S1: default 987/61000                 */
    uint64_t grant_num, grant_den;   /* S2: default 5/55; den 0 = no grants   */
    uint32_t fib_index;              /* E1: default 18 (2584 ballots)           */
    uint64_t threshold;              /* E1: explicit threshold; 0 = F(fib_index) */
    uint32_t min_reveals;            /* R3: 0 = every committed party (default) */
    uint64_t share_num, share_den;   /* F6: default 8/21; den 0 = no cap      */
    uint64_t nav_num, nav_den;       /* S5: default off (den 0)               */
    uint32_t mode;                   /* M0-M2: default ALLOT_MODE_FREE          */
    uint64_t rebate_num, rebate_den; /* S6: default 1/5; den 0 = off        */
    uint32_t luck_cap;               /* W1: 1..8, default 5; 1 = no weight  */
} allot_config_t;

/* One contiguous run of ballot ids owned by one player. */
typedef struct {
    uint8_t owner[ALLOT_ID_LEN];
    uint64_t first;
    uint64_t count;
    uint64_t fee;    /* S6: what this run paid in */
    bool gratuitous; /* L2: a free civic ballot (fee 0, no stake) */
} allot_range_t;

typedef enum {
    ALLOT_OPEN = 0,   /* accepting commits (before the first ballot) and entries */
    ALLOT_CLOSED = 1, /* threshold reached: accepting reveals, then the selection     */
    ALLOT_SETTLED = 2 /* settlement posted; call allot_next_epoch              */
} allot_phase_t;

typedef struct {
    uint64_t epoch;
    uint64_t ballots;
    uint64_t pool;
    uint64_t selected_ballot;
    uint8_t steward[ALLOT_ID_LEN];
    uint64_t reserve_skim;    /* S1: to the reserve floor   */
    uint64_t grant_pool;      /* S2: to the grant pool      */
    uint64_t nav_slice;       /* S5: to the basket's NAV    */
    uint64_t rebate_pool;     /* S6: the rebate slice       */
    uint64_t rebate_paid;     /* S6: sum of the rebates     */
    uint64_t crown_endowment; /* S3: to the steward          */
    uint8_t header_hash[ALLOT_HASH_LEN];
    uint8_t seed[ALLOT_HASH_LEN];
    uint8_t link[ALLOT_HASH_LEN]; /* hash of this record; seeds the next epoch's chain */
} allot_settlement_t;

/* F6 bookkeeping: ballots held per owner this epoch (optional, see
 * allot_set_tally). An empty slot has held == 0. */
typedef struct {
    uint8_t owner[ALLOT_ID_LEN];
    uint64_t held;
} allot_tally_t;

typedef struct {
    allot_config_t cfg;
    uint64_t epoch;
    uint64_t threshold;
    allot_phase_t phase;
    uint64_t ballots;
    uint64_t pool;
    uint64_t paid_entries, free_entries;
    uint8_t chain[ALLOT_HASH_LEN];
    uint8_t header_hash[ALLOT_HASH_LEN];
    allot_range_t *ranges; /* caller storage */
    uint32_t range_cap, range_len;
    allot_tally_t *tally; /* optional caller storage, power-of-two slots */
    uint32_t tally_cap, tally_len;
    uint32_t n_parties;
    uint8_t party[ALLOT_MAX_PARTIES][ALLOT_ID_LEN];
    uint8_t commit[ALLOT_MAX_PARTIES][ALLOT_HASH_LEN];
    uint8_t secret[ALLOT_MAX_PARTIES][ALLOT_HASH_LEN];
    bool revealed[ALLOT_MAX_PARTIES];
    bool is_vrf[ALLOT_MAX_PARTIES];                  /* R5 */
    uint8_t vrf_pk[ALLOT_MAX_PARTIES][ALLOT_VRF_PK]; /* R5 */
    allot_settlement_t last; /* valid once phase == ALLOT_SETTLED */
} allot_t;

/* What an entry minted (out-parameter of allot_enter / allot_free_entry). */
typedef struct {
    uint64_t fee;          /* charged into the pool                     */
    uint64_t first_ballot; /* first id minted (valid when count > 0)    */
    uint64_t count;        /* ballots minted                            */
    bool closed_epoch;     /* E2: this entry reached the threshold      */
} allot_receipt_t;

/* Defaults listed above: FREE mode (M0) with free entry on, and the
 * jurisdiction switch OFF (L1). */
void allot_config_default(allot_config_t *c);

/* Checks every ratio (num < den, den > 0), fib_index and
 * min_reveals <= ALLOT_MAX_PARTIES. */
allot_status_t allot_config_check(const allot_config_t *c);

/* F(n) for 1 <= n <= 93 (fits 64 bits); 0 outside that range. */
uint64_t allot_fib(uint32_t n);

/* F2/F3 without state: fee (== ballots) for an amount at `rate` (NULL =
 * the deployment's min_rate). */
allot_status_t allot_quote(const allot_config_t *c, uint64_t amount, const allot_rate_t *rate,
                           uint64_t *fee, uint64_t *ballots);

/* F6: how many more ballots `owner` may take this epoch (UINT64_MAX when
 * the cap is off). */
uint64_t allot_room(const allot_t *r, const uint8_t owner[ALLOT_ID_LEN]);

/* F6 speed-up: give the allotment a hash table of `cap` slots (a power of two,
 * >= 4) so allot_room is O(1) instead of a scan of the ranges. Only before
 * the first ballot; kept and cleared by allot_next_epoch. At most 3/4 of
 * the slots are used; an entry by a new owner past that is refused with
 * ALLOT_ERR_FULL. */
allot_status_t allot_set_tally(allot_t *r, allot_tally_t *tally, uint32_t cap);

/* Start epoch `epoch`. `prev_link` is the previous settlement's link (zeros
 * for the first epoch). `ranges` is caller storage for range_cap runs. */
allot_status_t allot_init(allot_t *r, const allot_config_t *c, uint64_t epoch,
                          const uint8_t prev_link[ALLOT_HASH_LEN], allot_range_t *ranges,
                          uint32_t range_cap);

/* R2: register a party's commitment. Only while OPEN with no ballots yet. */
allot_status_t allot_commit(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                            const uint8_t commitment[ALLOT_HASH_LEN]);

/* The commitment a party publishes for `secret` in `epoch`. */
void allot_commitment(uint64_t epoch, const uint8_t party[ALLOT_ID_LEN],
                      const uint8_t secret[ALLOT_HASH_LEN], uint8_t out[ALLOT_HASH_LEN]);

/* F1-F6, E2: a paid, opt-in entry of `amount` at `rate`. On error
 * nothing changes. */
allot_status_t allot_enter(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                           const allot_rate_t *rate, allot_receipt_t *out);

/* Would minting `count` ballots for `fee` to `owner` succeed now? Checks the
 * switch, the phase, the share cap, overflow and the range table; changes
 * nothing. */
allot_status_t allot_mint_check(const allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t fee,
                                uint64_t count);

/* Ladder use (allot_ladder.h): mint `fee` ballots for a share of a payment
 * whose fee was already computed. `amount` and `rate` are recorded in
 * the chain for audit. */
allot_status_t allot_enter_share(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                                 const allot_rate_t *rate, uint64_t fee, allot_receipt_t *out);

/* M1: a savings entry. fee = floor(profit * share), share in (0, 1]; it
 * mints floor(fee * luck) ballots (luck NULL = 1; else 1 <= luck <=
 * luck_cap, see allot_luck_mult). Allowed in both modes. */
allot_status_t allot_enter_yield(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t profit,
                                 allot_rate_t share, const allot_rate_t *luck,
                                 allot_receipt_t *out);

/* Ladder use: mint `ballots` for `fee` (fee <= ballots <= fee * luck_cap). */
allot_status_t allot_enter_weighted(allot_t *r, const uint8_t owner[ALLOT_ID_LEN], uint64_t amount,
                                    const allot_rate_t *rate, uint64_t fee, uint64_t ballots,
                                    allot_receipt_t *out);

/* W1: the weight after `misses` consecutive non-selections, capped at
 * c->luck_cap. Always >= 1. */
allot_rate_t allot_luck_mult(const allot_config_t *c, uint32_t misses);

/* W1: floor(fee * m), or 0 on overflow or a weight outside [1, luck_cap]. */
uint64_t allot_luck_ballots(const allot_config_t *c, uint64_t fee, allot_rate_t m);

/* L2, A1: one free civic ballot (no fee, no stake), always accepted while
 * the allotment is switched on. ALLOT_ERR_DISABLED while
 * jurisdiction_permits is false. In FREE mode an owner
 * that already holds a ballot this epoch is refused (ALLOT_ERR_CAP). */
allot_status_t allot_free_entry(allot_t *r, const uint8_t owner[ALLOT_ID_LEN],
                                allot_receipt_t *out);

/* A2 (FREE mode only): the operator adds `amount` of surplus it already
 * holds to this epoch's endowment. `source` names the account it comes
 * from and is folded into the chain for audit. Only while OPEN; the
 * caller moves the money into its holding account in the same posting.
 * No ballot is minted. ALLOT_ERR_MODE outside FREE mode, ALLOT_ERR_DUST
 * for 0. */
allot_status_t allot_endow(allot_t *r, const uint8_t source[ALLOT_ID_LEN], uint64_t amount);

/* R5: register a VRF beacon party by its public key. Same phase rule as
 * allot_commit (OPEN, no ballots yet). */
allot_status_t allot_commit_vrf(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                                const uint8_t pk[ALLOT_VRF_PK]);

/* R5: reveal a VRF party with its proof over the header hash. Only while
 * CLOSED. ALLOT_ERR_COMMIT when vrf_verify refuses the proof. */
allot_status_t allot_reveal_vrf(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                                const uint8_t pi[ALLOT_VRF_PROOF]);

/* R2: reveal a party's secret. Only while CLOSED. */
allot_status_t allot_reveal(allot_t *r, const uint8_t party[ALLOT_ID_LEN],
                            const uint8_t secret[ALLOT_HASH_LEN]);

/* R2-R4, S1: compute the settlement. Pure with respect to `r`. */
allot_status_t allot_select(const allot_t *r, allot_settlement_t *out);

/* S4: the destinations of a settlement's legs. */
typedef enum {
    ALLOT_DEST_RESERVE_FLOOR = 0, /* PAY_ASSURE_RESERVE_FLOOR account   */
    ALLOT_DEST_GRANT_POOL = 1,    /* Olive Oil Barrel grant pool account */
    ALLOT_DEST_STEWARD = 2,       /* `who` = the selected owner           */
    ALLOT_DEST_BASKET_NAV = 3,    /* S5: the basket token's component buy */
    ALLOT_DEST_REBATE = 4         /* S6: `who` = a non-selected owner      */
} allot_dest_t;

/* Stage one leg; return false to abort. `who` is NULL except for the steward. */
typedef bool (*allot_credit_fn)(void *ctx, allot_dest_t dest, const uint8_t *who, uint64_t amount);

/* S4: call `credit` for the reserve floor, the grant pool, the basket NAV
 * leg (S5), each rebate (S6, one per run of ballots, in ballot order) and the
 * steward, in that order, skipping zero legs. `r` is the closed epoch `s` was
 * drawn from (the rebates need its runs). Returns false on the first refusal
 * or a mismatch (the caller then discards its staged posting). */
bool allot_post(const allot_t *r, const allot_settlement_t *s, allot_credit_fn credit, void *ctx);

/* S4: after the caller has posted `s` to the ledger, mark the epoch settled.
 * Refuses a record that allot_select would not produce now. */
allot_status_t allot_settle(allot_t *r, const allot_settlement_t *s);

/* Open epoch+1 with the same config and storage, chained to the settlement. */
allot_status_t allot_next_epoch(allot_t *r);

/* R4: an unbiased index in [0, t) drawn from `seed`. False if t == 0 or
 * every one of ALLOT_SELECT_TRIES draws was rejected. */
bool allot_pick(const uint8_t seed[ALLOT_HASH_LEN], uint64_t t, uint64_t *out);

/* Who holds ballot `id` (binary search over the ranges). */
bool allot_owner_of(const allot_t *r, uint64_t id, uint8_t owner[ALLOT_ID_LEN]);

#endif /* ZXV_ALLOT_H */
