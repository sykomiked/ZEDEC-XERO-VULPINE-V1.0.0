/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_econ_triple.c — state-machine property harness for the nine-capital
 * triple ledger (finance/triple_ledger.c), built WITHOUT TEST_HOST so
 * surplus_real_t is the kernel's Q32.32 int64 and every balance is exact.
 *
 * Ops over 8 accounts (4 entities x 2 capitals): TRANSFER, POST (raw
 * debit/credit on any of the three ledgers), ISSUE_VOUCHER (mint),
 * TRANSFER_VOUCHER, REDEEM_VOUCHER, HEALTH / EXPORT, with edge-biased raw
 * Q32.32 values (0, 1, -1, INT64_MAX, INT64_MIN, 2^62, ...).
 * Invariants after every op:
 *   T1  a TRANSFER that succeeds leaves sum(conventional_balance) and the
 *       per-ledger sums unchanged and moves exactly `amount` from -> to on
 *       the financial ledger; one that fails changes nothing
 *   T2  a voucher redemption adds exactly merit_value (the mint) to the
 *       redeeming account and to sum(conventional_balance); a voucher is
 *       redeemed at most once
 *   T3  total_equity == total_assets - total_liabilities == sum of the
 *       conventional balances (the trial balance closes)
 *   T4  no signed overflow anywhere (UBSan aborts on one) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "triple_ledger.h"
#include "fuzz_in.h"

#define NACC 8

static triple_ledger_t g_tl;
static unsigned long long g_ops;

typedef struct {
    __int128 conv[NACC], bal[NACC][LEDGER_MAX];
    uint32_t nent[NACC];
} snap_t;

static void snap(snap_t *s)
{
    memset(s, 0, sizeof *s); /* padding too: snapshots are compared with memcmp */
    for (uint32_t i = 0; i < NACC; i++) {
        s->conv[i] = g_tl.accounts[i].conventional_balance;
        for (uint32_t l = 0; l < LEDGER_MAX; l++) s->bal[i][l] = g_tl.accounts[i].balance[l];
        s->nent[i] = g_tl.accounts[i].num_entries;
    }
}

static __int128 conv_sum(const snap_t *s)
{
    __int128 t = 0;
    for (uint32_t i = 0; i < NACC; i++) t += s->conv[i];
    return t;
}

static void trial_balance(void)
{
    __int128 sum = 0;
    for (uint32_t i = 0; i < NACC; i++) sum += g_tl.accounts[i].conventional_balance;
    if ((__int128) g_tl.total_assets - g_tl.total_liabilities != sum) abort(); /* T3 */
    if (g_tl.total_equity != g_tl.total_assets - g_tl.total_liabilities) abort();
    conventional_report_t rep;
    triple_ledger_export_conventional(&g_tl, &rep);
    if (rep.trial_balance != g_tl.total_assets - g_tl.total_liabilities) abort();
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size > 8192) return 0;
    fz_in in;
    fz_init(&in, data, size);
    triple_ledger_init(&g_tl);
    for (uint32_t i = 0; i < NACC; i++)
        if (triple_ledger_create_account(&g_tl, 10u + i / 2u, (i & 1u) ? CAP_SOCIAL : CAP_FINANCIAL,
                                         "acct") != i)
            abort();
    uint64_t vouchers[32];
    uint32_t nv = 0;

    snap_t b, a;
    while (in.n) {
        uint8_t op = fz_u8(&in) % 6u;
        uint32_t x = fz_u8(&in) % (NACC + 1u), y = fz_u8(&in) % (NACC + 1u);
        surplus_real_t v = (surplus_real_t) fz_edge64(&in);
        surplus_real_t ell = (surplus_real_t) (fz_u8(&in) * (SR_ONE / 128));
        surplus_real_t phi = (surplus_real_t) (int8_t) fz_u8(&in) * (SR_ONE / 64);
        g_ops++;
        snap(&b);
        switch (op) {
        case 0: {
            int32_t r = triple_ledger_transfer(&g_tl, x, y, CAP_FINANCIAL, v, ell, phi, "t");
            snap(&a);
            if (r != 0) {
                if (memcmp(&a, &b, sizeof a) != 0) abort(); /* T1: refused but changed */
            } else {
                if (x >= NACC || y >= NACC || x == y || v <= 0) abort();
                if (conv_sum(&a) != conv_sum(&b)) abort();
                for (uint32_t l = 0; l < LEDGER_MAX; l++) {
                    __int128 sb = 0, sa = 0;
                    for (uint32_t i = 0; i < NACC; i++) {
                        sb += b.bal[i][l];
                        sa += a.bal[i][l];
                    }
                    if (sa != sb) abort();
                }
                if (a.conv[x] != b.conv[x] - v || a.conv[y] != b.conv[y] + v) abort();
            }
            break;
        }
        case 1: {
            ledger_type_t l = (ledger_type_t) (fz_u8(&in) % (LEDGER_MAX + 1));
            surplus_real_t d = (surplus_real_t) fz_edge64(&in);
            int32_t r = triple_ledger_post(&g_tl, x, l, v, ell, phi, d, v, y, "p");
            snap(&a);
            if (r != 0) {
                if (memcmp(&a, &b, sizeof a) != 0) abort();
            } else if (l == LEDGER_FINANCIAL) {
                if (a.conv[x] != b.conv[x] + ((__int128) d - v)) abort();
            } else if (a.conv[x] != b.conv[x]) {
                abort(); /* provenance / externality never touch the books */
            }
            break;
        }
        case 2: {
            uint64_t id =
                triple_ledger_issue_voucher(&g_tl, 10u + x / 2u, 10u + y / 2u,
                                            (y & 1u) ? CAP_SOCIAL : CAP_FINANCIAL, v, ell, "v");
            if (id && nv < 32) vouchers[nv++] = id;
            snap(&a);
            if (memcmp(&a, &b, sizeof a) != 0) abort(); /* issuing moves no money yet */
            break;
        }
        case 3:
            if (nv) (void) triple_ledger_transfer_voucher(&g_tl, vouchers[x % nv], 10u + y / 2u);
            snap(&a);
            if (memcmp(&a, &b, sizeof a) != 0) abort();
            break;
        case 4: {
            if (!nv) break;
            uint64_t id = vouchers[y % nv];
            surplus_real_t merit = 0;
            bool was = false;
            for (uint32_t i = 0; i < NACC; i++)
                for (uint32_t j = 0; j < g_tl.accounts[i].num_vouchers; j++)
                    if (g_tl.accounts[i].vouchers[j].voucher_id == id) {
                        merit = g_tl.accounts[i].vouchers[j].merit_value;
                        was = g_tl.accounts[i].vouchers[j].redeemed;
                    }
            int32_t r = triple_ledger_redeem_voucher(&g_tl, id, x);
            snap(&a);
            if (r != 0) {
                if (memcmp(&a, &b, sizeof a) != 0) abort();
            } else {
                if (was) abort(); /* T2: redeemed twice */
                if (a.conv[x] != b.conv[x] + merit) abort();
                if (conv_sum(&a) != conv_sum(&b) + merit) abort(); /* minted */
            }
            break;
        }
        default:
            triple_ledger_update_health(&g_tl);
            (void) triple_ledger_verify_coverage(&g_tl, x);
            (void) triple_ledger_account_coverage(&g_tl, y);
            break;
        }
        trial_balance();
    }
    return 0;
}

__attribute__((destructor)) static void report(void)
{
    if (getenv("FUZZ_PROP_STATS")) fprintf(stderr, "fuzz_econ_triple: %llu ops checked\n", g_ops);
}
