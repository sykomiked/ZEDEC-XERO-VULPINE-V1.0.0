/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_count_house.c — state-machine property harness for the Count
 * House (count_house/count_house.c): stash-bucket deposits, supply minting
 * and the collateral gate.
 *
 * Signature verification is replaced by a hook that accepts a deposit iff
 * proof_sig[0] is even (the harness controls it from the input), so both
 * the verified and the refused paths run. Ops: DEPOSIT (peer 0..71, so the
 * 64-bucket capacity is crossed), MINT, SET_RESERVES, VALUATION / AUDIT,
 * with edge-biased amounts.
 * Invariants after every op:
 *   C1  sum(token_balance) == sum before + amount for a verified deposit,
 *       unchanged for a refused one (no wrap: a balance never decreases on
 *       a deposit)
 *   C2  total_supply_minted == before + the amount mint() returned, and a
 *       mint returns either 0 or exactly the amount asked
 *   C3  trust stays in 0..CH_TRUST_MAX; num_buckets <= CH_MAX_STASH_BUCKETS
 *   C4  a mint never lands the node below the hyperinflation floor */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "count_house.h"
#include "fuzz_in.h"

static count_house_t g_ch;
static unsigned long long g_ops;

static bool hook(const stash_bucket_t *b)
{
    return (b->proof_sig[0] & 1u) == 0;
}

static __uint128_t stash_sum(void)
{
    __uint128_t s = 0;
    for (uint32_t i = 0; i < g_ch.num_buckets; i++) s += g_ch.buckets[i].token_balance;
    return s;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    fz_in in;
    fz_init(&in, data, size);
    count_house_init(&g_ch, 1, "fuzz");
    g_ch.verify_sig = hook;

    while (in.n) {
        uint8_t op = fz_u8(&in) % 4u;
        g_ops++;
        if (op == 0) {
            word168_t id;
            memset(&id, 0, sizeof id);
            uint8_t peer = (uint8_t) (fz_u8(&in) % 72u);
            memcpy(&id, &peer, 1);
            uint8_t pk[CH_PUBKEY_LEN], sig[CH_PROOF_SIG_LEN];
            memset(pk, peer, sizeof pk);
            memset(sig, 0, sizeof sig);
            sig[0] = fz_u8(&in);
            uint64_t amt = fz_edge64(&in);
            int32_t idx = count_house_find_bucket(&g_ch, &id);
            uint64_t prior = idx >= 0 ? g_ch.buckets[idx].token_balance : 0;
            uint32_t nb = g_ch.num_buckets;
            __uint128_t before = stash_sum();
            int32_t r = count_house_deposit(&g_ch, &id, pk, amt, sig);
            __uint128_t after = stash_sum();
            if (r >= 0) {
                if (g_ch.buckets[r].token_balance < prior) abort(); /* C1: wrapped */
                if (after != before + amt) abort();                 /* C1 */
                if (!g_ch.buckets[r].sig_verified) abort();
            } else {
                if (after != before) abort(); /* C1: refused deposit moved value */
                if (r == -1 && g_ch.num_buckets != nb) abort();
            }
        } else if (op == 1) {
            uint64_t amt = fz_edge64(&in);
            uint64_t s0 = g_ch.total_supply_minted;
            uint64_t got = count_house_mint(&g_ch, amt);
            if (got != 0 && got != amt) abort();                  /* C2 */
            if (g_ch.total_supply_minted != s0 + got) abort();    /* C2 */
            if (got && g_ch.total_supply_minted < s0) abort();    /* wrapped */
            if (got && g_ch.irq_hyperinflation_detected) abort(); /* C4 */
        } else if (op == 2) {
            uint32_t r = fz_u32(&in);
            count_house_set_crypto_reserves(&g_ch, SR_FROM_INT(r % 1000000u));
        } else {
            (void) count_house_valuation(&g_ch);
            (void) count_house_audit(&g_ch);
            (void) count_house_fib_mint_allowance(&g_ch, fz_edge64(&in));
        }
        if (g_ch.num_buckets > CH_MAX_STASH_BUCKETS) abort(); /* C3 */
        for (uint32_t i = 0; i < g_ch.num_buckets; i++)
            if (g_ch.buckets[i].peer_trust_weight > CH_TRUST_MAX) abort();
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS"))
        fprintf(stderr, "fuzz_econ_count_house: %llu ops checked\n", g_ops);
}
