/* count_house.c — The Count House & Stash Buckets implementation
 *
 * See count_house.h for the full design rationale (trust-weighted
 * anti-Sybil gating, signature-staged deposits, ZPD zero-supply
 * handling). This file follows the same conventions as
 * kernel/src/hardware/dlp_projector.c and quantum_device.c: SR_*
 * fixed-point macros only (no surplus.c/edp_risk.c linkage needed --
 * coverage is computed inline, matching quantum_device.c's precedent),
 * m5_coords_t mirroring, and a permissive-but-labeled placeholder for
 * the one piece (signature verification) that is security-critical
 * and deliberately left pluggable rather than faked.
 *
 * Author: H.M. Michael-Laurence: Curzi (c)
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include "count_house.h"
#include "../robin_debanks/ed25519_verify.h"
#include <string.h>

/* Sum of (balance * trust_weight / CH_TRUST_MAX) across active,
 * signature-verified buckets only -- an unverified balance must never
 * count toward backing, or any peer could inflate the node's own
 * valuation for free by claiming an unverified deposit. */
static surplus_real_t ch_trust_weighted_stash(const count_house_t *ch) {
    surplus_real_t total = SR_ZERO;
    surplus_real_t trust_denom = SR_FROM_INT(CH_TRUST_MAX);
    for (uint32_t i = 0; i < ch->num_buckets; i++) {
        const stash_bucket_t *b = &ch->buckets[i];
        if (!b->active || !b->sig_verified) continue;
        surplus_real_t balance = SR_FROM_INT((int64_t)b->token_balance);
        surplus_real_t weight = SR_DIV(SR_FROM_INT((int64_t)b->peer_trust_weight), trust_denom);
        total = SR_ADD(total, SR_MUL(balance, weight));
    }
    return total;
}

void count_house_init(count_house_t *ch, uint32_t device_id, const char *name) {
    if (!ch) return;
    memset(ch, 0, sizeof(*ch));
    ch->device_id = device_id;

    uint32_t i;
    for (i = 0; i + 1 < CH_MAX_LABEL_LEN && name && name[i]; i++) {
        ch->name[i] = name[i];
    }
    ch->name[i] = '\0';

    ch->num_buckets = 0;
    ch->total_supply_minted = 0;
    ch->crypto_reserves = SR_ZERO;
    ch->verify_sig = 0; /* NULL -> falls back to ch_default_verify_sig */

    /* m5.omega/chi wired to device_id so multiple Count Houses (Scale 0
     * / Alliance / Global fractal tiers, see roadmap) stay
     * distinguishable in M5-coordinate space once that scaling lands. */
    ch->m5.omega = device_id;
    ch->m5.chi = device_id;
    ch->m5.phi = SR_ZERO;

    /* Fractal defaults: Scale-0, level 0, no children */
    ch->scale = CH_SCALE_0;
    ch->fractal_level = 0;
    ch->num_children = 0;
    ch->v_fractal = SR_ZERO;

    count_house_valuation(ch);
}

bool ch_default_verify_sig(const stash_bucket_t *bucket) {
    if (!bucket) return false;
    /* Ed25519 asymmetric verification against embedded PUBLIC key.
     * Private key held offline/HSM-backed. */
    uint8_t msg[21 + 8]; uint32_t pos = 0;
    for (uint32_t i = 0; i < 21; i++) msg[pos++] = bucket->peer_node_id.bytes[i];
    for (uint32_t i = 0; i < 8; i++) msg[pos++] = (uint8_t)(bucket->token_balance >> (i*8));
    return ed25519_verify(msg, pos, bucket->proof_sig, ED25519_PUBKEY_COUNT_HOUSE);
}

