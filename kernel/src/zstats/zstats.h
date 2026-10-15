/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* zstats.h — the public statistics feed: signed, hash-chained, per-epoch
 * snapshots of aggregate economy figures, read-only over the other modules.
 *
 * OWNER REQUEST (2026-10-11, quoted): "To maintain zero trust, the
 * microkernel can expose a lightweight, deterministic telemetry layer
 * directly over the mesh:
 *   - The Sovereign Reserve Floor: Live integer reserves backing liquid Vino
 *     vouchers versus outstanding claims.
 *   - The Vault Compression Index: Real-time distribution of circulating
 *     Vino versus locked, yield-bearing V-Notes across compression tiers.
 *   - The Sicilian Crown Epoch Ticker: Total accumulated pot, voluntary fee
 *     multiplier velocity, and time-to-threshold.
 *   - Commodity Entitlement Ledger: Physical barrel reserves committed,
 *     available grant inventory, and the prevailing G_rate."
 * G_rate = Olive Oil Reserve Inflow / Active Verified Enterprise Load, an
 * exact rational per epoch (grants.h G4 computes it). PROJECT WORDING: the
 * raffle is a "raffle"; V-Notes ARE V-Bills; nothing here is called a yield
 * or interest: positions are "locked" (and eligible for patronage
 * disbursement). Coordinator rule: aggregates only, no per-account data.
 *
 *   S1 SNAPSHOT. zstats_snapshot_t holds integer aggregates only: epoch,
 *      tick, the jurisdiction and publisher ids, and eight sections:
 *        RESERVE      reserve Vino (DEBIT of the deployment's reserve
 *                     account), outstanding Vino claims (the total CREDIT of
 *                     the claims asset, capital form FINANCIAL), the floor
 *                     reserve/claims and the required ratio req_num/req_den;
 *                     floor_ok iff reserve * req_den >= claims * req_num
 *                     (exact, 128-bit).
 *        VAULT        V-Bills held by the solvency vault h, V-Bills
 *                     outstanding N (svault_balances) and the share h/N.
 *        COMPRESSION  circulating Vino = total Vino CREDIT minus the vswap
 *                     reserve's DEBIT (Vino that bought V-Bills is not
 *                     circulating); V-Bills outstanding (vswap issuer CREDIT);
 *                     V-Bills locked (every vswap tier position plus, when
 *                     the BONDS section is present, every live V-Bond) and
 *                     unlocked = outstanding - locked; per lock tier (MEDIUM,
 *                     DEEP) the position count and V-Bill sum. Without the
 *                     BONDS section, bonded V-Bills count as unlocked here:
 *                     the present mask says which case a reader has.
 *        BONDS        per configured V-Bond term: its Fibonacci index k, the
 *                     live bond count and V-Bill sum.
 *        AUCTION      the last finished epoch in the vauction history ring
 *                     (epoch, uniform clearing price, quantity), the V-Bill
 *                     issuer's outstanding N (vauc_outstanding: every V-Bill
 *                     ever issued and not returned to the issuer, which is what "cumulative
 *                     issued" can honestly mean once the ring has wrapped),
 *                     the reserve R (vauc_reserve; flagged invalid when it
 *                     cannot be valued), the floor config and floor_ok.
 *        RAFFLE       zstats_raffle_t as the deployment fills it (see S2).
 *        BARRELS      grant_snapshot_t as grants.h G4 gives it: G_rate
 *                     num/den/defined, inflow, weight, barrels granted,
 *                     outstanding, backed and unbacked, pool and reserve.
 *        ZENNY        live chip count and total chips outstanding.
 *      Every rational carries num/den and a defined flag; an undefined one is
 *      0/0 and nothing ever divides by a den. The module never divides at
 *      all, so there is no rounding anywhere.
 *   S2 COLLECT. zstats_collect(out, epoch, tick, sources) fills a snapshot
 *      from const pointers. A NULL source (or a reserve account of
 *      ZSTATS_NONE, or a vault not set up) leaves its section ABSENT: its
 *      bit in `present` is clear and every field of it is zero, so it can
 *      never read as real zeros. Sums are overflow-checked (ZSTATS_ERR_
 *      OVERFLOW); a source that contradicts itself (locked above
 *      outstanding, h above N) is ZSTATS_ERR_STATE. The result must pass
 *      zstats_check. The raffle lives on another branch and is NOT included:
 *      zstats_raffle_t is a plain input the deployment fills. The raffle
 *      does not track time, so it carries no velocity or time-to-threshold:
 *      a reader derives velocity from successive snapshots (pot and tickets
 *      against epoch / tick). Tickets and pot can differ, because a luck
 *      weight mints extra tickets; the threshold is counted in tickets.
 *   S3 PRIVACY. A k-anonymity floor min_cohort (default 5, at least 1)
 *      applies to every count-backed group: each lock tier, each bond term
 *      and the Zenny chip set. A group with fewer than min_cohort members
 *      (zero included) publishes count 0 and sum 0 and sets its bit in
 *      `suppressed`. Totals stay exact, so the suppressed amounts stay
 *      inside them and are never split out. COMPLEMENTARY SUPPRESSION: the
 *      lock family (the two tiers plus the bond terms present) shares one
 *      total, so when exactly one of its groups would be suppressed, the
 *      next smallest group (by count, ties to the lower index; tiers come
 *      before terms) is suppressed too; otherwise total minus the published
 *      groups would give the small group back.
 *   S4 ENCODING. zstats_encode writes ZSTATS_ENC_LEN bytes, little-endian,
 *      no struct padding, in this order: tag "zxv/stats/v1" (12), version
 *      u16 (1), length u32, juris u32, publisher u32, prev_hash (32), epoch
 *      u64, tick u64, present u32, suppressed u32, min_cohort u32; then the
 *      sections in S1 order, each field in the order of its struct below
 *      (bool as one byte 0/1, u32 as 4 bytes, u64 as 8; a rational as num,
 *      den, defined). Absent sections are encoded as zeros. zstats_digest
 *      is SHA-256 of the encoding. zstats_decode is the exact inverse and
 *      refuses a wrong tag, version or length, a short buffer, trailing
 *      bytes and any bool byte other than 0 or 1 (ZSTATS_ERR_FORMAT).
 *   S5 SIGN, VERIFY, CHAIN. The publisher signs zstats_sign_msg(digest) =
 *      "zxv/stats-sig/v1" || digest outside the kernel (ML-DSA in
 *      production). A reader keeps a zstats_chain_t (juris, publisher, head
 *      hash, last epoch). zstats_verify decodes, then checks juris and
 *      publisher (ZSTATS_ERR_ARG), the signature through the caller's verify
 *      hook (ZSTATS_ERR_SIG), prev_hash == head (32 zero bytes before the
 *      first snapshot; ZSTATS_ERR_CHAIN), epoch strictly above the last
 *      accepted one (ZSTATS_ERR_EPOCH) and zstats_check (ZSTATS_ERR_
 *      CONSIST); only then it fills *snap and advances the chain.
 *      zstats_check is the consistency rule: no unknown present or
 *      suppressed bit, min_cohort >= 1, every absent section all zero, each
 *      rational defined iff den != 0 (and 0/0 when not), each rational equal
 *      to the figures it is made of, floor_ok equal to the exact comparison,
 *      h <= N, locked + unlocked == outstanding, published groups summing to
 *      at most the locked total (exactly it when none is suppressed), every
 *      group below min_cohort suppressed, every suppressed group zero, no
 *      lone suppressed group in a lock family of two or more, raffle mode 0
 *      or 1, backed + unbacked == outstanding barrels, lots_backed <= lots.
 *   S6 JSON. zstats_json renders integer-only JSON in a fixed key order with
 *      no whitespace. Every integer is a quoted decimal string (so no reader
 *      rounds a 64-bit figure through a double); a rational is
 *      {"num":"n","den":"d"}, an undefined rational and an absent section
 *      are null, a suppressed group shows "0" and "suppressed":true. No
 *      snprintf: the module has its own u64 formatter. It writes exactly
 *      *len bytes with no terminating NUL; when cap is too small it returns
 *      ZSTATS_ERR_SPACE, never writes past cap, and sets *len to 0.
 *
 * READ-ONLY. This module takes every source through a const pointer, calls
 * only their const read functions and never posts to the ledger.
 *
 * HONEST LIMITS. The figures are only as good as the ledger and the sources
 * the deployment passes in; a section the deployment leaves out is absent,
 * not proven empty. A signature proves who published a snapshot, not that
 * its numbers are true. The k-anonymity floor is a floor, not a guarantee:
 * differencing a series of snapshots (a group crossing the threshold, a
 * total moving between epochs, a single winner of an auction epoch) can
 * still reveal one holder's figures. The raffle, auction and barrel
 * sections publish the aggregates their modules define; a pool funded by
 * raffle proceeds may share the raffle's legal treatment. Designed to
 * support transparent reporting; it claims no audit, certification or
 * regulatory status. Freestanding C11: no allocation, no float, no libc.
 */
