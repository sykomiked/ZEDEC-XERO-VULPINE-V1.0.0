/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_target.c — the MCU subset on an emulated Cortex-M (QEMU mps2-an385
 * / mps2-an386, `make qemu-test`): bare-metal port, newlib, 32-bit
 * arithmetic, real heap sizes. Output through semihosting.
 *
 * QEMU's MPS2 boards have no TRNG, so this test installs a DETERMINISTIC,
 * INSECURE generator. It exists to exercise the code paths in an emulator
 * and must never be copied into firmware. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "zxe.h"
#include "zxp.h"
#include "zxp_baremetal.h"

static int g_checks, g_fail;
#define CHECK(c, ...)                                   \
    do {                                                \
        g_checks++;                                     \
        if (!(c)) {                                     \
            g_fail++;                                   \
            printf("FAIL line %d: ", __LINE__);         \
            printf(__VA_ARGS__);                        \
            printf("\n");                               \
        }                                               \
    } while (0)
#define OK(call) CHECK((call) == ZXV_OK, "%s", #call)

static uint64_t g_x = 0x243F6A8885A308D3ull;
static int insecure_emulator_entropy(void *u, uint8_t *b, size_t n)
{
    (void) u;
    for (size_t i = 0; i < n; i++) {
        g_x ^= g_x << 13;
        g_x ^= g_x >> 7;
        g_x ^= g_x << 17;
        b[i] = (uint8_t) (g_x >> 32);
    }
    return 0;
}

static uint64_t g_fake_us;
static uint64_t fake_clock(void)
{
    return g_fake_us;
}

static size_t live(void)
{
    zxp_heap_stats_t s;
    zxp_heap_stats(&s);
    return s.live_bytes;
}

static zxe_account accounts[4];
static zxe_refkey refs[64];

