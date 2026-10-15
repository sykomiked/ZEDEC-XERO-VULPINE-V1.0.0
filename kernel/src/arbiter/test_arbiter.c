/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* test_arbiter.c — both ends of the arbiter mailbox over one shared window:
 * header checks, ring bounds and backpressure, a hostile peer, attestation,
 * key release, tamper zeroisation, kill switches, the seal, telemetry, the
 * interlock, entropy and framebuffer flips. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "arb_dev.h"
#include "arb_host.h"

static int checks, failures;
#define CHECK(c)                                                                                   \
    do {                                                                                           \
        checks++;                                                                                  \
        if (!(c)) {                                                                                \
            failures++;                                                                            \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);                                    \
        }                                                                                          \
    } while (0)

#define WIN_FB_W 64u
#define WIN_FB_H 16u
#define WIN_SIZE (ARB_OFF_FB + 3u * WIN_FB_W * 4u * WIN_FB_H)

static uint8_t g_win[WIN_SIZE] __attribute__((aligned(4096)));
static uint8_t g_meas[ARB_MEAS_LEN];
static uint32_t g_entropy_ctr;

static void entropy(void *ctx, uint8_t *out, uint32_t n)
{
    (void) ctx;
    for (uint32_t i = 0; i < n; i++) out[i] = (uint8_t) (0xA5 ^ (g_entropy_ctr++ * 37u));
}

static int g_shown, g_cleared;
static arb_seal_view_t g_view;
static void seal_show(void *ctx, const arb_seal_view_t *v)
{
    (void) ctx;
    if (v) {
        g_shown++;
        g_view = *v;
    } else {
        g_cleared++;
    }
}

static void setup(arb_dev_t *d, arb_host_t *h, uint32_t cap)
{
    memset(g_win, 0xEE, sizeof g_win);
    for (uint32_t i = 0; i < ARB_MEAS_LEN; i++) g_meas[i] = (uint8_t) (i * 7 + 1);
    g_entropy_ctr = 0;
    g_shown = g_cleared = 0;
    CHECK(arb_dev_init(d, g_win, WIN_SIZE, cap, 3, WIN_FB_W, WIN_FB_H, g_meas, entropy, 0) == 0);
    d->seal_show = seal_show;
    CHECK(arb_host_init(h, g_win, WIN_SIZE) == 0);
}

/* one round trip: arbiter handles everything queued, host drains replies */
static void pump(arb_dev_t *d, arb_host_t *h)
{
    arb_dev_poll(d);
    arb_host_poll(h);
}

static void attest(arb_dev_t *d, arb_host_t *h)
{
    CHECK(arb_host_hello(h) == 0);
    CHECK(h->state == ARB_H_HELLO);
    pump(d, h);
    CHECK(h->state == ARB_H_READY);
    CHECK(memcmp(h->nonce, d->nonce, ARB_NONCE_LEN) == 0);
    CHECK(arb_host_attest(h, g_meas) == 0);
    pump(d, h);
    CHECK(h->state == ARB_H_ATTESTED);
    CHECK(h->attest_verdict == ARB_ATTEST_OK);
    CHECK(d->attested == 1);
}

static void test_packing(void)
{
    uint8_t p[16];
    memset(p, 0, sizeof p);
    arb_put32(p, 1, 0x11223344u);
    CHECK(p[1] == 0x44 && p[2] == 0x33 && p[3] == 0x22 && p[4] == 0x11 && p[0] == 0 && p[5] == 0);
    CHECK(arb_get32(p, 1) == 0x11223344u);
    arb_put64(p, 8, 0x0102030405060708ULL);
    CHECK(p[8] == 0x08 && p[15] == 0x01);
    CHECK(arb_get64(p, 8) == 0x0102030405060708ULL);
    uint8_t a[5] = {1, 2, 3, 4, 5}, b[5] = {1, 2, 3, 4, 5};
    CHECK(arb_ct_equal(a, b, 5));
    b[4] = 6;
    CHECK(!arb_ct_equal(a, b, 5));
    CHECK(arb_ct_equal(a, b, 4));
    b[0] = 9;
    CHECK(!arb_ct_equal(a, b, 1));
    CHECK(arb_ct_equal(a, b, 0));
    uint8_t c[5];
    arb_copy(c, a, 5);
    CHECK(memcmp(c, a, 5) == 0);
    arb_zero(c, 3);
    CHECK(c[0] == 0 && c[2] == 0 && c[3] == 4);
}