#ifndef ZXV_ZSTATS_H
#define ZXV_ZSTATS_H

#include <stdbool.h>
#include <stdint.h>
#include "../pay/pay_ledger.h"
#include "../grants/grants.h"
#include "../svault/svault.h"
#include "../vswap/vswap.h"
#include "../vauction/vauction.h"
#include "../vbond/vbond.h"
#include "../zenny/zenny.h"

#define ZSTATS_NONE           0xFFFFFFFFu
#define ZSTATS_VERSION        1u
#define ZSTATS_TAG            "zxv/stats/v1"
#define ZSTATS_TAG_LEN        12u
#define ZSTATS_SIG_TAG        "zxv/stats-sig/v1"
#define ZSTATS_SIG_MSG        (16u + 32u)
#define ZSTATS_MIN_COHORT     5u /* S3 default */
#define ZSTATS_TIERS          2u /* lock tiers MEDIUM, DEEP */
#define ZSTATS_TERMS          VBOND_MAX_TERMS
#define ZSTATS_ENC_LEN        597u /* S4 */
#define ZSTATS_RAFFLE_SAVINGS 0u
#define ZSTATS_RAFFLE_PAID    1u

/* S1 present bits */
#define ZSTATS_SEC_RESERVE     0x01u
#define ZSTATS_SEC_VAULT       0x02u
#define ZSTATS_SEC_COMPRESSION 0x04u
#define ZSTATS_SEC_BONDS       0x08u
#define ZSTATS_SEC_AUCTION     0x10u
#define ZSTATS_SEC_RAFFLE      0x20u
#define ZSTATS_SEC_BARRELS     0x40u
#define ZSTATS_SEC_ZENNY       0x80u
#define ZSTATS_SEC_ALL         0xFFu