int main(void)
{
    zxv_ctx *ctx = NULL;
    int64_t n, d;
    printf("target: %u-bit pointers, int64 alignment %u\n", (unsigned) (8 * sizeof(void *)),
           (unsigned) _Alignof(int64_t));
    /* fail closed before the port is up */
    CHECK(zxv_ctx_create(&ctx) == ZXV_E_INTERNAL, "closed before zxp_init");
    zxp_baremetal_set_clock(fake_clock);
    CHECK(zxp_init() == ZXP_E_NO_ENTROPY, "no entropy source");
    CHECK(zxp_set_entropy(insecure_emulator_entropy, NULL) == ZXP_OK, "set entropy");
    CHECK(zxp_init() == ZXP_OK, "init");

    /* exact rationals with 64-bit division on a 32-bit core */
    OK(zxv_rat_add(1, 3, 1, 6, &n, &d));
    CHECK(n == 1 && d == 2, "1/3+1/6");
    OK(zxv_rat_mul(9007199254740991LL, 3, 2, 9007199254740991LL, &n, &d));
    CHECK(n == 2 && d == 3, "big cancel");
    OK(zxv_rat_parse("-0.000123", &n, &d));
    CHECK(n == -123 && d == 1000000, "parse");
    char buf[40];
    size_t len;
    OK(zxv_rat_format(-7, 3, buf, sizeof buf, &len));
    CHECK(strcmp(buf, "-7/3") == 0, "format");
    int32_t t;
    OK(zxv_lp_and(ZXV_LP_TRUE, ZXV_LP_BOTH, &t));
    CHECK(t == ZXV_LP_BOTH, "lp and");
    uint8_t tag;
    int32_t act;
    OK(zxv_hex_pack(ZXV_HEX_BASE_BOTH, ZXV_HEX_POL_ENTROPIC, ZXV_HEX_PHASE_EMERGING, &tag));
    OK(zxv_hex_action(tag, &act));
    CHECK(act == ZXV_HEX_ACT_ESCALATE, "BOTH/entropic escalates");
    uint8_t h[32];
    OK(zxv_sha3_256((const uint8_t *) "abc", 3, h));
    CHECK(h[0] == 0x3a && h[31] == 0x32, "sha3 abc");

    /* handles and their heap on this target */
    zxv_cap *cap;
    zxv_rmag *rm;
    zxv_phase *ph;
    zxv_hexlog *log;
    size_t b0 = live();
    OK(zxv_ctx_create(&ctx));
    size_t s_ctx = live() - b0;
    OK(zxv_cap_create(ctx, 1, NULL, ZXV_CAP_ALL, &cap));
    size_t s_cap = live() - b0 - s_ctx, m = live();
    OK(zxv_rmag_create_cap(cap, &rm));
    size_t s_rmag = live() - m;
    m = live();
    OK(zxv_phase_create_cap(cap, &ph));
    size_t s_phase = live() - m;
    m = live();
    OK(zxv_hexlog_create(cap, 16, &log));
    size_t s_log = live() - m;
    printf("heap per handle (bytes): ctx %u, cap %u, rmag %u, phase %u, hexlog(16) %u\n",
           (unsigned) s_ctx, (unsigned) s_cap, (unsigned) s_rmag, (unsigned) s_phase, (unsigned) s_log);

    uint32_t b;
    OK(zxv_rmag_budget(cap, rm, "energy", ZXV_RES_ENERGY, 5, 2, 1, &b));
    OK(zxv_rmag_consume(cap, rm, b, 7, 3));
    CHECK(zxv_rmag_consume(cap, rm, b, 1, 3) == ZXV_E_BUDGET, "7/3 + 1/3 > 5/2");
    zxe_clock clk;
    uint64_t added = 0, total = 0;
    OK(zxe_clock_init(&clk, ZXV_PHASE_CYCLE));
    OK(zxe_clock_pump(&clk, cap, ph, NULL, NULL, NULL));
    int pumps_ok = 1;
    for (int i = 0; i < 1000; i++) {
        g_fake_us += 1000u + (uint64_t) (i % 7);
        pumps_ok &= zxe_clock_pump(&clk, cap, ph, &added, NULL, NULL) == ZXV_OK;
        total += added;
    }
    CHECK(pumps_ok, "1000 pumps");
    CHECK(total == g_fake_us * ZXV_PHASE_CYCLE / 1000000u, "clock exact (%u)", (unsigned) total);

    zxe_ledger L;
    uint32_t a0, a1;
    uint64_t id;
    int64_t eq;
    OK(zxe_ledger_init(&L, ctx, accounts, 4, refs, 64));
    OK(zxe_ledger_ccy_enable(&L, cap, "EUR", 2));
    OK(zxe_ledger_open_account(&L, cap, "issuer", "EUR", ZXV_ACCT_ISSUER, &a0));
    OK(zxe_ledger_open_account(&L, cap, "meter", "EUR", ZXV_ACCT_HOLDER, &a1));
    OK(zxe_ledger_post(&L, cap, a0, a1, 9007199254740991LL, "EUR", "max", &id));
    CHECK(zxe_ledger_post(&L, cap, a0, a1, 1, "EUR", "over", &id) == ZXV_E_RANGE, "2^53 rail");
    OK(zxe_ledger_balance(&L, cap, a1, NULL, NULL, &eq));
    CHECK(eq == 9007199254740991LL, "exact 2^53-1");
    OK(zxe_ledger_check(&L, cap));

    /* libzxv's own ledger: one kernel book does not fit this RAM */
    uint32_t acct;
    OK(zxv_ccy_enable_with_minor_cap(cap, "EUR", 2));
    zxp_set_heap_limit(live() + 512u * 1024u);
    CHECK(zxv_ledger_open_account_cap(cap, "a", "EUR", ZXV_ACCT_ISSUER, &acct) == ZXV_E_NOMEM,
          "the ~24 MB book is refused, not crashed into");
    zxp_set_heap_limit(0);

    zxe_ledger_destroy(&L);
    zxv_hexlog_destroy(log);
    zxv_phase_destroy(ph);
    zxv_rmag_destroy(rm);
    zxv_cap_destroy(cap);
    zxv_ctx_destroy(ctx);
    CHECK(live() == b0, "all heap returned");
    zxp_heap_stats_t hs;
    zxp_heap_stats(&hs);
    printf("heap peak %u bytes\n", (unsigned) hs.peak_bytes);
    printf("test_target: %d checks, %d failures\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