static void test_header(void)
{
    arb_dev_t d;
    arb_host_t h;
    memset(g_win, 0, sizeof g_win);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_MAGIC);
    CHECK(arb_header_check(g_win, ARB_WINDOW_MIN - 1) == ARB_E_SIZE);
    CHECK(arb_header_write(g_win, WIN_SIZE, 3, 0, 0, 0) == ARB_E_RING);
    CHECK(arb_header_write(g_win, WIN_SIZE, 1, 0, 0, 0) == ARB_E_RING);
    CHECK(arb_header_write(g_win, WIN_SIZE, 128, 0, 0, 0) == ARB_E_RING);
    CHECK(arb_header_write(g_win, WIN_SIZE, 64, 0, 0, 0) == 0);
    CHECK(arb_header_check(g_win, WIN_SIZE) == 0);
    CHECK(arb_header_write(g_win, ARB_WINDOW_MIN - 4, 8, 0, 0, 0) == ARB_E_SIZE);
    /* framebuffers must fit */
    CHECK(arb_header_write(g_win, WIN_SIZE, 8, 4, WIN_FB_W, WIN_FB_H) == ARB_E_FB);
    CHECK(arb_header_write(g_win, WIN_SIZE, 8, 3, WIN_FB_W, WIN_FB_H + 1) == ARB_E_FB);
    CHECK(arb_header_write(g_win, WIN_SIZE, 8, 3, 0, WIN_FB_H) == ARB_E_FB);
    CHECK(arb_header_write(g_win, WIN_SIZE, 8, 3, WIN_FB_W, WIN_FB_H) == 0);
    CHECK(arb_header_check(g_win, WIN_SIZE) == 0);
    CHECK(arb_header_check(g_win, WIN_SIZE - 1) == ARB_E_FB);
    CHECK(arb_rd32(g_win, ARB_OFF_FB_STRIDE) == WIN_FB_W * 4);
    CHECK(arb_rd32(g_win, ARB_OFF_FB_OFFSET) == ARB_OFF_FB);
    CHECK(arb_rd64(g_win, ARB_OFF_MAGIC) == ARB_MAGIC);
    CHECK(memcmp(g_win, "ZXVARBMB", 8) == 0); /* readable in a hex dump of the BAR */
    /* a peer that writes nonsense is refused, field by field */
    arb_wr32(g_win, ARB_OFF_VERSION, 2);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_VERSION);
    arb_wr32(g_win, ARB_OFF_VERSION, ARB_VERSION);
    arb_wr32(g_win, ARB_OFF_RING_CAP, 48);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_RING);
    arb_wr32(g_win, ARB_OFF_RING_CAP, 8);
    arb_wr32(g_win, ARB_OFF_MSG_SIZE, 32);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_RING);
    arb_wr32(g_win, ARB_OFF_MSG_SIZE, ARB_MSG_SIZE);
    arb_wr32(g_win, ARB_OFF_FB_STRIDE, WIN_FB_W * 4 - 4);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_FB);
    arb_wr32(g_win, ARB_OFF_FB_STRIDE, WIN_FB_W * 4 + 2);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_FB);
    arb_wr32(g_win, ARB_OFF_FB_STRIDE, WIN_FB_W * 4);
    arb_wr32(g_win, ARB_OFF_FB_OFFSET, ARB_OFF_FB + 0x10);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_FB);
    arb_wr32(g_win, ARB_OFF_FB_OFFSET, 0x1000);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_FB);
    arb_wr32(g_win, ARB_OFF_FB_OFFSET, ARB_OFF_FB);
    arb_wr32(g_win, ARB_OFF_FB_COUNT, 4);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_FB);
    arb_wr32(g_win, ARB_OFF_FB_COUNT, 3);
    arb_wr32(g_win, ARB_OFF_FB_HEIGHT, 0x10000);
    CHECK(arb_header_check(g_win, WIN_SIZE) == ARB_E_FB);
    arb_wr32(g_win, ARB_OFF_FB_HEIGHT, WIN_FB_H);
    CHECK(arb_header_check(g_win, WIN_SIZE) == 0);
    /* the host refuses a window that fails */
    arb_wr32(g_win, ARB_OFF_MAGIC, 0);
    CHECK(arb_host_init(&h, g_win, WIN_SIZE) == ARB_E_MAGIC);
    CHECK(h.w == 0);
    CHECK(arb_host_hello(&h) == -2);
    CHECK(arb_host_entropy_request(&h, 8) == -2);
    CHECK(arb_host_state_request(&h) == -2);
    CHECK(arb_host_poll(&h) == 0);
    /* the arbiter side refuses a missing entropy source */
    CHECK(arb_dev_init(&d, g_win, WIN_SIZE, 8, 0, 0, 0, g_meas, 0, 0) != 0);
    /* init clears below the framebuffers */
    memset(g_win, 0xEE, sizeof g_win);
    CHECK(arb_dev_init(&d, g_win, WIN_SIZE, 8, 0, 0, 0, g_meas, entropy, 0) == 0);
    CHECK(arb_rd32(g_win, ARB_OFF_DB_H2A) == 0 && arb_rd32(g_win, ARB_OFF_DB_A2H) == 0);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_H2A) == 0);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_A2H) == 0);
    CHECK(g_win[ARB_OFF_RING_A2H + ARB_RING_SLOTS + 5] == 0);
    CHECK(arb_rd32(g_win, ARB_OFF_FB_COUNT) == 0 && arb_rd32(g_win, ARB_OFF_FB_OFFSET) == 0);
    CHECK(arb_header_check(g_win, WIN_SIZE) == 0);
}

