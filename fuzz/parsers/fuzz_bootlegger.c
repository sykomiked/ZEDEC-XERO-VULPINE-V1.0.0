/* Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0 */
/* fuzz_bootlegger.c — the Bootlegger P2P handshake and message opener
 * (bootlegger/bootlegger.c): the transcript-bound ML-DSA-65 / ML-KEM-768
 * handshake (HELLO -> REPLY -> FINISH) and the sealed message format.
 *
 * Byte 0 (mod 4) picks:
 *   0  HELLO: a HELLO built from the input (version, nonce, identity, ek)
 *      given to bootlegger_hs_respond. Property: respond never leaves the
 *      connection authenticated, and a failure leaves no session.
 *   1  message: a 32-byte session key, then a message; bootlegger_open_msg
 *      on the raw bytes, and a seal -> open round trip of the same bytes as
 *      a payload must return them unchanged.
 *   2  REPLY: a genuine REPLY from a real handshake, XORed field by field
 *      with input bytes, given to bootlegger_hs_finish. Property: it
 *      succeeds exactly when the mask is all zero, and only then is the
 *      connection authenticated.
 *   3  FINISH: the same for a genuine FINISH given to bootlegger_hs_accept.
 *
 * The genuine handshake is built once from fixed seeds and replayed from a
 * snapshot for every input, so runs are deterministic. */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "bootlegger.h"
#include "fuzz_in.h"

static bootlegger_conn_t g_conn;
static uint8_t g_pl[65536 + 64], g_sealed[65536 + 64];

/* snapshot of a real handshake: A after initiate, B after respond, A after
 * finish (for FINISH), plus the messages */
static int g_ready;
static bootlegger_identity_t g_ia, g_ib;
static bootlegger_hs_t g_hs_a, g_hs_b;
static bootlegger_conn_t g_conn_a, g_conn_b;
static bootlegger_replay_cache_t g_rc_a, g_rc_b;
static bootlegger_hello_t g_hello;
static bootlegger_reply_t g_reply;
static bootlegger_finish_t g_finish;

static void rnd_fill(uint8_t out[BOOTLEGGER_RAND_BYTES], uint8_t tag)
{
    for (int i = 0; i < BOOTLEGGER_RAND_BYTES; i++) out[i] = (uint8_t) (tag * 31u + (unsigned) i * 7u + 1u);
}

static void setup(void)
{
    uint8_t seed[32], r[BOOTLEGGER_RAND_BYTES];
    memset(seed, 0xA1, sizeof seed);
    if (bootlegger_identity_init(&g_ia, seed)) abort();
    memset(seed, 0xB2, sizeof seed);
    if (bootlegger_identity_init(&g_ib, seed)) abort();
    bootlegger_replay_init(&g_rc_a);
    bootlegger_replay_init(&g_rc_b);
    memset(&g_conn_a, 0, sizeof g_conn_a);
    memset(&g_conn_b, 0, sizeof g_conn_b);
    g_conn_a.connected = g_conn_b.connected = 1;
    rnd_fill(r, 1);
    if (bootlegger_hs_initiate(&g_hs_a, &g_ia, &g_rc_a, r, &g_hello)) abort();
    rnd_fill(r, 2);
    if (bootlegger_hs_respond(&g_hs_b, &g_conn_b, &g_ib, &g_rc_b, &g_hello, r, &g_reply)) abort();
    /* a throwaway finish produces a genuine FINISH; the snapshot keeps the
     * pre-finish A state for mode 2 */
    bootlegger_hs_t hs_a = g_hs_a;
    bootlegger_conn_t conn_a = g_conn_a;
    bootlegger_replay_cache_t rc_a = g_rc_a;
    rnd_fill(r, 3);
    if (bootlegger_hs_finish(&hs_a, &conn_a, &g_ia, &rc_a, &g_reply, r, &g_finish)) abort();
    if (!conn_a.authenticated) abort();
    g_ready = 1;
}

