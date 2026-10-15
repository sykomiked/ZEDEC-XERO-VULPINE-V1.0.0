/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arb_host.h — ZXV's end of the arbiter mailbox.
 *
 * Every call is non-blocking: requests go into the H2A ring (with the
 * doorbell), and arb_host_poll() drains the A2H ring into the state below.
 * The caller decides how long to wait (the PCI driver in hwdrv/arbmbox.c
 * polls through the platform's delay).
 *
 * Session: HELLO -> (HELLO_ACK: the arbiter's nonce) -> ATTEST -> attested.
 * A TAMPER message latches `tampered` and wipes every key this side holds,
 * before anything else is processed; nothing on the host can clear it.
 */
#ifndef ZXV_ARB_HOST_H
#define ZXV_ARB_HOST_H

#include "arb_proto.h"

#define ARB_HOST_KEYS 4u

typedef enum {
    ARB_H_DOWN = 0,  /* no session yet (or the window failed its check: w == 0) */
    ARB_H_HELLO,     /* HELLO sent, waiting for the nonce */
    ARB_H_READY,     /* nonce held, not attested */
    ARB_H_ATTESTING, /* ATTEST sent */
    ARB_H_ATTESTED,
    ARB_H_REFUSED, /* the arbiter answered the attestation with a no */
    ARB_H_BROKEN   /* the arbiter broke the A2H ring: stop (state B) */
} arb_host_state_t;

typedef struct arb_host {
    volatile uint8_t *w;
    uint64_t size;
    uint32_t cap;
    uint32_t seq;
    arb_host_state_t state;
    uint32_t attest_verdict;
    uint8_t nonce[ARB_NONCE_LEN];
    int tampered;
    uint32_t tamper_cause;
    uint32_t kill_present, kill_on;
    int have_kill;
    uint32_t interlock_permitted, interlock_reason;
    int have_interlock;
    /* released keys: status per slot (0 = never asked/answered) */
    uint8_t key[ARB_HOST_KEYS][ARB_KEY_LEN];
    uint32_t key_status[ARB_HOST_KEYS];
    /* the last seal answer */
    uint32_t seal_id, seal_verdict;
    uint8_t seal_payee[ARB_PAYEE_LEN];
    int have_seal;
    /* entropy delivered and not yet taken */
    uint8_t entropy[ARB_ENTROPY_MAX];
    uint32_t entropy_n;
    /* latest telemetry per sensor */
    int32_t sensor[ARB_S_COUNT];
    uint32_t sensor_seen; /* bit per sensor */
    uint32_t fb_done_head, fb_done_gen;
    uint32_t received, unknown;
} arb_host_t;

/* Validate the window header; 0 or a negative ARB_E_* reason (state DOWN). */
int arb_host_init(arb_host_t *h, volatile uint8_t *w, uint64_t size);

/* Requests. 0 = queued; -1 = ring full (try again after a poll); -2 = wrong
 * state for this request (for example a key before attestation). */
int arb_host_hello(arb_host_t *h);
int arb_host_attest(arb_host_t *h, const uint8_t measurement[ARB_MEAS_LEN]);
int arb_host_key_request(arb_host_t *h, uint32_t slot);
int arb_host_seal_request(arb_host_t *h, uint32_t id, int64_t amount, const char ccy[4],
                          uint32_t expiry, const uint8_t payee[ARB_PAYEE_LEN]);
int arb_host_entropy_request(arb_host_t *h, uint32_t n);
int arb_host_state_request(arb_host_t *h);
int arb_host_flip(arb_host_t *h, uint32_t head, uint32_t generation);

/* Drain the A2H ring. Returns messages handled. */
uint32_t arb_host_poll(arb_host_t *h);

/* Take a released key: copies it out and wipes this side's copy. Returns the
 * slot's ARB_KEY_* status, or 0 when no answer has arrived yet. */
uint32_t arb_host_take_key(arb_host_t *h, uint32_t slot, uint8_t out[ARB_KEY_LEN]);

/* Take up to max entropy bytes delivered so far (copied out, then wiped). */
uint32_t arb_host_take_entropy(arb_host_t *h, uint8_t *out, uint32_t max);

/* 1 if every bit of `mask` is a switch this device has and it is ON. */
int arb_host_powered(const arb_host_t *h, uint32_t mask);

/* Latest reading of a sensor: 1 and *v set, or 0 if never reported. */
int arb_host_sensor(const arb_host_t *h, uint32_t sensor, int32_t *v);

/* Framebuffer i's pixels and geometry from the header; 0 if absent. */
volatile uint32_t *arb_host_fb(const arb_host_t *h, uint32_t i, uint32_t *width, uint32_t *height,
                               uint32_t *stride_px);

#endif