static void test_ring(void)
{
    memset(g_win, 0, sizeof g_win);
    CHECK(arb_header_write(g_win, WIN_SIZE, 4, 0, 0, 0) == 0);
    arb_msg_t m, r;
    memset(&m, 0, sizeof m);
    m.type = 0x42;
    m.len = ARB_MSG_PAYLOAD + 1;
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 4, &m) == -2);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_H2A) == 0);
    CHECK(arb_ring_get(g_win, ARB_OFF_RING_H2A, 4, &r) == 0);
    /* fill to capacity, then backpressure */
    for (uint32_t i = 0; i < 4; i++) {
        m.len = (uint16_t) (i * 13 + 3);
        m.seq = 100 + i;
        for (uint32_t b = 0; b < ARB_MSG_PAYLOAD; b++) m.payload[b] = (uint8_t) (i + b);
        CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 4, &m) == 0);
    }
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_H2A) == 4);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 4, &m) == -1);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_H2A) == 4);
    for (uint32_t i = 0; i < 4; i++) {
        memset(&r, 0xCC, sizeof r);
        CHECK(arb_ring_get(g_win, ARB_OFF_RING_H2A, 4, &r) == 1);
        CHECK(r.type == 0x42 && r.seq == 100 + i && r.len == i * 13 + 3);
        int ok = 1;
        for (uint32_t b = 0; b < ARB_MSG_PAYLOAD; b++)
            if (r.payload[b] != (b < r.len ? (uint8_t) (i + b) : 0)) ok = 0;
        CHECK(ok);
    }
    CHECK(arb_ring_get(g_win, ARB_OFF_RING_H2A, 4, &r) == 0);
    /* consumed slots are wiped */
    int clean = 1;
    for (uint32_t b = 0; b < 4 * ARB_MSG_SIZE; b++)
        if (g_win[ARB_OFF_RING_H2A + ARB_RING_SLOTS + b]) clean = 0;
    CHECK(clean);
    /* wrap the free-running counters past 2^32 */
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_HEAD, 0xFFFFFFFEu);
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_TAIL, 0xFFFFFFFEu);
    for (uint32_t i = 0; i < 4; i++) {
        m.len = 4;
        m.seq = i;
        CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 4, &m) == 0);
    }
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 4, &m) == -1);
    CHECK(arb_rd32(g_win, ARB_OFF_RING_A2H + ARB_RING_HEAD) == 2u);
    for (uint32_t i = 0; i < 4; i++) {
        CHECK(arb_ring_get(g_win, ARB_OFF_RING_A2H, 4, &r) == 1);
        CHECK(r.seq == i);
    }
    CHECK(arb_ring_get(g_win, ARB_OFF_RING_A2H, 4, &r) == 0);
    /* a hostile producer: fill above capacity, nothing consumed */
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_HEAD, 10);
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_TAIL, 5);
    CHECK(arb_ring_get(g_win, ARB_OFF_RING_A2H, 4, &r) == -1);
    CHECK(arb_rd32(g_win, ARB_OFF_RING_A2H + ARB_RING_TAIL) == 5);
    /* a length over the payload */
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_HEAD, 6);
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_SLOTS + (5 & 3) * ARB_MSG_SIZE,
             1u | ((ARB_MSG_PAYLOAD + 1) << 16));
    CHECK(arb_ring_get(g_win, ARB_OFF_RING_A2H, 4, &r) == -1);
    CHECK(arb_rd32(g_win, ARB_OFF_RING_A2H + ARB_RING_TAIL) == 5);
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_SLOTS + (5 & 3) * ARB_MSG_SIZE,
             1u | (ARB_MSG_PAYLOAD << 16));
    CHECK(arb_ring_get(g_win, ARB_OFF_RING_A2H, 4, &r) == 1);
    CHECK(r.type == 1 && r.len == ARB_MSG_PAYLOAD);
}

static void test_attest(void)
{
    arb_dev_t d;
    arb_host_t h;
    setup(&d, &h, 8);
    CHECK(h.state == ARB_H_DOWN);
    /* nothing before HELLO */
    CHECK(arb_host_attest(&h, g_meas) == -2);
    CHECK(arb_host_key_request(&h, 0) == -2);
    attest(&d, &h);
    CHECK(arb_rd32(g_win, ARB_OFF_DB_H2A) == 2);
    CHECK(arb_rd32(g_win, ARB_OFF_DB_A2H) == 2);
    /* the nonce answers once: a second ATTEST with it is stale */
    h.state = ARB_H_READY;
    CHECK(arb_host_attest(&h, g_meas) == 0);
    pump(&d, &h);
    CHECK(h.attest_verdict == ARB_ATTEST_STALE);
    CHECK(h.state == ARB_H_REFUSED);
    CHECK(d.attested == 0);
    /* a new HELLO brings a new nonce */
    uint8_t old[ARB_NONCE_LEN];
    memcpy(old, h.nonce, ARB_NONCE_LEN);
    attest(&d, &h);
    CHECK(memcmp(old, h.nonce, ARB_NONCE_LEN) != 0);

    /* wrong measurement */
    setup(&d, &h, 8);
    CHECK(arb_host_hello(&h) == 0);
    pump(&d, &h);
    uint8_t bad[ARB_MEAS_LEN];
    memcpy(bad, g_meas, sizeof bad);
    bad[31] ^= 1;
    CHECK(arb_host_attest(&h, bad) == 0);
    pump(&d, &h);
    CHECK(h.attest_verdict == ARB_ATTEST_MISMATCH && h.state == ARB_H_REFUSED);
    CHECK(d.attested == 0);
    bad[31] ^= 1;
    bad[0] ^= 0x80;
    CHECK(arb_host_hello(&h) == 0);
    pump(&d, &h);
    CHECK(arb_host_attest(&h, bad) == 0);
    pump(&d, &h);
    CHECK(h.attest_verdict == ARB_ATTEST_MISMATCH);

    /* wrong nonce */
    CHECK(arb_host_hello(&h) == 0);
    pump(&d, &h);
    h.nonce[3] ^= 1;
    CHECK(arb_host_attest(&h, g_meas) == 0);
    pump(&d, &h);
    CHECK(h.attest_verdict == ARB_ATTEST_STALE);

    /* a HELLO drops an existing attestation */
    attest(&d, &h);
    CHECK(arb_host_hello(&h) == 0);
    arb_dev_poll(&d);
    CHECK(d.attested == 0);
    arb_host_poll(&h);
    CHECK(h.state == ARB_H_READY);
    CHECK(arb_host_key_request(&h, 0) == -2);

    /* an ATTEST message too short is stale, not a crash */
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_ATTEST;
    m.len = 10;
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    h.state = ARB_H_ATTESTING;
    pump(&d, &h);
    CHECK(h.attest_verdict == ARB_ATTEST_STALE);
    /* an unrequested ATTEST_RESULT is ignored by the host */
    h.state = ARB_H_READY;
    uint8_t p[4];
    arb_put32(p, 0, ARB_ATTEST_OK);
    memset(&m, 0, sizeof m);
    m.type = ARB_M_ATTEST_RESULT;
    m.len = 4;
    memcpy(m.payload, p, 4);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(h.state == ARB_H_READY);
}

