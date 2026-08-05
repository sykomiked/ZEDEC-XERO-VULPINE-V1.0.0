/* count_house.h — The Count House & Stash Buckets: P2P Fractional-Reserve
 * Valuation Engine (ZEDEC XERO VULPINE / ZXV)
 *
 * A Stash Bucket is a ring-fenced reserve slot holding cryptographic
 * tokens deposited by one peer mesh node. The Count House aggregates
 * all of a local node's Stash Buckets plus its Robin DeBanks crypto
 * reserves into a single floor-price valuation for the node's
 * self-minted currency, and detects hyper-inflation / Sybil attempts.
 *
 * V_local = (Sum_i(Balance_i * TrustWeight_i) + CryptoReserves) / TotalSupplyMinted
 *
 * Design notes (filling gaps in the original spec):
 *   - peer_trust_weight is NOT caller-supplied on deposit. If a peer
 *     could hand the Count House its own trust score, Sybil resistance
 *     would be trivially defeated. Trust is instead managed here:
 *     it rises on successfully-verified deposits and falls (and the
 *     deposit's value is rejected) on signature verification failure.
 *   - A bucket's balance only counts toward valuation/collateral if
 *     its most recent signature verified. Unverified balances would
 *     otherwise let anyone inflate their own backing for free.
 *   - proof_sig verification is pluggable via count_house_t.verify_sig
 *     (NULL = HMAC-SHA256 verification, verifies HMAC-SHA256 against kernel authority key).
 *     Wiring in a real signature scheme (Ed25519 / ML-DSA-44) is a
 *     separate, security-critical task -- see verify_sig doc below.
 *   - peer_pubkey[32] was added to StashBucket; the original spec had
 *     no public key field, and a signature can't be verified without
 *     one. 32 bytes fits Ed25519 raw keys; a real ML-DSA-44 public key
 *     (~1312 bytes) would need a larger field.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: LicenseRef-OPL-1.1 AND CC-BY-SA-4.0
 * Licensed under OPL-1.1, SEL-3.3, the Royal Writ of the Sicilian Crown,
 * and CC BY-SA 4.0. See LICENSE at the repository root.
 */
#ifndef COUNT_HOUSE_H
#define COUNT_HOUSE_H

#include <stdint.h>
#include <stdbool.h>
#include "m5_types.h"
#include "surplus.h"
#include "edp_risk.h"

/* ===== Constants ===== */

#define CH_MAX_STASH_BUCKETS   64
#define CH_MAX_LABEL_LEN       32
#define CH_PROOF_SIG_LEN       64
#define CH_PUBKEY_LEN          32

/* Dynamic trust metric (0-1000), adjusted by the Count House itself */
#define CH_TRUST_INITIAL       100   /* new peer starts at 10% trust */
#define CH_TRUST_INCREMENT     50    /* +5% per verified deposit */
#define CH_TRUST_PENALTY       200   /* -20% per failed verification */
#define CH_TRUST_MAX           1000

/* Anti-Sybil / anti-spam thresholds (Collateral Ratio = reserves / supply) */
#define CH_COLLATERAL_HYPERINFLATION_THRESHOLD  SR_FROM_FLOAT(0.001)
#define CH_COLLATERAL_PRIORITY_THRESHOLD        SR_FROM_FLOAT(0.10)

/* ===== Fractal Scaling ===== */

typedef enum {
    CH_SCALE_0       = 0, /* Local node — single Count House with Stash Buckets */
    CH_SCALE_ALLIANCE = 1, /* Regional cluster — aggregates Scale-0 children */
    CH_SCALE_GLOBAL   = 2, /* Network-wide — aggregates Alliance children */
    CH_SCALE_MAX      = 3
} ch_scale_t;

#define CH_MAX_CHILDREN        16   /* child Count Houses per fractal tier */
#define CH_FIB_MINT_BASE       1    /* Fibonacci base for mint curve (F(1)=1) */
#define CH_FIB_MINT_MAX_LEVEL  30   /* cap to prevent uint32 overflow at F(30)=832040 */

__attribute__((unused))
static const char *ch_scale_name(ch_scale_t s) {
    switch (s) {
        case CH_SCALE_0:        return "Scale-0";
        case CH_SCALE_ALLIANCE: return "Alliance";
        case CH_SCALE_GLOBAL:   return "Global";
        default:                return "Unknown";
    }
}

/* ===== Stash Bucket ===== */

typedef struct stash_bucket {
    word168_t peer_node_id;             /* 168-bit peer ID */
    uint64_t  token_balance;            /* peer tokens held */
    uint32_t  peer_trust_weight;        /* 0-1000, Count-House-managed */
    uint8_t   peer_pubkey[CH_PUBKEY_LEN]; /* verification key (see file doc) */
    uint8_t   proof_sig[CH_PROOF_SIG_LEN]; /* signature over (peer_id||balance) */
    bool      sig_verified;             /* result of most recent verify */
    bool      active;
} stash_bucket_t;

/* ===== Count House (hardware-as-code device) ===== */

