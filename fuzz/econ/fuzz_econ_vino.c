/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_vino.c — state-machine property harness for the Vino ledger
 * (vino/vino.c).
 *
 * The input is an op sequence over a fresh ledger with 8 accounts. Ops:
 *   MINT      credit an account directly (the module has no capital mint, so
 *             this models an external deposit; tracked as `minted`)
 *   TRANSFER  vino_transfer with any capital index (in or out of range) and
 *             an edge-biased amount (0, 1, 2^31, 2^63, UINT64_MAX, ...)
 *   TRADE     vino_trade (a FINANCIAL transfer)
 *   BRIDGE    vino_bridge (a FINANCIAL transfer)
 *   ISSUE     vino_issue (asset counts, 32-bit)
 *   BALANCE   vino_get_balance with any capital index
 * Invariants after every op:
 *   V1  per capital: sum(balance) == minted (transfers conserve value)
 *   V2  a transfer that succeeds moves exactly `amount` from -> to; one that
 *       fails changes no balance, no counter and no chain head
 *   V3  per asset class: sum(asset_balances) == issued
 *   V4  num_txns counts exactly the successful transfers and issues, and
 *       the audit copy matches the primary record
 *   V5  total_volume[cap] never decreases (it is a running statistic; a
 *       wrap back toward zero was found under -fsanitize=integer)
 * Any break calls abort() so libFuzzer / the replay driver report it. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vino.h"
#include "rmag_core.h"
#include "fuzz_in.h"

#define NACCT 8

static vino_ledger_t g_v;
static unsigned long long g_ops;

static const char *addr(uint32_t i)
{
    static const char *a[NACCT + 1] = {"A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7", "NOPE"};
    return a[i % (NACCT + 1)];
}

typedef struct {
    uint64_t bal[NACCT][CAP_MAX];
    uint32_t assets[NACCT][ASSET_MAX];
    uint64_t volume[CAP_MAX];
    uint32_t num_txns;
    uint8_t head[VINO_HASH_LEN];
} snap_t;

static void snap(snap_t *s)
{
    memset(s, 0, sizeof *s); /* padding too: snapshots are compared with memcmp */
    for (uint32_t i = 0; i < NACCT; i++) {
        memcpy(s->bal[i], g_v.balances[i].balance, sizeof s->bal[i]);
        memcpy(s->assets[i], g_v.balances[i].asset_balances, sizeof s->assets[i]);
    }
    memcpy(s->volume, g_v.total_volume, sizeof s->volume);
    s->num_txns = g_v.num_txns;
    memcpy(s->head, g_v.chain_head_hash, VINO_HASH_LEN);
}

static void same(const snap_t *a, const snap_t *b)
{
    if (memcmp(a, b, sizeof *a) != 0) abort(); /* V2: a refused op changed state */
}

static __uint128_t g_minted[CAP_MAX];
static uint64_t g_issued[ASSET_MAX];
static uint32_t g_ok_txns;