/* XOR `n` bytes of input into `p`; returns 1 if any byte changed */
static int mutate(fz_in *in, void *p, size_t n)
{
    uint8_t m[4096];
    uint8_t *q = (uint8_t *) p;
    int changed = 0;
    while (n) {
        size_t k = n < sizeof m ? n : sizeof m;
        fz_bytes(in, m, k);
        for (size_t i = 0; i < k; i++) {
            changed |= m[i] != 0;
            q[i] ^= m[i];
        }
        q += k;
        n -= k;
    }
    return changed;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (size < 1 || size > 65536u) return 0;
    if (!g_ready) setup();
    fz_in in;
    fz_init(&in, data, size);
    uint8_t mode = (uint8_t) (fz_u8(&in) % 4u);
    memset(&g_conn, 0, sizeof g_conn);

    if (mode == 0) {
        bootlegger_hello_t h;
        bootlegger_hs_t hs;
        bootlegger_replay_cache_t rc;
        bootlegger_reply_t out;
        uint8_t r[BOOTLEGGER_RAND_BYTES];
        bootlegger_replay_init(&rc);
        h.version = fz_u16(&in);
        fz_bytes(&in, h.nonce, sizeof h.nonce);
        fz_bytes(&in, h.identity, sizeof h.identity);
        fz_bytes(&in, h.ek, sizeof h.ek);
        if (fz_u8(&in) & 1) bootlegger_pin_peer(&g_conn, g_ia.pk);
        g_conn.connected = 1;
        rnd_fill(r, 9);
        int rc0 = bootlegger_hs_respond(&hs, &g_conn, &g_ib, &rc, &h, r, &out);
        if (g_conn.authenticated) abort(); /* only accept authenticates B */
        if (rc0 != 0 && g_conn.has_session) abort();
    } else if (mode == 1) {
        uint8_t key[32];
        fz_bytes(&in, key, 32);
        memcpy(g_conn.rx_key, key, 32);
        memcpy(g_conn.tx_key, key, 32);
        g_conn.connected = 1;
        g_conn.authenticated = 1;
        g_conn.has_session = 1;
        uint8_t *msg = fz_dup(in.p, in.n);
        uint32_t len = (uint32_t) in.n;
        bootlegger_msg_type_t type;
        int r = bootlegger_open_msg(&g_conn, msg, len, &type, g_pl, sizeof g_pl);
        if (r > 0 && (uint32_t) r + BOOTLEGGER_MSG_HDR + BOOTLEGGER_TAG_SIZE != len) abort();
        /* round trip: seal the input as a payload, then open it */
        if (len <= 0xFFFFu) {
            g_conn.tx_seq = 0;
            g_conn.rx_seq = 0;
            int w = bootlegger_seal_msg(&g_conn, BOOTLEGGER_MSG_CHAT, msg, (uint16_t) len, g_sealed,
                                        sizeof g_sealed);
            if (w != (int) (len + BOOTLEGGER_MSG_HDR + BOOTLEGGER_TAG_SIZE)) abort();
            int o = bootlegger_open_msg(&g_conn, g_sealed, (uint32_t) w, &type, g_pl, sizeof g_pl);
            if (o != (int) len || type != BOOTLEGGER_MSG_CHAT || memcmp(g_pl, msg, len) != 0)
                abort();
        }
        free(msg);
    } else if (mode == 2) {
        bootlegger_hs_t hs = g_hs_a;
        bootlegger_conn_t conn = g_conn_a;
        bootlegger_replay_cache_t rc = g_rc_a;
        bootlegger_reply_t rp = g_reply;
        bootlegger_finish_t out;
        uint8_t r[BOOTLEGGER_RAND_BYTES];
        int changed = 0;
        uint16_t vm = fz_u16(&in);
        rp.hello.version ^= vm;
        changed |= vm != 0;
        changed |= mutate(&in, rp.hello.nonce, sizeof rp.hello.nonce);
        changed |= mutate(&in, rp.hello.identity, sizeof rp.hello.identity);
        changed |= mutate(&in, rp.hello.ek, sizeof rp.hello.ek);
        changed |= mutate(&in, rp.ct, sizeof rp.ct);
        changed |= mutate(&in, rp.sig, sizeof rp.sig);
        rnd_fill(r, 3);
        int rc0 = bootlegger_hs_finish(&hs, &conn, &g_ia, &rc, &rp, r, &out);
        if ((rc0 == 0) == changed) abort(); /* forged REPLY accepted, or genuine refused */
        if ((rc0 == 0) != (conn.authenticated == 1)) abort();
    } else {
        bootlegger_hs_t hs = g_hs_b;
        bootlegger_conn_t conn = g_conn_b;
        bootlegger_finish_t fn = g_finish;
        int changed = 0;
        changed |= mutate(&in, fn.ct, sizeof fn.ct);
        changed |= mutate(&in, fn.sig, sizeof fn.sig);
        changed |= mutate(&in, fn.confirm, sizeof fn.confirm);
        int rc0 = bootlegger_hs_accept(&hs, &conn, &fn);
        if ((rc0 == 0) == changed) abort(); /* forged FINISH accepted, or genuine refused */
        if ((rc0 == 0) != (conn.authenticated == 1)) abort();
    }
    return 0;
}