typedef struct count_house {
    uint32_t device_id;
    char name[CH_MAX_LABEL_LEN];

    stash_bucket_t buckets[CH_MAX_STASH_BUCKETS];
    uint32_t num_buckets;

    uint64_t total_supply_minted;      /* Token_self units issued so far */
    surplus_real_t crypto_reserves;    /* fed from Robin DeBanks vault */

    surplus_real_t v_local;            /* last computed floor price */
    surplus_real_t collateral_ratio;   /* trust-weighted reserves / supply */

    /* M5 coordinates (kernel-wide convention) */
    m5_coords_t m5;
    surplus_real_t coverage_ratio;

    /* IRQ-style status flags */
    bool irq_hyperinflation_detected;  /* collateral_ratio < 0.001 */
    bool irq_priority_dropped;         /* collateral_ratio < 0.10 */

    /* Pluggable signature verification. Return true iff proof_sig is a
     * valid signature by peer_pubkey over (peer_node_id || token_balance).
     * NULL = ch_default_verify_sig, which performs Ed25519 ASYMMETRIC
     * verification against the embedded PUBLIC key (the private key is held
     * offline). Override this hook to supply a different trust root, or in
     * tests that cannot produce a real Ed25519 signature. */
    bool (*verify_sig)(const stash_bucket_t *bucket);

    /* ===== Fractal scaling fields ===== */
    ch_scale_t scale;                         /* this Count House's tier */
    uint32_t fractal_level;                   /* Fibonacci mint-curve level */
    struct count_house *children[CH_MAX_CHILDREN]; /* child Count Houses */
    uint32_t num_children;
    surplus_real_t v_fractal;                 /* recursive aggregate valuation */
} count_house_t;

/* ===== API ===== */

void count_house_init(count_house_t *ch, uint32_t device_id, const char *name);

/* Default verifier: Ed25519 asymmetric verification of proof_sig over
 * (peer_node_id || token_balance) against the embedded COUNT_HOUSE public
 * key. This is real public-key verification — the corresponding private key
 * is held offline and never compiled in. (It replaced an earlier HMAC
 * placeholder; callers needing a different trust root override
 * count_house_t.verify_sig.) */
bool ch_default_verify_sig(const stash_bucket_t *bucket);

/* Deposit peer tokens into (or top up) a Stash Bucket. proof_sig must
 * verify against peer_pubkey for the deposit to count toward balance.
 * Returns bucket index on success, -1 if capacity exceeded, -2 if
 * signature verification failed (deposit rejected, trust penalized). */
int32_t count_house_deposit(count_house_t *ch, const word168_t *peer_id,
                             const uint8_t peer_pubkey[CH_PUBKEY_LEN],
                             uint64_t amount,
                             const uint8_t proof_sig[CH_PROOF_SIG_LEN]);

int32_t count_house_find_bucket(count_house_t *ch, const word168_t *peer_id);

void count_house_set_crypto_reserves(count_house_t *ch, surplus_real_t reserves);

/* Mint new Token_self supply; recomputes valuation/collateral afterward. */
uint64_t count_house_mint(count_house_t *ch, uint64_t amount);

/* Recompute V_local, collateral_ratio, coverage_ratio and IRQ flags from
 * current Stash Bucket + crypto reserve state. Returns V_local. */
surplus_real_t count_house_valuation(count_house_t *ch);

/* Alias for count_house_valuation() matching the `count-house --audit`
 * CLI verb; returns true iff the node is NOT flagged for hyperinflation. */
bool count_house_audit(count_house_t *ch);

/* ===== Fractal Scaling API ===== */

/* Initialize a Count House at a specific fractal tier (Scale-0, Alliance,
 * or Global). Scale-0 houses Stash Buckets directly; Alliance/Global
 * houses aggregate child Count Houses via count_house_add_child(). */
void count_house_init_fractal(count_house_t *ch, uint32_t device_id,
                                const char *name, ch_scale_t scale);

/* Attach a child Count House to a parent (Alliance or Global). The child
 * must be at the tier below (Scale-0 -> Alliance, Alliance -> Global).
 * Returns 0 on success, -1 if capacity exceeded, -2 if tier mismatch. */
int32_t count_house_add_child(count_house_t *parent, count_house_t *child);

/* Fibonacci mint curve: the maximum mintable amount at a given fractal
 * level is F(level+1) * base_unit. This makes supply expansion follow
 * the Fibonacci sequence rather than linear growth, so early-stage nodes
 * expand conservatively and mature nodes can mint more -- but always
 * bounded by the collateral ratio gate in count_house_mint(). */
uint64_t count_house_fib_mint_allowance(const count_house_t *ch, uint64_t base_unit);

/* Recursive valuation: computes v_fractal as the trust-weighted aggregate
 * of all children's v_local (or v_fractal for deeper recursion). For a
 * Scale-0 Count House with no children, v_fractal == v_local. */
surplus_real_t count_house_fractal_valuation(count_house_t *ch);

/* Recursive audit: returns true iff this node AND all children pass
 * count_house_audit() (no hyperinflation anywhere in the subtree). */
bool count_house_fractal_audit(count_house_t *ch);

#endif /* COUNT_HOUSE_H */
