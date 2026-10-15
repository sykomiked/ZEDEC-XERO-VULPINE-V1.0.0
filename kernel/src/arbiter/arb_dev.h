/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC */
/* SPDX-License-Identifier: Apache-2.0 */
/* arb_dev.h — the arbiter's end of the mailbox (the logic its firmware runs).
 *
 * The arbiter boots first, verifies the host's firmware and ZXV image, keeps
 * that image's measurement, and holds the host in reset until it is good
 * (OEM_INSTITUTIONAL.md section 6.1). This is what it does once the host runs:
 *
 *   ATTEST     The host answers the arbiter's current nonce with the
 *              measurement of the image it is running. A match is the
 *              attested state; a mismatch or a stale nonce is not. Nothing
 *              is released before attestation (6.1 step 4). This proves the
 *              host is the image the arbiter verified and answered this boot's
 *              nonce; the security comes from the arbiter owning reset and
 *              the boot flash, not from a secret on the host.
 *   KEYS       Up to ARB_KEY_SLOTS key-encryption keys (the drives' Opal
 *              authentication secrets, 6.3) live only here. One is released
 *              per request, only while attested and not tampered.
 *   TAMPER     A tamper clears every key FIRST, then latches, then tells the
 *              host (6.3: the KEK is gone before anyone is told). The latch
 *              survives until the arbiter is re-provisioned; nothing on the
 *              host side can clear it.
 *   KILL       Switch positions are reported as they change.
 *   SEAL       A payment approval: the arbiter shows amount, currency and
 *              payee on the seal display (a callback; the panel is wired to
 *              the arbiter alone) and waits for the seal button
 *              (mobile/MOBILE_COMMON.md section 4). One seal at a time; a
 *              seal past its expiry is refused, never approved.
 *   TELEMETRY  Power and temperature readings, pushed (section 5 of
 *              OS_HARDWARE_INTERFACE.md).
 *   INTERLOCK  Whether the host may start, and why not (SERVER_COMMON 2.3).
 *   ENTROPY    Bytes from the arbiter's entropy source (section 6).
 *   FRAMEBUF   Flips of the three panel framebuffers are acknowledged.
 */
#ifndef ZXV_ARB_DEV_H
#define ZXV_ARB_DEV_H

#include "arb_proto.h"

#define ARB_KEY_SLOTS 4u

typedef struct arb_seal_view {
    uint32_t id;
    int64_t amount; /* minor units */
    char ccy[4];
    uint32_t expiry; /* arbiter clock ticks */
    uint8_t payee[ARB_PAYEE_LEN];
} arb_seal_view_t;

typedef struct arb_dev {
    volatile uint8_t *w;
    uint32_t cap;
    uint32_t seq;
    uint32_t now; /* arbiter clock, ticks */
    /* measurement of the image the arbiter verified before releasing reset */
    uint8_t measurement[ARB_MEAS_LEN];
    uint8_t nonce[ARB_NONCE_LEN];
    int nonce_live; /* the nonce has been sent and not yet used */
    int attested;
    int tampered;
    uint32_t tamper_cause;
    uint8_t kek[ARB_KEY_SLOTS][ARB_KEY_LEN];
    uint8_t kek_present[ARB_KEY_SLOTS];
    uint32_t kill_present, kill_on;
    uint32_t interlock_permitted, interlock_reason;
    int seal_pending;
    arb_seal_view_t seal;
    uint32_t fb_head, fb_gen;
    uint32_t broken;  /* 1 once the host broke the H2A ring (state B) */
    uint32_t dropped; /* replies that found A2H full */
    /* Hardware hooks. entropy fills n bytes from the arbiter's source;
     * seal_show draws on the seal display (0 clears it). */
    void (*entropy)(void *ctx, uint8_t *out, uint32_t n);
    void (*seal_show)(void *ctx, const arb_seal_view_t *v);
    void *ctx;
} arb_dev_t;

/* Write the window header and start. The measurement is the arbiter's own
 * record of the verified image. 0 or a negative ARB_E_* reason. */
int arb_dev_init(arb_dev_t *d, volatile uint8_t *w, uint64_t size, uint32_t ring_cap,
                 uint32_t fb_count, uint32_t fb_width, uint32_t fb_height,
                 const uint8_t measurement[ARB_MEAS_LEN],
                 void (*entropy)(void *, uint8_t *, uint32_t), void *ctx);

/* Provision a key slot (factory or owner enrolment). 0, or -1 for a bad slot
 * or a tampered arbiter, which takes no new keys. */
int arb_dev_set_key(arb_dev_t *d, uint32_t slot, const uint8_t key[ARB_KEY_LEN]);

/* Process every pending host message. Returns how many were handled. */
uint32_t arb_dev_poll(arb_dev_t *d);

/* Advance the arbiter clock; a pending seal past its expiry is answered
 * EXPIRED and cleared from the display. */
void arb_dev_tick(arb_dev_t *d, uint32_t now);

/* A tamper sensor fired. */
void arb_dev_tamper(arb_dev_t *d, uint32_t cause);

/* Kill switch positions changed (present = which switches this device has). */
void arb_dev_kill(arb_dev_t *d, uint32_t present, uint32_t on);

/* The seal button: approve 1, deny 0. Returns 0 if a seal was pending. */
int arb_dev_seal_button(arb_dev_t *d, int approve);

/* Push up to ARB_TELEM_MAX readings. 0 queued, -1 full or bad count. */
int arb_dev_telemetry(arb_dev_t *d, const uint16_t *sensor, const int32_t *value, uint32_t n);

/* The start interlock changed. */
void arb_dev_interlock(arb_dev_t *d, uint32_t permitted, uint32_t reason);

#endif