/* S3 suppressed bits */
#define ZSTATS_SUP_TIER(i) (0x1u << (i))   /* i = 0 MEDIUM, 1 DEEP */
#define ZSTATS_SUP_TERM(i) (0x100u << (i)) /* bond term index i */
#define ZSTATS_SUP_ZENNY   0x10000u
#define ZSTATS_SUP_ALL     0x1FF03u

typedef enum {
    ZSTATS_OK = 0,
    ZSTATS_ERR_ARG = -1,      /* bad argument, or another juris / publisher */
    ZSTATS_ERR_OVERFLOW = -2, /* S2: a sum does not fit 64 bits */
    ZSTATS_ERR_STATE = -3,    /* S2: a source contradicts itself */
    ZSTATS_ERR_SPACE = -4,    /* S4/S6: the output buffer is too small */
    ZSTATS_ERR_FORMAT = -5,   /* S4: tag, version, length or a bool byte */
    ZSTATS_ERR_SIG = -6,      /* S5 */
    ZSTATS_ERR_CHAIN = -7,    /* S5: prev_hash is not the chain head */
    ZSTATS_ERR_EPOCH = -8,    /* S5: epoch not above the last */
    ZSTATS_ERR_CONSIST = -9   /* S5: zstats_check failed */
} zstats_status_t;

typedef struct {
    uint64_t num, den;
    bool defined; /* den != 0; 0/0 when false */
} zstats_rat_t;

/* S3: a count-backed group */
typedef struct {
    uint32_t count;
    uint64_t sum;
} zstats_group_t;

/* S2: the raffle's aggregates, filled by the deployment (the raffle module
 * is on another branch and not included). No velocity and no time: a reader
 * derives velocity from successive snapshots. */
typedef struct {
    uint64_t epoch;
    uint64_t pot;       /* Vino in the pot */
    uint64_t tickets;   /* may exceed pot: a luck weight mints extra tickets */
    uint64_t threshold; /* counted in tickets */
    uint64_t entries;
    uint64_t mode; /* ZSTATS_RAFFLE_SAVINGS or ZSTATS_RAFFLE_PAID */
} zstats_raffle_t;