static void test_keys(void)
{
    arb_dev_t d;
    arb_host_t h;
    uint8_t k0[ARB_KEY_LEN], k2[ARB_KEY_LEN], out[ARB_KEY_LEN];
    for (uint32_t i = 0; i < ARB_KEY_LEN; i++) {
        k0[i] = (uint8_t) (0x10 + i);
        k2[i] = (uint8_t) (0xF0 - i);
    }
    setup(&d, &h, 8);
    CHECK(arb_dev_set_key(&d, 0, k0) == 0);
    CHECK(arb_dev_set_key(&d, 2, k2) == 0);
    CHECK(arb_dev_set_key(&d, ARB_KEY_SLOTS, k2) == -1);
    /* before attestation the arbiter refuses even a forged request */
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_KEY_REQ;
    m.len = 4;
    arb_put32(m.payload, 0, 0);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    arb_dev_poll(&d);
    h.state = ARB_H_ATTESTED; /* even a host that thinks it is attested */
    arb_host_poll(&h);
    CHECK(h.key_status[0] == ARB_KEY_NOT_ATTESTED);
    memset(out, 0, sizeof out);
    CHECK(arb_host_take_key(&h, 0, out) == ARB_KEY_NOT_ATTESTED);
    CHECK(out[0] == 0);
    h.state = ARB_H_DOWN;

    attest(&d, &h);
    CHECK(arb_host_key_request(&h, 0) == 0);
    CHECK(arb_host_take_key(&h, 0, out) == 0); /* not answered yet */
    pump(&d, &h);
    memset(out, 0, sizeof out);
    CHECK(arb_host_take_key(&h, 0, out) == ARB_KEY_OK);
    CHECK(memcmp(out, k0, ARB_KEY_LEN) == 0);
    /* taking wipes the host's copy */
    uint8_t zero[ARB_KEY_LEN] = {0};
    CHECK(memcmp(h.key[0], zero, ARB_KEY_LEN) == 0);
    /* the window slot that carried it is wiped too */
    int clean = 1;
    for (uint32_t b = 0; b < 8 * ARB_MSG_SIZE; b++)
        if (g_win[ARB_OFF_RING_A2H + ARB_RING_SLOTS + b]) clean = 0;
    CHECK(clean);
    CHECK(arb_host_key_request(&h, 2) == 0);
    CHECK(arb_host_key_request(&h, 1) == 0);
    CHECK(arb_host_key_request(&h, ARB_HOST_KEYS) == -2);
    pump(&d, &h);
    CHECK(arb_host_take_key(&h, 2, out) == ARB_KEY_OK);
    CHECK(memcmp(out, k2, ARB_KEY_LEN) == 0);
    CHECK(arb_host_take_key(&h, 1, out) == ARB_KEY_NO_SLOT);
    CHECK(arb_host_take_key(&h, 9, out) == ARB_KEY_NO_SLOT);
    /* out-of-range slot from a forged request */
    memset(&m, 0, sizeof m);
    m.type = ARB_M_KEY_REQ;
    m.len = 4;
    arb_put32(m.payload, 0, 77);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    m.len = 0;
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    uint32_t before = h.received;
    pump(&d, &h);
    CHECK(h.received == before + 2); /* answered, and ignored as an unknown slot */

    /* a key in flight when the tamper lands is not kept */
    CHECK(arb_host_key_request(&h, 0) == 0);
    arb_dev_poll(&d); /* the key is in the A2H ring now */
    arb_dev_tamper(&d, ARB_TAMPER_MESH);
    arb_host_poll(&h); /* KEY first, then TAMPER */
    CHECK(h.tampered == 1);
    CHECK(memcmp(h.key[0], zero, ARB_KEY_LEN) == 0);
    memset(out, 0x55, sizeof out);
    CHECK(arb_host_take_key(&h, 0, out) == ARB_KEY_ZEROISED);
    CHECK(out[0] == 0x55);
}

