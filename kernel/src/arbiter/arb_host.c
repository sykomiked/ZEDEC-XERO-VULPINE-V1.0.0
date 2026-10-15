/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arb_host.c — ZXV's end of the arbiter mailbox (arb_host.h). */
#include "arb_host.h"

static int send(arb_host_t *h, uint16_t type, const uint8_t *p, uint16_t len)
{
    arb_msg_t m;
    m.type = type;
    m.len = len;
    m.seq = ++h->seq;
    arb_copy(m.payload, p, len);
    int r = arb_ring_put(h->w, ARB_OFF_RING_H2A, h->cap, &m);
    if (r != 0) return -1;
    arb_wr32(h->w, ARB_OFF_DB_H2A, arb_rd32(h->w, ARB_OFF_DB_H2A) + 1);
    return 0;
}

static void wipe_keys(arb_host_t *h)
{
    arb_zero(&h->key[0][0], sizeof h->key);
}

int arb_host_init(arb_host_t *h, volatile uint8_t *w, uint64_t size)
{
    arb_zero((volatile uint8_t *) h, sizeof *h);
    int r = arb_header_check(w, size);
    if (r != 0) return r;
    h->w = w;
    h->size = size;
    h->cap = arb_rd32(w, ARB_OFF_RING_CAP);
    h->state = ARB_H_DOWN; /* HELLO is the first request */
    return 0;
}

int arb_host_hello(arb_host_t *h)
{
    if (!h->w || h->state == ARB_H_BROKEN) return -2;
    uint8_t p[4];
    arb_put32(p, 0, ARB_VERSION);
    if (send(h, ARB_M_HELLO, p, 4) != 0) return -1;
    h->state = ARB_H_HELLO;
    return 0;
}

int arb_host_attest(arb_host_t *h, const uint8_t measurement[ARB_MEAS_LEN])
{
    if (h->state != ARB_H_READY) return -2;
    uint8_t p[ARB_NONCE_LEN + ARB_MEAS_LEN];
    arb_copy(p, h->nonce, ARB_NONCE_LEN);
    arb_copy(p + ARB_NONCE_LEN, measurement, ARB_MEAS_LEN);
    if (send(h, ARB_M_ATTEST, p, sizeof p) != 0) return -1;
    h->state = ARB_H_ATTESTING;
    return 0;
}

int arb_host_key_request(arb_host_t *h, uint32_t slot)
{
    if (h->state != ARB_H_ATTESTED || h->tampered || slot >= ARB_HOST_KEYS) return -2;
    uint8_t p[4];
    arb_put32(p, 0, slot);
    if (send(h, ARB_M_KEY_REQ, p, 4) != 0) return -1;
    h->key_status[slot] = 0;
    return 0;
}

int arb_host_seal_request(arb_host_t *h, uint32_t id, int64_t amount, const char ccy[4],
                          uint32_t expiry, const uint8_t payee[ARB_PAYEE_LEN])
{
    if (h->state != ARB_H_ATTESTED || h->tampered) return -2;
    uint8_t p[20 + ARB_PAYEE_LEN];
    arb_put32(p, 0, id);
    arb_put64(p, 4, (uint64_t) amount);
    arb_copy(p + 12, (const uint8_t *) ccy, 4);
    arb_put32(p, 16, expiry);
    arb_copy(p + 20, payee, ARB_PAYEE_LEN);
    if (send(h, ARB_M_SEAL_REQ, p, sizeof p) != 0) return -1;
    h->have_seal = 0;
    return 0;
}

int arb_host_entropy_request(arb_host_t *h, uint32_t n)
{
    if (!h->w || h->state == ARB_H_BROKEN || n == 0 || n > ARB_ENTROPY_MAX) return -2;
    uint8_t p[4];
    arb_put32(p, 0, n);
    return send(h, ARB_M_ENTROPY_REQ, p, 4) == 0 ? 0 : -1;
}

int arb_host_state_request(arb_host_t *h)
{
    if (!h->w || h->state == ARB_H_BROKEN) return -2;
    return send(h, ARB_M_STATE_REQ, 0, 0) == 0 ? 0 : -1;
}

