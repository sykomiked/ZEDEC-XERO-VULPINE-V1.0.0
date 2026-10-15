/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_arbmbox.c — arbmbox.c against a fake arbiter: a PCI function whose BAR
 * 0 is the mailbox window and whose tick runs the arbiter's own end
 * (src/arbiter/arb_dev.c), so the bind talks to the real protocol code. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "hwfake.h"
#include "../../arbiter/arb_dev.h"
#include "../../arbiter/arb_host.h"

#define WIN_POW2 0x20000u /* BAR 0: a power of two holding the header and 3 panels */

typedef struct farb {
    arb_dev_t dev;
    int awake;            /* 0: firmware still starting, never answers */
    uint32_t start_after; /* answer only after this many microseconds */
} farb_t;

static void fentropy(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (i * 29 + 3);
}

static void farb_tick(fake_dev_t *d, uint64_t now)
{
    farb_t *a = (farb_t *) d->state;
    if (a->awake && now >= a->start_after) arb_dev_poll(&a->dev);
}

static fake_dev_t *add_arbiter(fake_plat_t *fp, uint32_t bdf, farb_t *a, uint32_t fbs)
{
    fake_dev_t *d = fake_dev_new(fp, bdf, 0x11AA, 0x5A01, 0x08, 0x80, 0x5A);
    uint8_t *w = fake_dev_bar(d, 0, WIN_POW2);
    uint8_t meas[ARB_MEAS_LEN] = {0};
    CHECK(arb_dev_init(&a->dev, w, WIN_POW2, 8, fbs, 64, 32, meas, fentropy, 0) == 0);
    d->tick = farb_tick;
    d->state = a;
    return d;
}

static void test_online(void)
{
    fake_plat_t fp;
    hw_registry_t reg;
    farb_t a;
    memset(&a, 0, sizeof a);
    memset(&reg, 0, sizeof reg);
    hw_arbiter_reset();
    fake_plat_init(&fp);
    add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 3);
    a.awake = 1;
    arb_dev_kill(&a.dev, ARB_KILL_CELLULAR | ARB_KILL_WIFI_BT, ARB_KILL_WIFI_BT);
    arb_dev_interlock(&a.dev, 0, ARB_HOLD_DEW);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.functions == 1 && st.bound == 1 && st.failed == 0 && st.held == 0);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] arbiter 00:05.0 11aa:5a01"));
    CHECK(fake_log_has(&fp, "mailbox v1, ring 8, 3 panels 64x32"));
    CHECK(fake_log_has(&fp, "switches 04/05"));
    CHECK(fake_log_has(&fp, "start held"));
    CHECK(!fake_log_has(&fp, "TAMPER"));
    arb_host_t *h = hw_arbiter();
    CHECK(h != 0);
    CHECK(h->state == ARB_H_READY);
    CHECK(arb_host_powered(h, ARB_KILL_WIFI_BT));
    CHECK(!arb_host_powered(h, ARB_KILL_CELLULAR));
    /* the bound session goes on working: attest and fetch entropy */
    CHECK(arb_host_attest(h, a.dev.measurement) == 0);
    CHECK(arb_host_entropy_request(h, 16) == 0);
    fp.plat.delay_us(fp.plat.ctx, 10);
    arb_host_poll(h);
    CHECK(h->state == ARB_H_ATTESTED);
    CHECK(h->entropy_n == 16);
    /* command register: memory decode and bus master on */
    CHECK((fp.devs[0]->cfg[1] & 0x6) == 0x6);
    fake_plat_free(&fp);
}

static void test_tamper_reported(void)
{
    fake_plat_t fp;
    hw_registry_t reg;
    farb_t a;
    memset(&a, 0, sizeof a);
    memset(&reg, 0, sizeof reg);
    hw_arbiter_reset();
    fake_plat_init(&fp);
    add_arbiter(&fp, HW_BDF(0, 6, 0), &a, 0);
    a.awake = 1;
    arb_dev_tamper(&a.dev, ARB_TAMPER_LIGHT);
    hw_probe_all(&fp.plat, &reg);
    CHECK(fake_log_has(&fp, "[DRIVER ONLINE] arbiter"));
    CHECK(fake_log_has(&fp, "TAMPER LATCHED"));
    CHECK(!fake_log_has(&fp, "panels"));
    CHECK(hw_arbiter()->tampered == 1);
    fake_plat_free(&fp);
}

