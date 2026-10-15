/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arb_dev.c — the arbiter's end of the mailbox (arb_dev.h). */
#include "arb_dev.h"

static int send(arb_dev_t *d, uint16_t type, const uint8_t *p, uint16_t len)
{
    arb_msg_t m;
    m.type = type;
    m.len = len;
    m.seq = ++d->seq;
    arb_copy(m.payload, p, len);
    int r = arb_ring_put(d->w, ARB_OFF_RING_A2H, d->cap, &m);
    arb_zero((volatile uint8_t *) m.payload, ARB_MSG_PAYLOAD);
    if (r != 0) {
        d->dropped++;
        return -1;
    }
    arb_wr32(d->w, ARB_OFF_DB_A2H, arb_rd32(d->w, ARB_OFF_DB_A2H) + 1);
    return 0;
}

static void new_nonce(arb_dev_t *d)
{
    d->entropy(d->ctx, d->nonce, ARB_NONCE_LEN);
    d->nonce_live = 1;
}

static void send_hello_ack(arb_dev_t *d)
{
    uint8_t p[32];
    arb_put32(p, 0, ARB_VERSION);
    arb_copy(p + 4, d->nonce, ARB_NONCE_LEN);
    arb_put32(p, 20, d->kill_present);
    arb_put32(p, 24, d->kill_on);
    arb_put32(p, 28, d->tampered ? d->tamper_cause : 0);
    send(d, ARB_M_HELLO_ACK, p, 32);
}

static void send_kill(arb_dev_t *d)
{
    uint8_t p[8];
    arb_put32(p, 0, d->kill_present);
    arb_put32(p, 4, d->kill_on);
    send(d, ARB_M_KILL_STATE, p, 8);
}

static void send_interlock(arb_dev_t *d)
{
    uint8_t p[8];
    arb_put32(p, 0, d->interlock_permitted);
    arb_put32(p, 4, d->interlock_reason);
    send(d, ARB_M_INTERLOCK, p, 8);
}

static void send_tamper(arb_dev_t *d)
{
    uint8_t p[8];
    arb_put32(p, 0, d->tamper_cause);
    arb_put32(p, 4, 1);
    send(d, ARB_M_TAMPER, p, 8);
}

static void seal_answer(arb_dev_t *d, uint32_t id, uint32_t verdict, const uint8_t *payee)
{
    uint8_t p[40];
    arb_put32(p, 0, id);
    arb_put32(p, 4, verdict);
    if (payee)
        arb_copy(p + 8, payee, ARB_PAYEE_LEN);
    else
        arb_zero(p + 8, ARB_PAYEE_LEN);
    send(d, ARB_M_SEAL_RESULT, p, 40);
}

static void seal_clear(arb_dev_t *d)
{
    d->seal_pending = 0;
    if (d->seal_show) d->seal_show(d->ctx, 0);
}

int arb_dev_init(arb_dev_t *d, volatile uint8_t *w, uint64_t size, uint32_t ring_cap,
                 uint32_t fb_count, uint32_t fb_width, uint32_t fb_height,
                 const uint8_t measurement[ARB_MEAS_LEN],
                 void (*entropy)(void *, uint8_t *, uint32_t), void *ctx)
{
    if (!entropy) return ARB_E_SIZE;
    arb_zero((volatile uint8_t *) d, sizeof *d);
    int r = arb_header_write(w, size, ring_cap, fb_count, fb_width, fb_height);
    if (r != 0) return r;
    d->w = w;
    d->cap = ring_cap;
    d->entropy = entropy;
    d->ctx = ctx;
    arb_copy(d->measurement, measurement, ARB_MEAS_LEN);
    d->interlock_permitted = 1;
    return 0;
}

int arb_dev_set_key(arb_dev_t *d, uint32_t slot, const uint8_t key[ARB_KEY_LEN])
{
    if (slot >= ARB_KEY_SLOTS || d->tampered) return -1;
    arb_copy(d->kek[slot], key, ARB_KEY_LEN);
    d->kek_present[slot] = 1;
    return 0;
}

