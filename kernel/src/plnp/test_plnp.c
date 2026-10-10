/* test_plnp.c — PLNP: received frames must be checked before they are counted.
 *
 * Regression for the old module: plnp_conn_receive() took no bytes at all and
 * counted a frame as received and verified ("In real implementation, would
 * verify incoming frame"); content IDs and keys came from FNV-1a; the header
 * size constant (0x70) was shorter than the header (0x80), so the 5PL vector
 * was neither sent nor covered by the CRC. Each check below fails on that code.
 *
 * Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
 * SPDX-License-Identifier: Apache-2.0
 * Licensed under the Apache License, Version 2.0. See LICENSE at
 * the repository root.
 */
#include <stdio.h>
#include <string.h>
#include "plnp.h"
#include "../tls/hkdf.h"

static int fails = 0;
#define CK(c, m)                                                                                   \
    do {                                                                                           \
        if (c)                                                                                     \
            printf("  ok   %s\n", m);                                                              \
        else {                                                                                     \
            printf("  FAIL %s\n", m);                                                              \
            fails++;                                                                               \
        }                                                                                          \
    } while (0)

static plnp_stack_t s;
static plnp_frame_t f;
static uint8_t wire[PLNP_MAX_FRAME_SIZE], wire2[PLNP_MAX_FRAME_SIZE];

/* SHA3-256("abc"), FIPS 202 example. */
static const uint8_t SHA3_ABC[32] = {
    0x3a, 0x98, 0x5d, 0xa7, 0x4f, 0xe2, 0x25, 0xb2, 0x04, 0x5c, 0x17, 0x2d, 0x6b, 0xd3, 0x90, 0xbd,
    0x85, 0x5f, 0x08, 0x6e, 0x3e, 0x9d, 0x52, 0x5b, 0x46, 0xbf, 0xe2, 0x45, 0x11, 0x43, 0x15, 0x32};

/* Recompute the trailer CRC of a serialized frame, as any attacker can. */
static void fix_crc(uint8_t *w, int n)
{
    uint32_t crc = plnp_crc32(w, (uint32_t) n - PLNP_TRAILER_SIZE);
    w[n - 5] = (uint8_t) crc;
    w[n - 4] = (uint8_t) (crc >> 8);
    w[n - 3] = (uint8_t) (crc >> 16);
    w[n - 2] = (uint8_t) (crc >> 24);
}