static void test_held_and_failed(void)
{
    fake_plat_t fp;
    hw_registry_t reg;
    farb_t a, b;
    memset(&a, 0, sizeof a);
    memset(&b, 0, sizeof b);
    memset(&reg, 0, sizeof reg);
    hw_arbiter_reset();
    fake_plat_init(&fp);
    /* silent firmware: HELD, session kept */
    add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 0);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.held == 1 && st.bound == 0);
    CHECK(fake_log_has(&fp, "[DRIVER HELD] arbiter 00:05.0"));
    CHECK(fake_log_has(&fp, "no answer to HELLO yet"));
    CHECK(hw_arbiter() != 0 && hw_arbiter()->state == ARB_H_HELLO);
    /* it answers later */
    a.awake = 1;
    fp.plat.delay_us(fp.plat.ctx, 1);
    arb_host_poll(hw_arbiter());
    CHECK(hw_arbiter()->state == ARB_H_READY);
    fake_plat_free(&fp);

    /* slow firmware inside the budget: ONLINE */
    hw_arbiter_reset();
    memset(&a, 0, sizeof a);
    fake_plat_init(&fp);
    add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 0);
    a.awake = 1;
    a.start_after = 50000;
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.bound == 1);
    fake_plat_free(&fp);

    /* too slow: HELD */
    hw_arbiter_reset();
    memset(&a, 0, sizeof a);
    fake_plat_init(&fp);
    add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 0);
    a.awake = 1;
    a.start_after = 150000;
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.held == 1);
    fake_plat_free(&fp);

    /* a second arbiter is a contradiction; the first stays */
    hw_arbiter_reset();
    memset(&a, 0, sizeof a);
    fake_plat_init(&fp);
    add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 0);
    add_arbiter(&fp, HW_BDF(0, 7, 0), &b, 0);
    a.awake = b.awake = 1;
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.bound == 1 && st.held == 1);
    CHECK(fake_log_has(&fp, "[DRIVER HELD] arbiter 00:07.0"));
    CHECK(fake_log_has(&fp, "contradiction: a second arbiter"));
    CHECK(hw_arbiter()->w == fp.devs[0]->bar_mem[0]);
    fake_plat_free(&fp);

    /* class match without the header: FAILED, not adopted */
    hw_arbiter_reset();
    fake_plat_init(&fp);
    fake_dev_t *d = fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x1022, 0x1234, 0x08, 0x80, 0x5A);
    fake_dev_bar(d, 0, WIN_POW2);
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.failed == 1);
    CHECK(fake_log_has(&fp, "no ZXVARBMB header in BAR 0"));
    CHECK(hw_arbiter() == 0);
    fake_plat_free(&fp);

    /* a wrong version */
    hw_arbiter_reset();
    memset(&a, 0, sizeof a);
    fake_plat_init(&fp);
    d = add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 0);
    arb_wr32(d->bar_mem[0], ARB_OFF_VERSION, 9);
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.failed == 1 && fake_log_has(&fp, "unknown mailbox version"));
    CHECK(hw_arbiter() == 0);
    fake_plat_free(&fp);

    /* a BAR too small, and no BAR at all */
    hw_arbiter_reset();
    fake_plat_init(&fp);
    d = fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x1022, 0x1234, 0x08, 0x80, 0x5A);
    fake_dev_bar(d, 0, 0x2000);
    fake_dev_new(&fp, HW_BDF(0, 6, 0), 0x1022, 0x1235, 0x08, 0x80, 0x5A);
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.failed == 2);
    CHECK(fake_log_has(&fp, "not a memory window of at least 12 KiB"));
    fake_plat_free(&fp);

    /* an I/O BAR */
    hw_arbiter_reset();
    fake_plat_init(&fp);
    d = fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x1022, 0x1234, 0x08, 0x80, 0x5A);
    fake_dev_bar_at(d, 0, 0xC000, 0x4000, FAKE_BAR_IO);
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.failed == 1);
    fake_plat_free(&fp);

    /* other system peripherals (progif 0) are not claimed at all */
    hw_arbiter_reset();
    fake_plat_init(&fp);
    d = fake_dev_new(&fp, HW_BDF(0, 5, 0), 0x1022, 0x1234, 0x08, 0x80, 0x00);
    fake_dev_bar(d, 0, WIN_POW2);
    st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.unclaimed == 1 && st.failed == 0 && st.bound == 0);
    fake_plat_free(&fp);
}

static void test_broken_reply_ring(void)
{
    fake_plat_t fp;
    hw_registry_t reg;
    farb_t a;
    memset(&a, 0, sizeof a);
    memset(&reg, 0, sizeof reg);
    hw_arbiter_reset();
    fake_plat_init(&fp);
    fake_dev_t *d = add_arbiter(&fp, HW_BDF(0, 5, 0), &a, 0);
    /* the arbiter's reply ring claims more than its capacity */
    arb_wr32(d->bar_mem[0], ARB_OFF_RING_A2H + ARB_RING_HEAD, 40);
    hw_probe_stats_t st = hw_probe_all(&fp.plat, &reg);
    CHECK(st.failed == 1);
    CHECK(fake_log_has(&fp, "the arbiter broke its reply ring"));
    fake_plat_free(&fp);
}

int main(void)
{
    test_online();
    test_tamper_reported();
    test_held_and_failed();
    test_broken_reply_ring();
    printf("%s: %d checks, %d failed\n", fk_failures ? "FAILED" : "ok", fk_checks, fk_failures);
    return fk_failures ? 1 : 0;
}