int32_t count_house_find_bucket(count_house_t *ch, const word168_t *peer_id) {
    if (!ch || !peer_id) return -1;
    for (uint32_t i = 0; i < ch->num_buckets; i++) {
        if (!ch->buckets[i].active) continue;
        if (memcmp(ch->buckets[i].peer_node_id.bytes, peer_id->bytes, WORD168_OCTETS) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

int32_t count_house_deposit(count_house_t *ch, const word168_t *peer_id,
                             const uint8_t peer_pubkey[CH_PUBKEY_LEN],
                             uint64_t amount,
                             const uint8_t proof_sig[CH_PROOF_SIG_LEN]) {
    if (!ch || !peer_id || !peer_pubkey || !proof_sig) return -2;

    int32_t idx = count_house_find_bucket(ch, peer_id);
    bool is_new = (idx < 0);

    if (is_new) {
        if (ch->num_buckets >= CH_MAX_STASH_BUCKETS) return -1;
        /* Commit the bucket immediately (not only on successful
         * verification) so a peer_id's trust penalty persists across
         * repeated failed attempts -- if a brand-new peer's first
         * deposit could just be silently forgotten on failure, an
         * attacker could retry forever as a "new" peer for free,
         * defeating the whole point of trust-weighted anti-Sybil
         * gating. This does cost one of the CH_MAX_STASH_BUCKETS
         * slots per unique peer_id ever seen, verified or not. */
        idx = (int32_t)ch->num_buckets++;
        stash_bucket_t *nb = &ch->buckets[idx];
        nb->peer_node_id = *peer_id;
        nb->token_balance = 0;
        nb->peer_trust_weight = CH_TRUST_INITIAL;
        nb->sig_verified = false;
        nb->active = true;
        memset(nb->peer_pubkey, 0, CH_PUBKEY_LEN);
    }

    stash_bucket_t *bucket = &ch->buckets[idx];
    uint64_t prior_balance = bucket->token_balance;
    uint8_t prior_pubkey[CH_PUBKEY_LEN];
    memcpy(prior_pubkey, bucket->peer_pubkey, CH_PUBKEY_LEN);

    /* Stage the claimed post-deposit state so verify_sig checks
     * proof_sig against exactly what it attests to: (peer_node_id ||
     * token_balance), token_balance being the NEW cumulative total
     * per the field's documented signing convention (count_house.h). */
    memcpy(bucket->peer_pubkey, peer_pubkey, CH_PUBKEY_LEN);
    memcpy(bucket->proof_sig, proof_sig, CH_PROOF_SIG_LEN);
    bucket->token_balance = prior_balance + amount;

    bool ok = ch->verify_sig ? ch->verify_sig(bucket) : ch_default_verify_sig(bucket);

    if (ok) {
        bucket->sig_verified = true;
        bucket->peer_trust_weight += CH_TRUST_INCREMENT;
        if (bucket->peer_trust_weight > CH_TRUST_MAX) {
            bucket->peer_trust_weight = CH_TRUST_MAX;
        }
    } else {
        /* Reject: a failed signature must never move money or hijack
         * a bucket's key-of-record -- roll both back. The trust
         * penalty sticks (bucket is already committed above), which
         * is what makes Sybil spam actually cost something instead of
         * being a free "retry as a fresh peer" loop. */
        bucket->token_balance = prior_balance;
        memcpy(bucket->peer_pubkey, prior_pubkey, CH_PUBKEY_LEN);
        bucket->sig_verified = false;
        bucket->peer_trust_weight =
            (bucket->peer_trust_weight > CH_TRUST_PENALTY)
                ? bucket->peer_trust_weight - CH_TRUST_PENALTY : 0;
    }

    count_house_valuation(ch);
    return ok ? idx : -2;
}

void count_house_set_crypto_reserves(count_house_t *ch, surplus_real_t reserves) {
    if (!ch) return;
    ch->crypto_reserves = reserves;
    count_house_valuation(ch);
    /* Keep v_fractal in sync for leaf nodes (no children -> v_fractal == v_local).
     * For parent nodes, the caller should call count_house_fractal_valuation()
     * to recompute the full recursive aggregate. */
    if (ch->num_children == 0) {
        ch->v_fractal = ch->v_local;
    }
}

/* Largest total supply the collateral ratio can be computed for: SR_FROM_INT
 * is Q32.32 on the kernel path, so its integer part must stay below 2^31. */
#ifdef TEST_HOST
#    define CH_SUPPLY_MAX ((uint64_t) INT64_MAX)
#else
#    define CH_SUPPLY_MAX ((uint64_t) INT32_MAX)
#endif

uint64_t count_house_mint(count_house_t *ch, uint64_t amount) {
    if (!ch || amount == 0) return 0;

    /* Refuse a mint that would wrap the supply counter (it used to wrap to a
     * tiny value, pass the collateral gate, and return the huge amount) or
     * push it past what the ratio below can represent. */
    if (ch->total_supply_minted > CH_SUPPLY_MAX || amount > CH_SUPPLY_MAX - ch->total_supply_minted)
        return 0;
    uint64_t candidate_supply = ch->total_supply_minted + amount;

    /* Anti-Sybil / anti-hyperinflation gate: preview the collateral
     * ratio the candidate supply WOULD produce before committing the
     * mint, using the current (fixed-at-this-instant) trust-weighted
     * reserves. Refusing here means a mint call can never itself land
     * the node in a state count_house_valuation() would immediately
     * flag as hyperinflated. */
    surplus_real_t weighted_reserves = SR_ADD(ch_trust_weighted_stash(ch), ch->crypto_reserves);
    surplus_real_t candidate_ratio = (candidate_supply == 0)
        ? SR_ONE
        : SR_DIV(weighted_reserves, SR_FROM_INT((int64_t)candidate_supply));

    if (SR_CMP(candidate_ratio, CH_COLLATERAL_HYPERINFLATION_THRESHOLD) < 0) {
        /* Refuse: minting this much would push collateral below the
         * hyperinflation floor. Caller may retry with a smaller amount. */
        return 0;
    }

    ch->total_supply_minted = candidate_supply;
    count_house_valuation(ch);
    return amount;
}

surplus_real_t count_house_valuation(count_house_t *ch) {
    if (!ch) return SR_ZERO;

    surplus_real_t weighted_stash = ch_trust_weighted_stash(ch);
    surplus_real_t total_reserves = SR_ADD(weighted_stash, ch->crypto_reserves);

    if (ch->total_supply_minted == 0) {
        /* Zero-Preserving Division (see edp_risk.h's ZPD concept):
         * dividing by a zero supply should preserve the reserves'
         * own class identity rather than fault -- there is no minted
         * currency yet to price, so "value per unit" is undefined;
         * report the raw reserve floor and treat collateral as fully
         * backed (1.0) since there is nothing outstanding to under-back. */
        ch->v_local = total_reserves;
        ch->collateral_ratio = SR_ONE;
    } else {
        surplus_real_t supply = SR_FROM_INT((int64_t)ch->total_supply_minted);
        ch->v_local = SR_DIV(total_reserves, supply);
        /* V_local and collateral_ratio coincide under the current
         * single-formula spec (count_house.h) -- both are
         * total_reserves/supply. Kept as two fields since the fractal
         * scaling / Fibonacci mint curve roadmap item may cause them
         * to diverge later (e.g. a curve-adjusted floor price vs. the
         * raw backing ratio). */
        ch->collateral_ratio = ch->v_local;
    }

    ch->irq_hyperinflation_detected =
        SR_CMP(ch->collateral_ratio, CH_COLLATERAL_HYPERINFLATION_THRESHOLD) < 0;
    ch->irq_priority_dropped =
        SR_CMP(ch->collateral_ratio, CH_COLLATERAL_PRIORITY_THRESHOLD) < 0;

    /* Mirror into M5 coordinates per kernel-wide convention: r =
     * magnitude (V_local), ell = collateral truth-value clamped to
     * [0,1] (a ratio > 1 is fully-backed-and-then-some, clamp so it
     * still reads as a valid logical-axis truth value). */
    ch->m5.r = ch->v_local;
    surplus_real_t ell = ch->collateral_ratio;
    if (SR_CMP(ell, SR_ONE) > 0) ell = SR_ONE;
    if (SR_CMP(ell, SR_ZERO) < 0) ell = SR_ZERO;
    ch->m5.ell = ell;

    /* Coverage hyperbola (r*ell/1.8), computed inline rather than via
     * edp_coverage_product() -- matches the established convention in
     * quantum_device.c/dlp_projector.c of not linking edp_risk.c just
     * for this one formula under the TEST_HOST/freestanding SR_* macro
     * system, where the macros alone are already sufficient. */
    surplus_real_t product = SR_MUL(ch->m5.r, ch->m5.ell);
    surplus_real_t floor = SR_DIV(SR_FROM_INT(COVERAGE_FLOOR_NUM), SR_FROM_INT(COVERAGE_FLOOR_DEN));
    ch->coverage_ratio = SR_DIV(product, floor);

    return ch->v_local;
}

bool count_house_audit(count_house_t *ch) {
    if (!ch) return false;
    count_house_valuation(ch);
    return !ch->irq_hyperinflation_detected;
}

/* ===== Fractal Scaling Implementation ===== */

void count_house_init_fractal(count_house_t *ch, uint32_t device_id,
                                const char *name, ch_scale_t scale) {
    if (!ch) return;
    count_house_init(ch, device_id, name);
    ch->scale = scale;

    /* Fractal level tracks the Fibonacci mint-curve position:
     * Scale-0 starts at level 1 (F(2)=1), Alliance at level 3 (F(4)=3),
     * Global at level 5 (F(6)=8). This gives each tier a naturally
     * expanding but bounded mint allowance curve. */
    switch (scale) {
        case CH_SCALE_0:        ch->fractal_level = 1; break;
        case CH_SCALE_ALLIANCE: ch->fractal_level = 3; break;
        case CH_SCALE_GLOBAL:   ch->fractal_level = 5; break;
        default:                ch->fractal_level = 0; break;
    }

    /* Set v_fractal for leaf nodes (no children -> v_fractal == v_local) */
    count_house_fractal_valuation(ch);
}

int32_t count_house_add_child(count_house_t *parent, count_house_t *child) {
    if (!parent || !child) return -1;

    /* Tier validation: child must be exactly one tier below parent */
    if (parent->scale == CH_SCALE_0) return -2; /* Scale-0 can't have children */
    if (parent->scale == CH_SCALE_ALLIANCE && child->scale != CH_SCALE_0) return -2;
    if (parent->scale == CH_SCALE_GLOBAL && child->scale != CH_SCALE_ALLIANCE) return -2;

    if (parent->num_children >= CH_MAX_CHILDREN) return -1;

    parent->children[parent->num_children++] = child;
    count_house_fractal_valuation(parent);
    return 0;
}

uint64_t count_house_fib_mint_allowance(const count_house_t *ch, uint64_t base_unit) {
    if (!ch || base_unit == 0) return 0;

    uint32_t level = ch->fractal_level;
    if (level > CH_FIB_MINT_MAX_LEVEL) level = CH_FIB_MINT_MAX_LEVEL;

    /* F(level+1): level 1 -> F(2)=1, level 3 -> F(4)=3, level 5 -> F(6)=8 */
    uint32_t fib = edp_fibonacci(level + 1);
    if (fib == 0) return base_unit; /* fallback: at least base_unit */

    /* Cap at uint64 to prevent overflow on multiply */
    if (fib > 0 && base_unit > (UINT64_MAX / fib)) {
        return UINT64_MAX;
    }
    return base_unit * fib;
}

surplus_real_t count_house_fractal_valuation(count_house_t *ch) {
    if (!ch) return SR_ZERO;

    /* First compute this node's own local valuation */
    count_house_valuation(ch);

    if (ch->num_children == 0) {
        /* Leaf node: v_fractal == v_local */
        ch->v_fractal = ch->v_local;
        return ch->v_fractal;
    }

    /* Aggregate: v_fractal = (own_v_local + sum(children_v_fractal)) /
     * (1 + num_children). This weights the parent's own reserves equally
     * with each child's aggregate, so a parent with N children gets
     * 1/(N+1) weight on its own local reserves and N/(N+1) on children. */
    surplus_real_t sum = ch->v_local;
    for (uint32_t i = 0; i < ch->num_children; i++) {
        if (!ch->children[i]) continue;
        surplus_real_t child_v = count_house_fractal_valuation(ch->children[i]);
        sum = SR_ADD(sum, child_v);
    }
    surplus_real_t denom = SR_FROM_INT((int64_t)(ch->num_children + 1));
    ch->v_fractal = SR_DIV(sum, denom);

    return ch->v_fractal;
}

bool count_house_fractal_audit(count_house_t *ch) {
    if (!ch) return false;

    /* Audit this node first */
    if (!count_house_audit(ch)) return false;

    /* Recursively audit all children */
    for (uint32_t i = 0; i < ch->num_children; i++) {
        if (!ch->children[i]) continue;
        if (!count_house_fractal_audit(ch->children[i])) return false;
    }
    return true;
}