int main(void)
{
    printf("=== plnp: frames are verified before they count ===\n");
    uint8_t a[32], b[32], k[32], k2[32];
    plnp_derive_cid((const uint8_t *) "abc", 3, a);
    CK(memcmp(a, SHA3_ABC, 32) == 0, "content ID is SHA3-256 (FIPS 202 KAT)");
    plnp_derive_cid((const uint8_t *) "node-b", 6, b);
    plnp_derive_key(PLNP_KEY_K2, (const uint8_t *) "shared seed", 11, k);
    plnp_derive_key(PLNP_KEY_K3, (const uint8_t *) "shared seed", 11, k2);
    CK(memcmp(k, k2, 32) != 0, "key index separates derived keys");

    plnp_init(&s);
    int32_t A = plnp_conn_create(&s, a, b, PLNP_KEY_K1);
    int32_t B = plnp_conn_create(&s, b, a, PLNP_KEY_K1);

    CK(plnp_conn_receive(&s, (uint32_t) B, (const uint8_t *) "garbage", 7, &f) < 0 &&
           s.connections[B].frames_received == 0,
       "garbage is not counted as a received frame");

    const char *msg = "hello over plnp";
    int n = plnp_conn_send(&s, (uint32_t) A, (const uint8_t *) msg, (uint32_t) strlen(msg),
                           PLNP_PHASE_TRUE, wire, sizeof wire);
    CK(n == (int) (128 + strlen(msg) + PLNP_TRAILER_SIZE), "send serializes a 128-byte header");
    memcpy(wire2, wire, (size_t) n);
    wire2[130] ^= 1;
    CK(plnp_conn_receive(&s, (uint32_t) B, wire2, (uint32_t) n, &f) < 0,
       "corrupted frame rejected");
    int r = plnp_conn_receive(&s, (uint32_t) B, wire, (uint32_t) n, &f);
    CK(r == (int) strlen(msg) && memcmp(f.payload, msg, strlen(msg)) == 0 &&
           s.connections[B].frames_received == 1,
       "valid frame accepted once");
    CK(plnp_conn_receive(&s, (uint32_t) B, wire, (uint32_t) n, &f) == -5 &&
           s.connections[B].frames_received == 1,
       "replayed frame rejected (sequence)");
    CK(plnp_conn_receive(&s, (uint32_t) A, wire, (uint32_t) n, &f) == -4,
       "frame for other content IDs rejected");

    /* The 5PL vector at 0x70 is on the wire and under the CRC. */
    plnp_frame_init(&f, PLNP_KEY_K1, PLNP_PHASE_TRUE);
    plnp_frame_set_5pl(&f, PLNP_5PL_WITNESS);
    plnp_frame_seal(&f);
    n = plnp_frame_serialize(&f, wire, sizeof wire);
    CK(wire[0x70] == PLNP_5PL_WITNESS, "5PL vector serialized at 0x70");
    wire[0x70] ^= PLNP_5PL_CLAIM;
    CK(plnp_frame_deserialize(&f, wire, (uint32_t) n) > 0 && plnp_frame_verify(&f) == -2,
       "5PL vector covered by the CRC");

    /* Authenticated connections. */
    int32_t A2 = plnp_conn_create(&s, a, b, PLNP_KEY_K2);
    int32_t B2 = plnp_conn_create(&s, b, a, PLNP_KEY_K2);
    int32_t U = plnp_conn_create(&s, b, a, PLNP_KEY_K2); /* no key */
    int32_t W = plnp_conn_create(&s, b, a, PLNP_KEY_K2); /* wrong key */
    plnp_conn_set_key(&s, (uint32_t) A2, k);
    plnp_conn_set_key(&s, (uint32_t) B2, k);
    plnp_conn_set_key(&s, (uint32_t) W, k2);
    n = plnp_conn_send(&s, (uint32_t) A2, (const uint8_t *) msg, (uint32_t) strlen(msg),
                       PLNP_PHASE_TRUE, wire, sizeof wire);
    CK(n == (int) (128 + strlen(msg) + PLNP_MAC_SIZE + PLNP_TRAILER_SIZE),
       "AUTH frame carries a tag");
    uint8_t mac[32];
    hmac_sha256(k, 32, wire, 128 + (uint32_t) strlen(msg), mac);
    CK(memcmp(mac, wire + 128 + strlen(msg), 32) == 0,
       "tag is HMAC-SHA256 (matches RFC 4231 impl)");

    memcpy(wire2, wire, (size_t) n);
    wire2[128] ^= 1;
    fix_crc(wire2, n);
    CK(plnp_conn_receive(&s, (uint32_t) B2, wire2, (uint32_t) n, &f) == -3,
       "payload forged with a recomputed CRC is rejected");
    CK(plnp_conn_receive(&s, (uint32_t) U, wire, (uint32_t) n, &f) == -3,
       "receiver without a key cannot accept an AUTH frame");
    CK(plnp_conn_receive(&s, (uint32_t) W, wire, (uint32_t) n, &f) == -3, "wrong key rejected");

    /* Strip the tag and the AUTH flag, recompute the CRC: keyed side refuses. */
    plnp_frame_deserialize(&f, wire, (uint32_t) n);
    f.header.flags &= (uint8_t) ~PLNP_FLAG_AUTH;
    plnp_frame_seal(&f);
    int n2 = plnp_frame_serialize(&f, wire2, sizeof wire2);
    CK(plnp_conn_receive(&s, (uint32_t) B2, wire2, (uint32_t) n2, &f) == -3,
       "downgrade to an unauthenticated frame refused");
    CK(s.connections[B2].frames_received == 0, "nothing counted yet");

    r = plnp_conn_receive(&s, (uint32_t) B2, wire, (uint32_t) n, &f);
    CK(r == (int) strlen(msg) && s.connections[B2].frames_authenticated == 1,
       "genuine AUTH frame accepted and counted as authenticated");

    printf("=== %s (%d failure(s)) ===\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
