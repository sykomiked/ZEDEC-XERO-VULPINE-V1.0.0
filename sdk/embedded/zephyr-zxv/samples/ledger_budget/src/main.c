/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* main.c — the ZXV Zephyr sample: an exact ledger posting, a rational
 * budget with a periodic refill (the "budget tick"), and a phase clock
 * driven from k_uptime, with every check printed.
 *
 * ADVISORY ONLY: a real controller acts only through its own certified
 * safety system and with human approval. This program moves numbers. */
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "zxe.h"
#include "zxp.h"

static int checks, failures;
#define CHECK(c, what)                                         \
    do {                                                       \
        checks++;                                              \
        if (!(c)) {                                            \
            failures++;                                        \
            printk("FAIL line %d: %s\n", __LINE__, what);      \
        }                                                      \
    } while (0)
#define OK(call) CHECK((call) == ZXV_OK, #call)

static zxe_account accounts[4];
static zxe_refkey refs[64];

int main(void)
{
    zxv_ctx *ctx = NULL;
    zxv_cap *cap = NULL;
    zxv_rmag *rm = NULL;
    zxv_phase *ph = NULL;
    zxe_ledger L;
    zxe_clock clk;
    uint32_t budget, issuer, meter, grid;
    uint64_t id, tick0 = 0, tick1 = 0;
    int64_t eq;

    if (!zxp_ready()) {
        /* fail closed: no entropy source, no ZXV */
        CHECK(zxv_ctx_create(&ctx) == ZXV_E_INTERNAL, "closed without entropy");
        printk("zxv sample: ZXV closed (no entropy source configured)\n");
        printk("zxv sample: %d checks, %d failures\n", checks, failures);
        return 0;
    }
    OK(zxv_ctx_create(&ctx));
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));

    /* ledger */
    OK(zxe_ledger_init(&L, ctx, accounts, 4, refs, 64));
    OK(zxe_ledger_ccy_enable(&L, cap, "EUR", 2));
    OK(zxe_ledger_open_account(&L, cap, "issuer", "EUR", ZXV_ACCT_ISSUER, &issuer));
    OK(zxe_ledger_open_account(&L, cap, "meter", "EUR", ZXV_ACCT_HOLDER, &meter));
    OK(zxe_ledger_open_account(&L, cap, "grid", "EUR", ZXV_ACCT_HOLDER, &grid));
    OK(zxe_ledger_post(&L, cap, issuer, meter, 100000, "EUR", "mint-1", &id));
    OK(zxe_ledger_post(&L, cap, meter, grid, 1234, "EUR", "kwh-0001", &id));
    CHECK(zxe_ledger_post(&L, cap, meter, grid, 1234, "EUR", "kwh-0001", &id) == ZXV_E_DUPLICATE,
          "replay refused");
    CHECK(zxe_ledger_post(&L, cap, grid, meter, 5000, "EUR", "kwh-0002", &id) == ZXV_E_FUNDS,
          "no debt");
    OK(zxe_ledger_balance(&L, cap, meter, NULL, NULL, &eq));
    CHECK(eq == 98766, "meter equity");
    OK(zxe_ledger_check(&L, cap));

    /* budget: 5/2 units per period, exact */
    OK(zxv_rmag_create_cap(cap, &rm));
    OK(zxv_rmag_budget(cap, rm, "energy", ZXV_RES_ENERGY, 5, 2, 1, &budget));
    OK(zxv_rmag_consume(cap, rm, budget, 7, 3));
    CHECK(zxv_rmag_consume(cap, rm, budget, 1, 3) == ZXV_E_BUDGET, "7/3 + 1/3 > 5/2");

    /* phase clock (~22 KB of heap: refused, not crashed into, on a
     * 64 KB part) and the budget tick, three 100 ms periods */
    int32_t prc = zxv_phase_create_cap(cap, &ph);
    CHECK(prc == ZXV_OK || prc == ZXV_E_NOMEM, "phase clock created or refused for RAM");
    if (prc == ZXV_E_NOMEM) {
        ph = NULL;
        printk("zxv sample: phase clock refused (ZXV_E_NOMEM): CONFIG_ZXV_HEAP_SIZE=%d\n",
               CONFIG_ZXV_HEAP_SIZE);
    } else {
        OK(zxe_clock_init(&clk, ZXV_PHASE_CYCLE));
        OK(zxe_clock_pump(&clk, cap, ph, NULL, &tick0, NULL));
    }
    for (int i = 0; i < 3; i++) {
        k_sleep(K_MSEC(100));
        int64_t un = 0, ud = 1;
        OK(zxv_rmag_status(rm, budget, NULL, NULL, &un, &ud, NULL));
        if (un > 0) OK(zxv_rmag_release(cap, rm, budget, un, ud)); /* refill */
        if (ph) OK(zxe_clock_pump(&clk, cap, ph, NULL, &tick1, NULL));
    }
    if (ph) CHECK(tick1 > tick0, "phase clock advanced");
    OK(zxv_rmag_consume(cap, rm, budget, 5, 2)); /* a full period's budget again */

    zxp_heap_stats_t hs;
    zxp_heap_stats(&hs);
    printk("zxv sample: phase tick %u, ZXV heap live %u bytes, peak %u\n", (unsigned) tick1,
           (unsigned) hs.live_bytes, (unsigned) hs.peak_bytes);

    zxe_ledger_destroy(&L);
    if (ph) zxv_phase_destroy(ph);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    printk("zxv sample: %d checks, %d failures\n", checks, failures);
    return 0;
}