static void on_attest(arb_dev_t *d, const arb_msg_t *m)
{
    uint8_t p[4];
    uint32_t v;
    if (d->tampered)
        v = ARB_ATTEST_TAMPERED;
    else if (m->len < ARB_NONCE_LEN + ARB_MEAS_LEN || !d->nonce_live ||
             !arb_ct_equal(m->payload, d->nonce, ARB_NONCE_LEN))
        v = ARB_ATTEST_STALE;
    else if (!arb_ct_equal(m->payload + ARB_NONCE_LEN, d->measurement, ARB_MEAS_LEN))
        v = ARB_ATTEST_MISMATCH;
    else
        v = ARB_ATTEST_OK;
    /* A nonce answers once, right or wrong: a retry needs a new HELLO. */
    d->nonce_live = 0;
    d->attested = v == ARB_ATTEST_OK;
    arb_put32(p, 0, v);
    send(d, ARB_M_ATTEST_RESULT, p, 4);
}

static void on_key_req(arb_dev_t *d, const arb_msg_t *m)
{
    uint8_t p[8 + ARB_KEY_LEN];
    uint32_t slot = m->len >= 4 ? arb_get32(m->payload, 0) : ARB_KEY_SLOTS;
    uint32_t st;
    if (d->tampered)
        st = ARB_KEY_ZEROISED;
    else if (!d->attested)
        st = ARB_KEY_NOT_ATTESTED;
    else if (slot >= ARB_KEY_SLOTS || !d->kek_present[slot])
        st = ARB_KEY_NO_SLOT;
    else
        st = ARB_KEY_OK;
    arb_put32(p, 0, slot);
    arb_put32(p, 4, st);
    if (st == ARB_KEY_OK)
        arb_copy(p + 8, d->kek[slot], ARB_KEY_LEN);
    else
        arb_zero(p + 8, ARB_KEY_LEN);
    send(d, ARB_M_KEY, p, sizeof p);
    arb_zero(p, sizeof p);
}

static void on_seal_req(arb_dev_t *d, const arb_msg_t *m)
{
    if (m->len < 20 + ARB_PAYEE_LEN) return;
    uint32_t id = arb_get32(m->payload, 0);
    const uint8_t *payee = m->payload + 20;
    if (d->tampered || !d->attested) {
        seal_answer(d, id, ARB_SEAL_REFUSED, 0);
        return;
    }
    if (d->seal_pending) {
        seal_answer(d, id, ARB_SEAL_BUSY, 0);
        return;
    }
    uint32_t expiry = arb_get32(m->payload, 16);
    if ((int32_t) (expiry - d->now) <= 0) {
        seal_answer(d, id, ARB_SEAL_EXPIRED, 0);
        return;
    }
    d->seal.id = id;
    d->seal.amount = (int64_t) arb_get64(m->payload, 4);
    arb_copy((uint8_t *) d->seal.ccy, m->payload + 12, 4);
    d->seal.expiry = expiry;
    arb_copy(d->seal.payee, payee, ARB_PAYEE_LEN);
    d->seal_pending = 1;
    if (d->seal_show) d->seal_show(d->ctx, &d->seal);
}

static void on_entropy(arb_dev_t *d, const arb_msg_t *m)
{
    uint8_t p[4 + ARB_ENTROPY_MAX];
    uint32_t n = m->len >= 4 ? arb_get32(m->payload, 0) : 0;
    if (n > ARB_ENTROPY_MAX) n = ARB_ENTROPY_MAX;
    arb_put32(p, 0, n);
    if (n) d->entropy(d->ctx, p + 4, n);
    send(d, ARB_M_ENTROPY, p, (uint16_t) (4 + n));
    arb_zero(p, sizeof p);
}

static void on_flip(arb_dev_t *d, const arb_msg_t *m)
{
    if (m->len < 8) return;
    uint32_t head = arb_get32(m->payload, 0);
    if (head >= arb_rd32(d->w, ARB_OFF_FB_COUNT)) return;
    d->fb_head = head;
    d->fb_gen = arb_get32(m->payload, 4);
    uint8_t p[8];
    arb_put32(p, 0, d->fb_head);
    arb_put32(p, 4, d->fb_gen);
    send(d, ARB_M_FB_DONE, p, 8);
}