typedef struct {
    uint64_t epoch, tick;
    uint32_t juris, publisher;
    uint32_t present;    /* ZSTATS_SEC_* */
    uint32_t suppressed; /* ZSTATS_SUP_* */
    uint32_t min_cohort;
    struct {
        uint64_t reserve, claims;
        zstats_rat_t floor; /* reserve / claims */
        uint64_t req_num, req_den;
        bool floor_ok;
    } reserve;
    struct {
        uint64_t held, outstanding;
        zstats_rat_t share; /* held / outstanding */
    } vault;
    struct {
        uint64_t circulating_vino;
        uint64_t vbills_outstanding, vbills_unlocked, vbills_locked;
        zstats_group_t tier[ZSTATS_TIERS];
    } comp;
    struct {
        uint32_t n_terms;
        uint32_t k[ZSTATS_TERMS];
        zstats_group_t term[ZSTATS_TERMS];
    } bonds;
    struct {
        bool has_last;
        uint64_t last_epoch, price, qty;
        uint64_t outstanding, reserve;
        bool reserve_valid;
        uint64_t floor_num, floor_den, backing;
        bool floor_ok;
    } auction;
    zstats_raffle_t raffle;
    grant_snapshot_t barrels;
    struct {
        uint32_t live;
        uint64_t chips;
    } zenny;
} zstats_snapshot_t;

/* S2 sources: every pointer may be NULL. */
typedef struct {
    const pay_ledger_t *L;     /* needed by RESERVE, VAULT and COMPRESSION */
    uint32_t reserve_acct;     /* RESERVE: ZSTATS_NONE leaves it absent */
    uint16_t claims_asset;     /* RESERVE: the Vino asset */
    uint64_t req_num, req_den; /* RESERVE: required floor, den > 0 */
    const svault_t *vault;
    const vswap_t *swap;
    const vbond_t *bond;
    const vauc_t *auction;
    const grant_t *grants;
    const zenny_t *zenny;
    const zstats_raffle_t *raffle;
    uint32_t juris, publisher;
    uint32_t min_cohort; /* >= 1 */
} zstats_sources_t;

/* S5 reader state */
typedef struct {
    uint32_t juris, publisher;
    uint8_t head[32]; /* digest of the last accepted snapshot, or zeros */
    uint64_t epoch;   /* the last accepted epoch */
    bool started;
} zstats_chain_t;

/* verify(ctx, signer, msg, len, sig, sig_len): true when sig is signer's */
typedef bool (*zstats_verify_fn)(void *ctx, uint32_t signer, const uint8_t *msg, uint32_t len,
                                 const uint8_t *sig, uint32_t sig_len);

/* Every source NULL, reserve ZSTATS_NONE, required floor 1/1, min_cohort 5. */
void zstats_sources_default(zstats_sources_t *s);
/* S2 + S3 */
zstats_status_t zstats_collect(zstats_snapshot_t *out, uint64_t epoch, uint64_t tick,
                               const zstats_sources_t *src);
/* S5 consistency rule */
zstats_status_t zstats_check(const zstats_snapshot_t *s);

/* S4. prev may be NULL (32 zero bytes). */
zstats_status_t zstats_encode(const zstats_snapshot_t *s, const uint8_t prev[32], uint8_t *out,
                              uint32_t cap, uint32_t *len);
zstats_status_t zstats_decode(const uint8_t *in, uint32_t len, zstats_snapshot_t *out,
                              uint8_t prev[32]);
zstats_status_t zstats_digest(const zstats_snapshot_t *s, const uint8_t prev[32], uint8_t out[32]);

/* S5 */
void zstats_sign_msg(const uint8_t digest[32], uint8_t out[ZSTATS_SIG_MSG]);
void zstats_chain_init(zstats_chain_t *c, uint32_t juris, uint32_t publisher);
zstats_status_t zstats_verify(zstats_chain_t *c, const uint8_t *enc, uint32_t len,
                              const uint8_t *sig, uint32_t sig_len, zstats_verify_fn verify,
                              void *vctx, zstats_snapshot_t *snap);

/* S6 */
zstats_status_t zstats_json(const zstats_snapshot_t *s, char *buf, uint32_t cap, uint32_t *len);

#endif /* ZXV_ZSTATS_H */