int arb_host_flip(arb_host_t *h, uint32_t head, uint32_t generation)
{
    if (!h->w || h->state == ARB_H_BROKEN) return -2;
    if (head >= arb_rd32(h->w, ARB_OFF_FB_COUNT)) return -2;
    uint8_t p[8];
    arb_put32(p, 0, head);
    arb_put32(p, 4, generation);
    return send(h, ARB_M_FB_FLIP, p, 8) == 0 ? 0 : -1;
}

static void on_tamper(arb_host_t *h, const arb_msg_t *m)
{
    wipe_keys(h);
    for (uint32_t i = 0; i < ARB_HOST_KEYS; i++)
        if (h->key_status[i] == ARB_KEY_OK) h->key_status[i] = ARB_KEY_ZEROISED;
    if (!h->tampered) h->tamper_cause = m->len >= 4 ? arb_get32(m->payload, 0) : 0;
    h->tampered = 1;
    if (h->state == ARB_H_ATTESTED) h->state = ARB_H_REFUSED;
}

static void handle(arb_host_t *h, const arb_msg_t *m)
{
    const uint8_t *p = m->payload;
    switch (m->type) {
    case ARB_M_HELLO_ACK:
        if (m->len < 32 || arb_get32(p, 0) != ARB_VERSION) {
            h->state = ARB_H_BROKEN;
            return;
        }
        arb_copy(h->nonce, p + 4, ARB_NONCE_LEN);
        h->kill_present = arb_get32(p, 20) & ARB_KILL_ALL;
        h->kill_on = arb_get32(p, 24) & h->kill_present;
        h->have_kill = 1;
        if (arb_get32(p, 28) != 0) {
            arb_msg_t t = *m;
            arb_put32(t.payload, 0, arb_get32(p, 28));
            on_tamper(h, &t);
        }
        if (h->state == ARB_H_HELLO) h->state = ARB_H_READY;
        break;
    case ARB_M_ATTEST_RESULT:
        if (m->len < 4 || h->state != ARB_H_ATTESTING) return;
        h->attest_verdict = arb_get32(p, 0);
        h->state =
            h->attest_verdict == ARB_ATTEST_OK && !h->tampered ? ARB_H_ATTESTED : ARB_H_REFUSED;
        break;
    case ARB_M_KEY: {
        if (m->len < 8 + ARB_KEY_LEN) return;
        uint32_t slot = arb_get32(p, 0), st = arb_get32(p, 4);
        if (slot >= ARB_HOST_KEYS) return;
        if (st == ARB_KEY_OK && !h->tampered && h->state == ARB_H_ATTESTED)
            arb_copy(h->key[slot], p + 8, ARB_KEY_LEN);
        else if (st == ARB_KEY_OK)
            st = ARB_KEY_ZEROISED; /* a key that arrives after a tamper is not kept */
        h->key_status[slot] = st;
        break;
    }
    case ARB_M_SEAL_RESULT:
        if (m->len < 8 + ARB_PAYEE_LEN) return;
        h->seal_id = arb_get32(p, 0);
        h->seal_verdict = arb_get32(p, 4);
        arb_copy(h->seal_payee, p + 8, ARB_PAYEE_LEN);
        h->have_seal = 1;
        break;
    case ARB_M_ENTROPY: {
        if (m->len < 4) return;
        uint32_t n = arb_get32(p, 0);
        if (n > ARB_ENTROPY_MAX || n > (uint32_t) m->len - 4u) return;
        uint32_t room = ARB_ENTROPY_MAX - h->entropy_n;
        if (n > room) n = room;
        arb_copy(h->entropy + h->entropy_n, p + 4, n);
        h->entropy_n += n;
        break;
    }
    case ARB_M_FB_DONE:
        if (m->len < 8) return;
        h->fb_done_head = arb_get32(p, 0);
        h->fb_done_gen = arb_get32(p, 4);
        break;
    case ARB_M_KILL_STATE:
        if (m->len < 8) return;
        h->kill_present = arb_get32(p, 0) & ARB_KILL_ALL;
        h->kill_on = arb_get32(p, 4) & h->kill_present;
        h->have_kill = 1;
        break;
    case ARB_M_TAMPER:
        on_tamper(h, m);
        break;
    case ARB_M_TELEMETRY: {
        if (m->len < 4) return;
        uint32_t n = arb_get32(p, 0);
        if (n == 0 || n > ARB_TELEM_MAX || 4 + n * 8 > m->len) return;
        for (uint32_t i = 0; i < n; i++) {
            uint32_t s = arb_get32(p, 4 + i * 8) & 0xFFFFu;
            if (s >= ARB_S_COUNT) continue;
            h->sensor[s] = (int32_t) arb_get32(p, 8 + i * 8);
            h->sensor_seen |= 1u << s;
        }
        break;
    }
    case ARB_M_INTERLOCK:
        if (m->len < 8) return;
        h->interlock_permitted = arb_get32(p, 0) ? 1 : 0;
        h->interlock_reason = arb_get32(p, 4);
        h->have_interlock = 1;
        break;
    default:
        h->unknown++;
        break;
    }
}