static void test_tamper(void)
{
    arb_dev_t d;
    arb_host_t h;
    uint8_t k[ARB_KEY_LEN], out[ARB_KEY_LEN];
    memset(k, 0x3C, sizeof k);
    setup(&d, &h, 8);
    CHECK(arb_dev_set_key(&d, 1, k) == 0);
    attest(&d, &h);
    arb_dev_tamper(&d, ARB_TAMPER_PRESSURE);
    /* keys are gone on the arbiter before the host hears anything */
    int clear = 1;
    for (uint32_t s = 0; s < ARB_KEY_SLOTS; s++) {
        if (d.kek_present[s]) clear = 0;
        for (uint32_t i = 0; i < ARB_KEY_LEN; i++)
            if (d.kek[s][i]) clear = 0;
    }
    CHECK(clear);
    CHECK(d.tampered == 1 && d.attested == 0 && d.tamper_cause == ARB_TAMPER_PRESSURE);
    arb_host_poll(&h);
    CHECK(h.tampered == 1 && h.tamper_cause == ARB_TAMPER_PRESSURE);
    CHECK(h.state == ARB_H_REFUSED);
    CHECK(arb_host_key_request(&h, 1) == -2);
    CHECK(arb_host_seal_request(&h, 1, 100, "USD", 50, k) == -2);
    /* the latch keeps the first cause */
    arb_dev_tamper(&d, ARB_TAMPER_LIGHT);
    arb_host_poll(&h);
    CHECK(d.tamper_cause == ARB_TAMPER_PRESSURE && h.tamper_cause == ARB_TAMPER_PRESSURE);
    /* no new keys, no attestation, no keys even for a forged request */
    CHECK(arb_dev_set_key(&d, 1, k) == -1);
    CHECK(arb_host_hello(&h) == 0);
    pump(&d, &h);
    CHECK(h.state == ARB_H_READY && h.tampered == 1);
    CHECK(arb_host_attest(&h, g_meas) == 0);
    pump(&d, &h);
    CHECK(h.attest_verdict == ARB_ATTEST_TAMPERED && h.state == ARB_H_REFUSED);
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_KEY_REQ;
    m.len = 4;
    arb_put32(m.payload, 0, 1);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    pump(&d, &h);
    CHECK(h.key_status[1] == ARB_KEY_ZEROISED);
    CHECK(arb_host_take_key(&h, 1, out) == ARB_KEY_ZEROISED);
    /* a fresh host learns of the tamper from HELLO_ACK alone */
    arb_host_t h2;
    CHECK(arb_host_init(&h2, g_win, WIN_SIZE) == 0);
    CHECK(arb_host_hello(&h2) == 0);
    pump(&d, &h2);
    CHECK(h2.tampered == 1 && h2.tamper_cause == ARB_TAMPER_PRESSURE);
    /* and from a state request */
    arb_host_t h3;
    CHECK(arb_host_init(&h3, g_win, WIN_SIZE) == 0);
    CHECK(arb_host_state_request(&h3) == 0);
    pump(&d, &h3);
    CHECK(h3.tampered == 1);
}

static void test_kill(void)
{
    arb_dev_t d;
    arb_host_t h;
    setup(&d, &h, 8);
    CHECK(!arb_host_powered(&h, ARB_KILL_CELLULAR));
    arb_dev_kill(&d, ARB_KILL_CELLULAR | ARB_KILL_WIFI_BT | ARB_KILL_CAMERA | ARB_KILL_MIC,
                 ARB_KILL_CELLULAR | ARB_KILL_CAMERA | ARB_KILL_LORA);
    arb_host_poll(&h);
    CHECK(h.have_kill);
    CHECK(arb_host_powered(&h, ARB_KILL_CELLULAR));
    CHECK(arb_host_powered(&h, ARB_KILL_CAMERA));
    CHECK(arb_host_powered(&h, ARB_KILL_CELLULAR | ARB_KILL_CAMERA));
    CHECK(!arb_host_powered(&h, ARB_KILL_WIFI_BT));
    CHECK(!arb_host_powered(&h, ARB_KILL_MIC | ARB_KILL_CAMERA));
    CHECK(!arb_host_powered(&h, ARB_KILL_LORA)); /* on, but not a switch this device has */
    CHECK(!arb_host_powered(&h, 0));
    arb_dev_kill(&d, ARB_KILL_ALL, 0);
    arb_host_poll(&h);
    CHECK(!arb_host_powered(&h, ARB_KILL_CELLULAR));
    CHECK(h.kill_present == ARB_KILL_ALL && h.kill_on == 0);
    arb_dev_kill(&d, 0xFFFFFFFFu, 0xFFFFFFFFu);
    arb_host_poll(&h);
    CHECK(h.kill_present == ARB_KILL_ALL && h.kill_on == ARB_KILL_ALL);
    CHECK(arb_host_powered(&h, ARB_KILL_ALL));
    /* a hostile KILL_STATE can't claim switches outside the set */
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_KILL_STATE;
    m.len = 8;
    arb_put32(m.payload, 0, 0x81u);
    arb_put32(m.payload, 4, 0xFFu);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(h.kill_present == 0x01 && h.kill_on == 0x01);
    m.len = 4;
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(h.kill_present == 0x01);
    /* state request returns the switch positions */
    arb_dev_kill(&d, ARB_KILL_MIC, ARB_KILL_MIC);
    arb_host_poll(&h);
    h.kill_on = 0;
    CHECK(arb_host_state_request(&h) == 0);
    pump(&d, &h);
    CHECK(arb_host_powered(&h, ARB_KILL_MIC));
}