uint32_t arb_dev_poll(arb_dev_t *d)
{
    uint32_t n = 0;
    arb_msg_t m;
    while (!d->broken) {
        int r = arb_ring_get(d->w, ARB_OFF_RING_H2A, d->cap, &m);
        if (r == 0) break;
        if (r < 0) {
            d->broken = 1;
            break;
        }
        n++;
        switch (m.type) {
        case ARB_M_HELLO:
            /* A new session: fresh nonce, attestation starts over. */
            d->attested = 0;
            new_nonce(d);
            send_hello_ack(d);
            break;
        case ARB_M_ATTEST:
            on_attest(d, &m);
            break;
        case ARB_M_KEY_REQ:
            on_key_req(d, &m);
            break;
        case ARB_M_SEAL_REQ:
            on_seal_req(d, &m);
            break;
        case ARB_M_ENTROPY_REQ:
            on_entropy(d, &m);
            break;
        case ARB_M_FB_FLIP:
            on_flip(d, &m);
            break;
        case ARB_M_STATE_REQ:
            send_kill(d);
            send_interlock(d);
            if (d->tampered) send_tamper(d);
            break;
        default:
            break; /* unknown types are ignored: a newer host's request */
        }
        arb_zero((volatile uint8_t *) m.payload, ARB_MSG_PAYLOAD);
    }
    return n;
}

void arb_dev_tick(arb_dev_t *d, uint32_t now)
{
    d->now = now;
    if (d->seal_pending && (int32_t) (d->seal.expiry - now) <= 0) {
        uint32_t id = d->seal.id;
        seal_clear(d);
        seal_answer(d, id, ARB_SEAL_EXPIRED, 0);
    }
}

void arb_dev_tamper(arb_dev_t *d, uint32_t cause)
{
    /* Keys first (OEM_INSTITUTIONAL 6.3), then state, then the message. */
    arb_zero(&d->kek[0][0], sizeof d->kek);
    arb_zero(d->kek_present, sizeof d->kek_present);
    if (!d->tampered) d->tamper_cause = cause;
    d->tampered = 1;
    d->attested = 0;
    if (d->seal_pending) {
        uint32_t id = d->seal.id;
        seal_clear(d);
        seal_answer(d, id, ARB_SEAL_REFUSED, 0);
    }
    send_tamper(d);
}

void arb_dev_kill(arb_dev_t *d, uint32_t present, uint32_t on)
{
    d->kill_present = present & ARB_KILL_ALL;
    d->kill_on = on & d->kill_present;
    send_kill(d);
}

int arb_dev_seal_button(arb_dev_t *d, int approve)
{
    if (!d->seal_pending) return -1;
    if ((int32_t) (d->seal.expiry - d->now) <= 0) {
        arb_dev_tick(d, d->now);
        return 0;
    }
    uint8_t payee[ARB_PAYEE_LEN];
    uint32_t id = d->seal.id;
    arb_copy(payee, d->seal.payee, ARB_PAYEE_LEN);
    seal_clear(d);
    seal_answer(d, id, approve ? ARB_SEAL_APPROVED : ARB_SEAL_DENIED, payee);
    return 0;
}

int arb_dev_telemetry(arb_dev_t *d, const uint16_t *sensor, const int32_t *value, uint32_t n)
{
    if (n == 0 || n > ARB_TELEM_MAX) return -1;
    uint8_t p[4 + ARB_TELEM_MAX * 8];
    arb_put32(p, 0, n);
    for (uint32_t i = 0; i < n; i++) {
        arb_put32(p, 4 + i * 8, sensor[i]);
        arb_put32(p, 8 + i * 8, (uint32_t) value[i]);
    }
    return send(d, ARB_M_TELEMETRY, p, (uint16_t) (4 + n * 8));
}

void arb_dev_interlock(arb_dev_t *d, uint32_t permitted, uint32_t reason)
{
    d->interlock_permitted = permitted ? 1 : 0;
    d->interlock_reason = permitted ? ARB_HOLD_NONE : reason;
    send_interlock(d);
}