uint32_t arb_host_poll(arb_host_t *h)
{
    uint32_t n = 0;
    arb_msg_t m;
    while (h->w && h->state != ARB_H_BROKEN) {
        int r = arb_ring_get(h->w, ARB_OFF_RING_A2H, h->cap, &m);
        if (r == 0) break;
        if (r < 0) {
            h->state = ARB_H_BROKEN;
            break;
        }
        n++;
        h->received++;
        handle(h, &m);
        arb_zero((volatile uint8_t *) m.payload, ARB_MSG_PAYLOAD);
    }
    return n;
}

uint32_t arb_host_take_key(arb_host_t *h, uint32_t slot, uint8_t out[ARB_KEY_LEN])
{
    if (slot >= ARB_HOST_KEYS) return ARB_KEY_NO_SLOT;
    uint32_t st = h->key_status[slot];
    if (st == ARB_KEY_OK && !h->tampered) arb_copy(out, h->key[slot], ARB_KEY_LEN);
    arb_zero(h->key[slot], ARB_KEY_LEN);
    if (st == ARB_KEY_OK && h->tampered) st = ARB_KEY_ZEROISED;
    return st;
}

uint32_t arb_host_take_entropy(arb_host_t *h, uint8_t *out, uint32_t max)
{
    uint32_t n = h->entropy_n < max ? h->entropy_n : max;
    arb_copy(out, h->entropy, n);
    /* keep any remainder at the front */
    for (uint32_t i = n; i < h->entropy_n; i++) h->entropy[i - n] = h->entropy[i];
    h->entropy_n -= n;
    arb_zero(h->entropy + h->entropy_n, ARB_ENTROPY_MAX - h->entropy_n);
    return n;
}

int arb_host_powered(const arb_host_t *h, uint32_t mask)
{
    return h->have_kill && mask != 0 && (h->kill_present & mask) == mask &&
           (h->kill_on & mask) == mask;
}

int arb_host_sensor(const arb_host_t *h, uint32_t sensor, int32_t *v)
{
    if (sensor >= ARB_S_COUNT || !(h->sensor_seen & (1u << sensor))) return 0;
    *v = h->sensor[sensor];
    return 1;
}

volatile uint32_t *arb_host_fb(const arb_host_t *h, uint32_t i, uint32_t *width, uint32_t *height,
                               uint32_t *stride_px)
{
    /* The header is re-checked on every lookup: the window is the arbiter's,
     * and a geometry it changed after init must not walk past the BAR. */
    if (!h->w || arb_header_check(h->w, h->size) != 0) return 0;
    if (i >= arb_rd32(h->w, ARB_OFF_FB_COUNT)) return 0;
    uint32_t st = arb_rd32(h->w, ARB_OFF_FB_STRIDE), ht = arb_rd32(h->w, ARB_OFF_FB_HEIGHT);
    *width = arb_rd32(h->w, ARB_OFF_FB_WIDTH);
    *height = ht;
    *stride_px = st / 4;
    uint64_t off = (uint64_t) arb_rd32(h->w, ARB_OFF_FB_OFFSET) + (uint64_t) i * st * ht;
    return (volatile uint32_t *) (h->w + off);
}