static void test_seal(void)
{
    arb_dev_t d;
    arb_host_t h;
    uint8_t payee[ARB_PAYEE_LEN];
    for (uint32_t i = 0; i < ARB_PAYEE_LEN; i++) payee[i] = (uint8_t) (i ^ 0x5A);
    setup(&d, &h, 8);
    arb_dev_tick(&d, 1000);
    /* refused before attestation, even forged */
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_SEAL_REQ;
    m.len = 20 + ARB_PAYEE_LEN;
    arb_put32(m.payload, 0, 7);
    arb_put32(m.payload, 16, 2000);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    pump(&d, &h);
    CHECK(h.have_seal && h.seal_id == 7 && h.seal_verdict == ARB_SEAL_REFUSED);
    CHECK(g_shown == 0);
    attest(&d, &h);
    /* approved */
    CHECK(arb_host_seal_request(&h, 11, 123456789012LL, "EUR", 1500, payee) == 0);
    CHECK(h.have_seal == 0);
    pump(&d, &h);
    CHECK(g_shown == 1 && d.seal_pending == 1);
    CHECK(g_view.id == 11 && g_view.amount == 123456789012LL && g_view.expiry == 1500);
    CHECK(memcmp(g_view.ccy, "EUR", 4) == 0);
    CHECK(memcmp(g_view.payee, payee, ARB_PAYEE_LEN) == 0);
    CHECK(h.have_seal == 0); /* nothing until the button */
    /* a second seal while one is showing is busy */
    CHECK(arb_host_seal_request(&h, 12, 5, "EUR", 1500, payee) == 0);
    pump(&d, &h);
    CHECK(h.have_seal && h.seal_id == 12 && h.seal_verdict == ARB_SEAL_BUSY);
    CHECK(g_view.id == 11);
    CHECK(arb_dev_seal_button(&d, 1) == 0);
    arb_host_poll(&h);
    CHECK(h.have_seal && h.seal_id == 11 && h.seal_verdict == ARB_SEAL_APPROVED);
    CHECK(memcmp(h.seal_payee, payee, ARB_PAYEE_LEN) == 0);
    CHECK(g_cleared == 1 && d.seal_pending == 0);
    CHECK(arb_dev_seal_button(&d, 1) == -1);
    /* denied */
    CHECK(arb_host_seal_request(&h, 13, -5, "USD", 1500, payee) == 0);
    pump(&d, &h);
    CHECK(g_view.amount == -5);
    CHECK(arb_dev_seal_button(&d, 0) == 0);
    arb_host_poll(&h);
    CHECK(h.seal_id == 13 && h.seal_verdict == ARB_SEAL_DENIED);
    /* already expired on arrival */
    CHECK(arb_host_seal_request(&h, 14, 1, "USD", 1000, payee) == 0);
    pump(&d, &h);
    CHECK(h.seal_id == 14 && h.seal_verdict == ARB_SEAL_EXPIRED);
    CHECK(d.seal_pending == 0);
    /* expires while showing */
    CHECK(arb_host_seal_request(&h, 15, 1, "USD", 1010, payee) == 0);
    pump(&d, &h);
    CHECK(d.seal_pending == 1);
    arb_dev_tick(&d, 1009);
    arb_host_poll(&h);
    CHECK(h.seal_id == 14);
    arb_dev_tick(&d, 1010);
    arb_host_poll(&h);
    CHECK(h.seal_id == 15 && h.seal_verdict == ARB_SEAL_EXPIRED && d.seal_pending == 0);
    /* a button pressed after expiry never approves */
    CHECK(arb_host_seal_request(&h, 16, 1, "USD", 1020, payee) == 0);
    pump(&d, &h);
    d.now = 1030; /* clock moved without a tick */
    CHECK(arb_dev_seal_button(&d, 1) == 0);
    arb_host_poll(&h);
    CHECK(h.seal_id == 16 && h.seal_verdict == ARB_SEAL_EXPIRED);
    /* expiry across the clock wrap */
    arb_dev_tick(&d, 0xFFFFFFF0u);
    CHECK(arb_host_seal_request(&h, 17, 1, "USD", 0x10u, payee) == 0);
    pump(&d, &h);
    CHECK(d.seal_pending == 1);
    arb_dev_tick(&d, 0x5u);
    CHECK(d.seal_pending == 1);
    arb_dev_tick(&d, 0x10u);
    arb_host_poll(&h);
    CHECK(h.seal_id == 17 && h.seal_verdict == ARB_SEAL_EXPIRED);
    /* a tamper cancels a showing seal */
    CHECK(arb_host_seal_request(&h, 18, 1, "USD", 0x100u, payee) == 0);
    pump(&d, &h);
    CHECK(d.seal_pending == 1);
    arb_dev_tamper(&d, ARB_TAMPER_SWITCH);
    arb_host_poll(&h);
    CHECK(h.seal_id == 18 && h.seal_verdict == ARB_SEAL_REFUSED && d.seal_pending == 0);
    /* a short SEAL_REQ is dropped */
    memset(&m, 0, sizeof m);
    m.type = ARB_M_SEAL_REQ;
    m.len = 20;
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    uint32_t got = h.received;
    pump(&d, &h);
    CHECK(h.received == got);
}