static void check_totals(void)
{
    for (uint32_t c = 0; c < CAP_MAX; c++) {
        __uint128_t s = 0;
        for (uint32_t i = 0; i < NACCT; i++) s += g_v.balances[i].balance[c];
        if (s != g_minted[c]) abort(); /* V1 */
    }
    for (uint32_t a = 0; a < ASSET_MAX; a++) {
        uint64_t s = 0;
        for (uint32_t i = 0; i < NACCT; i++) s += g_v.balances[i].asset_balances[a];
        if (s != g_issued[a]) abort(); /* V3 */
    }
    if (g_v.num_txns != g_ok_txns || g_v.num_audit != g_ok_txns) abort(); /* V4 */
    if (g_ok_txns) {
        const vino_transaction_t *p = &g_v.primary[g_ok_txns - 1];
        if (memcmp(p, &g_v.audit[g_ok_txns - 1], sizeof *p) != 0) abort();
        if (memcmp(p->hash, g_v.chain_head_hash, VINO_HASH_LEN) != 0) abort();
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    fz_in in;
    fz_init(&in, data, size);
    vino_init(&g_v, 1);
    /* RMAG's quota table is process-global and vino_init keeps it: reset the
     * slots this ledger uses so every input starts from the same state */
    for (ordinal_t o = 0; o <= NACCT + 1; o++) {
        rational_t z = {0, 1};
        rmag_set_quota(o, z);
    }
    char nm[8];
    for (uint32_t i = 0; i < NACCT; i++) {
        snprintf(nm, sizeof nm, "acct%u", i);
        if (vino_create_account(&g_v, addr(i), nm) != (int32_t) i) abort();
    }
    if (vino_create_account(&g_v, "A0", "dup") != -1) abort(); /* one account per address */
    memset(g_minted, 0, sizeof g_minted);
    memset(g_issued, 0, sizeof g_issued);
    g_ok_txns = 0;

    snap_t before, after;
    while (in.n) {
        uint8_t op = fz_u8(&in) % 6u;
        uint32_t from = fz_u8(&in), to = fz_u8(&in);
        uint8_t capb = fz_u8(&in);
        uint32_t cap = capb < 0xF0u ? capb % CAP_MAX : capb; /* mostly valid, some not */
        uint64_t amt = fz_edge64(&in);
        snap(&before);
        int32_t r;
        g_ops++;
        switch (op) {
        case 0: { /* MINT: external deposit */
            uint32_t i = from % NACCT, c = cap % CAP_MAX;
            if (g_v.balances[i].balance[c] > UINT64_MAX - amt) break; /* not representable */
            g_v.balances[i].balance[c] += amt;
            g_minted[c] += amt;
            break;
        }
        case 1:
        case 2:
        case 3: {
            const char *fa = addr(from), *ta = addr(to);
            if (op == 1)
                r = vino_transfer(&g_v, fa, ta, amt, (capital_type_t) cap, RAIL_VINO_NATIVE, "t");
            else if (op == 2)
                r = vino_trade(&g_v, fa, ta, ASSET_EQUITY, amt, 1, "trade");
            else
                r = vino_bridge(&g_v, fa, ta, amt, RAIL_SWIFT, RAIL_SEPA, "bridge");
            uint32_t c = op == 1 ? cap : (uint32_t) CAP_FINANCIAL;
            snap(&after);
            if (r < 0) {
                same(&before, &after);
            } else {
                uint32_t f = from % (NACCT + 1), t = to % (NACCT + 1);
                if (f >= NACCT || t >= NACCT || c >= CAP_MAX) abort(); /* accepted a bad arg */
                if (f != t) {
                    if (after.bal[f][c] != before.bal[f][c] - amt) abort();
                    if (after.bal[t][c] != before.bal[t][c] + amt) abort();
                    if (before.bal[f][c] < amt) abort(); /* overdraft */
                }
                for (uint32_t i = 0; i < NACCT; i++)
                    for (uint32_t k = 0; k < CAP_MAX; k++)
                        if ((k != c || (i != f && i != t)) && after.bal[i][k] != before.bal[i][k])
                            abort(); /* touched an account it should not have */
                g_ok_txns++;
            }
            break;
        }
        case 4: {
            uint32_t asset = capb < 0xF0u ? capb % ASSET_MAX : capb;
            r = vino_issue(&g_v, addr(to), (asset_class_t) asset, amt, "issue");
            snap(&after);
            if (r < 0) {
                same(&before, &after);
            } else {
                uint32_t t = to % (NACCT + 1);
                if (t >= NACCT || asset >= ASSET_MAX) abort();
                if (after.assets[t][asset] != before.assets[t][asset] + amt) abort();
                g_issued[asset] += amt;
                g_ok_txns++;
            }
            break;
        }
        case 5: {
            uint64_t out = 0;
            r = vino_get_balance(&g_v, addr(from), (capital_type_t) cap, &out);
            uint32_t f = from % (NACCT + 1);
            if (r == 0 && (f >= NACCT || cap >= CAP_MAX)) abort(); /* read out of range */
            if (r == 0 && out != g_v.balances[f].balance[cap]) abort();
            snap(&after);
            same(&before, &after);
            break;
        }
        }
        check_totals();
        for (uint32_t c = 0; c < CAP_MAX; c++)
            if (g_v.total_volume[c] < before.volume[c]) abort(); /* V5 */
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_vino: %llu ops checked\n", g_ops);
}