static void test_telemetry_interlock_entropy(void)
{
    arb_dev_t d;
    arb_host_t h;
    int32_t v = 0;
    setup(&d, &h, 8);
    CHECK(!arb_host_sensor(&h, ARB_S_T_CPU, &v));
    uint16_t s[ARB_TELEM_MAX + 1] = {ARB_S_PWR_CPU, ARB_S_T_CPU, ARB_S_PWR_PROJ, ARB_S_HUMIDITY,
                                     ARB_S_T_CELL,  99,          ARB_S_T_VAULT};
    int32_t val[ARB_TELEM_MAX + 1] = {65000, 87250, 0, 4512, -12500, 1, 3};
    CHECK(arb_dev_telemetry(&d, s, val, 0) == -1);
    CHECK(arb_dev_telemetry(&d, s, val, ARB_TELEM_MAX + 1) == -1);
    CHECK(arb_dev_telemetry(&d, s, val, ARB_TELEM_MAX) == 0);
    arb_host_poll(&h);
    CHECK(arb_host_sensor(&h, ARB_S_PWR_CPU, &v) && v == 65000);
    CHECK(arb_host_sensor(&h, ARB_S_T_CPU, &v) && v == 87250);
    CHECK(arb_host_sensor(&h, ARB_S_PWR_PROJ, &v) && v == 0);
    CHECK(arb_host_sensor(&h, ARB_S_HUMIDITY, &v) && v == 4512);
    CHECK(arb_host_sensor(&h, ARB_S_T_CELL, &v) && v == -12500);
    CHECK(!arb_host_sensor(&h, 99, &v));
    CHECK(!arb_host_sensor(&h, ARB_S_T_VAULT, &v));
    CHECK(!arb_host_sensor(&h, ARB_S_COUNT, &v));
    /* a newer reading replaces the old */
    val[1] = 91000;
    CHECK(arb_dev_telemetry(&d, s + 1, val + 1, 1) == 0);
    arb_host_poll(&h);
    CHECK(arb_host_sensor(&h, ARB_S_T_CPU, &v) && v == 91000);
    /* a TELEMETRY that claims more entries than it carries is dropped */
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_TELEMETRY;
    m.len = 12;
    arb_put32(m.payload, 0, 2);
    arb_put32(m.payload, 4, ARB_S_T_INTAKE);
    arb_put32(m.payload, 8, 5);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(!arb_host_sensor(&h, ARB_S_T_INTAKE, &v));

    /* interlock */
    arb_dev_interlock(&d, 0, ARB_HOLD_DEW);
    arb_host_poll(&h);
    CHECK(h.have_interlock && h.interlock_permitted == 0 && h.interlock_reason == ARB_HOLD_DEW);
    arb_dev_interlock(&d, 1, ARB_HOLD_DEW);
    arb_host_poll(&h);
    CHECK(h.interlock_permitted == 1 && h.interlock_reason == ARB_HOLD_NONE);
    arb_dev_interlock(&d, 0, ARB_HOLD_COLD_CELL);
    h.have_interlock = 0;
    CHECK(arb_host_state_request(&h) == 0);
    pump(&d, &h);
    CHECK(h.have_interlock && h.interlock_reason == ARB_HOLD_COLD_CELL);

    /* entropy */
    uint8_t e[64];
    CHECK(arb_host_entropy_request(&h, 0) == -2);
    CHECK(arb_host_entropy_request(&h, ARB_ENTROPY_MAX + 1) == -2);
    g_entropy_ctr = 0;
    CHECK(arb_host_entropy_request(&h, 20) == 0);
    pump(&d, &h);
    CHECK(h.entropy_n == 20);
    CHECK(arb_host_take_entropy(&h, e, 8) == 8);
    CHECK(e[0] == (uint8_t) 0xA5 && e[1] == (uint8_t) (0xA5 ^ 37));
    CHECK(h.entropy_n == 12);
    CHECK(arb_host_take_entropy(&h, e, 64) == 12);
    CHECK(e[0] == (uint8_t) (0xA5 ^ (8 * 37)));
    CHECK(h.entropy_n == 0 && h.entropy[0] == 0);
    CHECK(arb_host_take_entropy(&h, e, 64) == 0);
    /* the buffer saturates at ARB_ENTROPY_MAX */
    CHECK(arb_host_entropy_request(&h, 40) == 0);
    CHECK(arb_host_entropy_request(&h, 40) == 0);
    pump(&d, &h);
    CHECK(h.entropy_n == ARB_ENTROPY_MAX);
    CHECK(arb_host_take_entropy(&h, e, 64) == ARB_ENTROPY_MAX);
    /* an ENTROPY that claims more than it carries is dropped */
    memset(&m, 0, sizeof m);
    m.type = ARB_M_ENTROPY;
    m.len = 8;
    arb_put32(m.payload, 0, 10);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(h.entropy_n == 0);
}

static void test_fb_and_backpressure(void)
{
    arb_dev_t d;
    arb_host_t h;
    setup(&d, &h, 4);
    uint32_t w, ht, st;
    for (uint32_t i = 0; i < 3; i++) {
        volatile uint32_t *fb = arb_host_fb(&h, i, &w, &ht, &st);
        CHECK(fb != 0);
        CHECK(w == WIN_FB_W && ht == WIN_FB_H && st == WIN_FB_W);
        CHECK((volatile uint8_t *) fb == g_win + ARB_OFF_FB + i * WIN_FB_W * 4 * WIN_FB_H);
        fb[st * (ht - 1) + w - 1] = 0x00F2C230u; /* last pixel, inside the window */
    }
    CHECK(arb_host_fb(&h, 3, &w, &ht, &st) == 0);
    CHECK(arb_host_flip(&h, 3, 1) == -2);
    CHECK(arb_host_flip(&h, 2, 41) == 0);
    pump(&d, &h);
    CHECK(d.fb_head == 2 && d.fb_gen == 41);
    CHECK(h.fb_done_head == 2 && h.fb_done_gen == 41);
    /* the arbiter rewrites the geometry past the window: the host refuses it */
    arb_wr32(g_win, ARB_OFF_FB_HEIGHT, WIN_FB_H * 2);
    CHECK(arb_host_fb(&h, 0, &w, &ht, &st) == 0);
    arb_wr32(g_win, ARB_OFF_FB_HEIGHT, WIN_FB_H);
    CHECK(arb_host_fb(&h, 0, &w, &ht, &st) != 0);

    /* H2A backpressure: four requests fill a ring of four */
    for (uint32_t i = 0; i < 4; i++) CHECK(arb_host_entropy_request(&h, 1) == 0);
    CHECK(arb_host_entropy_request(&h, 1) == -1);
    CHECK(arb_host_state_request(&h) == -1);
    CHECK(arb_rd32(g_win, ARB_OFF_DB_H2A) == 5); /* the flip + four */
    /* the arbiter answers what fits in A2H and counts the rest */
    CHECK(arb_dev_poll(&d) == 4);
    CHECK(d.dropped == 0);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_A2H) == 4);
    arb_dev_kill(&d, ARB_KILL_MIC, ARB_KILL_MIC);
    CHECK(d.dropped == 1);
    uint16_t s = ARB_S_T_CPU;
    int32_t v = 1;
    CHECK(arb_dev_telemetry(&d, &s, &v, 1) == -1);
    CHECK(arb_host_poll(&h) == 4);
    CHECK(h.entropy_n == 4);

    /* a host that breaks H2A stops the arbiter reading it */
    arb_wr32(g_win, ARB_OFF_RING_H2A + ARB_RING_HEAD,
             arb_rd32(g_win, ARB_OFF_RING_H2A + ARB_RING_TAIL) + 9);
    CHECK(arb_dev_poll(&d) == 0);
    CHECK(d.broken == 1);
    CHECK(arb_dev_poll(&d) == 0);
    /* an arbiter that breaks A2H stops the host */
    arb_wr32(g_win, ARB_OFF_RING_A2H + ARB_RING_HEAD,
             arb_rd32(g_win, ARB_OFF_RING_A2H + ARB_RING_TAIL) + 9);
    CHECK(arb_host_poll(&h) == 0);
    CHECK(h.state == ARB_H_BROKEN);
    CHECK(arb_host_hello(&h) == -2);
    CHECK(arb_host_entropy_request(&h, 1) == -2);
    CHECK(arb_host_flip(&h, 0, 1) == -2);

    /* a HELLO_ACK of the wrong version breaks the session */
    setup(&d, &h, 8);
    arb_msg_t m;
    memset(&m, 0, sizeof m);
    m.type = ARB_M_HELLO_ACK;
    m.len = 32;
    arb_put32(m.payload, 0, ARB_VERSION + 1);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(h.state == ARB_H_BROKEN);
    /* unknown types are counted on the host and ignored on the arbiter */
    setup(&d, &h, 8);
    m.type = 0x7F;
    m.len = 0;
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_A2H, 8, &m) == 0);
    arb_host_poll(&h);
    CHECK(h.unknown == 1 && h.state == ARB_H_DOWN);
    CHECK(arb_ring_put(g_win, ARB_OFF_RING_H2A, 8, &m) == 0);
    CHECK(arb_dev_poll(&d) == 1);
    CHECK(arb_ring_fill(g_win, ARB_OFF_RING_A2H) == 0);
}

int main(void)
{
    test_packing();
    test_header();
    test_ring();
    test_attest();
    test_keys();
    test_tamper();
    test_kill();
    test_seal();
    test_telemetry_interlock_entropy();
    test_fb_and_backpressure();
    printf("%s: %d checks, %d failed\n", failures ? "FAILED" : "ok", checks, failures);
    return failures ? 1 : 0;
}
